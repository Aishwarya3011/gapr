/* acceptor.hh
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
#ifndef _GAPR_INCLUDE_ACCEPTOR_HH_
#define _GAPR_INCLUDE_ACCEPTOR_HH_

#include "gapr/config.hh"

#include "gapr/detail/cb-wrapper.hh"

#include <deque>
#include <string>
#include <vector>
#include <memory>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace gapr {

	class GAPR_CORE_DECL Acceptor {
		public:
			using error_code=boost::system::error_code;
			using socket=boost::asio::ip::tcp::socket;

			explicit Acceptor(const boost::asio::any_io_executor& ex);
			~Acceptor();

			Acceptor(const Acceptor&) =delete;
			Acceptor& operator=(const Acceptor&) =delete;

			void bind(const char* host, unsigned short port) {
				_binds.emplace_back(host, port);
			}
			template<typename Cb> void async_listen(int backlog, Cb&& cb) {
				_backlog=backlog;
				do_listen(cb_wrapper::make_unique<AsyncOp>(std::move(cb)));
			}
			template<typename Cb> void async_accept(Cb&& cb) {
				do_accept(cb_wrapper::make_unique<AccOp>(std::move(cb)));
			}
			//XXX bool is_open() const;
			void close();

		private:
			using acceptor=boost::asio::ip::tcp::acceptor;
			using endpoint=boost::asio::ip::tcp::endpoint;
			struct AsyncOp: cb_wrapper::add<void(const error_code&)> {
				void complete(const error_code& ec) { return cb_wrapper_call(ec); }
			};
			struct AccOp: cb_wrapper::add<void(const error_code&, socket&& peer)> {
				void complete(const error_code& ec, socket&& peer) {
					return cb_wrapper_call(ec, std::move(peer));
				}
			};

			boost::asio::any_io_executor _ex;
			std::deque<std::pair<std::string, unsigned short>> _binds{};
			std::vector<acceptor> _acceptors{};
			std::deque<std::size_t> _que_free{};
			std::deque<std::unique_ptr<AccOp>> _que_ops{};
			std::deque<std::pair<error_code, socket>> _que_peers{};
			std::unique_ptr<AsyncOp> _listen_op{};
			int _backlog{-1};
			std::size_t _nlisten{0};
			std::size_t _npending{0};
			std::size_t _ntotal{0};

			void do_listen(std::unique_ptr<AsyncOp>&& op);
			void do_accept(std::unique_ptr<AccOp>&& op);
			void try_addr(const endpoint& ep, error_code& ec);
	};

}

#endif
