#include "gapr/fiber.hh"

#include "gapr/utility.hh"

struct gapr::fiber_PRIV::HelperB {
	boost::context::continuation fib_new;
	void operator()() {
		auto fib_nil=std::move(fib_new).resume();
		assert(!fib_nil);
	}
};
struct gapr::fiber_PRIV::HelperA {
	boost::asio::io_context::executor_type ex;
	boost::context::continuation operator()(boost::context::continuation&& fib_new) {
		boost::asio::post(ex, HelperB{std::move(fib_new)});
		return {};
	}
};

void gapr::fiber_PRIV::change_executor(boost::context::continuation& fib, boost::asio::io_context::executor_type ex) {
	if(!ex.running_in_this_thread()) {
		//gapr::print("before thread switch");
		fib/*io*/=std::move(fib/*x*/).resume_with(HelperA{ex});
		//gapr::print("after thread switch");
	} else {
		gapr::print("no thread switch");
	}
	assert(ex.running_in_this_thread());
}

