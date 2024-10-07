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


#include "gapr/connection.hh"

#include "gapr/utility.hh"
#include "gapr/parser.hh"


//#include <random>
#include <algorithm>

#include <boost/asio/read.hpp>
#include <boost/asio/ssl/error.hpp>
//#include <boost/asio/write.hpp>

#include <nghttp2/nghttp2.h>

namespace ba=boost::asio;
namespace bs=boost::system;
using impl=gapr::connection::impl;

enum ConnSt {
	ConnStPreCon, ConnStPreHs,
	ConnStOk, ConnStSd, ConnStPostSd, ConnStErr,
	//?PreClose,,
};

struct nghttp2_session_delete {
	void operator()(nghttp2_session* session) const noexcept {
		nghttp2_session_del(session);
	}
};
struct nghttp2_session_callbacks_delete {
	void operator()(nghttp2_session_callbacks* callbacks) const noexcept {
		nghttp2_session_callbacks_del(callbacks);
	}
};
struct nghttp2_option_delete {
	void operator()(nghttp2_option* options) const noexcept {
		nghttp2_option_del(options);
	}
};

using session_ptr=std::unique_ptr<nghttp2_session, nghttp2_session_delete>;
using callbacks_ptr=std::unique_ptr<nghttp2_session_callbacks, nghttp2_session_callbacks_delete>;
using option_ptr=std::unique_ptr<nghttp2_option, nghttp2_option_delete>;

enum RecvFlags {
	ZeroInput=0,
	HeaderFrame=1,
	HeaderOnly=2,
	FrameChunk=4,
	LastChunk=8,
	WindowUpdate=16,
};

struct gapr::connection::impl::impl_h2 {
	option_ptr opt;
	session_ptr sess;
	callbacks_ptr cbs;
	std::vector<uint8_t> outbuf;
	int32_t cur_str{-1};
	impl_h2() { }
	static option_ptr create_options() {
		nghttp2_option* ptr;
		auto r=nghttp2_option_new(&ptr);
		if(r!=0) {
			gapr::print("failed to create1: ", nghttp2_strerror(r));
			return option_ptr{};
		}
		option_ptr opt{ptr};
		nghttp2_option_set_no_http_messaging(ptr, 1);
		return opt;
	}
	static session_ptr create_server(const nghttp2_session_callbacks* cbs, impl_h2* uptr) {
		nghttp2_session* ptr;
		auto r=nghttp2_session_server_new3(&ptr, cbs, uptr, uptr->opt.get(), nullptr);
		if(r==0)
			return session_ptr{ptr};
		gapr::print("failed to create1: ", nghttp2_strerror(r));
		return session_ptr{};
	}
	static session_ptr create_client(const nghttp2_session_callbacks* cbs, impl_h2* uptr) {
		nghttp2_session* ptr;
		auto r=nghttp2_session_client_new3(&ptr, cbs, uptr, uptr->opt.get(), nullptr);
		if(r==0)
			return session_ptr{ptr};
		gapr::print("failed to create2: ", nghttp2_strerror(r));
		return session_ptr{};
	}
	static callbacks_ptr create_callbacks() {
		nghttp2_session_callbacks* ptr;
		auto r=nghttp2_session_callbacks_new(&ptr);
		if(r!=0) {
			gapr::print("failed to create2: ", nghttp2_strerror(r));
			return callbacks_ptr{};
		}
		callbacks_ptr cbs{ptr};
		nghttp2_session_callbacks_set_on_frame_not_send_callback(ptr, on_frame_not_send);
		nghttp2_session_callbacks_set_select_padding_callback(ptr, select_padding);
		nghttp2_session_callbacks_set_before_frame_send_callback(ptr, before_frame_send);
		nghttp2_session_callbacks_set_send_callback(ptr, send_cb);
		nghttp2_session_callbacks_set_on_frame_send_callback(ptr, on_frame_send);
		nghttp2_session_callbacks_set_on_stream_close_callback(ptr, on_stream_close);
		nghttp2_session_callbacks_set_recv_callback(ptr, recv_cb);
		nghttp2_session_callbacks_set_on_begin_frame_callback(ptr, on_begin_frame);
		nghttp2_session_callbacks_set_on_data_chunk_recv_callback(ptr, on_data_chunk_recv);
		nghttp2_session_callbacks_set_on_frame_recv_callback(ptr, on_frame_recv);
		nghttp2_session_callbacks_set_on_begin_headers_callback(ptr, on_begin_headers);
		nghttp2_session_callbacks_set_on_header_callback(ptr, on_header);
		nghttp2_session_callbacks_set_on_invalid_header_callback(ptr, on_invalid_header);
		nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(ptr, on_invalid_frame_recv);

		return cbs;
	}
	void setup_client(impl* ptr) {
		cbs=create_callbacks();
		opt=create_options();
		sess=create_client(cbs.get(), this);
		ensure_settings();
	}
	void setup_server(impl* ptr) {
		cbs=create_callbacks();
		opt=create_options();
		sess=create_server(cbs.get(), this);
		ensure_settings();
	}

