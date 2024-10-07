#include <iostream>
#include <thread>
#include <sstream>
#include <string>
#include <random>

#include <boost/asio/thread_pool.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <boost/qvm/vec.hpp>
#include <boost/qvm/mat.hpp>
#include <boost/qvm/mat_operations.hpp>

#include <jni.h>
#include <android/native_activity.h>
#include <android/configuration.h>
#include <android/log.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
//#include <GLES2/gl2ext.h>

#include "gapr/future.hh"
#include "gapr/fiber.hh"
#include "gapr/utility.hh"
#include "gapr/cube-builder.hh"
#include "gapr/str-glue.hh"
#include "gapr/parser.hh"
#include "gapr/mem-file.hh"
#include "gapr/connection.hh"
#include "gapr/trace-api.hh"
#include "gapr/streambuf.hh"
#include "gapr/timer.hh"

#include "model.hh"

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "gapr", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "gapr", __VA_ARGS__))
#ifndef NDEBUG
#  define LOGV(...)  ((void)__android_log_print(ANDROID_LOG_VERBOSE, "gapr", __VA_ARGS__))
#else
#  define LOGV(...)  ((void)0)
#endif

#include "looper-executor.hh"
#include "java-helper.hh"
#include "gapr/vec3.hh"
#include "gl-helpers.hh"
#include "compute.hh"
#include "misc-a.hh"
#include "fixes.hh"

using namespace gapr::fix;
using gapr::edge_model;
using namespace gapr;

namespace ba=boost::asio;
namespace bs=boost::system;

using gapr::client_end;

[[maybe_unused]] constexpr static int MAX_SLICES=480;
[[maybe_unused]] constexpr static std::array<int, 4> SCALE_FACTORS{0, 1, 2, 3};


#include "gapr/detail/template-class-static-data.hh"
#if 1
extern template class boost::asio::detail::call_stack<boost::asio::detail::thread_context, boost::asio::detail::thread_info_base>;
extern template class gapr::detail::template_class<int>;
#endif

class Engine: public std::enable_shared_from_this<Engine> {
	public:
		static Engine* instance();
		static void finalize(Engine* engine);
		Engine(const Engine&) =delete;
		Engine& operator=(const Engine&) =delete;

		struct Args {
			std::string user{};
			std::string passwd{};
			std::string host{};
			unsigned short port{0};
			std::string group{};
		};
		void set_args(Args&& args);
		void open_repository();
		void open_repository_finished();
		void update_control_areas(const int32_t* data, std::size_t size) {
			control_areas.clear();
			control_areas.reserve(size);
			for(std::size_t i=0; i<size; i++)
				control_areas.push_back(data[i]);
		}

		void onDestroy();
		void* onSaveInstanceState(size_t* outSize) {
			// XXX use malloc
			return nullptr;
		}
		static void DEBUG_CALLBACK(Engine* ptr, const char* tag) {
			LOGV("---------%s", tag);
#if 0
			if(ptr->surface!=EGL_NO_SURFACE)
				ptr->get_geometry();
#endif
		}
		void onStart() {
			DEBUG_CALLBACK(this, "onStart");
			setState(APP_PAUSED);
		}
		void onStop() {
			DEBUG_CALLBACK(this, "onStop");
			setState(APP_STOPPED);
		}
		void onResume() {
			DEBUG_CALLBACK(this, "onResume");
			setState(APP_ACTIVE);
		}
		void onPause() {
			DEBUG_CALLBACK(this, "onPause");
			setState(APP_PAUSED);
		}
		void onNativeWindowCreated(ANativeWindow* window) {
			DEBUG_CALLBACK(this, "onNativeWindowCreated");
			if(_window)
				kill_surface(_window);
			assert(window);
			try {
				init_surface(window);
			} catch(const std::exception& e) {
				//return critical_error("Unable to initialize canvas.", {}, ec.message());
				return display_error(e.what());
			} catch(...) {
				return display_error("Unknown error.");
			}
			if(_prog_state==PROG_INIT)
				display_login(nullptr);
		}
		void onNativeWindowDestroyed(ANativeWindow* window) {
			DEBUG_CALLBACK(this, "onNativeWindowDestroyed");
			assert(window);
			kill_surface(window);
			// XXX using window no more
		}
		void onWindowFocusChanged(int hasFocus) {
			DEBUG_CALLBACK(this, "onWindowFocusChanged");
			LOGV("focus: %d", hasFocus);
			// XXX lose input focus
		}
		void onNativeWindowRedrawNeeded(ANativeWindow* window) {
			DEBUG_CALLBACK(this, "onNativeWindowRedrawNeeded");
			if(surface==EGL_NO_SURFACE)
				return;
			_pending_update.fetch_and(0);
			draw_frame_impl();
		}
		void onNativeWindowResized(ANativeWindow* window) {
			DEBUG_CALLBACK(this, "onNativeWindowResized");
			assert(window==_window);
			[[maybe_unused]] auto xx=ANativeWindow_getWidth(window);
			[[maybe_unused]] auto yy=ANativeWindow_getHeight(window);
			LOGV("size: %d %d", xx, yy);

			if(surface==EGL_NO_SURFACE)
				return;
			auto [ww, hh]=get_geometry();
			resizeGL(ww, hh);
		}
		void onContentRectChanged(const ARect* rect) {
			DEBUG_CALLBACK(this, "onContentRectChanged");
			LOGV("rect: %d %d %d %d\n", rect->left, rect->top, rect->right, rect->bottom);
		}
		void onInputQueueCreated(AInputQueue* queue) {
			DEBUG_CALLBACK(this, "onInputQueueCreated");
			assert(queue);
			auto looper=ALooper_forThread();
			if(!looper)
				throw;
			AInputQueue_attachLooper(queue, looper, 999, input_callback, this);
			addInput(queue);
		}
		void onInputQueueDestroyed(AInputQueue* queue) {
			DEBUG_CALLBACK(this, "onInputQueueDestroyed");
			assert(queue);
			removeInput(queue);
			AInputQueue_detachLooper(queue);
		}
		void onConfigurationChanged() {
			DEBUG_CALLBACK(this, "onConfigurationChanged");
			assert(_config);
			auto prev=_config;
			_config=AConfiguration_new();
			AConfiguration_fromAssetManager(_config, _activity->assetManager);
			auto diff=AConfiguration_diff(prev, _config);
			AConfiguration_delete(prev);
			printConfig(_config);
			LOGV("config_diff: %08x", diff);
			if((diff&ACONFIGURATION_SCREEN_SIZE) && surface!=EGL_NO_SURFACE) {
				auto [ww, hh]=get_geometry();
				resizeGL(ww, hh);
			}
		}

		void onLowMemory() {
			DEBUG_CALLBACK(this, "onLowMemory\n");
			// XXX release resources
		}

		void init_display();
		void init_surface(ANativeWindow* window);
		void kill_surface(ANativeWindow* window);
		void update() {
			if(_pending_update.fetch_add(1)==0)
				boost::asio::post(_ui_ctx, [this]() { draw_frame(); });
		}
		void draw_frame_impl();
		void draw_frame() {
			if(surface==EGL_NO_SURFACE)
				return;
			if(_pending_update.fetch_and(0)==0)
				return;
			draw_frame_impl();
		}
		void draw_frame_simple(int code) {
			glClearColor(code&1, (code>>1)&1, (code>>2)&1, 1.0);
			glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
			glClearColor(0.0, 0.0, 0.0, 1.0);
			if(!eglSwapBuffers(display, surface))
				throw;
		};
		double get_dpr() {
			auto d=AConfiguration_getDensity(_config);
			switch(d) {
				case ACONFIGURATION_DENSITY_DEFAULT:
				case ACONFIGURATION_DENSITY_LOW:
				case ACONFIGURATION_DENSITY_ANY:
				case ACONFIGURATION_DENSITY_NONE:
					return 1.0;
				default:
					break;
			}
			auto r=d/double{ACONFIGURATION_DENSITY_MEDIUM};
			if(r<1.0)
				return 1.0;
			return r;
		}
		std::array<EGLint, 2> get_geometry() {
			assert(surface!=EGL_NO_SURFACE);
#if 1
			EGLint w, h;
			if(!eglQuerySurface(display, surface, EGL_WIDTH, &w))
				throw;
			if(!eglQuerySurface(display, surface, EGL_HEIGHT, &h))
				throw;
#else
			auto w=ANativeWindow_getWidth(_window);
			auto h=ANativeWindow_getHeight(_window);
#endif
			LOGV("surface: %d %d", w, h);
			return {w, h};
		}

