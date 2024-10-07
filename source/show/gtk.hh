#include <utility>
#include <cassert>

#include <gtk/gtk.h>

#define G_SETTINGS_ENABLE_BACKEND
#include <gio/gsettingsbackend.h>

#include <boost/asio/any_io_executor.hpp>

#include "gapr/detail/scoped-fence.hh"

namespace gtk {

	template<typename T, typename T2, void(*Del)(T2*)> class ref_base {
		public:
			constexpr ref_base() noexcept: _p{nullptr} { }
			//constexpr explicit ref_base(gpointer p) noexcept: _p{p} { }
			constexpr ref_base(T* p) noexcept: _p{p} { }
			~ref_base() { if(_p) Del(static_cast<T*>(_p)); }
			ref_base(const ref_base& r) =delete;
			ref_base& operator=(const ref_base& r) =delete;
			constexpr ref_base(ref_base&& r) noexcept: _p{r._p} {
				if(r._p) r._p=nullptr;
			}
			ref_base& operator=(ref_base&& r) noexcept {
				std::swap(_p, r._p);
				return *this;
			}

			operator T*() const noexcept { return get(); }
			T* get() const noexcept { return static_cast<T*>(_p); }
			operator bool() const noexcept { return this->_p; }

		protected:
			T* _p;
		private:
	};
	template<typename T, typename T2, void(*Del)(T2*), T2*(*Ref)(T2*)> class ref_base2: public ref_base<T, T2, Del> {
		public:
			constexpr ref_base2() noexcept: ref_base<T, T2, Del>{nullptr} { }
			//constexpr explicit ref_base2(gpointer p) noexcept: ref_base{p} { }
			constexpr ref_base2(T* p) noexcept: ref_base<T, T2, Del>{p} { }
			~ref_base2() { }
			constexpr ref_base2(const ref_base2& r) noexcept:
				ref_base2{r._p?Ref(r._p):nullptr} { }
			ref_base2& operator=(const ref_base2& r) noexcept {
				ref_base2 p{r};
				std::swap(this->_p, p._p);
				return *this;
			}
			constexpr ref_base2(ref_base2&& r) noexcept =default;
			ref_base2& operator=(ref_base2&& r) noexcept =default;
	};
	/*
	template<typename T, void(*Del)(T*)>
		using ref_base=ref_base<T, T, Del>;
		*/