	static FILE* open_dbg_file(const char* templ, bool s) {
		char buf[1024];
		snprintf(buf, 1024, "%s-%s", templ, s?"srv":"cli");
		//auto fd=mkstemp(buf)
		return fopen(buf, "wb");
	}
	FILE* dbg_file=nullptr;
	static int on_frame_not_send(nghttp2_session* session, const nghttp2_frame* frame, int lib_error_code, void* user_data) {
		gapr::print(1, "on_frame_not_send ", nghttp2_strerror(lib_error_code));
		return -1;
	}
	static ssize_t select_padding(nghttp2_session* session, const nghttp2_frame* frame, size_t max_payloadlen, void* user_data) {
		gapr::print(1, "select_padding");
		return frame->hd.length;
	}
	static int before_frame_send(nghttp2_session* session, const nghttp2_frame* frame, void* user_data) {
		gapr::print(1, "before_frame_send");
		return 0;
	}
	static ssize_t send_cb(nghttp2_session* session, const uint8_t* data, size_t length, int flags, void* user_data) {
		gapr::print(1, "send_cb");
		auto h2=(impl_h2*)user_data;
		auto& buf=h2->outbuf;
		buf.reserve(h2->outbuf.size()+length);
		for(std::size_t i=0; i<length; i++)
			buf.push_back(data[i]);

		{
			if(!h2->dbg_file)
				h2->dbg_file=open_dbg_file("/tmp/gapr-conn-out", nghttp2_session_check_server_session(h2->sess.get()));
			fwrite(data, 1, length, h2->dbg_file);
			fflush(h2->dbg_file);
		}
		return length;
	}
	bool frm_end;
	static int on_frame_send(nghttp2_session* session, const nghttp2_frame* frame, void* user_data) {
		gapr::print(1, "on_frame_send");
		auto h2=(impl_h2*)user_data;
		switch(frame->hd.type) {
			case NGHTTP2_SETTINGS:
				break;
			default:
				h2->frm_end=true;
				break;
		}
		return 0;
	}
	static int on_stream_close(nghttp2_session* session, int32_t stream_id, uint32_t error_code, void* user_data) {
		gapr::print(1, "on_stream_close ", nghttp2_strerror(error_code));
		auto h2=(impl_h2*)user_data;
		h2->cur_str=-1;
		return 0;
	}
	std::vector<uint8_t> inbuf;
	std::size_t inbufpos;
	bs::error_code inbufec;
	FILE* dbg_file_in=nullptr;
	int frm_in=RecvFlags::ZeroInput;
	static ssize_t recv_cb(nghttp2_session* session, uint8_t* buf, size_t length, int flags, void* user_data) {
		gapr::print(1, "recv_cb");
		auto h2=(impl_h2*)user_data;
		auto& inbuf=h2->inbuf;
		auto len=inbuf.size()-h2->inbufpos;
		if(len<=0) {
			if(h2->inbufec) {
				gapr::print(1, "recv_cb err ", h2->inbufec.message());
				if(h2->inbufec==ba::error::eof || h2->inbufec==ba::ssl::error::stream_truncated)
					return NGHTTP2_ERR_EOF;
				return NGHTTP2_ERR_CALLBACK_FAILURE;
			}
			gapr::print(1, "recv_cb block");
			return NGHTTP2_ERR_WOULDBLOCK;
		}
		if(len>length)
			len=length;
		std::copy_n(inbuf.data()+h2->inbufpos, len, buf);

		{
			if(!h2->dbg_file_in)
				h2->dbg_file_in=open_dbg_file("/tmp/gapr-conn-in", nghttp2_session_check_server_session(h2->sess.get()));
			fwrite(inbuf.data()+h2->inbufpos, 1, len, h2->dbg_file_in);
			fflush(h2->dbg_file_in);
		}
		h2->inbufpos+=len;
		return len;
	}
	static int on_begin_frame(nghttp2_session* session, const nghttp2_frame_hd* hd, void *user_data) {
		gapr::print(1, "on_begin_frame");
		return 0;
	}
	const uint8_t* chkptr;
	std::size_t chklen;
	static int on_data_chunk_recv(nghttp2_session* session, uint8_t flags, int32_t stream_id, const uint8_t* data, size_t len, void* user_data) {
		gapr::print(1, "on_data_chunk_recv");
		auto h2=(impl_h2*)user_data;
		h2->chkptr=data;
		h2->chklen=len;
		h2->frm_in|=FrameChunk;
		if(flags&NGHTTP2_FLAG_END_STREAM)
			h2->frm_in|=LastChunk;
		return 0;
	}

