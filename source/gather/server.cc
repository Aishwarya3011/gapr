/* gather-pr4m/server.cc
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


#include "server.hh"

#include "env.hh"
#include "model.hh"
#include "stats.hh"
#include "stop-signal.hh"

#include "gapr/acceptor.hh"
#include "gapr/connection.hh"
//#include "gapr/buffer.hh"
//#include "gapr/utility.hh"
//#include "gapr/mem-file.hh"
#include "gapr/future.hh"
#include "gapr/streambuf.hh"
#include "gapr/parser.hh"
#include "gapr/str-glue.hh"
#include "gapr/fix-error-code.hh"
//#include "gapr/uv-loop.hh"
//#include "gapr/archive.hh"

//#include <memory>
//#include <string>
#include <thread>
#include <fstream>
#include <filesystem>
//#include <istream>
//#include <climits>
//#include <cinttypes>
//#include <chrono>
//#include <thread>
//#include <mutex>
//#include <condition_variable>
#include <optional>
#include <charconv>
#include <fstream>
#include <sstream>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/thread_pool.hpp>

#include <boost/beast/http.hpp>

//#include <boost/beast/http/empty_body.hpp>
//#include <boost/beast/http/string_body.hpp>
//#include <boost/beast/http/file_body.hpp>
//#include <boost/beast/http/vector_body.hpp>
#include <boost/beast/core/flat_buffer.hpp>
//#include <boost/beast/http/read.hpp>
//#include <boost/beast/http/write.hpp>
//#include <boost/beast/http/parser.hpp>
#include <boost/beast/core/detect_ssl.hpp>

namespace ba=boost::asio;
namespace beast=boost::beast;
namespace http=boost::beast::http;
namespace ssl=boost::asio::ssl;
using tcp=ba::ip::tcp;
using bs_error_code=boost::system::error_code;

std::string gapr::gather_server::currentDateTime() {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void gapr::gather_server::logMessage(const std::string& context, const std::string& message) {
	// std::string filename = "server_log_" + currentDateTime() + ".txt";
	// std::ofstream logFile(filename, std::ios_base::app); 
	std::ofstream logFile("server_log.txt", std::ios_base::app);  

    if (logFile.is_open()) {
        logFile << "[" << currentDateTime() << "] (" << context << ") " << message << std::endl;
    } else {
        std::cerr << "Failed to open log file." << std::endl;
    }
}

#include "send-recv-file.hh"

template<typename T, typename=std::enable_if_t<std::is_integral_v<T>>>
inline static bool parse_http_args_impl2(std::string_view args, T& val) {
	std::string log_message = "Parsing value: " + std::string{args};
    gapr::gather_server::logMessage(__FILE__, log_message);

    auto [eptr, ec] = std::from_chars(args.begin(), args.end(), val, 10);
    // if(ec != std::errc{}) {
    //     std::string error_message = "Error during parsing: " + std::to_string(static_cast<int>(ec));
    //     gapr::gather_server::logMessage(__FILE__, error_message);
    //     return false;
    // }
    // if(eptr != args.end()) {
    //     std::string extra_message = "Extra characters found after parsing: " + std::string{eptr, args.end()};
    //     gapr::gather_server::logMessage(__FILE__, extra_message);
    //     return false;
    // }
    return true;

	// std::string log_message = "Parsing value (expected integer): " + std::string{args};
    // gapr::gather_server::logMessage(__FILE__, log_message);

    // auto slash_pos = args.find('/');
    // std::string_view parse_part = args;

    // if (slash_pos != std::string_view::npos) {
    //     parse_part = args.substr(0, slash_pos);
    // }

    // auto [eptr, ec] = std::from_chars(parse_part.begin(), parse_part.end(), val, 10);
    // if(ec != std::errc{}) {
    //     std::string error_message = "Error during parsing: " + std::to_string(static_cast<int>(ec));
    //     gapr::gather_server::logMessage(__FILE__, error_message);
    //     return false;
    // }

    // if(eptr != parse_part.end()) {
    //     std::string extra_message = "Extra characters found after parsing: " + std::string{eptr, parse_part.end()};
    //     gapr::gather_server::logMessage(__FILE__, extra_message);
    //     return false;
    // }

    // gapr::gather_server::logMessage(__FILE__, "Successfully parsed integer: " + std::to_string(val));
    // return true;
}
inline static bool parse_http_args_impl2(std::string_view args, std::string& val) {
	val=args;
	return true;
}
template<typename Req, typename... Reqs>
inline static bool parse_http_args_impl(std::string_view args, Req& req, Reqs&... reqs) {
	if(args.empty())
		return false;
	if constexpr(sizeof...(reqs)>0) {
		auto n=args.find('/');
		if(n==args.npos)
			return false;
		auto next=args;
		next.remove_prefix(n+1);
		if(!parse_http_args_impl(next, reqs...))
			return false;
		args=args.substr(0, n);
	}
	return parse_http_args_impl2(args, req);
}
template<typename Opt, typename... Opts>
inline static bool parse_http_args_impl(std::string_view args, std::nullptr_t, Opt& opt, Opts&... opts) {
	if(args.empty())
		return false;
	if constexpr(sizeof...(opts)>0) {
		auto n=args.find('/');
		if(n!=args.npos) {
			auto next=args;
			next.remove_prefix(n+1);
			if(!parse_http_args_impl(next, nullptr, opts...))
				return false;
			args=args.substr(0, n);
		}
	}
	return parse_http_args_impl2(args, opt);
}
template<typename Req, typename... Reqs, typename Opt, typename... Opts>
inline static bool parse_http_args_impl(std::string_view args, Req& req, Reqs&... reqs, std::nullptr_t, Opt& opt, Opts&... opts) {
	if(args.empty())
		return false;
	auto n=args.find('/');
	if(n==args.npos) {
		if constexpr(sizeof...(reqs)>0)
			return false;
	} else {
		auto next=args;
		next.remove_prefix(n+1);
		if(!parse_http_args_impl(next, reqs..., nullptr, opt, opts...))
			return false;
		args=args.substr(0, n);
	}
	return parse_http_args_impl2(args, req);
}
inline static bool parse_http_args(std::string_view args) {
	return args.empty();
}
template<typename Req, typename... Reqs>
inline static bool parse_http_args(std::string_view args, Req& req, Reqs&... reqs) {
	if(args.empty())
		return false;
	assert(args[0]=='/');
	args.remove_prefix(1);
	return parse_http_args_impl(args, req, reqs...);
}
template<typename Opt, typename... Opts>
inline static bool parse_http_args(std::string_view args, std::nullptr_t, Opt& opt, Opts&... opts) {
	if(args.empty())
		return true;
	assert(args[0]=='/');
	args.remove_prefix(1);
	return parse_http_args_impl(args, nullptr, opt, opts...);
}
template<typename Req, typename... Reqs, typename Opt, typename... Opts>
inline static bool parse_http_args(std::string_view args, Req& req, Reqs&... reqs, std::nullptr_t, Opt& opt, Opts&... opts) {
	if(args.empty())
		return false;
	assert(args[0]=='/');
	args.remove_prefix(1);
	return parse_http_args_impl(args, req, reqs..., nullptr, opt, opts...);
}

static constexpr unsigned char priv_proto[]={
	'g', 'a', 'p', 'r', '/', '1', '.', '1'
};
static constexpr unsigned char http11_proto[]={
	'h', 't', 't', 'p', '/', '1', '.', '1'
};
static int alpn_select_cb(SSL* ssl, const unsigned char** out, unsigned char* outlen, const unsigned char* in, unsigned int inlen, void*) noexcept {
	for(unsigned int i=0; i<inlen;) {
		unsigned char l=in[i];
		auto p=&in[i+1];
		if(std::equal(p, p+l, priv_proto, &priv_proto[sizeof(priv_proto)])) {
			*out=p;
			*outlen=l;
			return SSL_TLSEXT_ERR_OK;
		}
		if(std::equal(p, p+l, http11_proto, &http11_proto[sizeof(http11_proto)])) {
			*out=p;
			*outlen=l;
			return SSL_TLSEXT_ERR_OK;
		}
		i+=1+l;
	}
	return SSL_TLSEXT_ERR_NOACK;
}
static bool has_prefix(beast::string_view path, beast::string_view pfx) {
	if(path.size()<pfx.size())
		return false;
	return std::equal(path.begin(), path.begin()+pfx.size(), pfx.begin(), pfx.end());
}
static beast::string_view mime_type(beast::string_view path) {
	using beast::iequals;
	auto const ext=[&path]{
		auto const pos = path.rfind(".");
		if(pos == beast::string_view::npos)
			return beast::string_view{};
		return path.substr(pos);
	}();
	if(iequals(ext, ".htm"))  return "text/html";
	if(iequals(ext, ".html"))  return "text/html";
	if(iequals(ext, ".css"))  return "text/css";
	if(iequals(ext, ".woff")) return "application/font-woff";
	if(iequals(ext, ".js"))   return "application/javascript";

	if(iequals(ext, ".png"))  return "image/png";
	if(iequals(ext, ".jpeg")) return "image/jpeg";
	if(iequals(ext, ".jpg"))  return "image/jpeg";
	if(iequals(ext, ".gif"))  return "image/gif";
	if(iequals(ext, ".svg"))  return "image/svg+xml";
	if(iequals(ext, ".svgz")) return "image/svg+xml";
	if(iequals(ext, ".mp4"))  return "video/mp4";

	if(iequals(ext, ".xml"))  return "application/xml";
	if(iequals(ext, ".json")) return "application/json";
	if(iequals(ext, ".txt"))  return "text/plain";
	if(iequals(ext, ".pdf")) return "application/pdf";

	return "application/octet-stream";
}

struct gapr::gather_server::PRIV {

	std::shared_ptr<gapr::gather_env> _env;

	ba::io_context _io_ctx{1};
	ba::thread_pool _thr_pool{std::thread::hardware_concurrency()};
	ssl::context _ssl_ctx{ssl::context::tls};
	gapr::Acceptor _acceptor{_io_ctx.get_executor()};

	struct Session: std::enable_shared_from_this<Session> {
		std::string user;
		gapr::tier tier{gapr::tier::nobody};
		std::string project;
		gapr::stage stage;
		gapr::tier tier2{gapr::tier::nobody};
		gapr::commit_history history;
		//XXX
		//in cases tier/stage/tier2 becomes invalid (chtier/chstage/chacl),
		//client updates states by REFRESH.
	};
	std::vector<std::weak_ptr<Session>> _sessions;
	void add_session(Session& sess) {
		_sessions.push_back(sess.weak_from_this());
	}
	unsigned int _del_session_cnt{0};
	void del_session() {
		if(++_del_session_cnt<128)
			return;
		_del_session_cnt=0;
		std::size_t j=0;
		for(std::size_t i=0; i<_sessions.size(); ++i) {
			auto sess2=_sessions[i].lock();
			if(sess2) {
				if(i!=j)
					_sessions[j]=std::move(_sessions[i]);
				++j;
			}
		}
		while(_sessions.size()>j)
			_sessions.pop_back();
	}
	void supersede_session(const Session& sess) {
		for(auto& p: _sessions) {
			if(auto sess2=p.lock(); sess2 && sess2.get()!=&sess) {
				if(sess2->user==sess.user &&
						sess2->project==sess.project) {
					sess2->project="";
				}
			}
		}
	}
	using Command=void (PRIV::*)(Session&, gapr::server_end::msg_hdr_in, gapr::server_end&&);
	std::unordered_map<std::string, Command> _str2command{};
	struct ModelState {
		std::shared_ptr<gather_model> model;
		gapr::mem_file state;
		std::chrono::steady_clock::time_point pending_ts;

		ModelState() : model(nullptr), state{}, pending_ts{} { }

		explicit ModelState(std::shared_ptr<gather_model>&& model): model{std::move(model)}, state{}, pending_ts{} { }
	};
	std::unordered_map<std::string, ModelState> _models;
	gather_stats _stats;

	using HttpRequest=http::request<http::string_body>;
	class HttpResponse {
		public:
			virtual ~HttpResponse() { }
			HttpResponse(const HttpResponse&) =delete;
			HttpResponse& operator=(const HttpResponse&) =delete;
			virtual void async_send(ssl::stream<tcp::socket>& ssl, std::function<void(bs_error_code, std::size_t)> cb) =0;
		protected:
			constexpr HttpResponse() noexcept { }
		private:
	};
	using HttpResponsePtr=std::unique_ptr<HttpResponse>;
	struct HttpConnection: std::enable_shared_from_this<HttpConnection> {
		tcp::socket sock;
		tcp::endpoint endpoint;
		beast::flat_buffer buffer;
		std::optional<ssl::stream<tcp::socket>> ssl;
		std::optional<http::request_parser<http::string_body>> parser;
		std::string_view api_args;
		HttpResponsePtr res_ptr;
		std::optional<http::response<http::string_body>> res;
		std::optional<http::response_serializer<http::string_body>> serializer;
		HttpConnection(boost::asio::io_context& ctx): sock{ctx} { }
		HttpConnection(tcp::socket&& socket): sock{std::move(socket)} { }
		std::shared_ptr<HttpConnection> next;
	};
	struct HttpSession {
		std::string user;
		gapr::tier tier{gapr::tier::nobody};
		std::time_t pw_ts{0};
	};
	std::unordered_map<std::string, HttpSession> _http_sessions;
	using HttpApi=HttpResponsePtr (PRIV::*)(HttpConnection&);
	std::unordered_map<std::string, HttpApi> _http_apis{};

	explicit PRIV():
		_env{std::make_shared<gapr::gather_env>()},
		_str2command{}
	{
		_http_apis.try_emplace("login", &PRIV::handle_http_login);
		_http_apis.try_emplace("register", &PRIV::handle_http_register);
		_http_apis.try_emplace("chtier", &PRIV::handle_http_chtier);
		_http_apis.try_emplace("chstage", &PRIV::handle_http_chstage);
		_http_apis.try_emplace("stats", &PRIV::handle_http_stats);
		_http_apis.try_emplace("rank", &PRIV::handle_http_rank);
		_http_apis.try_emplace("progress", &PRIV::handle_http_progress);
		_http_apis.try_emplace("proofread-stats", &PRIV::handle_http_proofread_stats);
		_http_apis.try_emplace("catalog", &PRIV::handle_http_catalog);
		_http_apis.try_emplace("data", &PRIV::handle_http_data);
		_http_apis.try_emplace("pending", &PRIV::handle_http_pending);

		_str2command.try_emplace("LOGIN", &PRIV::handle_CMD_LOGIN);
		_str2command.try_emplace("SELECT", &PRIV::handle_CMD_SELECT);
		_str2command.try_emplace("GET_CATALOG", &PRIV::handle_CMD_GET_CATALOG);
		_str2command.try_emplace("GET_STATE", &PRIV::handle_CMD_GET_STATE);
		_str2command.try_emplace("GET_COMMIT", &PRIV::handle_CMD_GET_COMMIT);
		_str2command.try_emplace("GET_COMMITS", &PRIV::handle_CMD_GET_COMMITS);
		_str2command.try_emplace("GET_MODEL", &PRIV::handle_CMD_GET_MODEL);
		_str2command.try_emplace("COMMIT", &PRIV::handle_CMD_COMMIT);
	}
	PRIV(const PRIV&) =delete;
	PRIV& operator=(const PRIV&) =delete;
	~PRIV() { }

	void configure(std::string_view root) {
		_env->prepare(root);
		bs_error_code ec;
		auto& cert_file=_env->certificate();
		_ssl_ctx.use_certificate_file(cert_file, ssl::context::pem, ec);
		if(ec)
			throw gapr::CliErrorMsg{"use_certificate_file(`", cert_file,
				"'): ", ec.message()};
		auto& key_file=_env->private_key();
		_ssl_ctx.use_private_key_file(key_file, ssl::context::pem, ec);
		if(ec)
			throw gapr::CliErrorMsg{"use_private_key_file(`", key_file,
				"'): ", ec.message()};
		::SSL_CTX_set_alpn_select_cb(_ssl_ctx.native_handle(), alpn_select_cb, nullptr);

		for(auto& proj: _env->projects())
			load_project(std::move(proj));
	}

	stop_signal _stop_signal;

	int run(std::string_view host, unsigned short port) {
		_acceptor.bind(host.data(), port);
		_acceptor.async_listen(10, [this](bs_error_code ec) ->void {
			if(ec)
				throw gapr::CliErrorMsg{"async_listen(): ", ec.message()};
			do_accept();
		});

		_stop_signal.on_stop([this](unsigned int n) {
			boost::asio::post(_io_ctx, [this,n]() {
				if(n>1)
					_io_ctx.stop();
				else
					_acceptor.close();
			});
		});
		_stop_signal.start();

		std::thread save_thread{[env=_env] {
			gapr::print(1, "begin save thread");
			while(env->save_configs())
				;
			gapr::print(1, "end save thread");
		}};


		gapr::print("before run");
		auto c=_io_ctx.run();
		gapr::print("server stopped normally, ", c, " events.");

		_stop_signal.stop();

		_env->save_configs_end();
		save_thread.join();
		gapr::print("after run");

		_thr_pool.join();
		if(_stop_signal.signal()>1) {
			std::cerr<<"forced stop\n";
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	}
	void emergency() {
	}
	void update_state(const std::string& proj);
	void load_project(std::string&& proj);

	void do_accept();
	void do_handshake(tcp::socket&& sock);
	void recv_request(Session& ses, gapr::server_end&& req);

	void recv_request(HttpConnection& conn);
	void handle_api_request(HttpConnection& conn);
	void write_response(HttpConnection& conn, HttpResponsePtr res);

	template<typename Body>
		class HttpResponseImpl: public HttpResponse {
			public:
				template<typename... Args>
					explicit HttpResponseImpl(Args&&... args): _msg{std::forward<Args>(args)...}, _serializer{_msg} { }
			private:
				void async_send(ssl::stream<tcp::socket>& ssl, std::function<void(bs_error_code, std::size_t)> cb) override {
					http::async_write(ssl, _serializer, std::move(cb));
				}
				http::response<Body> _msg;
				http::response_serializer<Body> _serializer;
		};
	template<typename Body, typename... Args>
		static HttpResponsePtr make_http_response(http::response<Body>&& msg) {
			return std::make_unique<HttpResponseImpl<Body>>(std::move(msg));
		}
	HttpResponsePtr http_bad_request(HttpRequest& req) {
		http::response<http::string_body> res{http::status::bad_request, req.version()};
		res.set(http::field::server, _env->http_server());
		res.set(http::field::content_type, "text/html");
		res.keep_alive(req.keep_alive());
		res.body() = "<!DOCTYPE html>\n<html>\n<head>\n"
			"<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\">\n"
			"<meta charset=\"UTF-8\">\n<title>400 Bad Request</title>\n"
			"</head><body>\n<h1>Bad Request</h1>\n"
			"</body>\n</html>\n";
		res.prepare_payload();
		return make_http_response(std::move(res));
	}
	HttpResponsePtr http_not_found(HttpRequest& req) {
		http::response<http::string_body> res{http::status::not_found, req.version()};
		res.set(http::field::server, _env->http_server());
		res.set(http::field::content_type, "text/html");
		res.keep_alive(req.keep_alive());
		res.body() = "<!DOCTYPE html>\n<html>\n<head>\n"
			"<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\">\n"
			"<meta charset=\"UTF-8\">\n<title>404 Not Found</title>\n"
			"</head><body>\n<h1>Not Found</h1>\n"
			"</body>\n</html>\n";
		res.prepare_payload();
		return make_http_response(std::move(res));
	}
	HttpResponsePtr http_server_err(HttpRequest& req) {
		http::response<http::string_body> res{http::status::internal_server_error, req.version()};
		res.set(http::field::server, _env->http_server());
		res.set(http::field::content_type, "text/html");
		res.keep_alive(req.keep_alive());
		res.body() = "<!DOCTYPE html>\n<html>\n<head>\n"
			"<link rel=\"icon\" href=\"favicon.png\" type=\"image/png\">\n"
			"<meta charset=\"UTF-8\">\n<title>500 Internal Server Error</title>\n"
			"</head><body>\n<h1>Internal Server Error</h1>\n"
			"</body>\n</html>\n";
		res.prepare_payload();
		return make_http_response(std::move(res));
	}
	HttpResponsePtr http_unauthorized(HttpRequest& req) {
		http::response<http::empty_body> res{http::status::unauthorized, req.version()};
		res.set(http::field::server, _env->http_server());
		res.set(http::field::www_authenticate, "Bearer realm=\"example\"");
		res.keep_alive(req.keep_alive());
		res.prepare_payload();
		return make_http_response(std::move(res));
	}
	HttpResponsePtr http_redirect(HttpRequest& req, std::string_view uri) {
		http::response<http::empty_body> res{http::status::found, req.version()};
		res.set(http::field::server, _env->http_server());
		res.set(http::field::location, uri);
		res.prepare_payload();
		return make_http_response(std::move(res));
	}
	void redirect_to_ssl(HttpConnection& conn);
	HttpSession* check_http_auth(HttpRequest& req) {
		auto cookie=req[http::field::cookie];
		constexpr std::string_view tag{"token="};
		std::string tok;
		if(auto i=cookie.find(tag); i!=std::string_view::npos) {
			i+=tag.size();
			auto j=cookie.find(';', i);
			if(j==std::string_view::npos)
				j=cookie.size();
			tok=std::string{&cookie[0]+i, j-i};
		} else {
			auto auth=req[http::field::authorization];
			static std::string_view auth_type{"Bearer "};
			if(auth.size()<auth_type.size())
				return nullptr;
			if(!std::equal(auth.data(), auth.data()+auth_type.size(), auth_type.begin(), auth_type.end()))
				return nullptr;
			tok=std::string{auth.substr(auth_type.size())};
		}
		gapr::print("token: ", tok);
		auto it=_http_sessions.find(tok);
		if(it==_http_sessions.end())
			return nullptr;
		gapr::print("token: ", "asdfasdfasdf");
		return &it->second;
	}

	HttpResponsePtr handle_http_root(HttpRequest& req) {
		auto ses=check_http_auth(req);
		if(!ses)
			return http_redirect(req, "/login.html");
		// if(ses->tier>gapr::tier::proofreader)
		// 	return http_redirect(req, "/locked.html");
		if(ses->tier>gapr::tier::admin || ses->tier>gapr::tier::proofreader)
			return http_redirect(req, "/index.html");
		return http_redirect(req, "/admin.html");
	}

	HttpResponsePtr handle_http_login(HttpConnection& conn) {
		auto& req=conn.parser->get();
		if(req.method()!=http::verb::post){
			logMessage(__FILE__, "Invalid HTTP method for login request.");
			return http_server_err(req);
		}
		std::string_view args{req.body()};
		if(args.size()<=0){
			logMessage(__FILE__, "Empty body in login request.");
			return http_server_err(req);
		}
		std::size_t coli=args.find(':');
		if(coli==std::string_view::npos){
			logMessage(__FILE__, "Malformed login credentials in request.");
			return http_server_err(req);
		}
		std::string name{&args[0], coli};
		std::string_view password{&args[0]+coli+1, args.size()-coli-1};

		bool authorized{false};
		std::string gecos;
		gapr::tier tier;
		do {
			gapr::account_reader reader{*_env, name};
			if(!reader) {
				logMessage(__FILE__, "User not found: " + name);
				gapr::print("User not found: ", name);
				break;
			}
			if(!reader.login(password)){
				logMessage(__FILE__, "Invalid password for user: " + name);
				break;
			}
			authorized=true;
			gecos=reader.gecos();
			tier=reader.tier();
		} while(false);
		if(!authorized){
			logMessage(__FILE__, "Unauthorized login attempt for user: " + name);
			return http_unauthorized(req);
		}

		auto tok=gapr::gather_env::random_token(password);
		auto [it, ins]=_http_sessions.emplace(std::move(tok), HttpSession{});
		if(!ins){
			logMessage(__FILE__, "Failed to create session for user: " + name);
			return http_server_err(req);
		}
		std::swap(it->second.user, name);
		it->second.tier=tier;
		it->second.pw_ts=std::time(nullptr);

		http::response<http::string_body> res{http::status::ok, req.version()};
		res.set(http::field::server, _env->http_server());
		res.set(http::field::content_type, "application/json");
		gapr::str_glue cookie{"token=", it->first, "; Path=/; "};
		res.set(http::field::set_cookie, cookie.str());
		//Domain=...
		//Expires=Sat, 30 Jul 2022 10:04:04 GMT
		res.keep_alive(req.keep_alive());
		gapr::str_glue body{"{\"token\": \"", it->first,
			"\",\"gecos\": \"", gecos,
			"\",\"tier\": \"", static_cast<unsigned int>(tier), "\"}"};
		res.body()=body.str();
		res.prepare_payload();
		logMessage(__FILE__, "Login successful for user: " + name);
		return make_http_response(std::move(res));
	}

	HttpResponsePtr handle_http_register(HttpConnection& conn) {
		auto& req=conn.parser->get();
		if(req.method()!=http::verb::post){
			logMessage(__FILE__, "Invalid HTTP method for register request.");
			return http_server_err(req);
		}
		std::string_view args{req.body()};
		if(args.size()<=0){
			logMessage(__FILE__, "Empty body in register request.");
			return http_server_err(req);
		}
		std::size_t coli=args.find(':');
		if(coli==std::string_view::npos){
			logMessage(__FILE__, "Failed to parse username: malformed request.");
			return http_server_err(req);
		}
		std::size_t nli=args.find('\n', coli+1);
		if(nli==std::string_view::npos){
			logMessage(__FILE__, "Failed to parse password and gecos: malformed request.");
			return http_server_err(req);
		}
		std::string name{&args[0], coli};
		std::string_view password{&args[0]+coli+1, nli-coli-1};
		std::string_view gecos{&args[0]+nli+1, args.size()-nli-1};

		std::string err{};
		try {
			gapr::pw_hash hash;
			gapr::gather_env::check_username(name);
			gapr::gather_env::prepare_hash(hash, password);
			gapr::account_writer writer{*_env, name, hash, gecos};
			if(!writer)
				gapr::report("Username already exists");
		} catch(const std::runtime_error& e) {
			err=e.what();
		}

		http::response<http::string_body> res{http::status::ok, req.version()};
		res.set(http::field::server, _env->http_server());
		res.set(http::field::content_type, "application/json");
		res.keep_alive(req.keep_alive());
		res.body()=err;
		res.prepare_payload();
		return make_http_response(std::move(res));
	}

	HttpResponsePtr handle_http_chtier(HttpConnection& conn) {
		auto& req=conn.parser->get();
		if(req.method()!=http::verb::post){
			logMessage(__FILE__, "Invalid HTTP method for tier change request.");
			return http_server_err(req);
		}
		auto ses=check_http_auth(req);
		if(!ses){
			logMessage(__FILE__, "Unauthorized tier change request.");
			return http_unauthorized(req);
		}
		if(ses->tier>gapr::tier::admin){
			logMessage(__FILE__, "Insufficient permissions for tier change.");
			return http_unauthorized(req);
		}

		std::string_view args{req.body()};
		if(args.size()<=0){
			logMessage(__FILE__, "Empty body in tier change request.");
			return http_server_err(req);
		}
		std::vector<std::pair<std::string, gapr::tier>> chgs;
		while(args.size()>0) {
			if(args[0]=='\n') {
				args.remove_prefix(1);
				continue;
			}
			std::size_t coli=args.find(':');
			if(coli==std::string_view::npos){
				logMessage(__FILE__, "Malformed tier change request.");
				return http_server_err(req);
			}
			unsigned int tgt_tier_i;
			auto r=gapr::make_parser(tgt_tier_i).from_dec(&args[0]+coli+1, args.size()-coli-1);
			if(!r.second){
				logMessage(__FILE__, "Failed to parse tier level.");
				return http_server_err(req);
			}
			if(r.first>=args.size()-coli-1 || args[coli+1+r.first]!='\n'){
				logMessage(__FILE__, "Malformed tier change request.");
				return http_server_err(req);
			}
			auto tgt_tier=static_cast<gapr::tier>(tgt_tier_i);
			if(tgt_tier<ses->tier){
				logMessage(__FILE__, "Insufficient permissions for requested tier level.");
				return http_unauthorized(req);
			}
			chgs.emplace_back(std::string{&args[0], coli}, tgt_tier);
			args.remove_prefix(r.first+1+coli+1);
		}

		std::string err{};
		try {
			for(auto& [name, tgt_tier]: chgs) {
				gapr::account_writer writer{*_env, name};
				if(!writer)
					gapr::report("User not found: ", name);
				writer.tier(tgt_tier);
			}
			err="ok";
		} catch(const std::runtime_error& e) {
			err=e.what();
		}
		http::response<http::string_body> res{http::status::ok, req.version()};
		res.set(http::field::server, _env->http_server());
		res.set(http::field::content_type, "application/json");
		res.keep_alive(req.keep_alive());
		res.body()=err;
		res.prepare_payload();
		return make_http_response(std::move(res));
	}

	HttpResponsePtr handle_http_chstage(HttpConnection& conn) {
		auto& req=conn.parser->get();
		if(req.method()!=http::verb::post){
			logMessage(__FILE__, "Invalid HTTP method for chstage request.");
			return http_server_err(req);
		}
		auto ses=check_http_auth(req);
		if(!ses)
			return http_unauthorized(req);
		if(ses->tier>gapr::tier::admin)
			return http_unauthorized(req);

		std::string_view args{req.body()};
		if(args.size()<=0){
			logMessage(__FILE__, "Empty body in chstage request.");
			return http_server_err(req);
		}
		std::vector<std::pair<std::string, gapr::stage>> chgs;
		while(args.size()>0) {
			if(args[0]=='\n') {
				args.remove_prefix(1);
				continue;
			}
			std::size_t coli=args.find(':');
			if(coli==std::string_view::npos){
				logMessage(__FILE__, "Failed to parse chstage: missing colon separator.");
				return http_server_err(req);
			}
			unsigned int tgt_stage_i;
			auto r=gapr::make_parser(tgt_stage_i).from_dec(&args[0]+coli+1, args.size()-coli-1);
			if(!r.second){
				logMessage(__FILE__, "Failed to parse target stage in chstage request.");
				return http_server_err(req);
			}
			if(r.first>=args.size()-coli-1 || args[coli+1+r.first]!='\n'){
				logMessage(__FILE__, "Malformed target stage in chstage request.");
				return http_server_err(req);
			}
			auto tgt_stage=static_cast<gapr::stage>(tgt_stage_i);
			chgs.emplace_back(std::string{&args[0], coli}, tgt_stage);
			args.remove_prefix(r.first+1+coli+1);
		}

		std::string err{};
		try {
			for(auto& [name, tgt_stage]: chgs) {
				gapr::project_writer writer{*_env, name};
				if(!writer)
					gapr::report("Project not found: ", name);
				writer.stage(tgt_stage);
			}
			err="ok";
		} catch(const std::runtime_error& e) {
			err=e.what();
		}
		http::response<http::string_body> res{http::status::ok, req.version()};
		res.set(http::field::server, _env->http_server());
		res.set(http::field::content_type, "application/json");
		res.keep_alive(req.keep_alive());
		res.body()=err;
		res.prepare_payload();
		return make_http_response(std::move(res));
	}

	HttpResponsePtr handle_http_stats(HttpConnection& conn) {
		auto& req=conn.parser->get();
		if(req.method()!=http::verb::get){
			logMessage(__FILE__, "Invalid HTTP method for stats request.");
			return http_server_err(req);
		}
#if 0
		auto ses=check_http_auth(req);
		if(!ses)
			return http_unauthorized(req);
		if(ses->tier>gapr::tier::admin)
			return http_unauthorized(req);
#endif

		std::string proj;
		gapr::commit_id::data_type start=0;
		gapr::commit_id::data_type count=0;
		if(!parse_http_args(conn.api_args, proj, nullptr, start, count)){
			logMessage(__FILE__, "Failed to parse HTTP arguments for stats request.");
			return http_server_err(req);
		}

		auto it_model=_models.find(proj);
		if(it_model==_models.end()){
			logMessage(__FILE__, "Model not found for project: " + proj);
			return http_server_err(req);
		}
		auto model=it_model->second.model;
		gapr::promise<std::string> prom{};
		auto fut=prom.get_future();
		ba::post(_thr_pool.get_executor(), [model=std::move(model),prom=std::move(prom),start]() mutable {
			try {
				auto f=model->get_stats(start);
				std::move(prom).set(std::move(f));
			} catch(const std::runtime_error&) {
				unlikely(std::move(prom), std::current_exception());
			}
		});

		auto ex=_io_ctx.get_executor();
		std::move(fut).async_wait(ex, [this,conn=conn.shared_from_this(),&req](gapr::likely<std::string>&& r) mutable {
			if(!r) try {
				logMessage(__FILE__, "Error retrieving stats data.");
				return write_response(*conn, http_server_err(req));
			} catch(const std::runtime_error& e) {
				logMessage(__FILE__, "Runtime error while processing stats: " + std::string(e.what()));
				return write_response(*conn, http_server_err(req));
			}
			http::response<http::string_body> res{http::status::ok, req.version()};
			res.set(http::field::server, _env->http_server());
			res.set(http::field::content_type, "application/json");
			res.keep_alive(req.keep_alive());
			res.body()=r.get();
			res.prepare_payload();
			return write_response(*conn, make_http_response(std::move(res)));
		});
		return {};
	}

	HttpResponsePtr handle_http_proofread_stats(HttpConnection& conn) {
		auto& req=conn.parser->get();
		if(req.method()!=http::verb::get){
			logMessage(__FILE__, "Invalid HTTP method for proofread stats request.");
			return http_server_err(req);
		}
#if 0
		auto ses=check_http_auth(req);
		if(!ses)
			return http_unauthorized(req);
		if(ses->tier>gapr::tier::admin)
			return http_unauthorized(req);
#endif

		std::string proj;
		gapr::commit_id start{0};
		gapr::commit_id to{0};
		if(!parse_http_args(conn.api_args, proj, nullptr, start.data, to.data)){
			logMessage(__FILE__, "Failed to parse HTTP arguments for proofread stats request.");
			return http_server_err(req);
		}

		auto it_model=_models.find(proj);
		if(it_model==_models.end()){
			logMessage(__FILE__, "Model not found for project: " + proj);
			return http_server_err(req);
		}
		auto model=it_model->second.model;
		gapr::promise<std::string> prom{};
		auto fut=prom.get_future();
		ba::post(_thr_pool.get_executor(), [model=std::move(model),prom=std::move(prom),start,to]() mutable {
			try {
				auto f=model->get_proofread_stats(start, to);
				std::move(prom).set(std::move(f));
			} catch(const std::runtime_error&) {
				unlikely(std::move(prom), std::current_exception());
			}
		});

		auto ex=_io_ctx.get_executor();
		std::move(fut).async_wait(ex, [this,conn=conn.shared_from_this(),&req](gapr::likely<std::string>&& r) mutable {
			if(!r) try {
				logMessage(__FILE__, "Error retrieving proofread stats data.");
				return write_response(*conn, http_server_err(req));
			} catch(const std::runtime_error& e) {
				logMessage(__FILE__, "Runtime error while processing proofread stats: " + std::string(e.what()));
				return write_response(*conn, http_server_err(req));
			}
			http::response<http::string_body> res{http::status::ok, req.version()};
			res.set(http::field::server, _env->http_server());
			res.set(http::field::content_type, "application/json");
			res.keep_alive(req.keep_alive());
			res.body()=r.get();
			res.prepare_payload();
			return write_response(*conn, make_http_response(std::move(res)));
		});
		return {};
	}

	HttpResponsePtr handle_http_catalog(HttpConnection& conn) {
		auto& req=conn.parser->get();

		logMessage(__FILE__, "Received HTTP catalog request.");

		if(req.method()!=http::verb::get && req.method()!=http::verb::put){
			logMessage(__FILE__, "Invalid HTTP method for catalog request.");
			return http_server_err(req);
		}
#if 0
		auto ses=check_http_auth(req);
		if(!ses)
			return http_unauthorized(req);
		if(ses->tier>gapr::tier::admin)
			return http_unauthorized(req);
#endif

		std::string proj;
		std::string passwd;
		logMessage(__FILE__, "Raw API arguments: " + std::string(conn.api_args));

		if(!parse_http_args(conn.api_args, proj, passwd)){
			logMessage(__FILE__, "Failed to parse HTTP arguments for catalog request.");
			return http_server_err(req);
		}

		logMessage(__FILE__, "Parsed project: " + proj);

		if(req.method()==http::verb::get) {
			logMessage(__FILE__, "Handling GET request for catalog.");

			auto path2=_env->image_catalog(proj);
			if(path2.empty()){
				logMessage(__FILE__, "Failed to locate catalog for project: " + proj);
				return http_server_err(req);
			}
			return handle_data_request_impl(req, path2);
		}

		logMessage(__FILE__, "Handling PUT request for catalog.");

		std::string err{};
		std::string token;
		try {
			{
				logMessage(__FILE__, "Writing catalog to file for project: " + proj);

				std::ofstream fcat{_env->image_catalog(proj, false)};
				fcat<<req.body();
				fcat.close();
				if(!fcat){
					logMessage(__FILE__, "Failed to write catalog to file for project: " + proj);
					throw std::runtime_error{"failed to write catalog"};
				}
			}
			logMessage(__FILE__, "Catalog file written successfully for project: " + proj);

			gapr::pw_hash hash;
			token.resize(hash.size()*2);
			gapr::gather_env::prepare_secret(hash, passwd);
			dump_binary(&token[0], &hash[0], hash.size());
			gapr::print(1, "token: ", token);
			gapr::project_writer writer{*_env, proj, hash, ""};
			if(!writer){
				logMessage(__FILE__, "Failed to create project writer for project: " + proj);
				gapr::report("Project not created: ", proj);
			}
			gapr::print(1, "created: ", token);
			logMessage(__FILE__, "Catalog uploaded and project created successfully for project: " + proj);
		} catch(const std::runtime_error& e) {
			gapr::print(1, "err: ", e.what());
			err=e.what();
		}

		http::response<http::string_body> res{err.empty()?http::status::ok:http::status::internal_server_error, req.version()};
		res.set(http::field::server, _env->http_server());
		res.set(http::field::content_type, "application/json");
		res.keep_alive(req.keep_alive());
		res.body()=err.empty()?token:err;
		res.prepare_payload();

		if (!err.empty()) {
        	logMessage(__FILE__, "Responding with error: " + err);
    	}

		return make_http_response(std::move(res));
	}

	struct string_pair_hash {
		std::size_t operator()(const std::pair<std::string, std::string>& pair) const noexcept {
			std::hash<std::string> h{};
			return h(pair.first)*h(pair.second);
		}
	};
	std::unordered_map<std::pair<std::string, std::string>, std::pair<std::shared_ptr<HttpConnection>, uint64_t>, string_pair_hash> _pending_data_reqs;
	uint64_t _pending_data_seq{0};
	void pending_data_schedule(const std::string& proj, const std::string& path, HttpConnection& conn) {
		auto [it, ins]=_pending_data_reqs.try_emplace({proj, path});
		if(ins) {
			it->second.second=++_pending_data_seq;
			assert(!it->second.first);
			conn.next={};
		} else {
			conn.next=std::move(it->second.first);
		}
		it->second.first=conn.shared_from_this();
	}
	void pending_data_schedule(const std::string& proj, const std::string& path) {
		auto [it, ins]=_pending_data_reqs.try_emplace({proj, path});
		if(ins) {
			it->second.second=++_pending_data_seq;
			assert(!it->second.first);
			it->second.first={};
		}
	}
	
	void pending_data_get(const std::string& proj, std::ostream& str, uint64_t ts_sync) {
		//logMessage(__FILE__, "Starting pending_data_get for project: " + proj);
		
		auto it_end = _pending_data_reqs.end();
		std::vector<decltype(it_end)> iters;

		// Log the size of _pending_data_reqs before processing
		//logMessage(__FILE__, "Pending data requests size: " + std::to_string(_pending_data_reqs.size()));

		for (auto it = _pending_data_reqs.begin(); it != it_end; ++it) {
			if (it->first.first == proj) {
				logMessage(__FILE__, "Matching project found in pending data requests: " + it->first.first);
				iters.emplace_back(it);
			}
		}

		// Log the number of matched entries for sorting
		//logMessage(__FILE__, "Number of matching entries for project " + proj + ": " + std::to_string(iters.size()));

		// Sorting the matched entries
		std::sort(iters.begin(), iters.end(), [](auto a, auto b) {
			return a->second.second < b->second.second;
		});
		//logMessage(__FILE__, "Sorted matching entries by timestamp for project: " + proj);

		// Process each entry in iters
		for (auto it : iters) {
			logMessage(__FILE__, "Processing entry with timestamp: " + std::to_string(it->second.second) + " for key: " + it->first.second);
			
			// If the condition matches, erase the entry
			if (it->second.second <= ts_sync && !it->second.first) {
				logMessage(__FILE__, "Erasing pending data request for project: " + proj + ", key: " + it->first.second);
				_pending_data_reqs.erase(it);
				continue;
			}

			// Log the output being added to the stream
			str << it->second.second << ":" << it->first.second << "\n";
			logMessage(__FILE__, "Added data to stream for project: " + proj + ", timestamp: " + std::to_string(it->second.second) + ", key: " + it->first.second);
		}

		//logMessage(__FILE__, "Finished processing pending data for project: " + proj);
	}

	void pending_data_dispatch(const std::string& proj, const std::string& path, const std::string& data) {
		auto it=_pending_data_reqs.find({proj, path});
		if(it==_pending_data_reqs.end())
			return;
		auto conn=std::move(it->second.first);
		while(conn) {
			auto conn_next=std::move(conn->next);
			boost::asio::post(_io_ctx, [this,conn=std::move(conn),data]() {
				auto& req=conn->parser->get();
				if(data.empty()){
					logMessage(__FILE__, "Data is empty for pending data dispatch, sending HTTP server error.");
					return write_response(*conn, http_server_err(req));
				}
				auto resptr=handle_data_request_impl(req, data);
				write_response(*conn, std::move(resptr));
			});
			conn=std::move(conn_next);
		}
	}

	static std::string_view resolve_rel_path(std::string& res, std::string_view ref, std::string_view rel) {
		if(rel[0]=='/')
			return rel;
		auto last_slash=[](std::string_view s) {
			auto n=s.size();
			while(n>0 && s[n-1]!='/')
				--n;
			return n>0 ? n-1 : n;
		};
		while(!rel.empty()) {
			if(rel.size()>3 && rel[0]=='.' && rel[1]=='.' && rel[2]=='/') {
				ref=ref.substr(0, last_slash(ref));
				rel=rel.substr(3);
				continue;
			}
			if(rel.size()>2 && rel[0]=='.' && rel[1]=='/') {
				rel=rel.substr(2);
				continue;
			}
			break;
		}
		auto n=last_slash(ref);
		res.reserve(n+1+rel.size());
		res=ref.substr(0, n+1);
		res+=rel;
		return res;
	}

	void schedule_future_gets(const boost::beast::http::fields& req, std::string_view target) {
		using namespace std::string_view_literals;
		auto pfx="/api/data/"sv;
		std::string abspath;

		for(auto& item: req) {
			if(item.name_string()!="GaprWillGet"sv)
				continue;
			auto val=resolve_rel_path(abspath, target, item.value());
			if(val.size()<=pfx.size())
				continue;
			if(val.substr(0, pfx.size())!=pfx)
				continue;
			val.remove_prefix(pfx.size()-1);
			std::string proj;
			std::string relp;
			if(!parse_http_args(val, proj, relp))
				continue;
			auto path2=_env->image_data(proj, relp);
			if(!path2.empty())
				continue;
			pending_data_schedule(proj, relp);
		}
	}
	
	HttpResponsePtr handle_http_data(HttpConnection& conn) {
		auto& req=conn.parser->get();
		if(req.method()!=http::verb::get && req.method()!=http::verb::put){
			logMessage(__FILE__, "Invalid HTTP method for data request.");
			return http_server_err(req);
		}
#if 0
		auto ses=check_http_auth(req);
		if(!ses)
			return http_unauthorized(req);
		if(ses->tier>gapr::tier::admin)
			return http_unauthorized(req);
#endif

		std::string proj;
		std::string relp;
		if(!parse_http_args(conn.api_args, proj, relp)){
			logMessage(__FILE__, "Failed to parse HTTP arguments for data request.");
			return http_server_err(req);
		}

		if(req.method()==http::verb::get) {
			auto path2=_env->image_data(proj, relp);
			if(path2.empty()) {
				pending_data_schedule(proj, relp, conn);
				schedule_future_gets(req, req.target());
				return {};
			}
			pending_data_schedule(proj, relp);
			schedule_future_gets(req, req.target());
			return handle_data_request_impl(req, path2);
		}

		std::string err{};
		std::string path2{_env->image_data(proj, relp, false)};
		try {
			{
				std::filesystem::path dir{path2};
				dir=dir.parent_path();
				std::filesystem::create_directories(dir);
				std::ofstream fdat{path2};
				fdat<<req.body();
				fdat.close();
				if(!fdat)
					throw std::runtime_error{"failed to write data"};
			}
		} catch(const std::runtime_error& e) {
			gapr::print(1, "err: ", e.what());
			err=e.what();
		}
		pending_data_dispatch(proj, relp, path2);

		http::response<http::string_body> res{err.empty()?http::status::ok:http::status::internal_server_error, req.version()};
		res.set(http::field::server, _env->http_server());
		res.set(http::field::content_type, "application/json");
		res.keep_alive(req.keep_alive());
		res.body()=err.empty()?"ok":err;
		res.prepare_payload();
		return make_http_response(std::move(res));
	}

	HttpResponsePtr handle_http_pending(HttpConnection& conn) {
		auto& req=conn.parser->get();
		if(req.method()!=http::verb::get){
			logMessage(__FILE__, "Invalid HTTP method for pending request.");
			return http_server_err(req);
		}

		// logMessage(__FILE__, "Raw API arguments for pending request: " + std::string(conn.api_args));
#if 0
		auto ses=check_http_auth(req);
		if(!ses)
			return http_unauthorized(req);
		if(ses->tier>gapr::tier::admin)
			return http_unauthorized(req);
#endif

		std::string proj;
		uint64_t ts_sync{0};
		if(!parse_http_args(conn.api_args, proj, nullptr, ts_sync)){
			logMessage(__FILE__, "Failed to parse HTTP arguments for pending request.");
			return http_server_err(req);
		}

		std::string err{};
		std::ostringstream str;
		assert(_io_ctx.get_executor().running_in_this_thread());
		try {
			// logMessage(__FILE__, "Processing pending data for project: " + proj + " with ts_sync: " + std::to_string(ts_sync));

			// Log before attempting to update _models
			//logMessage(__FILE__, "Attempting to update pending timestamp for project: " + proj);

			std::shared_ptr<gather_model> model = std::make_shared<gather_model>();
			_models[proj] = ModelState(std::move(model));
			_models[proj].pending_ts = std::chrono::steady_clock::now();
			//logMessage(__FILE__, "Project successfully added to _models: " + proj);

			_models.at(proj).pending_ts = std::chrono::steady_clock::now();

			// Log before calling pending_data_get
			//logMessage(__FILE__, "Calling pending_data_get for project: " + proj);

			pending_data_get(proj, str, ts_sync);
			
			// Log after calling pending_data_get
			// logMessage(__FILE__, "Returned from pending_data_get for project: " + proj);
		} catch (const std::out_of_range& e) {
			logMessage(__FILE__, "Project not found in _models: " + proj);
			err = e.what();
		} catch (const std::runtime_error& e) {
			gapr::print(1, "err: ", e.what());
			err = e.what();
			logMessage(__FILE__, "Runtime error while processing pending data: " + err);
		}

		http::response<http::string_body> res{err.empty()?http::status::ok:http::status::internal_server_error, req.version()};
		res.set(http::field::server, _env->http_server());
		res.set(http::field::content_type, "application/json");
		res.keep_alive(req.keep_alive());
		res.body()=err.empty()?str.str():err;
		res.prepare_payload();
		return make_http_response(std::move(res));
	}

	std::chrono::system_clock::time_point maybe_update(std::vector<std::pair<std::string, std::shared_ptr<gather_model>>>&& todo);

	HttpResponsePtr handle_http_rank(HttpConnection& conn) {
		auto& req=conn.parser->get();
		if(req.method()!=http::verb::get){
			logMessage(__FILE__, "Invalid HTTP method for rank request.");
			return http_server_err(req);
		}
#if 0
		auto ses=check_http_auth(req);
		if(!ses)
			return http_unauthorized(req);
		if(ses->tier>gapr::tier::proofreader)
			return http_unauthorized(req);
#endif

		std::string_view args{req.body()};
		if(args.size()>0){
			logMessage(__FILE__, "Unexpected body in rank request.");
			return http_server_err(req);
		}

		std::vector<std::pair<std::string, std::shared_ptr<gather_model>>> todo;
		for(auto& [proj, state]: _models)
			todo.emplace_back(proj, state.model);
		gapr::promise<std::string> prom{};
		auto fut=prom.get_future();
		ba::post(_thr_pool, [this,prom=std::move(prom),todo=std::move(todo)]() mutable {
			try {
				auto ts=std::chrono::system_clock::to_time_t(maybe_update(std::move(todo)));
				std::unique_lock<std::mutex> lck;
				auto& ranks=_stats.per_user(lck);
				std::ostringstream oss;
				oss<<"{\"time\":\"";
				char time_str[128];
				auto n=std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S %z", std::localtime(&ts));
				if(n>0)
					oss.write(time_str, n);
				else
					oss<<"ERR";
				oss<<"\",\"ranks\":[";
				bool first=true;
				for(auto& rank: ranks) {
					if(first) {
						first=false;
					} else {
						oss<<',';
					}
					oss<<"{\"u\":\""<<rank.name;
					oss<<"\",\"sd\":"<<rank.score_d;
					oss<<",\"sdpr\":"<<rank.score_d_pr;
					oss<<",\"sdrep\":"<<rank.score_d_rep;
					oss<<",\"sm\":"<<rank.score_m;
					oss<<",\"rd\":"<<rank.rank_d;
					oss<<",\"rm\":"<<rank.rank_m<<"}";
				}
				oss<<"]}";
				std::move(prom).set(oss.str());
			} catch(const std::runtime_error&) {
				unlikely(std::move(prom), std::current_exception());
			}
		});

		auto ex=conn.ssl->get_executor();
		std::move(fut).async_wait(ex, [this,conn=conn.shared_from_this(),&req](gapr::likely<std::string>&& r) mutable {
			auto& req=conn->parser->get();
			if(!r) try {
				logMessage(__FILE__, "Rank request failed, sending HTTP server error.");
				return write_response(*conn, http_server_err(req));
			} catch(const std::runtime_error& e) {
				logMessage(__FILE__, std::string("Error during rank response: ") + e.what());
				return write_response(*conn, http_server_err(req));
			}
			http::response<http::string_body> res{http::status::ok, req.version()};
			res.set(http::field::server, _env->http_server());
			res.set(http::field::content_type, "application/json");
			res.keep_alive(req.keep_alive());
			res.body()=r.get();
			res.prepare_payload();
			return write_response(*conn, make_http_response(std::move(res)));
		});
		return {};
	}

	HttpResponsePtr handle_http_progress(HttpConnection& conn) {
		auto& req = conn.parser->get();
		if (req.method() != http::verb::get) {
			logMessage(__FILE__, "Invalid HTTP method for progress request.");
			return http_server_err(req);
		}

		std::string_view args{req.body()};
		if (args.size() > 0) {
			logMessage(__FILE__, "Unexpected body in progress request.");
			return http_server_err(req);
		}

		std::vector<std::pair<std::string, std::shared_ptr<gather_model>>> todo;
		std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_pendings;
		
		// Log to check if the thread context is correct
		logMessage(__FILE__, "Checking if running in the correct thread context.");
		assert(_io_ctx.get_executor().running_in_this_thread());

		// Logging each project's model being added to todo and last_pendings
		for (auto& [proj, state] : _models) {
			logMessage(__FILE__, "Processing project: " + proj);
			todo.emplace_back(proj, state.model);
			last_pendings.emplace(proj, state.pending_ts);
			logMessage(__FILE__, "Added project " + proj + " to last_pendings with timestamp.");
		}

		gapr::promise<std::string> prom{};
		auto fut = prom.get_future();

		    ba::post(_thr_pool, [this, prom = std::move(prom), todo = std::move(todo), last_pendings = std::move(last_pendings)]() mutable {
			try {
				auto steady_ts = std::chrono::steady_clock::now();
				logMessage(__FILE__, "Current steady timestamp captured.");

				// Maybe update todo and fetch a time value
				auto ts = std::chrono::system_clock::to_time_t(maybe_update(std::move(todo)));
				logMessage(__FILE__, "Updated todo list and fetched system timestamp.");

				std::unique_lock<std::mutex> lck;
				auto& progs = _stats.per_grp(lck);
				logMessage(__FILE__, "Fetched progress statistics per group.");

				std::ostringstream oss;
				oss << "{\"time\":\"";
				char time_str[128];
				auto n = std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S %z", std::localtime(&ts));
				if (n > 0) {
					oss.write(time_str, n);
				} else {
					logMessage(__FILE__, "Failed to format the timestamp.");
					oss << "ERR";
				}
				
				bool first = true;
				oss << "\",\"stats\":[";
				for (auto& prog : progs) {
					if (first) {
						first = false;
					} else {
						oss << ',';
					}

					// Logging each program's progress information
					logMessage(__FILE__, "Processing progress for program: " + prog.name);

					oss << "{\"g\":\"" << prog.name;
					oss << "\",\"nn\":" << prog.num_nodes;
					oss << ",\"nnr\":" << prog.num_nodes_raw;
					oss << ",\"nt\":" << prog.num_terms;
					oss << ",\"ntr\":" << prog.num_terms_raw;

					// Log key before accessing the unordered_map and catch potential out_of_range errors
					logMessage(__FILE__, "Accessing last pending timestamp for project: " + prog.name);
					auto last_pending = std::chrono::duration_cast<std::chrono::milliseconds>(steady_ts - last_pendings.at(prog.name)).count();
					assert(last_pending >= 0);
					logMessage(__FILE__, "Last pending time for project " + prog.name + ": " + std::to_string(last_pending) + " ms.");

					if (last_pending < 120'000) {
						oss << ",\"lp\":" << last_pending / 1000.0;
					}
					oss << ",\"c\":" << prog.num_commits;
					oss << ",\"cd\":" << prog.num_commits_d;
					oss << ",\"cm\":" << prog.num_commits_m << "}";
				}
				oss << "]}";
				logMessage(__FILE__, "Generated JSON response for progress data.");

				std::move(prom).set(oss.str());
			} catch (const std::out_of_range& e) {
				logMessage(__FILE__, std::string("Out of range error: ") + e.what());
				unlikely(std::move(prom), std::current_exception());
			} catch (const std::runtime_error& e) {
				logMessage(__FILE__, std::string("Runtime error during progress update: ") + e.what());
				unlikely(std::move(prom), std::current_exception());
			}
		});

		auto ex = conn.ssl->get_executor();
		std::move(fut).async_wait(ex, [this, conn = conn.shared_from_this(), &req](gapr::likely<std::string>&& r) mutable {
			auto& req = conn->parser->get();
			if (!r) {
				try {
					logMessage(__FILE__, "Progress request failed, sending HTTP server error.");
					return write_response(*conn, http_server_err(req));
				} catch (const std::runtime_error& e) {
					logMessage(__FILE__, std::string("Error during progress response: ") + e.what());
					return write_response(*conn, http_server_err(req));
				}
			}

			logMessage(__FILE__, "Progress request successful, sending response.");
			http::response<http::string_body> res{http::status::ok, req.version()};
			res.set(http::field::server, _env->http_server());
			res.set(http::field::content_type, "application/json");
			res.keep_alive(req.keep_alive());
			res.body() = r.get();
			res.prepare_payload();
			return write_response(*conn, make_http_response(std::move(res)));
		});

		return {};
	}

	HttpResponsePtr handle_data_request_impl(HttpRequest& req, const std::string& path) {
		std::array<char, 128> etag;
		auto etag_len=gapr::gather_env::generate_etag(etag, path);
		bool r304{false};
		if(etag_len) {
			auto etag2=req[http::field::if_none_match];
			if(etag2==std::string_view{&etag[0], etag_len})
				r304=true;
		}

		http::response_header<> res_hdr;
		res_hdr.version(req.version());
		res_hdr.set(http::field::server, _env->http_server());
		res_hdr.set(http::field::content_type, mime_type(path));
		res_hdr.set(http::field::cache_control, "max-age=60");
		if(etag_len)
			res_hdr.set(http::field::etag, std::string_view(&etag[0], etag_len));
		if(r304) {
			http::response<http::empty_body> res{std::move(res_hdr)};
			res.result(http::status::not_modified);
			res.keep_alive(req.keep_alive());
			return make_http_response(std::move(res));
		}
		bs_error_code ec;
		http::file_body::value_type body;
		gapr::print("path: ^", path, '$');
		body.open(path.c_str(), beast::file_mode::scan, ec);
		if(ec==beast::errc::no_such_file_or_directory)
			return http_not_found(req);
		if(ec) {
			gapr::print("http server err: ", ec.message());
			logMessage(__FILE__, "HTTP server error while opening file: " + ec.message());
			return http_server_err(req);
		}
		const auto size=body.size();
		http::response<http::file_body> res{std::move(res_hdr), std::move(body)};
		res.result(http::status::ok);
		res.content_length(size);
		res.keep_alive(req.keep_alive());
		return make_http_response(std::move(res));
	}

	HttpResponsePtr handle_data_request(HttpRequest& req) {
		if(req.method()!=http::verb::get)
			return http_bad_request(req);
		auto path_=req.target();
		if(path_.empty() || path_[0]!='/' || path_.find("..")!=path_.npos)
			return http_bad_request(req);
		std::string path=_env->html_docroot();
		if(!path.empty() && path.back()=='/')
			path.pop_back();
		path.append(path_.data(), path_.size());
		if(path.back()=='/')
			path+="index.html";
		return handle_data_request_impl(req, path);
	}


	static void handle_message_simple_(gapr::server_end&& msg, gapr::server_end::msg_hdr&& hdr);
	template<typename... Args>
		static void handle_message_simple(gapr::server_end&& msg, const char* tag, Args&&... args) {
			gapr::print("handle msg simple: ", tag);
			gapr::server_end::msg_hdr hdr{tag, std::forward<Args>(args)...};
			handle_message_simple_(std::move(msg), std::move(hdr));
		}

	void handle_CMD_LOGIN(Session& ses, gapr::server_end::msg_hdr_in hdr, gapr::server_end&& msg) {
		auto args=hdr.args();
		if(args.size()<=0)
			return handle_message_simple(std::move(msg),
					"ERR", "need USERNAME:PASSWORD");
		std::size_t coli=args.find(':');
		if(coli==std::string_view::npos)
			return handle_message_simple(std::move(msg),
					"ERR", "need USERNAME:PASSWORD");
		std::string name{&args[0], coli};
		std::string_view password{&args[0]+coli+1, args.size()-coli-1};

		bool authorized{false};
		std::string gecos;
		gapr::tier tier;
		do {
			gapr::account_reader reader{*_env, name};
			if(!reader) {
				gapr::print("User not found: ", name);
				break;
			}
			if(!reader.login(password))
				break;
			gecos=reader.gecos();
			tier=reader.tier();
			authorized=true;
		} while(false);
		if(!authorized)
			return handle_message_simple(std::move(msg),
					"NO", "Authentication failure.");

		std::swap(ses.user, name);
		ses.tier=tier;
		handle_message_simple(std::move(msg),
				"OK", static_cast<unsigned int>(tier), gecos.c_str());
	}

	void handle_CMD_SELECT(Session& ses, gapr::server_end::msg_hdr_in hdr, gapr::server_end&& msg) {
		auto args=hdr.args();
		if(args.size()<=0)
			return handle_message_simple(std::move(msg),
					"ERR", "need DATASET");
		std::string name{args};

		if(ses.tier>gapr::tier::restricted)
			return handle_message_simple(std::move(msg),
					"NO", "Permission denied.");

		gapr::tier tier2;
		gapr::stage stage;
		std::string secret;
		std::string desc;
		uint64_t last_commit;
		do {
			gapr::project_reader reader{*_env, name};
			if(!reader)
				return handle_message_simple(std::move(msg),
						"NO", "project not found");
			tier2=reader.access(ses.user, ses.tier);
			stage=reader.stage();
			secret=reader.secret();
			desc=reader.desc();

			auto it=_models.find(name);
			if(it==_models.end()) {
				if(ses.tier<=gapr::tier::admin)
					load_project(std::move(name));
				return handle_message_simple(std::move(msg),
						"NO", "no repository");
			}
			auto model=it->second.model;
			last_commit=model->num_commits();
		} while(false);
		if(tier2==gapr::tier::nobody)
			return handle_message_simple(std::move(msg),
					"NO", "Permission denied.");

		std::swap(ses.project, name);
		ses.tier2=tier2;
		ses.stage=stage;
		supersede_session(ses);
		handle_message_simple(std::move(msg), "OK",
				static_cast<unsigned int>(tier2), static_cast<unsigned int>(stage),
				last_commit, secret.c_str(), desc.c_str());
	}

	// XXX in http?
	void handle_CMD_GET_CATALOG(Session& ses, gapr::server_end::msg_hdr_in hdr, gapr::server_end&& msg) {
		auto args=hdr.args();
		if(args.size()>0)
			return handle_message_simple(std::move(msg),
					"ERR", "no args needed");

		if(ses.tier2>gapr::tier::restricted)
			return handle_message_simple(std::move(msg),
					"NO", "Permission denied.");

		auto path2=_env->image_catalog(ses.project);
		if(path2.empty())
			return handle_message_simple(std::move(msg),
					"NO", "Not found.");

		gapr::server_end::msg_hdr ohdr{"OK"};
		auto str=gapr::make_streambuf(path2.c_str());
		return async_send_file(_io_ctx, _thr_pool, std::move(ohdr), std::move(str), std::move(msg));
	}

	void handle_CMD_GET_STATE(Session& ses, gapr::server_end::msg_hdr_in hdr, gapr::server_end&& msg) {
		auto args=hdr.args();
		if(args.size()>0)
			return handle_message_simple(std::move(msg),
					"ERR", "no args needed");

		if(ses.tier>gapr::tier::restricted)
			return handle_message_simple(std::move(msg),
					"NO", "Permission denied.");

		auto path2=_env->session_state(ses.user, ses.project);
		if(path2.empty())
			return handle_message_simple(std::move(msg),
					"NO", "Not found.");

		gapr::server_end::msg_hdr ohdr{"OK"};
		auto str=gapr::make_streambuf(path2.c_str());
		return async_send_file(_io_ctx, _thr_pool, std::move(ohdr), std::move(str), std::move(msg));
	}

	void handle_CMD_GET_COMMIT(Session& ses, gapr::server_end::msg_hdr_in hdr, gapr::server_end&& msg) {
		auto args=hdr.args();
		if(args.size()<=0)
			return handle_message_simple(std::move(msg),
					"ERR", "args needed");
		uint64_t id;
		auto r=gapr::make_parser(id).from_dec(&args[0], args.size());
		if(!r.second)
			return handle_message_simple(std::move(msg),
					"ERR", "need an uint64_t");
		if(r.first<args.size())
			return handle_message_simple(std::move(msg),
					"ERR", "too many args");

		if(ses.tier2>gapr::tier::restricted)
			return handle_message_simple(std::move(msg),
					"NO", "Permission denied.");

		auto it_model=_models.find(ses.project);
		if(it_model==_models.end())
			return handle_message_simple(std::move(msg),
					"NO", "no repository");
		auto sbuf=it_model->second.model->get_commit(id);
		if(!sbuf)
			return handle_message_simple(std::move(msg),
					"NO", "Not found.");

		gapr::server_end::msg_hdr ohdr{"OK"};
		return async_send_file_enc(_io_ctx, _thr_pool, std::move(ohdr), std::move(sbuf), std::move(msg));
	}

	void handle_CMD_GET_COMMITS(Session& ses, gapr::server_end::msg_hdr_in hdr, gapr::server_end&& msg) {
		auto args=hdr.args();
		if(args.size()<=0)
			return handle_message_simple(std::move(msg),
					"ERR", "args needed");
		uint64_t upto;
		auto r=gapr::make_parser(upto).from_dec(&args[0], args.size());
		if(!r.second)
			return handle_message_simple(std::move(msg),
					"ERR", "need an uint64_t");
		if(r.first<args.size())
			return handle_message_simple(std::move(msg),
					"ERR", "too many args");

		if(ses.tier2>gapr::tier::restricted) {
			discard_input(msg);
			return handle_message_simple(std::move(msg),
					"NO", "Permission denied.");
		}

		auto fut=read_stream(msg);
		auto ex=msg.get_executor();
		std::move(fut).async_wait(ex, [this,ses=ses.shared_from_this(),msg=std::move(msg),upto](gapr::likely<gapr::mem_file>&& file) mutable {
			if(!file) try {
				auto ec=file.error_code();
				return handle_message_simple(std::move(msg),
						"ERR", ec.message());
			} catch(const std::runtime_error& e) {
				return handle_message_simple(std::move(msg),
						"ERR", e.what());
			}

			gapr::promise<gapr::commit_history> prom{};
			auto fut=prom.get_future();
			ba::post(_thr_pool.get_executor(), [file=std::move(file.get()),prom=std::move(prom)]() mutable {
				try {
					gapr::commit_history hist;
					auto buf=gapr::make_streambuf(std::move(file));
					if(!hist.load(*buf)) {
						hist.body_count(std::numeric_limits<uint64_t>::max());
					}
					std::move(prom).set(std::move(hist));
				} catch(const std::runtime_error&) {
					unlikely(std::move(prom), std::current_exception());
				}
			});
			auto ex=msg.get_executor();
			std::move(fut).async_wait(ex, [this,ses=std::move(ses),msg=std::move(msg),upto](gapr::likely<gapr::commit_history>&& res) mutable {
				if(!res) try {
					auto ec=res.error_code();
					return handle_message_simple(std::move(msg),
							"ERR", ec.message());
				} catch(const std::runtime_error& e) {
					return handle_message_simple(std::move(msg),
							"ERR", e.what());
				}
				auto hist=std::move(res.value());
				if(hist.body_count()>=upto)
					return handle_message_simple(std::move(msg),
							"ERR", "bad history");

				auto it_model=_models.find(ses->project);
				if(it_model==_models.end())
					return handle_message_simple(std::move(msg),
							"NO", "no repository");

				auto model=it_model->second.model;
				gapr::server_end::msg_hdr ohdr{"OK"};
				msg.async_send(std::move(ohdr), 0, 0, [this,model=std::move(model),msg,hist=std::move(hist),upto](bs_error_code ec) mutable {
					if(ec)
						return;
					return async_send_files(_io_ctx, _thr_pool, std::move(model), std::move(msg), std::move(hist), upto);
				});
			});
		});
	}

	void handle_CMD_GET_MODEL(Session& ses, gapr::server_end::msg_hdr_in hdr, gapr::server_end&& msg) {
		auto args=hdr.args();
		if(args.size()>0)
			return handle_message_simple(std::move(msg),
					"ERR", "no args needed");

		if(ses.tier2>gapr::tier::restricted)
			return handle_message_simple(std::move(msg),
					"NO", "Permission denied.");

		auto it_model=_models.find(ses.project);
		if(it_model==_models.end())
			return handle_message_simple(std::move(msg),
					"NO", "no repository");
		auto file=it_model->second.state;
		if(!file)
			return handle_message_simple(std::move(msg),
					"NO", "Not found.");

		gapr::server_end::msg_hdr ohdr{"OK"};
		return async_send_mem_file(_io_ctx, std::move(ohdr), std::move(file), std::move(msg));
	}

	void handle_CMD_COMMIT(Session& ses, gapr::server_end::msg_hdr_in hdr, gapr::server_end&& msg) {
		//XXX make args parsing generic
		//auto args=parse_tuple<...>(...);
		auto args=hdr.args();
		if(args.size()<=0)
			return handle_message_simple(std::move(msg),
					"ERR", "args needed");
		std::underlying_type_t<gapr::delta_type> type;
		auto r=gapr::make_parser(type).from_dec(&args[0], args.size());
		if(!r.second)
			return handle_message_simple(std::move(msg),
					"ERR", "need type");
		if(r.first>=args.size() || args[r.first]!=':')
			return handle_message_simple(std::move(msg),
					"ERR", "need base");
		args.remove_prefix(r.first+1);
		uint64_t base;
		r=gapr::make_parser(base).from_dec(args.data(), args.size());
		if(!r.second)
			return handle_message_simple(std::move(msg),
					"ERR", "need base");
		if(r.first>=args.size() || args[r.first]!=':')
			return handle_message_simple(std::move(msg),
					"ERR", "need tip");
		args.remove_prefix(r.first+1);
		uint64_t tip;
		r=gapr::make_parser(tip).from_dec(args.data(), args.size());
		if(!r.second)
			return handle_message_simple(std::move(msg),
					"ERR", "need tip");
		if(r.first<args.size())
			return handle_message_simple(std::move(msg),
					"ERR", "too many args");
		auto& hist=ses.history;
		{
			if(base!=0) {
				if(base<hist.body_count())
					return handle_message_simple(std::move(msg),
							"ERR", "wrong base");
				if(base==hist.body_count())
					base=0;
			}
			if(tip!=0) {
				if(hist.tail().empty()) {
					if(tip<=hist.body_count())
						return handle_message_simple(std::move(msg),
								"ERR", "wrong tip");
				} else {
					if(tip<hist.tail().front())
						return handle_message_simple(std::move(msg),
								"ERR", "wrong tip");
					if(tip==hist.tail().front())
						tip=0;
				}
			}
		}

		if(ses.tier2>gapr::tier::proofreader) {
			discard_input(msg);
			return handle_message_simple(std::move(msg),
					"NO", /*"Permission denied."*/0);
		}
		if(ses.tier2>_env->commit_min_tier(gapr::delta_type{type}, ses.stage)) {
			discard_input(msg);
			return handle_message_simple(std::move(msg),
					"NO", /*"Permission denied."*/0);
		}

		auto it_model=_models.find(ses.project);
		if(it_model==_models.end()) {
			discard_input(msg);
			return handle_message_simple(std::move(msg),
					"NO", "no repository", ses.project);
		}
		auto model=it_model->second.model;
		{
			if(tip!=0)
				hist.add_tail(tip);
			if(base!=0)
				hist.body_count(base);
		}

		auto fut=read_stream(msg);
		auto ex=msg.get_executor();
		std::move(fut).async_wait(ex, [this,model=std::move(model),msg=std::move(msg),ses=ses.shared_from_this(),type](gapr::likely<gapr::mem_file>&& file) mutable {
			if(!file) try {
				auto ec=file.error_code();
				return handle_message_simple(std::move(msg),
						"ERR", ec.message());
			} catch(const std::runtime_error& e) {
				return handle_message_simple(std::move(msg),
						"ERR", e.what());
			}

			gapr::promise<std::tuple<uint32_t, uint64_t, uint64_t>> prom{};
			auto fut=prom.get_future();
			const auto& hist=ses->history;
			auto proj=ses->project;
			auto who=ses->user;
			auto num_commits=model->num_commits();
			ba::post(_thr_pool.get_executor(), [model=std::move(model),&hist,type,file=std::move(file.get()),prom=std::move(prom),who=std::move(who)]() mutable {
				try {
					gather_model::modifier modif{*model, static_cast<gapr::delta_type>(type), std::move(file)};
					auto next_base=modif.prepare();
					if(next_base!=0)
						return std::move(prom).set(0, 0, next_base);
					auto r=modif.apply(std::move(who), hist);
					std::move(prom).set(std::get<0>(r), std::get<1>(r), std::get<2>(r));
				} catch(const std::runtime_error&) {
					unlikely(std::move(prom), std::current_exception());
				}
			});

			auto ex=msg.get_executor();
			std::move(fut).async_wait(ex, [this,msg=std::move(msg),num_commits,proj=std::move(proj)](gapr::likely<std::tuple<gapr::nid_t, uint64_t, uint64_t>>&& res) mutable {
				if(!res) try {
					auto ec=res.error_code();
					return handle_message_simple(std::move(msg),
							"ERR", ec.message());
				} catch(const std::runtime_error& e) {
					return handle_message_simple(std::move(msg),
							"ERR", e.what());
				}
				if(std::get<0>(res.get())) {
					if(num_commits%(5000)==0)
						update_state(proj);
					return handle_message_simple(std::move(msg),
							"OK", std::get<0>(res.get()), std::get<1>(res.get()), std::get<2>(res.get()));
				} else {
					if(std::get<1>(res.get())) {
						return handle_message_simple(std::move(msg),
								"RETRY", std::get<2>(res.get()));
					} else {
						return handle_message_simple(std::move(msg),
								"NO", std::get<2>(res.get()));
					}
				}
			});
		});
	}

};

