/* trace/main.cc
 *
 * Copyright (C) 2019 GOU Lingfeng
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


//#include "gapr/plugin-helper.hh"
//#include "gapr/program.hh"

#include "gapr/edge-model.hh"
#include "gapr/cube-builder.hh"
#include "compute.hh"
#include "gapr/utility.hh"
#include "gapr/cube-loader.hh"
#include "gapr/affine-xform.hh"
#include "gapr/cube.hh"
#include "utils.hh"

//#include "gapr/utility.hh"
#include "gapr/parser.hh"
//#include "gapr/mem-file.hh"
#include "gapr/str-glue.hh"
#include "gapr/fiber.hh"
#include "gapr/streambuf.hh"
#include "gapr/trace-api.hh"
#include "gapr/ask-secret.hh"
#include "gapr/swc-helper.hh"

//#include <memory>
#include <cmath>
#include <optional>
#include <queue>
#include <fstream>
//#include <unordered_set>
#include <random>
#include <chrono>
#include <thread>
#include <filesystem>
#include <algorithm>

#include <boost/asio/thread_pool.hpp>
#include <boost/asio/strand.hpp>
//#include <boost/asio/deadline_timer.hpp>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include <getopt.h>

#include "config.hh"

#include "../corelib/dup/serialize-delta.hh"

namespace ba=boost::asio;
namespace bs=boost::system;

using gapr::client_end;
using resolver=ba::ip::tcp::resolver;


namespace {
	class Tracer {
		public:
			struct Args {
				std::string user{};
				std::string passwd{};
				std::string host{};
				unsigned short port{0};
				std::string group{};
				std::string evaluator{};
				std::string sample{};
				bool reset{false};
				bool reset_all{false};
				std::vector<std::string> reset_diffs;
				bool evaluate{false};
				unsigned int jobs{1};
				unsigned int maxiter{0};
				double range0{0.0};
				double range1{INFINITY};
				bool benchmark{false};
				std::filesystem::path from_swc{};
				bool from_swc_fin{false};
			};
			explicit Tracer(Args&& args);
			~Tracer();
			Tracer(const Tracer&) =delete;
			Tracer& operator=(const Tracer&) =delete;

			int run();

		private:
			ba::io_context _io_ctx{1};
			ba::thread_pool _thr_pool{1+std::thread::hardware_concurrency()};
			ba::ssl::context _ssl_ctx{ba::ssl::context::tls};

			using input_strand=ba::strand<ba::thread_pool::executor_type>;
			input_strand _inp_strand{_thr_pool.get_executor()};
			ba::deadline_timer _timer{_io_ctx};
			std::atomic<bool> _cancel_flag;


			std::shared_ptr<gapr::cube_builder> _cube_builder{};

			Args _args;
			std::optional<resolver> _resolver;
			resolver::results_type _addrs{};
			resolver::endpoint_type _addr{ba::ip::address{}, 0};
			client_end _cur_conn{};
			std::string _srv_info;
			std::string _gecos;
			std::string _data_secret;
			uint64_t _latest_commit;
			std::vector<gapr::cube_info> _cube_infos;

			gapr::edge_model _model;
			gapr::commit_history _hist;
			gapr::trace_api api;

			std::size_t _global_ch{0};
			gapr::cube _global_cube;

			std::size_t _closeup_ch{0};
			gapr::cube _closeup_cube;
			std::array<unsigned int, 3> _closeup_offset;

			enum class TaskStage {
				Queued,
				Running,
				Skipped,
				Empty,
				Finished,
			};
			struct TaskInfo {
				int vote_total;
				int vote_ds;
				union {
					std::array<unsigned int, 3> offset;
					std::size_t next_free;
				};
				bool skip;
				TaskStage stage;
			};
			struct Compare {
				std::vector<TaskInfo>& store;
				double get_priority(std::size_t a) const {
					auto& aa=store[a];
					return std::ceil(std::log(aa.vote_total))*1e10+aa.offset[2];
				}
				bool operator()(std::size_t a, std::size_t b) const noexcept {
					return get_priority(a)<get_priority(b);
				}
			};
			struct Hash {
				std::size_t operator()(std::array<unsigned int, 3> v) const noexcept {
					std::hash<unsigned int> h{};
					return h(v[0])^h(v[1]+11)^h(v[2]+23);
				}
			};
			std::vector<TaskInfo> _pq_store;
			std::priority_queue<std::size_t, std::vector<std::size_t>, Compare> _pq_queue{Compare{_pq_store}};
			std::unordered_map<std::array<unsigned int, 3>, std::size_t, Hash> _pq_map;
			std::size_t _pq_store_free{0};
			void recycle(std::size_t i) {
				_pq_store[i].next_free=_pq_store_free;
				_pq_store_free=i+1;
			}
			void vote(const std::array<double, 3>& pos, const std::array<double, 3>& dir, int w0, int w1);
			void vote_helper(const std::array<unsigned, 3>& offset, int w0, int w1);
			bool _model_updated{true};
			void vote_unfinished();
			std::array<unsigned int, 3> to_offset(const std::array<double, 3>& pos);

			int run_impl(gapr::fiber_ctx& ctx);
			int run_impl2(gapr::fiber_ctx& ctx);
			int run_sample(gapr::fiber_ctx& ctx);
			int run_reset(gapr::fiber_ctx& ctx);
			int run_evaluate(gapr::fiber_ctx& ctx);
			int run_remove_edges(gapr::fiber_ctx& ctx);
			int run_benchmark(gapr::fiber_ctx& ctx);
			int run_import(gapr::fiber_ctx& ctx);
			void get_closeup(const std::array<unsigned int, 3>& offset, const std::array<double, 3>& center, gapr::fiber_ctx& ctx);
			void select(gapr::fiber_ctx& ctx);
			void prepare(gapr::fiber_ctx& ctx);
			std::string get_passwd(gapr::fiber_ctx& ctx, std::error_code& ec);
			bool get_retry(gapr::fiber_ctx& ctx);
			void start_cube_builder();
			void cube_finished(std::error_code ec, int progr);
			void get_cubes();
			bool load_model_state(gapr::fiber_ctx& ctx, client_end msg);
			bool load_commits(gapr::fiber_ctx& ctx, client_end msg, uint64_t upto);
			template<gapr::delta_type Typ>
				bool model_prepare(gapr::node_id nid0, gapr::delta<Typ>&& delta) {
					auto ex1=_thr_pool.get_executor();
					assert(ex1.running_in_this_thread());
					(void)ex1;
					gapr::print("model prepare2");

					gapr::edge_model::loader loader{_model};
					return loader.load<true>(nid0, std::move(delta));
				}
			gapr::node_id prev_nid0{};
			bool model_apply(bool last);
			//download_cube...;
			std::array<double, 3> get_center(const std::array<unsigned int, 3>& offset) {
				auto& info=_cube_infos[_closeup_ch-1];
				double x=offset[0]+info.cube_sizes[0]*3/2;
				double y=offset[1]+info.cube_sizes[1]*3/2;
				double z=offset[2]+info.cube_sizes[2]*3/2;
				return info.xform.from_offset_f({x, y, z});
			}
			enum class SubmitRes {
				Deny,
				Accept,
				Retry
			};
			template<gapr::delta_type Typ>
				std::pair<SubmitRes, std::string> submit_commit(gapr::fiber_ctx& ctx, gapr::delta<Typ>&& delta) {
					gapr::promise<gapr::mem_file> prom{};
					gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
					auto ex1=_thr_pool.get_executor();
					ba::post(ex1, [&delta,prom=std::move(prom)]() mutable {
						gapr::mem_file payload;
						try {
							if(cannolize(delta)<0)
								throw;
							payload=serialize(delta);
							std::move(prom).set(std::move(payload));
						} catch(const std::runtime_error& e) {
							return unlikely(std::move(prom), std::current_exception());
						}
					});
					auto payload=std::move(fib2).async_wait(gapr::yield{ctx});

					auto msg=_cur_conn;
					gapr::fiber fib{ctx.get_executor(), api.commit(msg, Typ, std::move(payload), _hist.body_count(), _hist.tail_tip())};
					auto res=std::move(fib).async_wait(gapr::yield{ctx});
#if 0
					if(err)
						return {SubmitRes::Deny, "msg"};
#endif
					auto [nid0, cmt_id, upd]=res;
					gapr::print("commit res: ", nid0, cmt_id, upd);
					auto r=load_commits(ctx, _cur_conn, upd);
					if(!r)
						return {SubmitRes::Deny, "err load"};
					if(!nid0) {
						if(cmt_id) {
							return {SubmitRes::Retry, {}};
						} else {
							return {SubmitRes::Deny, {}};
						}
					}
					gapr::promise<bool> prom2{};
					gapr::fiber fib3{ctx.get_executor(), prom2.get_future()};
					auto ex2=_thr_pool.get_executor();
					ba::post(ex2, [this,nid0=nid0,prom2=std::move(prom2),&delta]() mutable {
						auto r=model_prepare(gapr::node_id{nid0}, std::move(delta));
						return std::move(prom2).set(r);
					});
					// XXX join edges
					if(!std::move(fib3).async_wait(gapr::yield{ctx}))
						return {SubmitRes::Deny, "err load2"};
					gapr::print("prepare ok");
					_hist.add_tail(cmt_id);
					return {SubmitRes::Accept, {}};
				}
			std::pair<std::size_t, std::size_t> select_chan() {
				std::size_t first_c{0}, first_g{0};
				for(unsigned int i=0; i<_cube_infos.size(); i++) {
					// XXX in thread_pool
					if(!_cube_infos[i].xform.update_direction_inv())
						gapr::report("no inverse");
					_cube_infos[i].xform.update_resolution();
					if(_cube_infos[i].is_pattern()) {
						if(!first_c || true)
							first_c=i+1;
					} else {
						if(!first_g)
							first_g=i+1;
					}
				}
				if(!first_c)
					throw gapr::reported_error{"no high-definition channel found"};
				if(!first_g)
					throw gapr::reported_error{"no down-sampled channel found"};
				return {first_c, first_g};
			}
	};
}

Tracer::Tracer(Args&& args): _args{std::move(args)}
{
}

Tracer::~Tracer() {
#if 0
	auto loadThread=_cube_builder;
	if(loadThread) {
		if(loadThread->isRunning()) {
			gapr::print("stop load thread");
			loadThread->stop();
			if(!loadThread->wait()) {
				loadThread->terminate();
			}
		}
		delete loadThread;
	}
#endif
}

int Tracer::run() {
	gapr::fiber fib{_io_ctx.get_executor(), [this](gapr::fiber_ctx& ctx) {
		try {
			if(!_args.from_swc.empty())
				return run_import(ctx);
			if(_args.benchmark)
				return run_benchmark(ctx);
			if(_args.evaluate)
				return run_evaluate(ctx);
			if(_args.reset)
				return run_reset(ctx);
			if(!_args.sample.empty())
				return run_sample(ctx);
			switch(1) {
				default:
					break;
				case 2:
					return run_impl2(ctx);
			}
			return run_impl(ctx);
		} catch(const gapr::reported_error& e) {
			gapr::str_glue msg{"fatal: ", e.what(), '\n'};
			std::cerr<<msg.str();
			return -1;
		}
	}};
	auto c=_io_ctx.run();
	gapr::print("stopped normally, ", c, " events.");
	_thr_pool.join();
	return 0;
}

std::string Tracer::get_passwd(gapr::fiber_ctx& ctx, std::error_code& ec) {
	gapr::str_glue msg{_args.user, '@', _args.host, ':', _args.port, "'s password: "};
	gapr::ask_secret asks{};
	std::cerr<<msg.str();

	gapr::promise<std::string> prom{};
	auto fut=prom.get_future();
	ba::post(_inp_strand, [/*this,wg=make_work_guard(_io_ctx)*/asks=std::move(asks),prom=std::move(prom)]() mutable {
		std::string pw;
		try {
			pw=asks.get();
			std::move(prom).set(std::move(pw));
		} catch(const std::system_error& e) {
			unlikely(std::move(prom), e.code());
		} catch(const std::runtime_error& e) {
			unlikely(std::move(prom), std::current_exception());
		}
	});
	gapr::fiber fib{ctx.get_executor(), std::move(fut)};
	auto res=std::move(fib).async_wait(gapr::yield{ctx});
#if 0
	if(!res) try {
		gapr::str_glue err{"\nunable to read password: ", res.error().message()};
		throw gapr::reported_error{err.str()};
	} catch(const std::runtime_error& e) {
		gapr::str_glue err{"\nunable to read password: ", e.what()};
		throw gapr::reported_error{err.str()};
	}