		static void printConfig(AConfiguration* config) {
			[[maybe_unused]] auto d=AConfiguration_getDensity(config);
			[[maybe_unused]] auto w=AConfiguration_getScreenWidthDp(config);
			[[maybe_unused]] auto h=AConfiguration_getScreenHeightDp(config);
			LOGV("whd: %d %d %d", w, h, d);
		}
		void addInput(AInputQueue* queue) {
			assert(!_input);
			_input=queue;
		}
		void removeInput(AInputQueue* queue) {
			assert(_input && _input==queue);
			_input=nullptr;
		}
		void handleInput();
		void load_catalog(gapr::mem_file&& file);

		explicit Engine(ANativeActivity* activity);
		~Engine();

		void dump_infos(std::ostream& os) {
			os<<_egl_info;
			os<<_gles_info;
		}

	private:
		ANativeActivity* _activity;
		AConfiguration* _config{nullptr};
		AInputQueue* _input{nullptr};
		ANativeWindow* _window{nullptr};

		enum { APP_STOPPED, APP_PAUSED, APP_ACTIVE } _app_state;
		enum {
			PROG_INIT,
			PROG_CLOSED,
			PROG_OPENING,
			PROG_READY,
			PROG_BUSY,
			PROG_ERROR
		} _prog_state;

		std::thread _io_thread{};
		boost::asio::any_io_executor _work;
		boost::asio::io_context _io_ctx{1};
		boost::asio::thread_pool _pool{std::thread::hardware_concurrency()};
		boost::asio::ssl::context _ssl_ctx{boost::asio::ssl::context::tls};
		LooperContext _ui_ctx;

		std::optional<Args> _args;
		using resolver=boost::asio::ip::tcp::resolver;
		std::optional<resolver> _resolver;
		resolver::results_type _addrs{};
		resolver::endpoint_type _addr{boost::asio::ip::address{}, 0};
		gapr::client_end _cur_conn{};
		//client_end _conn_need_pw{};
		bool _initialized{false};
		std::string _data_secret;
		std::string _srv_info;
		gapr::tier _tier;
		std::string _gecos;
		gapr::stage _stage;
		gapr::tier _tier2;

		gapr::trace_api api;
		gapr::edge_model _model;
		gapr::commit_history _hist;
		uint64_t _latest_commit;

		std::vector<gapr::cube_info> _cube_infos;
		bool _states_valid{false};
		Position _cur_pos, _tgt_pos;
		gapr::anchor_id _cur_anchor, _tgt_anchor;
		FixPos _cur_fix, _tgt_fix;
		std::vector<std::array<double, 4>> _xfunc_states;

		gapr::cube_builder _cube_builder{_ui_ctx.get_executor(), _pool};

		EGLDisplay display{EGL_NO_DISPLAY};
		EGLContext context{EGL_NO_CONTEXT};
		EGLConfig _egl_config{0};
		EGLSurface surface{EGL_NO_SURFACE};
		std::atomic<int> _pending_update{0};
		bool _redraw_all{true};

		std::string _egl_info;
		std::string _gles_info;

		std::shared_ptr<VertexArray> _vao{};
		GLuint _tex{0};
		boost::qvm::mat<GLfloat, 4, 4> _mat_inv;
		gapr::cube _cube_data;
		float _mouse_y;


		void setState(decltype(_app_state) state) {
			_app_state=state;
			LOGI("app state: %d", state);
		}
		static int input_callback(int fd, int events, void* data) {
			assert(data);
			auto engine=static_cast<Engine*>(data);
			if(events&ALOOPER_EVENT_INPUT) {
				engine->handleInput();
			}
			return 1;
		}
		void display_error(const char* err) {
			_prog_state=PROG_ERROR;
			call_java(-2, err);
		}
		void display_login(const char* err) {
			if(err) {
				_prog_state=PROG_CLOSED;
				call_java(-1, err);
			} else {
				_prog_state=PROG_CLOSED;
				open_repository();
			}
		}
		int call_java(int action, const char* arg) {
			LOGV("------calljava %d %s", action, arg);
			auto env=_activity->env;
			auto klass=env->GetObjectClass(_activity->clazz);
			auto method=env->GetMethodID(klass, "call_java", "(ILjava/lang/String;)I");
			auto arg1=env->NewStringUTF(arg);
			auto res=env->CallIntMethod(_activity->clazz, method, action, arg1);
			return res;
		}




		const Position& pick() const noexcept { return _pick_pos; }


		int scale_factor() const noexcept { return _scale_factor; }
		std::array<unsigned int, 2> slice_pars() const noexcept {
			return _slice_pars;
		}
		double closeup_zoom() const noexcept { return _closeup_zoom; }
		const std::array<double, 6>& directions() const noexcept {
			return _direction;
		}
		//get colors...;


		void resizeGL(int w, int h);
		void handleMotion(const AInputEvent* event);
		void apply_changes_stage1();
		void edges_removed(const std::vector<edge_model::edge_id>& edges_del);
		//void handle_screen_changed(QScreen* screen);

		gapr::affine_xform _closeup_xform;
		std::array<unsigned int, 3> _closeup_sizes;
		std::array<unsigned int, 3> _closeup_cube_sizes;
		gapr::cube _closeup_cube;
		std::string _closeup_uri;
		std::array<double, 2> _closeup_xfunc;
		std::array<unsigned int, 3> _closeup_offset;
	std::size_t _closeup_ch{0};
	std::array<double, 6> _filter_bbox;

		const gapr::edge_model* _model2{nullptr};
		Position _pick_pos;
		std::vector<edge_model::point> _path_data;

		int _scale_factor{0};
		bool _slice_mode{false};
		std::array<unsigned int, 2> _slice_pars{480, 480};
		double _closeup_zoom;
		std::array<double, 6> _direction{0, -1.0, 0, 0, 0, -1.0};
		bool _data_only{false};

		enum class chg: unsigned int {
			closeup_info,
			closeup_cube,
			closeup_xfunc,
			model,
			model_update,
			cur_pos,
			tgt_pos,
			cursor,
			view_mode,
			path_data,
			scale_factor,
			slice_mode,
			slice_pars,
			closeup_zoom,
			direction,
			data_only,
			dpr,
			slice_delta,
			sel,
		};
		template<chg... c> constexpr unsigned int static
			mask=(0u | ... | (1u<<static_cast<unsigned int>(c)));
		unsigned int _changes0{0};

	void apply_changes() {
		if(_changes0)
			apply_changes_stage1();
	}
	void set_closeup_info(const gapr::affine_xform& xform, std::array<unsigned int, 3> sizes, std::array<unsigned int, 3> cube_sizes, std::array<unsigned int, 3> offset) {
		//loc off sizes
		//when apply
		//clearA
		_closeup_xform=xform;
		_closeup_sizes=sizes;
		_closeup_cube_sizes=cube_sizes;
		_changes0|=mask<chg::closeup_info>;
	}
	void set_closeup_cube(gapr::cube cube, std::string&& uri, std::array<unsigned int, 3> offset) {
		//when cube ready
		//showB
		_closeup_cube=std::move(cube);
		_closeup_uri=std::move(uri);
		_closeup_offset=offset;
		_changes0|=mask<chg::closeup_cube>;
	}
	void set_closeup_xfunc(std::array<double, 2> xfunc) {
		_closeup_xfunc=xfunc;
		_changes0|=mask<chg::closeup_xfunc>;
	}
	void set_model(const gapr::edge_model& model) {
		_model2=&model;
		_changes0|=mask<chg::model>;
	}
	void set_current(const Position& pos) {
		_cur_pos=pos;
		_changes0|=mask<chg::cur_pos>;
	}
	void set_target(const Position& pos) {
		_tgt_pos=pos;
		_changes0|=mask<chg::tgt_pos>;
	}
	void set_cursor(double x, double y, bool show) {
		if(x<-1)
			x=-1;
		if(x>1)
			x=1;
		if(y<-1)
			y=-1;
		if(y>1)
			y=1;
		_cursor_x=x;
		_cursor_y=y;
		_cursor_show=show;
		_changes0|=mask<chg::cursor>;
	}
	void set_path_data(std::vector<edge_model::point>&& points) {
		if(points.size()<2)
			points.clear();
		if(_path_data.empty() && points.empty())
			return;
		_path_data=std::move(points);
		_changes0|=mask<chg::path_data>;
	}
	void set_scale_factor(int factor) {
		if(_scale_factor==factor)
			return;
		_scale_factor=factor;
		_changes0|=mask<chg::scale_factor>;
	}
	void set_slice_mode(bool enabled) {
		_slice_mode=enabled;
		_changes0|=mask<chg::slice_mode>;
	}
	void set_slice_pars(std::array<unsigned int, 2> pars) {
		_slice_pars=pars;
		_changes0|=mask<chg::slice_pars>;
	}
	void set_closeup_zoom(double zoom) {
		_closeup_zoom=zoom;
		_changes0|=mask<chg::closeup_zoom>;
	}
	void set_directions(const std::array<double, 6>& dirs) {
		_direction=dirs;
		_changes0|=mask<chg::direction>;
	}
	void set_data_only(bool data_only) {
		_data_only=data_only;
		_changes0|=mask<chg::data_only>;
	}
	inline void clear_selection() noexcept {
		if(!_nodes_sel.empty()) {
			_nodes_sel.clear();
			_changes0|=mask<chg::sel>;
		}
	}
	inline void set_selection(std::vector<std::pair<gapr::node_id, gapr::node_attr>>&& nodes_sel) {
		_nodes_sel=std::move(nodes_sel);
		_changes0|=mask<chg::sel>;
	}
	void model_changed(const std::vector<edge_model::edge_id>& edges_del) {
		_changes0|=mask<chg::model_update>;
		if(!edges_del.empty())
			edges_removed(edges_del);
	}

