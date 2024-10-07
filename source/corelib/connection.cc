/* core/connection.cc
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


#define GAPR_NO_PRINT
#include "gapr/connection.hh"

#include "gapr/utility.hh"
#include "gapr/parser.hh"


//#include <random>
#include <algorithm>

#include <boost/asio/read.hpp>
#include <boost/asio/ssl/error.hpp>
//#include <boost/asio/write.hpp>


namespace ba=boost::asio;
namespace bs=boost::system;
using impl=gapr::connection::impl;

enum ConnSt {
	ConnStPreCon, ConnStPreHs,
	ConnStOk, ConnStSd, ConnStPostSd, ConnStErr,
	//?PreClose,,
};

enum ChrCode: unsigned char {
	CodeSpec=0b1000'0000,

	CodeMaskTag=0b0111'0000,
	CodeHdrOnly=0b0100'0000, CodeHdrOnlyV=CodeHdrOnly|CodeSpec,
	CodeHasBody=0b0010'0000, CodeHasBodyV=CodeHasBody|CodeSpec,
	CodeHasStrm=0b0001'0000, CodeHasStrmV=CodeHasStrm|CodeSpec,

	CodeIsCtrl=0b0100'0000,
	CodeMaskCtrl=0b0011'0000,
	CodeAbrt=0b0010'0000,
	CodeTryAbrt=0b0001'0000,
	CodeMsgEnd=0b0011'0000,

	CodeIsEof=0b0001'0000,
};

impl::impl(socket&& sock, ssl_context& ssl_ctx):
	_st0{ConnStPreHs}, _st1{ConnStPreHs}, _ops_valid{true},
	_refc{(1<<BIT_COUNT)+(1<<BIT_OPEN)}, _ssl{new ssl_stream{std::move(sock), ssl_ctx}}, _ops{}
{ }
impl::impl(std::shared_ptr<ssl_stream> ssl, bool srv):
	_st0{ConnStOk}, _st1{ConnStOk}, _ops_valid{true},
	_refc{(1<<BIT_COUNT)+(1<<BIT_OPEN)}, _ssl{std::move(ssl)}, _ops{}
{
	_is_srv=srv;
	//op->wptr.end();
	_recv_st=0;
	//op->complete(ec);
}
impl::impl(const boost::asio::any_io_executor& ex, ssl_context& ssl_ctx):
	_st0{ConnStPreCon}, _st1{ConnStPreCon}, _ops_valid{true},
	_refc{(1<<BIT_COUNT)}, _ssl{new ssl_stream{std::move(ex), ssl_ctx}}, _ops{}
{ }
impl::~impl() {
	if(_ops_valid)
		_ops.~Ops();
}

static inline bool check_st_conn(int st, bs::error_code& ec) noexcept {
	assert(st==ConnStPreCon);
	switch(st) {
		case ConnStPreCon:
			return true;
		case ConnStPreHs: case ConnStOk: case ConnStSd:
			ec=ba::error::already_connected;
			break;
		case ConnStPostSd: case ConnStErr:
			ec=ba::error::bad_descriptor;
			break;
	}
	return false;
}
void impl::do_connect_cli(const endpoint& peer, std::unique_ptr<ConnOp>&& op) {
	Lock lck_{this};
	bs::error_code ec;
	do {
		if(!check_st_conn(_st1, ec))
			break;
		if(!op->wptr.begin(this)) {
			ec=ba::error::already_started;
			break;
		}
		return _ssl->next_layer().async_connect(peer,
				[op=std::move(op),this](error_code ec) mutable {
					_st1=ec?ConnStPreCon:ConnStPreHs;
					op->wptr.end();
					op->complete(ec);
				});
	} while(false);
	assert(0);
	ba::post(sock().get_executor(), [op=std::move(op),ec]() mutable {
		op->complete(ec);
	});
}

static inline bool check_st_hs(int st, bs::error_code& ec) noexcept {
	assert(st==ConnStPreHs);
	switch(st) {
		case ConnStPreCon:
			ec=ba::error::not_connected;
			break;
		case ConnStPreHs:
			return true;
		case ConnStOk: case ConnStSd:
			ec=ba::error::already_open;
			break;
		case ConnStPostSd: case ConnStErr:
			ec=ba::error::bad_descriptor;
			break;
	}
	return false;
}
static inline bool check_hdr_len(std::size_t len, bs::error_code& ec) noexcept {
		if(len<=impl::MAX_HEADER)
			return true;
		ec=ba::error::invalid_argument;// XXX
		return false;
}
	

static constexpr unsigned char priv_protos[]={
	8, 'g', 'a', 'p', 'r', '/', '1', '.', '1'
};

void impl::do_handshake_cli(std::unique_ptr<CliHsOp>&& op) {
	Lock lck_{this};
	bs::error_code ec;
	do {
		if(!check_st_hs(_st1, ec))
			break;
		if(!op->wptr.begin(this)) {
			ec=ba::error::already_started;
			break;
		}

		auto r=::SSL_set_alpn_protos(_ssl->native_handle(), priv_protos, sizeof(priv_protos));
		if(r!=0) {
			gapr::print(3, "error: ", r);
			throw;
		}

		_ssl->async_handshake(ssl_stream::client,
				[op=std::move(op),this](error_code ec) mutable {
					do {
						if(ec) {
							_st0=_st1=ConnStErr;
							break;
						}
						//Ptr ptr{this};
						//if(!ptr) {
							//ec=ba::error::operation_aborted;
							//break;
						//}
						const unsigned char* alpn_ptr;
						unsigned int alpn_len;
						::SSL_get0_alpn_selected(_ssl->native_handle(), &alpn_ptr, &alpn_len);
						std::cerr<<"alpn: "<<alpn_len<<'\n';
						if(!std::equal(alpn_ptr, alpn_ptr+alpn_len, priv_protos+1, &priv_protos[sizeof(priv_protos)])) {
							_st0=_st1=ConnStErr;
							// XXX
							ec=ba::error::no_protocol_option;
							break;
						}
						_recv_off=0;
						_recv_buf[0]='*';
						_recv_buf[1]='x';
						_recv_buf[2]=' ';
						_recv_buf[3]='0';
						_recv_buf[4]=':';
						_recv_buf[5]='\0';
						hdr_info_base hdr{};
						if(!hdr.parse(&_recv_buf[1], &_recv_buf[5])) {
							ec=ba::error::operation_aborted; // XXX
							break;
						}
						_recv_off=5;
						_st0=_st1=ConnStOk;
						_is_srv=false;
						op->wptr.end();
						_recv_st=0;
						start_timer();
						return op->complete(ec, reinterpret_cast<msg_hdr_in&>(hdr));
					} while(false);
					_st0=_st1=ConnStErr;
					op->wptr.end();
					op->complete(ec);
				});
		return;
	} while(false);
	assert(0);
	ba::post(sock().get_executor(), [op=std::move(op),ec]() mutable {
		op->complete(ec);
	});
}

void impl::do_shutdown_srv(std::unique_ptr<ShutdownOp>&& op) {
	Lock lck_{this};
	// XXX
}
void impl::do_shutdown_cli(std::unique_ptr<ShutdownOp>&& op) {
	Lock lck_{this};
	// XXX
}

enum ReqStates: unsigned char {
	ReqStInit=0,
	ReqStOpening,
	ReqStOpen,
	ReqStClosing,
	ReqStClose,
};

static inline void fix_seq(impl::SendHdrOp& op) noexcept;

inline void check_if_done(impl* ptr) {
	auto& inst=ptr->_insts;
	gapr::print("check : ", ": ", inst.st0+0, " ", inst.st1+0);
	// XXX closing state is sufficient?
	if(inst.st0==ReqStClose && inst.st1==ReqStClose) {
		gapr::print("inst close terminal");
		inst.st1=inst.st0=ReqStInit;
		if(auto op=std::move(ptr->_ops.delayed_recv)) {
			assert(!ptr->_ops.recv);
			ptr->_ops.recv=std::move(op);
		}
		if(ptr->_ops.recv) {
			inst.st0=ReqStOpening;
		}
		if(gapr::connection::impl::WeakPtr<true, false, false> wptr{ptr})
			ptr->gapr::connection::impl::do_recv_impl(std::move(wptr), true);
		if(auto op=std::move(ptr->_ops.delayed_req)) {
			ptr->_insts.st1=ReqStOpening;
			gapr::print("delayed chain");
			fix_seq(*op);
			ptr->_ops.send_que1.push_back(std::move(op));
			if(impl::WeakPtr<false, true, false> wptr{ptr})
				ptr->do_send_impl_que1(std::move(wptr));
		}
	}
}

static inline bool check_st_recv_req(int st, bs::error_code& ec) noexcept {
	assert(st==ConnStOk);
	switch(st) {
		case ConnStPreCon:
			ec=ba::error::not_connected;
			break;
		case ConnStPreHs:
			ec=ba::error::not_connected;// XXX
			break;
		case ConnStOk:
			return true;
		case ConnStSd: case ConnStPostSd: case ConnStErr:
			ec=ba::error::bad_descriptor;
			break;
	}
	return false;
}
// XXX dup?
static inline bool check_st_recv_reply(int st, bs::error_code& ec) noexcept {
	assert(st==ConnStOk);
	switch(st) {
		case ConnStPreCon:
			ec=ba::error::not_connected;
			break;
		case ConnStPreHs:
			ec=ba::error::not_connected;// XXX
			break;
		case ConnStOk:
			return true;
		case ConnStSd: case ConnStPostSd: case ConnStErr:
			ec=ba::error::bad_descriptor;
			break;
	}
	return false;
}


void impl::do_recv_req(std::unique_ptr<RecvHdrOp>&& op) {
	gapr::print("do_recv_req");
	Lock lck_{this};
	bs::error_code ec;
	do {
		if(!check_st_recv_req(_st0, ec))
			break;
		if(_ops.recv) {
			ec=ba::error::in_progress;
			break;
		}
		if(_insts.st0!=ReqStInit || _insts.st1!=ReqStInit) {
			assert(!_ops.delayed_recv);
			_ops.delayed_recv=std::move(op);
			return;
		}
		assert(_insts.st0==ReqStInit && _insts.st1==ReqStInit);
		_ops.recv=std::move(op);
		if(_insts.st0==ReqStInit)
			_insts.st0=ReqStOpening;
		if(WeakPtr<true, false, false> wptr{this})
			do_recv_impl(std::move(wptr), true);
		return;
	} while(false);
	assert(0);
	ba::post(sock().get_executor(), [op=std::move(op),ec]() mutable {
		op->complete(ec);
	});
}

void impl::do_recv_reply(std::unique_ptr<RecvHdrOp>&& op) {
	Lock lck_{this};
	bs::error_code ec;
	do {
		if(!check_st_recv_reply(_st0, ec))
			break;
		gapr::print("do_recv_reply: ");
		if(_ops.recv_reply) {
			ec=ba::error::in_progress;
			break;
		}
		assert(_insts.st1>=ReqStOpen);
		assert(_insts.st0==ReqStInit);
		_ops.recv_reply=std::move(op);
		_recv_ops_reply_n++;
		_insts.st0=ReqStOpening;
		if(WeakPtr<true, false, false> wptr{this})
			do_recv_impl(std::move(wptr), true);
		return;
	} while(false);
	assert(0);
	ba::post(sock().get_executor(), [op=std::move(op),ec]() mutable {
		op->complete(ec);
	});
}

static inline bool check_st_send(int st, bs::error_code& ec) noexcept {
	assert(st==ConnStOk || st==ConnStErr);
	switch(st) {
		case ConnStPreCon:
			ec=ba::error::not_connected;
			break;
		case ConnStPreHs:
			ec=ba::error::not_connected;// XXX
			break;
		case ConnStOk:
			return true;
		case ConnStSd: case ConnStPostSd: case ConnStErr:
			ec=ba::error::bad_descriptor;
			break;
	}
	return false;
}

static inline void fix_seq(impl::SendHdrOp& op) noexcept {
	uint16_t seq=0;
	auto p=&op.hdr.data()[op.hdr_len];
	auto type=p[0];
	auto seq0=static_cast<unsigned char>(seq>>8);
	auto seq1=static_cast<unsigned char>(seq&0xFF);
	auto chk=((seq0>>4)^seq0^(seq1>>4)^seq1^(type>>4))&0x0f;
	p[0]=type|chk;
	p[1]=seq0;
	p[2]=seq1;
}

void impl::do_send_req(std::unique_ptr<SendHdrOp>&& op) {
	gapr::print("do_send_req");
	Lock lck_{this};
	bs::error_code ec;
	do {
		if(!check_st_send(_st1, ec))
			break;
		if(!check_hdr_len(op->hdr_len, ec))
			break;
		if(_insts.st1!=ReqStInit || _insts.st0!=ReqStInit) {
			assert(!_ops.delayed_req);
			_ops.delayed_req=std::move(op);
			return;
		}
		assert(_insts.st1==ReqStInit);
		//chainedN
		_insts.st1=ReqStOpening;

		gapr::print("immed chain");
		fix_seq(*op);
		////
		//fprintf(stderr, "DDD %p: ENQ2 %hd:%hd\n", this, op->seq(), op->idx);
		_ops.send_que1.push_back(std::move(op));
		if(WeakPtr<false, true, false> wptr{this})
			do_send_impl_que1(std::move(wptr));

		return;
	} while(false);
	assert(_st1==ConnStErr);
	ba::post(sock().get_executor(), [op=std::move(op),ec]() mutable {
		op->complete(ec);
	});
}
					// check size
					// if(small) {
					//   send chunk
					// } else
					//   send as stream
					//if(buf.size()>impl::MAX_CHUNK)
						//throw;
					//auto seq=_ptr->_insts[_idx-1].seq;
void impl::do_send_res(std::unique_ptr<SendHdrOp>&& op) {
	gapr::print("do_send_res");
	Lock lck_{this};
	bs::error_code ec;
	do {
		if(!check_st_send(_st1, ec))
			break;
		if(!check_hdr_len(op->hdr_len, ec))
			break;
		assert(_insts.st0>=ReqStOpen);
		assert(_insts.st1==ReqStInit);
		fix_seq(*op);
		//fprintf(stderr, "DDD %p: ENQ4 %hd:%hd\n", this, op->seq(), op->idx);
		_ops.send_que1.push_back(std::move(op));
		_insts.st1=ReqStOpening;
		if(WeakPtr<false, true, false> wptr{this})
			do_send_impl_que1(std::move(wptr));
		return;
	} while(false);
	assert(_st1==ConnStErr);
	ba::post(sock().get_executor(), [op=std::move(op),ec]() mutable {
		op->complete(ec);
	});
}

void impl::do_send_impl_que1(WeakPtr<false, true, false>&& wptr) {
	auto& _op=_ops.send_que1.front();
	switch(_op.type) {
		case SendOp::HdrOnly:
		case SendOp::StrmHdr:
			{
				auto op=wptr->_ops.send_que1.take_front<SendHdrOp>();
				op->wptr.relay(std::move(wptr));
				ba::const_buffer buf{op->hdr.data(), op->hdr.size()};
				//if(_op.type==SendOp::HdrOnly)
					//fprintf(stderr, "DDD %p: HDR %hd:%hd\n", this, op->seq(), op->idx);
				//else
					//fprintf(stderr, "DDD %p: HDR %hd:%hd {\n", this, op->seq(), op->idx);
				ba::async_write(sock(), buf,
						[op=std::move(op),this](error_code ec, std::size_t nbytes) mutable {
							if(ec) {
								_st1=ConnStErr;
								op->wptr.end();
								return op->complete(ec);
							}
							gapr::print("seq begin: ");
							assert(nbytes==op->hdr.size());
							auto is_str=op->type==SendOp::StrmHdr;
							gapr::print("cc");
							_insts.st1=is_str?ReqStOpen:ReqStClose;
							check_if_done(this);
							gapr::print("dd");
							//Ptr ptr{wptr};
							//if(!ptr) {
								//return wptr.end();
							//}
							do_send_impl_next(std::move(op->wptr));
							op->complete(ec);
						});
			}
			break;
		case SendOp::HdrBody:
			{
				auto op=wptr->_ops.send_que1.take_front<SendHdrOp>();
				op->wptr.relay(std::move(wptr));
				std::array<ba::const_buffer, 2> bufs={
					ba::const_buffer{op->hdr.data(), op->hdr.size()},
					ba::const_buffer{op->buf.data(), op->buf.size()}
				};
				//gapr::print("send chk: ", (int64_t)chkop.buf.data());
				//fwrite(chkop.buf.data(), 1, chkop.buf.size(), stderr);
				//fprintf(stderr, "DDD %p: HDR %hd:%hd\n", this, op->seq(), op->idx);
				return ba::async_write(sock(), bufs,
						[op=std::move(op),this](error_code ec, std::size_t nbytes) mutable {
							if(ec) {
								_st1=ConnStErr;
								op->wptr.end();
								return op->complete(ec);
							}
							gapr::print("seq begin: ");
							//assert(nbytes==op->hdr.size());

							gapr::print("aa");
							_insts.st1=ReqStClose;
							check_if_done(this);
							gapr::print("bb");
							//Ptr ptr{wptr};
							//if(!ptr) {
								//return wptr.end();
							//}
							do_send_impl_next(std::move(op->wptr));
							op->complete(ec);
						});
			}
			break;
		case SendOp::Chunk:
			{
				auto op=wptr->_ops.send_que1.take_front<SendChkOp>();
				op->wptr.relay(std::move(wptr));
				std::size_t towrite;
				if(op->buf.size()<=MAX_CHUNK) {
					if(op->eof)
						op->hdr[0]|=CodeIsEof;
					towrite=op->buf.size();
				} else {
					towrite=MAX_CHUNK;
				}
				op->hdr[3]=(towrite>>8);
				op->hdr[4]=(towrite&0xFF);
				std::array<ba::const_buffer, 2> bufs={
					ba::const_buffer{&op->hdr[0], op->hdr.size()},
					ba::const_buffer{op->buf.data(), towrite}
				};
				//if(op->hdr[0]&CodeIsEof)
					//fprintf(stderr, "DDD %p: CHK %hd:%hd }\n", this, op->seq(), op->idx);
				//else
					//fprintf(stderr, "DDD %p: CHK %hd:%hd .\n", this, op->seq(), op->idx);
				return ba::async_write(sock(), bufs,
						[op=std::move(op),this,towrite](error_code ec, std::size_t nbytes) mutable {
							if(ec) {
								_st1=ConnStErr;
								op->wptr.end();
								return op->complete(ec);
							}

							assert(nbytes==op->hdr.size()+towrite);
							op->nwrite+=towrite;
							op->buf.remove_prefix(towrite);
							if(op->buf.size()>0) {
								auto wptr=std::move(op->wptr);
								wptr->_ops.send_que1.push_back(std::move(op));
								return do_send_impl_next(std::move(wptr));
							}
							if(op->eof) {
								_insts.st1=ReqStClose;
								check_if_done(this);
							}
							//
							///////////////
							//Ptr ptr{wptr};
							//if(!ptr)
							//return;// XXX
							do_send_impl_next(std::move(op->wptr));
							op->complete(ec);
						});
			}
			break;
		default:
			gapr::print("type: ", _op.type);
			assert(0);
	}
}

void gapr::connection::impl::do_send_impl_next(WeakPtr<false, true, false>&& wptr) {
	assert(_st1==ConnStOk || _st1==ConnStSd);
	if(!_ops.send_que1.empty()) {
		do_send_impl_que1(std::move(wptr));
		return;
	}
	wptr.end();
#if 0
	while(!_write_ops.empty()) {
		//if(ok)
		do_send_cli_impl1(std::move(wptr));
		return;
		_write_ops.pop_front();
	}
	wptr.unset();
#endif
}

void impl::start_timer() {
	new(&_timer) steady_timer{_ssl->get_executor()};
	_refc.fetch_or(1<<BIT_TIMER_WAITING);
	WeakPtr<false, false, true> wptr{this};
	keep_heartbeat(std::move(wptr));
}
void impl::keep_heartbeat(WeakPtr<false, false, true>&& wptr) {
	auto nc=_timer.expires_after(std::chrono::seconds{96});
	(void)nc;
	_timer.async_wait([wptr=std::move(wptr),this](bs::error_code ec) mutable {
		do {
			if(ec)
				break;
			if(_st0!=ConnStOk || _st1!=ConnStOk)
				break;
			heartbeat();
			return keep_heartbeat(std::move(wptr));
		} while(false);
		wptr.end();
	});
}
void impl::heartbeat() {
	Lock lck_{this};
	[[maybe_unused]] bs::error_code ec;
	if(WeakPtr<false, true, false> wptr{this})
		heartbeat_impl(std::move(wptr));
}
void impl::heartbeat_impl(WeakPtr<false, true, false>&& wptr) {
	const char* blank_line="\n";
	ba::const_buffer buf{blank_line, 1};
	fprintf(stderr, "heartbeat\n");
	ba::async_write(sock(), buf,
		[wptr=std::move(wptr),this](error_code ec, std::size_t nbytes) mutable {
			if(ec) {
				_st1=ConnStErr;
				wptr.end();
				return;
			}
			assert(nbytes==1);
			do_send_impl_next(std::move(wptr));
	});
}
////////

//////////
					/////
#define XXX



static inline bool check_st_write(int st, bs::error_code& ec) noexcept {
	assert(st==ConnStOk || st==ConnStErr);
	switch(st) {
		case ConnStPreCon:
			ec=ba::error::not_connected;
			break;
		case ConnStPreHs:
			ec=ba::error::not_connected;// XXX
			break;
		case ConnStOk: case ConnStSd:
			return true;
		case ConnStPostSd: case ConnStErr:
			ec=ba::error::bad_descriptor;
			break;
	}
	return false;
}

void gapr::connection::impl::do_write_str(std::unique_ptr<SendChkOp>&& op) {
	Lock lck_{this};
	bs::error_code ec;
	do {
		if(!check_st_write(_st1, ec))
			break;
		assert(_insts.st1==ReqStOpen);
		if(op->eof)
			_insts.st1=ReqStClosing;
		uint16_t seq=0;
		op->hdr[0]=CodeSpec;
		op->hdr[1]=seq>>8;
		op->hdr[2]=seq&0xFF;
		_ops.send_que1.push_back(std::move(op));
		if(WeakPtr<false, true, false> wptr{this})
			do_send_impl_que1(std::move(wptr));
		return;
	} while(false);
	assert(_st1==ConnStErr);
	ba::post(sock().get_executor(), [op=std::move(op),ec]() mutable {
		op->complete(ec);
	});
}

static inline bool check_st_read(int st, bs::error_code& ec) noexcept {
	assert(st==ConnStOk);
	switch(st) {
		case ConnStPreCon:
			ec=ba::error::not_connected;
			break;
		case ConnStPreHs:
			ec=ba::error::not_connected;// XXX
			break;
		case ConnStOk:
			return true;
		case ConnStSd: case ConnStPostSd: case ConnStErr:
			ec=ba::error::bad_descriptor;
			break;
	}
	return false;
}

void gapr::connection::impl::do_read_str(std::unique_ptr<ReadStrOp>&& op) {
	Lock lck_{this};
	bs::error_code ec;
	do {
		if(!check_st_read(_st0, ec))
			break;
		if(_ops.read) {
			assert(0);
			//seq
			ec=ba::error::in_progress;
			break;
		}
		assert(_insts.st0==ReqStOpen);
		_ops.read=std::move(op);
		gapr::print("begin read: ");
		_read_ops_n++;
		if(WeakPtr<true, false, false> wptr{this})
			do_recv_impl(std::move(wptr), true);
		return;
	} while(false);
	assert(0);
	ba::post(sock().get_executor(), [op=std::move(op),ec]() mutable {
		op->complete(ec);
	});
}


enum RecvStates {
	RECV_INIT=0,
	RECV_GET_LINE0,
	RECV_GOT_LINE0,
	RECV_EXT_LINE,
	RECV_GOT_LINE,
	RECV_GET_MISC0,
	RECV_GOT_MISC0,
	RECV_GOT_CODE_0,
	RECV_GOT_CODE_B,
	RECV_GOT_CODE_S,
	//////////
//////////////////////////////////////////
	//////////
	RECV_GOT_MSG,
	RECV_GET_BODY,
	RECV_GOT_BODY,
	RECV_GOT_CHUNK,
	RECV_GOT_ABRT,
	RECV_GOT_TRY_ABRT,
	RECV_GOT_END,
	RECV_BODY,
	RECV_TEXT,
	RECV_NEXT,
	RECV_MAYBE_NEXT,
	RECV_OOO,
	RECV_OOO_ABRT,
	RECV_OOO_BODY,
	RECV_OOO_MSG,
	RECV_OOO_MSG_ABRT,
	//////

};

inline std::unique_ptr<impl::RecvHdrOp> impl::do_recv_impl_get_op() noexcept {
	if(!_recv_is_notif) { // notifs, reqs
		if(_is_srv) {
			if(!_ops.recv) {
				gapr::print("delayed");
				//_recv_msgs_que.emplace_back(std::move(_recv_msg));
				return nullptr;
			}
			_insts.misc=_recv_misc;
			_insts.st0=(_recv_has_body?ReqStOpen:ReqStClose);
			check_if_done(this);
			// XXX maybe no post?
			//post(sock().get_executor(), [this,op=std::move(_ops.recv)]() mutable {
			assert(!_ops.read);
			return std::move(_ops.recv);
			//gapr::print("dispatch req/notif: ");
			//});
		} else {
			if(!_ops.recv_reply) {
				gapr::print("delayed response: ");
				return nullptr;
			}
			gapr::print("imm response: ", ' ');
			_insts.misc=_recv_misc;
			_insts.st0=(_recv_has_body?ReqStOpen:ReqStClose);
			check_if_done(this);
			_recv_ops_reply_n--;
			// XXX maybe no post?
			assert(!_ops.read);
			return std::move(_ops.recv_reply);
		}
	}
	return {};
}

void gapr::connection::impl::do_recv_impl(WeakPtr<true, false, false>&& _wptr, bool in_api) {
	auto wptr=std::move(_wptr); // so that wptr.~dtor() call here
	//assert(!_recv_map.empty() || !_recv_wq.empty() || !_recv_strs.empty());

	do switch(_recv_st) {

		case RECV_INIT:
			_recv_start=0;
			_recv_off=0;
			_recv_st=RECV_GET_LINE0;
			continue;

		case RECV_GET_LINE0: // read to probe
			assert(_recv_buf.size()>=2*MAX_HEADER+_recv_start);
			assert(_recv_off==_recv_start);
			return sock().async_read_some(ba::mutable_buffer{_recv_buf.data()+_recv_start, MAX_HEADER}, [this,wptr=std::move(wptr)](bs::error_code ec, std::size_t nbytes) mutable {
				if(ec) {
					gapr::print("read err0: ", ec.message());
					_st0=ConnStErr;
					if(ec==ba::error::eof || ec==ba::ssl::error::stream_truncated) {
						if(auto op=std::move(_ops.recv)) {
							_insts.st0=ReqStInit;
							op->complete(ba::error::eof);
							//wptr.end();
							//return;
						}
						if(auto op=std::move(_ops.delayed_recv)) {
							op->complete(ba::error::eof);
						}
						if(auto op=std::move(_ops.read)) {
							op->complete(ba::ssl::error::stream_truncated);
						}

						//XXX
						if(auto op=std::move(_ops.recv_reply)) {
							//_insts[_recv_idx-1].st0=ReqStInit;
							op->complete(ec);
						}
						wptr.end();
						return;
					}
					//
					//err: sterr, close, dispatch
					//eof: stclose, noclose, dispatch
					gapr::print("read err: ", ec.message());
					//close???
#if 0
					while(!_recv_map.empty()) {
						auto it=_recv_map.begin();
						auto op=std::move(it->second);
						_recv_map.erase(it);
						op->complete(ec, {});
					}
					while(!_recv_wq.empty()) {
						auto op=std::move(_recv_wq.front());
						_recv_wq.pop_front();
						op->complete(ec, {});
					}
					while(!_recv_strs.empty()) {
						auto it=_recv_strs.begin();
						auto op=std::move(it->second);
						_recv_strs.erase(it);
						op->complete(ec);
					}
#endif
					if(auto op=std::move(_ops.recv)) {
						op->complete(ec);
						//wptr.end();
						//return;
					}
					if(auto op=std::move(_ops.recv_reply)) {
						//_insts[_recv_idx-1].st0=ReqStInit;
						op->complete(ec);
					}
					wptr.end();
					return;

					// XXX
					//throw;
				}
				gapr::print("read bytes: ", nbytes);
				if(nbytes>0) {
					_recv_off+=nbytes;
					_recv_st=RECV_GOT_LINE0;
				}
				do_recv_impl(std::move(wptr), false);
			});

		case RECV_GOT_LINE0:
			assert(_recv_buf.size()>=2*MAX_HEADER+_recv_start);
			assert(_recv_off>_recv_start);
			assert(_recv_off<=_recv_start+MAX_HEADER);
			if(!(_recv_buf[_recv_start]&CodeSpec)) {
				_recv_is_notif=(_recv_buf[_recv_start]=='*');
				if(_recv_is_notif)
					_recv_start++;

				auto end=_recv_buf.data()+_recv_off;
				auto ptr=std::find(_recv_buf.data()+_recv_start, end, '\n');
				if(ptr!=end) {
					*ptr='\x00';
					if(!_recv_hdr.parse(_recv_buf.data()+_recv_start, ptr))
						throw std::runtime_error{"fail to parse"};
					_recv_start=ptr+1-_recv_buf.data();
					_recv_st=RECV_GOT_LINE;
					continue;
				}
				if(_recv_off>=_recv_start+MAX_HEADER)
					throw std::runtime_error{"line too long"};
				_recv_st=RECV_EXT_LINE;
				continue;
			}
			_recv_msg_code=_recv_buf.data()[_recv_start];
			if(!(_recv_msg_code&CodeIsCtrl)) {
				if(_recv_off<_recv_start+5) {
					std::size_t toread=_recv_start+5-_recv_off;
					return ba::async_read(sock(), ba::mutable_buffer{&_recv_buf[_recv_off], toread}, [this,toread,wptr=std::move(wptr)](bs::error_code ec, std::size_t nbytes) mutable {
						if(ec)
							throw;
						if(nbytes!=toread)
							throw;
						_recv_off+=nbytes;
						_recv_st=RECV_GOT_CHUNK;
			assert(_recv_start+3+2<=_recv_off);
						do_recv_impl(std::move(wptr), false);
					});
				}
			assert(_recv_start+3+2<=_recv_off);
				_recv_st=RECV_GOT_CHUNK;
				continue;
			}
			switch(_recv_msg_code&CodeMaskCtrl) {
				case CodeAbrt:
					if(_recv_off<_recv_start+3) {
						std::size_t toread=_recv_start+3-_recv_off;
						return ba::async_read(sock(), ba::mutable_buffer{&_recv_buf[_recv_off], toread}, [this,toread,wptr=std::move(wptr)](bs::error_code ec, std::size_t nbytes) mutable {
							if(ec)
								throw;
							if(nbytes!=toread)
								throw;
							_recv_st=RECV_GOT_ABRT;
							do_recv_impl(std::move(wptr), false);
						});
					}
					_recv_st=RECV_GOT_ABRT;
					break;
				case CodeTryAbrt:
					if(_recv_off<_recv_start+3) {
						std::size_t toread=_recv_start+3-_recv_off;
						return ba::async_read(sock(), ba::mutable_buffer{&_recv_buf[_recv_off], toread}, [this,toread,wptr=std::move(wptr)](bs::error_code ec, std::size_t nbytes) mutable {
							if(ec)
								throw;
							if(nbytes!=toread)
								throw;
							_recv_st=RECV_GOT_TRY_ABRT;
							do_recv_impl(std::move(wptr), false);
						});
					}
					_recv_st=RECV_GOT_TRY_ABRT;
					break;
					//////////
				case CodeMsgEnd:
					_recv_st=RECV_GOT_END;
					break;
				default:
					if(auto op=std::move(_ops.recv)) {
						op->complete(ba::error::bad_descriptor);
						//wptr.end();
						//return;
					}
					if(auto op=std::move(_ops.recv_reply)) {
						//_insts[_recv_idx-1].st0=ReqStInit;
						op->complete(ba::error::bad_descriptor);
					}
					wptr.end();
					return;
					
			}
			continue;

		case RECV_EXT_LINE: // extend cur line
			assert(_recv_buf.size()>=2*MAX_HEADER+_recv_start);
			assert(_recv_off>_recv_start);
			assert(_recv_off<_recv_start+MAX_HEADER);
			// [0, _recv_off) no '\n'
			return sock().async_read_some(ba::mutable_buffer{_recv_buf.data()+_recv_off, _recv_start+MAX_HEADER-_recv_off}, [this,wptr=std::move(wptr)](bs::error_code ec, std::size_t nbytes) mutable {
				if(ec)
					throw ec; // XXX
				_recv_off+=nbytes;
				auto end=_recv_buf.data()+_recv_off;
				auto ptr=std::find(end-nbytes, end, '\n');
				if(ptr!=end) {
					*ptr='\x00';
					if(!_recv_hdr.parse(_recv_buf.data()+_recv_start, ptr))
						throw std::runtime_error{"fail to parse2"};
					_recv_start=ptr+1-_recv_buf.data();
					_recv_st=RECV_GOT_LINE;
				} else if(_recv_off>=_recv_start+MAX_HEADER)
					throw std::runtime_error{"line too long"};
				do_recv_impl(std::move(wptr), false);
			});

		case RECV_GOT_LINE:
			assert(_recv_hdr.ptr[_recv_hdr.len]=='\00');
			assert(_recv_off<_recv_start+MAX_HEADER);
			assert(_recv_buf.size()>=MAX_HEADER+_recv_start);

			//gapr::print("got line: ");
			//fwrite(_recv_hdr.ptr, 1, _recv_hdr.len, stderr);

			// _recv_buf()[0, _recv_off): remaining
			if(_recv_hdr.len==0) {
				fprintf(stderr, "empty line\n");
				_recv_st=RECV_NEXT;
				continue;
			}
			//if(!_recv_hdr.parse().second)
				//throw std::runtime_error{"wrong header"};
			if(_recv_off>_recv_start)
				_recv_st=RECV_GOT_MISC0;
			else
				_recv_st=RECV_GET_MISC0;
			continue;

		case RECV_GET_MISC0:
			assert(_recv_buf.size()>=MAX_HEADER+_recv_start);
			assert(_recv_off==_recv_start);
			return sock().async_read_some(ba::mutable_buffer{_recv_buf.data()+_recv_start, MAX_HEADER}, [this,wptr=std::move(wptr)](bs::error_code ec, std::size_t nbytes) mutable {
				if(ec) {
					_st0=ConnStErr;
					gapr::print("read err 2: ", ec.message());
					//close???
					return;
					// XXX
					//throw;
				}
				if(nbytes>0) {
					_recv_off+=nbytes;
					_recv_st=RECV_GOT_MISC0;
				}
				do_recv_impl(std::move(wptr), false);
			});

		case RECV_GOT_MISC0:
			assert(_recv_buf.size()>=MAX_HEADER+_recv_start);
			assert(_recv_off>_recv_start);
			assert(_recv_off<=_recv_start+MAX_HEADER);
			if(!(_recv_buf.data()[_recv_start]&CodeSpec)) {
				fprintf(stderr, "unknown char %02x\n", _recv_buf.data()[_recv_start]);
				if(auto op=std::move(_ops.recv)) {
					op->complete(ba::error::eof);
				}
				if(auto op=std::move(_ops.recv_reply)) {
					op->complete(ba::error::eof);
				}
				wptr.end();
				return;
				throw std::runtime_error{"unknown char"};
			}
			_recv_msg_code=_recv_buf.data()[_recv_start];
			switch(_recv_msg_code&CodeMaskTag) {
				case CodeHdrOnly:
					if(_recv_off<_recv_start+3) {
						std::size_t toread=_recv_start+3-_recv_off;
						return ba::async_read(sock(), ba::mutable_buffer{&_recv_buf[_recv_off], toread}, [this,toread,wptr=std::move(wptr)](bs::error_code ec, std::size_t nbytes) mutable {
							if(ec)
								throw;
							if(nbytes!=toread)
								throw;
							_recv_off+=nbytes;
							_recv_st=RECV_GOT_CODE_0;
							do_recv_impl(std::move(wptr), false);
						});
					}
					_recv_st=RECV_GOT_CODE_0;
					continue;
				case CodeHasBody:
					if(_recv_off<_recv_start+7) {
						std::size_t toread=_recv_start+7-_recv_off;
						return ba::async_read(sock(), ba::mutable_buffer{&_recv_buf[_recv_off], toread}, [this,toread,wptr=std::move(wptr)](bs::error_code ec, std::size_t nbytes) mutable {
							if(ec)
								throw;
							if(nbytes!=toread)
								throw;
							_recv_off+=nbytes;
							_recv_st=RECV_GOT_CODE_B;
							do_recv_impl(std::move(wptr), false);
						});
					}
					_recv_st=RECV_GOT_CODE_B;
					continue;
				case CodeHasStrm:
					gapr::print("has strm");
					if(_recv_off<_recv_start+13) {
						std::size_t toread=_recv_start+13-_recv_off;
						return ba::async_read(sock(), ba::mutable_buffer{&_recv_buf[_recv_off], toread}, [this,toread,wptr=std::move(wptr)](bs::error_code ec, std::size_t nbytes) mutable {
							if(ec)
								throw;
							if(nbytes!=toread)
								throw;
							_recv_off+=nbytes;
							_recv_st=RECV_GOT_CODE_S;
							assert(_recv_start+5+8<=_recv_off);
							do_recv_impl(std::move(wptr), false);
						});
					}
					assert(_recv_start+5+8<=_recv_off);
					_recv_st=RECV_GOT_CODE_S;
					continue;
				default:
					gapr::print("bincode: ", (int)_recv_msg_code);
					throw std::runtime_error{"unknown type"};
			}

		case RECV_GOT_CODE_0:
			_recv_has_body=false;
			_recv_misc.var=0;
			_recv_misc.siz=0;
			_recv_misc.typ=gapr::server_end::msg_type::hdr_only;
			_recv_start+=3;
			_recv_st=RECV_GOT_MSG;
			_recv_st_next=RECV_MAYBE_NEXT;
			gapr::print("got bin0: ");
			continue;

		case RECV_GOT_CODE_B:
			_recv_misc.var=be2host(*reinterpret_cast<uint16_t*>(&_recv_buf[_recv_start+3]));
			_recv_misc.sizhint=_recv_misc.siz=_recv_chk_left=be2host(*reinterpret_cast<uint16_t*>(&_recv_buf[_recv_start+5]));
			_recv_misc.typ=gapr::server_end::msg_type::stream;
			_recv_start+=7;
			_recv_st=RECV_GOT_MSG;
			_recv_chk_is_eof=true;
			_recv_st_next=_recv_chk_left>0?RECV_GET_BODY:RECV_MAYBE_NEXT;
			_recv_has_body=_recv_chk_left>0;
			gapr::print("got binB: ");
			continue;

		case RECV_GOT_CODE_S:
			assert(_recv_start+5+8<=_recv_off);
			_recv_misc.var=be2host(*reinterpret_cast<uint16_t*>(&_recv_buf[_recv_start+3]));
			_recv_misc.sizhint=be2host(*reinterpret_cast<uint64_t*>(&_recv_buf[_recv_start+5]));
			_recv_misc.typ=gapr::server_end::msg_type::stream;
			_recv_start+=13;
			assert(_recv_start<=_recv_off);
			_recv_st=RECV_GOT_MSG;
			_recv_st_next=RECV_MAYBE_NEXT;
			_recv_has_body=true;
			gapr::print("got binS: ");
			continue;

		case RECV_GOT_MSG:
			//std::cerr.write(_recv_hdr.base(), _recv_hdr.len());
			//gapr::print(_recv_msg.seq, ' ', _recv_msg.type);
			// _recv_msg: msg
			// _recv_buf()[0, _recv_off): remaining
			assert(_recv_off<_recv_start+MAX_HEADER);

			if(auto op=do_recv_impl_get_op()) {
				if(in_api) {
					gapr::print("post msg");
			//
					return post(sock().get_executor(), [op=std::move(op),this,wptr=std::move(wptr)]() mutable {
						gapr::print("got msg post: ", _recv_hdr.ptr);
						op->complete({}, reinterpret_cast<msg_hdr_in&>(_recv_hdr));
						_recv_st=_recv_st_next;
						do_recv_impl(std::move(wptr), false);
					});
				} else {
					gapr::print("got msg { ", _recv_hdr.ptr);
					op->complete({}, reinterpret_cast<msg_hdr_in&>(_recv_hdr));
					gapr::print("got msg } ");
					_recv_st=_recv_st_next;
				}
			} else {
				gapr::print("pending msg");
				wptr.end();
				return;
			}
			// if(op=func())
			//
			break;

		case RECV_GOT_CHUNK:
			assert(_recv_start+3+2<=_recv_off);
			_recv_chk_left=be2host(*reinterpret_cast<uint16_t*>(&_recv_buf[_recv_start+3]));
			_recv_chk_is_eof=(_recv_msg_code&CodeIsEof);
			_recv_start+=5;
			assert(_recv_off>=_recv_start);
			_recv_st=_recv_chk_left>0?RECV_GET_BODY:RECV_GET_BODY;
			continue;

		case RECV_GET_BODY:
			assert(_recv_chk_left>=0);
			assert(_recv_off>=_recv_start);
			if(auto op=_ops.read.get()) {
				auto toread=_recv_chk_left;
				if(toread>op->buf.size())
					toread=op->buf.size();
				if(_recv_off<_recv_start+toread) {
					_recv_chk_left-=toread;
					std::copy(&_recv_buf[_recv_start], &_recv_buf[_recv_off], op->buf.data());
					op->nread+=_recv_off-_recv_start;
					op->buf.skip(_recv_off-_recv_start);
					toread-=_recv_off-_recv_start;
					_recv_start=_recv_off;
					//gapr::print("recv toread: ", toread);
					return ba::async_read(sock(), ba::mutable_buffer{op->buf.data(), toread}, [this,wptr=std::move(wptr),toread,op](bs::error_code ec, std::size_t nbytes) mutable {
						if(ec) {
							gapr::print("err read body");
							//throw std::runtime_error{"err read body"};
							if(auto op=std::move(_ops.delayed_recv)) {
								op->complete(ba::error::eof);
							}
							if(auto op=std::move(_ops.read)) {
								op->complete(ba::ssl::error::stream_truncated);
							}

							wptr.end();
							return;
						}
						assert(nbytes==toread);
						//fwrite(op->buf.data(), 1, toread, stderr);
						op->nread+=toread;
						op->buf.skip(toread);
						_recv_st=RECV_GOT_BODY;
						do_recv_impl(std::move(wptr), false);
					});
				} else {
					std::copy(&_recv_buf[_recv_start], &_recv_buf[_recv_start+toread], op->buf.data());
					_recv_start+=toread;
					op->nread+=toread;
					op->buf.skip(toread);
					_recv_chk_left-=toread;
					_recv_st=RECV_GOT_BODY;
					continue;
				}
			}
			gapr::print("delayed body");
			wptr.end();
			return;

		case RECV_GOT_BODY:
			assert(_recv_start<=_recv_off);
			assert(_ops.read.get());

			gapr::print("got body: ");
			if(_recv_chk_left<=0 && _ops.read->buf.size()>0 && !_recv_chk_is_eof) {
				assert(_recv_start<=_recv_off);
				_recv_st=RECV_NEXT;
				continue;
			}
			{
				auto op=std::move(_ops.read);
				bool is_eof=_recv_chk_left<=0&&_recv_chk_is_eof;
				if(is_eof) {
					_insts.st0=ReqStClose;
					check_if_done(this);
				}
				_read_ops_n--;
				// XXX maybe no post?
				//XXXwptr
				if(in_api) {
					gapr::print("post chk");
					return ba::post(sock().get_executor(), [op=std::move(op),this,wptr=std::move(wptr),is_eof]() mutable {
						_recv_st=_recv_chk_left>0?RECV_GET_BODY:RECV_MAYBE_NEXT;
						op->complete(is_eof?ba::error::eof:bs::error_code{});
						do_recv_impl(std::move(wptr), false);
					});
				} else {
					_recv_st=_recv_chk_left>0?RECV_GET_BODY:RECV_MAYBE_NEXT;
					op->complete(is_eof?ba::error::eof:bs::error_code{});
				}
			}
			break;

		case RECV_NEXT: // next round, maybe with dirty buffer
			assert(_recv_off<_recv_start+MAX_HEADER);
			assert(_recv_start<=_recv_off);
			// _recv_buf[0, _recv_off): unprocessed data
			if(_recv_buf.size()<_recv_start+2*MAX_HEADER) {
				std::copy(&_recv_buf[_recv_start], &_recv_buf[_recv_off], &_recv_buf[0]);
				_recv_off-=_recv_start;
				_recv_start=0;
			}
			if(_recv_off<=_recv_start) {
				assert(_recv_off==_recv_start);
				_recv_st=RECV_GET_LINE0;
				continue;
			}
			_recv_st=RECV_GOT_LINE0;
			continue;

		default:
			throw std::runtime_error{"unknown state"};


			///////////////////////////////////////


#if 0
//
	auto buf=op->hdr.view();
	ba::async_write(sock(), ba::const_buffer{buf.data(), buf.size()},
			[this,op=std::move(op)](error_code ec, std::size_t nbytes) {
				if(ec)
					throw ec;
				assert(nbytes==op->hdr.size());
				do {
					if(try_send_next0())
						break;
					if(try_send_next1())
						break;
					_writing=false;
				} while(false);
				op->complete(ec);
			});
}
#endif
#if 0

void gapr::Connection::do_recv_impl() {

		case RECV_BODY:
			assert(_recv_off<_recv_msg.bdy_len);
			assert(_recv_buf.len()>=_recv_msg.bdy_len);
			// nothing remains

			return ba::async_read(sock(), ba::mutable_buffer{_recv_buf.base()+_recv_off, _recv_msg.bdy_len-_recv_off}, [this](bs::error_code ec, std::size_t nbytes) ->void {
				if(ec)
					throw;
				if(nbytes+_recv_off!=_recv_msg.bdy_len)
					throw;
				_recv_msg.bdy=_recv_buf.split(_recv_msg.bdy_len);
				_recv_off=0;
				_recv_st=RECV_MSG;
				do_recv_impl();
			});

		case RECV_OOO_ABRT:
			assert(_recv_off>0 && _recv_off<=MAX_HEADER);
			assert(_recv_buf.base()[0]&IS_CHK);
			assert(_recv_buf.base()[0]&IS_ABRT);

			if(_recv_off<5) {
				std::memcpy(&_chk_sig[3], _recv_buf.base(), _recv_off);
				auto toread=5-_recv_off;
				_recv_off=0;
				return ba::async_read(sock(), ba::mutable_buffer{&_chk_sig[12]-toread, toread}, [this,toread](bs::error_code ec, std::size_t nbytes) ->void {
					if(ec)
						throw;
					if(nbytes!=toread)
						throw;
					_recv_msg.seq=be2host(*reinterpret_cast<uint32_t*>(&_chk_sig[4]))^1;
					_recv_st=RECV_OOO_MSG_ABRT;
					do_recv_impl();
				});
			}
			_recv_msg.seq=be2host(*reinterpret_cast<uint32_t*>(&_recv_buf.base()[1]))^1;
			_recv_buf.skip(5);
			_recv_off-=5;
			_recv_st=RECV_OOO_MSG_ABRT;
			continue;

		case RECV_OOO_BODY:
			assert(_recv_off<_recv_msg.bdy_len);
			assert(_recv_buf.len()>=_recv_msg.bdy_len);
			// nothing remains

			return ba::async_read(sock(), ba::mutable_buffer{_recv_buf.base()+_recv_off, _recv_msg.bdy_len-_recv_off}, [this](bs::error_code ec, std::size_t nbytes) ->void {
				if(ec)
					throw;
				if(nbytes+_recv_off!=_recv_msg.bdy_len)
					throw;
				_recv_msg.bdy=_recv_buf.split(_recv_msg.bdy_len);
				_recv_off=0;
				_recv_st=RECV_OOO_MSG;
				do_recv_impl();
			});

		case RECV_OOO_MSG_ABRT:
			// _recv_msg: msg
			// _recv_buf()[0, _recv_off): remaining
			assert(_recv_off<MAX_HEADER);

			{
				bool handled=false;
				auto it=_recv_strs.find(_recv_msg.seq);
				if(it!=_recv_strs.end()) {
					if(it->second->can_recv) {
						it->second->complete(/*XXX*/{});
						handled=true;
					}
				}
				if(!handled) {
					auto it2=_recv_str_ques.emplace(_recv_msg.seq, std::deque<gapr::MsgHdrIn>{});
					_recv_msg.type=_chk_sig[3];
					it2.first->second.clear();
					it2.first->second.emplace_back(std::move(_recv_msg));
				}
			}
				assert(_recv_start<=_recv_off);
			_recv_st=RECV_NEXT;
			break;

		case RECV_OOO_MSG:
			// _recv_msg: msg
			// _recv_buf()[0, _recv_off): remaining
			assert(_recv_off<MAX_HEADER);

			{
				//gapr::print("str end");
				bool handled=false;
				auto it=_recv_strs.find(_recv_msg.seq);
				if(it!=_recv_strs.end()) {
					if(it->second->can_recv) {
						auto op=it->second;
						it->second->sink_cb(ReadSlot{std::move(op), std::move(_recv_msg)});
						if(_chk_sig[3]&IS_EOF) {
							auto op=std::move(it->second);
							_recv_strs.erase(it);
							op->complete(/*XXX*/{});
						}
						handled=true;
					}
				}
				if(!handled) {
					auto it2=_recv_str_ques.emplace(_recv_msg.seq, std::deque<gapr::MsgHdrIn>{});
					_recv_msg.type=_chk_sig[3];
					it2.first->second.emplace_back(std::move(_recv_msg));
				}
			}
				assert(_recv_start<=_recv_off);
			_recv_st=RECV_NEXT;
			break;