gapr::gather_server::gather_server(Args&& args):
	_args{std::move(args)},
	_ptr{std::make_shared<PRIV>()}
{ }
gapr::gather_server::~gather_server() { }

void gapr::gather_server::configure() { return _ptr->configure(_args.cwd); }
int gapr::gather_server::run() { return _ptr->run(_args.host, _args.port); }
void gapr::gather_server::emergency() { return _ptr->emergency(); }

void gapr::gather_server::PRIV::update_state(const std::string& proj) {
	logMessage(__FILE__, "Entered Update_State");
	gapr::promise<gapr::mem_file> prom;
	auto fut=prom.get_future();
	auto model=_models.at(proj).model;
	ba::post(_thr_pool, [prom=std::move(prom),model=std::move(model)]() mutable {
		try {
			gather_model::helper helper{*model};
			auto file=helper.dump_state();
			std::move(prom).set(file);
		} catch(const std::runtime_error&) {
			unlikely(std::move(prom), std::current_exception());
		}
	});
	std::move(fut).async_wait(_io_ctx.get_executor(), [this,proj](gapr::likely<gapr::mem_file>&& res) {
		if(!res) try {
			auto ec=res.error_code();
			std::cerr<<"failed to update state `"<<proj<<"': "<<ec.message()<<'\n';
			return;
		} catch(const std::runtime_error& e) {
			std::cerr<<"failed to update state `"<<proj<<"': "<<e.what()<<'\n';
			return;
		}
		auto it=_models.find(proj);
		if(it!=_models.end())
			it->second.state=std::move(res.get());
	});
}
void gapr::gather_server::PRIV::load_project(std::string&& proj) {
	logMessage(__FILE__, "Loading Project ...");
	gapr::promise<std::shared_ptr<gather_model>> prom;
	auto fut=prom.get_future();
	auto repo=_env->project_repo(proj);
	ba::post(_thr_pool, [prom=std::move(prom),repo]() mutable {
		try {
			auto m=std::make_shared<gather_model>(std::move(repo));
			return std::move(prom).set(std::move(m));
		} catch(const std::bad_alloc& e) {
			// XXX handle oom
		} catch(const std::runtime_error& e) {
			unlikely(std::move(prom), std::current_exception());
		}
	});
	std::move(fut).async_wait(_io_ctx.get_executor(), [this,proj=std::move(proj)](gapr::likely<std::shared_ptr<gather_model>>&& res) {
		if(!res) try {
			auto ec=res.error_code();
			std::cerr<<"failed to load repo `"<<proj<<"': "<<ec.message()<<'\n';
			return;
		} catch(const std::runtime_error& e) {
			std::cerr<<"failed to load repo `"<<proj<<"': "<<e.what()<<'\n';
			return;
		}
		auto [it, ins]=_models.emplace(proj, ModelState{std::move(res.get())});
		if(!ins)
			std::cerr<<"failed to add repo `"<<proj<<".\n";
		update_state(proj);
	});
}