	static int on_frame_recv(nghttp2_session* session, const nghttp2_frame* frame, void* user_data) {
		gapr::print(1, "on_frame_recv ", (int)frame->hd.type);
		auto h2=(impl_h2*)user_data;
		switch(frame->hd.type) {
			case NGHTTP2_SETTINGS:
				break;
			case NGHTTP2_HEADERS:
				assert(frame->hd.flags&NGHTTP2_FLAG_END_HEADERS);
				h2->frm_in|=HeaderFrame;
				if(frame->hd.flags&NGHTTP2_FLAG_END_STREAM)
					h2->frm_in|=HeaderOnly;
				h2->cur_str=frame->hd.stream_id;
				break;
			case NGHTTP2_WINDOW_UPDATE:
				h2->frm_in|=WindowUpdate;
				break;
			case NGHTTP2_DATA:
				h2->frm_in|=FrameChunk;
				if(frame->hd.flags&NGHTTP2_FLAG_END_STREAM)
					h2->frm_in|=LastChunk;
				break;
		}
		return 0;
	}
	std::string cur_header;
	std::size_t tag_size;
	static int on_begin_headers(nghttp2_session* session, const nghttp2_frame* frame, void* user_data) {
		gapr::print(1, "on_begin_headers");
		auto h2=(impl_h2*)user_data;
		h2->cur_header.resize(0);
		h2->tag_size=0;
		h2->chkptr=nullptr;
		h2->chklen=0;
		return 0;
	}
	static int on_header(nghttp2_session* session, const nghttp2_frame* frame, const uint8_t* name, size_t namelen, const uint8_t* value, size_t valuelen, uint8_t flags, void* user_data) {
		std::string_view nm{(const char*)name, namelen};
		std::string_view val{(const char*)value, valuelen};
		gapr::print(1, "on_header ", nm, ": ", val, "...", nm.size(), '+', val.size());
		auto h2=(impl_h2*)user_data;
		if(nm=="tag") {
			h2->cur_header.append(val);
			h2->tag_size=val.size();
		} else if(nm=="args") {
			h2->cur_header.push_back(' ');
			h2->cur_header.append(val);
		} else if(nm=="body") {
			h2->chkptr=value;
			h2->chklen=valuelen;
			h2->frm_in|=FrameChunk;
			h2->frm_in|=LastChunk;
		}
		return 0;
	}
	static int on_invalid_header(nghttp2_session* session, const nghttp2_frame* frame, const uint8_t* name, size_t namelen, const uint8_t* value, size_t valuelen, uint8_t flags, void* user_data) {
		gapr::print(1, "on_invalid_header ", std::string_view{(const char*)name, namelen}, ": ", std::string_view{(const char*)value, valuelen});
		return 0;
	}
	static int on_invalid_frame_recv(nghttp2_session* session, const nghttp2_frame* frame, int lib_error_code, void* user_data) {
		gapr::print(1, "on_invalid_frame_recv ", nghttp2_strerror(lib_error_code));
		return 0;
	}