	void critical_error_cb(int result);
	void ask_password_cb(int result);
	void show_retry_dlg_cb(int result);
	void cubeFinished(std::error_code ec, int progr);

	void on_xfunc_closeup_changed(double low, double up);
	void on_select_closeup_currentIndexChanged(int index);

	void on_select_scale_currentIndexChanged(int index);
	void on_total_slices_valueChanged(int value);
	void on_shown_slices_valueChanged(int value);
	void on_quality_button_box_accepted();
	void on_end_as_dialog_finished(int result);
	void on_end_as_dialog_finished2();
	void on_login_dialog_finished(int result);

	void on_canvas_ready(std::error_code err);
	void on_canvas_pick_changed();
	//void on_canvas_customContextMenuRequested(const QPoint& pos);

	void on_file_open_triggered();
	void on_file_close_triggered();
	void on_file_launch_triggered();
	void on_file_options_triggered();
	void on_file_quit_triggered();

	void on_goto_target_triggered();
	void on_pick_current_triggered();
	void on_goto_next_node_triggered();
	void on_goto_next_cube_triggered();
	void on_neuron_create_triggered();
	void on_report_error_triggered();

	void on_view_refresh_triggered();
	void on_view_slice_toggled(bool checked);
	void on_view_data_only_toggled(bool checked);
	void on_view_config_triggered();

	void on_tracing_connect_triggered();
	void on_tracing_extend_triggered();
	void on_tracing_branch_triggered();
	void on_tracing_end_triggered();
	void on_tracing_end_as_triggered();
	void on_tracing_delete_triggered();
	void on_tracing_examine_triggered();

	void on_help_manual_triggered();
	void on_help_about_triggered();

	void critical_error(const std::string& err, const std::string& info, const std::string& detail);
	/*
	void ask_password(const QString& str, const QString& err);
	void show_message(const QString& str);
	void show_retry_dlg(const QString& err, const QString& info, const QString& detail);
	*/

	void enter_stage0();
	void enter_stage1();
	void enter_stage2();
	void enter_stage3();

	/*
	void closeEvent(QCloseEvent* event) override;
	void changeEvent(QEvent* event) override;
	*/

	//Ui::Window _ui;
	//Ui::DisplayDialog _ui_display;
	//QDialog* _dlg_display{nullptr};
	//QMenu* _popup_canvas{nullptr};

	//std::unique_ptr<EndAsDialog> _dlg_end_as{};
	//std::unique_ptr<gapr::PasswordDialog> _dlg_pw{};
	//std::unique_ptr<gapr::login_dialog> _dlg_login{};

	std::size_t _list_sel{SIZE_MAX};


	//XXX state_section;
	std::atomic<bool> _cancel_flag{false};
	PreLock _prelock_model{}, _prelock_path{};
	gapr::delta_add_edge_ _path;
	std::vector<std::pair<gapr::node_id, gapr::node_attr>> _nodes_sel;

	std::default_random_engine rng{static_cast<unsigned int>(std::time(nullptr))};

	void setupUi();

	std::array<double, 2> calc_xfunc(std::size_t ch);
	void calc_center(Position& pos, std::size_t ch);
	double calc_default_zoom(std::size_t ch, unsigned int n);



	void xfunc_closeup_changed(double low, double up);
	void select_closeup_changed(int index);

	void startDownloadCube();
	void target_changed(const Position& pos, gapr::edge_model::view model);
	void pick_position();
	void pick_position(const gapr::fix::Position& pos);

	void jumpToPosition(const Position& pos, gapr::edge_model::view model);

	void critical_err(const std::string& err, const std::string& info, const std::string& detail) const;
	void get_passwd(const std::string& err) const;
	void show_message(const std::string& msg) const;
	void ask_retry(const std::string& err, const std::string& info, const std::string& detail) const;



	void got_passwd(std::string&& pw);




	void update_state(StateMask::base_type chg);

	bool check_connect();
	struct ConnectSt {
		std::atomic<bool> cancel_flag{false};
		Position cur_pos, tgt_pos;
		unsigned int method;
	};
	std::shared_ptr<ConnectSt> _prev_conn;

	template<typename T>
		bool model_prepare(gapr::node_id nid0, T&& delta) {
			//auto ex1=_pool.get_executor();
			//assert(ex1.running_in_this_thread());

			edge_model::loader loader{_model};
			return loader.load(nid0, std::move(delta));
		}
	bool model_prepare(gapr::mem_file file);

	void update_model_stats(gapr::edge_model::view model);

	Position fix_pos(const Position& pos, anchor_id anchor, gapr::edge_model::view model);
	Position fix_pos(const Position& pos, FixPos fix, gapr::edge_model::view model, gapr::node_id nid0);
	bool model_apply(bool last);
	bool load_commits(gapr::fiber_ctx& ctx, client_end msg, uint64_t upto);
	bool model_filter(gapr::fiber_ctx& ctx);
	bool model_filter(gapr::fiber_ctx& ctx, const std::array<double, 6>& bbox);
	bool load_model_state(gapr::fiber_ctx& ctx, client_end msg);
	gapr::future<bool> load_latest_commits();
	void latest_commits_loaded(gapr::likely<bool>&& res);
	gapr::future<bool> find_seed_pos();
	void present_cube(gapr::likely<bool>&& res);

	bool check_extend();

	enum class SubmitRes {
		Deny,
		Accept,
		Retry
	};
	template<gapr::delta_type Typ>
		std::pair<SubmitRes, std::string> submit_commit(gapr::fiber_ctx& ctx, gapr::delta<Typ>&& delta) {
			gapr::promise<gapr::mem_file> prom{};
			gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
			auto ex1=_pool.get_executor();
			ba::post(ex1, [&delta,prom=std::move(prom)]() mutable {
				gapr::mem_file payload;
				try {
					if(cannolize(delta)<0)
						throw;
					payload=serialize(delta);
					std::move(prom).set(std::move(payload));
				} catch(const std::runtime_error& e) {
					return unlikely(std::move(prom), std::current_exception());
				}
			});
			auto payload=std::move(fib2).async_wait(gapr::yield{ctx});

			auto msg=_cur_conn;
			gapr::fiber fib{ctx.get_executor(), api.commit(msg, Typ, std::move(payload), _hist.body_count(), _hist.tail_tip())};
			auto res=std::move(fib).async_wait(gapr::yield{ctx});
			auto [nid0, cmt_id, upd]=res;
			gapr::print("commit res: ", nid0, cmt_id, upd);
			auto r=load_commits(ctx, _cur_conn, upd);
			if(!r)
				return {SubmitRes::Deny, "err load"};
			if(!nid0) {
				if(cmt_id) {
					model_filter(ctx);
					return {SubmitRes::Retry, {}};
				} else {
					model_filter(ctx);
					return {SubmitRes::Deny, {}};
				}
			}
			gapr::promise<bool> prom2{};
			gapr::fiber fib3{ctx.get_executor(), prom2.get_future()};
			auto ex2=_pool.get_executor();
			ba::post(ex2, [this,nid0=nid0,prom2=std::move(prom2),&delta]() mutable {
				auto r=model_prepare(gapr::node_id{nid0}, delta);
				return std::move(prom2).set(r);
			});
			// XXX join edges
			if(!std::move(fib3).async_wait(gapr::yield{ctx}))
				return {SubmitRes::Deny, "err load2"};
			gapr::print("prepare ok");
			_hist.add_tail(cmt_id);
			model_filter(ctx);
			return {SubmitRes::Accept, {}};
		}

	gapr::future<std::pair<SubmitRes, std::string>> start_extend();
	gapr::future<std::pair<SubmitRes, std::string>> start_branch();
	void finish_extend(gapr::likely<std::pair<SubmitRes, std::string>>&& res);

	bool check_refresh();

	bool do_refresh(gapr::fiber_ctx& ctx);
	gapr::future<bool> start_refresh();
	void finish_refresh(gapr::likely<bool>&& res);

