#include "gapr/future.hh"

#include "gapr/utility.hh"

namespace gapr {

	const likely_PRIV::dumb_cat likely_PRIV::ecat_eptr;

}

#include <boost/asio/thread_pool.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/dispatch.hpp>

#ifdef _MSC_VER
static inline int usleep(int usec) { return 0; }
#else
#include <unistd.h>
#endif

namespace gapr_test {

	static gapr::likely_noexcept<std::string> f(int v) {
		if(v>0) {
			std::ostringstream oss;
			oss<<v;
			return oss.str();
		}
		if(v==0)
			return std::make_error_code(std::errc::invalid_argument);
		throw std::logic_error{"negative"};
	}
	static gapr::likely<std::string> g(int v) noexcept try {
		if(v>0) {
			std::ostringstream oss;
			oss<<v;
			return oss.str();
		}
		if(v==0)
			return std::make_error_code(std::errc::invalid_argument);
		throw std::runtime_error{"negative"};
	} catch(const std::runtime_error& e) {
		return std::current_exception();
	}

	int chk_likely() {
		for(int i=-1; i<=1; i++) {
			gapr::print("try f: ", i);
			try {
				if(auto ff=f(i))
					gapr::print("   val: ", ff.get());
				else
					gapr::print("   err: ", ff.error().message());
			} catch(const std::exception& e) {
				gapr::print("   exception: ", e.what());
			}
			gapr::print("try g: ", i);
			try {
				auto gg=g(i);
				gapr::print("   val: ", gg.value());
			} catch(const std::system_error& e) {
				gapr::print("   err: ", e.code().message());
			} catch(const std::exception& e) {
				gapr::print("   exception: ", e.what());
			}
		}
		return 0;
	}

	static void async_a(gapr::promise<std::string>&& prom, boost::asio::thread_pool& pool, int v) {
		boost::asio::dispatch(pool.get_executor(), [prom=std::move(prom),v]() mutable noexcept { try {
			usleep(1000*100);
			if(v>0) {
				std::ostringstream oss;
				oss<<v;
				return std::move(prom).set(oss.str());
			}
			if(v==0)
				return std::move(prom).set_error(std::make_error_code(std::errc::invalid_argument));
			throw std::runtime_error{"negative"};
		} catch(const std::runtime_error& e) {
			std::move(prom).set_error(std::current_exception());
		} });
	}
	static void async_b(gapr::promise<std::string>&& prom, boost::asio::thread_pool& pool, int v) {
		boost::asio::dispatch(pool.get_executor(), [prom=std::move(prom),v]() mutable noexcept { try {
			usleep(1000*100);
			if(v>0) {
				std::ostringstream oss;
				oss<<v;
				return std::move(prom).set(oss.str());
			}
			if(v==0)
				return std::move(prom).set_error(std::make_error_code(std::errc::invalid_argument));
			throw std::runtime_error{"negative"};
		} catch(const std::runtime_error& e) {
			std::move(prom).set_error(std::current_exception());
		} });
	}
	static auto async_serial(boost::asio::io_context& ctx, boost::asio::thread_pool& pool, int x, int y) {
#if 0
		auto a=f(x);
		if(!a)
			return a;
		auto b=g(y);
		if(!b)
			return b;
		return a+b;
#endif
		gapr::promise<std::string> prom{};
		auto fut=prom.get_future();

		gapr::promise<std::string> prom_a{};
		auto fut_a=prom_a.get_future();
		async_a(std::move(prom_a), pool, x);
		///
		///
		///
		/////////////////////////////////////////////////
		std::move(fut_a).async_wait(ctx.get_executor(), [&ctx,pool_ex=gapr::make_weak_executor(pool),prom=std::move(prom),y](gapr::likely<std::string>&& res_a) mutable /*n*/ {
			if(!res_a)
				return std::move(prom).set_error(std::move(res_a).error());
			gapr::promise<std::string> prom_b{};
			auto fut_b=prom_b.get_future();
			if(auto lck=pool_ex.lock()) {
				async_b(std::move(prom_b), boost::asio::query(lck.get_executor(), boost::asio::execution::context), y);
			} else
				return;
			std::move(fut_b).async_wait(ctx.get_executor(), [prom=std::move(prom),a=res_a.get()](gapr::likely<std::string>&& res_b) mutable /*n*/ {
				if(!res_b)
					return std::move(prom).set_error(std::move(res_b).error());
				std::move(prom).set(a+(res_b.get()));
			});
		});
		return fut;
	}
#if 0
	static void async_and(gapr::promise<std::string>&& prom, boost::asio::thread_pool& pool, int x, int y) {
#if 0
		// any fail: fail
		// all success: success
#pragma omp parallel
		{
#pragma omp task
			{
				a=f(x);
				if(!a)
					return false;
			}
#pragma omp task
			{
				b=g(y);
				if(!b)
					return false;
			}
		}
		return a+b;
#endif
	}
	static void async_or(gapr::promise<std::string>&& prom, boost::asio::thread_pool& pool, int x, int y) {
		//any succ: success
		//all fail: fail
#if 0
#pragma omp parallel
		{
#pragma omp task
			{
				a=f(x);
				if(!a)
					return false;
				return a;
			}
#pragma omp task
			{
				b=g(y);
				if(!b)
					return false;
				return b;
			}
		}
#endif
	}
#endif

	int chk_promise() {
		boost::asio::io_context ctx{1};
		boost::asio::thread_pool pool{16};

		for(int x=-1; x<=1; x++) {
			for(int y=-1; y<=1; y++) {
				gapr::print("try ", x, ',', y);
				//prom.canceler();
				auto fut=async_serial(ctx, pool, x, y);
				//////////////
				//////////////////////////////////////
				/////////
				std::move(fut).async_wait(ctx.get_executor(), [x,y](gapr::likely<std::string>&& v) {
					gapr::print("  prom_ser: ", x, '+', y, '=', v.value());
				});
			}
		}
		do {
			try {
				auto r=ctx.run();
				if(r==0)
					break;
			} catch(const std::exception& e) {
				gapr::print("   exception: ", e.what());
			}
		} while(true);
		return 0;
	}

}

