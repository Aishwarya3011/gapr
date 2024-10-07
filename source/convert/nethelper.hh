#include "gapr/mem-file.hh"

#include <iostream>
#include <deque>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>

#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/string_body.hpp>

#include "mem_file_body.hh"

struct network_helper {
	std::string host{};
	std::string port{};

	boost::asio::ip::tcp::resolver _resolver;
	boost::asio::ssl::context _ssl_ctx{boost::asio::ssl::context::tlsv12_client};
	boost::asio::ip::tcp::resolver::results_type addresses;
	std::vector<std::unique_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>>> _connections;

	struct session: public std::enable_shared_from_this<session> {
		template<typename Cb>
		session(gapr::mem_file&& file, Cb&& cb):
			_put{true}, req_file{std::move(file)}, _cb_put{std::forward<Cb>(cb)}
		{ }
		template<typename Cb>
		explicit session(Cb&& cb):
			_put{false}, _cb_get{std::forward<Cb>(cb)}
		{ }
		unsigned int bind(network_helper* par) noexcept {
			assert(par);
			assert(!_par);
			auto cnt=++par->_n_active;
			_par=par;
			return cnt;
		}
		~session() {
			if(_par)
				--_par->_n_active;
		}

		network_helper* _par{nullptr};
		bool _put;

		gapr::mem_file req_file;
		boost::beast::http::request<mem_file_body> req_put;
		boost::beast::http::response<boost::beast::http::string_body> res_put;
		std::function<void(std::string&&, const boost::beast::http::response_header<>&)> _cb_put;

		boost::beast::http::request<boost::beast::http::empty_body> req_get;
		boost::beast::http::response<mem_file_body> res_get;
		std::function<void(gapr::mem_file&& file, const boost::beast::http::response_header<>&)> _cb_get;

		std::unique_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>> ssl;

		boost::beast::flat_buffer req_buf;
		boost::beast::flat_buffer res_buf;

		auto& sock() { return *ssl; }
	};
	std::atomic<unsigned int> _n_active{0};
	std::deque<std::shared_ptr<session>> _pending;

	network_helper(boost::asio::io_context& ctx, std::string&& host, std::string&& port);
	~network_helper();

	void fail(boost::beast::error_code ec, const char* what) {
		std::cerr << what << ": " << ec.message() << "\n";
		exit(-1);
	}
	void do_shutdown(session& sess);
	template<bool PUT> void do_read(session& sess);
	template<bool PUT> void do_send(session& sess);
	void do_handshake(session& sess);
	void do_connect(session& sess);
	template<bool PUT> void do_init(session& sess);
	void do_session(session& sess, boost::asio::io_context::executor_type ex);
	void do_session_conn(session& sess, boost::asio::io_context::executor_type ex);
};

