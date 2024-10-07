/* gapr/coroutine.hh
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
#ifndef _GAPR_INCLUDE_COROUTINE_HH_
#define _GAPR_INCLUDE_COROUTINE_HH_

#include "gapr/future.hh"
#include "gapr/fix-error-code.hh"

#include <atomic>

#include <boost/context/continuation.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/io_context.hpp>

//XXX async_initiate

namespace gapr {

	template<typename T> class fiber;

	struct fiber_PRIV;

	class fiber_ctx {
		public:
			fiber_ctx() =delete;
			fiber_ctx(const fiber_ctx&) =delete;
			fiber_ctx& operator=(const fiber_ctx&) =delete;
#if 0
			fiber_ctx(fiber_ctx&& r) noexcept: _fiber{std::move(r._fiber)} { }
			fiber_ctx& operator=(fiber_ctx&& r) {
				_fiber=std::move(r._fiber);
				return *this;
			}
#endif

			boost::asio::io_context::executor_type get_executor() const noexcept { return _ex; }

		private:
			boost::asio::io_context::executor_type _ex;
			boost::context::continuation _fiber;
			fiber_ctx(boost::asio::io_context::executor_type ex, boost::context::continuation fib) noexcept:
				_ex{ex}, _fiber{std::move(fib)} { }
			~fiber_ctx() { }
			friend class yield;
			friend struct fiber_PRIV;
	};

	class yield {
		public:
			explicit yield(fiber_ctx& ctx) noexcept:
				_fiber{ctx._fiber}, _ec{nullptr} { }
			yield(fiber_ctx& ctx, std::error_code& ec) noexcept:
				_fiber{ctx._fiber}, _ec{&ec} { }

		private:
			boost::context::continuation& _fiber;
			std::error_code* _ec;
			friend struct fiber_PRIV;
	};

	struct fiber_PRIV {

		struct HelperB;
		struct HelperA;
		GAPR_CORE_DECL static void change_executor(boost::context::continuation& fib, boost::asio::io_context::executor_type ex);

		template<std::size_t... Indexes> struct IdxTup { };
		template<std::size_t I, std::size_t... Indexes>
			constexpr auto static get_idx_tup2(IdxTup<Indexes...>) noexcept {
				return IdxTup<Indexes..., I-1>{};
			}
		template<std::size_t I>
			constexpr auto static get_idx_tup() noexcept {
				if constexpr(I==0) {
					return IdxTup<>{};
				} else {
					return get_idx_tup2<I>(get_idx_tup<I-1>());
				}
			}

		template<typename Ret, typename Fn, typename... Args> struct FnAdaptor {
			gapr::promise<Ret> prom;
			boost::asio::io_context::executor_type ex;
			Fn fn;
			std::tuple<Args...> args;

			template<typename Fn1, typename... Args1>
				FnAdaptor(gapr::promise<Ret>&& prom, boost::asio::io_context::executor_type ex, Fn1&& fn, Args1&&... args):
					prom{std::move(prom)}, ex{ex}, fn{std::forward<Fn1>(fn)}, args(std::forward<Args1>(args)...) { }
			template<std::size_t... Indexes>
				decltype(auto) call_helper(Fn& fn, fiber_ctx& fib, std::tuple<Args...>& tup, IdxTup<Indexes...>) {
					return fn(fib, std::get<Indexes>(std::move(tup))...);
				}
			boost::context::continuation operator()(boost::context::continuation&& fib/*x*/) {
				change_executor(fib, ex);
				fiber_ctx ctx{ex, std::move(fib/*io*/)};
				decltype(auto) r=call_helper(fn, ctx, args, get_idx_tup<sizeof...(Args)>());
				std::move(prom).set(std::forward<decltype(r)>(r));
				return std::move(ctx._fiber);
			}
		};

		template<typename T, typename Fn, typename... Args>
			static gapr::future<T> spawn_helper(boost::asio::io_context::executor_type ex, Fn&& fn, Args&&... args) {
				gapr::promise<T> prom{};
				auto fut=prom.get_future();
				using Ret=std::invoke_result_t<Fn, fiber_ctx&, Args...>;
				using Typ=FnAdaptor<Ret, std::decay_t<Fn>, std::decay_t<Args>...>;
				auto fib_nil=boost::context::callcc(Typ{std::move(prom), ex, std::forward<Fn>(fn), std::forward<Args>(args)...});
				assert(!fib_nil);
				return fut;
			}

		struct state {
			std::atomic<unsigned int> _ready{0};
			gapr::error_mix _ec{std::error_code{}};
			boost::context::continuation _fib1;
			void pause(boost::context::continuation& fib0) {
				auto r=_ready.fetch_add(1);
				if(r==0)
					fib0=std::move(fib0).resume_with([this](boost::context::continuation&& fib1) {
						_fib1=std::move(fib1);
						_ready.fetch_add(1);
						return std::move(fib1);
					});
			}
			void resume() {
				auto r=_ready.fetch_add(1);
				if(r==0)
					return;
				while(r!=2)
					r=_ready.load();
				std::move(_fib1).resume();
			}
		};

		struct handler_void {
			boost::context::continuation& _fib0;
			std::error_code* _out_ec;
			state* _state;

			explicit handler_void(yield yield_) noexcept:
				_fib0{yield_._fiber}, _out_ec{yield_._ec}, _state{nullptr} { }
			~handler_void() {
				// XXX
			}

			void operator()() {
				_state->_ec=std::error_code{};
				_state->resume();
			}
			void operator()(boost::system::error_code ec) {
				_state->_ec=to_std_error_code(ec);
				_state->resume();
			}
		};

		template<typename T> struct handler: handler_void {
			T* _val;

			explicit handler(yield yield_) noexcept:
				handler_void{yield_}, _val{nullptr} { }
			~handler() {
				// XXX
			}

			template<typename T1>
				void operator()(gapr::likely<T1>&& val) {
					if(!val) {
						_state->_ec=std::move(val).error();
					} else {
						_state->_ec=std::error_code{};
						if constexpr(std::is_reference_v<T1>) {
							*_val=val.get();
						} else {
							*_val=std::move(val.get());
						}
					}
					_state->resume();
				}
			void operator()(T val) {
				_state->_ec=std::error_code{};
				*_val=std::move(val);
				_state->resume();
			}
			void operator()(std::error_code ec, T val) {
				_state->_ec=ec;
				*_val=std::move(val);
				_state->resume();
			}
			void operator()(boost::system::error_code ec, T val) {
				std::error_code ec2{ec};
				return (*this)(ec2, std::move(val));
			}
		};

		template<typename T> class result;
		class result_void {
			public:
				using completion_handler_type=handler_void;
				using return_type=void;

				explicit result_void(handler_void& h) noexcept:
					_fib0{h._fib0}, _out_ec{h._out_ec}, _state{} {
						h._state=&_state;
					}
				return_type get() {
					_state.pause(_fib0);
					if(_out_ec) {
						*_out_ec=_state._ec.code();
					} else {
						if(_state._ec)
							throw std::system_error{_state._ec.code()};
					}
					return;
				}
			private:
				boost::context::continuation& _fib0;
				std::error_code* _out_ec;
				state _state;

				template<typename T> friend class result;
		};

		template<typename T>
			class result {
				public:
					using completion_handler_type=handler<T>;
					using return_type=T;
					explicit result(handler<T>& h) noexcept:
						_fib0{h._fib0}, _out_ec{h._out_ec}, _state{} {
							h._state=&_state;
							h._val=&_val;
						}
					return_type get() {
						_state.pause(_fib0);
						if(_out_ec) {
							*_out_ec=_state._ec.code();
						} else {
							if(_state._ec)
								throw std::system_error{_state._ec.code()};
						}
						return std::move(_val);
					}

				private:
					boost::context::continuation& _fib0;
					std::error_code* _out_ec;
					state _state;
					T _val;
			};
		template<typename T>
			struct reference_wrapper {
				T* ref;
				reference_wrapper(): ref{nullptr} { }
				template<typename U>
					reference_wrapper(U&& v) {
						ref=&v;
					}
			};

	};

	template<typename T> class fiber {
		public:
			template<typename Fn, typename... Args>
				fiber(boost::asio::io_context::executor_type ex, Fn&& fn, Args&&... args):
					_ex{ex}, _fut{fiber_PRIV::spawn_helper<T>(ex, std::forward<Fn>(fn), std::forward<Args>(args)...)} { }
			~fiber() { }
			fiber(const fiber&) =delete;
			fiber& operator=(const fiber&) =delete;

			boost::asio::io_context::executor_type get_executor() const noexcept { return _ex; }

			template<typename Cb>
				decltype(auto) async_wait(Cb&& cb) && {
					boost::asio::async_completion<Cb, void(gapr::likely<T>&&)> init(cb);
					std::move(_fut).async_wait(_ex, init.completion_handler);
					if constexpr(std::is_same_v<std::decay_t<Cb>, gapr::yield>) {
						if constexpr(std::is_reference_v<T>) {
							return std::forward<T>(*init.result.get().ref);
						} else {
							return init.result.get();
						}
					} else {
						return init.result.get();
					}
				}

			fiber(boost::asio::io_context::executor_type ex, gapr::future<T> fut) noexcept:
				_ex{ex}, _fut{std::move(fut)} { }
			future<T> get_future() noexcept {
				return std::move(_fut);
			}
		private:
			boost::asio::io_context::executor_type _ex;
			gapr::future<T> _fut;
	};
	template<typename Fn, typename... Args>
		fiber(boost::asio::io_context::executor_type, Fn&&, Args&&...) -> fiber<std::invoke_result_t<Fn, fiber_ctx&, Args...>>;
	template<typename T>
	fiber(boost::asio::io_context::executor_type, gapr::future<T> fut) -> fiber<T>;