	bool settings_sent{false};
	void ensure_settings() {
		if(settings_sent)
			return;
		std::array<nghttp2_settings_entry, 4> settings{{
			{NGHTTP2_SETTINGS_ENABLE_PUSH, 0},
			{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 1},
			{NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 32*1024},
			{NGHTTP2_SETTINGS_MAX_FRAME_SIZE, 32*1024},
		}};
		auto r=nghttp2_submit_settings(sess.get(), NGHTTP2_FLAG_NONE, settings.data(), settings.size());
		if(r!=0)
			throw std::runtime_error{nghttp2_strerror(r)};
		settings_sent;
	}

};

impl::impl(socket&& sock, ssl_context& ssl_ctx):
	_st0{ConnStPreHs}, _st1{ConnStPreHs}, _ops_valid{true},
	_refc{(1<<BIT_COUNT)+(1<<BIT_OPEN)}, _ssl{new ssl_stream{std::move(sock), ssl_ctx}}, _ops{}, _h2{new impl_h2{}}
{
	gapr::print(1, "connection ctor 1");
}
	
impl::impl(std::unique_ptr<ssl_stream> ssl, bool srv):
	_st0{ConnStOk}, _st1{ConnStOk}, _ops_valid{true},
	_refc{(1<<BIT_COUNT)+(1<<BIT_OPEN)}, _ssl{std::move(ssl)}, _ops{}, _h2{new impl_h2{}}
{
	gapr::print(1, "connection ctor 2");
	_is_srv=srv;
	if(_is_srv) {
		_h2->setup_server(this);
	} else {
		_h2->setup_client(this);
	}
	//op->wptr.end();
	_recv_st=0;
	//op->complete(ec);
}
impl::impl(const boost::asio::any_io_executor& ex, ssl_context& ssl_ctx):
	_st0{ConnStPreCon}, _st1{ConnStPreCon}, _ops_valid{true},
	_refc{(1<<BIT_COUNT)}, _ssl{new ssl_stream{std::move(ex), ssl_ctx}}, _ops{}, _h2{new impl_h2{}}
{
	gapr::print(1, "connection ctor 3");
}
impl::~impl() {
	gapr::print(1, "connection dtor");
	if(_ops_valid)
		_ops.~Ops();
	if(_h2)
		delete _h2;
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
	6, 'g', 'a', 'p', 'r', '/', '1'
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
						_h2->setup_client(this);
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

		op->is_res=false;
		_ops.send_que1.push_back(std::move(op));
		if(WeakPtr<false, true, false> wptr{this})
			do_send_impl_que1(std::move(wptr));

		return;
	} while(false);
	assert(0);
	ba::post(sock().get_executor(), [op=std::move(op),ec]() mutable {
		op->complete(ec);
	});
}

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
		op->is_res=true;
		//fprintf(stderr, "DDD %p: ENQ4 %hd:%hd\n", this, op->seq(), op->idx);
		_ops.send_que1.push_back(std::move(op));
		_insts.st1=ReqStOpening;
		if(WeakPtr<false, true, false> wptr{this})
			do_send_impl_que1(std::move(wptr));
		return;
	} while(false);
	assert(0);
	ba::post(sock().get_executor(), [op=std::move(op),ec]() mutable {
		op->complete(ec);
	});
}

struct ReadBuffer {
	const uint8_t* buf;
	std::size_t siz;
	std::size_t pos;
};
ssize_t read_cbuffer_view(nghttp2_session* session, int32_t stream_id, uint8_t* buf, size_t length, uint32_t* data_flags, nghttp2_data_source* source, void* user_data) {
	auto src=(ReadBuffer*)source->ptr;
	auto len=src->siz-src->pos;
	gapr::print(1, "read_buffer ", len, ' ', length);
	if(len>length) {
		len=length;
	} else {
		*data_flags=NGHTTP2_DATA_FLAG_EOF;
	}
	std::copy_n(src->buf+src->pos, len, buf);
	src->pos+=len;
	return len;
}