	gapr::node_id get_seed_pos_random_edg(gapr::edge_model::view model);
	gapr::node_id get_seed_pos_random(gapr::edge_model::view model);
	bool filter_next_pos2(gapr::node_attr pos) const noexcept;
	bool filter_next_pos(gapr::node_attr pos) const noexcept;
	std::unordered_set<gapr::node_id> _next_mask;
	gapr::node_id get_next_pos(gapr::edge_model::view model, const std::unordered_set<gapr::node_id>& mask, gapr::node_attr pos);
	gapr::node_id get_next_pos_edg(gapr::edge_model::view model, gapr::node_id cur, bool mask_cur, gapr::node_attr::data_type _pos);
	gapr::node_id get_next_pos(gapr::edge_model::view model, gapr::node_id cur, bool mask_cur, gapr::node_attr::data_type pos);

	bool check_terminal();
	gapr::future<std::pair<SubmitRes, std::string>> start_terminal();
	gapr::future<std::pair<SubmitRes, std::string>> start_end_as(std::string&& val);
	void finish_terminal(gapr::likely<std::pair<SubmitRes, std::string>>&& res);
	struct sub_edge {
		gapr::edge_model::edge_id edge;
		uint32_t index0, index1;
	};
	sub_edge get_sub_edge();
	bool check_delete();
	gapr::future<std::pair<SubmitRes, std::string>> start_delete();
	void finish_delete(gapr::likely<std::pair<SubmitRes, std::string>>&& res);
	void jump_next_node();
	void jump_next_cube();

	bool check_neuron_create();

	gapr::future<std::pair<SubmitRes, std::string>> start_neuron_create(std::string&& name);

	void finish_neuron_create(gapr::likely<std::pair<SubmitRes, std::string>>&& res);

	bool check_report_error();

	gapr::future<std::pair<SubmitRes, std::string>> start_report_error(std::string&& name);

	void finish_report_error(gapr::likely<std::pair<SubmitRes, std::string>>&& res);


	bool check_examine();

	gapr::future<std::pair<SubmitRes, std::string>> start_examine();
	void finish_examine(gapr::likely<std::pair<SubmitRes, std::string>>&& res);
	
	bool funcs_ok{false};
	ViewerShared* viewerShared{nullptr};

	GLsizei fbo_width, fbo_height;
	GLsizei fbo_width_alloc, fbo_height_alloc;

	Framebuffer fbo_opaque{};
	FramebufferEdges fbo_edges{};
	Framebuffer fbo_cubes{};
	Framebuffer fbo_scale{};

	std::shared_ptr<Program> _prog_sort;
	std::shared_ptr<Program> _prog_edge;
	GLint _prog_edge_id;
	GLint _prog_edge_view;
	GLint _prog_edge_proj;
	GLint _prog_edge_thick;
	GLint _prog_edge_offset;
	GLint _prog_edge_color0;
	GLint _prog_edge_color1;
	std::shared_ptr<Program> _prog_cursor;
	GLint _prog_cursor_proj;
	GLint _prog_cursor_center;
	GLint _prog_cursor_size;
	GLint _prog_cursor_color;
	std::shared_ptr<Program> _prog_vert;
	GLint _prog_vert_offset;
	GLint _prog_vert_view;
	GLint _prog_vert_proj;
	GLint _prog_vert_center;
	GLint _prog_vert_size;
	GLint _prog_vert_color;
	GLint _prog_vert_nid;
	GLint _prog_vert_idx;
	std::shared_ptr<Program> _prog_line;
	GLint _prog_line_thick;
	GLint _prog_line_offset;
	GLint _prog_line_proj;
	GLint _prog_line_view;
	GLint _prog_line_color;
	std::shared_ptr<Program> _prog_mark;
	GLint _prog_mark_offset;
	GLint _prog_mark_view;
	GLint _prog_mark_proj;
	GLint _prog_mark_center;
	GLint _prog_mark_size;
	GLint _prog_mark_color;
	std::shared_ptr<Program> _prog_volume;
	GLint _volume_xfunc, _volume_mat_inv, _volume_zpars, _volume_color;

	int pbiPath{0}, pbiFixed{0};
	std::shared_ptr<VertexArray> _vao_fixed;

	std::vector<PathBuffer> pathBuffers;

	vec3<GLfloat> colors[COLOR_NUM];

	//Inset specific
	double _inset_zoom;
	std::array<double, 3> _inset_center;
	//asis inset (closeup)

	//MVP
	struct Matrices {
		mat4<GLfloat> mView, mrView, mProj, mrProj;
		mat4<GLfloat> cube_mat;
	} _mat;

	double global_min_voxel;
	double global_max_dim;
	int global_cube_tex{0};

	double closeup_min_voxel;
	double closeup_max_dim;
	std::shared_ptr<Texture3D> closeup_cube_tex;

	/////////////////////////////////////

	double _dpr;
	int _surface_w, _surface_h;

	// Volume
	int slice_delta{0};

	std::unordered_map<int32_t, MouseState> pointers;
	float _cursor_x, _cursor_y;
	float _prev_cursor_x, _prev_cursor_y;
	bool _cursor_show;
	std::vector<int32_t> control_areas;
	mat4<> mrViewProj;
	vec3<> _prev_up, _prev_rgt;
	double _prev_xfunc, _prev_xfunc2;

	//need? init? cfg control ctor/dtor/func
	vec3<> pickA{}, pickB{};
	//bool colorMode;
	unsigned int _changes1{0};


	int createPbi();
	void freePbi(int i);
	void paintFinish(GLuint fbo);
	std::unordered_map<edge_model::edge_id, int> _edge_vaoi;
	void paintEdge();
	void paintOpaqueInset();
	void paintOpaque();
	void paintBlank();

	void paintVolumeImpl(Matrices& mat, int tex, std::array<double, 2>& _xfunc, double min_voxel, double zoom);
	void paintVolume();

	void updateCubeMatrix();
	void updateInsetCenter();
	void updateZoomMinMax();
		void updateMVPMatrices();
	void updateCubeTexture();
	void updateInsetAxis();
	void resizeFrameBuffers();
	void set_dpr(double dpr);
	std::array<double, 6> handleRotate(const MouseState& state);
	double handleZoom(const MouseState& state);
	gapr::node_attr toGlobal(const vec3<>& p) const;
	vec3<> toLocal(const gapr::node_attr& p) const;

	/*! enable snapping, by *pixel. XXX not radius or distance.
	 * 
	 */
	bool pickPoint(float x, float y, Position& pos);
	void clearSelection();

};

Engine::Engine(ANativeActivity* activity):
	_activity{activity}, _app_state{APP_STOPPED}, _prog_state{PROG_INIT},
	_ui_ctx{ALooper_forThread()}
{
	assert(!activity->instance);
	activity->instance=this;
	_config=AConfiguration_new();
	AConfiguration_fromAssetManager(_config, _activity->assetManager);
	printConfig(_config);
	LOGV("internalDataPath %s", activity->internalDataPath);
	LOGV("externalDataPath %s", activity->externalDataPath);
	LOGV("obbPath %s", activity->obbPath);
	LOGV("sdkVersion %d", activity->sdkVersion);

	_work=boost::asio::require(_io_ctx.get_executor(), boost::asio::execution::outstanding_work.tracked);
	_io_thread=std::thread{[this]() {
		gapr::print("info: ", std::this_thread::get_id(), "begin run");
		[[maybe_unused]] auto n=_io_ctx.run();
		gapr::print("info: ", std::this_thread::get_id(), "end run");
		LOGV("processed %d messages", (int)n);
	}};

	PathBuffer pbHead;
	pbHead.prev_unused=0;
	pathBuffers.emplace_back(pbHead);
	// XXX color config
	for(int i=0; i<COLOR_NUM; i++) {
		colors[i]=colorData[i].def;
	}
}
void Engine::onDestroy() {
	if(_config)
		AConfiguration_delete(_config);
	if(_input)
		AInputQueue_detachLooper(_input);
	if(_window)
		kill_surface(_window);
	{
		auto x=std::move(_cur_conn);
	}
	_work={};
	_io_thread.join();
	_pool.join();
	if(1)
		gapr::print("info: ", std::this_thread::get_id(), "end");
}
Engine::~Engine() {
}

void Engine::init_display() {
	assert(display==EGL_NO_DISPLAY);
	assert(context==EGL_NO_CONTEXT);

	display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint major, minor;
	if(!eglInitialize(display, &major, &minor))
		throw std::runtime_error{"failed to initialize egl"};
	{
		std::ostringstream oss;
		oss<<"egl: "<<major<<'.'<<minor;
		for(auto n: {EGL_CLIENT_APIS, EGL_VENDOR, EGL_VERSION})
			oss<<"; "<<eglQueryString(display, n);
		oss<<'\n';
		_egl_info=oss.str();
	}

	const EGLint attribs[]={
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_ALPHA_SIZE, 0, //8,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_DEPTH_SIZE, 0,
		EGL_NONE
	};
	std::array<EGLConfig, 32> configs;
	EGLint num_config;
	if(!eglChooseConfig(display, attribs, configs.data(), configs.size(), &num_config))
		throw;
	_egl_config=configs[0];
	if(!_egl_config)
		throw;

	const EGLint ctx_attribs[]={
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 0,
		EGL_NONE
	};
	context=eglCreateContext(display, _egl_config, EGL_NO_CONTEXT, ctx_attribs);
	if(context==EGL_NO_CONTEXT)
		throw std::runtime_error{"failed to create egl context"};
}