#if 0
	// XXX
	template<typename F, typename CompletionToken>
		DEDUCED co_spawn(boost::asio::io_context::executor_type ex, F && f, CompletionToken && token);
		 void f(yield<boost::asio::io_context::executor_type>&);
#endif

}

namespace boost::asio {
	template<typename Sig>
		class async_result<gapr::yield, Sig> {
			//static_assert(0);
		};
	template<>
		class async_result<gapr::yield, void(boost::system::error_code)>:
		public gapr::fiber_PRIV::result_void {
			public:
				explicit async_result(completion_handler_type& h):
					gapr::fiber_PRIV::result_void{h} { }
		};
	template<>
		class async_result<gapr::yield, void()>:
		public gapr::fiber_PRIV::result_void {
			public:
				explicit async_result(completion_handler_type& h):
					gapr::fiber_PRIV::result_void{h} { }
		};
	template<typename T>
		class async_result<gapr::yield, void(boost::system::error_code, T)>:
		public gapr::fiber_PRIV::result<std::decay_t<T>> {
			public:
				// XXX
				explicit async_result(gapr::fiber_PRIV::handler<std::decay_t<T>>& h):
					gapr::fiber_PRIV::result<std::decay_t<T>>{h} { }
		};
	template<typename T>
		class async_result<gapr::yield, void(std::error_code, T)>:
		public gapr::fiber_PRIV::result<std::decay_t<T>> {
			public:
				// XXX
				explicit async_result(gapr::fiber_PRIV::handler<std::decay_t<T>>& h):
					gapr::fiber_PRIV::result<std::decay_t<T>>{h} { }
		};
	template<typename T>
		class async_result<gapr::yield, void(T)>:
		public gapr::fiber_PRIV::result<std::decay_t<T>> {
			public:
				explicit async_result(gapr::fiber_PRIV::handler<std::decay_t<T>>& h):
					gapr::fiber_PRIV::result<std::decay_t<T>>{h} { }
		};

	template<typename T>
		class async_result<gapr::yield, void(gapr::likely<T>&&)>:
		public gapr::fiber_PRIV::result<T> {
			public:
				explicit async_result(gapr::fiber_PRIV::handler<T>& h):
					gapr::fiber_PRIV::result<std::decay_t<T>>{h} { }
		};
	template<typename T>
		class async_result<gapr::yield, void(gapr::likely<T&>&&)>:
		public gapr::fiber_PRIV::result<gapr::fiber_PRIV::reference_wrapper<T>> {
			public:
				explicit async_result(gapr::fiber_PRIV::handler<gapr::fiber_PRIV::reference_wrapper<T>>& h):
					gapr::fiber_PRIV::result<gapr::fiber_PRIV::reference_wrapper<T>>{h} { }
		};
	template<typename T>
		class async_result<gapr::yield, void(gapr::likely<T&&>&&)>:
		public gapr::fiber_PRIV::result<gapr::fiber_PRIV::reference_wrapper<T>> {
			public:
				explicit async_result(gapr::fiber_PRIV::handler<gapr::fiber_PRIV::reference_wrapper<T>>& h):
					gapr::fiber_PRIV::result<gapr::fiber_PRIV::reference_wrapper<T>>{h} { }
		};
}


#endif
