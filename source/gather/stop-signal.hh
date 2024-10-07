#include <thread>
#include <iostream>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/post.hpp>

#include "gapr/utility.hh"

struct stop_signal {

	void start() {
		_sigset.add(SIGINT);
		_sigset.add(SIGTERM);
		watch_signals();
		io_ctx_alt_thread=std::thread{[this] {
			_io_ctx_alt.run();
		}};
	}
	void stop() {
		boost::asio::post(_io_ctx_alt, [this]() {
			_sigset.cancel();
		});
		io_ctx_alt_thread.join();
	}
	template<typename Cb>
	void on_stop(Cb&& cb) {
		_cb=std::forward<Cb>(cb);
	}
	unsigned int signal() const noexcept { return _stop_signal; }

	std::function<void(unsigned int)> _cb;
	std::thread io_ctx_alt_thread;
	boost::asio::io_context _io_ctx_alt{1};
	boost::asio::signal_set _sigset{_io_ctx_alt.get_executor()};
	std::chrono::steady_clock::time_point _sig_last;
	unsigned int _stop_signal{0};

	void watch_signals() {
		_sigset.async_wait([this](boost::system::error_code ec, int signum) {
			if(ec) {
				if(ec==boost::asio::error::operation_aborted)
					return;
				std::cerr<<"failed to wait signal: "<<ec.message()<<"\n";
				return;
			}
			gapr::print("sig: ", signum);
			switch(signum) {
			case SIGINT:
				break;
			case SIGTERM:
				_stop_signal=2;
				_cb(_stop_signal);
				return;
			default:
				assert(0);
				return;
			}

			auto t=std::chrono::steady_clock::now();
			if(!_stop_signal) {
				_stop_signal=1;
				_cb(_stop_signal);
			} else if(std::chrono::duration_cast<std::chrono::milliseconds>(t-_sig_last).count()<2000) {
				_stop_signal=2;
				_cb(_stop_signal);
				return;
			}
			_sig_last=t;
			watch_signals();
		});
	}

};
