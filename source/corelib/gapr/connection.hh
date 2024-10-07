/* connection.hh
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

//@@@@@
#ifndef _GAPR_INCLUDE_CONNECTION_HH_
#define _GAPR_INCLUDE_CONNECTION_HH_


#include "gapr/buffer-view.hh"
#include "gapr/detail/cb-wrapper.hh"
#include "gapr/exception.hh"

#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace gapr {

	class connection {
		public:
			using socket=boost::asio::ip::tcp::socket;
			using endpoint=boost::asio::ip::tcp::endpoint;
			using io_context=boost::asio::io_context;
			using ssl_context=boost::asio::ssl::context;
			using error_code=boost::system::error_code;

			class msg_hdr;
			class msg_hdr_in;
			enum class msg_type {
				hdr_only, // no body, no variant
				stream, // size hint, need wait, diff. variants
			};

			struct impl;
	};

}

#include "gapr/detail/connection.hh"

namespace gapr {

	class connection::msg_hdr {
		public:
			msg_hdr() noexcept =delete;
			template<typename... Args>
				explicit msg_hdr(const char* tag, Args&&... args): _hdr{} {
					_hdr.format_hdr(tag, std::forward<Args>(args)...);
				}
		private:
			impl::MsgHdr _hdr;
			friend class server_end;
			friend class client_end;
	};

	class connection::msg_hdr_in {
		public:
			explicit msg_hdr_in() noexcept =delete;
			std::string_view line() const noexcept { return {line_ptr(), line_len()}; }
			const char* line_ptr() const noexcept { return _b.ptr; }
			std::size_t line_len() const noexcept { return _b.len; }

			bool tag_is(const char* tag) const noexcept;
			std::string_view tag() const noexcept { return {line_ptr(), tag_len()}; }
			std::size_t tag_len() const noexcept { return _b.tlen; }

			std::string_view args() const noexcept { return {args_ptr(), args_len()}; }
			const char* args_ptr() const noexcept { return _b.ptr+_b.aoff; }
			std::size_t args_len() const noexcept { return _b.len-_b.aoff; }
		private:
			impl::hdr_info_base _b;
	};

	class server_end: public connection {
		public:
			explicit server_end() noexcept: _ptr{nullptr} { }
			server_end(socket&& sock, ssl_context& ssl_ctx):
				_ptr{new impl{std::move(sock), ssl_ctx}} { }
			server_end(std::shared_ptr<boost::asio::ssl::stream<socket>> ssl):
				_ptr{new impl{std::move(ssl)}} { }

			explicit operator bool() const noexcept { return _ptr.ptr; }

#if 0
			template<typename Cb>
				void async_handshake(msg_hdr<true>&& hdr, Cb&& cb) const;
#endif
			template<typename Cb>
				void async_shutdown(Cb&& cb) const;
			void close() const noexcept { _ptr->do_close(); }

			//void async_alert(hdr) XXX use cases?

			msg_type type_in() const noexcept { return _ptr.typ(); }
			uint16_t size_in() const noexcept { return _ptr.siz(); }
			uint64_t size_hint_in() const noexcept { return _ptr.sizhint(); }

			// _idx**?
			//server::end::msg msg;
			//cancel //close //is_open
			//*shutdown

			auto get_executor() const {
				return _ptr->sock().get_executor();
			}

			template<typename Cb>
				void async_recv(Cb&& cb) const;

			template<typename Cb>
				void async_send(msg_hdr&& hdr, Cb&& cb) const;
			template<typename Cb>
				void async_send(msg_hdr&& hdr, uint16_t var, std::string_view buf, Cb&& cb) const;
			template<typename Cb>
				void async_send(msg_hdr&& hdr, uint16_t var, uint64_t szhint, Cb&& cb) const;

			template<typename Cb>
				void async_read(buffer_view buf, Cb&& cb) const;
			template<typename Cb>
				void async_write(std::string_view buf, bool eof, Cb&& cb) const;

		private:
			impl::Ptr _ptr;
			explicit server_end(impl* ptr) noexcept: _ptr{ptr} { }
	};

	class client_end: public connection {
		public:
			explicit client_end() noexcept: _ptr{nullptr} { }
			client_end(const boost::asio::any_io_executor& ex, ssl_context& ssl_ctx):
				_ptr{new impl{std::move(ex), ssl_ctx}} { }
			client_end(socket&& sock, ssl_context& ssl_ctx):
				_ptr{new impl{std::move(sock), ssl_ctx}} { }
			client_end(std::unique_ptr<boost::asio::ssl::stream<socket>> ssl):
				_ptr{new impl{std::move(ssl), false}} { }

			explicit operator bool() const noexcept { return _ptr.ptr; }

			template<typename Cb>
				void async_connect(const endpoint& peer, Cb&& cb) const;
			template<typename Cb>
				void async_handshake(Cb&& cb) const;
			template<typename Cb>
				void async_shutdown(Cb&& cb) const;
			void close() noexcept { _ptr->do_close(); }


			msg_type type_in() const noexcept { return _ptr.typ(); }
			uint16_t size_in() const noexcept { return _ptr.siz(); }
			uint64_t size_hint_in() const noexcept { return _ptr.sizhint(); }

			auto get_executor() const {
				return _ptr->sock().get_executor();
			}
			//cancel //close //is_open
			//*shutdown
			//void discard_write();
			//void discard_read();

			template<typename Cb>
				void async_send(msg_hdr&& hdr, Cb&& cb) const;
			template<typename Cb>
				void async_send(msg_hdr&& hdr, uint16_t var, std::string_view buf, Cb&& cb) const;
			template<typename Cb>
				void async_send(msg_hdr&& hdr, uint16_t var, uint64_t szhint, Cb&& cb) const;

			template<typename Cb>
				void async_recv(Cb&& cb) const;

			template<typename Cb>
				void async_read(buffer_view buf, Cb&& cb) const;
			template<typename Cb>
				void async_write(std::string_view buf, bool eof, Cb&& cb) const;
		private:
			impl::Ptr _ptr;
			explicit client_end(impl* ptr) noexcept: _ptr{ptr} { }
	};

#if 0
	template<typename Cb>
		inline void server_end::async_handshake(msg_hdr<true>&& hdr, Cb&& cb) const {
			_ptr->do_handshake_srv(cb_wrapper::make_unique<impl::SrvHsOp>(std::move(cb), std::move(hdr._hdr)));
		}
#endif
	template<typename Cb>
		inline void server_end::async_shutdown(Cb&& cb) const {
			// wait for unsent notifs, client eof
			// disable notify now
			//_ptr->do_shutdown_srv(cb_wrapper::make_unique<impl::ShutdownOp>(std::move(cb)));
		}

	template<typename Cb>
		inline void server_end::async_recv(Cb&& cb) const {
			_ptr->do_recv_req(cb_wrapper::make_unique<impl::RecvHdrOp>(std::move(cb)));
		}
	template<typename Cb>
		inline void server_end::async_send(msg_hdr&& hdr, Cb&& cb) const {
			_ptr->do_send_res(cb_wrapper::make_unique<impl::SendHdrOp>(std::move(cb), std::move(hdr._hdr), nullptr));
		}
	template<typename Cb>
		inline void server_end::async_send(msg_hdr&& hdr, uint16_t var, std::string_view buf, Cb&& cb) const {
			_ptr->do_send_res(cb_wrapper::make_unique<impl::SendHdrOp>(std::move(cb), std::move(hdr._hdr), var, buf));
		}
	template<typename Cb>
		inline void server_end::async_send(msg_hdr&& hdr, uint16_t var, uint64_t szhint, Cb&& cb) const {
			_ptr->do_send_res(cb_wrapper::make_unique<impl::SendHdrOp>(std::move(cb), std::move(hdr._hdr), var, szhint));
		}
	template<typename Cb>
		inline void server_end::async_read(buffer_view buf, Cb&& cb) const {
			_ptr->do_read_str(cb_wrapper::make_unique<impl::ReadStrOp>(std::move(cb), buf));
		}
	template<typename Cb>
		inline void server_end::async_write(std::string_view buf, bool eof, Cb&& cb) const {
			_ptr->do_write_str(cb_wrapper::make_unique<impl::SendChkOp>(std::move(cb), buf, eof));
		}

	template<typename Cb>
		inline void client_end::async_connect(const endpoint& peer, Cb&& cb) const {
			_ptr->do_connect_cli(peer, cb_wrapper::make_unique<impl::ConnOp>(std::move(cb)));
		}
	template<typename Cb>
		inline void client_end::async_handshake(Cb&& cb) const {
			_ptr->do_handshake_cli(cb_wrapper::make_unique<impl::CliHsOp>(std::move(cb)));
		}
	template<typename Cb>
		inline void client_end::async_shutdown(Cb&& cb) const {
			// join ports
			// disable fork now
			//_ptr->do_shutdown_cli(cb_wrapper::make_unique<impl::ShutdownOp>(std::move(cb)));
		}

	template<typename Cb>
		inline void client_end::async_send(msg_hdr&& hdr, Cb&& cb) const {
			_ptr->do_send_req(cb_wrapper::make_unique<impl::SendHdrOp>(std::move(cb), std::move(hdr._hdr), nullptr));
		}
	template<typename Cb>
		inline void client_end::async_send(msg_hdr&& hdr, uint16_t var, std::string_view buf, Cb&& cb) const {
			_ptr->do_send_req(cb_wrapper::make_unique<impl::SendHdrOp>(std::move(cb), std::move(hdr._hdr), var, buf));
		}
	template<typename Cb>
		inline void client_end::async_send(msg_hdr&& hdr, uint16_t var, uint64_t szhint, Cb&& cb) const {
			_ptr->do_send_req(cb_wrapper::make_unique<impl::SendHdrOp>(std::move(cb), std::move(hdr._hdr), var, szhint));
		}
	template<typename Cb>
		inline void client_end::async_recv(Cb&& cb) const {
			_ptr->do_recv_reply(cb_wrapper::make_unique<impl::RecvHdrOp>(std::move(cb)));
		}
	template<typename Cb>
		inline void client_end::async_read(buffer_view buf, Cb&& cb) const {
			_ptr->do_read_str(cb_wrapper::make_unique<impl::ReadStrOp>(std::move(cb), buf));
		}
	template<typename Cb>
		inline void client_end::async_write(std::string_view buf, bool eof, Cb&& cb) const {
			_ptr->do_write_str(cb_wrapper::make_unique<impl::SendChkOp>(std::move(cb), buf, eof));
		}

	namespace unit_test { int chk_connection(); }
}

#endif
