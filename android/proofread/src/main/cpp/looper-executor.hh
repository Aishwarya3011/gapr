#ifndef _GAPR_LOOPER_CONTEXT_HH_
#define _GAPR_LOOPER_CONTEXT_HH_

#include <mutex>

#include <boost/asio/execution_context.hpp>
#include <gapr/detail/scoped-fence.hh>

class LooperContext: public boost::asio::execution_context {
	public:
		template<bool block=true, bool defer=true, bool track=false>
				class executor_base;
		using executor_type=executor_base<>;

		explicit LooperContext(ALooper* looper):
			_looper{looper}, _thread_id{std::this_thread::get_id()}
		{
			ALooper_acquire(_looper);
			auto r=pipe(_fds);
			if(r==-1)
				throw std::system_error{std::error_code{errno, std::system_category()}};
			r=ALooper_addFd(looper, _fds[0], ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, &callback, this);
			if(r==-1)
				throw;
		}
		~LooperContext() {
			//Destroys all unexecuted function objects that were submitted via an executor object that is associated with the execution context.

			auto r=ALooper_removeFd(_looper, _fds[0]);
			if(r==-1)
				std::terminate(); //term
			r=close(_fds[1]);
			if(r==-1)
				std::terminate();
			r=close(_fds[0]);
			if(r==-1)
				std::terminate();
			ALooper_release(_looper);
		}
		LooperContext(const LooperContext&) =delete;
		LooperContext& operator=(const LooperContext&) =delete;

		executor_type get_executor() noexcept;

	private:
		ALooper* _looper;
		std::thread::id _thread_id;
		std::mutex _mtx;
		struct Callback: gapr::cb_wrapper::add<void()> {
			Callback* next{nullptr};
			void complete() { return cb_wrapper_call(); }
		};
		Callback* _list_first{nullptr};
		Callback* _list_last{nullptr};
		std::atomic<int> _refc{0};
		int _fds[2];


		int callback_(int fd, int events) {
			char x;
			auto r=read(fd, &x, 1);
			if(r==-1)
				throw std::system_error{std::error_code{errno, std::system_category()}};
			Callback* maybe_next;
			do {
				std::unique_ptr<Callback> cb;
				{
					std::lock_guard lck{_mtx};
					if(!_list_first)
						break;
					cb.reset(_list_first);
					maybe_next=_list_first=cb->next;
				}
				cb->complete();
			} while(maybe_next);
			return 1;
		}
		static int callback(int fd, int events, void* data) {
			return static_cast<LooperContext*>(data)->callback_(fd, events);
		}
		void post(std::unique_ptr<Callback> cb) {
			{
				std::lock_guard lck{_mtx};
				if(_list_first) {
					auto p=cb.release();
					_list_last->next=p;
					_list_last=p;
				} else {
					_list_first=_list_last=cb.release();
				}
			}
			auto r=write(_fds[1], "x", 1);
			if(r==-1)
				throw std::system_error{std::error_code{errno, std::system_category()}};
		}
		void start_work() noexcept {
			_refc.fetch_add(1);
		}
		void finish_work() noexcept {
			_refc.fetch_sub(1);
		}
		void handle_exception() {
			// XXX
		}
		template<typename Func, typename Alloc>
		void post(Func&& func, const Alloc& alloc) {
			// XXX use alloc
			auto f1=gapr::cb_wrapper::make_unique<Callback>(std::forward<Func>(func));
			post(std::move(f1));
		}
};

template<bool block, bool defer, bool track>
class LooperContext::executor_base {
public:
	executor_base(LooperContext& ctx) noexcept: _ctx{&ctx} {
		if(track)
			_ctx->start_work();
	}
	~executor_base() {
		if(track && _ctx)
			_ctx->finish_work();
	}
	executor_base(const executor_base& r) noexcept: _ctx{r._ctx} {
		if(track && _ctx)
			_ctx->start_work();
	}
	executor_base& operator=(const executor_base& r) noexcept {
		auto prev_ctx=_ctx;
		_ctx=r._ctx;
		if(track) {
			if(_ctx)
				_ctx->start_work();
			if(prev_ctx)
				prev_ctx->finish_work();
		}
		return *this;
	}
	executor_base(executor_base&& r) noexcept: _ctx{r._ctx} {
		if(track)
			r._ctx=nullptr;
	}
	executor_base& operator=(executor_base&& r) noexcept {
		auto prev_ctx=_ctx;
		_ctx=r._ctx;
		if(track) {
			r._ctx=nullptr;
			if(prev_ctx)
				prev_ctx->finish_work();
		}
		return *this;
	}

	bool operator==(const executor_base& r) const noexcept {
		return _ctx==r._ctx;
	}
	bool operator!=(const executor_base& r) const noexcept {
		return !(*this==r);
	}
	void swap(executor_base& r) noexcept {
		std::swap(_ctx, r._ctx);
	}

	bool running_in_this_thread() const noexcept {
		auto a=(std::this_thread::get_id()==_ctx->_thread_id);
		assert(a==(ALooper_forThread()==_ctx->_looper));
		return a;
	}

	template<typename F>
	void execute(F&& f) const {
		using FF=std::decay_t<F>;
		if(block && running_in_this_thread()) {
			FF ff{std::forward<F>(f)};
			try {
				gapr::scoped_fence<std::memory_order_release> fence{std::memory_order_acquire};
				ff();
				return;
			} catch(...) {
				_ctx->handle_exception();
				return;
			}
		}
		return _ctx->post(std::forward<F>(f), std::allocator<int>{});
	}
	LooperContext& query(boost::asio::execution::context_t) const noexcept {
		return *_ctx;
	}
	constexpr executor_base<false, defer, track> require(boost::asio::execution::blocking_t::never_t) const {
		return {*_ctx};
	}

	constexpr executor_base<true, defer, track> require(boost::asio::execution::blocking_t::possibly_t) const {
		return {*_ctx};
	}
	constexpr executor_base<block, false, track> require(boost::asio::execution::relationship_t::fork_t) const {
		return {*_ctx};
	}
	constexpr executor_base<block, true, track> require(boost::asio::execution::relationship_t::continuation_t) const {
		return {*_ctx};
	}
	constexpr executor_base<block, defer, false> require(boost::asio::execution::outstanding_work_t::untracked_t) const {
		return {*_ctx};
	}
	constexpr executor_base<block, defer, true> require(boost::asio::execution::outstanding_work_t::tracked_t) const {
		return {*_ctx};
	}
#if 0
	static constexpr boost::asio::execution::blocking_t::never_t query(
			boost::asio::execution::blocking_t) noexcept
	{
		return boost::asio::execution::blocking.never;
	}
#endif

private:
	LooperContext* _ctx;
};
inline LooperContext::executor_type LooperContext::get_executor() noexcept {
	return executor_type{*this};
}

#endif
