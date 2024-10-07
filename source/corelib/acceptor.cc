/* core/acceptor.cc
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

//@@@@

#include "gapr/acceptor.hh"

#include "gapr/utility.hh"
#include "gapr/fix-error-code.hh"

namespace ba=boost::asio;
using resolver=ba::ip::tcp::resolver;
using res_results=resolver::results_type;


gapr::Acceptor::Acceptor(const boost::asio::any_io_executor& ex): _ex{std::move(ex)} { }
gapr::Acceptor::~Acceptor() { }

void gapr::Acceptor::close() {
	while(!_que_ops.empty()) {
		auto op=std::move(_que_ops.front());
		_que_ops.pop_front();
		ba::post(_ex, [ex=_ex,op=std::move(op)]() {
			op->complete(ba::error::operation_aborted, socket{ex});
		});
	}
	_que_free.clear();
	error_code ec;
	while(!_acceptors.empty()) {
		auto acc=std::move(_acceptors.back());
		_acceptors.pop_back();
		acc.close(ec);
		if(ec)
			throw std::system_error{to_std_error_code(ec)};
	}
}

void gapr::Acceptor::do_listen(std::unique_ptr<AsyncOp>&& op) {
	if(_binds.empty())
		return ba::post(_ex, [op=std::move(op)]() {
			op->complete(ba::error::not_found);
		});

	_ntotal=_binds.size();
	error_code ec;
	std::deque<std::pair<std::string, unsigned short>> pending{};
	do {
		auto addr=ba::ip::make_address(_binds.front().first, ec);
		if(!ec) {
			endpoint ep{addr, _binds.front().second};
			_binds.pop_front();
			try_addr(ep, ec);
			continue;
		}
		pending.emplace_back(std::move(_binds.front()));
		_binds.pop_front();
	} while(!_binds.empty());

	if(!pending.empty()) {
		_npending=pending.size();
		auto resolver_=std::make_shared<resolver>(_ex);
		do {
			resolver_->async_resolve(pending.front().first, "0", [this,
					resolver_,port=pending.front().second
			](error_code ec, const res_results& results) {
				_npending--;
				int nres=0;
				if(ec) {
					gapr::print("resolve error: ", ec.message());
				} else {
					endpoint ep{ba::ip::address{}, port};
					for(auto it=results.begin(); it!=results.end(); ++it) {
						nres++;
						ep.address(it->endpoint().address());
						try_addr(ep, ec);
					}
				}
				if(_npending>0)
					return;
				auto op=std::move(_listen_op);
				if(_nlisten>0)
					return op->complete(error_code{});
				if(_ntotal>1)
					return op->complete(ba::error::not_found);
				if(nres>1)
					return op->complete(ba::error::not_found);
				return op->complete(ec);
			});
			pending.pop_front();
		} while(!pending.empty());
		_listen_op=std::move(op);
		return;
	}
	if(_nlisten>0) {
		ec=error_code{};
	} else if(_ntotal>1) {
		ec=ba::error::not_found;
	}
	ba::post(_ex, [op=std::move(op),ec]() {
		op->complete(ec);
	});
}

void gapr::Acceptor::do_accept(std::unique_ptr<AccOp>&& op) {
	if(!_que_peers.empty()) {
		auto peer=std::move(_que_peers.front());
		_que_peers.pop_front();
		return ba::post(_ex, [op=std::move(op),peer=std::move(peer)]() mutable {
			op->complete(peer.first, std::move(peer.second));
		});
	}
	if(_acceptors.empty()) {
		return ba::post(_ex, [ex=_ex,op=std::move(op)]() {
			op->complete(ba::error::not_found, socket{ex});
		});
	}
	while(!_que_free.empty()) {
		auto idx=_que_free.front();
		_que_free.pop_front();
		_acceptors[idx].async_accept(_ex, [this,idx](error_code ec, socket&& peer) {
			_que_free.emplace_back(idx);
			if(ec) {
				// log this err
				gapr::print("Listener: ", ec.message());
				return;
			}
			if(_que_ops.empty()) {
				_que_peers.emplace_back(std::make_pair(ec, std::move(peer)));
				return;
			}
			auto op=std::move(_que_ops.front());
			_que_ops.pop_front();
			op->complete(ec, std::move(peer));
		});
	}
	_que_ops.emplace_back(std::move(op));
}

void gapr::Acceptor::try_addr(const endpoint& ep, error_code& ec) {
	do {
		acceptor acc{_ex};
		acc.open(ep.protocol(), ec);
		if(ec) break;
		acc.set_option(socket::reuse_address(true), ec);
		if(ec) break;
		acc.bind(ep, ec);
		if(ec) break;
		acc.listen(_backlog, ec);
		if(ec) break;

		_acceptors.emplace_back(std::move(acc));
		_que_free.emplace_back(_acceptors.size()-1);
		_nlisten++;
		return;
	} while(false);
	gapr::print("listen error: ", ec.message());
}

