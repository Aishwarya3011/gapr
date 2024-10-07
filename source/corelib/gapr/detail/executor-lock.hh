/* detail/executor-lock.hh
 *
 * Copyright (C) 2018 GOU Lingfeng
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
#ifndef _GAPR_INCLUDE_DETAIL_EXECUTOR_LOCK_HH_
#define _GAPR_INCLUDE_DETAIL_EXECUTOR_LOCK_HH_

#include "gapr/config.hh"

#include <atomic>
//#include <type_traits>

#include <boost/asio/any_io_executor.hpp>

namespace boost { namespace asio { class execution_context; } }

namespace gapr {

#if 0
#endif

	class weak_executor_PRIV {

		struct state {
			std::atomic<unsigned int> lock_cnt{0};
			std::atomic<unsigned int> refc{1};
			void upref() noexcept {
				//fprintf(stderr, "upref\n");
				refc.fetch_add(1);
			}
		};
		static void unref(state* st) noexcept {
			//fprintf(stderr, "downref\n");
			if(st->refc.fetch_sub(1)==1) {
				//fprintf(stderr, "delete\n");
				delete st;
			}
		}
		static void unlock(state* st) noexcept {
			if((st->lock_cnt.fetch_sub(2)|1)==3)
				;//XXX signal
		}
		static bool lock(state* st) noexcept {
			auto& cnt=st->lock_cnt;
			auto v=cnt.load();
			while(!(v&1))
				if(cnt.compare_exchange_weak(v, v+2))
					return true;
			return false;
		}
		struct myservice;
		GAPR_CORE_DECL static state* get_state(boost::asio::execution_context& ctx);

		template<typename Executor> friend class weak_executor;
		template<typename Executor> friend class executor_lock;
	};

	template<typename Executor> class executor_lock;

	template<typename Executor> class weak_executor {
		public:
			explicit weak_executor(const Executor& ex):
				_ex{ex}, _st{weak_executor_PRIV::get_state(boost::asio::query(_ex, boost::asio::execution::context))}
			{
				_work=boost::asio::prefer(ex, boost::asio::execution::outstanding_work.tracked);
			}
			~weak_executor() {
				if(_st)
					destroy();
			}
			weak_executor(const weak_executor&) =delete;
			weak_executor& operator=(const weak_executor&) =delete;
			weak_executor(weak_executor&& r) noexcept:
				_ex{std::move(r._ex)}, _st{r._st} { r._st=nullptr; }
			weak_executor& operator=(weak_executor&&) =delete;

			//explicit operator bool() const noexcept { return _st; }
			executor_lock<Executor> lock() const noexcept;
			//void reset() noexcept { if(_st) { destroy(); _st=false; } }
		private:
			Executor _ex;
			boost::asio::any_io_executor _work;
			weak_executor_PRIV::state* _st;
			void destroy() noexcept {
				if(weak_executor_PRIV::lock(_st)) {
					_work={};
					weak_executor_PRIV::unlock(_st);
				}
				weak_executor_PRIV::unref(_st);
			}
			friend class executor_lock<Executor>;
	};

	template<typename Executor> class executor_lock {
		public:
			using executor_type=Executor;
			explicit executor_lock(const weak_executor<Executor>& ex) noexcept:
				_ex{ex._ex},
				_st{weak_executor_PRIV::lock(ex._st)?ex._st:nullptr} { }
			~executor_lock() { if(_st) weak_executor_PRIV::unlock(_st); }
			executor_lock(const executor_lock&) =delete;
			executor_lock& operator=(const executor_lock&) =delete;
			executor_lock(executor_lock&& r) noexcept:
				_ex{std::move(r._ex)}, _st{std::move(r._st)} { }
			executor_lock& operator=(executor_lock&&) =delete;

			explicit operator bool() const noexcept { return _st; }
			executor_type get_executor() const noexcept { return _ex; }

		private:
			Executor _ex;
			weak_executor_PRIV::state* _st;
	};

	template<typename Ctx>
		inline weak_executor<typename Ctx::executor_type> make_weak_executor(Ctx& ctx) noexcept {
			return weak_executor<typename Ctx::executor_type>{ctx.get_executor()};
		}

	template<typename Executor>
		inline executor_lock<Executor> weak_executor<Executor>::lock() const noexcept {
			return executor_lock<Executor>{*this};
		}

}

#endif
