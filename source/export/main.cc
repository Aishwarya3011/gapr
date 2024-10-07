/* export/main.cc
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


#include "gapr/edge-model.hh"

#include "gapr/archive.hh"
#include "gapr/utility.hh"
#include "gapr/fiber.hh"
#include "gapr/exception.hh"
#include "gapr/str-glue.hh"
#include "gapr/connection.hh"
#include "gapr/parser.hh"
#include "gapr/trace-api.hh"
#include "gapr/ask-secret.hh"
#include "gapr/swc-helper.hh"
#include "gapr/streambuf.hh"

#include <fstream>
#include <iostream>
#include <cassert>
#include <optional>
#include <variant>
#include <filesystem>

#include <boost/asio/thread_pool.hpp>
#include <boost/asio/strand.hpp>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include <getopt.h>

#include "config.hh"

#include "dup/serialize-delta.hh"
#include "../corelib/parse-helper.hh"

namespace ba=boost::asio;
namespace bs=boost::system;
using gapr::client_end;

namespace {
	class Exporter {
		public:
			struct Args {
				std::string user{};
				std::string passwd{};
				std::string host{};
				unsigned short port{0};
				std::string group{};

				std::string file;
				uint32_t num{0};
				bool split{false};
				bool raw{false};
				bool gzip{false};
				//////////////////////////
				//std::vector<std::string> files;
				//???bool force{false};
			};
			explicit Exporter(Args&& args);
			~Exporter();
			Exporter(const Exporter&) =delete;
			Exporter& operator=(const Exporter&) =delete;

			int run();

		private:
			Args _args;

			ba::io_context _io_ctx{1};
			ba::thread_pool _thr_pool{1+1};
			ba::ssl::context _ssl_ctx{ba::ssl::context::tls};
			using input_strand=ba::strand<ba::thread_pool::executor_type>;
			input_strand _inp_strand{_thr_pool.get_executor()};

			using resolver=ba::ip::tcp::resolver;
			std::optional<resolver> _resolver;
			resolver::results_type _addrs{};
			resolver::endpoint_type _addr{ba::ip::address{}, 0};
			client_end _cur_conn{};
			std::string _srv_info;
			std::string _gecos;
			std::string _data_secret;
			uint64_t _latest_commit;

			gapr::trace_api api;
			gapr::edge_model _model;
			gapr::commit_history _hist;

			int run_impl(gapr::fiber_ctx& ctx);
			int run_impl_raw(gapr::fiber_ctx& ctx);
			void prepare(gapr::fiber_ctx& ctx);
			bool load_commits(gapr::fiber_ctx& ctx, client_end msg, uint64_t upto);
			bool load_model_state(gapr::fiber_ctx& ctx, client_end msg);
			bool model_apply(bool last);
			std::string get_passwd(gapr::fiber_ctx& ctx, std::error_code& ec);
			void select(gapr::fiber_ctx& ctx);
			[[maybe_unused]] bool model_prepare(gapr::mem_file file);
			void save_swc();
			void save_fnt();
			void save_swc_split();
			void save_raw(gapr::mem_file&& commits);
			std::string swc_comment() {
				std::ostringstream oss;
				oss<<"# Repo: "<<_args.host<<':'<<_args.port<<'/'<<_args.group<<'\n';
				oss<<"# Commits: "<<_hist.body_count()<<"\n\n";
				return oss.str();
			}
#if 0

			ba::deadline_timer _timer{_io_ctx};
			std::atomic<bool> _cancel_flag;


			bool get_retry(gapr::fiber_ctx& ctx);
#endif
	};
}
//////////////////////////////////////////////////
#if 0
class Importer {
///////////////////////////////////////////////


	public:
	private:

		//using steady_timer=ba::basic_waitable_timer<std::chrono::steady_clock>;
		//steady_timer _timer{_io_ctx};
};
#endif




#if 0
class Importer {
	void upload_cb(int64_t tip) {
#if 0
		switch(...) {
			case ok large: case cando large:
				"OK";
				apply;
				delta_idx++;
				upload_delta_again();
				...;
			case cannot do 0:
				"NO";
				not possible;
				...;
			case cannot safely do small:
				"NO ID";
				client.fetch(id, fetch_finish, fetch_error);
				break;
			case cando, update medium:
				"OK ID";
				pull_new(pull_cb_2)
					...;
			case other:
				conn->reset();
				retry_until...;
		}
#endif
	}
};
#endif
///////////////////////////////////////////////
Exporter::Exporter(Args&& args): _args{std::move(args)}
{ }
Exporter::~Exporter() { }

int Exporter::run() {
	gapr::fiber fib{_io_ctx.get_executor(), [this](gapr::fiber_ctx& ctx) -> int {
		try {
			if(_args.raw)
				return run_impl_raw(ctx);
			return run_impl(ctx);
		} catch(const gapr::reported_error& e) {
			gapr::str_glue msg{"fatal: ", e.what(), '\n'};
			std::cerr<<msg.str();
			return EXIT_FAILURE;
		}
	}};
	auto c=_io_ctx.run();
	gapr::print("stopped normally, ", c, " events.");
	_thr_pool.join();
	return EXIT_SUCCESS;
}

int Exporter::run_impl(gapr::fiber_ctx& ctx) {
	prepare(ctx);
	if(_args.num>0) {
		if(_args.num>_latest_commit) {
			gapr::str_glue err{_latest_commit, " commits available, but ",
				_args.num, " requested"};
			throw gapr::reported_error{err.str()};
		}
	} else {
		if(_latest_commit<=0)
			throw gapr::reported_error{"repository is empty"};
		_args.num=_latest_commit;
		if(!load_model_state(ctx, _cur_conn))
			throw gapr::reported_error{"failed to load model state"};
	}

	if(!load_commits(ctx, _cur_conn, _args.num))
		throw gapr::reported_error{"failed to load commits"};
	if(1) {
		_cur_conn.~client_end();
		new(&_cur_conn) client_end{};
	}
	if(!model_apply(true))
		throw gapr::reported_error{"failed to update model"};

	/*! should be able to export as is */
	if(1)
		_model.apply_attach();

	{
		gapr::promise<bool> prom;
		gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
		auto ex1=_thr_pool.get_executor();
		ba::post(ex1, [this,prom=std::move(prom)]() mutable {
			try {
				if(_args.split)
					save_swc_split();
				else if(1) save_swc();
				else save_fnt();
				std::move(prom).set(true);
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});
	}

	return 0;
}