void gapr::gather_server::PRIV::do_accept() {
    logMessage(__FILE__, "Starting do_accept...");

    _acceptor.async_accept([this](bs_error_code ec, tcp::socket&& sock) mutable {
        if(ec) {
            logMessage(__FILE__, std::string("Error during accept: ") + ec.message());
            gapr::print("accept err: ", ec.message());
            return;
        }

        logMessage(__FILE__, "Connection accepted, proceeding with handshake.");
        do_handshake(std::move(sock));

        if (!_stop_signal.signal()) {
            logMessage(__FILE__, "No stop signal, continuing to accept more connections.");
            do_accept();
        } else {
            logMessage(__FILE__, "Stop signal received, no further accept calls.");
        }
    });
}

void gapr::gather_server::PRIV::do_handshake(tcp::socket&& sock) {
    logMessage(__FILE__, "Starting handshake process.");

    // Creating an HttpConnection
    auto conn = std::make_shared<HttpConnection>(std::move(sock));

    // Detecting if the connection is using SSL
    beast::async_detect_ssl(conn->sock, conn->buffer, [this, conn](bs_error_code ec, bool res) {
        if (ec) {
            logMessage(__FILE__, std::string("SSL detection error: ") + ec.message());
            gapr::print(1, "ssl detection err: ", ec.message());
            return;
        }

        if (!res) {
            logMessage(__FILE__, "No SSL detected, redirecting to plain HTTP.");
            return redirect_to_ssl(*conn);
        }

        logMessage(__FILE__, "SSL detected, proceeding with handshake.");

        // Performing the SSL handshake
        auto& ssl = conn->ssl.emplace(std::move(conn->sock), _ssl_ctx);
        ssl.async_handshake(ssl.server, conn->buffer.cdata(), [this, conn](bs_error_code ec, std::size_t used) mutable {
            if (ec) {
                logMessage(__FILE__, std::string("SSL handshake error: ") + ec.message());
                gapr::print(1, "handshake err: ", ec.message());
                return;
            }

            if (conn->buffer.size() != used) {
                logMessage(__FILE__, "Handshake error: buffer mismatch after handshake.");
                gapr::print(1, "handshake err: ", "lost buffer");
                return;
            }

            logMessage(__FILE__, "Handshake successful, clearing buffer.");
            conn->buffer.clear();

            const unsigned char* alpn_str;
            unsigned int alpn_len;
            auto& ssl = *conn->ssl;
            ::SSL_get0_alpn_selected(ssl.native_handle(), &alpn_str, &alpn_len);

            if (alpn_str && std::equal(alpn_str, alpn_str + alpn_len, &priv_proto[0], &priv_proto[sizeof(priv_proto)])) {
                logMessage(__FILE__, "ALPN protocol matched, creating session.");
                auto ses = std::make_shared<Session>();
                std::shared_ptr<ssl::stream<tcp::socket>> ssl2{conn, &ssl};
                add_session(*ses);
                recv_request(*ses, gapr::server_end{std::move(ssl2)});
            } else {
                logMessage(__FILE__, "No ALPN match, receiving request directly.");
                recv_request(*conn);
            }
        });
    });
}