#endif
	std::cerr<<'\n';
	return res;
}

bool Tracer::get_retry(gapr::fiber_ctx& ctx) {
	std::cerr<<"retry? (y/n): ";
	gapr::promise<bool> prom{};
	auto fut=prom.get_future();
	ba::post(_inp_strand, [/*this,wg=make_work_guard(_io_ctx)*/prom=std::move(prom)]() mutable {
		try {
			//return std::move(prom).set(true);
			std::string line;
			while(std::getline(std::cin, line)) {
				if(line.empty())
					continue;
				if(line[0]=='y') {
					return std::move(prom).set(true);
				} else if(line[0]=='n') {
					return std::move(prom).set(false);
				}
			}
			unlikely(std::move(prom), std::make_error_code(std::io_errc::stream));
		} catch(const std::system_error& e) {
			unlikely(std::move(prom), e.code());
		} catch(const std::runtime_error& e) {
			unlikely(std::move(prom), std::current_exception());
		}
	});
	gapr::fiber fib{ctx.get_executor(), std::move(fut)};
	auto res=std::move(fib).async_wait(gapr::yield{ctx});
#if 0
	if(!res) try {
		gapr::str_glue err{"\nunable to read password: ", res.error().message()};
		throw gapr::reported_error{err.str()};
	} catch(const std::runtime_error& e) {
		gapr::str_glue err{"\nunable to read password: ", e.what()};
		throw gapr::reported_error{err.str()};
	}
#endif
	std::cerr<<'\n';
	return res;
}

void Tracer::select(gapr::fiber_ctx& ctx) {
	auto msg=_cur_conn;
	gapr::fiber fib{ctx.get_executor(), api.select(msg, _args.group)};
	auto res=std::move(fib).async_wait(gapr::yield{ctx});
#if 0
	if(!res) try {
		gapr::str_glue err{"select error: network failure: ", res.error().message()};
		throw gapr::reported_error{err.str()};
	} catch(const std::runtime_error& e) {
		gapr::str_glue err{"select error: ", e.what()};
		throw gapr::reported_error{err.str()};
	}
#endif
	_data_secret=std::move(res.secret);
	_latest_commit=res.commit_num;
#if 0
	if(...==MAX)
		err;
#endif
}
void Tracer::prepare(gapr::fiber_ctx& ctx) {
	std::error_code ec;
	bs::error_code ec0;
	auto addr=ba::ip::make_address(_args.host, ec0);
	if(ec0) {
		if(!_resolver)
			_resolver.emplace(_io_ctx);
		_addrs=_resolver->async_resolve(_args.host, "0", gapr::yield{ctx, ec});
		if(ec) {
			gapr::str_glue err{"unable to look up `", _args.host, "': ", ec.message()};
			throw gapr::reported_error{err.str()};
		}
		assert(!_addrs.empty());
	} else {
		_addr.address(addr);
		_addr.port(_args.port);
	}

	if(_args.passwd.empty()) {
		do {
			_args.passwd=get_passwd(ctx, ec);
			if(ec) {
				gapr::str_glue err{"unable to read password: ", ec.message()};
				throw gapr::reported_error{err.str()};
			}
		} while(_args.passwd.empty());
	}

	ba::ip::tcp::socket sock{_io_ctx};
	if(!_addr.port()) {
		auto it=_addrs.begin();
		do {
			resolver::endpoint_type addr{it->endpoint().address(), _args.port};
			sock.async_connect(addr, gapr::yield{ctx, ec});
			if(!ec)
				break;
			gapr::str_glue err{"unable to connect to [", addr, "]: ", ec.message(), '\n'};
			std::cerr<<err.str();
			++it;
		} while(it!=_addrs.end());
		if(it==_addrs.end()) {
			gapr::str_glue err{"unable to connect to `", _args.host, ':', _args.port, "'"};
			throw gapr::reported_error{err.str()};
		}
		_addr.address(it->endpoint().address());
		_addr.port(_args.port);
	} else {
		sock.async_connect(_addr, gapr::yield{ctx, ec});
		if(ec) {
			gapr::str_glue err{"unable to connect to [", _addr, "]: ", ec.message()};
			throw gapr::reported_error{err.str()};
		}
	}
	_cur_conn=client_end{std::move(sock), _ssl_ctx};


	{
		gapr::fiber fib{ctx.get_executor(), api.handshake(_cur_conn)};
		auto res=std::move(fib).async_wait(gapr::yield{ctx});
#if 0
		if(!res) try {
			gapr::str_glue err{"unable to handshake: ", res.error().message()};
			throw gapr::reported_error{err.str()};
		} catch(const std::runtime_error& e) {
			gapr::str_glue err{"unable to handshake: ", e.what()};
			throw gapr::reported_error{err.str()};
		}
#endif
		_srv_info=std::move(res.banner);
	}

	do {
		auto msg=_cur_conn;
		gapr::fiber fib{ctx.get_executor(), api.login(msg, _args.user, _args.passwd)};
		auto res=std::move(fib).async_wait(gapr::yield{ctx});
#if 0
		if(!res) try {
			gapr::str_glue err{"login error: network failure: ", res.error().message()};
			throw gapr::reported_error{err.str()};
		} catch(const std::runtime_error& e) {
			gapr::str_glue err{"login error: ", e.what()};
			throw gapr::reported_error{err.str()};
		}
#endif
		if(res.tier<gapr::tier::locked) {
			_gecos=std::move(res.gecos);
			break;
		}
		if(res.tier==gapr::tier::locked)
			throw gapr::reported_error{"login error: user locked"};
		gapr::str_glue err{"login failure: ", res.gecos, '\n'};
		std::cerr<<err.str();
		do {
			_args.passwd=get_passwd(ctx, ec);
			if(ec) {
				gapr::str_glue err{"unable to read password: ", ec.message()};
				throw gapr::reported_error{err.str()};
			}
		} while(_args.passwd.empty());
	} while(true);

	select(ctx);

	{
		auto msg=_cur_conn;
		gapr::fiber fib{ctx.get_executor(), api.get_catalog(msg)};
		auto res=std::move(fib).async_wait(gapr::yield{ctx});
#if 0
		if(!res) try {
			gapr::str_glue err{"unable to load catalog: ", ec.message()};
			throw gapr::reported_error{err.str()};
		} catch(const std::runtime_error& e) {
			gapr::str_glue err{"unable to load catalog: ", e.what()};
			throw gapr::reported_error{err.str()};
		}
#endif
		auto f=gapr::make_streambuf(std::move(res.file));
		std::istream str{f.get()};
		std::vector<gapr::mesh_info> mesh_infos;
		auto base_url="https://"+_args.host+":"+std::to_string(_args.port)+"/api/data/"+_args.group+"/";
		gapr::parse_catalog(str, _cube_infos, mesh_infos, base_url);
		if(_cube_infos.empty())
			throw gapr::reported_error{"no imaging data"};
	}
}

void Tracer::start_cube_builder() {
	_cube_builder=std::make_shared<gapr::cube_builder>(_io_ctx.get_executor(), _thr_pool);
	_cube_builder->async_wait([this](std::error_code ec, int progr) {
		return cube_finished(ec, progr);
	});
	//_cube_builder->start();
}
void Tracer::cube_finished(std::error_code ec, int progr) {
	if(ec)
		gapr::print("error load cube: ", ec.message());
	if(progr==1001) {
		assert(_io_ctx.get_executor().running_in_this_thread());
		_timer.cancel();
	}
	gapr::print("load data: ", progr);
	_cube_builder->async_wait([this](std::error_code ec, int progr) {
		return cube_finished(ec, progr);
	});
}

void Tracer::get_cubes() {
	auto cube=_cube_builder->get();
	while(cube.data) {
		gapr::print("cube_out: ", cube.chan);
		if(_global_ch==cube.chan) {
			_global_cube=cube.data;
		} else if(_closeup_ch==cube.chan) {
			_closeup_cube=cube.data;
			_closeup_offset=cube.offset;
		}
		cube=_cube_builder->get();
	}
}

void Tracer::get_closeup(const std::array<unsigned int, 3>& offset, const std::array<double, 3>& center, gapr::fiber_ctx& ctx) {
	int retry_get_cubes=0;
	do {
		if(_cube_builder->build(_closeup_ch, _cube_infos[_closeup_ch-1], center, true, _closeup_cube?&_closeup_offset:nullptr, true)) {
			_timer.expires_from_now(boost::posix_time::seconds{1000});
			std::error_code ec;
			_timer.async_wait(gapr::yield{ctx, ec});
		}
		get_cubes();
		if(!_closeup_cube || _closeup_offset!=offset) {
			if(retry_get_cubes++<10) {
				int waitfor=2*retry_get_cubes*60;
				gapr::print("retry in ", waitfor, 's');
				_timer.expires_from_now(boost::posix_time::seconds{waitfor});
				std::error_code ec;
				_timer.async_wait(gapr::yield{ctx, ec});
				continue;
			}
			throw gapr::reported_error{"failed to load cube"};
		}
		break;
	} while(true);
}

void Tracer::vote_unfinished() {
	if(!_model_updated)
		return;
	{
				std::vector<std::size_t> idxes;
				while(!_pq_queue.empty()) {
					auto i=_pq_queue.top();
					_pq_queue.pop();
					if(_pq_store[i].skip) {
						recycle(i);
						continue;
					}
					if(_pq_store[i].vote_ds==0) {
						_pq_map.erase(_pq_store[i].offset);
						recycle(i);
						continue;
					}
					_pq_store[i].vote_total=_pq_store[i].vote_ds;
					idxes.push_back(i);
				}
				for(auto i: idxes)
					_pq_queue.push(i);
	}
	gapr::edge_model::reader model{_model};

	for(auto& [vid, vert]: model.vertices()) {
		if(vert.edges.size()>=2)
			continue;
		if(model.props().find(gapr::prop_id{vid, "state"})!=model.props().end())
			continue;
		auto& p=vert.attr;
		int www=10000;
		if(model.props().find(gapr::prop_id{vid, ".traced"})!=model.props().end()) {
			if(vert.edges.size()==0)
			continue;
			www=1;
		}
		if(vert.edges.size()==0) {
			vote({p.pos(0), p.pos(1), p.pos(2)}, {0, 0, 0}, 10*www, 0);
			continue;
		}
		auto [eid, dir]=vert.edges[0];
		auto& edg=model.edges().at(eid);
		std::size_t i=SIZE_MAX;
		if(edg.points.size()>30) {
			if(dir) {
				i=edg.points.size()-30;
			} else {
				i=30;
			}
		}
		vote({p.pos(0), p.pos(1), p.pos(2)}, {0, 0, 0}, 10*www, 0);
		if(i!=SIZE_MAX) {
			gapr::node_attr p2{edg.points[i]};
			vote({p.pos(0), p.pos(1), p.pos(2)}, {p.pos(0)-p2.pos(0), p.pos(1)-p2.pos(1), p.pos(2)-p2.pos(2)}, 15*www, 0);
		}
	}
	_model_updated=false;
}

std::array<unsigned int, 3> Tracer::to_offset(const std::array<double, 3>& pos) {
	auto& info=_cube_infos[_closeup_ch-1];
	auto offi=info.to_offseti(pos, true);
	std::array<unsigned int, 3> off;
	for(unsigned int i=0; i<3; ++i) {
		auto oo=offi[i];
		if(oo<0)
			oo=0;
		else if(oo>=info.sizes[i])
			oo=(info.sizes[i]-1)/info.cube_sizes[i]*info.cube_sizes[i];
		off[i]=oo;
	}
	return off;
}
void Tracer::vote(const std::array<double, 3>& _pos, const std::array<double, 3>& _dir, int w0, int w1) {
	//////////////////////////////////////////////////////////if(_pos[2]<24000) { fprintf(stderr, "."); return; }

	//if(_pos[2]>22000) { fprintf(stderr, "."); return; }
	//XXX consider
	//- position
	//- direction
	//- locality
	auto pos=_pos;
	gapr::vec3<double> dir{_dir[0], _dir[1], _dir[2]};
	if(auto m2=dir.mag2(); m2>900)
		dir=dir*(50/std::sqrt(m2));
	for(unsigned int i=0; i<3; i++) {
		pos[i]+=1*dir[i];
	}

	auto offset=to_offset(pos);
	auto get_rng=[](unsigned int v, unsigned int d, unsigned int m) ->auto {
		std::array<std::pair<unsigned int, int>, 3> rng;
		rng[0].first=(v>=d?v-d:v);
		rng[0].second=1;
		rng[1].first=v;
		rng[1].second=2;
		rng[2].first=(v+d<m?v+d:v);
		rng[2].second=1;
		return rng;
	};
	auto& info=_cube_infos[_closeup_ch-1];
	for(auto [xi, xw]: get_rng(offset[0], info.cube_sizes[0], info.sizes[0])) {
		for(auto [yi, yw]: get_rng(offset[1], info.cube_sizes[1], info.sizes[1])) {
			for(auto [zi, zw]: get_rng(offset[2], info.cube_sizes[2], info.sizes[2])) {
				std::array<unsigned int, 3> offset2{xi, yi, zi};
				auto cent=get_center(offset2);
				offset2=to_offset(cent);
				auto w=xw+yw+zw;
				vote_helper(offset2, w*w0, w*w1);
			}
		}
	}
}

