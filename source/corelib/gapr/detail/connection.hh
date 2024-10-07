/* detail/connection.hh
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


#ifndef _GAPR_INCLUDE_DETAIL_CONNECTION_HH_
#define _GAPR_INCLUDE_DETAIL_CONNECTION_HH_

// XXX
#include "gapr/utility.hh"

#include <boost/asio/steady_timer.hpp>

struct gapr::connection::impl {
	/* constants */
	static constexpr std::size_t MAX_HEADER=512;
	static constexpr std::size_t ALLOC_HEADER=4*512;
	static constexpr std::size_t MAX_CHUNK=16*1024;

	/* aliases */
	using ssl_stream=boost::asio::ssl::stream<socket>;
	using steady_timer=boost::asio::steady_timer;

	/* helper types */
	struct hdr_info_base {
		const char* ptr;
		std::size_t len, tlen, aoff;
		bool parse(const void* ptr, const void* eptr) noexcept;
	};
	enum TIMER_BITS: int {
		//////////
	};
	enum REFC_BITS: int {
		BIT_TIMER_INVALID=0,
		BIT_TIMER_WAITING,
		BIT_TIMER_CLOSE,

		BIT_LOCK,
		BIT_OPEN,
		BIT_TIMER, // weak ref
		BIT_WRITE, // weak ref
		BIT_READ, // weak ref
		BIT_COUNT, // ref
		///// // //
	};
	/////
	//

	struct MsgHdr;
	template<typename T> GAPR_CORE_DECL static void do_format_int(MsgHdr& hdr, T v) noexcept;
	struct MsgHdr {
		using LineBuf=std::array<unsigned char, ALLOC_HEADER>;
		static_assert(2*MAX_HEADER<=std::tuple_size<LineBuf>::value);
		std::unique_ptr<LineBuf> buf;
		std::size_t tag_size;
		std::size_t hdr_size;
		std::size_t idx;
		explicit MsgHdr(): buf{std::make_unique<LineBuf>()}, idx{0} { }
		~MsgHdr() { }
		MsgHdr(MsgHdr&& r) noexcept: buf{std::move(r.buf)}, tag_size{r.tag_size}, hdr_size{r.hdr_size}, idx{r.idx} { }
		//MsgHdr& operator=(MsgHdr&& r) noexcept {
			//buf=std::move(r.buf); idx=r.idx;
			//return *this;
		//}
		bool try_push() noexcept {
			if(idx<MAX_HEADER) return true;
			++idx; return false;
		}
		bool full_hdr() const noexcept { return idx>=MAX_HEADER; }
		bool full() const noexcept {
			return idx>=std::tuple_size<LineBuf>::value;
		}
		void push_back(unsigned char c) noexcept { (*buf)[idx++]=c; }
		std::string_view view() const noexcept { return {reinterpret_cast<char*>(data()), idx}; }
		unsigned char* data() const noexcept { return &(*buf)[0]; }
		std::size_t size() const noexcept { return idx; }

		template<typename T, typename=std::enable_if_t<std::is_integral<T>::value>> void do_format(T v) noexcept {
			using T1=std::conditional_t<sizeof(T)<=sizeof(int), int, std::conditional_t<sizeof(T)<=sizeof(int64_t), int64_t, T>>;
			using T2=std::conditional_t<std::is_unsigned<T>::value, std::make_unsigned_t<T1>, T1>;
			return do_format_int<T2>(*this, v);
		}
		GAPR_CORE_DECL void do_format(const char* v) noexcept;
		GAPR_CORE_DECL void do_format(const std::string& v) noexcept;

		void do_format_tag(const char* tag) noexcept
		{ do_format(tag); }
		void do_formats() noexcept { }
		template<typename T, typename... Args>
			void do_formats(T&& a0, Args&&... args) noexcept {
				if(try_push())
					push_back(':');
				do_format(std::forward<T>(a0));
				do_formats(std::forward<Args>(args)...);
			}
		bool do_format_args() noexcept { return true; }
		template<typename T, typename... Args>
			void do_format_args(T&& a0, Args&&... args) noexcept {
				if(try_push())
					push_back(' ');
				do_format(std::forward<T>(a0));
				do_formats(std::forward<Args>(args)...);
			}

		template<typename... Args>
			void format_hdr(const char* tag, Args&&... args) noexcept {
				do_format_tag(tag);
				tag_size=idx;
				// XXX no conds
				do_format_args(std::forward<Args>(args)...);
				hdr_size=idx;
				if(try_push())
					push_back('\n');
			}
	};
	struct InstMisc {
		msg_type typ;
		uint16_t var;
		uint16_t siz;
		uint64_t sizhint;
	};
	struct InstInfo {
		InstMisc misc;
		unsigned char st0{0};
		unsigned char st1{0};
	};
	template<bool MORE>
		impl* lock_rwt(int mask) noexcept;
	static impl* lock_ref(impl* ptr) noexcept;
	template<bool R, bool W, bool T> struct WeakPtr {
		static constexpr int mask=(R?(1<<BIT_READ):0)+(W?(1<<BIT_WRITE):0)+(T?(1<<BIT_TIMER):0);
		static constexpr bool more=(R?1:0)+(W?1:0)+(T?1:0)>1;
		static_assert(mask!=0);
		impl* ptr;
		explicit WeakPtr() noexcept: ptr{nullptr} { }
		// XXX
		explicit WeakPtr(impl* ptr) noexcept: ptr{ptr->lock_rwt<more>(mask)} { }
		~WeakPtr() {
			if(ptr) {
				constexpr auto v1=(1<<BIT_TIMER)|(1<<BIT_WRITE)|(1<<BIT_READ);
				auto r=ptr->_refc.fetch_and(~mask);
				if(!((r&(~mask)&v1)||(r>>BIT_COUNT)>0)) {
					//gapr::print("destroyed wptr");
					delete ptr;
				} else {
					auto r=ptr->_ops_valid;
					ptr->_ops_valid=false;
					if(r) {
						//assert(0); XXX can fail
						//throw;
						//gapr::print("del ops");
						ptr->_ops.~Ops();
					}
				}
			////
				//in_this_thread(impl);
			}
			// XXX
#if 0
			if(_ptr) {
				if(R) _ptr->_reading=false;
				if(W) _ptr->_writing=false;
				// break cyc ownership
			}
#endif
		}
		WeakPtr(const WeakPtr&) =delete;
		WeakPtr& operator=(const WeakPtr&) =delete;
		WeakPtr(WeakPtr&& r) noexcept: ptr{r.ptr} { r.ptr=nullptr; }
		WeakPtr& operator=(WeakPtr&&) =delete;
		explicit operator bool() const noexcept { return ptr; }
		impl* operator->() const noexcept { return ptr; }

		bool begin(impl* ptr_) noexcept {
			assert(!ptr);
			return (ptr=ptr_->lock_rwt<more>(mask));
		}
		void relay(WeakPtr&& prev) noexcept {
			assert(!ptr);
			assert(prev.ptr);
			ptr=prev.ptr;
			prev.ptr=nullptr;
		}
		void end() noexcept {
			assert(ptr);
			constexpr auto v1=(1<<BIT_TIMER)|(1<<BIT_WRITE)|(1<<BIT_READ);
			auto r=ptr->_refc.fetch_and(~mask);
				if(!((r&(~mask)&v1)||(r>>BIT_COUNT)>0)) {
					//gapr::print("destroyed wptr");
					delete ptr;
				}
			ptr=nullptr;
		}
	};
	///////////////////////////////////
	struct Ptr {
		impl* ptr;
		explicit Ptr(impl* ptr) noexcept: ptr{ptr} { }
		~Ptr() { if(ptr) ref_dec(ptr); }
		Ptr(const Ptr& r) noexcept: ptr{r.ptr} { try_ref(ptr); }
		Ptr& operator=(const Ptr& r) noexcept {
			Ptr x{ptr}; try_ref(ptr=r.ptr); return *this;
		}
		Ptr(Ptr&& r) noexcept: ptr{r.ptr} { if(ptr) r.release(); }
		Ptr& operator=(Ptr&& r) noexcept {
			auto p=r.ptr; if(p) r.release(); Ptr x{ptr}; ptr=p; return *this;
		}

		template<bool R, bool W, bool T>
			explicit Ptr(const WeakPtr<R, W, T>& r) noexcept:
			ptr{lock_ref(r.ptr)} { }

		explicit operator bool() const noexcept { return ptr; }
		impl* operator->() const noexcept { return ptr; }
		void release() noexcept { assert(ptr); ptr=nullptr; }

		msg_type typ() const noexcept {
			assert(ptr);
			return ptr->_insts.misc.typ;
		}
		uint16_t siz() const noexcept {
			assert(ptr);
			return ptr->_insts.misc.siz;
		}
		uint64_t sizhint() const noexcept {
			assert(ptr);
			return ptr->_insts.misc.sizhint;
		}
	};
	impl* api_lock();
	struct Lock {
		impl* ptr;
		explicit Lock(impl* ptr): ptr{ptr->api_lock()} { }
		~Lock() { ptr->_refc.fetch_and(~(1<<BIT_LOCK)); }
		Lock(const Lock&) =delete;
		Lock& operator=(const Lock&) =delete;
	};
		///
	/* cb wrappers */
	struct ConnOp: cb_wrapper::add<void(const error_code&)> {
		WeakPtr<true, true, true> wptr{};
		void complete(const error_code& ec) { return cb_wrapper_call(ec); }
	};
	struct SrvHsOp: cb_wrapper::add<void(const error_code&)> {
		WeakPtr<true, true, true> wptr{};
		MsgHdr hdr;
		SrvHsOp(MsgHdr&& _hdr) noexcept: hdr{std::move(_hdr)} { }
		void complete(const error_code& ec) { return cb_wrapper_call(ec); }
	};
	const constexpr static hdr_info_base _null_hdr{nullptr, 0, 0, 0};
	struct CliHsOp: cb_wrapper::add<void(const error_code&, const msg_hdr_in&)> {
		WeakPtr<true, true, true> wptr{};
		void complete(const error_code& ec) { return cb_wrapper_call(ec, reinterpret_cast<const msg_hdr_in&>(_null_hdr)); }
		void complete(const error_code& ec, const msg_hdr_in& hdr) { return cb_wrapper_call(ec, hdr); }
	};
	struct ShutdownOp: cb_wrapper::add<void(const error_code&, socket&& sock)> {
		void complete(const error_code& ec, socket&& sock) { return cb_wrapper_call(ec, std::move(sock)); }
	};
	struct RecvHdrOp: cb_wrapper::add<void(const error_code&, const msg_hdr_in&)> {
		void complete(const error_code& ec) { return cb_wrapper_call(ec, reinterpret_cast<const msg_hdr_in&>(_null_hdr)); }
		void complete(const error_code& ec, const msg_hdr_in& hdr) { return cb_wrapper_call(ec, hdr); }
	};
	struct SendOp {
		enum SendOpTypes {
			HdrOnly,
			HdrBody,
			StrmHdr,
			//////////
			CodeOnly,
			Chunk,
		};
		int type;
		SendOp* next;
		SendOp(int type) noexcept: type{type} { }
		virtual ~SendOp() { }
	};
	using SendHdrOp_=cb_wrapper::add<void(const error_code&), SendOp>;
	struct SendHdrOp: SendHdrOp_ {
		WeakPtr<false, true, false> wptr{};
		MsgHdr hdr;
		std::size_t hdr_len;
		std::string_view buf;
		bool is_res;
		//unsigned char type;
		SendHdrOp(MsgHdr&& _hdr, int type) noexcept:
			SendHdrOp_{type}, hdr{std::move(_hdr)}, hdr_len{hdr.size()}, buf{} { }
		SendHdrOp(MsgHdr&& _hdr) noexcept:
			SendHdrOp{std::move(_hdr), HdrOnly} { }
		GAPR_CORE_DECL SendHdrOp(MsgHdr&& _hdr, std::nullptr_t) noexcept;
		GAPR_CORE_DECL SendHdrOp(MsgHdr&& _hdr, uint16_t var, std::string_view buf) noexcept;
		GAPR_CORE_DECL SendHdrOp(MsgHdr&& _hdr, uint16_t var, uint64_t szhint) noexcept;
		void complete(const error_code& ec) { return cb_wrapper_call(ec); }
	};
	struct SendCodeOp: cb_wrapper::add<void(const error_code&), SendOp> {
		//? code;
		//std::array<unsigned char, 5> hdr;
	};
	using SendChkOp_=cb_wrapper::add<void(const error_code&, std::size_t nbytes), SendOp>;
	struct SendChkOp: SendChkOp_ {
		WeakPtr<false, true, false> wptr{};
		std::size_t nwrite;
		std::string_view buf;
		std::array<unsigned char, 5> hdr;
		bool eof;
		void complete(const error_code& ec) { return cb_wrapper_call(ec, nwrite); }
		SendChkOp(std::string_view buf, bool eof):
			SendChkOp_{Chunk}, nwrite{0}, buf{buf}, eof{eof}
		{
		}
	};
		////////
	////////////////////////////////////////////////////////////////////////
	struct IoOp: cb_wrapper::add<void(const error_code&, std::size_t nbytes)> {
	};
	struct ReadStrOp: IoOp {
		std::size_t nread;
		buffer_view buf;
		void complete(const error_code& ec) { return cb_wrapper_call(ec, nread); }
		ReadStrOp(buffer_view buf):
			nread{0}, buf{buf} { }
	};

	/* member data */
	int _st0, _st1;
	bool _ops_valid;//, _timer_valid;
	std::atomic<int> _refc;
	struct SendQue {
		SendOp* head;
		SendOp* tail;
		SendQue() noexcept: head{nullptr} { }
		~SendQue() {
			for(auto p=head; p; ) {
				auto pp=p; p=p->next;
				delete pp;
			}
		}
		bool empty() const noexcept { return !head; }
		SendOp& front() const noexcept { return *head; }
		template<typename T=SendOp>
			std::unique_ptr<T> take_front() noexcept {
				auto h=head;
				head=h->next;
				return std::unique_ptr<T>{static_cast<T*>(h)};
			}
		void push_back(std::unique_ptr<SendOp>&& op) noexcept {
			op->next=nullptr;
			if(head) {
				auto last=tail;
				tail=last->next=op.release();
			} else {
				tail=head=op.release();
			}
		}
	};
	struct Ops { /* ops, be aware of cyclic ownership */
		std::unique_ptr<RecvHdrOp> recv; // notifs, reqs
		std::unique_ptr<RecvHdrOp> recv_reply;
		SendQue send_que0; //hdr(notif) (!!!no code)
		SendQue send_que1; //hdr hdr+buf code+chunk
		SendQue send_que2; //hdr hdr+buf:  pending reqs
		std::unique_ptr<SendHdrOp> send_que3;
		std::unique_ptr<ReadStrOp> read;

		std::unique_ptr<SendHdrOp> delayed_req;
		std::unique_ptr<RecvHdrOp> delayed_recv;
		//send hdr code write
		//
		//recv hdr code read
		//std::deque<std::unique_ptr<SendOp>> _send_ops; // notifs, reqs, replys
		//std::deque<std::unique_ptr<WriteStrOp>> _write_ops;
#if 0
1sN/!conn
1sN/!hands
1?mN?m?notify
1/Ns!read
1sNs!recv
1/Ns!reply
1/N?m?send
1sN/!shutd
1/Ns!write
#endif
	};
	std::shared_ptr<ssl_stream> _ssl;
	union { steady_timer _timer; };
	union { Ops _ops; };
	std::size_t _read_ops_n{0}, _recv_ops_reply_n{0};

	/* read cache */
	std::array<unsigned char, MAX_CHUNK> _recv_buf;
	std::size_t _recv_start;
	std::size_t _recv_off;
	hdr_info_base _recv_hdr;
	InstMisc _recv_misc;
	unsigned char _recv_msg_code;
	// no need** uint16_t _recv_msg_typ;
	// no need** uint64_t _recv_msg_len64;
	uint16_t _recv_chk_left;
	bool _recv_chk_is_eof;
	int _recv_st;
	int _recv_st_next;
	bool _recv_has_body;
	bool _recv_is_notif;
	bool _is_srv;

	/* seqs and insts */
	InstInfo _insts;

	auto& sock() noexcept {
		return *_ssl;
	}
	/// //////////////////////////
	// ////////////////////////////////////////////////

	//static void unref(impl* ptr) noexcept;


	////////// ///// // //////////////////////////////


	/* member funcs */
	GAPR_CORE_DECL impl(std::shared_ptr<ssl_stream> ssl, bool srv=true);
	GAPR_CORE_DECL impl(socket&& sock, ssl_context& ssl_ctx);
	GAPR_CORE_DECL impl(const boost::asio::any_io_executor& ex, ssl_context& ssl_ctx);
	GAPR_CORE_DECL ~impl();
	impl(const impl&) =delete;
	impl& operator=(const impl&) =delete;

	bool is_native() noexcept {
		return true;//_ssl.get_executor().running_in_this_thread();
	}
	//uint16_t do_alloc() noexcept;
	//uint16_t do_alloc_impl() noexcept;
	void do_close() noexcept {
		assert(is_native());
		auto r=_refc.fetch_and(~(1<<BIT_OPEN));
		if(r&(1<<BIT_OPEN))
			do_close_impl();
	}
	void try_close() noexcept {
		auto r=_refc.fetch_and(~(1<<BIT_OPEN));
		if(r&(1<<BIT_OPEN))
			do_close_impl();
	}
	void do_close_impl() noexcept;

	GAPR_CORE_DECL void do_connect_cli(const endpoint& peer, std::unique_ptr<ConnOp>&& op);
	void do_handshake_cli(std::unique_ptr<CliHsOp>&& op);
	void do_handshake_cli_impl(std::unique_ptr<CliHsOp>&& op);
	void do_shutdown_srv(std::unique_ptr<ShutdownOp>&& op);
	void do_shutdown_cli(std::unique_ptr<ShutdownOp>&& op);

	GAPR_CORE_DECL void do_recv_req(std::unique_ptr<RecvHdrOp>&& op);
	void do_recv_reply(std::unique_ptr<RecvHdrOp>&& op);
	void do_send_req(std::unique_ptr<SendHdrOp>&& op);
	GAPR_CORE_DECL void do_send_res(std::unique_ptr<SendHdrOp>&& op);
	void do_send_impl_que1(WeakPtr<false, true, false>&& wptr);
	void do_send_impl_next(WeakPtr<false, true, false>&& wptr);
	void do_send_impl_chk(WeakPtr<false, true, false>&& wptr);
	void do_recv_impl(WeakPtr<true, false, false>&& wptr, bool in_api);
	std::unique_ptr<RecvHdrOp> do_recv_impl_get_op() noexcept;
	GAPR_CORE_DECL void do_read_str(std::unique_ptr<ReadStrOp>&& op);
	GAPR_CORE_DECL void do_write_str(std::unique_ptr<SendChkOp>&& op);

	void start_timer();
	void keep_heartbeat(WeakPtr<false, false, true>&& wptr);
	void heartbeat();
	void heartbeat_impl(WeakPtr<false, true, false>&& wptr);

