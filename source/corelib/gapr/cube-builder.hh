/* cube-builder.hh
 *
 * Copyright (C) 2018-2020 GOU Lingfeng
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
#ifndef _GAPR_INCLUDE_CUBE_BUILDER_HH_
#define _GAPR_INCLUDE_CUBE_BUILDER_HH_

#include "gapr/config.hh"

#include <future>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/thread_pool.hpp>

#include "gapr/cube.hh"
#include "gapr/detail/cb-wrapper.hh"

namespace gapr {
	struct cube_builder_PRIV;
	class GAPR_CORE_DECL cube_builder {
		public:
			using executor_type=boost::asio::any_io_executor;

			cube_builder(const executor_type& ex, boost::asio::thread_pool& pool);
			~cube_builder();
			cube_builder(const cube_builder&) =delete;
			cube_builder& operator=(const cube_builder&) =delete;
			cube_builder(cube_builder&&) =delete;
			cube_builder& operator=(cube_builder&&) =delete;

			bool build(unsigned int chan, const gapr::cube_info& info,
					const std::array<double, 3>& pos, bool bigger,
					const std::array<unsigned int, 3>* offset, bool fixed=false/*, id*, attr*/);
			void build(unsigned int chan, const gapr::cube_info& info,
					const std::array<unsigned, 3>& offset, bool bigger);
			void build(unsigned int chan, const gapr::cube_info& info);

			void update_login(std::string&& username, std::string&& password);

			/*! currently one wait call after construction FIXME */
			template<typename Handler>
				auto async_wait(/*for progress, */Handler&& handler) {
					using Sig=void(std::error_code, int);
					return boost::asio::async_initiate<Handler, Sig>(init_wait{_priv.get()}, handler);
				}

			struct Output {
				gapr::cube data;
				unsigned int chan;
				std::array<unsigned int, 3> offset;
				std::string uri;
			};
			Output get() const {
#if 0
				if(!data().notified)
					// XXX dont use std.errors, that's misleading!!!!!!
					throw std::future_error{std::future_errc::broken_promise};
#endif
				if(data().get_head!=0)
					return do_get();
				return {gapr::cube{}, 0, {}, {}};
			}

			void cancel();
			//QVector<QString> sources() const;
			//bool getBoundingBox(std::array<double, 6>* bbox) const;
			//bool getMinVoxelSize(double* vsize) const;
			bool get_bbox(std::array<uint32_t, 6>& bbox);
			//void threadWarning(const QString& msg);
			//void threadError(const QString& msg);
			//signal passwd

		private:
			struct DATA {
				bool notified{false};
				unsigned int get_head{0};
			};
			std::shared_ptr<cube_builder_PRIV> _priv;
			DATA& data() const noexcept {
				return *reinterpret_cast<DATA*>(_priv.get());
			}

			struct WaitOp: cb_wrapper::add<void(const std::error_code&, int)> {
				void complete(const std::error_code& ec, int v) {
					return cb_wrapper_call(ec, v);
				}
			};
			static void do_wait(cube_builder_PRIV* p, std::unique_ptr<WaitOp> op);
			Output do_get() const;
			class init_wait {
				public:
					explicit init_wait(cube_builder_PRIV* p) noexcept: _p{p} { }
					template<typename Handler> void operator()(Handler&& handler) {
						auto op=cb_wrapper::make_unique<WaitOp>(std::move(handler));
						do_wait(_p, std::move(op));
					}
				private:
					cube_builder_PRIV* _p;
			};


			friend struct cube_builder_PRIV;
	};
}

#endif