void gapr::gather_server::PRIV::recv_request(Session& ses, gapr::server_end&& msg) {
	assert(msg);
	msg.async_recv([this,msg,ses=ses.shared_from_this()]
			(bs_error_code ec, const gapr::server_end::msg_hdr_in& hdr) mutable {
				if(ec) {
					gapr::print("recv err: ", ec.message());
					return;
				}
				recv_request(*ses, gapr::server_end{msg});

				std::string cmdstr{};
				{
					auto tag=hdr.tag();
					cmdstr.reserve(tag.size());
					for(std::size_t i=0; i<tag.size(); i++) {
						auto c=tag.data()[i];
						cmdstr.push_back(c=='.'?'_':std::toupper(c));
					}
				}
				gapr::print("handle msg: ", hdr.line_ptr());

				auto it=_str2command.find(cmdstr);
				if(it==_str2command.end())
					return handle_message_simple(std::move(msg),
							"ERR", "Unknown command.");

				return (this->*(it->second))(*ses, hdr, std::move(msg));
			});
}

void gapr::gather_server::PRIV::recv_request(HttpConnection& conn) {
	auto& parser=conn.parser.emplace();
	parser.body_limit(768*1024*1024);
	http::async_read(*conn.ssl, conn.buffer, parser,
			[this,conn=conn.shared_from_this()](bs_error_code ec, std::size_t nbytes) mutable {
				if(ec) {
					if(ec==http::error::end_of_stream) {
						//return do_close();
						//gapr::print("error read http eos: ", ec.message());
						del_session();
						return;
					}
					gapr::print("error read http: ", ec.message());
					del_session();
					return;
				}

				auto& req=conn->parser->get();
				gapr::print("uri: ", req.method(), '^', req.target(), '$');
				if(req.target()=="/")
					return write_response(*conn, handle_http_root(req));
				if(has_prefix(req.target(), "/api/"))
					return handle_api_request(*conn);

				auto res=handle_data_request(req);
				return write_response(*conn, std::move(res));
			});
}
void gapr::gather_server::PRIV::handle_api_request(HttpConnection& conn) {
	auto& req=conn.parser->get();

	auto target=req.target().substr(5);
	if(auto n=target.find('/'); n!=target.npos) {
		conn.api_args=target.substr(n);
		target=target.substr(0, n);
	} else {
		conn.api_args={};
	}

	std::string api_tgt{target};
	auto it=_http_apis.find(api_tgt);
	if(it==_http_apis.end()){
		logMessage(__FILE__, "API endpoint not found: " + api_tgt);
		return write_response(conn, http_server_err(req));
	}

	auto res=(this->*(it->second))(conn);
	if(res)
		write_response(conn, std::move(res));
}
void gapr::gather_server::PRIV::write_response(HttpConnection& conn, HttpResponsePtr res) {
	assert(!conn.res_ptr);
	conn.res_ptr=std::move(res);
	conn.res_ptr->async_send(*conn.ssl,
			[this,conn=conn.shared_from_this()](bs_error_code ec, std::size_t nbytes) {
				if(ec) {
					gapr::print("failed to write response: ", ec.message());
					return;
				}
				conn->res_ptr={};
				recv_request(*conn);
				// close???
			});
}