#if 0

	void do_recv_notif(std::unique_ptr<RecvHdrOp>&& op);


	/////

	//

	bool try_send_next0();
	bool try_send_next1();



#endif
	static void try_ref(impl* ptr) noexcept { if(ptr) ptr->ref_inc(); }
	static void ref_dec(impl* ptr) noexcept {
		auto r=ptr->_refc.fetch_sub(1<<BIT_COUNT);
		//gapr::print("impldec: ", r>>BIT_COUNT);
		assert(r>=(1<<BIT_COUNT));
		if(r<(2<<BIT_COUNT)) {
			constexpr auto v0=1<<BIT_OPEN;
			constexpr auto v1=(1<<BIT_TIMER)|(1<<BIT_WRITE)|(1<<BIT_READ);
			if(r&v1) {
				ptr->try_destroy_timer((r&v0)?(1<<BIT_TIMER_CLOSE):0);
			} else {
				//gapr::print("destroyed");
				delete ptr;
			}
		}
	}
	void try_destroy_timer(int mask) noexcept {
		auto r=_refc.fetch_or((1<<BIT_TIMER_INVALID)|mask);
		if(!(r&(1<<BIT_TIMER_INVALID)) && (r&(1<<BIT_TIMER_WAITING))) {
			destroy_timer();
		}
	}
	void destroy_timer() noexcept {
		//gapr::print("timer destroyed");
		_timer.~steady_timer();
	}
	void ref_inc() noexcept {
		auto r=_refc.fetch_add(1<<BIT_COUNT);
		//gapr::print("implinc: ", r>>BIT_COUNT);
		(void)r;
	}

	struct impl_h2;
	impl_h2* _h2;
};

template<> inline auto gapr::connection::impl::lock_rwt<false>(int mask) noexcept -> impl* {
	return (_refc.fetch_or(mask)&mask)?nullptr:this;
}

inline auto gapr::connection::impl::api_lock() -> impl* {
	constexpr static auto mask=(1<<BIT_LOCK)+(1<<BIT_READ)+(1<<BIT_WRITE)+(1<<BIT_TIMER);
	auto r=_refc.fetch_or(1<<BIT_LOCK);
	if((r&mask) && !true)//_ssl.get_executor().running_in_this_thread())
		throw race_condition{};
	return this;
}

#endif