#endif
		case RECV_MAYBE_NEXT: // next round, maybe with dirty buffer
			if(!_ops.recv && _recv_ops_reply_n<=0 && _read_ops_n<=0 ) {
				wptr.end();
				//gapr::print("!receiving");
				return;
			}
			assert(_recv_start<=_recv_off);
			_recv_st=RECV_NEXT;
			continue;
			////////!_recv_map.empty() || !_recv_wq.empty() || !_recv_strs.empty());
	} while(true);
}


bool impl::hdr_info_base::parse(const void* _ptr, const void* _eptr) noexcept {
	auto p=static_cast<const char*>(_ptr);
	//gapr::print("code: ", p);
	std::size_t n=static_cast<const char*>(_eptr)-p;
	if(n==0) {
		ptr=p;
		len=0;
		return true;
	}
	auto res=gapr::parse_name(p, n);
	if(!res.second)
		return false;
	auto i=res.first;
	if(i<n) {
		if(!std::isspace(p[i]))
			return false;
		i++;
		while(i<n && std::isspace(p[i]))
			i++;
	}
	tlen=res.first;
	aoff=i;
	ptr=p;
	len=n;
	return true;
}

void impl::MsgHdr::do_format(const char* v) noexcept {
	for(auto c=*v; c; c=*(++v)) {
		if(try_push())
			push_back(c);
	}
}
void impl::MsgHdr::do_format(const std::string& v) noexcept {
	for(std::size_t i=0; i<v.length(); i++) {
		if(try_push())
			push_back(v[i]);
	}
}
template<typename T>
static inline void do_format_int_num(impl::MsgHdr& hdr, T v) noexcept {
	static_assert(std::is_unsigned<T>::value);
	unsigned char buf[32];
	std::size_t i=0;
	for(; v; v=(v/10)) {
		buf[i++]='0'+v%10;
	}
	while(i-->0) {
		if(hdr.try_push())
			hdr.push_back(buf[i]);
	}
}
static inline void do_format_int_0(impl::MsgHdr& hdr) noexcept {
	if(hdr.try_push())
		hdr.push_back('0');
}
template<typename T>
static inline void do_format_int_u(impl::MsgHdr& hdr, T v) noexcept {
	static_assert(std::is_unsigned<T>::value);
	if(!v)
		return do_format_int_0(hdr);
	do_format_int_num<T>(hdr, v);
}
template<typename T>
static inline void do_format_int_s(impl::MsgHdr& hdr, T v) noexcept {
	static_assert(std::is_signed<T>::value);
	if(!v)
		return do_format_int_0(hdr);
	do_format_int_num<std::make_unsigned_t<T>>(hdr, v>0?v:-v);
}

