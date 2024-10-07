/* gapr/downloader.hh
 *
 * Copyright (C) 2019,2020 GOU Lingfeng
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

//@@@
#ifndef _GAPR_INCLUDE_DOWNLOADER_HH_
#define _GAPR_INCLUDE_DOWNLOADER_HH_

#include "gapr/config.hh"

#include <future>
#include <vector>
#include <boost/asio/any_io_executor.hpp>

#include "gapr/detail/cb-wrapper.hh"

typedef struct Curl_URL CURLU;

namespace gapr {

	struct downloader_PRIV;
	class GAPR_CORE_DECL downloader {
		public:
			using executor_type=boost::asio::any_io_executor;

			constexpr downloader() noexcept: _priv{} { }
			downloader(const executor_type& ex, std::string&& url);
			downloader(const executor_type& ex, const std::vector<std::string>& urls);
			~downloader();
			downloader(const downloader&) =delete;
			downloader& operator=(const downloader&) =delete;
			downloader(downloader&& r) noexcept =default;
			downloader& operator=(downloader&& r) noexcept =default;

			operator bool() const noexcept { return !!_priv; }

			void update_login(std::string&& username, std::string&& password);

			template<typename Handler>
				auto async_wait(/*for progress, */Handler&& handler) {
					using Sig=void(std::error_code, int);
					return boost::asio::async_initiate<Handler, Sig>(init_wait{_priv.get()}, handler);
				}

			std::pair<std::unique_ptr<std::streambuf>, unsigned int> get() const {
				if(!data().notified)
					throw std::future_error{std::future_errc::broken_promise};
				if(data().get_head!=0)
					return do_get();
				return {nullptr, 0};
			}

#if 0
			// not needed?
			std::error_code get_error() const {
				if(!data().notified)
					throw std::future_error{std::future_errc::broken_promise};
				return data().ec;
			}
			int progress() { return 0; }
#endif

			static void when_idle(std::string&& url);

			constexpr static int FINISHED=1001;
			constexpr static int TOTAL=1000;
			constexpr static int FAILED=-2;
			constexpr static int UNKNOWN=-1;

		private:
			struct DATA {
				executor_type ex;
				bool notified{false};
				unsigned int get_head{0};
				std::error_code ec{};
				std::string _login_url;
				std::string _login_usr;
				std::string _login_pw;
			};
			std::shared_ptr<downloader_PRIV> _priv;
			DATA& data() const noexcept {
				return *reinterpret_cast<DATA*>(_priv.get());
			}
			std::pair<std::unique_ptr<std::streambuf>, unsigned int> do_get() const;

			struct WaitOp: cb_wrapper::add<void(const std::error_code&, int)> {
				void complete(const std::error_code& ec, int v) {
					return cb_wrapper_call(ec, v);
				}
			};
			static void do_wait(downloader_PRIV* p, std::unique_ptr<WaitOp> op);
			class init_wait {
				public:
					explicit init_wait(downloader_PRIV* p) noexcept: _p{p} { }
					template<typename Handler> void operator()(Handler&& handler) {
						auto op=cb_wrapper::make_unique<WaitOp>(std::move(handler));
						do_wait(_p, std::move(op));
					}
				private:
					downloader_PRIV* _p;
			};
			
			friend struct downloader_PRIV;
	};

}

#endif