int Exporter::run_impl_raw(gapr::fiber_ctx& ctx) {
	prepare(ctx);
	if(_args.num>0) {
		if(_args.num>_latest_commit) {
			gapr::str_glue err{_latest_commit, " commits available, but ",
				_args.num, " requested"};
			throw gapr::reported_error{err.str()};
		}
	} else {
		if(_latest_commit<=0)
			throw gapr::reported_error{"repository is empty"};
		_args.num=_latest_commit;
	}

	gapr::mem_file commits_file;
	{
		gapr::promise<gapr::mem_file> prom{};
		gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
		auto ex1=_thr_pool.get_executor();
		ba::post(ex1, [prom=std::move(prom)]() mutable {
			gapr::mem_file hist_data;
			try {
				gapr::commit_history empty_hist{};
				hist_data=serialize(empty_hist);
				std::move(prom).set(std::move(hist_data));
			} catch(const std::runtime_error& e) {
				return unlikely(std::move(prom), std::current_exception());
			}
		});
		auto hist_data=std::move(fib2).async_wait(gapr::yield{ctx});
		gapr::fiber fib{ctx.get_executor(), api.get_commits(_cur_conn, std::move(hist_data), _args.num)};
		auto cmts=std::move(fib).async_wait(gapr::yield{ctx});
		commits_file=std::move(cmts.file);
	}
	if(1) {
		_cur_conn.~client_end();
		new(&_cur_conn) client_end{};
	}

	{
		gapr::promise<bool> prom;
		gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
		auto ex1=_thr_pool.get_executor();
		ba::post(ex1, [this,prom=std::move(prom),commits_file=std::move(commits_file)]() mutable {
			try {
				save_raw(std::move(commits_file));
				std::move(prom).set(true);
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});
	}
	return 0;
}
void Exporter::save_raw(gapr::mem_file&& commits) {
	auto strmbuf=gapr::make_streambuf(std::move(commits));
	gapr::archive arch{_args.file.c_str()};
	std::array<char, 32> fnbuf;
	for(uint32_t i=0; i<_args.num; ++i) {
		auto pos0=strmbuf->pubseekoff(0, std::ios::cur);
		assert(pos0!=-1);
		gapr::commit_info info;
		if(!info.load(*strmbuf))
			gapr::report("commit file no commit info");
		gapr::delta_variant::visit<void>(gapr::delta_type{info.type},
				[&info,strmbuf=strmbuf.get()](auto typ) {
					gapr::delta<typ> delta;
					if(!gapr::load(delta, *strmbuf))
						gapr::report("commit file no delta");
				});
		auto pos1=strmbuf->pubseekoff(0, std::ios::cur);
		assert(pos1!=-1);
		strmbuf->pubseekpos(pos0);

		auto ofs=arch.get_writer(gapr::to_string_lex(fnbuf, i));
		if(!ofs)
			gapr::report("failed to open file");
		while(pos0<pos1) {
			auto [buf, siz]=ofs.buffer();
			if(!buf)
				gapr::report("failed to alloc buffer");
			if(siz+pos0>static_cast<std::size_t>(pos1))
				siz=pos1-pos0;
			strmbuf->sgetn(buf, siz);
			ofs.commit(siz);
			pos0+=siz;
		}
		if(!ofs.flush())
			gapr::report("failed to close file");
	}
}

bool Exporter::load_model_state(gapr::fiber_ctx& ctx, client_end msg) {
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

bool Exporter::load_commits(gapr::fiber_ctx& ctx, client_end msg, uint64_t upto) {
	if(_hist.body_count()>=upto)
		return true;
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
		gapr::fiber fib{ctx.get_executor(), api.get_commits(msg, std::move(hist_data), upto)};
		auto cmts=std::move(fib).async_wait(gapr::yield{ctx});
		commits_file=std::move(cmts.file);
	}

	auto strmbuf=gapr::make_streambuf(std::move(commits_file));

	while(_hist.body_count()<upto) {
		gapr::promise<uint64_t> prom{};
		gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
		auto ex1=_thr_pool.get_executor();
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
		_hist.body_count(next_id+1);
	}
	return true;
}

bool Exporter::model_apply(bool last) {
	auto ex2=_io_ctx.get_executor();
	assert(ex2.running_in_this_thread());
	(void)ex2;
	gapr::print("model apply");

	gapr::edge_model::updater updater{_model};
	if(!updater.empty()) {
		if(!updater.apply())
			return false;
	}
	return true;
}
bool Exporter::model_prepare(gapr::mem_file file) {
	auto ex1=_thr_pool.get_executor();
	assert(ex1.running_in_this_thread());
	(void)ex1;
	gapr::print("model prepare");

	gapr::edge_model::loader loader{_model};
	return loader.load_file(std::move(file));
}

void Exporter::prepare(gapr::fiber_ctx& ctx) {
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
		gapr::str_glue err{"login error: ", res.gecos, '\n'};
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

}

std::string Exporter::get_passwd(gapr::fiber_ctx& ctx, std::error_code& ec) {
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

void Exporter::select(gapr::fiber_ctx& ctx) {
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
	_latest_commit=res.commit_num;
#if 0
	if(...==MAX)
		err;
#endif
}

template<typename Helper>
static void save_swc_impl(Helper& swc, const gapr::edge_model& _model) {
	gapr::edge_model::reader model{_model};
	auto N=model.nodes().size();
	std::vector<gapr::node_id> todo;
	todo.reserve(N*2);
	std::unordered_map<gapr::node_id, bool> finished;
	finished.reserve(N);
	for(auto& [nid, pos]: model.nodes())
		todo.push_back(nid);
	for(auto& [pid, prop]: model.props()) {
		if(pid.key=="root") {
			todo.push_back(pid.node);
			finished.emplace(pid.node, false);
		}
	}
	auto greater=[](auto a, auto b) {
		return a.data>b.data;
	};
	std::sort(todo.begin(), todo.begin()+N, greater);
	std::sort(todo.begin()+N, todo.end(), greater);

	std::vector<gapr::node_id> neighbors;
	while(!todo.empty()) {
		auto id=todo.back();
		todo.pop_back();
		auto [it, ins]=finished.emplace(id, true);
		bool is_root{false};
		if(!ins) {
			if(it->second)
				continue;
			is_root=true;
			it->second=true;
		}

		gapr::node_attr nattr;
		auto pos=model.nodes().at(id);
		if(pos.edge) {
			auto& edg=model.edges().at(pos.edge);
			auto i=pos.index/128;
			gapr::node_attr attr{edg.points[i]};
			neighbors.emplace_back(edg.nodes[i+1]);
			neighbors.emplace_back(edg.nodes[i-1]);
			nattr=attr;
		} else if(pos.vertex) {
			auto& vert=model.vertices().at(pos.vertex);
			nattr=vert.attr;
			for(auto [eid, dir]: vert.edges) {
				auto& edg=model.edges().at(eid);
				std::size_t nidx{1};
				if(dir)
					nidx=edg.nodes.size()-2;
				neighbors.emplace_back(edg.nodes[nidx]);
			}
		} else {
			assert(0);
		}
		std::sort(neighbors.begin(), neighbors.end(), [](auto a, auto b) {
			return a.data<b.data;
		});
		nattr.misc=nattr.misc.cannolize();

		bool node_written{false};
		bool self_loop{false};
		if(is_root) {
			/*! root node can have type !=1.  eg: root at axon initiation */
			swc.node(id, gapr::node_id{}, nattr);
			node_written=true;
		}
		while(!neighbors.empty()) {
			auto id2=neighbors.back();
			neighbors.pop_back();

			if(id2==id) {
				self_loop=true;
			} else {
				auto it2=finished.find(id2);
				if(it2==finished.end()) {
					todo.push_back(id2);
				} else if(it2->second==true) {
					if(node_written) {
						swc.loop(id, id2);
					} else {
						swc.node(id, id2, nattr);
						node_written=true;
					}
				}
			}
		}
		if(!node_written) {
			/*! non-root nodes can have type ==1.  eg: nodes to fill soma */
			swc.node(id, gapr::node_id{}, nattr);
		}
		if(self_loop)
			swc.loop(id, id);
	}

	std::vector<decltype(model.props().begin())> prop_its;
	for(auto it=model.props().begin(); it!=model.props().end(); ++it)
		prop_its.push_back(it);
	std::sort(prop_its.begin(), prop_its.end(), [](auto& a, auto& b) {
		if(a->first.node.data<b->first.node.data)
			return false;
		if(a->first.node.data>b->first.node.data)
			return true;
		return a->first.key>b->first.key;
	});

	while(!prop_its.empty()) {
		auto it=prop_its.back();
		prop_its.pop_back();
		swc.annot(it->first.node, it->first.key.c_str(), it->second.c_str());
	}
}

void Exporter::save_swc() {
	std::ofstream ofs{_args.file};
	if(!ofs)
		throw std::ios_base::failure{"failed to open file"};

	boost::iostreams::filtering_ostream filter{};
	if(_args.gzip)
		filter.push(boost::iostreams::gzip_compressor{4});
	filter.push(ofs);

	gapr::swc_output swc{filter};
	swc.header();
	swc.comment(swc_comment().c_str());
	save_swc_impl(swc, _model);

	filter.reset();
	ofs.close();
	if(!ofs)
		throw std::ios_base::failure{"failed to close file"};
}

static std::string filename_safe(std::string_view str) {
	const char* hexchrs="0123456789ABCDEF";
	std::string out;
	out.reserve(str.size());
	for(auto c: str) {
		if(std::strchr("/<>:\"\\|?*", c)!=nullptr) {
			out+='%';
			out+=hexchrs[(c>>4)&0xf];
			out+=hexchrs[c&0xf];
			continue;
		}
		out+=c;
	}
	return out;
}

void Exporter::save_swc_split() {

	struct Cache {
		struct Annot {
			std::string key, val;
		};
		struct Node {
			gapr::node_id par;
			gapr::node_attr attr;
		};
		struct Item {
			gapr::node_id id;
			std::variant<Annot,gapr::node_id,Node> var;
		};
		struct Replay {
			gapr::swc_output& swc;
			gapr::node_id id;
			void operator()(const Annot& annot) {
				swc.annot(id, annot.key.c_str(), annot.val.c_str());
			}
			void operator()(const Node& node) {
				swc.node(id, node.par, node.attr);
			}
			void operator()(gapr::node_id id2) {
				swc.loop(id, id2);
			}
		};
		using OptName=std::optional<std::string>;
		std::vector<Item> items;
		std::unordered_map<gapr::node_id, gapr::node_id> node2root{};
		std::unordered_map<gapr::node_id, OptName> root2name;
		std::vector<std::pair<gapr::node_id, gapr::node_id>> loops;
		std::unordered_set<gapr::node_id> raised_roots;

		void annot(gapr::node_id id, const char* key, const char* val) {
			Annot annot{key, val};
			if(annot.key=="root") {
				auto& name=root2name.at(id);
				assert(!name.has_value());
				name.emplace(annot.val);
			} else if(annot.key=="raise") {
				raised_roots.emplace(node2root.at(id));
			}
			items.emplace_back(Item{id, std::move(annot)});
		}
		void loop(gapr::node_id id, gapr::node_id id2) {
			gapr::node_id a=node2root.at(id), b=node2root.at(id2);
			loops.emplace_back(a, b);
			loops.emplace_back(b, a);
			items.emplace_back(Item{id, id2});
		}
		void node(gapr::node_id id, gapr::node_id par, gapr::node_attr attr) {
			gapr::node_id root;
			if(par) {
				root=node2root.at(par);
			} else {
				root=id;
				auto [it, ins]=root2name.emplace(id, OptName{});
				assert(ins);
			}
			auto [it, ins]=node2root.emplace(id, root);
			assert(ins);
			items.emplace_back(Item{id, Node{par, attr}});
		}
	};

	Cache cache;
	save_swc_impl(cache, _model);

	gapr::node_id node0a{}, node0b{};
	for(auto& [id, name]: cache.root2name) {
		if(!name.has_value()) {
			bool raised{false};
			if(cache.raised_roots.find(id)!=cache.raised_roots.end())
				raised=true;
			[&cache](auto& ref, auto id) {
				if(!ref) {
					ref=id;
					return;
				}
				cache.loops.emplace_back(id, ref);
				cache.loops.emplace_back(ref, id);
			}(raised?node0a:node0b, id);
		}
	}

	struct Group {
		gapr::node_id id;
		std::size_t cnt;
		std::string name;
		int order;
		std::size_t node_cnt{0};
	};
	std::vector<Group> groups;
	std::unordered_map<gapr::node_id, unsigned int> root2grpidx;
	while(!cache.root2name.empty()) {
		std::unordered_set<gapr::node_id> roots;
		std::vector<gapr::node_id> todo;
		auto r0=cache.root2name.begin()->first;
		roots.emplace(r0);
		todo.push_back(r0);
		do {
			auto rr=todo.back();
			todo.pop_back();
			for(auto [id1, id2]: cache.loops)
				if(id1==rr) {
					auto [it, ins]=roots.emplace(id2);
					if(ins)
						todo.push_back(id2);
				}
		} while(!todo.empty());

		int order_d=0;
		if(!cache.raised_roots.empty()) {
			unsigned int hit=0;
			for(auto n: roots) {
				if(cache.raised_roots.find(n)!=cache.raised_roots.end())
					++hit;
			}
			if(!hit)
				order_d=1000;
		}

		unsigned int idx=groups.size();
		auto& grp=groups.emplace_back(Group{r0, roots.size()});
		if(roots.find(node0a)!=roots.end()) {
			grp.order=10+order_d;
		} else if(roots.find(node0b)!=roots.end()) {
			grp.order=10+order_d;
		} else if(roots.size()>1) {
			grp.order=5+order_d;
		} else {
			grp.order=0+order_d;
			auto& name=cache.root2name.begin()->second;
			grp.name=*name;
		}

		for(auto id: roots) {
			cache.root2name.erase(id);
			root2grpidx.emplace(id, idx);
		}
	}

	for(auto& [id, root]: cache.node2root) {
		auto& grp=groups[root2grpidx.at(root)];
		root=grp.id;
		++grp.node_cnt;
	}
	std::sort(groups.begin(), groups.end(), [](auto& a, auto& b) {
		if(a.order<b.order)
			return true;
		if(a.order>b.order)
			return false;
		return a.node_cnt>b.node_cnt;
	});

	std::error_code ec;
	std::filesystem::create_directory(_args.file, ec);
	if(ec)
		throw std::system_error{ec};

	for(std::size_t idx=0; idx<groups.size(); idx++) {
		auto& grp=groups[idx];
		auto id=grp.id;
		std::ostringstream oss;
		oss<<_args.file<<'/';
		if(grp.order>=1000)
			oss<<"999";
		oss.width(3);
		oss.fill('0');
		oss<<idx;
		if(grp.order%1000==10) {
			oss<<"-JUNK";
		} else if(grp.cnt>1) {
			oss<<'-'<<grp.cnt<<"-TREES";
		} else {
			oss<<'@'<<filename_safe(grp.name);
		}
		oss<<".swc";
		if(_args.gzip)
			oss<<".gz";
		auto fn=oss.str();

		std::ofstream ofs{fn};
		if(!ofs)
			throw std::ios_base::failure{"failed to open file"};

		boost::iostreams::filtering_ostream filter{};
		if(_args.gzip)
			filter.push(boost::iostreams::gzip_compressor{4});
		filter.push(ofs);

		gapr::swc_output swc{filter};
		swc.header();
		swc.comment(swc_comment().c_str());
		for(auto& item: cache.items) {
			if(item.id && cache.node2root.at(item.id)==id) {
				std::visit(Cache::Replay{swc, item.id}, item.var);
				item.id=gapr::node_id{};
			}
		}

		filter.reset();
		ofs.close();
		if(!ofs)
			throw std::ios_base::failure{"failed to close file"};
	}
}

void Exporter::save_fnt() {
	gapr::edge_model::reader model{_model};
	std::ofstream ofs{_args.file};
	if(!ofs)
		throw std::ios_base::failure{"failed to open file"};

	//ofs.precision(13);
	//ofs.setf(std::ios::scientific);
	ofs<<"Fast Neurite Tracer Session File 1.0\nNONE\n";
	if(!ofs)
		gapr::report("Failed to write line");
	ofs<<"BEGIN_TRACING_DATA\n";
	if(!ofs)
		gapr::report("Failed to write data start mark");

	auto& edges=model.edges();
	std::unordered_map<gapr::node_id, std::size_t> vid2idx;
	std::vector<gapr::node_id> vids;
	for(auto& [eid, edg]: edges) {
		for(auto vid: {edg.left, edg.right}) {
			auto [it, ins]=vid2idx.try_emplace(vid, vids.size());
			if(ins)
				vids.push_back(vid);
		}
	}

	ofs<<vids.size()<<'\n';
	if(!ofs)
		gapr::report("Failed to write header");
	for(std::size_t i=0; i<vids.size(); i++) {
		auto vid=vids[i];
		auto& vert=model.vertices().at(vid);
		bool fin{false};
		if(model.props().find({vid, "state"})!=model.props().end())
			fin=true;
		assert(vert.edges.size()>0);
		if(vert.edges.size()>1)
			fin=true;
		ofs<<fin<<'\n';
		if(!ofs)
			gapr::report("Failed to write header");
	}

	ofs<<edges.size()<<'\n';
	if(!ofs)
		gapr::report("Failed to write header");
	for(auto& [eid, edg]: edges) {
		ofs<<0<<' ';
		ofs<<vid2idx.at(edg.left)<<' ';
		ofs<<vid2idx.at(edg.right)<<' ';
		ofs<<edg.points.size()<<'\n';
		if(!ofs)
			gapr::report("Failed to write header");
		for(size_t j=0; j<edg.points.size(); j++) {
			gapr::node_attr p{edg.points[j]};
			ofs<<p.misc.t()<<' '<<p.ipos[0]/4<<' '<<p.ipos[1]/4<<' '<<p.ipos[2]/4<<' '<<int(p.misc.r()*256)<<'\n';
			if(!ofs)
				gapr::report("Failed to write header");
		}
	}

	std::size_t nt{0};
	for(auto& [pid, pval]: model.props()) {
		if(pid.key=="root") {
			auto it=vid2idx.find(pid.node);
			if(it!=vid2idx.end()) {
				nt++;
			}
		}
	}
	ofs<<nt<<'\n';
	if(!ofs)
		gapr::report("Failed to write header");
	for(auto& [pid, pval]: model.props()) {
		if(pid.key=="root") {
			auto it=vid2idx.find(pid.node);
			if(it!=vid2idx.end()) {
				ofs<<it->second<<' '<<(pval.empty()?"Unnamed":pval)<<'\n';
				if(!ofs)
					gapr::report("Failed to write header");
			}
		}
	}
	ofs.close();
	if(!ofs)
		throw std::ios_base::failure{"failed to close file"};
}


////////////////////////////////////
//namespace ba=boost::asio;
//namespace bs=boost::system;

//using gapr::client_end;
//using resolver=ba::ip::tcp::resolver;





#if 0

bool Tracer::get_retry(gapr::fiber_ctx& ctx) {
	std::cerr<<"retry? (y/n): ";
	gapr::promise<bool> prom{};
	auto fut=prom.get_future();
	ba::post(_inp_strand, [/*this,wg=make_work_guard(_io_ctx)*/prom=std::move(prom)]() mutable {
		try {
			return std::move(prom).set(true);
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
			unlikely(std::move(prom), std::io_errc::stream);
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








#endif



constexpr static const char* opts=":c:n:sz";
constexpr static const struct option opts_long[]={
	{"config", 1, nullptr, 'c'},
	{"split", 0, nullptr, 's'},
	{"raw", 0, nullptr, 1000+'r'},
	{"help", 0, nullptr, 1000+'h'},
	{"version", 0, nullptr, 1000+'v'},
	{nullptr, 0, nullptr, 0}
	// XXX -f, --force? add_opt_force()
};

static void usage(const char* argv0) {
	std::cout<<
		"usage: "<<argv0<<" [-c <cfg-file>] <repo> { <swc-file> | -s <dir> }\n"
		"       export the repository\n\n"
		"   or: "<<argv0<<" --help|--version\n"
		"       display this help or version information, and exit\n\n"
		"Export repository.\n\n"
		"Options:\n"
		"   -n           <num>   Export exactly <num> commits.\n"
		"   -s, --split  <dir>   Split and write to directory <dir>.\n"
		"   -c, --config <cfg-file> Load extra configuration file.\n\n"
		"Arguments:\n"
		"   <repo>               Export this repository.\n"
		"                        :format: [[user@]host:port/]repo-id\n"
		"   <swc-file>           Write to this file.\n\n"
		PACKAGE_NAME " " PACKAGE_VERSION "\n"
		"Homepage: <" PACKAGE_URL ">\n";
}
static void version() {
	std::cout<<
		"export (" PACKAGE_NAME ") " PACKAGE_VERSION "\n"
		PACKAGE_COPYRIGHT "\n"
		"License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n"
		"This is free software: you are free to change and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.\n";
}

int main(int argc, char* argv[]) {
	gapr::cli_helper cli_helper{};

	Exporter::Args args;
	std::string cfg_file;
	try {
		int opt;
		while((opt=getopt_long(argc, argv, opts, &opts_long[0], nullptr))!=-1) {
			switch(opt) {
				case 'c':
					cfg_file=optarg;
					break;
				case 'n':
					{
						char* eptr;
						errno=0;
						auto n=std::strtoul(optarg, &eptr, 10);
						if(errno!=0 || *eptr!='\0')
							throw gapr::reported_error{"unable to parse <num>"};
							//throw std::system_error{std::error_code{errno, std::generic_category()}, "wrong format"};
						if(n<=0)
							throw gapr::reported_error{"<num> should be positive"};
						args.num=n;
					}
					break;
				case 's':
					args.split=true;
					break;
				case 'z':
					args.gzip=true;
					break;
				case 1000+'r':
					args.raw=true;
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
		if(optind+1>argc)
			throw gapr::reported_error{"argument <repo> missing"};
		if(optind+2<argc)
			throw gapr::reported_error{"too many arguments"};
		gapr::parse_repo(argv[optind], args.user, args.host, args.port, args.group);
		if(optind+2>argc) {
			if(args.split)
				throw gapr::reported_error{"argument <dir> missing"};
			if(args.raw)
				throw gapr::reported_error{"argument <raw-file> missing"};
			throw gapr::reported_error{"argument <swc-file> missing"};
		}
		args.file=argv[optind+1];

		load_configs(cfg_file, {
			host_port_cfg("client.server", args.host, args.port),
			string_cfg("client.user", args.user),
			string_cfg("client.password", args.passwd),
		});

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
	}

	try {
#if 0
		// XXX
		if(gapr::test_file('e', _args->file.c_str())) {
			if(!_args->force) {
				// XXX prompt;
			}
		}
#endif

		Exporter exporter{std::move(args)};
		return exporter.run();

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