	template<typename T>
		using ref_base_obj=ref_base2<T, void, g_object_unref, g_object_ref>;
	template<typename T> class ref final: public ref_base_obj<T> {
		public:
			constexpr ref() noexcept: ref_base_obj<T>{} { }
			constexpr ref(T* p) noexcept: ref_base_obj<T>{p} {
				if(p && G_IS_INITIALLY_UNOWNED(p)) {
					g_object_ref_sink(p);
				}
			}
			constexpr explicit ref(gpointer p) noexcept:
				ref{static_cast<T*>(p)} { }
			constexpr ref(ref&& r) noexcept =default;
			ref& operator=(ref&& r) noexcept =default;
			/*
			operator T*() const noexcept {
				return static_cast<T*>(_p);
			}
			*/
			operator GTypeInstance*() const noexcept {
				return reinterpret_cast<GTypeInstance*>(this->_p);
			}
			operator gpointer() const noexcept {
				return this->_p;
			}
			T* operator->() const noexcept { return *this; }
			T* release() noexcept {
				auto p=this->_p;
				this->_p=nullptr;
				return p;
			}

		private:
			constexpr ref(bool, T* p) noexcept: ref_base_obj<T>{p} { }
			template<typename TT> friend class weak_ref;
	};
	template<> class ref<GSettingsSchema> final:
		public ref_base2<GSettingsSchema, GSettingsSchema, g_settings_schema_unref, g_settings_schema_ref> {
			public:
				constexpr ref() noexcept: ref_base2{} { }
				constexpr ref(GSettingsSchema* p) noexcept: ref_base2{p} { }
				constexpr ref(ref&& r) noexcept =default;
				ref& operator=(ref&& r) noexcept =default;
		};
	template<> class ref<GError> final:
		public ref_base<GError, GError, g_error_free> {
			public:
				constexpr ref() noexcept: ref_base{} { }
				constexpr ref(GError* p) noexcept: ref_base{p} { }

				GError** operator&() /*const*/ noexcept {
					return &_p;
				}
				GError* operator->() const noexcept { return _p; }
		};
	template<> class ref<GSource> final:
		public ref_base2<GSource, GSource, g_source_unref, g_source_ref> {
			public:
				constexpr ref() noexcept: ref_base2{} { }
				constexpr ref(GSource* p) noexcept: ref_base2{p} { }
				constexpr ref(ref&& r) noexcept =default;
				ref& operator=(ref&& r) noexcept =default;
		};
	template<> class ref<cairo_t> final:
		public ref_base2<cairo_t, cairo_t, cairo_destroy, cairo_reference> {
			public:
				constexpr ref() noexcept: ref_base2{} { }
				constexpr ref(cairo_t* p) noexcept: ref_base2{p} { }
				constexpr ref(ref&& r) noexcept =default;
				ref& operator=(ref&& r) noexcept =default;
		};
	template<> class ref<cairo_surface_t> final:
		public ref_base2<cairo_surface_t, cairo_surface_t, cairo_surface_destroy, cairo_surface_reference> {
			public:
				constexpr ref() noexcept: ref_base2{} { }
				constexpr ref(cairo_surface_t* p) noexcept: ref_base2{p} { }
				constexpr ref(ref&& r) noexcept =default;
				ref& operator=(ref&& r) noexcept =default;
		};
	inline PangoFontDescription* pango_font_description_copy_fix_type(PangoFontDescription* ptr) {
		return pango_font_description_copy(ptr);
	}
	template<> class ref<PangoFontDescription> final:
		public ref_base2<PangoFontDescription, PangoFontDescription, pango_font_description_free, pango_font_description_copy_fix_type> {
			public:
				constexpr ref() noexcept: ref_base2{} { }
				constexpr ref(PangoFontDescription* p) noexcept: ref_base2{p} { }
				constexpr ref(ref&& r) noexcept =default;
				ref& operator=(ref&& r) noexcept =default;
		};
	template<> class ref<gchar*> final:
		public ref_base<gchar*, gchar*, g_strfreev> {
			public:
				constexpr ref() noexcept: ref_base{} { }
				constexpr ref(gchar** p) noexcept: ref_base{p} { }
		};
	template<> class ref<gchar> final:
		public ref_base<gchar, void, g_free> {
			public:
				constexpr ref() noexcept: ref_base{} { }
				constexpr ref(gchar* p) noexcept: ref_base{p} { }
		};

	template<typename T> class weak_ref {
		public:
			constexpr weak_ref() noexcept {
				g_weak_ref_init(&_p, nullptr);
			}
			constexpr explicit weak_ref(T* p) noexcept {
				g_weak_ref_init(&_p, p);
			}
			~weak_ref() { g_weak_ref_clear(&_p); }
			weak_ref(const weak_ref& r) =delete;
			weak_ref& operator=(const weak_ref& r) =delete;
			weak_ref(weak_ref&& r) =delete;
			weak_ref& operator=(weak_ref&& r) =delete;
			weak_ref& operator=(T* p) noexcept {
				g_weak_ref_set(&_p, p);
				return *this;
			}

			ref<T> lock() noexcept {
				return ref<T>{true, static_cast<T*>(g_weak_ref_get(&_p))};
			}
		private:
			GWeakRef _p;
	};

	template<typename T>
		ref(T*) -> ref<T>;

	inline ref<GSettings> make_g_settings(const gchar* schema_id) {
		auto source=g_settings_schema_source_get_default();
		if(!source)
			return {};
		ref schema=g_settings_schema_source_lookup(source, schema_id, TRUE);
		if(!schema)
			return {};
		ref backend=g_settings_backend_get_default();
		assert(backend);
		return g_settings_new_full(schema, backend, nullptr);
	}
}