void Tracer::vote_helper(const std::array<unsigned, 3>& offset, int w0, int w1) {
	auto& info=_cube_infos[_closeup_ch-1];
	for(unsigned int i=0; i<3; i++) {
		if((offset[i]/info.cube_sizes[i])%2!=0)
			return;
	}
	//gapr::print("vote: [", offset[0], ':', offset[1], ':', offset[2], "] [", w0, '/', w1, ']');
	auto [it, ins]=_pq_map.emplace(offset, SIZE_MAX);
	w0+=w1;
	if(!ins) {
		auto idx0=it->second;
		if(_pq_store[idx0].stage!=TaskStage::Queued)
			return;
		w1+=_pq_store[idx0].vote_ds;
		w0+=_pq_store[idx0].vote_total;
		_pq_store[idx0].skip=true;
	}
	std::size_t idx;
	if(_pq_store_free) {
		idx=_pq_store_free-1;
		_pq_store_free=_pq_store[idx].next_free;
	} else {
		_pq_store.emplace_back();
		idx=_pq_store.size()-1;
	}
	_pq_store[idx].vote_total=w0;
	_pq_store[idx].vote_ds=w1;
	_pq_store[idx].offset=offset;
	_pq_store[idx].skip=false;
	_pq_store[idx].stage=TaskStage::Queued;
	it->second=idx;
	_pq_queue.push(idx);
}

bool Tracer::load_commits(gapr::fiber_ctx& ctx, client_end msg, uint64_t upto) {

	do {
		if(upto<10)
			break;
		if(_hist.body_count()>0)
			break;
		gapr::fiber fib{ctx.get_executor(), api.get_model(msg)};
		auto cmt=std::move(fib).async_wait(gapr::yield{ctx});
		if(!cmt.file)
			break;

		gapr::promise<uint64_t> prom{};
		gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
		auto ex1=_thr_pool.get_executor();
		ba::post(ex1, [this,prom=std::move(prom),file=std::move(cmt.file)]() mutable {
			auto ex1=_thr_pool.get_executor();
			assert(ex1.running_in_this_thread());
			(void)ex1;
			gapr::edge_model::loader loader{_model};
			auto sb=gapr::make_streambuf(std::move(file));
			auto r=loader.init(*sb);
			return std::move(prom).set(r);
		});
		auto r=std::move(fib2).async_wait(gapr::yield{ctx});
		if(!r)
			return false;
		_hist.body_count(r);
	} while(false);

	auto t0=std::chrono::steady_clock::now();
#if 0
	while(_hist.body_count()<upto) {
		auto next_id=_hist.body_count();
		gapr::fiber fib{ctx.get_executor(), api.get_commit(msg, next_id)};
		auto cmt=std::move(fib).async_wait(gapr::yield{ctx});
		{
			auto msg2=std::move(msg);
			msg=std::move(msg2);
		}

		gapr::promise<bool> prom{};
		gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
		auto ex1=_thr_pool.get_executor();
		ba::post(ex1, [this,prom=std::move(prom),file=std::move(cmt.file)]() mutable {
			auto r=model_prepare(std::move(file));
			return std::move(prom).set(r);
		});
		if(!std::move(fib2).async_wait(gapr::yield{ctx}))
			return false;
		_hist.body_count(next_id+1);
	}
#else
		//ensure conn
		//assert(!_prelock_model.can_read_async());
		if(_hist.body_count()>=upto)
			return true;
		int i=0;
		//gapr::timer<4> timer;
		gapr::mem_file commits_file;
		{
			gapr::promise<gapr::mem_file> prom{};
			gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
			auto ex1=_thr_pool.get_executor();
			ba::post(ex1, [hist=_hist,prom=std::move(prom)]() mutable {
				gapr::mem_file hist_data;
				try {
					hist_data=serialize(hist);
					std::move(prom).set(std::move(hist_data));
				} catch(const std::runtime_error& e) {
					return unlikely(std::move(prom), std::current_exception());
				}
			});
			auto hist_data=std::move(fib2).async_wait(gapr::yield{ctx});
			//timer.mark<0>();
			gapr::fiber fib{ctx.get_executor(), api.get_commits(msg, std::move(hist_data), upto)};
			auto cmts=std::move(fib).async_wait(gapr::yield{ctx});
			commits_file=std::move(cmts.file);
			//timer.mark<1>();
		}
		auto strmbuf=gapr::make_streambuf(std::move(commits_file));

		while(_hist.body_count()<upto) {
			auto ex1=_thr_pool.get_executor();
			gapr::promise<uint64_t> prom{};
			gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
			ba::post(ex1, [this,prom=std::move(prom),strmbuf=strmbuf.get()]() mutable {
				gapr::commit_info info;
				if(!info.load(*strmbuf))
					gapr::report("commit file no commit info");
				auto ex1=_thr_pool.get_executor();
				assert(ex1.running_in_this_thread());
				(void)ex1;
				gapr::edge_model::loader loader{_model};
				auto r=gapr::delta_variant::visit<bool>(gapr::delta_type{info.type},
						[&loader,&info,strmbuf](auto typ) {
							gapr::delta<typ> delta;
							if(!gapr::load(delta, *strmbuf))
								gapr::report("commit file no delta");
							if(!loader.load(gapr::node_id{info.nid0}, std::move(delta)))
								return false;
							return true;
						});
				if(!r)
					return std::move(prom).set(std::numeric_limits<uint64_t>::max());
				return std::move(prom).set(info.id);
			});

			auto next_id=std::move(fib2).async_wait(gapr::yield{ctx});
			if(next_id!=_hist.body_count())
				return false;
			//timer.mark<2>();
			_hist.body_count(next_id+1);
			i++;

			//timer.mark<3>();
		}
#endif
	auto t1=std::chrono::steady_clock::now();
	fprintf(stderr, "load_commits time: %ld\n", std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count());
	return true;
}

bool Tracer::model_apply(bool last) {
	auto ex2=_io_ctx.get_executor();
	assert(ex2.running_in_this_thread());
	(void)ex2;
	gapr::print("model apply");

	gapr::edge_model::updater updater{_model};
	if(!updater.empty()) {
		if(!updater.apply())
			return false;
		_model_updated=true;
		prev_nid0=updater.nid0();
	}
	return true;
}

