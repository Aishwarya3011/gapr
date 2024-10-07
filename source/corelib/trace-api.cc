#include "gapr/trace-api.hh"

#include "gapr/connection.hh"
#include "gapr/parser.hh"
#include "gapr/str-glue.hh"
#include "gapr/mem-file.hh"
#include "gapr/fix-error-code.hh"

#include <chrono>


using tapi=gapr::trace_api;
namespace bs=boost::system;
static void async_write_mem_file_impl(gapr::client_end& cli, gapr::mem_file&& file, std::size_t n) {
	auto buf=file.map(n);
	gapr::print("send map: ", buf.size());
	if(buf.size()>0) {
		cli.async_write(buf, false, [n,cli,file=std::move(file)](bs::error_code ec, std::size_t nbytes) mutable {
			if(ec)
				return;
			async_write_mem_file_impl(cli, std::move(file), n+nbytes);
		});
	} else {
		cli.async_write(buf, true, [cli](bs::error_code ec, std::size_t nbytes) {
		});
	}
}
static void async_write_mem_file(gapr::client_end& cli, gapr::mem_file&& file) {
	async_write_mem_file_impl(cli, std::move(file), 0);
}

gapr::future<tapi::handshake_result> tapi::handshake(client_end& cli) {
	gapr::promise<handshake_result> prom{};
	auto fut=prom.get_future();
	cli.async_handshake([prom=std::move(prom)](bs::error_code ec, const client_end::msg_hdr_in& hdr) mutable {
		if(ec)
			return unlikely(std::move(prom), to_std_error_code(ec));
		try {
			do {
				auto args=hdr.args();
#if 0
				//if(!hdr.tag_is("gapr.srv"))
					//break;
#endif
				unsigned int ver;
				auto r=gapr::make_parser(ver).from_dec(args.data(), args.size());
				if(!r.second)
					break;
				if(ver!=0/*XXX*/) {
					gapr::str_glue err{"wrong protocol version: ", ver};
					throw std::runtime_error{err.str()};
				}
				std::string info{};
				if(r.first<args.size()) {
					if(args.data()[r.first]!=':')
						break;
					info=std::string{&args.data()[r.first+1], args.size()-r.first-1};
				}
				std::move(prom).set(handshake_result{ver, std::move(info)});
				return;
			} while(false);
			gapr::str_glue err{"invalid reply: ", std::string{hdr.line_ptr(), hdr.line_len()}};
			throw std::runtime_error{err.str()};
		} catch(const std::runtime_error& e) {
			unlikely(std::move(prom), std::current_exception());
		}
	});
	return fut;
}
gapr::future<tapi::login_result> tapi::login(client_end& cli, const std::string& usr, const std::string& pw) {
	gapr::promise<login_result> prom{};
	auto fut=prom.get_future();
	client_end::msg_hdr hdr{"LOGIN", usr, pw};
	cli.async_send(std::move(hdr), [cli,prom=std::move(prom)](bs::error_code ec) mutable {
		if(ec)
			return unlikely(std::move(prom), to_std_error_code(ec));
		cli.async_recv([prom=std::move(prom)](bs::error_code ec, const client_end::msg_hdr_in& hdr) mutable {
			if(ec)
				return unlikely(std::move(prom), to_std_error_code(ec));
			try {
				do {
					auto args=hdr.args();
					if(hdr.tag_is("NO")) {
						if(!args.size())
							break;
						return std::move(prom).set(login_result{gapr::tier::nobody, std::string{args.data(), args.size()}});
					}
					if(hdr.tag_is("OK")) {
						unsigned int tier;
						if(!args.size())
							break;
						auto r=gapr::make_parser(tier).from_dec(args.data(), args.size());
						if(!r.second)
							break;
						std::string info;
						if(r.first<args.size()) {
							if(args.data()[r.first]!=':')
								break;
							info=std::string{args.data()+r.first+1, args.size()-r.first-1};
						}
						return std::move(prom).set(login_result{static_cast<gapr::tier>(tier), std::move(info)});
#if 0
						if(hdr.body()) {
							std::cerr<<"MOTD:\n";
							std::cerr.write(hdr.body().base(), hdr.body().len());
							std::cerr<<"\nEND MOTD\n";
						}
					if(msg.type()=='b' || msg.type()=='t') {
						std::cerr<<"MOTD:\n";
						std::cerr.write(msg.body().base(), msg.body().len());
						std::cerr<<"\nEND MOTD\n";
					}
#endif
					}
					if(hdr.tag_is("ERR")) {
						if(!args.size())
							break;
						gapr::str_glue err{"server failure: ", args};
						throw std::runtime_error{err.str()};
					}
				} while(false);
				gapr::str_glue err{"invalid reply: ", std::string{hdr.line_ptr(), hdr.line_len()}};
				throw std::runtime_error{err.str()};
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});
	});
	return fut;
}

gapr::future<tapi::select_result> tapi::select(client_end& cli, const std::string& grp) {
	promise<select_result> prom{};
	auto fut=prom.get_future();
	client_end::msg_hdr hdr{"SELECT", grp};
	cli.async_send(std::move(hdr), [cli,prom=std::move(prom)](bs::error_code ec) mutable {
		if(ec)
			return unlikely(std::move(prom), to_std_error_code(ec));
		cli.async_recv([prom=std::move(prom)](bs::error_code ec, const client_end::msg_hdr_in& hdr) mutable {
			if(ec)
				return unlikely(std::move(prom), to_std_error_code(ec));
			try {
				do {
					auto args=hdr.args();
					if(hdr.tag_is("NO")) {
						if(!args.size())
							break;
						gapr::str_glue err{std::string{args.data(), args.size()}};
						throw std::runtime_error{err.str()};
					}
					if(hdr.tag_is("OK")) {
						unsigned int tier2;
						unsigned int stage;
						uint64_t id;

						if(!args.size())
							break;
						auto r=gapr::make_parser(tier2).from_dec(&args[0], args.size());
						if(!r.second)
							break;
						if(r.first>=args.size())
							break;
						if(args[r.first]!=':')
							break;
						args.remove_prefix(r.first+1);

						if(args.empty())
							break;
						r=gapr::make_parser(stage).from_dec(&args[0], args.size());
						if(!r.second)
							break;
						if(r.first>=args.size())
							break;
						if(args[r.first]!=':')
							break;
						args.remove_prefix(r.first+1);

						if(args.empty())
							break;
						r=gapr::make_parser(id).from_dec(&args[0], args.size());
						if(!r.second)
							break;
						if(r.first>=args.size())
							break;
						if(args[r.first]!=':')
							break;
						args.remove_prefix(r.first+1);

						r.first=args.find(':');
						if(r.first==std::string_view::npos)
							break;
						return std::move(prom).set(select_result{
							static_cast<gapr::tier>(tier2),
								static_cast<gapr::stage>(stage),
								id, std::string{&args[0], r.first},
								std::string{&args[0]+r.first+1, args.size()-r.first-1}
						});
					}
					if(hdr.tag_is("ERR")) {
						if(!args.size())
							break;
						gapr::str_glue err{"server failure: ", std::string{args.data(), args.size()}};
						throw std::runtime_error{err.str()};
					}
				} while(false);
				gapr::str_glue err{"invalid reply: ", std::string{hdr.line_ptr(), hdr.line_len()}};
				throw std::runtime_error{err.str()};
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});
	});
	return fut;
}

// helpers
static void receive_file_func(gapr::promise<gapr::mem_file>&& prom, gapr::client_end&& msg, gapr::mutable_mem_file&& file) {
	auto buf=file.map_tail();
	msg.async_read(buf, [file=std::move(file),prom=std::move(prom),msg](bs::error_code ec, std::size_t nbytes) mutable {
		if(ec) {
			if(ec==boost::asio::error::eof) {
				file.add_tail(nbytes);
				return std::move(prom).set(std::move(file));
			}
			return std::move(prom).set_error(to_std_error_code(ec));
		}
		file.add_tail(nbytes);
		receive_file_func(std::move(prom), std::move(msg), std::move(file));
	});
}
static void receive_file(gapr::promise<gapr::mem_file>&& prom, gapr::client_end&& msg) {
	gapr::mutable_mem_file file{true};
	return receive_file_func(std::move(prom), std::move(msg), std::move(file));
}

gapr::future<tapi::get_catalog_result> tapi::get_catalog(client_end& cli) {
	gapr::promise<get_catalog_result> prom{};
	auto fut=prom.get_future();
	client_end::msg_hdr hdr{"GET.CATALOG"};
	////////////////////////////////
	//////////
	cli.async_send(std::move(hdr), [cli,prom=std::move(prom)](bs::error_code ec) mutable {
		if(ec)
			return unlikely(std::move(prom), to_std_error_code(ec));
		cli.async_recv([cli,prom=std::move(prom)](bs::error_code ec, const client_end::msg_hdr_in& hdr) mutable {
			if(ec)
				return unlikely(std::move(prom), to_std_error_code(ec));
			try {
				do {
					auto args=hdr.args();
					if(hdr.tag_is("NO")) {
						if(!args.size())
							break;
						throw std::runtime_error{std::string{args.data(), args.size()}};
					}
					if(hdr.tag_is("OK")) {
						if(!args.empty())
							break;
						if(cli.type_in()!=client_end::msg_type::stream)
							break;
						gapr::promise<gapr::mem_file> cat_file{};
						auto fut=cat_file.get_future();
						// XXX
						auto ex=cli.get_executor();
						receive_file(std::move(cat_file), std::move(cli));
						return std::move(fut).async_wait(ex, [prom=std::move(prom)](gapr::likely<gapr::mem_file>&& catfile) mutable {
							if(!catfile)
								return std::move(prom).set_error(std::move(catfile).error());
							return std::move(prom).set(get_catalog_result{std::move(catfile.get())});
						});
					}
					if(hdr.tag_is("ERR")) {
						if(!args.size())
							break;
						gapr::str_glue err{"server failure: ", std::string{args.data(), args.size()}};
						throw std::runtime_error{err.str()};
					}
				} while(false);
				gapr::str_glue err{"invalid reply: ", std::string{hdr.line_ptr(), hdr.line_len()}};
				throw std::runtime_error{err.str()};
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});
	});
	return fut;
}

gapr::future<tapi::get_state_result> tapi::get_state(client_end& cli) {
	gapr::promise<get_state_result> prom{};
	auto fut=prom.get_future();
	client_end::msg_hdr hdr{"GET.STATE"};
	cli.async_send(std::move(hdr), [cli,prom=std::move(prom)](bs::error_code ec) mutable {
		if(ec)
			return unlikely(std::move(prom), to_std_error_code(ec));
		cli.async_recv([cli,prom=std::move(prom)](bs::error_code ec, const client_end::msg_hdr_in& hdr) mutable {
			if(ec)
				return unlikely(std::move(prom), to_std_error_code(ec));
			try {
				do {
					auto args=hdr.args();
					if(hdr.tag_is("NO")) {
						if(!args.size())
							break;
						return std::move(prom).set(get_state_result{gapr::mem_file{}});
						//throw std::runtime_error{std::string{args.data(), args.size()}};
					}
					if(hdr.tag_is("OK")) {
						if(args.size())
							break;
						if(cli.type_in()!=client_end::msg_type::stream)
							break;
						gapr::promise<gapr::mem_file> file{};
						auto fut=file.get_future();
						auto ex=cli.get_executor();
						receive_file(std::move(file), std::move(cli));
						return std::move(fut).async_wait(ex, [prom=std::move(prom)](gapr::likely<gapr::mem_file>&& file) mutable {
							if(!file)
								return std::move(prom).set_error(std::move(file).error());
							return std::move(prom).set(get_state_result{std::move(file.get())});
						});
					}
					if(hdr.tag_is("ERR")) {
						if(!args.size())
							break;
						gapr::str_glue err{"server failure: ", std::string{args.data(), args.size()}};
						throw std::runtime_error{err.str()};
					}
				} while(false);
				gapr::str_glue err{"invalid reply: ", std::string{hdr.line_ptr(), hdr.line_len()}};
				throw std::runtime_error{err.str()};
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});
	});
	return fut;
}

gapr::future<tapi::get_commit_result> tapi::get_commit(client_end& cli, uint64_t id) {
// encrypt(aes-cbc-128)
	gapr::promise<get_commit_result> prom{};
	auto fut=prom.get_future();
	client_end::msg_hdr hdr{"GET.COMMIT", id};
	cli.async_send(std::move(hdr), [cli,prom=std::move(prom)](bs::error_code ec) mutable {
		if(ec)
			return unlikely(std::move(prom), to_std_error_code(ec));
		cli.async_recv([cli,prom=std::move(prom)](bs::error_code ec, const client_end::msg_hdr_in& hdr) mutable {
			if(ec)
				return unlikely(std::move(prom), to_std_error_code(ec));
			try {
				do {
					auto args=hdr.args();
					if(hdr.tag_is("NO")) {
						if(!args.size())
							break;
						return std::move(prom).set(get_commit_result{gapr::mem_file{}});
						//throw std::runtime_error{std::string{args.data(), args.size()}};
					}
					if(hdr.tag_is("OK")) {
						if(args.size())
							break;
						if(cli.type_in()!=client_end::msg_type::stream)
							break;
						gapr::promise<gapr::mem_file> file{};
						auto fut=file.get_future();
						auto ex=cli.get_executor();
						receive_file(std::move(file), std::move(cli));
						return std::move(fut).async_wait(ex, [prom=std::move(prom)](gapr::likely<gapr::mem_file>&& file) mutable {
							if(!file)
								return std::move(prom).set_error(std::move(file).error());
							return std::move(prom).set(get_commit_result{std::move(file.get())});
						});
					}
					if(hdr.tag_is("ERR")) {
						if(!args.size())
							break;
						gapr::str_glue err{"server failure: ", std::string{args.data(), args.size()}};
						throw std::runtime_error{err.str()};
					}
				} while(false);
				gapr::str_glue err{"invalid reply: ", std::string{hdr.line_ptr(), hdr.line_len()}};
				throw std::runtime_error{err.str()};
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});
	});
	return fut;
}

gapr::future<tapi::get_commits_result> tapi::get_commits(client_end& cli, gapr::mem_file&& hist, uint64_t upto) {
	gapr::promise<get_commits_result> prom{};
	auto fut=prom.get_future();
	auto sz=hist.size();
	client_end::msg_hdr hdr{"GET.COMMITS", upto};
	cli.async_send(std::move(hdr), 1, sz, [cli,prom=std::move(prom),hist=std::move(hist)](bs::error_code ec) mutable {
		if(ec)
			return unlikely(std::move(prom), to_std_error_code(ec));
		async_write_mem_file(cli, std::move(hist));
		cli.async_recv([cli,prom=std::move(prom)](bs::error_code ec, const client_end::msg_hdr_in& hdr) mutable {
			if(ec)
				return unlikely(std::move(prom), to_std_error_code(ec));
			try {
				do {
					auto args=hdr.args();
					if(hdr.tag_is("NO")) {
						if(!args.size())
							break;
						return std::move(prom).set(get_commits_result{gapr::mem_file{}});
						//throw std::runtime_error{std::string{args.data(), args.size()}};
					}
					if(hdr.tag_is("OK")) {
						if(args.size())
							break;
						if(cli.type_in()!=client_end::msg_type::stream)
							break;
						gapr::promise<gapr::mem_file> file{};
						auto fut=file.get_future();
						auto ex=cli.get_executor();
						receive_file(std::move(file), std::move(cli));
						return std::move(fut).async_wait(ex, [prom=std::move(prom)](gapr::likely<gapr::mem_file>&& file) mutable {
							if(!file)
								return std::move(prom).set_error(std::move(file).error());
							return std::move(prom).set(get_commits_result{std::move(file.get())});
						});
					}
					if(hdr.tag_is("ERR")) {
						if(!args.size())
							break;
						gapr::str_glue err{"server failure: ", std::string{args.data(), args.size()}};
						throw std::runtime_error{err.str()};
					}
				} while(false);
				gapr::str_glue err{"invalid reply: ", std::string{hdr.line_ptr(), hdr.line_len()}};
				throw std::runtime_error{err.str()};
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});
	});
	return fut;
}

gapr::future<tapi::commit_result> tapi::commit(client_end& cli, gapr::delta_type type, gapr::mem_file&& payload, uint64_t base, uint64_t tip) {
	auto t0=std::chrono::steady_clock::now();
	gapr::promise<commit_result> prom{};
	auto fut=prom.get_future();
	client_end::msg_hdr hdr{"COMMIT", static_cast<std::underlying_type_t<gapr::delta_type>>(type), base, tip};
	auto sz=payload.size();
	cli.async_send(std::move(hdr), 1, sz, [t0,cli,prom=std::move(prom),payload=std::move(payload)](bs::error_code ec) mutable {
		if(ec)
			return unlikely(std::move(prom), to_std_error_code(ec));
		async_write_mem_file(cli, std::move(payload));
		cli.async_recv([t0,cli,prom=std::move(prom)](bs::error_code ec, const client_end::msg_hdr_in& hdr) mutable {
			if(ec)
				return unlikely(std::move(prom), to_std_error_code(ec));
			try {
				auto t1=std::chrono::steady_clock::now();
				auto nus=std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count();
				gapr::print("commit rtt: ", nus, "us");
				do {
					auto args=hdr.args();
					if(hdr.tag_is("NO")) {
						if(!args.size())
							break;
						uint64_t id;
						auto r=gapr::make_parser(id).from_dec(args.data(), args.size());
						if(!r.second)
							break;
						if(r.first<args.size())
							break;
						return std::move(prom).set(commit_result{0, 0, id});
					}
					if(hdr.tag_is("RETRY")) {
						if(!args.size())
							break;
						uint64_t id;
						auto r=gapr::make_parser(id).from_dec(args.data(), args.size());
						if(!r.second)
							break;
						if(r.first<args.size())
							break;
						return std::move(prom).set(commit_result{0, 1, id});
					}
					if(hdr.tag_is("OK")) {
						if(args.size()<=0)
							break;
						uint32_t nid_alloc;
						uint64_t id;
						uint64_t base;
						auto r=gapr::make_parser(nid_alloc).from_dec(args.data(), args.size());
						if(!r.second)
							break;
						if(r.first>=args.size() || args.data()[r.first]!=':')
							break;
						args.remove_prefix(r.first+1);
						if(args.size()<=0)
							break;
						r=gapr::make_parser(id).from_dec(args.data(), args.size());
						if(!r.second)
							break;
						if(r.first>=args.size() || args.data()[r.first]!=':')
							break;
						args.remove_prefix(r.first+1);
						if(args.size()<=0)
							break;
						r=gapr::make_parser(base).from_dec(args.data(), args.size());
						if(!r.second)
							break;
						if(r.first<args.size())
							break;
						return std::move(prom).set(commit_result{nid_alloc, id, base});
					}
					if(hdr.tag_is("ERR")) {
						if(!args.size())
							break;
						gapr::str_glue err{"server failure: ", std::string{args.data(), args.size()}};
						throw std::runtime_error{err.str()};
					}
				} while(false);
				gapr::str_glue err{"invalid reply: ", std::string{hdr.line_ptr(), hdr.line_len()}};
				throw std::runtime_error{err.str()};
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});
	});
	return fut;
}

gapr::future<tapi::get_model_result> tapi::get_model(client_end& cli) {
	gapr::promise<get_model_result> prom{};
	auto fut=prom.get_future();
	client_end::msg_hdr hdr{"GET.MODEL"};
	cli.async_send(std::move(hdr), [cli,prom=std::move(prom)](bs::error_code ec) mutable {
		if(ec)
			return unlikely(std::move(prom), to_std_error_code(ec));
		cli.async_recv([cli,prom=std::move(prom)](bs::error_code ec, const client_end::msg_hdr_in& hdr) mutable {
			if(ec)
				return unlikely(std::move(prom), to_std_error_code(ec));
			try {
				do {
					auto args=hdr.args();
					if(hdr.tag_is("NO")) {
						if(!args.size())
							break;
						return std::move(prom).set(get_model_result{gapr::mem_file{}});
						//throw std::runtime_error{std::string{args.data(), args.size()}};
					}
					if(hdr.tag_is("OK")) {
						if(args.size())
							break;
						if(cli.type_in()!=client_end::msg_type::stream)
							break;
						gapr::promise<gapr::mem_file> file{};
						auto fut=file.get_future();
						auto ex=cli.get_executor();
						receive_file(std::move(file), std::move(cli));
						return std::move(fut).async_wait(ex, [prom=std::move(prom)](gapr::likely<gapr::mem_file>&& file) mutable {
							if(!file)
								return std::move(prom).set_error(std::move(file).error());
							return std::move(prom).set(get_model_result{std::move(file.get())});
						});
					}
					if(hdr.tag_is("ERR")) {
						if(!args.size())
							break;
						gapr::str_glue err{"server failure: ", std::string{args.data(), args.size()}};
						throw std::runtime_error{err.str()};
					}
				} while(false);
				gapr::str_glue err{"invalid reply: ", std::string{hdr.line_ptr(), hdr.line_len()}};
				throw std::runtime_error{err.str()};
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});
	});
	return fut;
}

gapr::future<int> tapi::upload_fragment(client_end& cli, gapr::mem_file&& payload, uint64_t base, uint64_t tip) {
	auto t0=std::chrono::steady_clock::now();
	gapr::promise<int> prom{};
	auto fut=prom.get_future();
	//XXX
	client_end::msg_hdr hdr{"FRAGMENT", base, tip};
	auto sz=payload.size();
	cli.async_send(std::move(hdr), 1, sz, [t0,cli,prom=std::move(prom),payload=std::move(payload)](bs::error_code ec) mutable {
		if(ec)
			return unlikely(std::move(prom), to_std_error_code(ec));
		async_write_mem_file(cli, std::move(payload));
		cli.async_recv([t0,cli,prom=std::move(prom)](bs::error_code ec, const client_end::msg_hdr_in& hdr) mutable {
			if(ec)
				return unlikely(std::move(prom), to_std_error_code(ec));
			try {
				auto t1=std::chrono::steady_clock::now();
				auto nus=std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count();
				gapr::print("fragment rtt: ", nus, "us");
				do {
					auto args=hdr.args();
					if(hdr.tag_is("RETRY")) {
						if(args.size())
							break;
						return std::move(prom).set(1);
					}
					if(hdr.tag_is("OK")) {
						if(args.size()>0)
							break;
						return std::move(prom).set(0);
					}
					if(hdr.tag_is("ERR")) {
						if(!args.size())
							break;
						return std::move(prom).set(-1);
						//gapr::str_glue err{"server failure: ", std::string{args.data(), args.size()}};
						//throw std::runtime_error{err.str()};
					}
				} while(false);
				gapr::str_glue err{"invalid reply: ", std::string{hdr.line_ptr(), hdr.line_len()}};
				throw std::runtime_error{err.str()};
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});
	});
	return fut;
}

//#include "gapr/trace.client.h"

//#include "gapr/connection.h"
//#include "gapr/uv-wrapper.h"
//#include "gapr/model.h"

/*
gapr::TraceClient::TraceClient(gapr::uv::Loop* loop, SSL_CTX* ssl_ctx):
	_loop{loop}, _client{new gapr::Client{ssl_ctx}}, _conn{nullptr}
{
}
gapr::TraceClient::~TraceClient() {
	delete _client;
	if(_conn)
		delete _conn;
}
*/

/*
void gapr::TraceClient::resolve(const char* host, const char* port, const resolve_cb& cb) {
	_loop->getaddrinfo(host, port, [cb](const struct addrinfo* res) { if(res) cb(nullptr, res->ai_addr); else cb("Failed to resolve", nullptr); });
}

void gapr::TraceClient::connect_cb_(gapr::Connection* con) {
}
void gapr::TraceClient::connect(const struct sockaddr* addr, const connect_cb& cb) {
}
*/


#if 0
void gapr::TraceClient::commit(const gapr::Delta& delta, const commit_cb& cb) {
	gapr::Message msg;
	auto p=msg.head.alloc(GAPR_SERVER_MAX_LINE);
	auto n=snprintf(p, GAPR_SERVER_MAX_LINE, "COMMIT\n");
	msg.head.shrink(n);
	auto s=delta.encoded_size();
	auto pdata=msg.body.alloc(s);
	delta.encode(pdata, s);
	_commit_req=_conn->write(std::move(msg), nullptr);
	_commit_cb=cb;
}

void gapr::TraceClient::save(const save_cb& cb) {
	gapr::Message msg;
	auto p=msg.head.alloc(GAPR_SERVER_MAX_LINE);
	auto n=snprintf(p, GAPR_SERVER_MAX_LINE, "SAVE\n");
	msg.head.shrink(n);
	_save_req=_conn->write(std::move(msg), nullptr);
	_save_cb=cb;
}


#endif