template<> GAPR_CORE_DECL void impl::do_format_int<int>(MsgHdr& hdr, int v) noexcept {
	do_format_int_s<int>(hdr, v);
}
template<> GAPR_CORE_DECL void impl::do_format_int<int64_t>(MsgHdr& hdr, int64_t v) noexcept {
	do_format_int_s<int64_t>(hdr, v);
}
template<> GAPR_CORE_DECL void impl::do_format_int<unsigned int>(MsgHdr& hdr, unsigned int v) noexcept {
	do_format_int_u<unsigned int>(hdr, v);
}
template<> GAPR_CORE_DECL void impl::do_format_int<uint64_t>(MsgHdr& hdr, uint64_t v) noexcept {
	do_format_int_u<uint64_t>(hdr, v);
}

void impl::do_close_impl() noexcept {
	gapr::print("sock close");
	bs::error_code ec;
	_ssl->next_layer().close(ec);
}
template<>
impl* impl::lock_rwt<true>(int mask) noexcept {
	auto& refc=_refc;
	auto c=refc.load();
	//gapr::print("ent: ", mask);
	while(!(c&mask)) {
		gapr::print("cmp: ", c>>BIT_COUNT);
		if(refc.compare_exchange_weak(c, c|mask))
			return this;
	}
	//gapr::print("fail: ", c);
	return nullptr;
}
impl* impl::lock_ref(impl* ptr) noexcept {
	auto& refc=ptr->_refc;
	auto c=refc.load();
	gapr::print("lock: ", c>>BIT_COUNT);
	while(c>0) {
		if(refc.compare_exchange_weak(c, c+(1<<BIT_COUNT)))
			return ptr;
	}
	return nullptr;
}