void Engine::init_surface(ANativeWindow* window) {
	assert(!_window);
	_window=window;

	if(display==EGL_NO_DISPLAY) {
		init_display();
	}

	EGLint format;
	if(!eglGetConfigAttrib(display, _egl_config, EGL_NATIVE_VISUAL_ID, &format))
		throw;
	ANativeWindow_setBuffersGeometry(window, 0, 0, format);
	[[maybe_unused]] auto fmt=ANativeWindow_getFormat(window);
	LOGV("window format: %d %d", fmt, format);

	surface=eglCreateWindowSurface(display, _egl_config, window, nullptr);
	if(!eglMakeCurrent(display, surface, surface, context))
		throw std::runtime_error{"failed to make current"};
	{
		std::ostringstream oss;
		GLint maj, min;
		glGetIntegerv(GL_MAJOR_VERSION, &maj);
		glGetIntegerv(GL_MINOR_VERSION, &min);
		oss<<"gles: "<<maj<<'.'<<min;
		for(auto n: {GL_VENDOR, GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION}) {
			auto info=glGetString(n);
			oss<<"; "<<info;
		}
		for(auto s: {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER}) {
			for(auto p: {GL_LOW_FLOAT, GL_MEDIUM_FLOAT, GL_HIGH_FLOAT, GL_LOW_INT, GL_MEDIUM_INT, GL_HIGH_INT}) {
				GLint rng[2], prec;
				glGetShaderPrecisionFormat(s, p, rng, &prec);
				oss<<"; prec/"<<rng[0]<<'/'<<rng[1]<<'/'<<prec;
			}
		}
		{
			GLfloat range[2];
			for(auto p: {GL_ALIASED_LINE_WIDTH_RANGE, GL_ALIASED_POINT_SIZE_RANGE}) {
				glGetFloatv(p, range);
				oss<<"; "<<range[0]<<'/'<<range[1];
			}
		}
		{
			GLint f;
			for(auto p: {GL_IMPLEMENTATION_COLOR_READ_FORMAT,
				GL_IMPLEMENTATION_COLOR_READ_TYPE,
				GL_MAX_3D_TEXTURE_SIZE,
				GL_MAX_COLOR_ATTACHMENTS,
				GL_MAX_DRAW_BUFFERS,
				GL_MAX_ELEMENT_INDEX,
				GL_MAX_ELEMENTS_VERTICES,
				GL_MAX_SAMPLES,
				GL_MAX_TEXTURE_SIZE}) {
				glGetIntegerv(p, &f);
				oss<<"; "<<f;
			}
		}
		oss<<'\n';
		_gles_info=oss.str();
	}

	if(!viewerShared) {
		viewerShared=new ViewerShared{};
		viewerShared->create();
	}

	{
		_dpr=get_dpr();
		auto [ww, hh]=get_geometry();
		_surface_w=ww;
		_surface_h=hh;
		auto scale_factor=_scale_factor;
		fbo_width=(ww+scale_factor)/(1+scale_factor);
		fbo_height=(hh+scale_factor)/(1+scale_factor);

		fbo_width_alloc=(fbo_width+FBO_ALLOC)/FBO_ALIGN*FBO_ALIGN;
		fbo_height_alloc=(fbo_height+FBO_ALLOC)/FBO_ALIGN*FBO_ALIGN;

		auto create_or_resize=[w=fbo_width_alloc,h=fbo_height_alloc]
			(auto& f, auto& fbo0, auto&... fbos) {
				if(!fbo0) {
					LOGV("  create fbo");
					fbo0.create(w, h);
				} else {
					LOGV("  resize fbo");
					fbo0.resize(w, h);
				}
				if constexpr(sizeof...(fbos)>0) {
					f(f, fbos...);
				}
			};
		create_or_resize(create_or_resize,
				fbo_opaque, fbo_edges, fbo_cubes, fbo_scale);

		if(!pbiFixed) {
			pbiFixed=createPbi();
			glBindBuffer(GL_ARRAY_BUFFER, pathBuffers[pbiFixed].buffer());
			glBufferData(GL_ARRAY_BUFFER, (9*24+2)*sizeof(PointGL), nullptr, GL_STATIC_DRAW);
		}
		if(!pbiPath) {
			pbiPath=createPbi();
		}
		_vao_fixed=viewerShared->get_vao_fixed();
	}

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClearDepthf(1.0);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	//glActiveTexture(GL_TEXTURE1);
	//glEnable(GL_TEXTURE_3D);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	_vao=viewerShared->get_vao("vao.quad", [](VertexArray& vao) {
		struct Type {
			std::array<GLfloat, 2> pt;
		} data[]={
			{-1.0, -1.0},
			{1.0, -1.0},
			{-1.0, 1.0},
			{1.0, 1.0}
		};
		vao.create(&Type::pt);
		glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err buffer"};
	});

	auto assetMgr=_activity->assetManager;
	auto create_prog=[assetMgr](Program& prog, const char* vert_path, const char* frag_path) {
		gapr::gl::Shader<GlFuncs> vert, frag;
		AssetSource src{assetMgr};
		vert.create(src, GL_VERTEX_SHADER, vert_path);
		frag.create(src, GL_FRAGMENT_SHADER, frag_path);
		prog.create({vert, frag});
	};
	_prog_volume=viewerShared->get_prog("volume.mip", [create_prog,this](Program& prog) {
		LOGV("compiling volume.mip");
		create_prog(prog, "volume.vert", "volume.frag");
		auto _volume_tex=glGetUniformLocation(prog, "tex3d_cube");
		_volume_xfunc=glGetUniformLocation(prog, "xfunc_cube");
		_volume_mat_inv=glGetUniformLocation(prog, "mrTexViewProj");
		_volume_zpars=glGetUniformLocation(prog, "zparsCube");
		_volume_color=glGetUniformLocation(prog, "color_volume");
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc1"};
		glUseProgram(prog);
		glUniform1i(_volume_tex, 1);
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err set"};
	});
	_prog_edge=viewerShared->get_prog("proofread.edges", [create_prog,this](Program& prog) {
		LOGV("compiling proofread.edges");
		create_prog(prog, "edges.vert", "edges.frag");
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err link2"};
		_prog_edge_id=glGetUniformLocation(prog, "edge_id");
		_prog_edge_view=glGetUniformLocation(prog, "mView");
		_prog_edge_proj=glGetUniformLocation(prog, "mProj");
		_prog_edge_thick=glGetUniformLocation(prog, "thickness");
		_prog_edge_offset=glGetUniformLocation(prog, "pos_offset");
		_prog_edge_color0=glGetUniformLocation(prog, "colors[0]");
		_prog_edge_color1=glGetUniformLocation(prog, "colors[1]");
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc2"};
	});
	_prog_line=viewerShared->get_prog("proofread.line", [create_prog,this](Program& prog) {
		LOGV("compiling proofread.line");
		create_prog(prog, "line.vert", "line.frag");
		_prog_line_view=glGetUniformLocation(prog, "mView");
		_prog_line_proj=glGetUniformLocation(prog, "mProj");
		_prog_line_thick=glGetUniformLocation(prog, "thickness");
		_prog_line_offset=glGetUniformLocation(prog, "pos_offset");
		_prog_line_color=glGetUniformLocation(prog, "color");
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc3"};
	});
	_prog_cursor=viewerShared->get_prog("proofread.cursor", [create_prog,this](Program& prog) {
		LOGV("compiling proofread.cursor");
		create_prog(prog, "cursor.vert", "cursor.frag");
		_prog_cursor_proj=glGetUniformLocation(prog, "mProj");
		_prog_cursor_color=glGetUniformLocation(prog, "color");
		_prog_cursor_center=glGetUniformLocation(prog, "center");
		_prog_cursor_size=glGetUniformLocation(prog, "umpp");
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc4"};
	});
	_prog_mark=viewerShared->get_prog("proofread.mark", [create_prog,this](Program& prog) {
		LOGV("compiling proofread.mark");
		create_prog(prog, "mark.vert", "mark.frag");
		_prog_mark_offset=glGetUniformLocation(prog, "pos_offset");
		_prog_mark_view=glGetUniformLocation(prog, "mView");
		_prog_mark_proj=glGetUniformLocation(prog, "mProj");
		_prog_mark_color=glGetUniformLocation(prog, "color");
		_prog_mark_center=glGetUniformLocation(prog, "center");
		_prog_mark_size=glGetUniformLocation(prog, "umpp");
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc5"};
	});
	_prog_vert=viewerShared->get_prog("proofread.vert", [create_prog,this](Program& prog) {
		LOGV("compiling proofread.vert");
		create_prog(prog, "vert.vert", "vert.frag");
		_prog_vert_offset=glGetUniformLocation(prog, "pos_offset");
		_prog_vert_view=glGetUniformLocation(prog, "mView");
		_prog_vert_proj=glGetUniformLocation(prog, "mProj");
		_prog_vert_color=glGetUniformLocation(prog, "color");
		_prog_vert_center=glGetUniformLocation(prog, "center");
		_prog_vert_size=glGetUniformLocation(prog, "umpp");
		_prog_vert_nid=glGetUniformLocation(prog, "nid");
		_prog_vert_idx=glGetUniformLocation(prog, "idx");
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc6"};
	});
	_prog_sort=viewerShared->get_prog("proofread.sort", [create_prog](Program& prog) {
		LOGV("compiling proofread.sort");
		create_prog(prog, "sort.vert", "sort.frag");
		auto edg_depth=glGetUniformLocation(prog, "tex_edges_depth");
		auto edg_color=glGetUniformLocation(prog, "tex_edges_color");
		auto opaque_depth=glGetUniformLocation(prog, "tex_opaque_depth");
		auto opaque_color=glGetUniformLocation(prog, "tex_opaque_color");
		auto cube_depth=glGetUniformLocation(prog, "tex_volume0_depth");
		auto cube_color=glGetUniformLocation(prog, "tex_volume0_color");
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc7"};
		glUseProgram(prog);
		glUniform1i(edg_depth, 2);
		glUniform1i(edg_color, 3);
		glUniform1i(opaque_depth, 4);
		glUniform1i(opaque_color, 5);
		glUniform1i(cube_depth, 6);
		glUniform1i(cube_color, 7);
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err set"};
	});


	//////////////////////////////////////////////////////////////
	//...
	
}
void Engine::kill_surface(ANativeWindow* window) {
	assert(_window==window);
	_prog_volume={};
	if(surface!=EGL_NO_SURFACE) {
		if(!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT))
			throw;
		if(!eglDestroySurface(display, surface))
			throw;
		surface=EGL_NO_SURFACE;
	}
	///////////////////////////////////////////