int Tracer::run_impl(gapr::fiber_ctx& ctx) {
	prepare(ctx);

	auto [first_c, first_g]=select_chan();
	{
		_global_ch=first_g;
		_closeup_ch=first_c;
		gapr::str_glue msg{
			"Server: ", _srv_info,
			"\nUser: ", _gecos,
			"\nCommits: ", _latest_commit,
			"\nDown-sampled data: ", _cube_infos[first_g-1].name(),
			"\nData: ", _cube_infos[first_c-1].name(),
			'\n'};
		std::cerr<<msg.str();
	}

	start_cube_builder();

	if(0) {
		assert(!_global_cube);
		_cube_builder->build(_global_ch, _cube_infos[_global_ch-1]);
		if(true) {
			_timer.expires_from_now(boost::posix_time::seconds{1000});
			std::error_code ec;
			_timer.async_wait(gapr::yield{ctx, ec});
		}
		get_cubes();
		if(!_global_cube)
			throw gapr::reported_error{"failed to load down-sampled cube"};

		gapr::trace::ProbeAlg alg{_cube_infos[_global_ch-1].xform};
		gapr::trace::ProbeAlg::Job job{_global_cube};
		job.cancel=&_cancel_flag;

		gapr::promise<int> prom{};
		gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
		auto ex=_thr_pool.get_executor();
		ba::post(ex, [prom=std::move(prom),&alg,&job]() mutable {
			try {
				job(alg);
				std::move(prom).set(0);
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});
		std::move(fib2).async_wait(gapr::yield{ctx});
		auto points=std::move(job.seeds);
		for(auto& pt: points) {
			vote({pt[0], pt[1], pt[2]}, {0, 0, 0}, 0, 1);
		}
	}

	std::unordered_set<std::array<unsigned int, 3>, Hash> traced_cubes;
	std::size_t traced_cubes_last{0};

	unsigned int niter=0;
	gapr::trace::ConnectAlg alg{"", _cube_infos[_closeup_ch-1].xform, _model};
	alg.evaluator(_args.evaluator);
	bool need_loading{true};
	std::deque<std::tuple<std::size_t, gapr::future<int>, std::unique_ptr<gapr::trace::ConnectAlg::Job>>> running_tasks{};
	do {
		if(_args.maxiter==0 || niter<_args.maxiter) {
			if(need_loading) {
				if(!load_commits(ctx, _cur_conn, _latest_commit))
					throw gapr::reported_error{"failed to load commits"};
				if(!model_apply(true))
					throw gapr::reported_error{"failed to update model"};
				need_loading=false;
			}
			vote_unfinished();

			std::vector<std::size_t> avoiding{};
			auto avoid_running=[this,&running_tasks](const std::array<unsigned, 3>& offset) ->bool {
				auto& info=_cube_infos[_closeup_ch-1];
				for(auto& [idx, fut_, alg_]: running_tasks) {
					auto o2=_pq_store[idx].offset;
					bool hit=true;
					for(unsigned int i=0; i<3; i++) {
						unsigned int pad=4;
						unsigned int s=info.cube_sizes[i]*3+pad;
						hit=hit&&(offset[i]<o2[i]+s);
						hit=hit&&(o2[i]<offset[i]+s);
					}
					if(hit) {
						return true;
					}
				}
				return false;
			};

			std::size_t idx{SIZE_MAX};
			while(!_pq_queue.empty()) {
				auto i=_pq_queue.top();
				_pq_queue.pop();
				if(!_pq_store[i].skip) {
					if(avoid_running(_pq_store[i].offset)) {
						avoiding.push_back(i);
						continue;
					}
					idx=i;
					break;
				}
				recycle(i);
			}
			for(auto i: avoiding)
				_pq_queue.push(i);
			if(idx==SIZE_MAX) {
				if(running_tasks.empty()) {
					if(!get_retry(ctx))
						break;
					select(ctx);
					need_loading=true;
					continue;
				}
			} else {
				_pq_store[idx].stage=TaskStage::Running;

				std::vector<gapr::node_attr> to_skip{};
				if(true) {
					gapr::edge_model::reader model{_model};
					for(auto& [vid, vert]: model.vertices()) {
						gapr::edge_model::prop_id pid{vid, "root"};
						if(model.props().find(pid)!=model.props().end()) {
							to_skip.push_back(vert.attr);
						}
					}

					using namespace std::string_view_literals;
					auto& logs=model.logs();
					while(traced_cubes_last<logs.size()) {
						std::string_view log{logs[traced_cubes_last++]};
						auto i=log.find('=');
						auto key=log.substr(0, i);
						if(key!="traced"sv)
							continue;
						std::array<unsigned int, 3> off;
						if(i+1>=log.size())
							continue;
						if(!gapr::parse_tuple(&log.substr(i+1)[0], &off[0], &off[1], &off[2]))
							continue;
						gapr::print("traced cube: ", log, "   ", traced_cubes_last, '/', logs.size());
						traced_cubes.emplace(off);
					}
				}

				auto& info=_pq_store[idx];
				gapr::print("cur cube: [", info.offset[0], ':', info.offset[1], ':', info.offset[2], "] [", info.vote_total, '/', info.vote_ds, "] [", info.skip, ',', static_cast<int>(info.stage), ']');
				std::array<double, 3> center=get_center(info.offset);
				if(!to_skip.empty()) {
					bool skip{false};
					bool in_range{false};
					for(auto p: to_skip) {
						auto dist=p.dist_to(center);
						if(dist<_args.range0)
							skip=true;
						if(dist<_args.range1)
							in_range=true;
					}
					if(!in_range) {
						gapr::print(1, "skip it, out of range");
						_pq_store[idx].stage=TaskStage::Skipped;
						continue;
					}
					if(skip) {
						gapr::print(1, "skip it, near soma");
						_pq_store[idx].stage=TaskStage::Skipped;
						continue;
					}
				}
				if(!traced_cubes.empty()) {
					auto it=traced_cubes.find(info.offset);
					if(it!=traced_cubes.end()) {
						gapr::print(1, "skip it, previously traced");
						_pq_store[idx].stage=TaskStage::Skipped;
						continue;
					}
				}

				get_closeup(info.offset, center, ctx);

				//XXX lock best
				std::unique_ptr<gapr::trace::ConnectAlg::Job> job{new gapr::trace::ConnectAlg::Job{_closeup_cube, _closeup_offset}};
				job->cancel=&_cancel_flag;
				gapr::promise<int> prom;
				auto fut=prom.get_future();
				auto ex1=_thr_pool.get_executor();
				ba::post(ex1, [prom=std::move(prom),&alg,&job=*job]() mutable {
					try {
						job(alg);
						std::move(prom).set(0);
					} catch(const std::runtime_error& e) {
						unlikely(std::move(prom), std::current_exception());
					}
				});

				++niter;
				running_tasks.emplace_back(idx, std::move(fut), std::move(job));

				if(_args.jobs!=0 && running_tasks.size()<_args.jobs)
					continue;
			}
		} else {
			if(running_tasks.empty())
				break;
		}

		do {
			auto [idx, fut, alg]=std::move(running_tasks.front());
			running_tasks.pop_front();
			gapr::fiber fib2{ctx.get_executor(), std::move(fut)};
			std::move(fib2).async_wait(gapr::yield{ctx});
			auto delta=std::move(alg->delta);
			{
				using namespace std::string_view_literals;
				assert(_pq_store[idx].offset==alg->offset);
				auto [x, y, z]=alg->offset;
				auto log=gapr::str_glue{nullptr, ":"sv}("traced=")(x, y, z).str();
				delta.props.emplace_back(gapr::node_id::max().data, std::move(log));
			}
			bool is_empty=delta.nodes.size()<1;
			auto res=submit_commit(ctx, std::move(delta));
			if(!model_apply(true))
				throw gapr::reported_error{"failed to update model 2"};
			switch(res.first) {
				case SubmitRes::Retry:
				case SubmitRes::Deny:
					if(!res.second.empty()) {
						gapr::str_glue err{"failed to commit: ", res.second};
						throw gapr::reported_error{err.str()};
					}
					break;
				case SubmitRes::Accept:
					break;
			}
			if(auto delta=std::move(alg->delta2); !delta.nodes.empty()) {
				auto res=submit_commit(ctx, std::move(delta));
				if(!model_apply(true))
					throw gapr::reported_error{"failed to update model 2"};
				switch(res.first) {
					case SubmitRes::Retry:
					case SubmitRes::Deny:
						if(!res.second.empty()) {
							gapr::str_glue err{"failed to commit: ", res.second};
							throw gapr::reported_error{err.str()};
						}
						break;
					case SubmitRes::Accept:
						break;
				}
			}
			_pq_store[idx].stage=TaskStage::Finished;
			if(is_empty)
				_pq_store[idx].stage=TaskStage::Empty;
		} while(false);

		if(_args.jobs==0) {
			if(_args.maxiter!=0 && niter>=_args.maxiter)
				break;
			if(!get_retry(ctx))
				break;
		}

		select(ctx);
		need_loading=true;
	} while(true);

	{
		auto cli=std::move(_cur_conn);
	}
	return 0;
}

struct ImportHelper {
	struct Node: gapr::node_attr {
		int64_t par_id;
		gapr::node_id id2{};
		unsigned int state;
	};
	std::unordered_map<int64_t, Node> nodes;
	std::unordered_multimap<int64_t, std::string> props;
	std::unordered_multimap<int64_t, int64_t> loops;
	std::vector<int64_t> todo{};
	struct DeltaBuilder {
		ImportHelper& helper;
		gapr::delta_add_patch_ delta{};
		gapr::node_id cur_id2{0};
		std::vector<Node*> tofix;
		DeltaBuilder(ImportHelper& helper): helper{helper} { }
		void add_props(gapr::node_id id2, int64_t id) {
			auto [a, b]=helper.props.equal_range(id);
			for(auto it=a; it!=b; ++it)
				delta.props.emplace_back(id2.data, it->second);
		}
		void add_link(gapr::node_id id2, const Node& np) {
			delta.nodes.emplace_back(gapr::node_attr{}.data(), id2.data);
			cur_id2=cur_id2.offset(1);
			assert(np.state==2);
			assert(np.id2!=gapr::node_id{});
			gapr::link_id l{np.id2, {}};
			delta.links.emplace_back(cur_id2.data, l.data());
		}
		gapr::vec3<> init(int64_t seed) {
			gapr::print("seed: ", seed);
			auto& seed_n=helper.nodes.at(seed);
			delta.nodes.emplace_back(seed_n.data(), gapr::node_id{}.data);
			cur_id2=cur_id2.offset(1);
			assert(seed_n.id2==gapr::node_id{});
			seed_n.id2=cur_id2;
			assert(seed_n.state==0);
			seed_n.state=1;
			tofix.emplace_back(&seed_n);
			add_props(seed_n.id2, seed);
			if(seed_n.par_id!=-1) {
				auto& seed_p=helper.nodes.at(seed_n.par_id);
				add_link(seed_n.id2, seed_p);
			}
			auto [l_a, l_b]=helper.loops.equal_range(seed);
			for(auto l=l_a; l!=l_b; ++l) {
				auto& seed_p=helper.nodes.at(l->second);
				add_link(seed_n.id2, seed_p);
			}
			return seed_n.pos();
		}
		template<typename Func>
		void finish(Func func) {
			for(std::size_t k=helper.todo.size(); k-->0; ) {
				auto id=helper.todo[k];
				if(id==-1)
					continue;
				auto& nn=helper.nodes.at(id);

				gapr::node_id par_id{};
				Node* par{nullptr};
				if(nn.par_id!=-1) {
					auto& np=helper.nodes.at(nn.par_id);
					if(np.state==0)
						continue;
					else if(np.state==1)
						par_id=np.id2;
					else
						par=&np;
				}

				unsigned int links_unready{0};
				auto [lbeg, lend]=helper.loops.equal_range(id);
				for(auto i=lbeg; i!=lend; ++i) {
					auto& np=helper.nodes.at(i->second);
					if(np.state!=2)
						++links_unready;
				}
				if(links_unready>0)
					continue;

				auto noutside=func(nn.pos());
				if(noutside>0)
					continue;

				delta.nodes.emplace_back(nn.data(), par_id.data);
				cur_id2=cur_id2.offset(1);
				assert(nn.id2==gapr::node_id{});
				nn.id2=cur_id2;
				assert(nn.state==0);
				nn.state=1;
				tofix.emplace_back(&nn);
				helper.todo[k]=-1;
				add_props(nn.id2, id);
				if(par)
					add_link(nn.id2, *par);
				for(auto l=lbeg; l!=lend; ++l) {
					auto& np=helper.nodes.at(l->second);
					add_link(nn.id2, np);
				}
			}
			std::sort(delta.props.begin(), delta.props.end(), [](auto& a, auto& b) {
				if(a.first<b.first)
					return true;
				if(a.first>b.first)
					return false;
				return a.second<b.second;
			});
			//dump(delta, std::cerr, 1, gapr::node_id{});
		}
	};
	void add_node(int64_t id, const gapr::swc_node& n, gapr::misc_attr misc) {
		auto [it, ins]=nodes.emplace(id, Node{});
		assert(ins);
		auto& nn=it->second;
		nn.misc=misc;
		nn.misc.t(n.type);
		nn.pos(n.pos);
		nn.misc.r(n.radius);
		nn.par_id=n.par_id;
		nn.id2={};
		nn.state=0;
		todo.push_back(id);
	}
	void add_prop(int64_t id, std::string_view prop) {
		if(prop.size()>0 && prop[0]=='.')
			return;
		auto it=props.emplace(id, prop);
	}
	void add_loop(int64_t id, int64_t id2) {
		auto it=loops.emplace(id, id2);
	}
	void add_misc(int64_t id, gapr::misc_attr misc) {
		nodes.at(id).misc=misc;
	}
	void set_coverage(bool v) {
		for(auto& [id, n]: nodes)
			n.misc.coverage(v);
	}
	void finish_terms() {
		std::unordered_map<int64_t, unsigned int> n2d;
		auto add_deg=[&n2d](int64_t id) {
			auto [it, ins]=n2d.emplace(id, 1);
			if(!ins)
				++it->second;
		};
		for(auto [a, b]: loops) {
			add_deg(a);
			add_deg(b);
		}
		for(auto& [id, n]: nodes) {
			if(n.par_id!=-1) {
				add_deg(id);
				add_deg(n.par_id);
			}
		}
		auto is_tag=[](std::string_view kval, std::string_view tag) {
			if(kval.size()<tag.size())
				return false;
			if(kval.compare(0, tag.size(), tag)!=0)
				return false;
			if(kval.size()==tag.size())
				return true;
			if(kval[tag.size()]=='=')
				return true;
			return false;
		};
		std::string term_annot{"state=end"};
		auto term_tag=term_annot.substr(0, term_annot.find_first_of('='));
		for(auto [id, d]: n2d) {
			if(d==1) {
				auto [a, b]=props.equal_range(id);
				unsigned int hit=0;
				for(auto it=a; it!=b; ++it) {
					assert(it->first==id);
					if(is_tag(it->second, term_tag))
						++hit;
				}
				if(hit==0)
					props.emplace(id, term_annot);
			}
		}
	}
};

int Tracer::run_import(gapr::fiber_ctx& ctx) {
	ImportHelper helper;
	{
		std::ifstream fs{_args.from_swc};
		boost::iostreams::filtering_istream filter{};
		if(_args.from_swc.extension()==".gz")
			filter.push(boost::iostreams::gzip_decompressor{});
		filter.push(fs);
		gapr::swc_input swc{filter};
		while(swc.read()) {
			switch(swc.tag()) {
			case gapr::swc_input::tags::comment:
				break;
			case gapr::swc_input::tags::node:
				helper.add_node(swc.id(), swc.node(), swc.misc_attr());
				break;
			case gapr::swc_input::tags::annot:
				helper.add_prop(swc.id(), swc.annot());
				break;
			case gapr::swc_input::tags::misc_attr:
				helper.add_misc(swc.id(), swc.misc_attr());
				break;
			case gapr::swc_input::tags::loop:
				helper.add_loop(swc.id(), swc.loop());
				break;
			}
		}
		if(!swc.eof())
			gapr::report("Failed to read swc file.");
	}
	if(helper.todo.empty())
		return 0;
	if(_args.from_swc_fin) {
		helper.set_coverage(true);
		helper.finish_terms();
	}

	prepare(ctx);
	auto [closeup_ch, global_ch]=select_chan();
	_closeup_ch=closeup_ch;
		select(ctx);
		if(!load_commits(ctx, _cur_conn, _latest_commit))
			throw gapr::reported_error{"failed to load commits"};
		if(!model_apply(true))
			throw gapr::reported_error{"failed to update model"};

	start_cube_builder();
	std::reverse(helper.todo.begin(), helper.todo.end());
	do {
		assert(!helper.todo.empty());
		auto seed=helper.todo.back();
		helper.todo.pop_back();
		if(seed==-1)
			continue;

		ImportHelper::DeltaBuilder builder{helper};
		auto seed_pos=builder.init(seed);
		auto seed_off=_cube_infos[_closeup_ch-1].to_offseti(seed_pos, true);
		fprintf(stderr, "%d %d %d\n", seed_off[0], seed_off[1], seed_off[2]);
		//gapr::print("cur cube: [", info.offset[0], ':', info.offset[1], ':', info.offset[2], "] [", info.vote_total, '/', info.vote_ds, "] [", info.skip, ',', static_cast<int>(info.stage), ']');
		//
		//get closeup cube
		//XXX lock best
		//
		auto& info=_cube_infos[closeup_ch-1];
		auto filter=[&info,&seed_off,&seed_pos](const auto& pos) {
			double maxl1=0;
			for(unsigned int i=0; i<3; ++i)
				maxl1=std::max(maxl1, std::abs(pos[i]-seed_pos[i]));
			if(maxl1>1600)
				return 1u;
			auto node_off=info.xform.to_offseti(pos);
			unsigned int noutside{0};
			for(unsigned int i=0; i<3; ++i) {
				if(node_off[i]<=seed_off[i])
					++noutside;
				if(node_off[i]>=seed_off[i]+static_cast<int>(info.cube_sizes[i]*3))
					++noutside;
			}
			return noutside;
		};

		gapr::promise<int> prom;
		auto fut=prom.get_future();
		auto ex1=_thr_pool.get_executor();
		ba::post(ex1, [prom=std::move(prom),&builder,filter]() mutable {
			try {
				builder.finish(filter);
				std::move(prom).set(0);
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});

		do {
			gapr::fiber fib2{ctx.get_executor(), std::move(fut)};
			std::move(fib2).async_wait(gapr::yield{ctx});
			auto delta=std::move(builder.delta);
			assert(delta.nodes.size()>0);
			auto res=submit_commit(ctx, std::move(delta));
			if(!model_apply(true))
				throw gapr::reported_error{"failed to update model 2"};
			switch(res.first) {
			case SubmitRes::Retry:
			case SubmitRes::Deny:
				if(!res.second.empty()) {
					gapr::str_glue err{"failed to commit: ", res.second};
					throw gapr::reported_error{err.str()};
				}
				break;
			case SubmitRes::Accept:
				break;
			}

			for(unsigned int i=0; i<builder.tofix.size(); ++i) {
				auto n=builder.tofix[i];
				assert(n);
				assert(n->id2);
				assert(n->state==1);
				n->id2=prev_nid0.offset(i);
				n->state=2;
			}
		} while(false);

		select(ctx);
		if(!load_commits(ctx, _cur_conn, _latest_commit))
			throw gapr::reported_error{"failed to load commits"};
		if(!model_apply(true))
			throw gapr::reported_error{"failed to update model"};
	} while(!helper.todo.empty());

	{
		auto cli=std::move(_cur_conn);
	}
	return 0;
}

int Tracer::run_impl2(gapr::fiber_ctx& ctx) {
	prepare(ctx);

	struct Node {
		gapr::node_attr pos;
		gapr::edge_model::edge_id eid;
	};
	auto can_connect=[](const std::vector<Node>& nodes, gapr::node_attr pos0, gapr::edge_model::edge_id eid0) {
		bool hits{false};
		gapr::node_attr posa;
		gapr::node_attr posb;
		double d=4;
		for(unsigned int i=0; i<3; i++) {
			posa.pos(i, pos0.pos(i)-d);
			posb.pos(i, pos0.pos(i)+d);
		}
		for(auto& [pos, eid]: nodes) {
			if(eid==eid0)
				continue;

			bool outside{false};
			for(unsigned int i=0; i<3; i++) {
				if(pos.ipos[i]<posa.ipos[i])
					outside=true;
				if(pos.ipos[i]>posb.ipos[i])
					outside=true;
			}
			if(!outside && pos.dist_to(pos0)<4) {
				hits=true;
			}
		}
		return hits;
	};

	std::unordered_set<gapr::node_id> node_mask{};
	do {
		gapr::print(1, "start");
		if(!load_commits(ctx, _cur_conn, _latest_commit))
			throw gapr::reported_error{"failed to load commits"};
		gapr::print(1, "loaded");
		if(!model_apply(true))
			throw gapr::reported_error{"failed to update model"};
		gapr::print(1, "applied");

		gapr::delta_del_patch_ delta{};
		gapr::delta_add_patch_ delta2{};

		{
			gapr::edge_model::reader model{_model};
			{
				std::ofstream ofs{"/tmp/gapr-edge-stats.txt"};
				for(auto& [eid, edg]: model.edges()) {
					auto& vert0=model.vertices().at(edg.left);
					auto& vert1=model.vertices().at(edg.right);
					ofs<<vert0.edges.size()<<' '<<vert1.edges.size()<<' '<<edg.nodes.size()<<' '<<edg.points.size()<<'\n';
				}
			}

			const gapr::edge_model::edge* seed_edge{nullptr};
			for(auto& [eid, edg]: model.edges()) {
				auto& vert0=model.vertices().at(edg.left);
				if(vert0.edges.size()>1)
					continue;
				auto& vert1=model.vertices().at(edg.right);
				if(vert1.edges.size()>1)
					continue;
				if(edg.points.size()>30)
					continue;
				if(node_mask.find(edg.left)!=node_mask.end())
					continue;
				if(node_mask.find(edg.right)!=node_mask.end())
					continue;
				seed_edge=&edg;
				break;
			}
			if(!seed_edge)
				break;

			gapr::print("n1: ", seed_edge->points.size());
			gapr::print("n2: ", seed_edge->nodes.size());
			gapr::node_attr bbox0l{seed_edge->points.front()}, bbox1l, bbox1r;
			auto bbox0r=bbox0l;
			for(auto pt: seed_edge->points) {
				gapr::node_attr pos{pt};
				for(unsigned int i=0; i<3; i++) {
					if(pos.ipos[i]<bbox0l.ipos[i])
						bbox0l.ipos[i]=pos.ipos[i];
					if(pos.ipos[i]>bbox0r.ipos[i])
						bbox0r.ipos[i]=pos.ipos[i];
				}
			}
			for(unsigned int i=0; i<3; i++) {
				auto d=bbox0r.pos(i)-bbox0l.pos(i);
				if(d+60<200) {
					d=(200-d)/2;
				} else {
					d=30;
				}
				bbox0l.pos(i, bbox0l.pos(i)-d);
				bbox0r.pos(i, bbox0r.pos(i)+d);
				bbox1l.pos(i, bbox0l.pos(i)-5);
				bbox1r.pos(i, bbox0r.pos(i)+5);
			}
			gapr::print("bbox: ", bbox0l.pos(0), ',', bbox0l.pos(1), ',', bbox0l.pos(2),
					',', bbox0r.pos(0), ',', bbox0r.pos(1), ',', bbox0r.pos(2));

			//XXX local lock (seed_edge fully contained)
			//update
			std::unordered_set<gapr::edge_model::edge_id> edges_to_fix;
			std::vector<Node> nodes_around;
			auto test_inside=[](gapr::node_attr l, gapr::node_attr u, gapr::node_attr p) {
				bool inside{true};
				for(unsigned int i=0; i<3; i++) {
					if(l.ipos[i]>p.ipos[i])
						inside=false;
					if(u.ipos[i]<p.ipos[i])
						inside=false;
				}
				return inside;
			};
			for(auto& [eid, edg]: model.edges()) {
				bool inside{true};
				for(auto pt: edg.points) {
					gapr::node_attr pos{pt};
					if(test_inside(bbox1l, bbox1r, pos)) {
						nodes_around.push_back(Node{pos, eid});
						if(!test_inside(bbox0l, bbox0r, pos))
							inside=false;
					} else {
						inside=false;
					}
				}
				auto& vert0=model.vertices().at(edg.left);
				if(vert0.edges.size()>1)
					continue;
				auto& vert1=model.vertices().at(edg.right);
				if(vert1.edges.size()>1)
					continue;
				if(edg.points.size()>30)
					continue;
				if(!inside)
					continue;
				node_mask.emplace(edg.left);
				node_mask.emplace(edg.right);
				edges_to_fix.emplace(eid);
			}

			std::unordered_set<gapr::node_id> nodes_del{};
			for(auto eid: edges_to_fix) {
				auto& edg=model.edges().at(eid);
				if(can_connect(nodes_around, gapr::node_attr{edg.points.front()}, eid))
					continue;
				if(can_connect(nodes_around, gapr::node_attr{edg.points.back()}, eid))
					continue;
				gapr::print(1, "remove edges", 1);
				delta.nodes.push_back(edg.nodes.front().data);
				for(auto n: edg.nodes) {
					nodes_del.emplace(n);
					delta.nodes.push_back(n.data);
				}
				delta.nodes.push_back(edg.nodes.back().data);
			}
			for(auto& [pid, pval]: model.props()) {
				if(nodes_del.find(pid.node)!=nodes_del.end()) {
					delta.props.emplace_back(pid.node.data, std::move(pid.key));
				}
			}
			std::sort(delta.props.begin(), delta.props.end());
		}

		do {
			if(delta.nodes.empty())
				break;
			auto res=submit_commit(ctx, std::move(delta));
			if(!model_apply(true))
				throw gapr::reported_error{"failed to update model 2"};
			switch(res.first) {
				case SubmitRes::Retry:
				case SubmitRes::Deny:
					if(!res.second.empty()) {
						gapr::str_glue err{"failed to commit: ", res.second};
						throw gapr::reported_error{err.str()};
					}
					break;
				case SubmitRes::Accept:
					break;
			}
		} while(false);

		if(false && !get_retry(ctx))
			break;
		select(ctx);
	} while(true);
	return 0;
}

static double dist_to(const std::array<double, 3>& p,
		const std::array<double, 3>& p2) {
	auto dx=p2[0]-p[0];
	auto dy=p2[1]-p[1];
	auto dz=p2[2]-p[2];
	return std::sqrt(dx*dx+dy*dy+dz*dz);
}
using Vec=std::array<double, 3>;
static double len(const Vec& v) {
	double s=0.0;
	for(unsigned int i=0; i<3; i++) {
		auto vv=v[i];
		s+=vv*vv;
	}
	return std::sqrt(s);
}
static double dot(const Vec& v, const Vec& v2) {
	double s=0.0;
	for(unsigned int i=0; i<3; i++)
		s+=v[i]*v2[i];
	return s;
}
static Vec sub(const Vec& v, const Vec& v2) {
	Vec r;
	for(unsigned int i=0; i<3; i++)
		r[i]=v[i]-v2[i];
	return r;
}
static double dist_to(const std::array<double, 3>& p,
		const std::array<double, 3>& p2,
		const std::array<double, 3>& p3) {
	auto d23=sub(p3, p2);
	auto l=len(d23);
	auto d2=sub(p, p2);
	if(l<0.1)
		return len(d2);
	auto ll=dot(d2, d23)/l;
	if(ll<0)
		return len(d2);
	if(ll>l)
		return len(sub(p, p3));
	for(unsigned int i=0; i<3; i++)
		d2[i]-=d23[i]/l*ll;
	return len(d2);
}
static bool check_inside(const std::array<unsigned int, 3>& offset, const std::array<unsigned int, 3>& cube_offset, const std::array<unsigned int, 3>& cube_sizes) {
	for(unsigned int i=0; i<3; i++) {
		unsigned int npix=(i<2?48:16)/2+1;
		if(offset[i]<cube_offset[i]+npix)
			return false;
		if(offset[i]+npix>=cube_sizes[i]+cube_offset[i])
			return false;
	}
	return true;
}

int Tracer::run_sample(gapr::fiber_ctx& ctx) {
	const double near_thr=2.5;
	const double near_thr2=3.0;
	const double center_thr2=1.5;

	prepare(ctx);

	//gapr::node_id nid_cmp{2339719};
	gapr::node_id nid_cmp{6109005};
	{
		std::size_t first_c{0}, first_g{0};
		for(unsigned int i=0; i<_cube_infos.size(); i++) {
			// XXX in thread_pool
			if(!_cube_infos[i].xform.update_direction_inv())
				gapr::report("no inverse");
			_cube_infos[i].xform.update_resolution();
			if(_cube_infos[i].is_pattern()) {
				if(!first_c)
					first_c=i+1;
			} else {
				if(!first_g)
					first_g=i+1;
			}
		}
		if(!first_c)
			throw gapr::reported_error{"no high-definition channel found"};
		if(!first_g)
			throw gapr::reported_error{"no down-sampled channel found"};
		_global_ch=first_g;
		_closeup_ch=first_c;
	}

	start_cube_builder();

	if(!load_commits(ctx, _cur_conn, _latest_commit))
		throw gapr::reported_error{"failed to load commits"};
	if(!model_apply(true))
		throw gapr::reported_error{"failed to update model"};

	std::vector<std::pair<gapr::edge_model::edge_id, unsigned int>> sorted_edges;
	{
		gapr::edge_model::reader model{_model};
		for(auto& [eid, edg]: model.edges())
			sorted_edges.emplace_back(eid, edg.points.size());
		std::sort(sorted_edges.begin(), sorted_edges.end(), [](auto a, auto b) {
			return a.second<b.second;
		});
		if(1)
			std::shuffle(sorted_edges.begin(), sorted_edges.end(), std::mt19937{});
		for(auto& p: sorted_edges)
			p.second=0;
	}

	unsigned int samp_pos=0;
	unsigned int samp_neg=0;
	unsigned int samp_0=0;
	using Sample=std::array<float, 48*48*16>;
	std::vector<CompressedSample> samples;

	std::unordered_set<gapr::node_id> dirty;

	std::mt19937 rng{};

	while(!sorted_edges.empty()) {
		gapr::edge_model::reader model{_model};

		gapr::node_attr seed;
		{
			auto [eid, idx]=sorted_edges.back();
			auto& edg=model.edges().at(eid);
			while(idx<edg.points.size()) {
				if(dirty.find(edg.nodes[idx])==dirty.end())
					break;
				idx++;
			}
			if(idx>=edg.points.size()) {
				sorted_edges.pop_back();
				continue;
			}
			sorted_edges.back().second=idx+1;
			seed=gapr::node_attr{edg.points[idx]};
		}

		auto offset=to_offset({seed.pos(0), seed.pos(1), seed.pos(2)});
		auto seed_center=get_center(offset);
		struct NodeInfo {
			gapr::edge_model::edge_id eid;
			uint32_t idx;
			gapr::node_id nid;
			std::array<double, 3> pos;
			int category;
			std::array<double, 3> pos2;
		};
		std::vector<NodeInfo> nodes_in;
		for(auto& [eid, edg]: model.edges()) {
			for(unsigned int idx=0; idx<edg.points.size(); idx++) {
				gapr::node_attr attr{edg.points[idx]};
				std::array<double, 3> pos{attr.pos(0), attr.pos(1), attr.pos(2)};
				if(dist_to(pos, seed_center)<200) {
					unsigned int idx2=idx;
					if(idx+1<edg.points.size())
						idx2=idx+1;
					gapr::node_attr attr2{edg.points[idx2]};
					std::array<double, 3> pos2{attr2.pos(0), attr2.pos(1), attr2.pos(2)};
					nodes_in.emplace_back(NodeInfo{eid, idx, edg.nodes[idx], pos, 0, pos2});
				}
			}
		}

		if(nodes_in.size()>=5000 || nodes_in.size()<500) {
			for(auto& node: nodes_in) {
				if(dist_to(node.pos, seed_center)<150)
					dirty.insert(node.nid);
			}
			continue;
		}

		get_closeup(offset, seed_center, ctx);

		std::array<unsigned int, 3> cube_sizes;
		{
			auto view=_closeup_cube.view<char>();
			for(unsigned int i=0; i<3; i++)
				cube_sizes[i]=view.sizes(i);
		}
		std::shuffle(nodes_in.begin(), nodes_in.end(), rng);
		std::vector<NodeInfo> nodes_sel;
		[[maybe_unused]] unsigned int cnt_pos=0, cnt_neg=0, cnt_0=0;
		for(auto& node: nodes_in) {
			if(dirty.find(node.nid)!=dirty.end())
				continue;
			auto node_off=_cube_infos[_closeup_ch-1].xform.to_offset(node.pos);
			if(!check_inside(node_off, _closeup_offset, cube_sizes))
				continue;
			if(dist_to(node.pos, seed_center)>150)
				continue;

			dirty.insert(node.nid);
			if(node.nid.data>nid_cmp.data)
				continue;
			double mdist{INFINITY};
			for(auto& node2: nodes_in) {
				if(node2.eid==node.eid)
					continue;
				auto d=dist_to(node.pos, node2.pos, node2.pos2);
				if(d<mdist)
					mdist=d;
			}
			if(mdist<near_thr) {
				node.category=0;
				cnt_0++;
			} else if(mdist>near_thr2) {
				node.category=1;
				cnt_pos++;
			} else {
				continue;
			}
			nodes_sel.push_back(node);
		}

		std::uniform_real_distribution udist{};
		unsigned int retry_cnt{0};
		while(retry_cnt<(cnt_0+cnt_pos)) {
			auto& node0=nodes_in[rng()%nodes_in.size()];
			if(false && dirty.find(node0.nid)!=dirty.end())
				continue;
			std::array<double, 3> pos;
			for(unsigned int i=0; i<3; i++)
				pos[i]=node0.pos[i]+udist(rng)*30-15;

			auto node_off=_cube_infos[_closeup_ch-1].xform.to_offset(pos);
			if(!check_inside(node_off, _closeup_offset, cube_sizes))
				continue;
			if(dist_to(pos, seed_center)>150)
				continue;

			double mdist{INFINITY};
			for(auto& node2: nodes_in) {
				auto d=dist_to(pos, node2.pos, node2.pos2);
				if(d<mdist)
					mdist=d;
			}
			retry_cnt++;
			if(mdist<center_thr2)
				continue;
			int category=-1;
			cnt_neg++;
			nodes_sel.push_back(NodeInfo{0, 0, gapr::node_id{}, pos, category});
		}

		auto view=_closeup_cube.view<uint16_t>();
		assert(view.type()==gapr::cube_type::u16);
		for(auto& node: nodes_sel) {
			auto node_off=_cube_infos[_closeup_ch-1].xform.to_offset(node.pos);
			for(unsigned int i=0; i<3; i++)
				node_off[i]-=_closeup_offset[i];
			auto samp=std::make_unique<Sample>();
			for(unsigned int z=0; z<16; z++) {
				for(unsigned int y=0; y<48; y++) {
					auto p=view.row(y+node_off[1]-24, z+node_off[2]-8);
					for(unsigned int x=0; x<48; x++) {
						auto v=p[x+node_off[0]-24];
						(*samp)[x+y*48+z*(48*48)]=v/65535.0;
					}
				}
			}
			switch(node.category) {
				case -1:
					samp_neg++;
					break;
				case 0:
					samp_0++;
					break;
				case 1:
					samp_pos++;
					break;
			}
			auto data=compress_zlib(samp->data(), samp->size()*sizeof(float));
			if(1) {
				auto samp2=std::make_unique<Sample>();
				if(!decompress_zlib(samp2->data(), samp2->size()*sizeof(float), data))
					throw std::runtime_error{"failed to compress and decompress"};
				if(*samp!=*samp2)
					throw std::runtime_error{"failed to compress and decompress 2"};
			}
			samples.emplace_back(CompressedSample{std::move(data), node.category});
		}

		gapr::print("cnt: ", samp_neg, ' ', samp_0, ' ', samp_pos);
		if(samples.size()<300'000)
			continue;

		auto cnt_min=samp_pos;
		if(cnt_min>samp_neg)
			cnt_min=samp_neg;
		if(cnt_min>samp_0)
			cnt_min=samp_0;
		std::shuffle(samples.begin(), samples.end(), rng);
		std::size_t i=samples.size();
		auto cnt_max=cnt_min*5+1000;
		while(i-->0 && (samp_0>cnt_max || samp_neg>cnt_max || samp_pos>cnt_max)) {
			auto v=samples[i].tag;
			unsigned int* pdel{nullptr};
			if(v>0) {
				if(samp_pos>cnt_max)
					pdel=&samp_pos;
			} else if(v<0) {
				if(samp_neg>cnt_max)
					pdel=&samp_neg;
			} else {
				if(samp_0>cnt_max)
					pdel=&samp_0;
			}
			if(pdel) {
					samples[i]=std::move(samples.back());
					samples.pop_back();
					(*pdel)--;
			}
		}
		if(samples.size()>=300'000)
			break;
	}

	std::ofstream fs{_args.sample};
	if(!save(samples, fs))
		return -1;
	fs.close();

	{
		auto cli=std::move(_cur_conn);
	}

	return 0;
}

gapr::delta_reset_proofread_ compute_reset_proofread(const gapr::edge_model& model, const std::vector<std::string>& diffs, bool reset_all);
int Tracer::run_reset(gapr::fiber_ctx& ctx) {
	prepare(ctx);

	//XXX lock
	//  maybe compare_and_exchange like??? no

	if(!load_commits(ctx, _cur_conn, _latest_commit))
		throw gapr::reported_error{"failed to load commits"};

	if(!model_apply(true))
		throw gapr::reported_error{"failed to update model"};

	gapr::promise<gapr::delta_reset_proofread_> prom;
	gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
	auto ex1=_thr_pool.get_executor();
	ba::post(ex1, [prom=std::move(prom),this]() mutable {
		try {
			std::move(prom).set(compute_reset_proofread(_model, _args.reset_diffs, _args.reset_all));
		} catch(const std::runtime_error& e) {
			unlikely(std::move(prom), std::current_exception());
		}
	});
	auto delta=std::move(fib2).async_wait(gapr::yield{ctx});
	if(delta.nodes.empty() && delta.props.empty())
		return 0;
	auto res=submit_commit(ctx, std::move(delta));
	if(!model_apply(true))
		throw gapr::reported_error{"failed to update model 2"};

	switch(res.first) {
		case SubmitRes::Retry:
		case SubmitRes::Deny:
			if(!res.second.empty()) {
				gapr::str_glue err{"failed to commit: ", res.second};
				throw gapr::reported_error{err.str()};
			}
			break;
		case SubmitRes::Accept:
			break;
	}
	// unlock
	{
		auto cli=std::move(_cur_conn);
	}
	return 0;
}

int Tracer::run_evaluate(gapr::fiber_ctx& ctx) {
	if(0)
		return run_remove_edges(ctx);
	prepare(ctx);

	{
		std::size_t first_c{0};
		for(unsigned int i=0; i<_cube_infos.size(); i++) {
			// XXX in thread_pool
			if(!_cube_infos[i].xform.update_direction_inv())
				gapr::report("no inverse");
			_cube_infos[i].xform.update_resolution();
			if(_cube_infos[i].is_pattern()) {
				if(!first_c)
					first_c=i+1;
			}
		}
		if(!first_c)
			throw gapr::reported_error{"no high-definition channel found"};
		_closeup_ch=first_c;
	}

	start_cube_builder();

	gapr::trace::EvaluateAlg alg{_args.evaluator, _cube_infos[_closeup_ch-1].xform, _model};
	do {
		if(!load_commits(ctx, _cur_conn, _latest_commit))
			throw gapr::reported_error{"failed to load commits"};
		if(!model_apply(true))
			throw gapr::reported_error{"failed to update model"};

		gapr::node_id seed;
		gapr::node_attr seedp;
		do {
			gapr::edge_model::reader model{_model};
			for(auto& [eid, edg]: model.edges()) {
				for(unsigned int i=0; i<edg.nodes.size(); i++) {
					auto id=edg.nodes[i];
					if(alg.skip_node(id))
						continue;
					gapr::node_attr attr{edg.points[i]};
					if(!attr.misc.coverage()) {
						seed=id;
						seedp=attr;
						break;
					}
				}
				if(seed)
					break;
			}
		} while(false);
		if(!seed)
			break;

		auto offset=to_offset({seedp.pos(0), seedp.pos(1), seedp.pos(2)});
		auto seed_center=get_center(offset);

		get_closeup(offset, seed_center, ctx);

		// XXX lock

		gapr::trace::EvaluateAlg::Job job{_closeup_cube, _closeup_offset};
		job.cancel=&_cancel_flag;
		gapr::promise<int> prom;
		gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
		auto ex1=_thr_pool.get_executor();
		ba::post(ex1, [prom=std::move(prom),&alg,&job]() mutable {
			try {
				job(alg);
				std::move(prom).set(0);
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});
		std::move(fib2).async_wait(gapr::yield{ctx});
		auto delta=std::move(job.delta);
		if(!delta.nodes.empty()) {
			auto res=submit_commit(ctx, std::move(delta));
			if(!model_apply(true))
				throw gapr::reported_error{"failed to update model 2"};
			switch(res.first) {
				case SubmitRes::Retry:
				case SubmitRes::Deny:
					if(!res.second.empty()) {
						gapr::str_glue err{"failed to commit: ", res.second};
						throw gapr::reported_error{err.str()};
					}
					break;
				case SubmitRes::Accept:
					break;
			}
		}

		select(ctx);
	} while(true);

	{
		auto cli=std::move(_cur_conn);
	}
	return 0;
}

int Tracer::run_remove_edges(gapr::fiber_ctx& ctx) {
	prepare(ctx);

	std::unordered_set<gapr::node_id> nodes_to_del;
	std::unordered_set<gapr::link_id> links_to_del;
	{
		std::ifstream fs{_args.evaluator};
		std::string line;
		while(std::getline(fs, line)) {
			if(line.empty())
				continue;
			unsigned int a, b;
			if(auto r=std::sscanf(&line[0], "%u-%u", &a, &b); r==2) {
				gapr::node_id aa{a}, bb{b};
				links_to_del.insert(gapr::link_id{aa, bb}.cannolize());
			} else if(auto r=std::sscanf(&line[0], "%u", &a); r==1) {
				gapr::node_id aa{a};
				nodes_to_del.insert(aa);
			} else {
				throw std::runtime_error{"wrong line"};
			}
		}
		if(!fs.eof())
			throw std::runtime_error{"failed to read file"};
	}

	std::unordered_set<gapr::edge_model::edge_id> edges_to_skip;
	std::unordered_set<gapr::node_id> nodes_to_reset;
	do {
		if(!load_commits(ctx, _cur_conn, _latest_commit))
			throw gapr::reported_error{"failed to load commits"};
		if(!model_apply(true))
			throw gapr::reported_error{"failed to update model"};

		std::future<gapr::delta_del_patch_> fut;
		int fut_prio{-1};
		{
			gapr::edge_model::reader model{_model};
			for(auto& [eid, edg]: model.edges()) {
				if(edges_to_skip.find(eid)!=edges_to_skip.end())
					continue;
				std::size_t a=0;
				while(a+1<edg.nodes.size()) {
					gapr::node_id id0{edg.nodes[a]};
					gapr::node_id id1{edg.nodes[a+1]};
					gapr::link_id l{id0, id1};
					l=l.cannolize();
					if(links_to_del.find(l)!=links_to_del.end())
						break;
					a++;
				}
				if(a+1>=edg.nodes.size())
					continue;
				std::size_t b=a+1;
				while(b+1<edg.nodes.size()) {
					gapr::node_id id0{edg.nodes[b]};
					if(nodes_to_del.find(id0)==nodes_to_del.end())
						break;
					gapr::node_id id1{edg.nodes[b+1]};
					gapr::link_id l{id0, id1};
					l=l.cannolize();
					if(links_to_del.find(l)==links_to_del.end())
						break;
					b++;
				}
				int prio{0};
				int keep_cnt{0};
				std::array<gapr::node_id, 2> to_del_st{};
				std::array<gapr::node_id, 2> to_rst_pr{};
				std::array<bool, 2> del_inc{false, false};
				if(a==0) {
					auto& vert=model.vertices().at(edg.left);
					switch(vert.edges.size()) {
						case 0:
							assert(0);
							break;
						case 1:
							prio+=9999;
							if(nodes_to_del.find(edg.nodes[a])!=nodes_to_del.end())
								del_inc[0]=true;
							else
								to_del_st[0]=edg.nodes[a];
							keep_cnt++;
							break;
						case 2:
							prio+=99;
							to_del_st[0]=edg.nodes[a];
							break;
						default:
							prio+=9;
							to_rst_pr[0]=edg.nodes[a];
							break;
					}
				} else {
					prio+=999;
					to_del_st[0]=edg.nodes[a];
				}
				if(b==edg.nodes.size()-1) {
					auto& vert=model.vertices().at(edg.right);
					switch(vert.edges.size()) {
						case 0:
							assert(0);
							break;
						case 1:
							prio+=9999;
							if(nodes_to_del.find(edg.nodes[b])!=nodes_to_del.end())
								del_inc[1]=true;
							else
								to_del_st[1]=edg.nodes[b];
							keep_cnt++;
							break;
						case 2:
							prio+=99;
							to_del_st[1]=edg.nodes[b];
							break;
						default:
							prio+=9;
							to_rst_pr[1]=edg.nodes[b];
							break;
					}
				} else {
					prio+=999;
					to_del_st[1]=edg.nodes[b];
				}

				if(keep_cnt>=2) {
					edges_to_skip.insert(eid);
					for(auto n: edg.nodes)
						nodes_to_reset.insert(n);
					continue;
				}

				if(prio<=fut_prio)
					continue;

				fut=std::async(std::launch::deferred, [this,del_inc,to_del_st,to_rst_pr,eid=eid,a,b,&nodes_to_reset]() ->gapr::delta_del_patch_ {
					gapr::edge_model::reader model{_model};
					auto& edg=model.edges().at(eid);

					gapr::delta_del_patch_ delta;
					auto del_props=[&model,&delta](gapr::node_id nid) {
						for(auto& pid: model.props()) {
							if(pid.first.node==nid)
								delta.props.emplace_back(pid.first.node.data, pid.first.key);
						}
					};
					if(del_inc[0]) {
						delta.nodes.push_back(edg.nodes[a].data);
						del_props(edg.nodes[a]);
					}
					delta.nodes.push_back(edg.nodes[a].data);
					for(std::size_t i=a+1; i<b; i++) {
						delta.nodes.push_back(edg.nodes[i].data);
						del_props(edg.nodes[i]);
					}
					delta.nodes.push_back(edg.nodes[b].data);
					if(del_inc[1]) {
						delta.nodes.push_back(edg.nodes[b].data);
						del_props(edg.nodes[b]);
					}
					for(auto nid: to_del_st) {
						if(!nid)
							continue;
						gapr::prop_id pid{nid, "state"};
						auto it=model.props().find(pid);
						if(it!=model.props().end())
							delta.props.emplace_back(pid.node.data, pid.key);
					}

					for(auto nid: to_rst_pr) {
						if(!nid)
							continue;
						auto& vert=model.vertices().at(nid);
						for(auto [eid, dir]: vert.edges) {
							auto& edg=model.edges().at(eid);
							auto idx=dir?edg.nodes.size()-1:0;
							std::size_t aa=0;
							if(idx>=2)
								aa=idx-2;
							std::size_t bb=edg.nodes.size()-1;
							if(idx+2<edg.nodes.size())
								bb=idx+2;
							for(std::size_t i=aa; i<=bb; i++)
								nodes_to_reset.insert(edg.nodes[i]);
						}
					}
					std::sort(delta.props.begin(), delta.props.end());
					auto it=std::unique(delta.props.begin(), delta.props.end());
					delta.props.erase(it, delta.props.end());
					return delta;
				});
				fut_prio=prio;
			}
		}

		if(fut_prio<0)
			break;

		auto delta=fut.get();
		assert(!delta.nodes.empty());
		auto res=submit_commit(ctx, std::move(delta));
		if(!model_apply(true))
			throw gapr::reported_error{"failed to update model 2"};
		switch(res.first) {
			case SubmitRes::Retry:
			case SubmitRes::Deny:
				if(!res.second.empty()) {
					gapr::str_glue err{"failed to commit: ", res.second};
					throw gapr::reported_error{err.str()};
				}
				gapr::print("failed to commit");
				return -1;
				break;
			case SubmitRes::Accept:
				break;
		}

		select(ctx);
	} while(true);

	do {
		gapr::delta_reset_proofread_0_ delta;
		{
			gapr::edge_model::reader model{_model};
			for(auto& [eid, edg]: model.edges()) {
				for(unsigned int i=0; i<edg.nodes.size(); i++) {
					auto n=edg.nodes[i];
					if(auto it=nodes_to_reset.find(n); it!=nodes_to_reset.end()) {
						gapr::node_attr attr{edg.points[i]};
						if(attr.misc.coverage()) {
							delta.nodes.push_back(n.data);
							nodes_to_reset.erase(it);
						}
					}
				}
			}
		}
		if(delta.nodes.empty())
			break;
		auto res=submit_commit(ctx, std::move(delta));
		if(!model_apply(true))
			throw gapr::reported_error{"failed to update model 2"};
		switch(res.first) {
			case SubmitRes::Retry:
			case SubmitRes::Deny:
				if(!res.second.empty()) {
					gapr::str_glue err{"failed to commit: ", res.second};
					throw gapr::reported_error{err.str()};
				}
				gapr::print("failed to commit");
				return -1;
				break;
			case SubmitRes::Accept:
				break;
		}
	} while(false);

	{
		auto cli=std::move(_cur_conn);
	}
	return 0;
}

int Tracer::run_benchmark(gapr::fiber_ctx& ctx) {
	prepare(ctx);
	if(!load_commits(ctx, _cur_conn, _latest_commit))
		throw gapr::reported_error{"failed to load commits"};
	if(!model_apply(true))
		throw gapr::reported_error{"failed to update model"};

	std::mt19937 rng{std::random_device{}()};
	std::vector<gapr::node_id> todo;
	bool stop{false};
	auto t0=std::chrono::steady_clock::now();
	do {

		gapr::delta_proofread_ delta;
		do {
			gapr::edge_model::reader model{_model};
			if(todo.empty()) {
				gapr::node_id seed{};
				gapr::node_attr seedp;
				for(auto& [eid, edg]: model.edges()) {
					for(unsigned int i=0; i<edg.nodes.size(); i++) {
						auto id=edg.nodes[i];
						gapr::node_attr attr{edg.points[i]};
						if(!attr.misc.coverage()) {
							seed=id;
							seedp=attr;
							break;
						}
					}
					if(seed && rng()%1000==0)
						break;
				}
				if(!seed) {
					stop=true;
					break;
				}
				gapr::bbox_int bbox{};
				bbox.add(seedp.ipos);
				bbox.grow(1024*50);
				for(auto& [eid, edg]: model.edges()) {
					for(unsigned int i=0; i<edg.nodes.size(); i++) {
						auto id=edg.nodes[i];
						gapr::node_attr attr{edg.points[i]};
						if(!bbox.hit_test(attr.ipos))
							continue;
						if(!attr.misc.coverage())
							todo.push_back(id);
					}
				}
			}
			assert(!todo.empty());

			auto cur=todo.back();
			todo.pop_back();
			const gapr::edge_model::edge* edg{nullptr};
			unsigned int idx;
			auto pos=model.nodes().at(cur);
			if(pos.edge) {
				edg=&model.edges().at(pos.edge);
				idx=pos.index/128;
			} else {
				assert(pos.vertex);
				auto v=model.vertices().at(pos.vertex);
				for(auto [eid, dir]: v.edges) {
					edg=&model.edges().at(eid);
					idx=dir?edg->nodes.size()-1:0;
					break;
				}
			}
			if(gapr::node_attr attr{edg->points[idx]}; attr.misc.coverage())
				break;

			auto idx0=idx>10?idx-10:0u;
			auto idx1=idx+10<edg->nodes.size()?idx+10:edg->nodes.size();
			for(idx=idx0; idx<idx1; ++idx) {
				gapr::node_attr attr{edg->points[idx]};
				if(!attr.misc.coverage())
					delta.nodes.push_back(edg->nodes[idx].data);
			}
		} while(false);


		if(!delta.nodes.empty()) {
			auto t1=std::chrono::steady_clock::now();

			auto res=submit_commit(ctx, std::move(delta));
			if(!model_apply(true))
				throw gapr::reported_error{"failed to update model 2"};
			switch(res.first) {
				case SubmitRes::Retry:
				case SubmitRes::Deny:
					if(!res.second.empty()) {
						gapr::str_glue err{"failed to commit: ", res.second};
						throw gapr::reported_error{err.str()};
					}
					std::shuffle(todo.begin(), todo.end(), rng);
					break;
				case SubmitRes::Accept:
					break;
			}
			auto t2=std::chrono::steady_clock::now();
			fprintf(stderr, "dt: %u\n", (unsigned int)std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1).count());
			std::this_thread::sleep_until(t0+std::chrono::milliseconds{2000});
			t0=std::chrono::steady_clock::now();
		}

	} while(!stop);

	{
		auto cli=std::move(_cur_conn);
	}
	return 0;
}

bool Tracer::load_model_state(gapr::fiber_ctx& ctx, client_end msg) {
	//ensure conn
	gapr::fiber fib{ctx.get_executor(), api.get_model(msg)};
	auto cmt=std::move(fib).async_wait(gapr::yield{ctx});
	if(!cmt.file)
		return true;

	gapr::promise<uint64_t> prom{};
	gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
	auto ex1=_thr_pool.get_executor();
	ba::post(ex1, [this,prom=std::move(prom),file=std::move(cmt.file)]() mutable {
		auto ex1=_thr_pool.get_executor();
		assert(ex1.running_in_this_thread());
		(void)ex1;
		gapr::edge_model::loader loader{_model};
		auto sb=gapr::make_streambuf(std::move(file));
		auto r=loader.init(*sb);
		return std::move(prom).set(r);
	});
	auto r=std::move(fib2).async_wait(gapr::yield{ctx});
	if(!r)
		return false;
	_hist.body_count(r);
	return true;
}


constexpr static const char* opts=":c:e:E:j::";
constexpr static const struct option opts_long[]={
	{"config", 1, nullptr, 'c'},
	{"jobs", 2, nullptr, 'j'},
	{"maxiter", 1, nullptr, 1000+'M'},
	{"evaluator", 1, nullptr, 'e'},
	{"evaluate", 1, nullptr, 'E'},
	{"sample", 1, nullptr, 1000+'s'},
	{"train-resnet", 1, nullptr, 1000+'t'},
	{"train-unet", 1, nullptr, 1300+'t'},
	{"reset", 0, nullptr, 1000+'r'},
	{"reset-all", 0, nullptr, 1000+'R'},
	{"diff", 1, nullptr, 1000+'d'},
	{"near", 1, nullptr, 1001+'n'},
	{"distant", 1, nullptr, 1001+'d'},
	{"benchmark", 0, nullptr, 1100+'b'},
	{"import", 1, nullptr, 1200+'I'},
	{"import-fin", 1, nullptr, 1200+'F'},
	{"cube", 1, nullptr, 1500+'c'},
	{"help", 0, nullptr, 1000+'h'},
	{"version", 0, nullptr, 1000+'v'},
	{nullptr, 0, nullptr, 0}
};

static void usage(const char* argv0) {
	std::cout<<
		"usage: "<<argv0<<" [-c <cfg-file>] <repo>\n"
		"       trace the repository\n\n"
		"   or: "<<argv0<<" --help|--version\n"
		"       display this help or version information, and exit\n\n"
		"Automatic tracing (using neuTube algorithm).\n\n"
		"Options:\n"
		"   -c, --config <cfg-file> Load extra configuration file.\n\n"
		"Arguments:\n"
		"   <repo>               Upload to this repository.\n"
		"                        :format: [[user@]host:port/]repo-id\n\n"
		PACKAGE_NAME " " PACKAGE_VERSION "\n"
		"Homepage: <" PACKAGE_URL ">\n";
}
static void version() {
	std::cout<<
		"trace (" PACKAGE_NAME ") " PACKAGE_VERSION "\n"
		PACKAGE_COPYRIGHT "\n"
		"License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n"
		"This is free software: you are free to change and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.\n";
}

int train_evaluator(const std::filesystem::path& out_pars, int nsamp, char* samp_files[]);
int train_prober(const std::filesystem::path& out_pars, int ndata, char* data_files[]);

int trace_cube(const std::string& evaluator, const gapr::vec3<double>& res, const char* cubefn, const char* swcfn) {
	gapr::affine_xform xform{};
	gapr::cube cube{};
	gapr::edge_model mdl{};

	{
		for(unsigned int i=0; i<3; ++i)
			xform.direction[i*4]=res[i];
		xform.update_direction_inv();
		xform.update_resolution();

		fprintf(stderr, "load cube\n");
		auto cubef=gapr::make_streambuf(cubefn);
		if(!cubef)
			gapr::report("Cannot open file: ", cubefn);
		auto loader=gapr::make_cube_loader(cubefn, *cubef);
		if(!loader)
			gapr::report("Cannot open image: ", cubefn);
		gapr::cube_type type=loader->type();
		switch(type) {
		case gapr::cube_type::u8:
		case gapr::cube_type::u16:
		case gapr::cube_type::u32:
		case gapr::cube_type::i8:
		case gapr::cube_type::i16:
		case gapr::cube_type::i32:
		case gapr::cube_type::f32:
			break;
		default:
			gapr::report("Voxel type not supported");
		}
		std::array<unsigned int, 3> sizes;
		for(unsigned int i=0; i<3; ++i)
			sizes[i]=loader->sizes()[i];
		gapr::mutable_cube mcube{type, sizes};
		auto cube_view=mcube.view<char>();
		loader->load(cube_view.row(0, 0), cube_view.ystride(), cube_view.zstride());
		cube=std::move(mcube);
	}

	gapr::trace::ConnectAlg alg{"", xform, mdl};
	alg.evaluator(evaluator);

	gapr::trace::ConnectAlg::Job job{cube, {0,0,0}};
	std::atomic<bool> cancel;
	job.cancel=&cancel;
	job(alg);

	fprintf(stderr, "output swc\n");
	{
		std::ofstream ofs{swcfn};
		if(!ofs)
			gapr::report("Cannot open file: ", swcfn);
		gapr::swc_output swc{ofs};

		assert(job.delta.links.empty());
		auto& nodes=job.delta.nodes;
		for(unsigned int i=0; i<nodes.size(); ++i) {
			gapr::node_attr attr{nodes[i].first};
			for(unsigned int j=0; j<3; ++j)
				attr.pos(j, attr.pos(j)/res[j]);
			swc.node(gapr::node_id{i+1}, gapr::node_id{nodes[i].second}, attr);
		}
		ofs.close();
		if(!ofs)
			gapr::report("Failed to save file: ", swcfn);
	}
	return 0;
}

int main(int argc, char* argv[]) {
	gapr::cli_helper cli_helper{};

	Tracer::Args args;
	std::string cfg_file;
	std::filesystem::path train_resnet{};
	std::filesystem::path train_unet{};
	gapr::vec3<double> cube_res;
	bool trace_a_cube{false};
	do try {
		int opt;
		while((opt=getopt_long(argc, argv, opts, &opts_long[0], nullptr))!=-1) {
			switch(opt) {
				case 'c':
					cfg_file=optarg;
					break;
				case 'E':
					args.evaluator=optarg;
					args.evaluate=true;
					break;
				case 'e':
					args.evaluator=optarg;
					break;
				case 1000+'r':
					args.reset=true;
					break;
				case 1000+'R':
					args.reset=true;
					args.reset_all=true;
					break;
				case 1000+'d':
					if(std::string s{optarg}; !s.empty()) {
						args.reset_diffs.emplace_back(std::move(s));
						break;
					}
					throw gapr::reported_error{"filename cannot be empty"};
				case 1000+'s':
					args.sample=optarg;
					break;
				case 1000+'t':
					train_resnet=optarg;
					break;
				case 1300+'t':
					train_unet=optarg;
					break;
				case 'j':
					if(optarg) {
						char* eptr;
						errno=0;
						auto n=std::strtoul(optarg, &eptr, 10);
						if(errno!=0 || *eptr!='\0')
							throw gapr::reported_error{"unable to parse <jobs>"};
							//throw std::system_error{std::error_code{errno, std::generic_category()}, "wrong format"};
						args.jobs=n;
					} else {
						args.jobs=std::thread::hardware_concurrency();
					}
					break;
				case 1000+'M':
					{
						char* eptr;
						errno=0;
						auto n=std::strtoul(optarg, &eptr, 10);
						if(errno!=0 || *eptr!='\0')
							throw gapr::reported_error{"unable to parse <miter>"};
							//throw std::system_error{std::error_code{errno, std::generic_category()}, "wrong format"};
						if(n<=0)
							throw gapr::reported_error{"<miter> should be positive"};
						args.maxiter=n;
					}
					break;
				case 1001+'n':
					{
						char* eptr;
						errno=0;
						auto d=std::strtod(optarg, &eptr);
						if(errno!=0 || *eptr!='\0')
							throw gapr::reported_error{"unable to parse 'range1'"};
						if(d<=args.range0)
							throw gapr::reported_error{"requires 'range1' > 'range0'"};
						args.range1=d;
					}
					break;
				case 1001+'d':
					{
						char* eptr;
						errno=0;
						auto d=std::strtod(optarg, &eptr);
						if(errno!=0 || *eptr!='\0')
							throw gapr::reported_error{"unable to parse 'range0'"};
						if(d>=args.range1)
							throw gapr::reported_error{"requires 'range0' < 'range1'"};
						args.range0=d;
					}
					break;
				case 1100+'b':
					args.benchmark=true;
					break;
				case 1200+'I':
					args.from_swc=std::filesystem::u8path(optarg);
					break;
				case 1200+'F':
					args.from_swc=std::filesystem::u8path(optarg);
					args.from_swc_fin=true;
					break;
				case 1500+'c':
					if(gapr::parse_tuple(optarg, &cube_res[0], &cube_res[1], &cube_res[2])!=3)
						throw gapr::reported_error{"unable to parse resolution x:y:z"};
					trace_a_cube=true;
					break;
				case 1000+'h':
					usage(argv[0]);
					return EXIT_SUCCESS;
				case 1000+'v':
					version();
					return EXIT_SUCCESS;
				case '?':
					cli_helper.report_unknown_opt(argc, argv);
					break;
				case ':':
					cli_helper.report_missing_arg(argc, argv);
					break;
				default:
					cli_helper.report_unmatched_opt(argc, argv);
			}
		}
		if(!train_resnet.empty()) {
			if(optind+1>argc)
				throw gapr::reported_error{"argument <samp> missing"};
			break;
		}
		if(!train_unet.empty()) {
			if(optind+1>argc)
				throw gapr::reported_error{"argument <data> missing"};
			break;
		}
		if(trace_a_cube) {
			// XXX helper to handle this routine 
			if(optind+1>argc)
				throw gapr::reported_error{"argument <cube> missing"};
			if(optind+2>argc)
				throw gapr::reported_error{"argument <swc-out> missing"};
			if(optind+2<argc)
				throw gapr::reported_error{"too many arguments"};
			break;
		}
		/////////////////////////////
		if(optind+1>argc)
			throw gapr::reported_error{"argument <repo> missing"};
		if(optind+1<argc)
			throw gapr::reported_error{"too many arguments"};
		//
		gapr::parse_repo(argv[optind], args.user, args.host, args.port, args.group);
		gapr::parse_repo(argv[optind], args.user, args.host, args.port, args.group);

		int cfg_type=0;
		do {
			std::string fn;
			if(cfg_type) {
				// XXX
				fn="home/." PACKAGE_TARNAME "/config";
				if(!gapr::test_file('f', fn.c_str()))
					fn.clear();
			} else {
				fn=std::move(cfg_file);
			}
			if(fn.empty())
				continue;

			try {
				auto cfg=gapr::load_config(fn.c_str());
				if(args.host.empty() || args.port==0) {
					auto is=cfg.find("client.server");
					if(is!=cfg.end()) {
						auto i=is->second.rfind(':');
						if(i==std::string::npos)
							gapr::report("Failed to parse `client.server': ", is->second);
						if(i<=0)
							gapr::report("Failed to parse `client.server', empty HOST: ", is->second);
						if(i+1>=is->second.length())
							gapr::report("Failed to parse `client.server', empty PORT: ", is->second);
						args.host=is->second.substr(0, i);
						args.port=gapr::parse_port(&is->second[i+1], is->second.size()-i-1);
					}
				}
				if(args.user.empty()) {
					auto iu=cfg.find("client.user");
					if(iu!=cfg.end())
						args.user=std::move(iu->second);
				}
				if(args.passwd.empty()) {
					auto ip=cfg.find("client.password");
					if(ip!=cfg.end())
						args.passwd=std::move(ip->second);
				}
			} catch(const std::exception& e) {
				gapr::str_glue err{"in config file: ", e.what()};
				throw gapr::reported_error{err.str()};
			}
		} while(++cfg_type<2 &&
				(args.host.empty()||args.port==0||args.user.empty()||args.passwd.empty()));

		if(args.host.empty())
			throw gapr::reported_error{"in <repo>: missing HOST"};
		if(args.port==0)
			throw gapr::reported_error{"in <repo>: missing PORT"};
		if(args.user.empty())
			throw gapr::reported_error{"in <repo>: missing USER"};

	} catch(const gapr::reported_error& e) {
		gapr::str_glue msg{"error: ", e.what(), '\n',
			"try `", argv[0], " --help", "' for more information.\n"};
		std::cerr<<msg.str();
		return EXIT_FAILURE;
	} while(false);

	if(!train_resnet.empty())
		return train_evaluator(train_resnet, argc-optind, argv+optind);
	if(!train_unet.empty())
		return train_prober(train_unet, argc-optind, argv+optind);
	if(trace_a_cube)
		return trace_cube(args.evaluator, cube_res, argv[optind], argv[optind+1]);

	try {

		Tracer tracer{std::move(args)};
		return tracer.run();

	} catch(const gapr::reported_error& e) {
		gapr::str_glue msg{"fatal: ", e.what(), '\n'};
		std::cerr<<msg.str();
		return EXIT_FAILURE;
	} catch(const gapr::CliErrorMsg& e) {
		gapr::CliErrorMsg msg{"error: ", e.message(), '\n'};
		std::cerr<<msg.message();
		return EXIT_FAILURE;
	}

}