static inline void push_type(impl::MsgHdr& hdr, unsigned char code) noexcept {
	if(hdr.try_push()) hdr.push_back(code);
	if(hdr.try_push()) hdr.push_back(' ');
	if(hdr.try_push()) hdr.push_back(' ');
}
impl::SendHdrOp::SendHdrOp(MsgHdr&& _hdr, std::nullptr_t) noexcept:
SendHdrOp_{HdrOnly}, hdr{std::move(_hdr)}, hdr_len{hdr.size()}, buf{} {
	push_type(hdr, CodeHdrOnlyV);
}
impl::SendHdrOp::SendHdrOp(MsgHdr&& _hdr, uint16_t var, std::string_view buf) noexcept:
SendHdrOp_{HdrBody}, hdr{std::move(_hdr)}, hdr_len{hdr.size()}, buf{buf} {
	push_type(hdr, CodeHasBodyV);
	if(hdr.try_push()) hdr.push_back(var>>8);
	if(hdr.try_push()) hdr.push_back(var&0xFF);
	uint16_t len=buf.size();
	if(hdr.try_push()) hdr.push_back(len>>8);
	if(hdr.try_push()) hdr.push_back(len&0xFF);
}
impl::SendHdrOp::SendHdrOp(MsgHdr&& _hdr, uint16_t var, uint64_t szhint) noexcept:
SendHdrOp_{StrmHdr}, hdr{std::move(_hdr)}, hdr_len{hdr.size()}, buf{} {
	push_type(hdr, CodeHasStrmV);
	if(hdr.try_push()) hdr.push_back(var>>8);
	if(hdr.try_push()) hdr.push_back(var&0xFF);
	for(std::size_t i=sizeof(szhint); i-->0; )
		if(hdr.try_push()) hdr.push_back((szhint>>(8*i))&0xff);
}
bool gapr::connection::msg_hdr_in::tag_is(const char* tag) const noexcept {
	std::size_t i;
	for(i=0; i<_b.tlen; i++) {
		if(_b.ptr[i]!=tag[i]) return false;
	}
	return tag[i]=='\x00';
}

// XXX // ensure dtor cb
//        that's: _ssl exists when need in cb's.



#include "detail/connection.unit-test.hh"
#include "gapr/detail/template-class-static-data.hh"

void* template_class_static_data_2() {
	return &gapr::detail::template_class<int>::data;
}