#if 0
	if(context!=EGL_NO_CONTEXT) {
		//if(!eglDestroyContext(display, context))
			//throw;
		//context=EGL_NO_CONTEXT;
	}
	if(display!=EGL_NO_DISPLAY) {
		if(!eglTerminate(display))
			throw;
		display=EGL_NO_DISPLAY;
	}
#endif
	_window=nullptr;
}
static void dump_key_event(const AInputEvent* event) {
	[[maybe_unused]] auto act=AKeyEvent_getAction(event);
	[[maybe_unused]] auto flags=AKeyEvent_getFlags(event);
	[[maybe_unused]] auto code=AKeyEvent_getKeyCode(event);
	[[maybe_unused]] auto scan_code=AKeyEvent_getScanCode(event);
	[[maybe_unused]] auto meta_state=AKeyEvent_getMetaState(event);
	[[maybe_unused]] auto rep=AKeyEvent_getRepeatCount(event);
	[[maybe_unused]] auto down_time=AKeyEvent_getDownTime(event);
	[[maybe_unused]] auto event_time=AKeyEvent_getEventTime(event);
	LOGV("act=%x flags=%x code=%x scan_code=%x meta_state=%x"
			"rep=%d down_time=%" PRId64 " event_time=%" PRId64, act, flags, code,
			scan_code, meta_state, rep, down_time, event_time);
}
static void dump_motion_event(const AInputEvent* event) {
	[[maybe_unused]] auto act=AMotionEvent_getAction(event);
	[[maybe_unused]] auto flags=AMotionEvent_getFlags(event);
	[[maybe_unused]] auto meta_state=AMotionEvent_getMetaState(event);
	[[maybe_unused]] auto btn_state=AMotionEvent_getButtonState(event);
	[[maybe_unused]] auto edg_flags=AMotionEvent_getEdgeFlags(event);
	[[maybe_unused]] auto down_time=AMotionEvent_getDownTime(event);
	[[maybe_unused]] auto event_time=AMotionEvent_getEventTime(event);
	[[maybe_unused]] auto xoffset=AMotionEvent_getXOffset(event);
	[[maybe_unused]] auto yoffset=AMotionEvent_getYOffset(event);
	[[maybe_unused]] auto xprec=AMotionEvent_getXPrecision(event);
	[[maybe_unused]] auto yprec=AMotionEvent_getYPrecision(event);
	LOGV("act=%x flags=%x meta_state=%x btn_state=%x edg_flags=%x"
			"down_time=%" PRId64 " event_time=%" PRId64 " xoffset=%f yoffset=%f"
			"xprec=%f yprec=%f", act, flags, meta_state, btn_state,
			edg_flags, down_time, event_time, xoffset, yoffset,
			xprec, yprec);
	auto N=AMotionEvent_getPointerCount(event);
	for(std::size_t i=0; i<N; i++) {
		[[maybe_unused]] auto id=AMotionEvent_getPointerId(event, i);
		[[maybe_unused]] auto tool=AMotionEvent_getToolType(event, i);
		[[maybe_unused]] auto rawx=AMotionEvent_getRawX(event, i);
		[[maybe_unused]] auto rawy=AMotionEvent_getRawY(event, i);
		[[maybe_unused]] auto x=AMotionEvent_getX(event, i);
		[[maybe_unused]] auto y=AMotionEvent_getY(event, i);
		[[maybe_unused]] auto pressure=AMotionEvent_getPressure(event, i);
		[[maybe_unused]] auto size=AMotionEvent_getSize(event, i);
		[[maybe_unused]] auto major=AMotionEvent_getTouchMajor(event, i);
		[[maybe_unused]] auto minor=AMotionEvent_getTouchMinor(event, i);
		[[maybe_unused]] auto tool_major=AMotionEvent_getToolMajor(event, i);
		[[maybe_unused]] auto tool_minor=AMotionEvent_getToolMinor(event, i);
		[[maybe_unused]] auto orient=AMotionEvent_getOrientation(event, i);
		LOGV("%d: id=%d tool=%x rawx=%f rawy=%f x=%f y=%f pressure=%f"
				"size=%f major=%f minor=%f tool_major=%f tool_minor=%f"
				"orient=%f", (int)i, id, tool, rawx, rawy, x, y,
				pressure, size, major, minor, tool_major, tool_minor,
				orient);
	}
	auto M=AMotionEvent_getHistorySize(event);
	for(std::size_t i=0; i<M; i++) {
		[[maybe_unused]] auto time=AMotionEvent_getHistoricalEventTime(event, i);
		[[maybe_unused]] auto x0=AMotionEvent_getHistoricalX(event, 0, i);
		[[maybe_unused]] auto y0=AMotionEvent_getHistoricalY(event, 0, i);
		LOGV("hist %d: time=%" PRId64 " x0=%f y0=%f", (int)i, time, x0, y0);
	}
	//float AMotionEvent_getHistoricalRawX(const AInputEvent* motion_event, size_t pointer_index, size_t history_index);
	//float AMotionEvent_getHistoricalRawY(const AInputEvent* motion_event, size_t pointer_index, size_t history_index);
	//float AMotionEvent_getHistoricalPressure(const AInputEvent* motion_event, size_t pointer_index, size_t history_index);
	//float AMotionEvent_getHistoricalSize(const AInputEvent* motion_event, size_t pointer_index, size_t history_index);
	//float AMotionEvent_getHistoricalTouchMajor(const AInputEvent* motion_event, size_t pointer_index, size_t history_index);
	//float AMotionEvent_getHistoricalTouchMinor(const AInputEvent* motion_event, size_t pointer_index, size_t history_index);
	//float AMotionEvent_getHistoricalToolMajor(const AInputEvent* motion_event, size_t pointer_index, size_t history_index);
	//float AMotionEvent_getHistoricalToolMinor(const AInputEvent* motion_event, size_t pointer_index, size_t history_index);
	//float AMotionEvent_getHistoricalOrientation(const AInputEvent* motion_event, size_t pointer_index, size_t history_index);
}
void Engine::handleInput() {
	AInputEvent* event{nullptr};
	while(AInputQueue_getEvent(_input, &event)>=0) {
		auto type=AInputEvent_getType(event);
		[[maybe_unused]] auto devid=AInputEvent_getDeviceId(event);
		[[maybe_unused]] auto source=AInputEvent_getSource(event);
		LOGV("type=%x, devid=%x, source=%x\n", type, devid, source);
		if(AInputQueue_preDispatchEvent(_input, event)!=0)
			continue;
		int handled{0};
		switch(type) {
			default:
				break;
			case AINPUT_EVENT_TYPE_KEY:
				if(1)
					dump_key_event(event);
				break;
			case AINPUT_EVENT_TYPE_MOTION:
				if(0)
					dump_motion_event(event);
				handleMotion(event);
				handled=1;
				break;
		}
		AInputQueue_finishEvent(_input, event, handled);
	}
}
	//////////////////////