void gapr::gather_server::PRIV::redirect_to_ssl(HttpConnection& conn) {
	auto& parser=conn.parser.emplace();
	http::async_read(conn.sock, conn.buffer, parser, [this,conn=conn.shared_from_this()](bs_error_code ec, std::size_t nbytes) mutable {
		if(ec) {
			if(ec==http::error::end_of_stream)
				return;
			gapr::print("error read http: ", ec.message());
			return;
		}

		auto& req=conn->parser->get();
		std::string uri{"https://"};
		auto host=req[http::field::host];
		uri.reserve(host.size()+2);
		uri+=host;
		uri+='/';
		auto& res=conn->res.emplace(http::status::moved_permanently, req.version());
		res.set(http::field::server, _env->http_server());
		res.set(http::field::location, uri);
		res.prepare_payload();

		auto& serializer=conn->serializer.emplace(res);
		http::async_write(conn->sock, serializer, [conn](bs_error_code ec, std::size_t nbytes) {
			if(ec) {
				gapr::print("failed to write response: ", ec.message());
				return;
			}
		});
	});
}

std::chrono::system_clock::time_point gapr::gather_server::PRIV::maybe_update(std::vector<std::pair<std::string, std::shared_ptr<gather_model>>>&& todo) {
	auto ts=gapr::from_timestamp(_stats.last_update());
	if(std::difftime(std::time(nullptr), std::chrono::system_clock::to_time_t(ts))>1*60) {
		for(auto& [proj, mdl]: todo)
			_stats.update(proj, *mdl);
		_stats.update_end();
		ts=gapr::from_timestamp(_stats.last_update());
	}
	return ts;
}

void gapr::gather_server::PRIV::handle_message_simple_(gapr::server_end&& msg, gapr::server_end::msg_hdr&& hdr) {
	return msg.async_send(std::move(hdr), [msg](bs_error_code ec) {
	});
}

