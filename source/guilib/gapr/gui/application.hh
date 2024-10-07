/* gui/application.hh
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


#ifndef _GAPR_INCLUDE_GUI_APPLICATION_HH_
#define _GAPR_INCLUDE_GUI_APPLICATION_HH_

#include "gapr/config.hh"

#include "gapr/detail/cb-wrapper.hh"
#include "gapr/detail/scoped-fence.hh"

//#include "gapr/mem-file-cache.hh"

#include <thread>

#include <QApplication>
#include <QEvent>
#include <boost/asio/io_context.hpp>
//#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/thread_pool.hpp>
//#include <utility>


// XXX rename gapr::application when better

namespace gapr {

	class Program;

	class GAPR_GUI_DECL Application: public QApplication,
	public boost::asio::execution_context {
		Q_OBJECT
		public:
			using ssl_context_type=boost::asio::ssl::context;
			struct Enabler {
				explicit Enabler() { if(0==_inst_refc++) create_instance(); }
				~Enabler() noexcept { if(1==_inst_refc--) destroy_instance(); }
				Enabler(const Enabler&) =delete;
				Enabler& operator=(const Enabler&) =delete;
			};

			class executor_type;
			executor_type get_executor();

			~Application();
			/* XXX Destroys all unexecuted function objects that were submitted via an executor object that is associated with the execution context. */

			Application(const Application&) =delete;
			Application& operator=(const Application&) =delete;

			static Application& instance() noexcept {
				assert(_instance); return *_instance;
			}

			boost::asio::io_context& io_context() noexcept { return _io_ctx; }
			ssl_context_type& ssl_context() noexcept { return _ssl_ctx; }
			boost::asio::thread_pool& thread_pool() noexcept { return _threadp; }
			//FileCache& file_cache() noexcept { return _file_cache; }

			static void request_quit();

			static void show_start_window(QWidget* src=nullptr);
			static void show_start_window(Program* factory, int argc, char* argv[], QWidget* src=nullptr);
			static void show_options_dialog(QWidget& src);
			static void display_help(QWidget& src, const QString& part={});
			static void show_about_dialog(QWidget& src);

			static int exec();

		private:
			boost::asio::io_context _io_ctx;
			ssl_context_type _ssl_ctx;
			boost::asio::thread_pool _threadp;

			//FileCache _file_cache;

			QTranslator* _translator;
			std::thread::id _gui_thread{};

			Application(int& argc, char** argv);
			bool notify(QObject* receiver, QEvent* event) override;
			bool event(QEvent* e) override;

			static int _inst_refc;
			static Application* _instance;
			static void create_instance();
			static void destroy_instance();

			constexpr static auto _event_post_cb{static_cast<QEvent::Type>(QEvent::User+0)};
			using PostExecEvent=gapr::cb_wrapper::add<void(), QEvent>;
	};

	inline Application& app() noexcept {
		return Application::instance();
	}

}

class gapr::Application::executor_type {
	public:
		executor_type(const executor_type& r) noexcept: _ctx{r._ctx}, _block{r._block} { }
		executor_type(executor_type&& r) noexcept: _ctx{r._ctx}, _block{r._block} { }
		//executor_type& operator=(const executor_type& r) noexcept { }
		//executor_type& operator=(executor_type&& r) noexcept { }

		~executor_type() { }

		// nodata race
		bool operator==(const executor_type& r) const noexcept {
			return std::addressof(_ctx)==std::addressof(r._ctx);
		}
		bool operator!=(const executor_type& r) const noexcept {
			return !(*this==r);
		}

		bool running_in_this_thread() const noexcept {
			return _ctx._gui_thread==std::this_thread::get_id();
		}

		Application& query(boost::asio::execution::context_t) const noexcept {
			return _ctx;
		}
		template<typename F> void execute(F&& f) const {
				using FF=std::decay_t<F>;
				if(_block && running_in_this_thread()) {
					FF ff{std::forward<F>(f)};
					try {
						gapr::scoped_fence<std::memory_order_release> fence{std::memory_order_acquire};
						ff();
						return;
					} catch(...) {
						//handle_exception();
						return;
					}
				}
				auto e=gapr::cb_wrapper::make_unique<PostExecEvent>(std::forward<F>(f), _event_post_cb);
				return QCoreApplication::postEvent(&_ctx, e.release());
		}
		executor_type require(boost::asio::execution::blocking_t::never_t) const {
			return {_ctx, false};
		}

		//swap noexcept
	private:
		Application& _ctx;
		bool _block;
		executor_type(Application& ctx, bool block=true): _ctx{ctx}, _block{block} { }
		friend class gapr::Application;
};

inline gapr::Application::executor_type gapr::Application::get_executor() { return executor_type{*this}; }

#endif