static std::size_t prepare_nva(std::array<nghttp2_nv, 6>& nva, const auto& hdr, bool is_res) {
	std::size_t i=0;
	constexpr std::string_view tag_name{"tag"};
	constexpr std::string_view arg_name{"args"};
	auto add=[&i,&nva](std::string_view n, std::string_view v) {
		nva[i].name=(uint8_t*)n.data();
		nva[i].namelen=n.size();
		nva[i].value=(uint8_t*)v.data();
		nva[i].valuelen=v.size();
		nva[i].flags=0;
		i++;
	};
	nva[i].name=(uint8_t*)tag_name.data();
	nva[i].namelen=tag_name.size();
	nva[i].value=hdr.data();
	nva[i].valuelen=hdr.tag_size;
	nva[i].flags=0;
	i++;
	nva[i].name=(uint8_t*)arg_name.data();
	nva[i].namelen=arg_name.size();
	if(hdr.tag_size<hdr.hdr_size) {
		nva[i].value=hdr.data()+hdr.tag_size+1;
		nva[i].valuelen=hdr.hdr_size-hdr.tag_size-1;
	} else {
		nva[i].value=nullptr;
		nva[i].valuelen=0;
	}
	nva[i].flags=0;
	i++;
	return i;
}

void impl::do_send_impl_que1(WeakPtr<false, true, false>&& wptr) {
	if(_ops.send_que1.empty()) {
		do {
			auto r=nghttp2_session_send(_h2->sess.get());
			gapr::print(1, "send noiter ", r);
			if(r!=0)
				throw std::runtime_error{nghttp2_strerror(r)};
		} while(false);
		ba::const_buffer buf{_h2->outbuf.data(), _h2->outbuf.size()};
		return ba::async_write(sock(), buf,
				[this,buf,wptr=std::move(wptr)](error_code ec, std::size_t nbytes) mutable {
					if(ec) {
						_st1=ConnStErr;
						return;
					}

					assert(nbytes==buf.size());
					_h2->outbuf.clear();
					wptr.end();
					//do_send_impl_next(std::move(wptr));
				});
	}
	auto& _op=_ops.send_que1.front();
	gapr::print(1, "do_send_impl_que1");
	switch(_op.type) {
		case SendOp::HdrOnly:
			{
				assert(_h2->outbuf.empty());
				auto op=wptr->_ops.send_que1.take_front<SendHdrOp>();
				op->wptr.relay(std::move(wptr));
				std::array<nghttp2_nv, 6> nva;
				auto nvlen=prepare_nva(nva, op->hdr, op->is_res);
				gapr::print(1, "submit_headers ", _h2->cur_str);
				auto r=nghttp2_submit_headers(_h2->sess.get(), NGHTTP2_FLAG_END_STREAM, _h2->cur_str, nullptr, nva.data(), nvlen, nullptr);
				if(r<0)
					throw std::runtime_error{nghttp2_strerror(r)};

				_h2->frm_end=false;
				do {
					r=nghttp2_session_send(_h2->sess.get());
					if(r!=0)
						throw std::runtime_error{nghttp2_strerror(r)};
				} while(!_h2->frm_end);

				ba::const_buffer buf{_h2->outbuf.data(), _h2->outbuf.size()};
				ba::async_write(sock(), buf,
						[op=std::move(op),this,buf](error_code ec, std::size_t nbytes) mutable {
							if(ec) {
								_st1=ConnStErr;
								op->wptr.end();
								return op->complete(ec);
							}
							gapr::print("seq begin: ");
							assert(nbytes==buf.size());
							_h2->outbuf.clear();
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
		case SendOp::StrmHdr:
			{
				assert(_h2->outbuf.empty());
				auto op=wptr->_ops.send_que1.take_front<SendHdrOp>();
				op->wptr.relay(std::move(wptr));
				std::array<nghttp2_nv, 6> nva;
				auto nvlen=prepare_nva(nva, op->hdr, op->is_res);
				gapr::print(1, "submit_headers ", _h2->cur_str);
				auto r=nghttp2_submit_headers(_h2->sess.get(), 0, _h2->cur_str, nullptr, nva.data(), nvlen, nullptr);
				if(r<0)
					throw std::runtime_error{nghttp2_strerror(r)};
				if(_h2->cur_str==-1 && r!=0)
					_h2->cur_str=r;

				_h2->frm_end=false;
				do {
					r=nghttp2_session_send(_h2->sess.get());
					if(r!=0)
						throw std::runtime_error{nghttp2_strerror(r)};
				} while(!_h2->frm_end);

				ba::const_buffer buf{_h2->outbuf.data(), _h2->outbuf.size()};
				ba::async_write(sock(), buf,
						[op=std::move(op),this,buf](error_code ec, std::size_t nbytes) mutable {
							if(ec) {
								_st1=ConnStErr;
								op->wptr.end();
								return op->complete(ec);
							}
							gapr::print("seq begin: ");
							assert(nbytes==buf.size());
							_h2->outbuf.clear();
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
				assert(_h2->outbuf.empty());
				auto op=wptr->_ops.send_que1.take_front<SendHdrOp>();
				op->wptr.relay(std::move(wptr));
				std::array<nghttp2_nv, 6> nva;
				auto nvlen=prepare_nva(nva, op->hdr, op->is_res);
				uint8_t flags=0;
				if(op->buf.size()<512) {
					constexpr std::string_view body_name{"body"};
					nva[nvlen].name=(uint8_t*)body_name.data();
					nva[nvlen].namelen=body_name.size();
					nva[nvlen].value=(uint8_t*)op->buf.data();
					nva[nvlen].valuelen=op->buf.size();
					nva[nvlen].flags=0;
					nvlen++;
					flags|=NGHTTP2_FLAG_END_STREAM;
				}
				gapr::print(1, "submit_headers ", _h2->cur_str);
				auto r=nghttp2_submit_headers(_h2->sess.get(), flags, _h2->cur_str, nullptr, nva.data(), nvlen, nullptr);
				if(r!=0)
					throw std::runtime_error{nghttp2_strerror(r)};
				if(_h2->cur_str==-1 && r!=0)
					_h2->cur_str=r;

				if(!(flags&NGHTTP2_FLAG_END_STREAM)) {
					ReadBuffer data_buf{(const uint8_t*)op->buf.data(), op->buf.size(), 0};
					nghttp2_data_provider data;
					data.source.ptr=&data_buf;
					data.read_callback=read_cbuffer_view;
					gapr::print(1, "submit_data_one ", _h2->cur_str);
					r=nghttp2_submit_data(_h2->sess.get(), NGHTTP2_FLAG_END_STREAM, _h2->cur_str, &data);
					if(r!=0)
						throw std::runtime_error{nghttp2_strerror(r)};
				}

				_h2->frm_end=false;
				do {
					r=nghttp2_session_send(_h2->sess.get());
					if(r!=0)
						throw std::runtime_error{nghttp2_strerror(r)};
				} while(!_h2->frm_end);
				ba::const_buffer buf{_h2->outbuf.data(), _h2->outbuf.size()};

				return ba::async_write(sock(), buf,
						[op=std::move(op),this](error_code ec, std::size_t nbytes) mutable {
							if(ec) {
								_st1=ConnStErr;
								op->wptr.end();
								return op->complete(ec);
							}
							gapr::print("seq begin: ");
							_h2->outbuf.clear();
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
				if(auto w=nghttp2_session_get_stream_remote_window_size(_h2->sess.get(), _h2->cur_str); w<MAX_CHUNK) {
					gapr::print(1, "end send: window size: ", w);
					if(gapr::connection::impl::WeakPtr<true, false, false> wptr2{this})
						do_recv_impl(std::move(wptr2), true);
					wptr.end();
					return;
				}
				assert(_h2->outbuf.empty());
				auto op=wptr->_ops.send_que1.take_front<SendChkOp>();
				op->wptr.relay(std::move(wptr));

				std::size_t towrite;
				uint8_t flags=0;
				if(op->buf.size()<=MAX_CHUNK) {
					if(op->eof)
						flags|=NGHTTP2_FLAG_END_STREAM;
					towrite=op->buf.size();
				} else {
					towrite=MAX_CHUNK;
				}
				
				ReadBuffer data_buf{(const uint8_t*)op->buf.data(), towrite, 0};
				nghttp2_data_provider data;
				data.source.ptr=&data_buf;
				data.read_callback=read_cbuffer_view;
				gapr::print(1, "submit_data ", _h2->cur_str, " ", (int)flags, " ", towrite);
				auto r=nghttp2_submit_data(_h2->sess.get(), flags, _h2->cur_str, &data);
				if(r!=0)
					throw std::runtime_error{nghttp2_strerror(r)};

				_h2->frm_end=false;
				int iter=0;
				do {
					r=nghttp2_session_send(_h2->sess.get());
					gapr::print(1, "send iter ", r);
					if(r!=0)
						throw std::runtime_error{nghttp2_strerror(r)};
				} while(!_h2->frm_end && iter++<10);
				ba::const_buffer buf{_h2->outbuf.data(), _h2->outbuf.size()};
				return ba::async_write(sock(), buf,
						[op=std::move(op),this,towrite,buf](error_code ec, std::size_t nbytes) mutable {
							if(ec) {
								gapr::print(1, "send error ", ec.message());
								_st1=ConnStErr;
								op->wptr.end();
								return op->complete(ec);
							}

							assert(nbytes==buf.size());
							_h2->outbuf.clear();
							op->nwrite+=towrite;
							op->buf.skip(towrite);
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

	gapr::print(1, "!end send");
	wptr.end();
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

static inline bool check_st_write(int st, bs::error_code& ec) noexcept {
	assert(st==ConnStOk);
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
		_ops.send_que1.push_back(std::move(op));
		if(WeakPtr<false, true, false> wptr{this})
			do_send_impl_que1(std::move(wptr));
		return;
	} while(false);
	assert(0);
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
	gapr::print("!do_recv_impl ", _h2->frm_in);
	auto wptr=std::move(_wptr); // so that wptr.~dtor() call here
	bool check_send{false};
	do {
		while(_h2->frm_in==ZeroInput) {
			if(_h2->inbuf.size()<=_h2->inbufpos && !_h2->inbufec) {
				_h2->inbuf.resize(65536);
				gapr::print("!do_recv_impl async_read_some");
				sock().async_read_some(ba::mutable_buffer{_h2->inbuf.data(), _h2->inbuf.size()}, [this,wptr=std::move(wptr)](bs::error_code ec, std::size_t nbytes) mutable {
					gapr::print("!do_recv_impl async_readsome res: ", ec.message());
					_h2->inbufec=ec;
					if(ec) {
						_h2->inbuf.resize(0);
						_h2->inbufpos=0;
					} else {
						assert(nbytes<=_h2->inbuf.size());
						_h2->inbuf.resize(nbytes);
						_h2->inbufpos=0;
					}
					return do_recv_impl(std::move(wptr), false);
				});
				return;
			}
			auto r=nghttp2_session_recv(_h2->sess.get());
			gapr::print(1, "recv iter ", r);
			if(r!=0) {
				if(r==NGHTTP2_ERR_EOF) {
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
						op->complete(_h2->inbufec);
					}
					break;
				}
					//
				if(auto op=std::move(_ops.recv)) {
					op->complete(_h2->inbufec);
					//wptr.end();
					//return;
				}
				if(auto op=std::move(_ops.recv_reply)) {
					//_insts[_recv_idx-1].st0=ReqStInit;
					op->complete(_h2->inbufec);
				}
				if(auto op=std::move(_ops.read)) {
					op->complete(_h2->inbufec);
				}
				break;
				//throw std::runtime_error{nghttp2_strerror(r)};
			}
		}

		if(_h2->frm_in&WindowUpdate) {
			_h2->frm_in&=~WindowUpdate;
			check_send=true;
		}

		if(_h2->frm_in&HeaderFrame) {
			bool eof=(_h2->frm_in&HeaderOnly)&&_h2->chklen==0;
			_h2->frm_in&=~(HeaderFrame|HeaderOnly);
			_h2->cur_header.push_back('\x00');
			if(!_recv_hdr.parse(&*_h2->cur_header.begin(), &*_h2->cur_header.end()-1))
				throw std::runtime_error{"fail to parse"};
			gapr::print(1, "args len", _recv_hdr.len);
			_recv_misc.var=0;
			_recv_misc.siz=0;
			_recv_misc.sizhint=0;
			_recv_misc.typ=eof?gapr::server_end::msg_type::hdr_only:gapr::server_end::msg_type::stream;
			_recv_has_body=!eof;

			//_recv_chk_is_eof=

			if(auto op=do_recv_impl_get_op()) {
				if(in_api) {
					return post(sock().get_executor(), [op=std::move(op),this,wptr=std::move(wptr)]() mutable {
						gapr::print("got msg post: ", _recv_hdr.ptr);
						op->complete({}, reinterpret_cast<msg_hdr_in&>(_recv_hdr));
						_recv_st=_recv_st_next;
						do_recv_impl(std::move(wptr), false);
					});
				} else {
					gapr::print("got msg { ", _recv_hdr.ptr);
					gapr::print(1, "args len", _recv_hdr.len);
					op->complete({}, reinterpret_cast<msg_hdr_in&>(_recv_hdr));
					gapr::print("got msg } ");
					_recv_st=_recv_st_next;
				}
			} else {
				gapr::print("pending msg");
				break;
			}
		}
		if(_h2->frm_in&FrameChunk) {
			_recv_chk_is_eof=(_h2->frm_in&LastChunk);
			auto op=_ops.read.get();
			if(!op) {
				gapr::print("delayed body");
				break;
			}
			auto toread=_h2->chklen;
			if(toread>op->buf.size())
				toread=op->buf.size();
			std::copy_n(_h2->chkptr, toread, op->buf.data());
			_h2->chkptr+=toread;
			_h2->chklen-=toread;
			op->nread+=toread;
			op->buf.skip(toread);
			if(_h2->chklen<=0) {
				_h2->frm_in&=~(FrameChunk|LastChunk);
				if(impl::WeakPtr<false, true, false> wptr2{this})
					do_send_impl_que1(std::move(wptr2));
			}
			if(_h2->chklen<=0 && _ops.read->buf.size()>0 && !_recv_chk_is_eof) {
				continue;
			}

			{
				auto op=std::move(_ops.read);
				bool is_eof=_h2->chklen<=0&&_recv_chk_is_eof;
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
						op->complete(is_eof?ba::error::eof:bs::error_code{});
						if(_h2->frm_in || _ops.recv || _recv_ops_reply_n>0 || _read_ops_n>0)
							do_recv_impl(std::move(wptr), false);
					});
				} else {
					op->complete(is_eof?ba::error::eof:bs::error_code{});
					if(_h2->frm_in)
						continue;
				}
			}
		}
	} while(_ops.recv || _recv_ops_reply_n>0 || _read_ops_n>0);
	gapr::print("!receving");
	wptr.end();
	if(check_send)
		if(impl::WeakPtr<false, true, false> wptr2{this})
			do_send_impl_que1(std::move(wptr2));

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
	gapr::print(1, "parse res: ", tlen, '+', aoff, '+', len);
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

impl::SendHdrOp::SendHdrOp(MsgHdr&& _hdr, std::nullptr_t) noexcept:
SendHdrOp_{HdrOnly}, hdr{std::move(_hdr)}, hdr_len{hdr.size()}, buf{} {
}
impl::SendHdrOp::SendHdrOp(MsgHdr&& _hdr, uint16_t var, cbuffer_view buf) noexcept:
SendHdrOp_{HdrBody}, hdr{std::move(_hdr)}, hdr_len{hdr.size()}, buf{buf} {
}
impl::SendHdrOp::SendHdrOp(MsgHdr&& _hdr, uint16_t var, uint64_t szhint) noexcept:
SendHdrOp_{StrmHdr}, hdr{std::move(_hdr)}, hdr_len{hdr.size()}, buf{} {
}
bool gapr::connection::msg_hdr_in::tag_is(const char* tag) const noexcept {
	std::size_t i;
	for(i=0; i<_b.tlen; i++) {
		if(_b.ptr[i]!=tag[i]) return false;
	}
	return tag[i]=='\x00';
}

#include "detail/connection.unit-test.hh"
#include "gapr/detail/template-class-static-data.hh"

void* template_class_static_data_2() {
	return &gapr::detail::template_class<int>::data;
}