#include <boost/asio/io_context.hpp>
#include <boost/asio/thread_pool.hpp>
#include <thread>

namespace gtk {

	class MainContext: public boost::asio::execution_context {
		public:
			template<bool block=true, bool defer=true, bool track=false>
				class executor_base;
			using executor_type=executor_base<>;

			explicit MainContext(GMainContext* ctx, GApplication* app): _ctx{ctx}, _app{app} {
			}
			~MainContext() {
				//Destroys all unexecuted function objects that were submitted via an executor object that is associated with the execution context.
			}

			MainContext(const MainContext&) =delete;
			MainContext& operator=(const MainContext&) =delete;

			executor_type get_executor() noexcept;

		private:
			GMainContext* _ctx;
			template<typename Func> struct Wrap {
				template<typename Func2>
					explicit Wrap(Func2&& f): f1{std::forward<Func2>(f)} {
					}

				static gboolean call(gpointer ptr) {
					auto p=static_cast<Wrap*>(ptr);
					p->f1();
					return false;
				}
				static void destroy(gpointer ptr) {
					std::unique_ptr<Wrap> p{static_cast<Wrap*>(ptr)};
				}
				Func f1;
			};
			template<typename Func, typename Alloc>
				void post(Func&& func, const Alloc& alloc) {
					using Wrap=Wrap<std::decay_t<Func>>;
					auto f1=std::make_unique<Wrap>(std::forward<Func>(func));
					gtk::ref<GSource> source=g_idle_source_new();
					g_source_set_priority(source, G_PRIORITY_DEFAULT);
					g_source_set_callback(source, Wrap::call, f1.release(), Wrap::destroy);
					g_source_attach(source, _ctx);
					//g_source_unref(source);
				}
			GApplication* _app;
			std::exception_ptr _eptr;
			void handle_exception() {
				if(!_eptr)
					_eptr=std::current_exception();
				g_application_quit(_app);
			}
			void check_exception() {
				if(_eptr) {
					auto eptr=std::move(_eptr);
					std::rethrow_exception(eptr);
				}
			}
			void start_work() const noexcept {
				g_object_ref(_app);
			}
			void finish_work() const noexcept {
				g_object_unref(_app);
			}
	};