void Engine::draw_frame_impl() {
	//assert(has context && viewport set);

	switch(_prog_state) {
		case PROG_INIT:
			LOGE("window ready when init?");
			return;
		case PROG_CLOSED:
		case PROG_OPENING:
		case PROG_ERROR:
			paintBlank();
			break;
		case PROG_READY:
		case PROG_BUSY:
			if(_redraw_all) {
				paintEdge();
				paintOpaque();
				paintVolume();
				_redraw_all=false;
			}
			paintFinish(0);
			break;
		default:
			break;
	}

	if(!eglSwapBuffers(display, surface))
		throw;
}

namespace {
	template<auto MFP>
		struct CbWrap;
	template<typename... Args, void (Engine::*MFP)(Args...)>
		struct CbWrap<MFP> {
			static void f(ANativeActivity* activity, Args... args) {
				assert(activity);
				if(auto engine=static_cast<Engine*>(activity->instance))
					(engine->*MFP)(args...);
			}
		};
}

static std::shared_ptr<Engine> _engine_instance{};
Engine* Engine::instance() {
	return _engine_instance.get();
}

static void DEBUG_CALLBACK(const char* tag) {
	LOGV("---------%s", tag);
}
static void onDestroy(ANativeActivity* activity) {
	assert(activity);
	DEBUG_CALLBACK("onDestroy");
	if(auto engine=static_cast<Engine*>(activity->instance)) {
		assert(_engine_instance.get()==engine);
		engine->onDestroy();
		_engine_instance={};
	}
}
static void* onSaveInstanceState(ANativeActivity* activity, size_t* outSize) {
	assert(activity);
	DEBUG_CALLBACK("onSaveInstanceState");
	if(auto engine=static_cast<Engine*>(activity->instance))
		return engine->onSaveInstanceState(outSize);
	return nullptr;
}

extern "C" void create_native_activity(ANativeActivity* activity, void* savedState, size_t savedStateSize);
JNIEXPORT void create_native_activity(ANativeActivity* activity, void* savedState, size_t savedStateSize) {
	LOGV("---------onCreate: %p\n", activity);
	assert(activity);
	assert(!(savedStateSize && !savedState));
	assert(!activity->instance);

	auto callbacks=activity->callbacks;
	callbacks->onDestroy=onDestroy;
	callbacks->onStart=&CbWrap<&Engine::onStart>::f;
	callbacks->onStop=&CbWrap<&Engine::onStop>::f;
	callbacks->onResume=&CbWrap<&Engine::onResume>::f;
	callbacks->onPause=&CbWrap<&Engine::onPause>::f;
	callbacks->onSaveInstanceState=onSaveInstanceState;
	callbacks->onNativeWindowCreated=&CbWrap<&Engine::onNativeWindowCreated>::f;
	callbacks->onNativeWindowDestroyed=&CbWrap<&Engine::onNativeWindowDestroyed>::f;
	callbacks->onWindowFocusChanged=&CbWrap<&Engine::onWindowFocusChanged>::f;
	callbacks->onNativeWindowRedrawNeeded=&CbWrap<&Engine::onNativeWindowRedrawNeeded>::f;
	callbacks->onNativeWindowResized=&CbWrap<&Engine::onNativeWindowResized>::f;
	callbacks->onContentRectChanged=&CbWrap<&Engine::onContentRectChanged>::f;
	callbacks->onInputQueueCreated=&CbWrap<&Engine::onInputQueueCreated>::f;
	callbacks->onInputQueueDestroyed=&CbWrap<&Engine::onInputQueueDestroyed>::f;
	callbacks->onConfigurationChanged=&CbWrap<&Engine::onConfigurationChanged>::f;
	callbacks->onLowMemory=&CbWrap<&Engine::onLowMemory>::f;

	assert(!_engine_instance);
	_engine_instance=std::make_shared<Engine>(activity);

	if(savedState) {
		// XXX make copy
	}

	ANativeActivity_hideSoftInput(activity, ANATIVEACTIVITY_HIDE_SOFT_INPUT_IMPLICIT_ONLY|ANATIVEACTIVITY_HIDE_SOFT_INPUT_NOT_ALWAYS);
}

///////////
extern "C" JNIEXPORT jstring JNICALL
Java_goulf_gapr_Proofread_stringFromJNI(JNIEnv* env, jobject) {
	std::ostringstream oss;
	oss<<"hello "<<std::this_thread::get_id();
	return env->NewStringUTF(oss.str().c_str());
}

void Engine::set_args(Args&& args) {
	if(args.user=="trigger.error")
		return display_error("Test error");
	assert(_activity);
	assert(_prog_state==PROG_INIT);
	_args.emplace(std::move(args));
}
void Engine::open_repository() {
	assert(_activity);
	assert(_prog_state==PROG_CLOSED);
	_prog_state=PROG_OPENING;

	gapr::fiber fib{_io_ctx.get_executor(), [this](gapr::fiber_ctx& fib) -> int {
		{
			boost::system::error_code ec;
			auto addr=boost::asio::ip::make_address(_args->host, ec);
			if(!ec) {
				_addr.address(addr);
				_addr.port(_args->port);
			} else {
				if(!_resolver)
					_resolver.emplace(_io_ctx);
				std::error_code ec2;
				auto res=_resolver->async_resolve(_args->host, "0", gapr::yield{fib, ec2});
				if(ec2) {
					gapr::str_glue err{"Unable to look up `", _args->host, "': ", ec2.message()};
					// critical err
					boost::asio::post(_ui_ctx, [this,err=err.str()]() {
						display_login(err.c_str());
					});
					return -1;
				}
				assert(!res.empty());
				_addrs=res;
			}
		}

		if(_args->passwd.empty()) {
			//get_passwd({});
		}

		boost::asio::ip::tcp::socket sock{_io_ctx};
		if(!_addr.port()) {
			auto it=_addrs.begin();
			std::error_code ec;
			do {
				resolver::endpoint_type addr{it->endpoint().address(), _args->port};
				sock.async_connect(addr, gapr::yield{fib, ec});
				if(!ec)
					break;
				//gapr::str_glue msg{"Unable to connect to [", addr, "]: ", ec.message()};
				//show_message(msg.str());
				++it;
			} while(it!=_addrs.end());
			if(it==_addrs.end()) {
				gapr::str_glue err{"Unable to connect to `", _args->host, ':', _args->port, "': Tried each resolved address."};
				//ask retry
				boost::asio::post(_ui_ctx, [this,err=err.str()]() {
					display_login(err.c_str());
				});
				return -1;
			}
			_addr.address(it->endpoint().address());
			_addr.port(_args->port);
		} else {
			std::error_code ec;
			sock.async_connect(_addr, gapr::yield{fib, ec});
			if(ec) {
				gapr::str_glue err{"Unable to connect to [", _addr, "]: ", ec.message()};
				// ask retry
				boost::asio::post(_ui_ctx, [this,err=err.str()]() {
					display_login(err.c_str());
				});
				return -1;
			}
		}

		auto conn=gapr::client_end{std::move(sock), _ssl_ctx};
		{
			gapr::fiber fib2{fib.get_executor(), api.handshake(conn)};
			auto res=std::move(fib2).async_wait(gapr::yield{fib});
#if 0
			if(!res) try {
				auto ec=res.error();
				return ask_retry("Unable to handshake.", ec.message(), {});
			} catch(const std::runtime_error& e) {
				return critical_err("Unable to handshake.", e.what(), {});
			}
#endif
			_srv_info=std::move(res.banner);
		}

		do {
			gapr::fiber fib3{fib.get_executor(), api.login(conn, _args->user, _args->passwd)};
			auto res=std::move(fib3).async_wait(gapr::yield{fib});
#if 0
			if(!res) try {
				auto ec=res.error();
				return ask_retry("Login error: network failure.", ec.message(), {});
			} catch(const std::runtime_error& e) {
				return critical_err("Login error.", e.what(), {});
			}
#endif
			if(res.tier==gapr::tier::locked) {
				// critical error
				std::string err{"Login error: user locked."};
				boost::asio::post(_ui_ctx, [this,err=std::move(err)]() {
					display_login(err.c_str());
				});
				return -1;
			}
			if(res.tier<gapr::tier::locked) {
				_tier=res.tier;
				_gecos=std::move(res.gecos);
				break;
			}
#if 0
			//get_passwd(res.get().gecos);
			while(pw.empty()) {
				//get_passwd("empty.");
			}
			_args->passwd=std::move(pw);
#endif
		} while(true);

		// XXX
		// handle specific errors with exception mechanism
		// catch(gapr::client::network_error)
		// catch(gapr::client::server_error)
		// then a single network_error handler for the whole func
		// and server_error handler for each step
		{
			gapr::fiber fib4{fib.get_executor(), api.select(conn, _args->group)};
			auto res=std::move(fib4).async_wait(gapr::yield{fib});
#if 0
			if(!res) try {
				auto ec=res.error();
				return ask_retry("Select error: network failure.", ec.message(), {});
			} catch(const std::runtime_error& e) {
				return critical_err("Select error.", e.what(), {});
			}
#endif
			_latest_commit=res.commit_num;
			_data_secret=res.secret;
			_stage=res.stage;
			_tier2=res.tier2;
		}

		if(!_initialized) {
			gapr::fiber fib5{fib.get_executor(), api.get_catalog(conn)};
			auto res=std::move(fib5).async_wait(gapr::yield{fib});
#if 0
			if(!res) try {
				auto ec=res.error();
				return ask_retry("Unable to load catalog: network failure.", ec.message(), {});
			} catch(const std::runtime_error& e) {
				return critical_err("Unable to load catalog.", e.what(), {});
			}
#endif
			load_catalog(std::move(res.file));

			gapr::fiber fib6{fib.get_executor(), api.get_state(conn)};
			auto res2=std::move(fib6).async_wait(gapr::yield{fib});
#if 0
			if(!res) try {
				auto ec=res.error();
				return ask_retry("Unable to load state: network failure.", ec.message(), {});
			} catch(const std::runtime_error& e) {
				return critical_err("Unable to load state.", e.what(), {});
			}
#endif

#if 0
			if(file) {
				auto f=gapr::make_streambuf(std::move(file));
				std::istream str{f.get()};
				state_section=load_section(str, "fix"); //check ini format, and return section lines
			}
#endif
			_initialized=true;
		}
		_cur_conn=std::move(conn);

		boost::asio::post(_ui_ctx, [this]() mutable {
			open_repository_finished();
		});
		return 0;
	}};
}

