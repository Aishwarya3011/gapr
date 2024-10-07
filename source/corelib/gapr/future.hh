/* gapr/future.hh
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
#ifndef _GAPR_INCLUDE_FUTURE_HH_
#define _GAPR_INCLUDE_FUTURE_HH_


#include "gapr/likely.hh"
#include "gapr/detail/executor-lock.hh"
//#include "gapr/detail/cb-wrapper.hh"

#include <atomic>
#include <memory>

#include <boost/asio/post.hpp>

namespace gapr {

	class broken_promise: public std::exception {
		public:
			explicit broken_promise() noexcept: std::exception{} { }
			broken_promise(const broken_promise&) noexcept =default;
			broken_promise& operator=(const broken_promise&) noexcept =default;
			~broken_promise() override { }
			const char* what() const noexcept override {
				return "broken promise";
			}
		private:
	};
	class already_retrieved: public std::exception {
		public:
			explicit already_retrieved() noexcept: std::exception{} { }
			already_retrieved(const already_retrieved&) noexcept =default;
			already_retrieved& operator=(const already_retrieved&) noexcept =default;
			~already_retrieved() override { }
			const char* what() const noexcept override {
				return "already retrieved";
			}
		private:
	};

	class future_PRIV {

		struct trigger_op_base;
		struct state_base;
		using trigger_fn=void(state_base* st);
		constexpr static std::size_t RESERVE_SIZ=48+32;

		enum STATES: unsigned int {
			REF_PROMISE=1,
			REF_FUTURE=2,
			REF_CANCEL=4,
			REF_PRIV=8,

			HAS_DATA=1,
			HAS_WAIT=2,
			HAS_ALLOC=4,
			HAS_CANCEL=8
		};

		struct trigger_op_base {
			trigger_fn* _fn;
			trigger_op_base(trigger_fn* fn) noexcept: _fn{fn} { }
			virtual ~trigger_op_base() =default;
			trigger_op_base(const trigger_op_base&) =delete;
			trigger_op_base& operator=(const trigger_op_base&) =delete;
		};
		struct state_base {
			std::atomic<unsigned int> _refc;
			std::atomic<unsigned int> _st;
			std::unique_ptr<trigger_op_base> _op;
			char* _alloc_buf[(RESERVE_SIZ+sizeof(char*)-1)/sizeof(char*)];
			explicit state_base() noexcept:
				_refc{REF_PROMISE}, _st{0}, _op{nullptr} { }
			virtual ~state_base() =default;
			state_base(const state_base&) =delete;
			state_base& operator=(const state_base&) =delete;

			template<STATES St>
				bool upref() noexcept { return _refc.fetch_or(St)&St; }
			void trigger() noexcept {
				auto r=_st.fetch_or(HAS_DATA);
				if(!(r&HAS_WAIT))
					return;
				assert(_op);
				(*_op->_fn)(this);
			}
		};

		template<STATES St>
			static void unref(state_base* p) noexcept {
				auto r=p->_refc.fetch_and(~St);
				//fprintf(stderr, "refc: %x %x\n", r, ~St);
				if(r==St)
					delete p;
			}

		struct Ptr {
			state_base* _p;
			explicit Ptr(state_base* p) noexcept: _p{p} { }
			~Ptr() {
				if(_p)
					unref<REF_PRIV>(_p);
			}
			Ptr(const Ptr&) =delete;
			Ptr& operator=(const Ptr&) =delete;
			Ptr(Ptr&& r) noexcept: _p{r._p} { r._p=nullptr; }

			template<typename State>
				State* get() const noexcept {
					return static_cast<State*>(_p);
				}
		};

		template<typename T> struct allocator {
			state_base& obj;
			using value_type=T;
			explicit allocator(state_base& obj) noexcept: obj{obj} { }
			template<typename U>
				allocator(const allocator<U>& r) noexcept: obj{r.obj} { }
			T* allocate(std::size_t n) const noexcept {
				static_assert(sizeof(T)<=RESERVE_SIZ);
				//fprintf(stderr, "alloc: %zu %zu\n", n, sizeof(T));
				assert(n==1);
				auto r=obj._st.fetch_or(HAS_ALLOC);
				assert(!(r&HAS_ALLOC));
				(void)r;
				return reinterpret_cast<T*>(obj._alloc_buf);
			}
			void deallocate(T* ptr, std::size_t n) const noexcept {
				//fprintf(stderr, "dealloc: %zu %zu\n", n, sizeof(T));
				assert(n==1);
				auto r=obj._st.fetch_and(~HAS_ALLOC);
				assert((r&HAS_ALLOC));
				(void)r;
			}
		};

		template<typename T> struct state: state_base {
			union {
				likely<T> _data;
			};
			explicit state() noexcept: state_base{} { }
			~state() override {
				if(_st.load()&HAS_DATA)
					_data.~likely();
			}
			template<typename Executor, typename Handler>
				void do_wait(const Executor& ex, Handler&& cb) {
					auto r=_st.load();
					if(r&HAS_DATA) {
						if(upref<REF_PRIV>())
							std::terminate();
						Ptr st{this};
						boost::asio::post(ex, [cb=std::move(cb),st=std::move(st)]() mutable {
							cb(std::move(st.get<state>()->_data));
						});
					} else {
						_op.reset(new trigger_op<Executor, Handler, T>{ex, std::move(cb)});
						r=_st.fetch_or(HAS_WAIT);
						if(r&HAS_DATA) {
							(*_op->_fn)(this);
						}
					}
				}
			void broken() noexcept {
				new(&_data) likely<T>{std::make_exception_ptr(broken_promise{})};
				trigger();
			}
		};

		template<typename Executor, typename Handler, typename T>
			struct trigger_op: trigger_op_base {
				weak_executor<Executor> _ex;
				Handler _cb;
				trigger_op(const Executor& ex, Handler&& cb):
					trigger_op_base{&trigger_fn}, _ex{ex}, _cb{std::move(cb)} { }
				~trigger_op() override =default;
				static void trigger_fn(state_base* st) noexcept {
					auto op2=static_cast<trigger_op*>(st->_op.release());
					std::unique_ptr<trigger_op> op{op2};
					if(st->upref<REF_PRIV>())
						std::terminate();
					Ptr st1{st};
					allocator<void> alloc{*st};
					auto func=[op=std::move(op), st=std::move(st1)]() {
						op->_cb(std::move(st.get<state<T>>()->_data));
					};
					if(auto lck=op2->_ex.lock()) {
						auto ex=boost::asio::prefer(lck.get_executor(), boost::asio::execution::allocator(alloc));
						boost::asio::post(ex, std::move(func));
					}
				}
			};

		friend class canceler;
		template<typename> friend class promise;
		template<typename> friend class future;
	};

	class canceler {
		public:
			canceler() =delete;
			~canceler() {
				if(_p)
					future_PRIV::unref<future_PRIV::REF_CANCEL>(_p);
			}
			canceler(const canceler&) =delete;
			canceler& operator=(const canceler&) =delete;
			canceler(canceler&&) =delete;
			canceler& operator=(canceler&&) =delete;

			explicit operator bool() const noexcept { return _p; }
			void cancel() const noexcept {
				assert(_p);
				_p->_st.fetch_or(future_PRIV::HAS_CANCEL);
			}

		private:
			future_PRIV::state_base* _p;
			explicit canceler(future_PRIV::state_base* p) noexcept: _p{p} { }
			//XXX request or just cancel it?
	};

	template<typename T> class future {
		public:
			constexpr future() noexcept: _p{nullptr} { }
			~future() { if(_p) future_PRIV::unref<future_PRIV::REF_FUTURE>(_p); }
			future(const future&) =delete;
			future& operator=(const future&) =delete;
			future(future&& r) noexcept: _p{r._p} { r._p=nullptr; }
			future& operator=(future&&) =delete;

			constexpr explicit operator bool() const noexcept { return _p; }
			template<typename Executor, typename Handler>
				void async_wait(const Executor& ex, Handler&& handler) && {
					_p->do_wait(ex, std::move(handler));
					future_PRIV::unref<future_PRIV::REF_FUTURE>(_p);
					_p=nullptr;
				}
		private:
			future_PRIV::state<T>* _p;
			explicit future(future_PRIV::state<T>* p) noexcept: _p{p} { }
			template<typename> friend class promise;
	};

	template<typename T> class promise {
		public:
			explicit promise():
				_p{new future_PRIV::state<T>{}} { }
			~promise() {
				if(_p) {
					_p->broken();
					future_PRIV::unref<future_PRIV::REF_PROMISE>(_p);
				}
			}
			promise(const promise&) =delete;
			promise& operator=(const promise&) =delete;
			promise(promise&& r) noexcept: _p{r._p} { r._p=nullptr; }
#if 0
			promise& operator=(promise&&) =delete;
#endif

			canceler get_canceler() {
				if(_p->template upref<future_PRIV::REF_CANCEL>())
					throw already_retrieved{};
				return canceler{_p};
			}
			future<T> get_future() {
				if(_p->template upref<future_PRIV::REF_FUTURE>())
					throw already_retrieved{};
				return future<T>{_p};
			}

			template<typename... Args>
				void set(Args&&... args) &&
				noexcept(noexcept(likely<T>{std::forward<Args>(args)...})) {
					new(&_p->_data) likely<T>{std::forward<Args>(args)...};
					_p->trigger();
					future_PRIV::unref<future_PRIV::REF_PROMISE>(_p);
					_p=nullptr;
				}
			template<typename E>
				void set_error(E&& e) &&
				noexcept(noexcept(likely<T>{std::forward<E>(e)})) {
					new(&_p->_data) likely<T>{std::forward<E>(e)};
					_p->trigger();
					future_PRIV::unref<future_PRIV::REF_PROMISE>(_p);
					_p=nullptr;
				}

		private:
			future_PRIV::state<T>* _p;
			explicit promise(future_PRIV::state<T>* p) noexcept: _p{p} { }
	};

	template<typename T, typename E>
		void unlikely(promise<T>&& prom, E&& e) {
			return std::move(prom).set_error(std::forward<E>(e));
		}

}

#endif
