#include "nethelper.hh"
#include "gapr/mem-file.hh"

#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

network_helper::network_helper(boost::asio::io_context& ctx, std::string&& host, std::string&& port):
	host{std::move(host)}, port{std::move(port)}, _resolver(ctx.get_executor()) {
		//load_root_certificates(ctx);
		//helper._ssl_ctx.set_verify_mode(boost::asio::ssl::verify_peer);
		_ssl_ctx.set_verify_mode(boost::asio::ssl::verify_none);

	}
network_helper::~network_helper() {
}
void network_helper::do_shutdown(session& sess) {
	//boost::beast::get_lowest_layer(sess.ssl).expires_after(std::chrono::seconds(30));
#ifndef NO_SSL
	sess.ssl->async_shutdown([this,sess=sess.shared_from_this()](boost::beast::error_code ec) {
		if(ec==boost::asio::error::eof)
			ec = {};
		//if(ec)
			//return fail(ec, "shutdown");
	});
#endif
}
template<bool PUT>
static auto& get_res(network_helper::session& sess) {
	if constexpr(PUT) {
		return sess.res_put;
	} else {
		return sess.res_get;
	}
}
template<bool PUT>
static auto& get_req(network_helper::session& sess) {
	if constexpr(PUT) {
		return sess.req_put;
	} else {
		return sess.req_get;
	}
}
template<bool PUT>
void call_cb(network_helper::session& sess) {
	if constexpr(PUT) {
		sess._cb_put(std::move(sess.res_put.body()), sess.res_put);
	} else {
		sess._cb_get(std::move(sess.res_get.body().mfile), sess.res_get);
	}
}
template<bool PUT>
void network_helper::do_read(session& sess) {
	boost::beast::http::async_read(sess.sock(), sess.res_buf, get_res<PUT>(sess), [this,sess=sess.shared_from_this()](boost::beast::error_code ec, std::size_t bytes_transferred) {
		auto ex=_resolver.get_executor();
		//assert(ex.running_in_this_thread());
		boost::ignore_unused(bytes_transferred);
		if(ec)
			return fail(ec, "read");
		call_cb<PUT>(*sess);
		if(!get_res<PUT>(*sess).keep_alive()) {
			do_shutdown(*sess);
			if(!_pending.empty()) {
				auto sessnext=std::move(_pending.front());
				_pending.pop_front();
				do_session_conn(*sessnext, *ex.target<boost::asio::io_context::executor_type>());
			}
			return;
		}
		if(_pending.empty()) {
			_connections.push_back(std::move(sess->ssl));
			return;
		}
		auto sessnext=std::move(_pending.front());
		_pending.pop_front();
		sessnext->ssl=std::move(sess->ssl);
		if(sessnext->_put)
			do_send<true>(*sessnext);
		else
			do_send<false>(*sessnext);
	});
}
template<bool PUT>
void network_helper::do_send(session& sess) {
	//boost::beast::get_lowest_layer(sess.ssl).expires_after(std::chrono::seconds(30));
	boost::beast::http::async_write(sess.sock(), get_req<PUT>(sess), [this,sess=sess.shared_from_this()](boost::beast::error_code ec, std::size_t bytes_transferred) {
		boost::ignore_unused(bytes_transferred);
		if(ec)
			return fail(ec, "write");
		do_read<PUT>(*sess);
	});
}
void network_helper::do_handshake(session& sess) {
#ifndef NO_SSL
	sess.ssl->async_handshake(boost::asio::ssl::stream_base::client, [this,sess=sess.shared_from_this()](boost::beast::error_code ec) {
		if(ec)
			return fail(ec, "handshake");
#endif
		if(sess->_put)
			do_send<true>(*sess);
		else
			do_send<false>(*sess);
#ifndef NO_SSL
	});
#endif
}
void network_helper::do_connect(session& sess) {
	//boost::beast::get_lowest_layer(sess.ssl).expires_after(std::chrono::seconds(30));
	boost::beast::get_lowest_layer(sess.sock()).async_connect(addresses, [this,sess=sess.shared_from_this()](boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type::endpoint_type) {
		if(ec)
			return fail(ec, "connect");
		do_handshake(*sess);
	});
}
template<bool PUT>
void network_helper::do_init(session& sess) {
	get_req<PUT>(sess).version(11);
	get_req<PUT>(sess).set(boost::beast::http::field::host, host);
	get_req<PUT>(sess).set(boost::beast::http::field::user_agent, "gapr");
	if constexpr(PUT) {
		get_req<PUT>(sess).set(boost::beast::http::field::content_type, "application/json");
		get_req<PUT>(sess).keep_alive(true);
		get_req<PUT>(sess).body().file=std::move(sess.req_file);
		get_req<PUT>(sess).prepare_payload();
	} else {
		get_res<PUT>(sess).body().mfile=gapr::mutable_mem_file{false};
	}
}
void network_helper::do_session(session& sess, boost::asio::io_context::executor_type ex) {
	assert(ex.running_in_this_thread());
	if(sess._put)
		do_init<true>(sess);
	else
		do_init<false>(sess);
	auto n_sess=sess.bind(this);
	if(!_connections.empty()) {
		sess.ssl=std::move(_connections.back());
		_connections.pop_back();
		if(sess._put)
			do_send<true>(sess);
		else
			do_send<false>(sess);
		return;
	}
	if(n_sess>3) {
		_pending.push_back(sess.shared_from_this());
		return;
	}
	do_session_conn(sess, ex);
}
void network_helper::do_session_conn(session& sess, boost::asio::io_context::executor_type ex) {
	auto ssl=std::make_unique<boost::beast::ssl_stream<boost::beast::tcp_stream>>(ex, _ssl_ctx);
	if(!SSL_set_tlsext_host_name(ssl->native_handle(), host.c_str())) {
		boost::beast::error_code ec{static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category()};
		std::cerr<<ec.message()<<"\n";
		return;
	}
	sess.ssl=std::move(ssl);
	if(!addresses.empty()) {
		do_connect(sess);
		return;
	}
	_resolver.async_resolve(host, port, [this,sess=sess.shared_from_this()](boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results) {
		if(ec)
			return fail(ec, "resolve");
		assert(!results.empty());
		addresses=std::move(results);
		//for(auto a: addresses)
		//a.service_name
		do_connect(*sess);
	});
}