void Engine::open_repository_finished() {
	{
		gapr::str_glue title{_args->user, '@', _args->host, ':', _args->port, '/', _args->group};
		_prog_state=PROG_BUSY;
		call_java(8, title.str().c_str());
	}
	if(0)
		return display_error("Test error 2");

	_cube_builder.async_wait([this](std::error_code ec, int progr) {
		return cubeFinished(ec, progr);
	});

	//_cube_builder->getBoundingBox(&bbox);
	//_cube_builder->getMinVoxelSize(&mvol);

	set_model(_model);
	_cur_anchor=gapr::anchor_id{gapr::node_id{1}, {}, 0};
	// XXX
	_tgt_anchor=gapr::anchor_id{gapr::node_id{4}, {}, 0};
	set_current(_cur_pos);

	// XXX parse states right after receiving
	// put parsing results in mem. var.
	// do check while parsing.
	// XXX restore _xfunc_states
	// "xfunc_states.I"

	{
		// Assume parsing done and values in range.
		//auto selc=_ui_display.select_closeup;
		//QSignalBlocker _blk1{selc};
		std::size_t first_c{0};
		for(unsigned int i=0; i<_cube_infos.size(); i++) {
			if(_cube_infos[i].is_pattern()) {
				if(!first_c) {
					first_c=i+1;
					if(!_states_valid)
						_closeup_ch=first_c;
				}
				//selc->addItem(QString::fromStdString(_cube_infos[i].name()), i+1);
				//if(_closeup_ch==i+1)
					//selc->setCurrentIndex(selc->count()-1);
			}
		}
		//_ui_display.frame_closeup->setEnabled(first_c);
	}
	if(_closeup_ch) {
		//_ui_display.xfunc_closeup->set_state(_xfunc_states[_closeup_ch-1]);
		//_ui_display.xfunc_closeup->setEnabled(true);
		auto& info=_cube_infos[_closeup_ch-1];
		set_closeup_info(info.xform, info.sizes, info.cube_sizes, {});
		set_closeup_xfunc(calc_xfunc(_closeup_ch));
		if(!_cur_pos.valid()) {
			calc_center(_cur_pos, _closeup_ch);
		}
		auto def_zoom=calc_default_zoom(_closeup_ch, 2);
		LOGV("def zoom: %lf", def_zoom);
		set_closeup_zoom(def_zoom);
		//??**void set_closeup_cube(gapr::cube cube, std::string&& uri, std::array<unsigned int, 3> offset);
	} else {
		//_ui_display.xfunc_closeup->setEnabled(false);
	}

	//startDownloadCube();
	apply_changes();

	//_ui.xfunc_slave->set_master(_ui_display.xfunc_closeup);

	//_ui.canvas->set_directions(const std::array<double, 6>& dirs);

	//startDownloadCube();

	update_state(0);
	std::vector<edge_model::point> path;
	path.push_back(_cur_pos.point);
	path.push_back(_cur_pos.point);
	std::get<0>(path.back().first)+=20480;
	set_path_data(std::move(path));

	apply_changes();

	auto fut=load_latest_commits();
	if(!fut)
		return;
	auto ex2=_ui_ctx.get_executor();
	std::move(fut).async_wait(ex2, [this](gapr::likely<bool>&& res) {
		latest_commits_loaded(std::move(res));
		auto fut=find_seed_pos();
		auto ex2=_ui_ctx.get_executor();
		if(fut)
			std::move(fut).async_wait(ex2, [this](gapr::likely<bool>&& res) {
				present_cube(std::move(res));
			});
	});
}
void Engine::load_catalog(gapr::mem_file&& file) {
	auto f=gapr::make_streambuf(std::move(file));
	std::istream str{f.get()};
	// XXX mesh in viewer
	std::vector<gapr::mesh_info> mesh_infos;
	auto base_url="https://"+_args->host+":"+std::to_string(_args->port)+"/api/data/"+_args->group+"/";
	gapr::parse_catalog(str, _cube_infos, mesh_infos, base_url);
	if(_cube_infos.empty()) {
		// no imaging data, any operation is invalid
		// XXX
		// report and terminate
		throw;
	}
	_xfunc_states.clear();
	for(auto& s: _cube_infos) {
		// XXX in thread_pool
		if(!s.xform.update_direction_inv())
			gapr::report("no inverse");
		s.xform.update_resolution();
		gapr::print("cube: ", s.location());
	}
	_xfunc_states.resize(_cube_infos.size(), {0.0, 1.0, 0.0, 1.0});
	for(auto& s: mesh_infos) {
		if(!s.xform.update_direction_inv())
			gapr::report("no inverse");
		s.xform.update_resolution();
		gapr::print("mesh: ", s.location());
	}
}

extern "C" JNIEXPORT jstring JNICALL
Java_goulf_gapr_Proofread_open_1repository(JNIEnv* env, jobject thiz, jstring username, jstring password, jstring repo) {
	JstringHelper u{env, username};
	if(0 /*check username*/)
		return env->NewStringUTF("username invalid");
	JstringHelper p{env, password};
	if(0 /*check username*/)
		return env->NewStringUTF("username invalid");
	JstringHelper r{env, repo};
	if(0 /*check username*/)
		return env->NewStringUTF("username invalid");

	Engine::Args args;
	try {
		std::string user_;
		gapr::parse_repo(std::string{r.view()}, user_, args.host, args.port, args.group);
	} catch(...) {
		return env->NewStringUTF("repository invalid");
	}
	args.user=u.view();
	args.passwd=p.view();

	auto engine=Engine::instance();
	engine->set_args(std::move(args));
	return nullptr;
}

#include "main-impl.cc"
#include "gapr/gui/opengl-impl.hh"

extern "C" JNIEXPORT void JNICALL
Java_goulf_gapr_Proofread_update_1rects(JNIEnv* env, jobject thiz, jintArray data) {
	JarrayHelper<int32_t> dat{env, data};
	if(dat.size()%5!=0)
		return;
	auto engine=Engine::instance();
	engine->update_control_areas(dat.data(), dat.size());
}
extern "C" JNIEXPORT jstring JNICALL
Java_goulf_gapr_Proofread_get_1infos(JNIEnv* env, jobject thiz) {
	auto engine=Engine::instance();
	std::ostringstream oss;
	engine->dump_infos(oss);
	return env->NewStringUTF(oss.str().c_str());
}
extern "C" JNIEXPORT void JNICALL
Java_goulf_gapr_Proofread_prepare_1library(JNIEnv* env, jclass clazz) {
	fix_main_thread_stack_guard();
	LOGV("template_class_static_member: %p %p %p",
			template_class_static_data_1(),
			template_class_static_data_2(),
			template_class_static_data_3());
}
