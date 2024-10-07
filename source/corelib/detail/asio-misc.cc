#include "gapr/detail/executor-lock.hh"

#include <boost/asio/execution_context.hpp>

struct gapr::weak_executor_PRIV::myservice: boost::asio::execution_context::service {
	using key_type=myservice;
	explicit myservice(boost::asio::execution_context& ctx):
		service{ctx}, _st{new state{}} { }
	~myservice() { unref(_st); }

	state* _st;
	private:
	void shutdown() noexcept override {
		auto& cnt=_st->lock_cnt;
		auto r=cnt.fetch_or(1);
		while((r|1)!=1) {
			//XXX wait;
			r=cnt.load();
		}
	}
};

gapr::weak_executor_PRIV::state* gapr::weak_executor_PRIV::get_state(boost::asio::execution_context& ctx) {
	auto& svc=boost::asio::use_service<myservice>(ctx);
	svc._st->upref();
	return svc._st;
}