	template<bool block, bool defer, bool track> class MainContext::executor_base {
		public:
			executor_base(MainContext& ctx) noexcept: _ctx{&ctx} {
				if(track)
					_ctx->start_work();
			}
			~executor_base() {
				if(track && _ctx)
					_ctx->finish_work();
			}
			executor_base(const executor_base& r) noexcept: _ctx{r._ctx} {
				if(track && _ctx)
					_ctx->start_work();
			}
			executor_base& operator=(const executor_base& r) noexcept {
				auto prev_ctx=_ctx;
				_ctx=r._ctx;
				if(track) {
					if(_ctx)
						_ctx->start_work();
					if(prev_ctx)
						prev_ctx->finish_work();
				}
				return *this;
			}
			executor_base(executor_base&& r) noexcept: _ctx{r._ctx} {
				if(track)
					r._ctx=nullptr;
			}
			executor_base& operator=(executor_base&& r) noexcept {
				auto prev_ctx=_ctx;
				_ctx=r._ctx;
				if(track) {
					r._ctx=nullptr;
				  	if(prev_ctx)
						prev_ctx->finish_work();
				}
				return *this;
			}

			bool operator==(const executor_base& r) const noexcept {
				return _ctx==r._ctx;
			}
			bool operator!=(const executor_base& r) const noexcept {
				return !(*this==r);
			}
			void swap(executor_base& r) noexcept {
				std::swap(_ctx, r._ctx);
			}

			bool running_in_this_thread() const noexcept {
				return g_main_context_is_owner(_ctx->_ctx);
			}

			template<typename F> void execute(F&& f) const {
				using FF=std::decay_t<F>;
				if(block && g_main_context_is_owner(_ctx->_ctx)) {
					FF ff{std::forward<F>(f)};
					try {
						gapr::scoped_fence<std::memory_order_release> fence{std::memory_order_acquire};
						ff();
						return;
					} catch(...) {
						_ctx->handle_exception();
						return;
					}
				}
				return _ctx->post(std::forward<F>(f), std::allocator<void>{});
			}
			MainContext& query(boost::asio::execution::context_t) const noexcept {
				return *_ctx;
			}
			constexpr executor_base<false, defer, track> require(boost::asio::execution::blocking_t::never_t) const {
				return {*_ctx};
			}

			constexpr executor_base<true, defer, track> require(boost::asio::execution::blocking_t::possibly_t) const {
				return {*_ctx};
			}
			constexpr executor_base<block, false, track> require(boost::asio::execution::relationship_t::fork_t) const {
				return {*_ctx};
			}
			constexpr executor_base<block, true, track> require(boost::asio::execution::relationship_t::continuation_t) const {
				return {*_ctx};
			}
			constexpr executor_base<block, defer, false> require(boost::asio::execution::outstanding_work_t::untracked_t) const {
				return {*_ctx};
			}
			constexpr executor_base<block, defer, true> require(boost::asio::execution::outstanding_work_t::tracked_t) const {
				return {*_ctx};
			}
#if 0
			static constexpr boost::asio::execution::blocking_t::never_t query(
					boost::asio::execution::blocking_t) noexcept
			{
				return boost::asio::execution::blocking.never;
			}
#endif

		private:
			MainContext* _ctx;
	};
	inline MainContext::executor_type MainContext::get_executor() noexcept {
		return executor_type{*this};
	}

	struct Resources {
		struct GBytesDeletor {
			void operator()(GBytes* b) const {
				g_bytes_unref(b);
			}
		};
		struct Bytes: std::unique_ptr<GBytes, GBytesDeletor> {
			gconstpointer _data;
			gsize _size;
			Bytes(GBytes* b): unique_ptr{b} {
				if(b)
					_data=g_bytes_get_data(b, &_size);
			}
			const char* data() {
				return static_cast<const char*>(_data);
			}
			gsize size() { return _size; }
		};
		Bytes load(const char* path) {
			return Bytes{g_resources_lookup_data(path,
					G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr)};
		}
	};

	template<typename T, typename... T2>
		T flags(T v0, T2... v2) {
			//static_assert((std::is_same_v<T, T2> && ...));
			if constexpr(sizeof...(v2)==0) {
				return v0;
			} else {
				return static_cast<T>(v0|(v2|...));
			}
		}

}

namespace gapr {

	class Context: public std::enable_shared_from_this<Context> {
		public:
			explicit Context(GApplication* app): _ui_ctx{g_main_context_default(), app},
						_io_ctx{1}, _pool{std::thread::hardware_concurrency()}, _io_thr{} { }
			~Context() { }
			Context(const Context& r) =delete;
			Context& operator=(const Context& r) =delete;

			gtk::MainContext& main_context() noexcept { return _ui_ctx; }
			boost::asio::thread_pool& thread_pool() noexcept { return _pool; }
			boost::asio::io_context& io_context() {
				if(!_io_thr.joinable()) {
					_work=boost::asio::require(_io_ctx.get_executor(), boost::asio::execution::outstanding_work.tracked);
					_io_thr=std::thread{[this]() {
						[[maybe_unused]] auto n=_io_ctx.run();
						fprintf(stderr, "processed %d messages\n", (int)n);
					}};
				}
				return _io_ctx;
			}

			void join() {
				if(_io_thr.joinable()) {
					_work={};
					_io_thr.join();
				}
				_pool.join();
			}

		private:
			gtk::MainContext _ui_ctx;
			boost::asio::io_context _io_ctx;
			boost::asio::thread_pool _pool;
			std::thread _io_thr;
			boost::asio::any_io_executor _work;
	};

}
