#include "window.hh"

#include "../gather/model.hh"

#include "gapr/utility.hh"
#include "gapr/swc-helper.hh"
#include "gapr/downloader.hh"
#include "gapr/commit.hh"
#include "gapr/mem-file.hh"
#include "gapr/archive.hh"
#include "gapr/streambuf.hh"
#include "gapr/bbox.hh"
#include "gapr/cube-loader.hh"

#include <type_traits>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <deque>

#include <boost/asio/post.hpp>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include <epoxy/gl.h>
#include <glib/gi18n.h>

#include <lua.hpp>

#include "config.hh"

/*
scaling, dpr and multisample
----------------------------

dpr: use screen dpr, [1, 2, ...].
multisample: for geometries, [1, 2, ...]; for volume, [1] (disabled).
scale down: disabled scale_down
scale up: for geometries, [1] (disabled); for volume, [1, 2, ..., dpr, ...]
*/

constexpr static int PICK_SIZE=5;
static constexpr int FBO_ALLOC{64}, FBO_ALIGN{16};
static inline std::pair<int, int> get_fbo_alloc(int w, int h) {
	return {(w+FBO_ALLOC)/FBO_ALIGN*FBO_ALIGN, (h+FBO_ALLOC)/FBO_ALIGN*FBO_ALIGN};
}
template<typename Canvas>
static inline bool check_fbo_resize(int w, int h, const Canvas& canvas) {
	return w>canvas.width_alloc
		|| w+2*FBO_ALLOC<canvas.width_alloc
		|| h>canvas.height_alloc
		|| h+2*FBO_ALLOC<canvas.height_alloc;
}

namespace gapr::show {

	G_DECLARE_FINAL_TYPE(MainWindow, main_window, PRIV, MAIN_WINDOW, GtkApplicationWindow);
	struct _MainWindow { GtkApplicationWindow parent_instance;
		GtkHeaderBar* header_bar;
		GtkGLArea* canvas;
		GtkSpinner* spinner;
		GtkSpinner* spinner_img;
		GtkScale* xfunc_min;
		GtkScale* xfunc_max;
		GtkLabel* node_info;
		GtkPaned* paned;
		GtkComboBox* select_mode;
		GtkEntry* frame_entry;
		GtkButton* btn_next_frame;
		GtkListStore* display_modes;
		GtkComboBox* user_filter;
		GtkListStore* users_list;
		GtkLabel* commit_info;
		GtkStatusbar* statusbar;
		gint width, height;
		GtkGestureDrag* drag_gesture;
		GtkGestureMultiPress* multipress_gesture;
		std::shared_ptr<Session> session;

		static GdkGLContext* canvas_create_context(GtkGLArea* area, MainWindow* win) {
			gapr::print("canvas_create_context");
			gtk::ref<GError> err{};
			auto window=gtk_widget_get_window((GtkWidget*)area);
			gtk::ref ctx=gdk_window_create_gl_context(window, &err);
			if(err) {
				gtk_gl_area_set_error(area, err);
				return nullptr;
			}

			gdk_gl_context_set_use_es(ctx, false);
			gdk_gl_context_set_required_version(ctx, 3, 3);
			gdk_gl_context_realize(ctx, &err);
			if(err) {
				gtk_gl_area_set_error(area, err);
				return nullptr;
			}
			return ctx.release();
		}
		static void canvas_realize(GtkGLArea* area, MainWindow* win) {
			gapr::print("canvas_realize");
			gtk_gl_area_make_current(area);
			if(gtk_gl_area_get_error(area)!=nullptr)
				return;

			gtk_widget_add_events((GtkWidget*)area, GDK_BUTTON_MOTION_MASK|GDK_SMOOTH_SCROLL_MASK|GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);
			//gtk_widget_add_events((GtkWidget*)area, GDK_POINTER_MOTION_MASK|GDK_SCROLL_MASK);
			//gtk_widget_add_events((GtkWidget*)area, GDK_ENTER_NOTIFY_MASK|GDK_LEAVE_NOTIFY_MASK);

			int scale;
			g_object_get(area, "scale-factor", &scale, nullptr);
			auto width=gtk_widget_get_allocated_width((GtkWidget*)area)*scale;
			auto height=gtk_widget_get_allocated_height((GtkWidget*)area)*scale;
			try {
				win->session->init_opengl(win, scale, width, height);
			} catch(const std::runtime_error& e) {
				// XXX map system_error to GError
				gtk::ref err=g_error_new(GDK_GL_ERROR, GDK_GL_ERROR_NOT_AVAILABLE, "failed to setup OpenGL: %s", e.what());
				gtk_gl_area_set_error(area, err);
			}
		}
		static void canvas_unrealize(GtkGLArea* area, MainWindow* win) {
			gapr::print("canvas_unrealize");
			gtk_gl_area_make_current(area);
			if(gtk_gl_area_get_error(area)!=nullptr)
				return;

			win->session->deinit_opengl(win);
		}
		static void canvas_notify_scale_factor(GtkGLArea* area, GParamSpec* pspec, MainWindow* win) {
			int scale_factor;
			g_object_get(area, "scale-factor", &scale_factor, nullptr);
			gapr::print("canvas_notify_scale_factor ", scale_factor);
			win->session->set_scale_factor(win, scale_factor);
		}
		static void canvas_resize(GtkGLArea* area, int w, int h, MainWindow* win) {
			gapr::print("on resize ", w, 'x', h);
			/*! already current */
			if(gtk_gl_area_get_error(area)!=nullptr)
				return;

			win->session->resize(win, w, h);
		}
		static gboolean canvas_render(GtkGLArea* area, GdkGLContext* ctx, MainWindow* win) {
			gapr::print("on paint");
			/*! already current */
			if(gtk_gl_area_get_error(area)!=nullptr)
				return false;

			win->session->render(win);
			glFlush();
			return true;
		}
		static gboolean canvas_scroll_event(GtkGLArea* area, GdkEvent* ev, MainWindow* win) {
			GdkScrollDirection dir;
			gdouble dx, dy;
			if(gdk_event_get_scroll_direction(ev, &dir)) {
				switch(dir) {
					case GDK_SCROLL_UP:
						gapr::print("scroll up");
						win->session->scroll_event(win, -1.0);
						return true;
					case GDK_SCROLL_DOWN:
						gapr::print("scroll down");
						win->session->scroll_event(win, 1.0);
						return true;
					default:
						break;
				}
			} else if(gdk_event_get_scroll_deltas(ev, &dx, &dy)) {
				gapr::print("scroll ", dx, ' ', dy);
				win->session->scroll_event(win, dy);
				return true;
			}
			return false;
		}
			//////////
		/////////////////////////
		static void canvas_drag_update(GtkGestureDrag* gesture, double offset_x, double offset_y, MainWindow* win) {
			gdouble x, y;
			if(gtk_gesture_drag_get_start_point(gesture, &x, &y)) {
				x+=offset_x;
				y+=offset_y;
				gapr::print("button motion ", x, ',', y);
				win->session->motion_notify_event(win, x, y);
				gtk_gesture_set_state((GtkGesture*)win->multipress_gesture, GTK_EVENT_SEQUENCE_CLAIMED);
				return;
			}
			gtk_gesture_set_state((GtkGesture*)win->multipress_gesture, GTK_EVENT_SEQUENCE_DENIED);
		}
		static void canvas_pressed(GtkGestureMultiPress* gesture, int n_press, double x, double y, MainWindow* win) {
			auto sequence=gtk_gesture_single_get_current_sequence((GtkGestureSingle*)gesture);
			auto event=gtk_gesture_get_last_event((GtkGesture*)gesture, sequence);
			if(gdk_event_triggers_context_menu(event)) {
				// handle ctx menu
				return;
			}
			GdkModifierType st;
			auto btn=gtk_gesture_single_get_current_button((GtkGestureSingle*)gesture);
			if(btn && gdk_event_get_state(event, &st)) {
				gapr::print("button press ", btn, ' ', x, ',', y);
				win->session->button_press_event(win, btn, x, y, st&GDK_SHIFT_MASK, st&GDK_CONTROL_MASK, n_press==1);
				gtk_gesture_set_state((GtkGesture*)win->multipress_gesture, GTK_EVENT_SEQUENCE_CLAIMED);
				return;
			}
			gtk_gesture_set_state((GtkGesture*)win->multipress_gesture, GTK_EVENT_SEQUENCE_DENIED);
		}
		static void canvas_released(GtkGestureMultiPress* gesture, int n_press, double x, double y, MainWindow* win) {
			auto btn=gtk_gesture_single_get_current_button((GtkGestureSingle*)gesture);
			if(btn) {
				gapr::print("button release ", btn, ' ', x, ',', y);
				win->session->button_release_event(win, btn, x, y);
				gtk_gesture_set_state((GtkGesture*)win->multipress_gesture, GTK_EVENT_SEQUENCE_CLAIMED);
				return;
			}
			gtk_gesture_set_state((GtkGesture*)win->multipress_gesture, GTK_EVENT_SEQUENCE_DENIED);
		}
		static gboolean window_delete(MainWindow* win, GdkEvent* ev, gpointer udata) {
			return false;
		}
		static void select_mode_changed(GtkComboBox* select_mode, MainWindow* win) {
			gapr::print("mode changed");
			auto id=gtk_combo_box_get_active_id(select_mode);
			win->session->change_mode(win, id);
		}
		static void mode_script_file_set(GtkFileChooserButton* chooser, MainWindow* win) {
			auto f=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
			gapr::print("script file set: ", f);
			win->session->change_mode_s(win, f);
		}
		static void jump_frame_clicked(GtkButton* button, MainWindow* win) {
			gapr::print("jump frame");
			auto t=gtk_entry_get_text(win->frame_entry);
			win->session->jump_frame1(win, t);
		}
		static void next_frame_clicked(GtkButton* button, MainWindow* win) {
			gapr::print("next frame");
			win->session->jump_frame1(win, nullptr);
		}
		static void jump_begin_clicked(GtkButton* button, MainWindow* win) {
			gapr::print("jump frame 0");
			win->session->jump_frame(win, 0u);
		}
		static void frame_entry_activate(GtkEntry* entry, MainWindow* win) {
			gapr::print("frame entry activate");
			auto t=gtk_entry_get_text(entry);
			win->session->jump_frame1(win, t);
		}
		static void user_filter_changed(GtkComboBox* user_filter, MainWindow* win) {
			gapr::print("user changed");
			auto id=gtk_combo_box_get_active_id(user_filter);
			win->session->change_user(win, id);
		}
		static void locate_clicked(GtkButton* button, MainWindow* win) {
			gapr::print("locate");
			win->session->locate_camera(win);
		}
		static void jump_to_clicked(GtkButton* button, MainWindow* win) {
			gapr::print("jump to");
			auto t=gtk_entry_get_text(win->frame_entry);
			win->session->jump_frame1(win, t);
		}
		static void size_allocate(MainWindow* win, GdkRectangle* allocation, gpointer user_data) {
			gtk_window_get_size(GTK_WINDOW(win), &win->width, &win->height);
		}

		static void load_image(GSimpleAction* act, GVariant* par, gpointer udata) {
			auto win=static_cast<MainWindow*>(udata);
			gapr::print("refresh clicked");
			win->session->refresh_image(win);
		}
		static void load_script(GSimpleAction* act, GVariant* par, gpointer udata) {
			auto win=static_cast<MainWindow*>(udata);
			auto dlg=gtk_file_chooser_dialog_new("Open Script", GTK_WINDOW(win), GTK_FILE_CHOOSER_ACTION_OPEN,
					_("_Cancel"), GTK_RESPONSE_CANCEL, _("_Open"), GTK_RESPONSE_ACCEPT, nullptr);
			auto res=gtk_dialog_run(GTK_DIALOG(dlg));
			gtk::ref<gchar> filename{};
			if(res==GTK_RESPONSE_ACCEPT)
				filename=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
			gtk_widget_destroy(dlg);
			if(res==GTK_RESPONSE_ACCEPT)
				win->session->run_script(filename.get());
		}
		static void show_about_dialog(GSimpleAction* act, GVariant* par, gpointer udata) {
			auto win=static_cast<MainWindow*>(udata);
			std::initializer_list<const char*> authors={
				"GOU Lingfeng",
				nullptr
			};
			gtk::ref dlg=(GtkAboutDialog*)gtk_about_dialog_new();
			gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dlg), PACKAGE_NAME);
			gtk_about_dialog_set_comments(dlg, PACKAGE_DESCRIPTION);
			gtk_about_dialog_set_version(dlg, PACKAGE_VERSION);
			gtk_about_dialog_set_copyright(dlg, PACKAGE_COPYRIGHT);
			gtk_about_dialog_set_license_type(dlg, GTK_LICENSE_GPL_3_0);
			gtk_about_dialog_set_website(dlg, PACKAGE_URL);
			gtk_about_dialog_set_website_label(dlg, PACKAGE_ORG);
			gtk_about_dialog_set_authors(dlg, const_cast<const char**>(authors.begin()));
			gtk_about_dialog_set_logo_icon_name(dlg, APPLICATION_ID);
			gtk_window_set_title(GTK_WINDOW(dlg), "About " PACKAGE_NAME);
			gtk_window_set_modal(GTK_WINDOW(dlg), true);
			gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(win));
			gtk_window_present(GTK_WINDOW(dlg.release()));
		}
		static void close_window(GSimpleAction* act, GVariant* par, gpointer udata) {
			auto win=static_cast<GtkWindow*>(udata);
			gtk_window_close(win);
		}
		static void hide_graph(GSimpleAction* act, GVariant* val, gpointer udata) {
			auto win=static_cast<MainWindow*>(udata);
			gapr::print("hide clicked");
			auto h=g_variant_get_boolean(val);
			win->session->hide_graph(win, h);
			g_simple_action_set_state(act, val);
		}
		static void xfunc_changed0(GtkScale* xfunc, MainWindow* win) {
			gapr::print("xfunc changed");
			auto v=gtk_range_get_value((GtkRange*)xfunc);
			win->session->update_xfunc(win, 0, v);
		}
		static void xfunc_changed1(GtkScale* xfunc, MainWindow* win) {
			auto v=gtk_range_get_value((GtkRange*)xfunc);
			win->session->update_xfunc(win, 1, v);
		}

		static void constructed(GObject* obj);
		static void finalize(GObject* obj);
		static void destroy(GtkWidget* widget);
		static void set_property(GObject* obj, guint prop_id, const GValue* val, GParamSpec* pspec) {
			auto self=gapr::show::PRIV_MAIN_WINDOW(obj);
			switch (prop_id) {
				case 1:
					gapr::print("set_prop ", g_value_get_pointer(val));
					self->session=static_cast<Session*>(g_value_get_pointer(val))->shared_from_this();
					break;
				default:
					G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
			}
		}
		static void get_property(GObject* obj, guint prop_id, GValue* val, GParamSpec* pspec) {
			switch(prop_id) {
				case 1:
					gapr::print("get_prop");
					g_value_set_pointer(val, nullptr);
					break;
				default:
					G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
			}
		}
	};

	G_DEFINE_TYPE(MainWindow, main_window, GTK_TYPE_APPLICATION_WINDOW);
	static GParamSpec* pspec1{nullptr};
	static void main_window_class_init(MainWindowClass* klass) {
		gapr::print("window_class_init");
		auto wid_class=GTK_WIDGET_CLASS(klass);
		wid_class->destroy=MainWindow::destroy;
		gtk_widget_class_set_template_from_resource(wid_class, APPLICATION_PATH "/ui/window.ui");
		gtk_widget_class_bind_template_child(wid_class, MainWindow, header_bar);
		gtk_widget_class_bind_template_child(wid_class, MainWindow, canvas);
		gtk_widget_class_bind_template_child(wid_class, MainWindow, spinner);
		gtk_widget_class_bind_template_child(wid_class, MainWindow, spinner_img);
		gtk_widget_class_bind_template_child(wid_class, MainWindow, xfunc_min);
		gtk_widget_class_bind_template_child(wid_class, MainWindow, xfunc_max);
		gtk_widget_class_bind_template_child(wid_class, MainWindow, node_info);
		gtk_widget_class_bind_template_child(wid_class, MainWindow, paned);
		gtk_widget_class_bind_template_child(wid_class, MainWindow, select_mode);
		gtk_widget_class_bind_template_child(wid_class, MainWindow, display_modes);
		gtk_widget_class_bind_template_child(wid_class, MainWindow, frame_entry);
		gtk_widget_class_bind_template_child(wid_class, MainWindow, btn_next_frame);
		gtk_widget_class_bind_template_child(wid_class, MainWindow, user_filter);
		gtk_widget_class_bind_template_child(wid_class, MainWindow, users_list);
		gtk_widget_class_bind_template_child(wid_class, MainWindow, commit_info);
		gtk_widget_class_bind_template_child(wid_class, MainWindow, statusbar);

		gtk_widget_class_bind_template_callback(wid_class, MainWindow::canvas_create_context);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::canvas_realize);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::canvas_unrealize);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::canvas_notify_scale_factor);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::canvas_resize);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::canvas_render);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::canvas_scroll_event);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::window_delete);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::select_mode_changed);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::mode_script_file_set);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::jump_frame_clicked);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::next_frame_clicked);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::jump_begin_clicked);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::frame_entry_activate);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::user_filter_changed);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::locate_clicked);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::jump_to_clicked);
		gtk_widget_class_bind_template_callback(wid_class, MainWindow::size_allocate);
		
		auto obj_class=G_OBJECT_CLASS(klass);
		obj_class->finalize=MainWindow::finalize;
		obj_class->constructed=MainWindow::constructed;
		obj_class->set_property=MainWindow::set_property;
		obj_class->get_property=MainWindow::get_property;
		pspec1=g_param_spec_pointer("cpp-ptr", N_("C++"), N_("C++ data"), gtk::flags(G_PARAM_CONSTRUCT_ONLY, G_PARAM_WRITABLE, G_PARAM_STATIC_STRINGS));
		g_object_class_install_property(obj_class, 1, pspec1);
	}
	static void main_window_init(MainWindow* self) {
		static_assert(std::is_standard_layout_v<_MainWindow>);
		gapr::print("window_init");
		new(&self->session) std::shared_ptr<Session>{};
		gtk_widget_init_template(GTK_WIDGET(self));

		self->drag_gesture=(GtkGestureDrag*)gtk_gesture_drag_new((GtkWidget*)self->canvas);
		gtk_gesture_single_set_button((GtkGestureSingle*)self->drag_gesture, 0);
		g_signal_connect(self->drag_gesture, "drag-update", G_CALLBACK(MainWindow::canvas_drag_update), self);

		self->multipress_gesture=(GtkGestureMultiPress*)gtk_gesture_multi_press_new((GtkWidget*)self->canvas);
		gtk_gesture_single_set_button((GtkGestureSingle*)self->multipress_gesture, 0);
		gtk_gesture_group((GtkGesture*)self->drag_gesture, (GtkGesture*)self->multipress_gesture);
		g_signal_connect(self->multipress_gesture, "pressed", G_CALLBACK(MainWindow::canvas_pressed), self);
		g_signal_connect(self->multipress_gesture, "released", G_CALLBACK(MainWindow::canvas_released), self);

		gtk_range_set_range((GtkRange*)self->xfunc_max, 0.0, 1.0);
		gtk_range_set_value((GtkRange*)self->xfunc_max, 1.0);
		gtk_range_set_range((GtkRange*)self->xfunc_min, 0.0, 1.0);
		gtk_range_set_value((GtkRange*)self->xfunc_min, 0.0);
		g_signal_connect(self->xfunc_max, "value-changed", G_CALLBACK(MainWindow::xfunc_changed1), self);
		g_signal_connect(self->xfunc_min, "value-changed", G_CALLBACK(MainWindow::xfunc_changed0), self);

		std::initializer_list<GActionEntry> actions={
			{"hide-graph", nullptr, nullptr, "false", MainWindow::hide_graph},
			{"load-image", MainWindow::load_image, nullptr, nullptr, nullptr},
			{"xfunc-shrink", nullptr, nullptr, nullptr, nullptr},
			{"xfunc-reset", nullptr, nullptr, nullptr, nullptr},
			{"run-script", MainWindow::load_script, nullptr, nullptr, nullptr},
			{"show-about-dialog", MainWindow::show_about_dialog, nullptr, nullptr, nullptr},
			{"close", MainWindow::close_window, nullptr, nullptr, nullptr}
		};
		g_action_map_add_action_entries(G_ACTION_MAP(self), actions.begin(), actions.size(), self);


		struct AccelPair {
			const char* action;
			std::initializer_list<const char*> accels;

			static gboolean cc(GtkAccelGroup* acc_grp, GObject* obj, guint key, GdkModifierType mods, GAction* action) {
				g_action_activate(action, nullptr);
				return true;
			}
		};
		std::initializer_list<AccelPair> win_accels={
			{"hide-graph", {"e", nullptr}},
			{"load-image", {"r", nullptr}},
		};
		gtk::ref acc_grp=gtk_accel_group_new();
		for(auto [action, accels]: win_accels) {
			for(auto accel: accels) {
				if(!accel)
					break;
				guint key;
				GdkModifierType mods;
				gtk_accelerator_parse (accel, &key, &mods);
				if(key==0)
					continue;
				auto act=g_action_map_lookup_action(G_ACTION_MAP(self), action);
				auto cc=g_cclosure_new(G_CALLBACK(AccelPair::cc), (gpointer)act, nullptr);
				gtk_accel_group_connect(acc_grp, key, mods, GTK_ACCEL_VISIBLE, cc);
			}
		}
		gtk_window_add_accel_group((GtkWindow*)self, acc_grp);

		auto select_mode=GTK_CELL_LAYOUT(self->select_mode);
		auto render=gtk_cell_renderer_text_new();
		gtk_cell_layout_pack_end(select_mode, render, true);
		gtk_cell_layout_set_attributes(select_mode, render, "text", 0, "sensitive", 2, nullptr);

		auto user_filter=GTK_CELL_LAYOUT(self->user_filter);
		render=gtk_cell_renderer_text_new();
		gtk_cell_layout_pack_end(user_filter, render, true);
		gtk_cell_layout_set_attributes(user_filter, render, "text", 0, nullptr);

		auto context_id=gtk_statusbar_get_context_id(self->statusbar, "hello");
		auto message_id=gtk_statusbar_push(self->statusbar, context_id, "Ready");
	}
	void MainWindow::constructed(GObject* obj) {
		gapr::print("constructed");
		auto self=gapr::show::PRIV_MAIN_WINDOW(obj);

		if(gtk::ref settings=gtk::make_g_settings(APPLICATION_ID); settings) {
			gint w, h;
			g_settings_get(settings, "window-size", "(ii)", &w, &h);
			gapr::print("window-size: ", w, ',', h);
			if(w>0 && h>0)
				gtk_window_set_default_size(GTK_WINDOW(self), w, h);
		}

		G_OBJECT_CLASS(main_window_parent_class)->constructed(obj);
		auto ses=self->session.get();
		ses->start(self);
	}
	void MainWindow::destroy(GtkWidget* widget) {
		gapr::print("window_destroy");
		auto self=gapr::show::PRIV_MAIN_WINDOW(widget);

		do {
			if(auto session=std::move(self->session); !session)
				break;
			gtk::ref settings=gtk::make_g_settings(APPLICATION_ID);
			if(!settings)
				break;
			gint w=self->width, h=self->height;
			if(!g_settings_set(settings, "window-size", "(ii)", w, h))
				;
			g_settings_sync();
		} while(false);

		GTK_WIDGET_CLASS(main_window_parent_class)->destroy(widget);
	}
	void MainWindow::finalize(GObject* obj) {
		gapr::print("window_finalize");
		auto self=gapr::show::PRIV_MAIN_WINDOW(obj);
		g_object_unref(self->multipress_gesture);
		g_object_unref(self->drag_gesture);

		G_OBJECT_CLASS(main_window_parent_class)->finalize(obj);
		self->session.~shared_ptr();
	}

}

static bool startswith(std::string_view a, std::string_view v) {
	return v.size()>=3 && v.substr(0, a.size())==a;
}

struct gapr::show::Session::playback_helper {
	gapr::bbox bbox{};
	gapr::archive _repo;
	std::unique_ptr<gather_model> _model;
	struct CommitInfo: gapr::commit_info {
		gapr::bbox_int bbox{};
		std::vector<std::tuple<gapr::node_id, std::string, std::string, int>> props_mod;
	};
	std::vector<CommitInfo> _infos;
	std::unique_ptr<std::streambuf> get_payload(uint64_t num) {
		std::array<char,32> fn_buf;
		auto sbuf=_repo.reader_streambuf(to_string_lex(fn_buf, num));
		gapr::commit_info info;
		if(!info.load(*sbuf))
			gapr::report("commit file no commit info");
		return sbuf;
	}
	std::vector<std::string> _users;
	uint64_t _nodes_ver{0};
	uint64_t _model_ver;

	void load_repo(const std::string& file) {
		_repo=gapr::archive{file.c_str(), true};
		_model=std::make_unique<gather_model>();
		std::unordered_set<std::string> users;
		while(true) {
			std::array<char,32> fn_buf;
			auto sbuf=_repo.reader_streambuf(to_string_lex(fn_buf, _infos.size()));
			if(!sbuf)
				break;
			if(!sbuf)
				gapr::report("failed to open file");
			CommitInfo info;
			if(!info.load(*sbuf))
				gapr::report("commit file no commit info");
			if(info.id!=_infos.size())
				gapr::report("commit file wrong id");
			users.insert(info.who);
			_model->xxx_prepare(info, *sbuf);
			_model->peek_props_mod(info.props_mod);
			_model->peek_bbox(info.bbox);
			_model->xxx_apply();
			bbox.add(gapr::bbox{info.bbox});
			_infos.emplace_back(std::move(info));
		}
		for(auto&& u: users)
			_users.emplace_back(std::move(u));
		std::sort(_users.begin(), _users.end());
		_model_ver=_infos.size();
	}

	void model_jump_to(uint64_t num) {
		auto cur=_model_ver;
		if(cur>num) {
			_model=std::make_unique<gather_model>();
			cur=0;
		}
		while(cur<num) {
			auto f=get_payload(cur);
			_model->xxx_prepare(_infos[cur], *f);
			_model->xxx_apply();
			++cur;
		}
		_model_ver=cur;
	}
	void nodes_increment(std::unordered_map<int64_t, NodeData>& nodes) {
		gapr::print("nodes increment");
		std::vector<decltype(nodes.end())> todel;
		auto handle_links=[](NodeData& nd) {
			for(unsigned int i=0; i<nd.num_links; ++i) {
				auto& ld=nd.link_at(i);
				switch(ld.state) {
					case DiffState::Del:
						{
							if(i+1<nd.num_links) {
								auto& ld2=nd.link_at(nd.num_links-1);
								std::swap(ld, ld2);
							}
							if(nd.num_links>2)
								nd.more_links.pop_back();
							--nd.num_links;
							--i;
						}
						break;
					case DiffState::Add:
						ld.in_loop=false;
						ld.state=DiffState::Eq;
						break;
					case DiffState::Eq:
						ld.in_loop=false;
						break;
					case DiffState::Chg:
						assert(0);
						break;
				}
			}
		};
		for(auto it=nodes.begin(); it!=nodes.end(); ++it) {
			auto& nd=it->second;
			switch(nd.state) {
				case DiffState::Del:
					todel.push_back(it);
					break;
				case DiffState::Add:
					nd.state=DiffState::Eq;
					nd.root_id=-1;
					for(unsigned int i=0; i<nd.num_links; ++i) {
						auto& ld=nd.link_at(i);
						assert(ld.state==DiffState::Add);
						ld.in_loop=false;
						ld.state=DiffState::Eq;
					}
					break;
				case DiffState::Eq:
					nd.root_id=-1;
					handle_links(nd);
					break;
				case DiffState::Chg:
					nd.state=DiffState::Eq;
					nd.root_id=-1;
					handle_links(nd);
					break;
			}
		}
		for(auto it: todel) {
			nodes.erase(it);
		}
	}
	void nodes_from_model(std::unordered_map<int64_t, NodeData>& nodes) {
		gapr::print("nodes from model");
		nodes.clear();

		for(auto& [id_, val]: _model->peek_nodes()) {
			int64_t id=id_.data;
			auto [it, ins]=nodes.emplace(id, NodeData{});
			auto& nd=it->second;
			if(ins) {
				nd.state=DiffState::Eq;
				nd.attr=val.attr;
				nd.type=val.attr.misc.t();
				nd.radius=val.attr.misc.r();
				nd.pos={val.attr.pos(0), val.attr.pos(1), val.attr.pos(2)};
				nd.id=id;
				nd.num_links=0;
			} else {
				throw std::runtime_error{"dup node"};
			}
		}
		auto handle_link=[](NodeData& nd, int64_t id) {
			if(nd.id==id) {
				std::cerr<<"loop: "<<id<<'\n';
				return;
			}
			auto [it, ins]=nd.link_ins(id);
			if(ins) {
				it->id2=id;
				it->state=DiffState::Eq;
			} else {
				std::cerr<<"dup link: "<<nd.id<<' '<<id<<'\n';
			}
		};
		for(auto& [link, val]: _model->peek_links()) {
			auto [id, id2]=link.nodes;
			handle_link(nodes.at(id.data), id2.data);
			handle_link(nodes.at(id2.data), id.data);
		}
		for(auto& [pkey, val]: _model->peek_props()) {
			int64_t id=pkey.node.data;
			auto& key=pkey.key;
			auto& nd=nodes.at(id);
			if(key.size()>0 && key[0]=='.')
				continue;
			std::string v{key};
			if(!val.empty()) {
				v+='=';
				v+=val;
			}
			nd.annots.emplace_back(std::move(v));
			if(key=="root" && !startswith("seg", val))
				nd.is_root=true;
		}
	}
	void nodes_diff_from_model(std::unordered_map<int64_t, NodeData>& nodes) {
		gapr::print("nodes diff from model");

		std::vector<std::tuple<gapr::node_id, gapr::node_attr, int>> nodes_mod;
		_model->peek_nodes_mod(nodes_mod);
		for(auto& [id, attr, c]: nodes_mod) {
			auto [it, ins]=nodes.emplace(id.data, NodeData{});
			auto& nd=it->second;
			if(c<0) {
				assert(!ins);
				nd.state=DiffState::Del;
				nd.is_root=false;
			} else if(c>0) {
				assert(ins);
				nd.state=DiffState::Add;
				nd.attr=attr;
				nd.type=attr.misc.t();
				nd.radius=attr.misc.r();
				nd.pos={attr.pos(0), attr.pos(1), attr.pos(2)};
				nd.id=id.data;
				nd.num_links=0;
			} else {
				assert(!ins);
				nd.state=DiffState::Chg;
				nd.attr2=nd.attr;
				nd.attr=attr;
				nd.type2=nd.type;
				nd.type=attr.misc.t();
				nd.radius2=nd.radius;
				nd.radius=attr.misc.r();
				for(unsigned int i=0; i<3; ++i) {
					auto p=attr.pos(i);
					nd.pos_diff[i]=nd.pos[i]-p;
					nd.pos[i]=p;
				}
				nd.annots2=nd.annots;
			}
		}
		std::vector<std::tuple<gapr::node_id, gapr::node_id, int>> links_mod;
		_model->peek_links_mod(links_mod);
		auto handle_link=[](NodeData& nd, int64_t id, bool add) {
			auto [it, ins]=nd.link_ins(id);
			if(add) {
				assert(ins);
				it->id2=id;
				it->state=DiffState::Add;
			} else {
				assert(!ins);
				assert(it->id2==id);
				it->state=DiffState::Del;
			}
		};
		for(auto& [id, id2, c]: links_mod) {
			if(id==id2)
				continue;
			assert(c!=0);
			if(c>0) {
				handle_link(nodes.at(id.data), id2.data, true);
				handle_link(nodes.at(id2.data), id.data, true);
			} else {
				handle_link(nodes.at(id.data), id2.data, false);
				handle_link(nodes.at(id2.data), id.data, false);
			}
		}
		std::vector<std::tuple<gapr::node_id, std::string, std::string, int>> props_mod;
		_model->peek_props_mod(props_mod);
		std::unordered_set<NodeData*> to_sort;
		auto get_annot=[](NodeData& nd, const std::string& k) ->std::string& {
			for(std::size_t i=0; i<nd.annots.size(); ++i) {
				auto& k2=nd.annots[i];
				if(k2.size()<k.size())
					continue;
				if(k2.compare(0, k.size(), k)!=0)
					continue;
				if(k2.size()==k.size())
					return k2;
				if(k2[k.size()]=='=')
					return k2;
			}
			assert(0);
		};
		for(auto& [id, key, val, c]: props_mod) {
			if(key.size()>0 && key[0]=='.')
				continue;
			auto& nd=nodes.at(id.data);
			switch(nd.state) {
				case DiffState::Add:
					assert(c>0);
					break;
				case DiffState::Del:
					assert(c<0);
					if(c<0)
						continue;
					break;
				case DiffState::Eq:
					nd.state=DiffState::Chg;
					nd.attr2=nd.attr;
					nd.type2=nd.type;
					nd.radius2=nd.radius;
					for(unsigned int i=0; i<3; ++i)
						nd.pos_diff[i]=0;
					nd.annots2=nd.annots;
					break;
				case DiffState::Chg:
					break;
			}
			if(c<0) {
				to_sort.insert(&nd);
				auto& annot=get_annot(nd, key);
				if(annot.empty())
					annot.push_back('\xff');
				else
					annot[0]='\xff';
				if(key=="root")
					nd.is_root=false;
				continue;
			}
			std::string v{key};
			if(!val.empty()) {
				v+='=';
				v+=val;
			}
			if(c>0) {
				to_sort.insert(&nd);
				nd.annots.push_back(std::move(v));
				if(key=="root" && !startswith("seg", val))
					nd.is_root=true;
			} else {
				auto& annot=get_annot(nd, key);
				annot=std::move(v);
			}
		}
		for(auto nd: to_sort) {
			auto& annots=nd->annots;
			std::sort(annots.begin(), annots.end());
			while(!annots.empty()) {
				auto& s=annots.back();
				if(s[0]!='\xff')
					break;
				annots.pop_back();
			}
		}
	}
	void nodes_jump_to(uint64_t num, std::unordered_map<int64_t, NodeData>& nodes) {
		if(num==0) {
			if(_nodes_ver!=0) {
				nodes.clear();
				_nodes_ver=0;
			}
			return;
		}
		if(_nodes_ver==num) {
			nodes_increment(nodes);
			return;
		}
		nodes_from_model(nodes);
		_nodes_ver=_model_ver;
	}
	bool jump_to(uint64_t num, std::unordered_map<int64_t, NodeData>& nodes) {
		gapr::print("jump to: ", _model_ver, ' ', _nodes_ver, ' ', num);
		if(_nodes_ver==num)
			return false;
		assert(num<=_infos.size());
		if(num==0) {
			nodes.clear();
			_nodes_ver=0;
			return true;
		}
		model_jump_to(num-1);
		nodes_jump_to(num-1, nodes);
		assert(_nodes_ver==num-1);

		auto f=get_payload(num-1);
		_model->xxx_prepare(_infos[num-1], *f);
		assert(_model_ver==_nodes_ver);
		nodes_diff_from_model(nodes);
		++_nodes_ver;
		_model->xxx_apply();
		++_model_ver;
		return true;
	}
};

struct lua_base {
	struct lua_State_Del {
		void operator()(lua_State* L) const { ::lua_close(L); }
	};
	static void* lua_alloc_f(void* ud, void* ptr, std::size_t osize, std::size_t nsize) {
		if(nsize!=0)
			return std::realloc(ptr, nsize);
		std::free(ptr);
		return nullptr;
	}
	static int lua_panic(lua_State* L) {
		std::cerr<<"lua err\n";
		return 0;
	}
	static std::string get_stack(lua_State* L, int r) {
		std::ostringstream oss;
		for(int i=0; i<r; ++i) {
			if(r>1)
				oss<<i<<": ";
			auto tt=lua_type(L, i+1);
			switch(tt) {
				case LUA_TNUMBER:
				case LUA_TBOOLEAN:
				case LUA_TSTRING:
					{
						std::size_t n;
						auto p=::lua_tolstring(L, i+1, &n);
						oss<<std::string_view{p, n}<<'\n';
					}
					break;
				default:
					oss<<lua_typename(L, tt)<<'\n';
			}
		}
		return oss.str();
	}
	static void report_lua_error(int ret) {
		switch(ret) {
			case LUA_OK:
				return;
			case LUA_ERRRUN:
				throw std::runtime_error{"LUA_ERRRUN"};
			case LUA_ERRMEM:
				throw std::runtime_error{"LUA_ERRMEM"};
			case LUA_ERRSYNTAX:
				throw std::runtime_error{"LUA_ERRSYNTAX"};
			case LUA_ERRERR:
				throw std::runtime_error{"LUA_ERRERR"};
				//case LUA_ERRGCMM:
				//throw std::runtime_error{"LUA_ERRGCMM"};
			default:
				throw std::runtime_error{"lua_error "+std::to_string(ret)};
		}
	}
};


struct gapr::show::Session::script_helper: lua_base {
	static auto load_mesh(const std::filesystem::path& fn) {
		std::ifstream f{fn};
		if(!f)
			throw std::runtime_error{"Failed to open"};

		std::vector<MeshVert> meshVerts;
		std::vector<GLuint> meshIdxes;
		std::string line;
		std::vector<gapr::vec3<double>> vertNormals;
		while(std::getline(f, line)) {
			if(line.empty())
				continue;
			if(line[0]=='#')
				continue;
			std::size_t p=0;
			auto get_pfx=[&p,&line]() ->std::string_view {
				assert(p==0);
				while(p<line.size()) {
					if(std::isspace(line[p]))
						return {&line[0], p};
					++p;
				}
				return {&line[0], p};
			};
			auto get_double=[&p,&line]() ->double {
				assert(std::isspace(line[p]));
				do {
					++p;
				} while(std::isspace(line[p]));
				char* eptr;
				errno=0;
				auto v=strtod(&line[p], &eptr);
				if(errno)
					throw std::runtime_error{"failed to parse double"};
				p=eptr-&line[0];
				return v;
			};
			auto get_index=[&p,&line]() ->unsigned int {
				assert(std::isspace(line[p]));
				do {
					++p;
				} while(std::isspace(line[p]));
				char* eptr;
				errno=0;
				auto v=strtoul(&line[p], &eptr, 10);
				if(errno)
					throw std::runtime_error{"failed to parse uint"};
				p=eptr-&line[0];
				while(p<line.size()) {
					if(std::isspace(line[p]))
						break;
					++p;
				}
				return v;
			};
			auto pfx=get_pfx();
			if(pfx=="mtllib")
				continue;
			if(pfx=="o")
				continue;
			if(pfx=="g")
				continue;
			if(pfx=="usemtl")
				continue;
			if(pfx=="s")
				continue;
			if(pfx=="v") {
				auto x=get_double();
				auto y=get_double();
				auto z=get_double();
				if(p!=line.size())
					throw std::runtime_error{"Wrong number of coords: "};
				gapr::node_attr p{x, y, z};
				auto& v=meshVerts.emplace_back();
				v.ipos=p.ipos;
				v.norm.value=0;
				if(std::isnan(x)||std::isnan(y)||std::isnan(z))
					v.norm.value=1;
			} else if(pfx=="vn") {
				auto x=get_double();
				auto y=get_double();
				auto z=get_double();
				if(p!=line.size())
					throw std::runtime_error{"Wrong number of coords: "};
				vertNormals.emplace_back(x, y, z);
			} else if(pfx=="f") {
				auto a=get_index();
				auto b=get_index();
				auto c=get_index();
				while(std::isspace(line[p]))
					++p;
				if(p!=line.size())
					throw std::runtime_error{"Wrong number of coords: "};
				bool ok=true;
				if(meshVerts[a-1].norm.value
						|| meshVerts[b-1].norm.value
						|| meshVerts[c-1].norm.value)
					ok=false;
				if(ok) {
					meshIdxes.push_back(a-1);
					meshIdxes.push_back(b-1);
					meshIdxes.push_back(c-1);
				}
			} else {
				throw std::runtime_error{"Unknown line: "};
			}
		}
		if(!f.eof())
			throw std::runtime_error{"Failed to read line"};

		gapr::print("nv: ", meshVerts.size(), "; ni: ", meshIdxes.size());
		if(!meshVerts.empty() && !meshIdxes.empty()) {
			if(0) {
			vertNormals.resize(meshVerts.size(), {0.0f, 0.0f, 0.0f});
			double suminv=0.0;
			for(size_t i=0; i+2<meshIdxes.size(); i+=3) {
				auto a=meshIdxes[i];
				auto b=meshIdxes[i+1];
				auto c=meshIdxes[i+2];
				gapr::vec3<double> pa(meshVerts[a].ipos[0], meshVerts[a].ipos[1], meshVerts[a].ipos[2]);
				gapr::vec3<double> pb(meshVerts[b].ipos[0], meshVerts[b].ipos[1], meshVerts[b].ipos[2]);
				gapr::vec3<double> pc(meshVerts[c].ipos[0], meshVerts[c].ipos[1], meshVerts[c].ipos[2]);
				auto na=gapr::cross(pb-pa, pc-pa);
				vertNormals[a]+=na;
				vertNormals[b]+=na;
				vertNormals[c]+=na;
				suminv+=gapr::dot(pa, na);
				suminv+=gapr::dot(pb, na);
				suminv+=gapr::dot(pc, na);
			}
			if(suminv<0) {
				for(auto& n: vertNormals)
					n=-n;
			}
			}
			auto conv_bits=[](double x) ->unsigned int {
				if(std::isnan(x))
					x=0;
				assert(x>=-1);
				assert(x<=1);
				auto b=lround(x*0b01'1111'1111);
				return b&0b11'1111'1111;
			};
			assert(conv_bits(0.0)==0);
			assert(conv_bits(1.0)==0b01'1111'1111);
			assert(conv_bits(-1.0)==0b10'0000'0001);
			auto pack_norm=[conv_bits](const gapr::vec3<double>& v) ->GLuint {
				GLuint r{0};
				for(unsigned int i=0; i<3; ++i) {
					auto b=conv_bits(v[i]);
					r=r|b<<((2-i)*10);
				}
				return r;
			};
			for(size_t i=0; i<meshVerts.size(); i++) {
				auto n=vertNormals[i].mag();
				if(meshVerts[i].norm.value)
					continue;
				meshVerts[i].norm.value=pack_norm(vertNormals[i]/n);
			}
		}
		/*
		   meshVerts.push_back(MeshVert{5000*1024, 0, 0, 0.0, 0.0, 0.0});
		   meshVerts.push_back(MeshVert{0, 5000*1024, 0, 0.0, 0.0, 0.0});
		   meshVerts.push_back(MeshVert{0, 0, 5000*1024, 0.0, 0.0, 0.0});
		   meshIdxes.push_back(0);
		   meshIdxes.push_back(2);
		   meshIdxes.push_back(1);
		   */
		return std::make_pair(std::move(meshVerts), std::move(meshIdxes));
	}

	static int f_commit_infos(lua_State* L) {
		auto argc=lua_gettop(L);
		if(argc!=0)
			return luaL_error(L, "wrong number of arguments");
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));
		if(!sess->_playback)
			return luaL_error(L, "not in playback mode");

		auto& infos=sess->_playback->_infos;
		lua_newtable(L);
		for(std::size_t i=0; i<infos.size(); ++i) {
			lua_newtable(L);
			auto& info=infos[i];
			lua_pushinteger(L, info.id);
			lua_setfield(L, argc+2, "id");
			lua_pushlstring(L, info.who.data(), info.who.size());
			lua_setfield(L, argc+2, "who");
			lua_pushinteger(L, info.when);
			lua_setfield(L, argc+2, "when");
			lua_pushinteger(L, info.nid0);
			lua_setfield(L, argc+2, "nid0");
			auto type=to_string(gapr::delta_type{info.type});
			lua_pushlstring(L, type.data(), type.size());
			lua_setfield(L, argc+2, "type");

			lua_newtable(L);
			gapr::bbox bbox{info.bbox};
			gapr::vec3<double> cent;
			bbox.diameter_and_center(cent);
			for(unsigned int i=0; i<3; ++i) {
				lua_pushnumber(L, cent[i]);
				lua_seti(L, argc+3, i+1);
			}
			lua_setfield(L, argc+2, "pos");

			lua_newtable(L);
			for(std::size_t k=0; k<info.props_mod.size(); ++k) {
				lua_newtable(L);
				auto& [id, key, val, chg]=info.props_mod[k];
				lua_pushinteger(L, id.data);
				lua_setfield(L, argc+4, "node");
				lua_pushlstring(L, key.data(), key.size());
				lua_setfield(L, argc+4, "key");
				if(chg>=0) {
					lua_pushlstring(L, val.data(), val.size());
					lua_setfield(L, argc+4, "val");
				}
				lua_pushinteger(L, chg);
				lua_setfield(L, argc+4, "chg");
				lua_seti(L, argc+3, k+1);
			}
			lua_setfield(L, argc+2, "props_mod");

			lua_seti(L, argc+1, i+1);
		}
		return 1;
	}
	static int f_stats(lua_State* L) {
		auto argc=lua_gettop(L);
		if(argc!=0)
			return luaL_error(L, "wrong number of arguments");
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));

		struct Stats {
			unsigned int loops;
			unsigned int num_nodes;
			unsigned int num_nodes_raw;
			unsigned int num_terms;
			unsigned int num_terms_raw;
			unsigned int num_terms_good;
		};
		std::promise<Stats> prom{};
		auto fut=prom.get_future();
		boost::asio::post(sess->_ctx->main_context(), [sess,prom=std::move(prom)]() mutable {
			if(auto win=sess->_win.lock(); win) {
				Stats s;
				s.loops=sess->_data.loop_cnt;
				s.num_nodes=0;
				s.num_nodes_raw=0;
				s.num_terms=0;
				s.num_terms_raw=0;
				s.num_terms_good=0;
				std::string_view key{"state"};
				auto handle_term=[&s,key](const NodeData& dat) {
					++s.num_terms;
					bool hit=false;
					for(auto& a: dat.annots) {
						if(a.size()<key.size())
							continue;
						if(a.compare(0, key.size(), key.data())!=0)
							continue;
						if(a.size()>key.size()) {
							if(a[key.size()]!='=')
								continue;
							if(a.compare(key.size()+1, a.size()-key.size()-1, "end")==0)
								++s.num_terms_good;
						}
						hit=true;
					}
					if(!hit)
						++s.num_terms_raw;
				};
				for(auto& [nid, dat]: sess->_data.nodes) {
					if(dat.state==DiffState::Del)
						continue;
					++s.num_nodes;
					if(!dat.attr.misc.coverage())
						++s.num_nodes_raw;
					unsigned int links=0;
					for(unsigned int i=0; i<dat.num_links; ++i)
						links+=(dat.link_at(i).state==DiffState::Del)?0:1;
					if(links<2) {
						handle_term(dat);
					} else if(dat.is_root) {
						handle_term(dat);
					}
				}
				prom.set_value(s);
			}
		});
		auto cnt=fut.get();
		lua_newtable(L);
		lua_pushinteger(L, cnt.loops);
		lua_setfield(L, argc+1, "loops");
		lua_pushinteger(L, cnt.num_nodes);
		lua_setfield(L, argc+1, "nodes");
		lua_pushinteger(L, cnt.num_nodes_raw);
		lua_setfield(L, argc+1, "nodes_raw");
		lua_pushinteger(L, cnt.num_terms);
		lua_setfield(L, argc+1, "terms");
		lua_pushinteger(L, cnt.num_terms_raw);
		lua_setfield(L, argc+1, "terms_raw");
		lua_pushinteger(L, cnt.num_terms_good);
		lua_setfield(L, argc+1, "terms_good");
		return 1;
	}

	std::optional<std::promise<void>> _pending_seek;
	static int f_seek(lua_State* L) {
		auto argc=lua_gettop(L);
		if(argc!=1)
			return luaL_error(L, "wrong number of arguments");
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));
		if(!sess->_playback)
			return luaL_error(L, "not in playback mode");
		int isnum;
		uint64_t ii=lua_tointegerx(L, 1, &isnum);
		if(!isnum)
			return luaL_argerror(L, 1, "is not an integer");
		if(ii<0 || ii>sess->_playback->_infos.size())
			return luaL_argerror(L, 1, "out of range");

		std::promise<void> prom{};
		auto fut=prom.get_future();
		boost::asio::post(sess->_ctx->main_context(), [sess,ii,prom=std::move(prom)]() mutable {
			if(auto win=sess->_win.lock(); win) {
				sess->jump_frame(win, ii);
				sess->_script->_pending_seek.emplace(std::move(prom));
			}
		});
		gapr::print("started seek");
		fut.get();
		gapr::print("seek ended");
		return 0;
	}

	struct Surface {
		std::unique_ptr<unsigned char[]> data;
		int width, height, stride;

		void alloc(int w, int h) {
			width=w;
			height=h;
			stride=cairo_format_stride_for_width(CAIRO_FORMAT_RGB30, width);
			assert(stride%(4)==0);
			data=std::make_unique<unsigned char[]>(stride*height);
		}
		void read_pixels() {
			// *** GL_BGRA & GL_UNSIGNED_INT_2_10_10_10_REV
			::glReadPixels(0, 0, stride/(4), height, GL_BGRA, GL_UNSIGNED_INT_2_10_10_10_REV, &data[0]);
			if(auto r=glGetError(); r!=GL_NO_ERROR) {
				gapr::print("cur err: ", r);
				throw std::runtime_error{"failed to read pixels"};
			}
		}
		gtk::ref<cairo_surface_t> to_cairo() {
			return cairo_image_surface_create_for_data(&data[0], CAIRO_FORMAT_RGB30, width, height, stride);
		}
	};
	std::optional<std::pair<std::promise<Surface>, bool>> _pending_ss{};
	static int f_screenshot_raw(lua_State* L) {
		auto argc=lua_gettop(L);
		if(argc!=1)
			return luaL_error(L, "wrong number of arguments");
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));
		std::size_t l;
		auto s=lua_tolstring(L, 1, &l);
		if(!s)
			return luaL_argerror(L, 1, "is not a string");

		std::promise<Surface> prom{};
		auto fut=prom.get_future();
		boost::asio::post(sess->_ctx->main_context(), [sess,prom=std::move(prom)]() mutable {
			if(auto win=sess->_win.lock(); win) {
				sess->update(win);
				sess->_script->_pending_ss.emplace(std::move(prom), true);
			}
		});
		auto surface=fut.get();
		auto surf=surface.to_cairo();
		auto surf_width=surface.width;
		auto surf_height=surface.height;
		gtk::ref pixbuf=gdk_pixbuf_get_from_surface(surf, 0, 0, surf_width, surf_height);
		gtk::ref pixbuf2=gdk_pixbuf_flip(pixbuf, false);
		gtk::ref<GError> err{};
		if(!gdk_pixbuf_save(pixbuf2, s, "png", &err, nullptr)) {
			assert(err.get());
			throw std::runtime_error{err->message};
		}
		return 0;
	}
	static int f_screenshot(lua_State* L) {
		auto argc=lua_gettop(L);
		if(argc<1)
			return luaL_error(L, "wrong number of arguments");
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));
		std::size_t l;
		auto s=lua_tolstring(L, 1, &l);
		if(!s)
			return luaL_argerror(L, 1, "is not a string");

		std::promise<Surface> prom{};
		auto fut=prom.get_future();
		boost::asio::post(sess->_ctx->main_context(), [sess,prom=std::move(prom)]() mutable {
			if(auto win=sess->_win.lock(); win) {
				sess->update(win);
				sess->_script->_pending_ss.emplace(std::move(prom), false);
			}
		});
		auto surface=fut.get();
		auto surf=surface.to_cairo();
		auto surf_width=surface.width;
		auto surf_height=surface.height;
		if(argc>1) {
			gtk::ref cr=cairo_create(surf);
			cairo_translate(cr, 0, surf_height);
			cairo_scale(cr, 1.0, -1.0);
			int txti=2;
			do {
				if(!lua_istable(L, txti))
					return luaL_argerror(L, txti, "is not a table");
				gtk::ref layout=pango_cairo_create_layout(cr);
				if(auto t=lua_geti(L, txti, 1); t!=LUA_TSTRING)
					return luaL_argerror(L, txti, "item #1 is not a string");
				std::size_t txtn;
				auto txtp=lua_tolstring(L, argc+1, &txtn);
				assert(txtp);
				pango_layout_set_markup(layout, txtp, txtn);
				if(auto t=lua_getfield(L, txti, "width"); t!=LUA_TNIL) {
					if(t!=LUA_TNUMBER)
						return luaL_argerror(L, txti, "width is not a number");
					int isnum;
					auto width=lua_tointegerx(L, argc+2, &isnum);
					assert(isnum);
					width=width*PANGO_SCALE;
					pango_layout_set_width(layout, width);
				}
				if(auto t=lua_getfield(L, txti, "justify"); t!=LUA_TNIL) {
					if(t!=LUA_TBOOLEAN)
						return luaL_argerror(L, txti, "justify is not a boolean");
					auto justify=lua_toboolean(L, argc+3);
					pango_layout_set_justify(layout, justify);
				}
				auto handle_str=[L,txti](const char* key, int stk, const char* err) ->std::string_view {
					if(auto t=lua_getfield(L, txti, key); t!=LUA_TNIL) {
						if(t!=LUA_TSTRING)
							luaL_argerror(L, txti, err);
						std::size_t n;
						auto s=lua_tolstring(L, stk, &n);
						assert(s);
						return {s, n};
					}
					return {};
				};
				if(auto s=handle_str("font", argc+4, "font is not a string"); !s.empty()) {
					gtk::ref desc=pango_font_description_from_string(s.data());
					pango_layout_set_font_description(layout, desc);
				}
				if(auto s=handle_str("wrap", argc+5, "wrap  is not a string"); !s.empty()) {
					PangoWrapMode wrap;
					if(s=="word") {
						wrap=PANGO_WRAP_WORD;
					} else if(s=="char") {
						wrap=PANGO_WRAP_CHAR;
					} else if(s=="word-char") {
						wrap=PANGO_WRAP_WORD_CHAR;
					} else {
						return luaL_argerror(L, txti, "wrap cannot be recognized");
					}
					pango_layout_set_wrap(layout, wrap);
				}
				cairo_save(cr);
				cairo_new_path(cr);
				if(auto s=handle_str("color", argc+6, "color is not a string"); !s.empty()) {
					GdkRGBA cc;
					if(!gdk_rgba_parse(&cc, s.data()))
						return luaL_argerror(L, txti, "color cannot be parsed");
					cairo_set_source_rgba(cr, cc.red, cc.green, cc.blue, cc.alpha);
				}
				pango_cairo_update_layout(cr, layout);
				if(auto t=lua_getfield(L, txti, "move_to"); t!=LUA_TNIL) {
					if(t!=LUA_TFUNCTION)
						return luaL_argerror(L, txti, "move_to is not a function");
					int width, height;
					pango_layout_get_size(layout, &width, &height);
					lua_pushnumber(L, surf_width);
					lua_pushnumber(L, surf_height);
					lua_pushnumber(L, width/PANGO_SCALE);
					lua_pushnumber(L, height/PANGO_SCALE);
					lua_callk(L, 4, 2, 0, nullptr);
					int isnum;
					auto x=lua_tonumberx(L, argc+7, &isnum);
					if(!isnum)
						return luaL_argerror(L, txti, "move_to res #1 is not an integer");
					auto y=lua_tonumberx(L, argc+8, &isnum);
					if(!isnum)
						return luaL_argerror(L, txti, "move_to res #2 is not an integer");
					cairo_move_to(cr, x, y);
				}
				lua_pop(L, 8);
				pango_cairo_show_layout(cr, layout);
				cairo_restore(cr);
			} while(++txti<=argc);
		}
		gtk::ref pixbuf=gdk_pixbuf_get_from_surface(surf, 0, 0, surf_width, surf_height);
		gtk::ref pixbuf2=gdk_pixbuf_flip(pixbuf, false);
		gtk::ref<GError> err{};
		if(!gdk_pixbuf_save(pixbuf2, s, "png", &err, nullptr)) {
			assert(err.get());
			throw std::runtime_error{err->message};
		}
		return 0;
	}

	std::optional<std::promise<int>> _pending_mode;
	static int f_mode(lua_State* L) {
		auto argc=lua_gettop(L);
		if(argc!=1)
			return luaL_error(L, "wrong number of arguments");
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));
		std::size_t n;
		auto s=lua_tolstring(L, 1, &n);
		if(!s)
			return luaL_argerror(L, 1, "is not a string");
		std::string_view ss{s, n};

		std::promise<int> prom{};
		auto fut=prom.get_future();
		boost::asio::post(sess->_ctx->main_context(), [sess,ss,prom=std::move(prom)]() mutable {
			if(auto win=sess->_win.lock(); win) {
				if(sess->_cur_mode==ss) {
					prom.set_value(1);
					return;
				}
				if(ss.substr(0, 7)=="script:") {
					sess->change_mode_s(win, &ss[0]+7);
				} else {
				if(!gtk_combo_box_set_active_id(win->select_mode, ss.data())) {
					prom.set_value(0);
					return;
				}
				}
				sess->_script->_pending_mode.emplace(std::move(prom));
			}
		});
		gapr::print("started chgmode");
		auto r=fut.get();
		if(r==0)
			return luaL_argerror(L, 1, "invalid mode");
		gapr::print("chgmode ended");
		return 0;
	}

	std::optional<std::promise<int>> _pending_hide;
	static int f_hide(lua_State* L) {
		auto argc=lua_gettop(L);
		if(argc!=1)
			return luaL_error(L, "wrong number of arguments");
		if(!lua_isboolean(L, 1))
			return luaL_argerror(L, 1, "is not a boolean");
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));
		bool v=lua_toboolean(L, 1);

		std::promise<int> prom{};
		auto fut=prom.get_future();
		boost::asio::post(sess->_ctx->main_context(), [sess,v,prom=std::move(prom)]() mutable {
			if(auto win=sess->_win.lock(); win) {
				if(sess->_canvas.hide_graph==v) {
					prom.set_value(1);
					return;
				}
				sess->_script->_pending_hide.emplace(std::move(prom));
				auto var=g_variant_new_boolean(v);
				g_action_group_change_action_state(G_ACTION_GROUP(win), "hide-graph", var);
			}
		});
		auto r=fut.get();
		if(r==0)
			return luaL_argerror(L, 1, "invalid mode");
		return 0;
	}

	bool _pending_close{false};
	static int f_auto_close(lua_State* L) {
		auto argc=lua_gettop(L);
		if(argc!=0)
			return luaL_error(L, "wrong number of arguments");
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));

		std::promise<void> prom{};
		auto fut=prom.get_future();
		boost::asio::post(sess->_ctx->main_context(), [sess,prom=std::move(prom)]() mutable {
			if(auto win=sess->_win.lock(); win) {
				//gtk_window_close(GTK_WINDOW(win));
				//gtk_widget_destroy(GTK_WIDGET(win));
				sess->_script->_pending_close=true;
				prom.set_value();
			}
		});
		gapr::print("started close");
		fut.get();
		gapr::print("close ended");
		return 0;
	}
	static int f_resize(lua_State* L) {
		auto argc=lua_gettop(L);
		if(argc<2 || argc>3)
			return luaL_error(L, "wrong number of arguments");
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));
		int isnum;
		auto w=lua_tointegerx(L, 1, &isnum);
		if(!isnum)
			return luaL_argerror(L, 1, "is not an integer");
		auto h=lua_tointegerx(L, 2, &isnum);
		if(!isnum)
			return luaL_argerror(L, 2, "is not an integer");
		int s{0};
		if(argc>2) {
			s=lua_tointegerx(L, 3, &isnum);
			if(!isnum)
				return luaL_argerror(L, 3, "is not an integer");
		}

		std::promise<std::pair<int, int>> prom{};
		auto fut=prom.get_future();
		boost::asio::post(sess->_ctx->main_context(), [sess,w,h,s,prom=std::move(prom)]() mutable {
			if(auto win=sess->_win.lock(); win) {
				if(s)
					sess->_canvas_os.scale=s;
				if(sess->_canvas.width==w && sess->_canvas.height==h) {
					prom.set_value({sess->_canvas.width, sess->_canvas.height});
					return;
				}
				/*
				if(force) {
					//change size request
					//hide bbox
				}
				*/
				auto scale=sess->_canvas_s.scale;
				sess->_canvas_os.width=w*scale;
				sess->_canvas_os.height=h*scale;
				prom.set_value({sess->_canvas_os.width, sess->_canvas_os.height});
			}
		});
		auto [ww, hh]=fut.get();
		if(ww!=w || hh!=h)
			return luaL_error(L, "resize failed %d %d", ww, hh);
		return 0;
	}

	static int f_sphere(lua_State* L) {
		auto argc=lua_gettop(L);
		if(argc!=0)
			return luaL_error(L, "wrong number of arguments");
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));

		std::promise<std::array<double, 4>> prom{};
		auto fut=prom.get_future();
		boost::asio::post(sess->_ctx->main_context(), [sess,prom=std::move(prom)]() mutable {
			if(auto win=sess->_win.lock(); win) {
				std::array<double, 4> r;
				for(unsigned int i=0; i<3; ++i)
					r[i]=sess->_camera.center_init[i];
				r[3]=sess->_camera.diameter/2;
				prom.set_value(r);
			}
		});
		auto r=fut.get();
		lua_newtable(L);
		for(int i=1; i<4; ++i) {
			lua_pushnumber(L, r[i-1]);
			lua_seti(L, argc+1, i);
		}
		lua_pushnumber(L, r[3]);
		lua_setfield(L, argc+1, "radius");
		return 1;
	}

	static int f_camera(lua_State* L) {
		auto argc=lua_gettop(L);
		if(argc!=1)
			return luaL_error(L, "wrong number of arguments");
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));
		if(!lua_istable(L, 1))
			return luaL_argerror(L, 1, "is not a table");
		auto get_opt_coords=[L,argc](auto& coords, const char* key, const char* err1, const char* err2) ->auto* {
			auto t=lua_getfield(L, 1, key);
			if(t==LUA_TNIL) {
				lua_pop(L, 1);
				return true?nullptr:&coords[0];
			}
			if(t!=LUA_TTABLE)
				luaL_argerror(L, 1, err1);
			for(int i=1; i<coords.size()+1; ++i) {
				if(auto t=lua_geti(L, argc+1, i); t!=LUA_TNUMBER)
					luaL_argerror(L, 1, err2);
				int isnum;
				auto v=lua_tonumberx(L, argc+1+i, &isnum);
				assert(isnum);
				coords[i-1]=v;
			}
			lua_pop(L, coords.size()+1);
			gapr::print(key, " ", coords[0], ' ', coords[1], ' ', coords[2]);
			return &coords[0];
		};
		std::array<double, 3> center_;
		auto center=get_opt_coords(center_, "center", "center is not a table", "center has invalid value");
		std::array<double, 3> up_;
		auto up=get_opt_coords(up_, "up", "up is not a table", "up has invalid value");
		std::array<double, 3> eye_;
		auto eye=get_opt_coords(eye_, "eye", "eye is not a table", "eye has invalid value");
		std::array<int, 2> off_;
		auto off=get_opt_coords(off_, "offset", "offset is not a table", "offset has invalid value");

		std::promise<void> prom{};
		auto fut=prom.get_future();
		boost::asio::post(sess->_ctx->main_context(), [sess,center,up,eye,off,prom=std::move(prom)]() mutable {
			if(auto win=sess->_win.lock(); win) {
				auto& camera=sess->_camera;
				if(center) {
					for(unsigned int i=0; i<3; ++i)
						camera.center[i]=center[i];
					camera.changed=true;
				}
				if(up) {
					for(unsigned int i=0; i<3; ++i)
						camera.up[i]=up[i];
					camera.changed=true;
				}
				if(eye) {
					double r2{0.0};
					for(unsigned int i=0; i<3; ++i) {
						auto v=eye[i];
						r2+=v*v;
					}
					auto z=camera.zoom=std::sqrt(r2);
					for(unsigned int i=0; i<3; ++i)
						camera.rgt[i]=eye[i]/z;
					camera.changed=true;
				}
				if(off) {
					for(unsigned int i=0; i<2; ++i)
						camera.offset[i]=off[i];
				}
				sess->update_camera();
				sess->update(win);
				prom.set_value();
			}
		});
		fut.get();
		return 0;
	}

	std::optional<std::promise<void>> _pending_image;
	static int f_load_image(lua_State* L) {
		auto argc=lua_gettop(L);
		if(argc!=1)
			return luaL_error(L, "wrong number of arguments");
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));
		if(!sess->_cube_builder)
			return luaL_error(L, "no image channel available");
		int isnum;
		unsigned int ii=lua_tointegerx(L, 1, &isnum);
		if(!isnum)
			return luaL_argerror(L, 1, "is not an integer");
		if(ii<0 || ii>sess->_image.cube_infos.size())
			return luaL_argerror(L, 1, "out of range");
		
		std::promise<void> prom{};
		auto fut=prom.get_future();
		boost::asio::post(sess->_ctx->main_context(), [sess,ii,prom=std::move(prom)]() mutable {
			if(auto win=sess->_win.lock(); win) {
				sess->_image.chan=ii;
				if(ii==0 || !sess->do_refresh_image(win)) {
					sess->_image.changed=true;
					sess->_camera.changed=true;
					sess->update_camera();
					sess->update(win);
					prom.set_value();
					return;
				}
				sess->_script->_pending_image.emplace(std::move(prom));
			}
		});
		gapr::print("started load image");
		fut.get();
		gapr::print("load image ended");
		return 0;
	}
	static int f_contrast(lua_State* L) {
		auto argc=lua_gettop(L);
		if(argc!=2)
			return luaL_error(L, "wrong number of arguments");
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));
		if(!sess->_cube_builder)
			return luaL_error(L, "no image channel available");
		int isnum;
		auto a=lua_tonumberx(L, 1, &isnum);
		if(!isnum)
			return luaL_argerror(L, 1, "is not a number");
		auto b=lua_tonumberx(L, 2, &isnum);
		if(!isnum)
			return luaL_argerror(L, 2, "is not a number");

		std::promise<void> prom{};
		auto fut=prom.get_future();
		boost::asio::post(sess->_ctx->main_context(), [sess,a,b,prom=std::move(prom)]() mutable {
			if(auto win=sess->_win.lock(); win) {
				sess->_image.xfunc_state[0]=a;
				sess->_image.xfunc_state[1]=b;
				prom.set_value();
			}
		});
		fut.get();
		return 0;
	}
	static int f_dark(lua_State* L) {
		auto argc=lua_gettop(L);
		if(argc!=1)
			return luaL_error(L, "wrong number of arguments");
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));
		if(!lua_isboolean(L, 1))
			return luaL_argerror(L, 1, "is not a boolean");
		bool v=lua_toboolean(L, 1);

		std::promise<void> prom{};
		auto fut=prom.get_future();
		boost::asio::post(sess->_ctx->main_context(), [sess,v,prom=std::move(prom)]() mutable {
			if(auto win=sess->_win.lock(); win) {
				if(auto settings=gtk_settings_get_default(); settings!=nullptr) {
					g_object_set(G_OBJECT(settings), "gtk-application-prefer-dark-theme", v, nullptr);
				}
				prom.set_value();
			}
		});
		fut.get();
		return 0;
	}

	static int f_mesh_clear(lua_State* L) {
		auto argc=lua_gettop(L);
		assert(argc==0);
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));

		std::promise<void> prom{};
		auto fut=prom.get_future();
		boost::asio::post(sess->_ctx->main_context(), [sess,prom=std::move(prom)]() mutable {
			if(auto win=sess->_win.lock(); win) {
				sess->_canvas.meshes.clear();
				sess->_canvas.meshes_changed=true;
				std::move(prom).set_value();
			}
		});
		fut.get();
		return 0;
	}
	static int f_mesh(lua_State* L) {
		auto argc=lua_gettop(L);
		if(argc==0)
			return f_mesh_clear(L);
		if(argc!=2)
			return luaL_error(L, "wrong number of arguments");
		auto sess=static_cast<Session*>(lua_touserdata(L, lua_upvalueindex(1)));
		std::size_t n;
		auto s=lua_tolstring(L, 1, &n);
		if(!s)
			return luaL_argerror(L, 1, "is not a string");
		std::string fn{s, n};
		s=lua_tolstring(L, 2, &n);
		if(!s)
			return luaL_argerror(L, 2, "is not a string");
		std::string_view col_str{s, n};
		GdkRGBA col;
		if(!gdk_rgba_parse(&col, col_str.data()))
			return luaL_argerror(L, 2, "color cannot be parsed");

		std::promise<void> prom{};
		auto fut=prom.get_future();
		boost::asio::post(sess->_ctx->thread_pool(), [sess,prom=std::move(prom),fn=std::move(fn),col]() mutable {
			auto [verts, idxes]=load_mesh(fn.c_str());
			boost::asio::post(sess->_ctx->main_context(), [sess,prom=std::move(prom),col,verts=std::move(verts),idxes=std::move(idxes)]() mutable {
				if(auto win=sess->_win.lock(); win) {
					auto& mesh=sess->_canvas.meshes.emplace_back();
					mesh.changed=true;
					mesh.color[0]=col.red;
					mesh.color[1]=col.green;
					mesh.color[2]=col.blue;
					mesh.color[3]=col.alpha;
					mesh.idxes=std::move(idxes);
					mesh.verts=std::move(verts);
					sess->_canvas.meshes_changed=true;
					std::move(prom).set_value();
				}
			});
		});
		gapr::print("started mesh");
		fut.get();
		gapr::print("mesh ended");
		return 0;
	}

	std::string run(const char* file, Session* sess) {
		auto L=::lua_newstate(lua_alloc_f, this);
		if(!L)
			throw std::runtime_error{"failed to create lua state"};
		std::unique_ptr<lua_State, lua_State_Del> lua_{L};
		::lua_atpanic(L, lua_panic);
		auto ver=::lua_version(L);
		luaL_openlibs(L);
		std::cerr<<"lua "<<ver<<" ...\n";
		luaL_Reg funcs[]={
			{"commit_infos", f_commit_infos},
			{"stats", f_stats},
			{"seek", f_seek},
			{"screenshot", f_screenshot},
			{"screenshot_raw", f_screenshot_raw},
			{"mode", f_mode},
			{"hide", f_hide},
			{"auto_close", f_auto_close},
			{"resize", f_resize},
			{"sphere", f_sphere},
			{"camera", f_camera},
			{"load_image", f_load_image},
			{"contrast", f_contrast},
			{"dark", f_dark},
			{"mesh", f_mesh},
			{nullptr, nullptr},
		};
		luaL_checkversion(L);
		luaL_newlibtable(L, funcs);
		lua_pushlightuserdata(L, sess);
		luaL_setfuncs(L, funcs, 1);
		lua_setglobal(L, "gapr");
		if(auto r=luaL_loadfilex(L, file, "t"); r!=LUA_OK) {
			if(r==LUA_ERRSYNTAX) {
				auto r=lua_gettop(L);
				gapr::print("syntax err ", lua_gettop(L));
				std::size_t msgl;
				auto msg=lua_tolstring(L, 1, &msgl);
				if(msg)
					throw std::runtime_error{get_stack(L, r)};
			}
			report_lua_error(r);
		}
		gapr::print("after load ", std::this_thread::get_id());
		if(auto r=lua_pcallk(L, 0, LUA_MULTRET, 0, 0, nullptr); r!=LUA_OK) {
			if(r==LUA_ERRRUN) {
				if(auto r=lua_gettop(L); r>0)
					throw std::runtime_error{get_stack(L, r)};
			}
				if(auto r=lua_gettop(L); r>0)
					throw std::runtime_error{get_stack(L, r)};
			report_lua_error(r);
		}
		if(auto r=lua_gettop(L); r>0)
			return get_stack(L, r);
		return {};
	}
};

inline void gapr::show::Session::update(MainWindow* win) {
	gapr::print("update");
	gtk_gl_area_queue_render(win->canvas);
}

///////////////////

GtkWindow* gapr::show::Session::create_window(GtkApplication* app) {
	gapr::print("before new ", this);
	auto window=g_object_new(gapr::show::main_window_get_type(),
				"application", app,
				"cpp-ptr", this,
				NULL);
	gapr::print("after new");
	return static_cast<GtkWindow*>(window);
}

void gapr::show::Session::resize(MainWindow* win, int width, int height) {
	_canvas.width=width/_canvas_s.scale;
	_canvas.height=height/_canvas_s.scale;
	if(check_fbo_resize(width, height, _canvas_s)) {
		auto [width_alloc, height_alloc]=get_fbo_alloc(width, height);
		_canvas_s.width_alloc=width_alloc;
		_canvas_s.height_alloc=height_alloc;
		_canvas_s.fbo_opaque.resize(width_alloc, height_alloc);
		auto scale=_canvas_s.scale;
		_canvas.fbo_volume.resize(width_alloc/scale, height_alloc/scale);
		_canvas_s.fbo_edges.resize(width_alloc, height_alloc);
	}

	glViewport(0, 0, width, height);
	_camera.changed=true;
	update_camera();
}

void gapr::show::Session::scroll_event(MainWindow* win, double delta) {
	_camera.set_zoom(_camera.zoom*std::exp(0.05*delta));
	_camera.changed=true;
	update_camera();
	update(win);
}

void gapr::show::Session::button_press_event(MainWindow* win, unsigned int button, double x, double y, bool shift, bool ctrl, bool simple) {
	switch(_mouse.state) {
		case MouseEmpty:
			break;
		case MouseIgnore:
			if(simple)
				++_mouse.press_cnt;
			return;
		default:
			if(simple)
				++_mouse.press_cnt;
			else
				_mouse.state=MouseIgnore;
			return;
	}
	switch(button) {
		case GDK_BUTTON_PRIMARY:
			if(!ctrl) {
				if(shift) {
					_mouse.state=MousePanSel;
					_camera.center_save=_camera.center;
					_camera.rview_rproj_save=_camera.rview*_camera.rproj;
				} else {
					_mouse.state=MouseRotSel;
					_camera.rgt_save=_camera.rgt;
					_camera.up_save=_camera.up;
					_camera.rview_rproj_save=_camera.rview*_camera.rproj;
				}
				break;
			}
			goto IGNORE_PRESS;
		case GDK_BUTTON_MIDDLE:
			if(!ctrl) {
				if(shift) {
					_mouse.state=MouseZoomRst;
				} else {
					_mouse.state=MouseZoom;
					_camera.zoom_save=_camera.zoom;
				}
				break;
			}
			goto IGNORE_PRESS;
		case GDK_BUTTON_SECONDARY:
			if(!ctrl) {
				_mouse.state=shift?MouseCenter:MouseMenu;
				break;
			}
			goto IGNORE_PRESS;
		default:
IGNORE_PRESS:
			_mouse.state=MouseIgnore;
			break;
	}
	_mouse.button=button;
	_mouse.press_cnt=1;
	_mouse.press_x=x;
	_mouse.press_y=y;
}
int64_t gapr::show::Session::pick_node(MainWindow* win, int x, int y) {
	int64_t id{-1};
	auto scale=_canvas_s.scale;
	auto width=_canvas.width*scale;
	auto height=_canvas.height*scale;
	int pick_size=PICK_SIZE*scale;
	x*=scale;
	y*=scale;
	gtk_gl_area_make_current(win->canvas);
	if(x-pick_size>=0 && y-pick_size>=0
			&& y+pick_size<height
			&& x+pick_size<width) {
		int pickdr2=pick_size*pick_size+1;
		int pickI=-1;

		do {
			glBindFramebuffer(GL_FRAMEBUFFER, _canvas_s.fbo_pick);
			glViewport(0, 0, 2*pick_size+1, 2*pick_size+1);
			glClearColor(0.0, 0.0, 0.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
			if(!_data.ready || _canvas.hide_graph)
				break;
			auto zoom=_camera.zoom;
			auto centx=(2.0*x/width-1.0)*zoom;
			auto centy=(1.0-2.0*y/height)*zoom*height/width;
			auto span=pick_size*zoom/width;
			mat4<GLfloat> mProj2;
			mProj2.ortho(centx-span, centx+span, centy-span, centy+span, (1-.5)*zoom, (1+.5)*zoom);

			glBindVertexArray(_canvas.vao_edges);
			glUseProgram(_canvas_s.pick_vert.prog);
			glUniformMatrix4fv(_canvas_s.pick_vert.proj, 1, GL_FALSE, &mProj2(0, 0));
			glUniformMatrix4fv(_canvas_s.pick_vert.view, 1, GL_FALSE, &_camera.view(0, 0));
			glUniform1ui(_canvas_s.pick_vert.id, 2);
			glUniform1f(_canvas_s.pick_vert.thick, _camera.thickness);
			glUniform3iv(_canvas_s.pick_vert.center, 1, &_camera.icenter[0]);
			glDrawArrays(GL_POINTS, 0, _data.nverts);

			glUseProgram(_canvas_s.pick_edge.prog);
			glUniformMatrix4fv(_canvas_s.pick_edge.proj, 1, GL_FALSE, &mProj2(0, 0));
			glUniformMatrix4fv(_canvas_s.pick_edge.view, 1, GL_FALSE, &_camera.view(0, 0));
			glUniform1ui(_canvas_s.pick_edge.id, 1);
			glUniform1f(_canvas_s.pick_edge.thick, 1*_camera.thickness);
			glUniform3iv(_canvas_s.pick_edge.center, 1, &_camera.icenter[0]);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _canvas.vbo_idx);
			glMultiDrawElements(GL_LINE_STRIP, _data.vcount.data(), GL_UNSIGNED_INT, _data.vfirst.data(), _data.vcount.size());
			glFlush();
		} while(false);

		std::vector<GLuint> bufIdx((2*pick_size+1)*(2*pick_size+1));
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glReadPixels(0, 0, pick_size*2+1, pick_size*2+1, GL_RED_INTEGER, GL_UNSIGNED_INT, &bufIdx[0]);

		for(int dy=-pick_size; dy<=pick_size; dy++) {
			for(int dx=-pick_size; dx<=pick_size; dx++) {
				int i=dx+pick_size+(dy+pick_size)*(2*pick_size+1);
				if(bufIdx[i]>0) {
					auto dr2=dx*dx+dy*dy;
					if(dr2<pickdr2) {
						pickI=i;
						pickdr2=dr2;
					}
				}
			}
		}

		if(pickI>=0) {
			std::vector<GLint> bufPos((2*pick_size+1)*(2*pick_size+1));
			glReadBuffer(GL_COLOR_ATTACHMENT1);
			glReadPixels(0, 0, pick_size*2+1, pick_size*2+1, GL_RED_INTEGER, GL_INT, &bufPos[0]);
			auto v1=bufIdx[pickI];
			auto v2=bufPos[pickI];
			gapr::print("select ", v1, ' ', v2);
			if(v1==1) {
				auto idx=(v2+8)/16;
				id=_data.ids[idx];
			} else if(v1==2) {
				id=_data.ids[v2];
			}
		}
	}
	return id;
}
void gapr::show::Session::button_release_event(MainWindow* win, unsigned int button, double x, double y) {
	auto check_click=[this](double x, double y) {
		auto dx=x-_mouse.press_x;
		auto dy=y-_mouse.press_y;
		return dx*dx+dy*dy<3*3;
	};
	--_mouse.press_cnt;
	switch(_mouse.state) {
		case MouseEmpty:
			//assert(0);
			return;
		case MouseIgnore:
			if(_mouse.press_cnt==0)
				_mouse.state=MouseEmpty;
			return;
		case MouseMenu:
			if(button==_mouse.button) {
				if(check_click(x, y)) {
					gapr::print("show menu");
				}
				break;
			}
			assert(_mouse.press_cnt>0);
			return;
		case MouseCenter:
			if(button==_mouse.button) {
				if(check_click(x, y)) {
					vec3<double> p{x*2/_canvas.width-1,
						1-y*2/_canvas.height, 0.0};
					p=_camera.rview*(_camera.rproj*p);
					_camera.center+=p;
					_camera.changed=true;
					update_camera();
					update(win);
				}
				break;
			}
			assert(_mouse.press_cnt>0);
			return;
		case MouseRotSel:
			if(button==_mouse.button) {
				if(check_click(x, y)) {
					auto id=pick_node(win, x, y);
					show_node(win, id);
				}
				break;
			}
			assert(_mouse.press_cnt>0);
			return;
		case MousePanSel:
			if(button==_mouse.button) {
				if(check_click(x, y)) {
					auto id=pick_node(win, x, y);
					show_node(win, id);
					if(id!=-1) {
						auto& nd=_data.nodes.at(id);
						for(unsigned int i=0; i<3; i++)
							_camera.center[i]=nd.pos[i];
						_camera.changed=true;
						update_camera();
						update(win);
					}
				}
				break;
			}
			assert(_mouse.press_cnt>0);
			return;
		case MouseZoom:
			if(button==_mouse.button) {
				// XXX
				break;
			}
			assert(_mouse.press_cnt>0);
			return;
		case MouseZoomRst:
			if(button==_mouse.button) {
				if(check_click(x, y)) {
					_camera.set_zoom(_camera.diameter/2*std::sqrt(3));
					_camera.changed=true;
					update_camera();
					update(win);
				}
				break;
			}
			assert(_mouse.press_cnt>0);
			return;
	}
	_mouse.state=(_mouse.press_cnt==0?MouseEmpty:MouseIgnore);
}
void gapr::show::Session::motion_notify_event(MainWindow* win, double x, double y) {
	//assert(_mouse.press_cnt>0);
	switch(_mouse.state) {
		case MouseEmpty:
			//assert(0);
			return;
		case MouseIgnore:
			return;
		case MouseMenu:
			break;
		case MouseCenter:
			break;
		case MouseRotSel:
			{
				vec3<double> a{_mouse.press_x*2/_canvas.width-1,
					1-_mouse.press_y*2/_canvas.height, -1.0};
				vec3<double> b{x*2/_canvas.width-1,
					1-y*2/_canvas.height, -1.0};
				a=_camera.rview_rproj_save*a;
				a=a/a.mag();
				b=_camera.rview_rproj_save*b;
				b=b/b.mag();
				// XXX a==b???
				auto norm=cross(a, b);
				if(norm.mag2()<1e-7)
					norm[0]+=1e-3;
				auto proj=dot(a, b);
				if(proj<-1) proj=-1;
				if(proj>1) proj=1;
				auto r=/*(180/M_PI)*/std::acos(proj);
				mat4<double> mat;
				mat.rotate(-r, norm);
				auto up=mat*_camera.up_save;
				_camera.up=up/up.mag();
				auto rgt=mat*_camera.rgt_save;
				_camera.rgt=rgt/rgt.mag();
				_camera.changed=true;
				update_camera();
				update(win);
			}
			break;
		case MousePanSel:
			{
				vec3<double> c{(_mouse.press_x-x)*2/_canvas.width,
					-(_mouse.press_y-y)*2/_canvas.height, 0.0};
				c=_camera.rview_rproj_save*c;
				_camera.center=_camera.center_save+c;
				_camera.changed=true;
				update_camera();
				update(win);
			}
			break;
		case MouseZoom:
			_camera.set_zoom(_camera.zoom_save*std::exp(0.01*(y-_mouse.press_y)));
			_camera.changed=true;
			update_camera();
			update(win);
			break;
		case MouseZoomRst:
			break;
	}
}
void gapr::show::Session::update_camera() {
	if(!_camera.changed)
		return;
	{
		gapr::print("center: ", _camera.center);
		gapr::print("up: ", _camera.up);
		gapr::print("eye: ", _camera.zoom*_camera.rgt);
	}
	gapr::node_attr attr{_camera.center[0], _camera.center[1], _camera.center[2]};
	for(unsigned int i=0; i<3; i++)
		_camera.icenter[i]=attr.ipos[i];
	auto zoom=_camera.zoom;
	_camera.view.look_at(zoom*_camera.rgt, {0.0, 0.0, 0.0}, _camera.up, &_camera.rview);
	double vspan=zoom*_canvas.height/_canvas.width;
	_camera.proj.ortho(-zoom, zoom, -vspan, vspan, 0.5*zoom, 1.5*zoom, &_camera.rproj);
	_camera.thickness=zoom/_canvas.width;

	auto get_cube_mat_inv=[this](const gapr::cube& cube, gapr::cube_info& info, std::array<unsigned int, 3> offset) {
		gapr::affine_xform* xform=&info.xform;
		vec3<double> p0;
		for(unsigned int i=0; i<3; i++) {
			p0[i]=_camera.center[i]-xform->origin[i];
			auto r=xform->resolution[i];
			if(r<_camera.resolution)
				_camera.resolution=r;
		}
		std::array<std::size_t, 3> size;
		auto cube_view=cube.view<const char>();
		size={cube_view.width_adj(), cube_view.sizes(1), cube_view.sizes(2)};

		mat4<double> cube_mat;
		for(int i=0; i<4; i++)
			cube_mat(3, i)=i<3?0:1;
		auto& rdir=xform->direction_inv;
		for(int i=0; i<3; i++) {
			double v=0.0;
			for(int j=0; j<3; j++) {
				cube_mat(i, j)=rdir[i+j*3]/size[i];
				v+=rdir[i+j*3]*p0[j];
			}
			cube_mat(i, 3)=(v-offset[i])/size[i];
		}
		return cube_mat*_camera.rview*_camera.rproj;
	};

	if(_image.closeup_cube && _image.closeup_chan==_image.chan) {
		_camera.cube_mat_inv=get_cube_mat_inv(_image.closeup_cube, _image.cube_infos[_image.chan-1], _image.closeup_offset);
	} else if(_image.global_cube && _image.global_chan==_image.chan) {
		_camera.cube_mat_inv=get_cube_mat_inv(_image.global_cube, _image.cube_infos[_image.chan-1], {0, 0, 0});
	}
	_camera.changed=false;
}

void gapr::show::Session::render(MainWindow* win) {
	if(_data.changed) {
		glBindBuffer(GL_ARRAY_BUFFER, _canvas.vao_edges.buffer());
		glBufferData(GL_ARRAY_BUFFER, sizeof(PointGL)*_data.points.size(), _data.points.data(), GL_STATIC_DRAW);
		std::vector<GLuint> indices;
		indices.reserve(_data.points.size());
		for(std::size_t i=0; i<_data.points.size(); i++)
			indices.push_back(i);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _canvas.vbo_idx);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLuint)*indices.size(), indices.data(), GL_STATIC_DRAW);
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err buffer"};
		_data.changed=false;
	}

	if(_image.changed) {
		if(_image.closeup_cube && _image.closeup_chan==_image.chan) {
			glBindTexture(GL_TEXTURE_3D, _canvas.cube_tex);
			gapr::gl::upload_texture3d(_image.closeup_cube);
		}
		if(_image.global_cube && _image.global_chan==_image.chan) {
			glBindTexture(GL_TEXTURE_3D, _canvas.cube_tex);
			gapr::gl::upload_texture3d(_image.global_cube);
		}
		_image.changed=false;
	}

	if(_canvas.meshes_changed) {
		for(auto& mesh: _canvas.meshes) {
			if(mesh.changed) {
				mesh.vao.create(&MeshVert::ipos, &MeshVert::norm);
				//gl(EnableVertexAttribArray, funcs)(2);
				//gl(VertexAttribIPointer, funcs)(2, 1, GL_UNSIGNED_SHORT, sizeof(MeshVert), static_cast<char*>(nullptr)+offsetof(MeshVert, id));
				//gl(EnableVertexAttribArray, funcs)(3);
				//gl(VertexAttribIPointer, funcs)(3, 1, GL_UNSIGNED_SHORT, sizeof(MeshVert), static_cast<char*>(nullptr)+offsetof(MeshVert, par));
				//glBindBuffer(GL_ARRAY_BUFFER, mesh.vao.buffer());
				glBufferData(GL_ARRAY_BUFFER, sizeof(MeshVert)*mesh.verts.size(), &mesh.verts[0], GL_STATIC_DRAW);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.vao.element());
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLuint)*mesh.idxes.size(), &mesh.idxes[0], GL_STATIC_DRAW);
				mesh.count=mesh.idxes.size();
				mesh.idxes.clear();
				mesh.verts.clear();
				mesh.changed=false;
			}
		}
		_canvas.meshes_changed=false;
	}
	if(_script && _script->_pending_ss && _canvas_os.width>0 && _canvas_os.height>0) {
		resize(win, _canvas_os.width, _canvas_os.height);
	}

	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

	auto render_verts=[](auto& _canvas, auto& _data, auto& _camera, auto& _canvas_x) {
		glBindFramebuffer(GL_FRAMEBUFFER, _canvas_x.fbo_edges);
		glViewport(0, 0, _canvas.width*_canvas_x.scale, _canvas.height*_canvas_x.scale);
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
		if(!_data.ready || _canvas.hide_graph)
			return;
		/*
			mat4<double> mrot;
			mrot.rotate(_rot, {1.0, 0.0, 0.0});
			{
			}
			mview=mview*mrot;
			*/

		glBindVertexArray(_canvas.vao_edges);
		glUseProgram(_canvas.prog_verts);
		glUniformMatrix4fv(_canvas.prog_verts_proj, 1, GL_FALSE, &_camera.proj(0, 0));
		glUniformMatrix4fv(_canvas.prog_verts_view, 1, GL_FALSE, &_camera.view(0, 0));
		glUniform1f(_canvas.prog_verts_thick, _camera.thickness);
		glUniform3iv(_canvas.prog_verts_center, 1, &_camera.icenter[0]);
		if(!_data.colors.empty())
			glUniform3fv(_canvas.prog_verts_color0, _data.colors.size(), &_data.colors[0][0]);
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err set uniform"};
		glDrawArrays(GL_POINTS, 0, _data.nverts);
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err draw"};

		glUseProgram(_canvas.prog_edges);
		glUniformMatrix4fv(_canvas.prog_edges_proj, 1, GL_FALSE, &_camera.proj(0, 0));
		glUniformMatrix4fv(_canvas.prog_edges_view, 1, GL_FALSE, &_camera.view(0, 0));
		glUniform1f(_canvas.prog_edges_thick, 1*_camera.thickness);
		glUniform3iv(_canvas.prog_edges_center, 1, &_camera.icenter[0]);
		if(!_data.colors.empty())
			glUniform3fv(_canvas.prog_edges_color0, _data.colors.size(), &_data.colors[0][0]);
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err set uniform"};
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _canvas.vbo_idx);
		glMultiDrawElements(GL_LINE_STRIP, _data.vcount.data(), GL_UNSIGNED_INT, _data.vfirst.data(), _data.vcount.size());
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err draw"};
	};
	auto render_cubes=[](auto& _image, auto& _canvas, auto& _camera) {
		auto calc_xfunc=[&_image](std::size_t chan) ->std::array<double, 2> {
			auto& st=_image.xfunc_state;
			auto& range=_image.cube_infos[chan-1].range;
			auto d=range[1]-range[0];
			return {range[0]+st[0]*d, range[1]+(st[1]-1)*d};
		};

		glBindFramebuffer(GL_FRAMEBUFFER, _canvas.fbo_volume);
		glViewport(0, 0, _canvas.width, _canvas.height);
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
		if(!_canvas.cube_tex || !_image.chan)
			return;
		//if(_image.closeup_cube && _image.closeup_chan==_image.chan) {
		//}
		//if(_image.global_cube && _image.global_chan==_image.chan) {
		//}
		glDisable(GL_DEPTH_TEST);
		//glBindFramebuffer(GL_FRAMEBUFFER, fbo_cubes);
		//glViewport(0, 0, fbo_width/VOLUME_SCALE, fbo_height/VOLUME_SCALE);
		//glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

		glUseProgram(_canvas.prog_volume);
		vec3<GLfloat> color{1.0, 1.0, 1.0};
		glUniform3fv(_canvas.prog_volume_color, 1, &color[0]);
		glBindVertexArray(_canvas.vao_quad);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, _canvas.cube_tex);
		auto xfunc=calc_xfunc(_image.chan);
		xfunc[1];
		gapr::print("paint volume: ", xfunc[0], ':', xfunc[1]);
		glUniform2f(_canvas.prog_volume_xfunc, xfunc[0], xfunc[1]);
		glUniformMatrix4fv(_canvas.prog_volume_mat_inv, 1, false, &_camera.cube_mat_inv(0, 0));
		{
			auto mvs=_camera.resolution;
			auto radU=_camera.zoom;
			auto slice_pars=std::array<int, 2>{480, 480};//_slice_pars;
			bool _slice_mode=false;
			int slice_delta=0;
			if(_slice_mode) {
				glUniform3f(_canvas.prog_volume_zpars, -1.0*slice_delta/slice_pars[1],
						-1.0*slice_delta/slice_pars[1], std::max(1.0/slice_pars[1], 9*mvs/radU/8));
			} else {
				glUniform3f(_canvas.prog_volume_zpars, -1.0*slice_pars[0]/slice_pars[1],
						1.0*slice_pars[0]/slice_pars[1], std::max(1.0/slice_pars[1], 9*mvs/radU/8));
			}
		}
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glEnable(GL_DEPTH_TEST);
	};

	auto render_opaque=[](auto& _canvas, auto& _canvas_x) {
		glBindFramebuffer(GL_FRAMEBUFFER, _canvas_x.fbo_opaque);
		glViewport(0, 0, _canvas.width*_canvas_x.scale, _canvas.height*_canvas_x.scale);
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
	};

	auto render_meshes=[](auto& _canvas, auto& _camera, auto& _canvas_x, auto& meshes) {
		if(meshes.empty())
			return;
		//glBindFramebuffer(GL_FRAMEBUFFER, _canvas_x.fbo_opaque);
		glBindFramebuffer(GL_FRAMEBUFFER, _canvas_x.fbo_opaque);
		glViewport(0, 0, _canvas.width*_canvas_x.scale, _canvas.height*_canvas_x.scale);
		//glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

		glDisable(GL_BLEND);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		//glEnable(GL_CULL_FACE);
		glUseProgram(_canvas.prog_mesh);
		glUniform3iv(_canvas.prog_mesh_center, 1, &_camera.icenter[0]);
		glUniformMatrix4fv(_canvas.prog_mesh_proj, 1, GL_FALSE, &_camera.proj(0, 0));
		glUniformMatrix4fv(_canvas.prog_mesh_view, 1, GL_FALSE, &_camera.view(0, 0));

		for(auto& mesh: meshes) {
			glUniform4fv(_canvas.prog_mesh_color, 1, &mesh.color[0]);
			glBindVertexArray(mesh.vao);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.vao.element());
			glDrawElements(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr);
		}
		glEnable(GL_BLEND);
	};

	auto render_finish=[](auto& _canvas, auto& _canvas_x, auto& _camera) {
		glUseProgram(_canvas.prog_sort);
		glUniform1i(_canvas.prog_sort_vol_scale, _canvas_x.scale);
		glUniform2iv(_canvas.prog_sort_offset, 1, &_camera.offset[0]);
		glBindVertexArray(_canvas.vao_quad);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, _canvas_x.fbo_edges.depth());
		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, _canvas_x.fbo_edges.color());
		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_2D, _canvas_x.fbo_opaque.depth());
		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_2D, _canvas_x.fbo_opaque.color());
		glActiveTexture(GL_TEXTURE6);
		glBindTexture(GL_TEXTURE_2D, _canvas.fbo_volume.depth());
		glActiveTexture(GL_TEXTURE7);
		glBindTexture(GL_TEXTURE_2D, _canvas.fbo_volume.color());
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	};

	render_verts(_canvas, _data, _camera, _canvas_s);
	render_cubes(_image, _canvas, _camera);
	render_opaque(_canvas, _canvas_s);
	render_meshes(_canvas, _camera, _canvas_s, _canvas.meshes);
	gtk_gl_area_attach_buffers(win->canvas);
	glViewport(0, 0, _canvas_s.width_alloc*_canvas_s.scale, _canvas_s.height_alloc*_canvas_s.scale);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
	//glViewport(0, 0, (_canvas.width_px*_canvas_x.scale), (_canvas.height_px*_canvas_x.scale));
	render_finish(_canvas, _canvas_s, _camera);

#if 0
	boost::asio::post(_ctx->main_context(), [this,ptr=shared_from_this()]() {
		_rot+=0.01;
		update();
	});
#endif
	if(_script && _script->_pending_ss) {
		auto width=_canvas.width*_canvas_os.scale;
		auto height=_canvas.height*_canvas_os.scale;
		if(!_canvas_os.has_fbos) {
			auto [width_alloc, height_alloc]=get_fbo_alloc(width, height);
			_canvas_os.width_alloc=width_alloc;
			_canvas_os.height_alloc=height_alloc;
			_canvas_os.fbo_opaque.create(width_alloc, height_alloc);
			_canvas_os.fbo_edges.create(width_alloc, height_alloc);
			_canvas_os.fbo_offscreen.create(width_alloc, height_alloc);
			_canvas_os.fbo_resize.create(width_alloc/_canvas_os.scale, height_alloc/_canvas_os.scale);
			_canvas_os.has_fbos=true;
		} else if(check_fbo_resize(width, height, _canvas_os)) {
			auto [width_alloc, height_alloc]=get_fbo_alloc(width, height);
			_canvas_os.width_alloc=width_alloc;
			_canvas_os.height_alloc=height_alloc;
			_canvas_os.fbo_opaque.resize(width_alloc, height_alloc);
			_canvas_os.fbo_edges.resize(width_alloc, height_alloc);
			_canvas_os.fbo_offscreen.resize(width_alloc, height_alloc);
			_canvas_os.fbo_resize.resize(width_alloc/_canvas_os.scale, height_alloc/_canvas_os.scale);
			//_camera.changed=true;
			//update_camera();
		}
		render_verts(_canvas, _data, _camera, _canvas_os);
		render_opaque(_canvas, _canvas_os);
		render_meshes(_canvas, _camera, _canvas_os, _canvas.meshes);
		glBindFramebuffer(GL_FRAMEBUFFER, _canvas_os.fbo_offscreen);
		glViewport(0, 0, _canvas_os.width_alloc*_canvas_os.scale, _canvas_os.height_alloc*_canvas_os.scale);
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
		render_finish(_canvas, _canvas_os, _camera);
		auto [prom, raw]=std::move(*_script->_pending_ss);
		_script->_pending_ss.reset();
		script_helper::Surface surf;
		if(raw) {
			surf.alloc(_canvas.width*_canvas_os.scale, _canvas.height*_canvas_os.scale);
		} else {
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _canvas_os.fbo_resize);
			glBlitFramebuffer(0, 0, _canvas.width*_canvas_os.scale, _canvas.height*_canvas_os.scale, 0, 0, _canvas.width, _canvas.height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, _canvas_os.fbo_resize);
			surf.alloc(_canvas.width, _canvas.height);
		}
		surf.read_pixels();
		prom.set_value(std::move(surf));
	}
}

void gapr::show::Session::init_opengl(MainWindow* win, int scale, int width, int height) {
	auto [width_alloc, height_alloc]=get_fbo_alloc(width, height);
	_canvas_s.scale=scale;
	_canvas.width=width/_canvas_s.scale;
	_canvas.height=height/_canvas_s.scale;
	//auto vol_scale_factor=0 1 2 3 ...;
	_canvas_s.width_alloc=width_alloc;
	_canvas_s.height_alloc=height_alloc;

	_canvas_s.fbo_opaque.create(width_alloc, height_alloc);
	_canvas.fbo_volume.create(width_alloc/scale, height_alloc/scale);
	_canvas_s.fbo_edges.create(width_alloc, height_alloc);
	_canvas_s.fbo_pick.create(128, 128);
#if 0
	if(!pbiFixed) {
		pbiFixed=createPbi();
		glBindBuffer(GL_ARRAY_BUFFER, pathBuffers[pbiFixed].buffer());
		glBufferData(GL_ARRAY_BUFFER, (9*24+2)*sizeof(PointGL), nullptr, GL_STATIC_DRAW);
	}
	if(!pbiPath) {
		pbiPath=createPbi();
	}
	_vao_fixed=viewerShared->get_vao_fixed();
#endif

	glClearDepthf(1.0);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	{
		struct Type {
			std::array<GLfloat, 2> pt;
		} data[]={
			{-1.0, -1.0},
			{1.0, -1.0},
			{-1.0, 1.0},
			{1.0, 1.0}
		};
		_canvas.vao_quad.create(&Type::pt);
		glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_STATIC_DRAW);
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err buffer"};
		_canvas.vao_edges.create(&PointGL::ipos, &PointGL::misc);
		glGenBuffers(1, &_canvas.vbo_idx);
	}
	auto create_prog=[](Program& prog, const char* pvert, const char* pgeom, const char* pfrag, std::string_view defs) {
		gapr::gl::Shader<Funcs> vert, geom, frag;
		vert.create(gtk::Resources{}, GL_VERTEX_SHADER, pvert, defs);
		geom.create(gtk::Resources{}, GL_GEOMETRY_SHADER, pgeom, defs);
		frag.create(gtk::Resources{}, GL_FRAGMENT_SHADER, pfrag, defs);
		prog.create({vert, geom, frag});
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err link2"};
		vert.destroy();
		geom.destroy();
		frag.destroy();
	};
	std::string_view defs{"#define DIFF_MODE"};
	{
		create_prog(_canvas.prog_edges, APPLICATION_PATH "/glsl/edge.vert", APPLICATION_PATH "/glsl/edge.geom", APPLICATION_PATH "/glsl/edge.frag", defs);
		_canvas.prog_edges_center=_canvas.prog_edges.uniformLocation("pos_offset");
		_canvas.prog_edges_color0=_canvas.prog_edges.uniformLocation("colors[0]");
		_canvas.prog_edges_proj=_canvas.prog_edges.uniformLocation("mProj");
		_canvas.prog_edges_thick=_canvas.prog_edges.uniformLocation("thickness");
		_canvas.prog_edges_view=_canvas.prog_edges.uniformLocation("mView");
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc"};
	}
	{
		create_prog(_canvas.prog_verts, APPLICATION_PATH "/glsl/vert.vert", APPLICATION_PATH "/glsl/vert.geom", APPLICATION_PATH "/glsl/vert.frag", defs);
		_canvas.prog_verts_center=_canvas.prog_verts.uniformLocation("pos_offset");
		_canvas.prog_verts_color0=_canvas.prog_verts.uniformLocation("colors[0]");
		_canvas.prog_verts_proj=_canvas.prog_verts.uniformLocation("mProj");
		_canvas.prog_verts_thick=_canvas.prog_verts.uniformLocation("thickness");
		_canvas.prog_verts_view=_canvas.prog_verts.uniformLocation("mView");
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc"};
	}
	std::string_view pick_defs{"#define DIFF_MODE\n#define PICK_MODE"};
	{
		create_prog(_canvas_s.pick_edge.prog, APPLICATION_PATH "/glsl/edge.vert", APPLICATION_PATH "/glsl/edge.geom", APPLICATION_PATH "/glsl/edge.frag", pick_defs);
		_canvas_s.pick_edge.center=_canvas_s.pick_edge.prog.uniformLocation("pos_offset");
		_canvas_s.pick_edge.id=_canvas_s.pick_edge.prog.uniformLocation("edge_id");
		_canvas_s.pick_edge.proj=_canvas_s.pick_edge.prog.uniformLocation("mProj");
		_canvas_s.pick_edge.thick=_canvas_s.pick_edge.prog.uniformLocation("thickness");
		_canvas_s.pick_edge.view=_canvas_s.pick_edge.prog.uniformLocation("mView");
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc"};
	}
	{
		create_prog(_canvas_s.pick_vert.prog, APPLICATION_PATH "/glsl/vert.vert", APPLICATION_PATH "/glsl/vert.geom", APPLICATION_PATH "/glsl/vert.frag", pick_defs);
		_canvas_s.pick_vert.center=_canvas_s.pick_vert.prog.uniformLocation("pos_offset");
		_canvas_s.pick_vert.id=_canvas_s.pick_vert.prog.uniformLocation("edge_id");
		_canvas_s.pick_vert.proj=_canvas_s.pick_vert.prog.uniformLocation("mProj");
		_canvas_s.pick_vert.thick=_canvas_s.pick_vert.prog.uniformLocation("thickness");
		_canvas_s.pick_vert.view=_canvas_s.pick_vert.prog.uniformLocation("mView");
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc"};
	}
	{
		gapr::print("compiling prog volume");
		auto& prog=_canvas.prog_volume;
		gapr::gl::Shader<Funcs> vert, frag;
		vert.create(gtk::Resources{}, GL_VERTEX_SHADER, APPLICATION_PATH "/glsl/volume.vert");
		frag.create(gtk::Resources{}, GL_FRAGMENT_SHADER, APPLICATION_PATH "/glsl/volume.frag");
		prog.create({vert, frag});
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err link2"};
		auto loctex=glGetUniformLocation(prog, "tex3d_cube");
		_canvas.prog_volume_xfunc=glGetUniformLocation(prog, "xfunc_cube");
		_canvas.prog_volume_mat_inv=glGetUniformLocation(prog, "mrTexViewProj");
		_canvas.prog_volume_zpars=glGetUniformLocation(prog, "zparsCube");
		_canvas.prog_volume_color=glGetUniformLocation(prog, "color_volume");
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc"};
		vert.destroy();
		frag.destroy();
		glUseProgram(prog);
		glUniform1i(loctex, 1);
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err set"};
	}
	{
		gapr::print("compiling prog mesh");
		auto& prog=_canvas.prog_mesh;
		gapr::gl::Shader<Funcs> vert, frag;
		vert.create(gtk::Resources{}, GL_VERTEX_SHADER, APPLICATION_PATH "/glsl/mesh.vert");
		frag.create(gtk::Resources{}, GL_FRAGMENT_SHADER, APPLICATION_PATH "/glsl/mesh.frag");
		prog.create({vert, frag});
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err link2"};
		vert.destroy();
		frag.destroy();
		_canvas.prog_mesh_center=glGetUniformLocation(prog, "pos_offset");
		_canvas.prog_mesh_view=glGetUniformLocation(prog, "mView");
		_canvas.prog_mesh_proj=glGetUniformLocation(prog, "mProj");
		_canvas.prog_mesh_color=glGetUniformLocation(prog, "color");
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc"};
	}
	_canvas.cube_tex.create();
	{
		gapr::print("compiling prog sort");
		auto& prog=_canvas.prog_sort;
		gapr::gl::Shader<Funcs> vert, frag;
		vert.create(gtk::Resources{}, GL_VERTEX_SHADER, APPLICATION_PATH "/glsl/sort.vert");
		frag.create(gtk::Resources{}, GL_FRAGMENT_SHADER, APPLICATION_PATH "/glsl/sort.frag");
		prog.create({vert, frag});
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err link2"};
		auto loc_edg_d=glGetUniformLocation(prog, "tex_edges_depth");
		auto loc_edg_c=glGetUniformLocation(prog, "tex_edges_color");
		auto loc_o_d=glGetUniformLocation(prog, "tex_opaque_depth");
		auto loc_o_c=glGetUniformLocation(prog, "tex_opaque_color");
		auto loc_vol_d=glGetUniformLocation(prog, "tex_volume0_depth");
		auto loc_vol_c=glGetUniformLocation(prog, "tex_volume0_color");
		_canvas.prog_sort_vol_scale=glGetUniformLocation(prog, "volume_scale");
		_canvas.prog_sort_offset=glGetUniformLocation(prog, "offset");
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc"};
		vert.destroy();
		frag.destroy();
		glUseProgram(prog);
		glUniform1i(loc_edg_d, 2);
		glUniform1i(loc_edg_c, 3);
		glUniform1i(loc_o_d, 4);
		glUniform1i(loc_o_c, 5);
		glUniform1i(loc_vol_d, 6);
		glUniform1i(loc_vol_c, 7);
		if(glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err set2"};
	}
}

void gapr::show::Session::deinit_opengl(MainWindow* win) {
	_canvas.cube_tex.destroy();
	_canvas_s.fbo_pick.destroy();
	_canvas_s.fbo_edges.destroy();
	_canvas_s.fbo_opaque.destroy();
	_canvas_os.fbo_offscreen.destroy();
	_canvas_os.fbo_resize.destroy();
	_canvas_os.fbo_edges.destroy();
	_canvas_os.fbo_opaque.destroy();
	_canvas.fbo_volume.destroy();
	_canvas.prog_sort.destroy();
	_canvas.prog_volume.destroy();
	for(auto& mesh: _canvas.meshes)
		mesh.vao.destroy();
	_canvas.prog_mesh.destroy();
	_canvas_s.pick_vert.prog.destroy();
	_canvas_s.pick_edge.prog.destroy();
	_canvas.prog_verts.destroy();
	_canvas.prog_edges.destroy();
	glDeleteBuffers(1, &_canvas.vbo_idx);
	_canvas.vao_edges.destroy();
	_canvas.vao_quad.destroy();
}

void gapr::show::Session::start(MainWindow* win) {
	_win=win;
	bool enable_diff{true};
	bool enable_rec{true};
	bool enable_res{true};
	const char* def_mode{nullptr};
	if(_args.playback) {
		gtk_header_bar_set_subtitle(win->header_bar, _args.repo_file.c_str());
		def_mode="difference";
	} else {
		gtk_header_bar_set_subtitle(win->header_bar,
				(_args.swc_files[0].u8string()+(_args.swc_files.size()>1?" ...":"")).c_str());
		if(_args.swc_files_cmp.empty()) {
			def_mode="result";
			enable_diff=false;
		} else {
			def_mode="difference";
		}
	}
	{
		GtkTreeIter iter;
		auto list_store=win->display_modes;
		auto tree_mdl=GTK_TREE_MODEL(list_store);
		for(int i=0; gtk_tree_model_iter_nth_child(tree_mdl, &iter, nullptr, i); ++i) {
			gchararray name;
			gtk_tree_model_get(tree_mdl, &iter, 1, &name, -1);
			bool enabled{false};
			if(name==std::string_view{"difference"}) {
				enabled=enable_diff;
			} else if(name==std::string_view{"reconstruction"}) {
				enabled=enable_rec;
			} else if(name==std::string_view{"result"}) {
				enabled=enable_res;
			}
			gtk_list_store_set(list_store, &iter, 2, enabled, -1);
			g_free(name);
		}

		_cur_mode=def_mode;
		// XXX scoped signal blocker
		std::vector<gulong> handlers;
		while(auto h=g_signal_handler_find(win->select_mode, static_cast<GSignalMatchType>(G_SIGNAL_MATCH_FUNC|G_SIGNAL_MATCH_DATA|G_SIGNAL_MATCH_UNBLOCKED), 0, 0, nullptr, reinterpret_cast<void*>(&MainWindow::select_mode_changed), win)) {
			g_signal_handler_block(win->select_mode, h);
			handlers.push_back(h);
		}
		if(!gtk_combo_box_set_active_id(win->select_mode, def_mode))
			;
		for(auto h: handlers)
			g_signal_handler_unblock(win->select_mode, h);
	}
	{
		_cur_user=".all";
		// XXX scoped signal blocker
		std::vector<gulong> handlers;
		while(auto h=g_signal_handler_find(win->user_filter, static_cast<GSignalMatchType>(G_SIGNAL_MATCH_FUNC|G_SIGNAL_MATCH_DATA|G_SIGNAL_MATCH_UNBLOCKED), 0, 0, nullptr, reinterpret_cast<void*>(&MainWindow::user_filter_changed), win)) {
			g_signal_handler_block(win->user_filter, h);
			handlers.push_back(h);
		}
		if(!gtk_combo_box_set_active_id(win->user_filter, ".all"))
			;
		for(auto h: handlers)
			g_signal_handler_unblock(win->user_filter, h);
	}

	gtk_spinner_start(win->spinner);
	/*! use shared (not weak) ptr */
	auto ptr=shared_from_this();
	boost::asio::post(_ctx->thread_pool(), [this,ptr]() {
		load_data();
		boost::asio::post(_ctx->main_context(), [this,ptr]() {
			if(auto win=_win.lock(); win) {
				gtk_spinner_stop(win->spinner);
				load_data_cb(win);
			}
		});
	});

	auto act=g_action_map_lookup_action(G_ACTION_MAP(win), "load-image");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(act), false);
	if(!_args.mesh_path.empty()) {
		GdkRGBA col;
		col.alpha=0.5;
		col.blue=0.5;
		col.red=0.5;
		col.green=0.5;
		boost::asio::post(_ctx->thread_pool(), [this,ptr,fn=_args.mesh_path,col]() mutable {
			auto [verts, idxes]=script_helper::load_mesh(fn.c_str());
			boost::asio::post(_ctx->main_context(), [this,ptr,col,verts=std::move(verts),idxes=std::move(idxes)]() mutable {
				if(auto win=_win.lock(); win) {
					auto& mesh=_canvas.meshes.emplace_back();
					mesh.changed=true;
					mesh.color[0]=col.red;
					mesh.color[1]=col.green;
					mesh.color[2]=col.blue;
					mesh.color[3]=col.alpha;
					mesh.idxes=std::move(idxes);
					mesh.verts=std::move(verts);
					_canvas.meshes_changed=true;
				}
			});
		});
	}
	if(_args.image_url.empty())
		return;
	if(_args.image_url.compare(0, 7, "file://")==0) try {
		fprintf(stderr, "try cube file: %s\n", _args.image_url.c_str()+7);
		auto file=gapr::make_streambuf(_args.image_url.c_str()+7);
		auto loader=gapr::make_cube_loader(_args.image_url.c_str(), *file);
		if(!loader)
			throw std::runtime_error{"not a cube"};
		gapr::cube_info info{"main", std::string{_args.image_url}, false};
		info.xform.update_direction_inv();
		info.xform.update_resolution();
		info.range={0.0, 1.0};
		_image.cube_infos.push_back(info);
		boost::asio::post(_ctx->main_context(), [this,ptr]() {
			if(auto win=_win.lock(); win)
				load_catalog_cb(win, 1);
		});
		return;
	} catch(const std::exception& e) {
		fprintf(stderr, "exception: %s\n", e.what());
	}
	auto dl=std::make_shared<gapr::downloader>(_ctx->main_context().get_executor(), std::string{_args.image_url});
	auto cb=[this,ptr,dl](auto&& cb2, std::error_code ec, int progr) {
		if(ec) {
			if(auto win=_win.lock(); win) {
				gapr::print("error load catalog");
				return load_catalog_cb(win, 0);
			}
		}
		if(progr>=dl->FINISHED) {
			auto [sb, idx]=dl->get();
			boost::asio::post(_ctx->thread_pool(), [this,ptr,sb=std::move(sb)]() {
				auto chan=parse_catalog(*sb);
				boost::asio::post(_ctx->main_context(), [this,ptr,chan]() {
					if(auto win=_win.lock(); win)
						load_catalog_cb(win, chan);
				});
			});
			return;
		}
		dl->async_wait(std::bind(cb2, cb2, std::placeholders::_1, std::placeholders::_2));
	};
	dl->async_wait(std::bind(cb, cb, std::placeholders::_1, std::placeholders::_2));

}

template<typename T> class path_extractor {
	public:
		explicit path_extractor(T id_null): _id_null{id_null} { }

		bool extract(std::vector<T>& path) {
			if(_adjs.empty())
				return false;
			auto it=_adjs.begin();
			auto [a, b]=*it;
			//gapr::print("seed: ", a.data, ',', b.data);
			assert(a!=b);
			extend_path(path, a, b);
			std::reverse(path.begin(), path.end());
			extend_path(path, b, a);
			return true;
		}
		void probe_verts() {
			T prev{_id_null};
			unsigned int cnt{0};
			for(auto [a, b]: _adjs) {
				if(a==prev) {
					cnt++;
					continue;
				}
				if(prev!=_id_null) {
					if(cnt!=2)
						_verts.emplace(prev);
				}
				prev=a;
				cnt=1;
			}
			if(prev!=_id_null) {
				if(cnt!=2)
					_verts.emplace(prev);
			}
		}
		void add_link(T id, T id2) {
			do_insert(id, id2);
		}
		void add_vert(T id) {
			_verts.emplace(id);
		}
		bool linked(T id) {
			return _adjs.find(id)!=_adjs.end();
		}

		void refresh() {
			assert(_adjs.empty());
			_verts.clear();
			_hint={};
		}

	private:
		T _id_null;
		using multimap=std::unordered_multimap<T, T>;
		multimap _adjs;
		std::unordered_set<T> _verts;
		std::vector<typename multimap::node_type> _storage;
		typename multimap::const_iterator _hint{};

		void do_insert(T id, T id2) {
			if(_storage.empty()) {
				_hint=_adjs.emplace_hint(_hint, id, id2);
				return;
			}
			auto n=std::move(_storage.back());
			_storage.pop_back();
			n.key()=id;
			n.mapped()=id2;
			_hint=_adjs.insert(_hint, std::move(n));
		}
		void do_erase(typename multimap::const_iterator it) {
			auto n=_adjs.extract(it);
			_storage.push_back(std::move(n));
		}

		void extend_path(std::vector<T>& path, T start, T b) {
			auto a=start;
			do {
				path.push_back(b);
				//gapr::print("path: ", b.data);
				auto [it1, it2]=_adjs.equal_range(b);
				auto it_del=it2;
				auto it_del2=it2;
				unsigned int eq_hits=0;
				unsigned int ne_hits=0;
				for(auto it=it1; it!=it2; ++it) {
					if(it->second==a) {
						eq_hits++;
						it_del=it;
					} else {
						ne_hits++;
						it_del2=it;
					}
				}
				if(eq_hits!=1) {
					gapr::print("eq_hits: ", eq_hits, ',', a);
					for(auto it=it1; it!=it2; ++it)
						gapr::print("eq_hits: ", it->first, ':', it->second);
					assert(eq_hits==1);
				}
				do_erase(it_del);
				if(ne_hits!=1 || _verts.find(b)!=_verts.end())
					break;
				auto c=it_del2->second;
				if(c==start)
					break;
				do_erase(it_del2);
				a=b;
				b=c;
			} while(true);
			//gapr::print("path: ", "end");
		}
};






inline gapr::show::Session::LinkData& gapr::show::Session::NodeData::link_at(std::size_t i) {
	if(i<links.size())
		return links[i];
	return more_links[i-links.size()];
}
std::pair<gapr::show::Session::LinkData*, bool> gapr::show::Session::NodeData::link_ins(int64_t id2) {
	switch(num_links) {
		case 0:
			return {&links[num_links++], true};
		case 1:
			if(links[0].id2==id2)
				return {&links[0], false};
			return {&links[num_links++], true};
		default:
			break;
	}
	for(unsigned int i=0; i<2; i++) {
		if(links[i].id2==id2)
			return {&links[i], false};
	}
	for(unsigned int i=0; i<num_links-2; i++) {
		if(more_links[i].id2==id2)
			return {&more_links[i], false};
	}
	more_links.push_back(LinkData{});
	return {&more_links[++num_links-3], true};
}

struct gapr::show::Session::mode_helper_diff {
	constexpr static bool diff_mode{true};
	static std::array<uint16_t, 4> get_diff_color(DiffState st) {
		switch(st) {
			case DiffState::Del:
				return {255, 0, 0, 1};
			case DiffState::Add:
				return {0, 255, 0, 1};
			case DiffState::Eq:
				return {128, 128, 128, 0};
			case DiffState::Chg:
				return {255, 255, 0, 1};
		}
		assert(0);
	}
	std::array<uint16_t, 4> handle_node(const NodeData& nd) const {
		do {
			if(nd.state!=DiffState::Eq)
				break;
			if(nd.num_links==0)
				break;
			return {0, 0, 0, 0};
		} while(false);
		auto res=get_diff_color(nd.state);
		res[3]=2;
		return res;
	}
	std::array<uint16_t, 4> handle_link(const LinkData& ld, const NodeData& nd) const {
		auto res=get_diff_color(ld.state);
		res[3]=res[3]?2:1;
		return res;
	}
};
struct gapr::show::Session::mode_helper_recon {
	constexpr static bool diff_mode{false};
	std::array<uint16_t, 4> handle_node(const NodeData& nd) const {
		for(auto& ann_: nd.annots) {
			std::string_view ann=ann_;
			if(ann.compare(0, 5, "error")==0) {
				if(ann.size()>5) {
					if(ann[5]!='=')
						continue;
					ann.remove_prefix(6);
				} else {
					ann.remove_prefix(5);
				}
				if(ann=="" || ann=="deferred")
					return {255, 0, 0, 4};
				return {128, 0, 0, 3};
			}
		}
		if(!nd.attr.misc.coverage())
			return {255, 255, 0, 2};
		return {0, 0, 0, 0};
	}
	std::array<uint16_t, 4> handle_link(const LinkData& ld, const NodeData& nd) const {
		if(ld.in_loop)
			return {255, 128, 0, 2};
		return {128, 128, 128, 1};
	}
};
struct gapr::show::Session::mode_helper_result {
	constexpr static bool diff_mode{false};
	constexpr static std::array<uint16_t, 4> colors[]={
		{128, 128, 128, 0},
		{255, 0, 0, 0},
		{0, 255, 0, 0},
		{255, 128, 0, 0},
		{128, 128, 0, 0},
		{0, 0, 255, 0},
	};
	std::array<uint16_t, 4> handle_node(const NodeData& nd) const {
		if(nd.is_root) {
			auto t=nd.type;
			if(t>5)
				t=5;
			if(t<0)
				t=5;
			auto res=colors[t];
			res[3]=5;
			return res;
		}
		//auto res=iff_mode_helper::handle_node(nd);
		//std::swap(res[0], res[2]);
		return {0,0,0,0};
	}
	std::array<uint16_t, 4> handle_link(const LinkData& ld, const NodeData& nd) const {
		auto t=nd.type;
		if(t>5)
			t=5;
		if(t<0)
			t=5;
		auto res=colors[t];
		res[3]=1;
		return res;
	}
};
struct gapr::show::Session::mode_helper_script: lua_base {
	explicit mode_helper_script(const char* fn): lua_base{}, lua_file{fn} { }
	// XXX runtime?
	bool diff_mode{false};
	static void add_diff_state(lua_State* L, const char* key, DiffState val) {
		std::string_view v;
		switch(val) {
			case DiffState::Del:
				v=std::string_view{"del"};
				break;
			case DiffState::Add:
				v=std::string_view{"add"};
				break;
			case DiffState::Eq:
				v=std::string_view{"eq"};
				break;
			case DiffState::Chg:
				v=std::string_view{"chg"};
				break;
			default:
				v={};
		}
		lua_pushlstring(L, v.data(), v.size());
		lua_setfield(L, 3, key);
	}
	static void add_node_data(lua_State* L, const NodeData& nd) {
		add_diff_state(L, "node_state", nd.state);
		lua_pushinteger(L, nd.num_links);
		lua_setfield(L, 3, "node_degree");
		lua_pushboolean(L, nd.attr.misc.coverage());
		lua_setfield(L, 3, "node_pr");
		lua_pushboolean(L, nd.is_root);
		lua_setfield(L, 3, "node_is_root");
		lua_pushinteger(L, nd.type);
		lua_setfield(L, 3, "node_type");
		lua_pushnumber(L, nd.radius);
		lua_setfield(L, 3, "node_radius");
		lua_pushinteger(L, nd.id);
		lua_setfield(L, 3, "node_id");
		lua_pushinteger(L, nd.root_id);
		lua_setfield(L, 3, "node_root_id");
		// XXX error_lvl
		auto get_err=[&nd]() ->unsigned int {
			for(auto& ann_: nd.annots) {
				std::string_view ann=ann_;
				if(ann.compare(0, 5, "error")==0) {
					if(ann.size()>5) {
						if(ann[5]!='=')
							continue;
						ann.remove_prefix(6);
					} else {
						ann.remove_prefix(5);
					}
					if(ann=="" || ann=="deferred")
						return 2;
					return 1;
				}
			}
			return 0;
		};
		lua_pushinteger(L, get_err());
		lua_setfield(L, 3, "node_error");
	}
	static void add_link_data(lua_State* L, const LinkData& ld) {
		add_diff_state(L, "link_state", ld.state);
		lua_pushboolean(L, ld.in_loop);
		lua_setfield(L, 3, "link_in_loop");
	}
	static std::array<uint16_t, 4> handle_res(lua_State* L) {
		lua_callk(L, 1, 2, 0, nullptr);
		int isnum;
		auto s=lua_tointegerx(L, 2, &isnum);
		if(!isnum)
			throw std::runtime_error{"returned size is not an integer"};
		std::size_t n;
		auto c=lua_tolstring(L, 3, &n);
		if(!c)
			throw std::runtime_error{"returned color is not a string"};
		GdkRGBA cc;
		if(!gdk_rgba_parse(&cc, c))
			throw std::runtime_error{"failed to parse returned color"};
		lua_pop(L, 2);
		if(s<0)
			s=0;
		if(s>65535)
			s=65535;
		auto get_16=[](double v) {
			int r=v*255;
			if(r>255)
				r=255;
			if(r<0)
				r=0;
			return (uint16_t)r;
		};
		return {get_16(cc.red), get_16(cc.green), get_16(cc.blue), (uint16_t)s};
	}
	std::array<uint16_t, 4> handle_node(const NodeData& nd) const {
		auto L=_lua.get();
		assert(L);
		auto tf=lua_getfield(L, 1, "node");
		assert(tf==LUA_TFUNCTION);
		assert(lua_gettop(L)==2);
		lua_newtable(L);
		add_node_data(L, nd);
		return handle_res(L);
	}
	std::array<uint16_t, 4> handle_link(const LinkData& ld, const NodeData& nd) const {
		auto L=_lua.get();
		assert(L);
		auto tf=lua_getfield(L, 1, "link");
		assert(tf==LUA_TFUNCTION);
		assert(lua_gettop(L)==2);
		lua_newtable(L);
		add_node_data(L, nd);
		add_link_data(L, ld);
		return handle_res(L);
	}
	std::unique_ptr<lua_State, lua_State_Del> _lua;
	std::string lua_file;
	void ensure_loaded() {
		if(_lua)
			return;
		auto L=::lua_newstate(lua_alloc_f, this);
		if(!L)
			throw std::runtime_error{"failed to create lua state"};
		std::unique_ptr<lua_State, lua_State_Del> lua_{L};
		::lua_atpanic(L, lua_panic);
		auto ver=::lua_version(L);
		gapr::print("lua ", ver, " ...");
		luaL_openlibs(L);
		if(auto r=luaL_loadfilex(L, lua_file.c_str(), "t"); r!=LUA_OK) {
			if(r==LUA_ERRSYNTAX) {
				auto r=lua_gettop(L);
				gapr::print("syntax err ", lua_gettop(L));
				std::size_t msgl;
				auto msg=lua_tolstring(L, 1, &msgl);
				if(msg)
					throw std::runtime_error{get_stack(L, r)};
			}
			report_lua_error(r);
		}
		if(auto r=lua_pcallk(L, 0, LUA_MULTRET, 0, 0, nullptr); r!=LUA_OK) {
			if(r==LUA_ERRRUN) {
				if(auto r=lua_gettop(L); r>0)
					throw std::runtime_error{get_stack(L, r)};
			}
			if(auto r=lua_gettop(L); r>0)
				throw std::runtime_error{get_stack(L, r)};
			report_lua_error(r);
		}
		auto nret=lua_gettop(L);
		if(nret!=1)
			throw std::runtime_error{"wrong number of returned values"};
		if(!lua_istable(L, 1))
			throw std::runtime_error{"returned value is not a table"};
		if(auto t=lua_getfield(L, 1, "node"); t!=LUA_TFUNCTION)
			throw std::runtime_error{"ret.node is not a function"};
		if(auto t=lua_getfield(L, 1, "link"); t!=LUA_TFUNCTION)
			throw std::runtime_error{"ret.link is not a function"};
		if(auto t=lua_getfield(L, 1, "diff"); t!=LUA_TNIL) {
			if(t!=LUA_TBOOLEAN)
				throw std::runtime_error{"ret.diff is not a boolean"};
			diff_mode=lua_toboolean(L, 4);
		}
		lua_pop(L, 1);
		lua_pop(L, 2);
		_lua=std::move(lua_);
	}
};

struct gapr::show::Session::loop_helper {
	static unsigned int probe_loops(std::unordered_map<int64_t, NodeData>& nodes) {
		std::deque<int64_t> queue;
		for(auto& [id, nd]: nodes) {
			if(nd.is_root) {
				assert(nd.state!=DiffState::Del);
				nd.root_id=id;
				nd.par_id=-1;
				queue.push_back(id);
			}
		}
		std::vector<std::pair<int64_t, int64_t>> loops;
		unsigned int loop_cnt=0;
		auto handle_loop=[&nodes,&loops,&loop_cnt](int64_t id, const NodeData& nd, int64_t id2, const NodeData& nd2) {
			++loop_cnt;
			assert(id!=-1);
			assert(id2!=-1);
			assert(id!=id2);
			std::unordered_map<int64_t, std::size_t> left_map{};
			std::vector<int64_t> arr{};
			left_map.emplace(id, arr.size());
			arr.push_back(id);
			int64_t par=nd.par_id;
			while(par!=-1) {
				left_map.emplace(par, arr.size());
				arr.push_back(par);
				auto& parn=nodes.at(par);
				par=parn.par_id;
			}
			std::size_t left_n=arr.size();
			arr.push_back(id2);
			par=nd2.par_id;
			std::size_t left_i=0;
			while(par!=-1) {
				arr.push_back(par);
				auto it=left_map.find(par);
				if(it!=left_map.end()) {
					left_i=left_n-it->second-1;
					break;
				}
				auto& parn=nodes.at(par);
				par=parn.par_id;
			}
			std::reverse(arr.begin(), arr.begin()+left_n);

			for(unsigned int i=left_i+1; i<arr.size(); ++i) {
				auto a=arr[i];
				auto b=arr[i-1];
				loops.emplace_back(a, b);
			}
		};
		gapr::print("handle loop begin");
		while(!queue.empty()) {
			auto id=queue.front();
			queue.pop_front();
			auto& nd=nodes.at(id);

			for(unsigned int i=0; i<nd.num_links; ++i) {
				auto& ld=nd.link_at(i);
				if(ld.state==DiffState::Del)
					continue;
				if(ld.id2==nd.par_id)
					continue;
				auto& nd2=nodes.at(ld.id2);
				if(nd2.root_id!=-1) {
					handle_loop(id, nd, ld.id2, nd2);
					continue;
				}
				nd2.par_id=id;
				nd2.root_id=nd.root_id;
				queue.push_back(ld.id2);
			}
		}
		gapr::print("handle loop finished ", loop_cnt);
		for(auto [a, b]: loops) {
			auto& nd=nodes.at(a);
			auto [it, ins]=nd.link_ins(b);
			assert(!ins);
			if(it->in_loop)
				continue;
			it->in_loop=true;
			auto& nd2=nodes.at(b);
			auto [it2, ins2]=nd2.link_ins(a);
			assert(!ins2);
			it2->in_loop=true;
		}
		assert(loop_cnt%2==0);
		return loop_cnt/2;
	}
};



void gapr::show::Session::load_data_swc() {
	gapr::bbox bbox01{};
	auto& nodes=_data.nodes;
	auto handle_link=[](NodeData& nd, int64_t id, auto is_cmp) {
		auto [it, ins]=nd.link_ins(id);
		if(ins) {
			if constexpr(is_cmp) {
				it->id2=id;
				it->state=DiffState::Del;
			} else {
				it->id2=id;
				it->state=DiffState::Add;
			}
		} else {
			if constexpr(is_cmp) {
				if(it->id2!=id || it->state!=DiffState::Add)
					throw std::runtime_error{"dup link"};
				it->state=DiffState::Eq;
			} else {
				throw std::runtime_error{"dup link"};
			}
		}
	};
	auto handle_swc_file=[&nodes,&bbox01,handle_link](const std::filesystem::path& fn, auto is_cmp) {
		std::ifstream fs{fn};
		boost::iostreams::filtering_istream filter{};
		if(fn.extension()==".gz")
			filter.push(boost::iostreams::gzip_decompressor{});
		filter.push(fs);

		gapr::swc_input swc{filter};
		while(swc.read()) {
			switch(swc.tag()) {
				case swc_input::tags::comment:
					break;
				case swc_input::tags::node:
					{
						auto& n=swc.node();
						auto [it, ins]=nodes.emplace(swc.id(), NodeData{});
						auto& nd=it->second;
						if(ins) {
							if constexpr(is_cmp) {
								nd.state=DiffState::Del;
							} else {
								nd.state=DiffState::Add;
							}
							nd.attr.misc=swc.misc_attr();
							nd.type=n.type;
							nd.radius=n.radius;
							nd.pos=n.pos;
							for(unsigned int i=0; i<3; ++i)
								nd.attr.pos(i, n.pos[i]);
							nd.id=n.id;
							nd.num_links=0;
						} else {
							if constexpr(is_cmp) {
								if(nd.id!=n.id || nd.state!=DiffState::Add)
									throw std::runtime_error{"dup node"};
								nd.state=DiffState::Eq;
								nd.attr2.misc=swc.misc_attr();
								nd.type2=n.type;
								nd.radius2=n.radius;
								for(unsigned int i=0; i<3; i++) {
									nd.pos_diff[i]=n.pos[i]-nd.pos[i];
									nd.attr2.pos(i, n.pos[i]);
								}
							} else {
								throw std::runtime_error{"dup node"};
							}
						}
						bbox01.add(n.pos);
						if(n.par_id!=-1) {
							handle_link(nd, n.par_id, is_cmp);
							handle_link(nodes.at(n.par_id), n.id, is_cmp);
						}
					}
					break;
				case swc_input::tags::annot:
					{
						auto id=swc.id();
						auto& nd=nodes.at(id);
						if(swc.annot().size()>0 && swc.annot()[0]=='.')
							break;
						if constexpr(is_cmp) {
							nd.annots2.emplace_back(swc.annot());
						} else {
							nd.annots.emplace_back(swc.annot());
							if(swc.annot_key()=="root" && !startswith("seg", swc.annot_val()))
								nd.is_root=true;
						}
					}
					break;
				case swc_input::tags::misc_attr:
					{
						auto id=swc.id();
						auto& nd=nodes.at(id);
						if constexpr(is_cmp) {
							nd.attr2.misc=swc.misc_attr();
						} else {
							nd.attr.misc=swc.misc_attr();
						}
					}
					break;
				case swc_input::tags::loop:
					{
						auto id=swc.id();
						auto id2=swc.loop();
						handle_link(nodes.at(id), id2, is_cmp);
						handle_link(nodes.at(id2), id, is_cmp);
					}
					break;
			}
		}
		if(!swc.eof())
			gapr::report("Failed to read swc file.");
	};

	for(auto& fn: _args.swc_files)
		handle_swc_file(fn, std::false_type{});
	for(auto& fn: _args.swc_files_cmp)
		handle_swc_file(fn, std::true_type{});

	for(auto& [id, nd]: nodes) {
		if(nd.state==DiffState::Eq) {
			bool chg{false};
			if(nd.attr!=nd.attr2)
				chg=true;
			if(nd.type!=nd.type2)
				chg=true;
			if(nd.radius!=nd.radius2)
				chg=true;
			for(unsigned int i=0; i<3; i++) {
				if(nd.pos_diff[i]!=0)
					chg=true;
			}
			std::sort(nd.annots.begin(), nd.annots.end());
			std::sort(nd.annots2.begin(), nd.annots2.end());
			if(nd.annots.size()==nd.annots2.size()) {
				for(std::size_t i=0; i<nd.annots.size(); i++) {
					if(nd.annots[i]!=nd.annots2[i])
						chg=true;
				}
			} else {
				chg=true;
			}
			if(chg) {
				nd.state=DiffState::Chg;
			}
		}
		if(_args.swc_files_cmp.empty()) {
			for(unsigned int i=0; i<nd.num_links; i++)
				nd.link_at(i).state=DiffState::Eq;
			nd.state=DiffState::Eq;
		}
	}

	_data.loop_cnt=loop_helper::probe_loops(nodes);

	auto max_d=bbox01.diameter_and_center(_camera.center_init);
	_camera.diameter=max_d;
	_camera.resolution=max_d/10000;
}

void gapr::show::Session::load_data_repo() {
	_playback=std::make_shared<playback_helper>();
	_playback->load_repo(_args.repo_file);
	_data.loop_cnt=loop_helper::probe_loops(_data.nodes);

	auto max_d=_playback->bbox.diameter_and_center(_camera.center_init);
	_camera.diameter=max_d;
	_camera.resolution=max_d/10000;
}

struct color_helper {
	std::vector<gapr::vec3<GLfloat>> colors;
	std::unordered_map<uint64_t, uint16_t> map;
	uint16_t get_idx(uint16_t r, uint16_t g, uint16_t b) {
		uint64_t v=r;
		v=(v<<16)|g;
		v=(v<<16)|b;
		auto [it, ins]=map.emplace(v, 65535);
		if(!ins)
			return it->second;
		if(colors.size()<8) {
			colors.push_back({r/255.0f, g/255.0f, b/255.0f});
			return it->second=colors.size()-1;
		}
		return it->second;
	}
};

static path_extractor<int64_t> pextr{-1};
static std::mutex pextr_mtx;

void gapr::show::Session::load_data_prepare_buffer() {
	assert(_data.ids.empty());
	assert(_data.points.empty());
	assert(_data.vfirst.empty());
	assert(_data.vcount.empty());
	assert(_data.nverts==0);

	std::scoped_lock pextr_lck{pextr_mtx};
	pextr.refresh();

	color_helper colors;
	auto to_gl=[](const NodeData& n) ->PointGL {
		return PointGL{n.attr.ipos, (GLuint)n.id%4};
	};
	auto& nodes=_data.nodes;
	std::vector<int64_t> path;
	auto add_edge=[this,&path,to_gl,&nodes,&colors](std::size_t i0, std::size_t i1, const auto& helper) {
		auto first=_data.ids.size();
		_data.ids.reserve(i1-i0);
		_data.points.reserve(i1-i0);
		const NodeData* prev_nd{nullptr};
		for(std::size_t i=i0; i<i1; i++) {
			auto id=path[i];
			auto& nd=nodes.at(id);
			auto pt=to_gl(nd);
			if(i>i0) {
				auto id_prev=path[i-1];
				auto [it, ins]=nd.link_ins(id_prev);
				assert(!ins);
				auto res=helper.handle_link(*it, nd.par_id==id_prev?nd:*prev_nd);
				auto ci=colors.get_idx(res[0], res[1], res[2]);
				pt.misc=(GLuint)(ci|((unsigned int)res[3]<<16));
			}
			_data.points.push_back(pt);
			_data.ids.push_back(id);
			prev_nd=&nd;
		}
		auto last=_data.ids.size();
		_data.vfirst.push_back(static_cast<const GLuint*>(nullptr)+first);
		_data.vcount.push_back(last-first);
	};

	auto do_prepare=[this,&nodes,/*&pextr,*/&colors,to_gl,add_edge,&path](const auto& helper) {
		gapr::print("begin prepare");
		for(auto& [id, nd]: nodes) {
			if(!helper.diff_mode && nd.state==DiffState::Del)
				continue;
			for(std::size_t i=0; i<nd.num_links; i++) {
				auto& ld=nd.link_at(i);
				if(!helper.diff_mode && ld.state==DiffState::Del)
					continue;
				pextr.add_link(nd.id, ld.id2);
			}
			if(nd.is_root)
				pextr.add_vert(id);
			auto res=helper.handle_node(nd);
			if(res[3]!=0) {
				_data.ids.push_back(id);
				//auto& nd2=nodes.at(id);
				//assert(&nd==&nd2);
				auto pt=to_gl(nd);
				//auto res2=helper.handle_node(nd);
				//assert(res==res2);
				auto ci=colors.get_idx(res[0], res[1], res[2]);
				pt.misc=(GLuint)(ci|((unsigned int)res[3]<<16));
				_data.points.push_back(pt);
			}
		}
		gapr::print("after add links");
		pextr.probe_verts();
		gapr::print("after probe verts");
		_data.nverts=_data.ids.size();
		while(pextr.extract(path)) {
			if(path.front()==path.back()) {
				assert(path.size()>=4);
				auto i=path.size()/2;
				add_edge(0, i, helper);
				add_edge(i-1, path.size(), helper);
			} else {
				add_edge(0, path.size(), helper);
			}
			path.clear();
		}
		gapr::print("after exract");
	};

	if(_cur_mode=="difference") {
		do_prepare(mode_helper_diff{});
	} else if(_cur_mode=="reconstruction") {
		do_prepare(mode_helper_recon{});
	} else if(_cur_mode=="result") {
		do_prepare(mode_helper_result{});
	} else if(_cur_mode=="script") {
		assert(_mod_s);
		_mod_s->ensure_loaded();
		do_prepare(*_mod_s);
	} else {
		gapr::print("unknown mode: ", _cur_mode);
	}

	_data.colors=std::move(colors.colors);
	assert(_data.points.size()==_data.ids.size());
	assert(_data.vfirst.size()==_data.vcount.size());
	gapr::print("sizes: ", _data.points.size());
	gapr::print("sizes: ", _data.ids.size());
	gapr::print("sizes: ", _data.vfirst.size());
	gapr::print("sizes: ", _data.vcount.size());
	gapr::print("sizes: ", _data.nverts);

	if(0)
		dump_diff();
}

void gapr::show::Session::load_data() {
	gapr::print("load data...");
	if(_args.playback) {
		load_data_repo();
	} else {
		load_data_swc();
	}
	load_data_prepare_buffer();
}

void gapr::show::Session::dump_diff() {
	std::vector<std::pair<int64_t, DiffState>> diff_nodes;
	std::vector<std::tuple<int64_t, int64_t, DiffState>> diff_links;
	auto& nodes=_data.nodes;
	for(auto& [id, dat]: nodes) {
		assert(id!=0);
		if(dat.state!=DiffState::Eq)
			diff_nodes.emplace_back(id, dat.state);
		for(unsigned int i=0; i<dat.num_links; i++) {
			auto [id2, st, _inloop]=dat.link_at(i);
			assert(id2!=0);
			if(st!=DiffState::Eq) {
				if(id<=id2)
					diff_links.emplace_back(id, id2, st);
				else
					diff_links.emplace_back(id2, id, st);
			}
		}
	}
	std::sort(diff_nodes.begin(), diff_nodes.end());
	std::sort(diff_links.begin(), diff_links.end());
	diff_links.erase(std::unique(diff_links.begin(), diff_links.end()), diff_links.end());
	auto get_chr=[](DiffState st) {
		switch(st) {
			case DiffState::Del:
				return '-';
			case DiffState::Add:
				return '+';
			case DiffState::Eq:
				return '=';
			case DiffState::Chg:
				return '!';
		}
		assert(0);
	};
	std::ofstream fs{"./tmpdiff.diff"};
	for(auto [id, st]: diff_nodes) {
		fs<<get_chr(st)<<id<<'\n';
	}
	for(auto [id, id2, st]: diff_links) {
		fs<<get_chr(st)<<id<<'/'<<id2<<'\n';
	}
}

template<typename T>
static std::string format_commit_info(uint64_t num, const T& infos) {
	std::ostringstream oss;
	oss<<num<<'/'<<infos.size();
	if(num>0) {
		auto& info=infos[num-1];
		assert(info.id==num-1);
		std::array<char, 128> time_str;
		auto p=gapr::timestamp_to_chars(time_str.begin(), time_str.end(), info.when, false);
		oss<<"\nauthor: <span size='small' face='monospace'>"<<info.who;
		oss<<"</span>\ntime: <span size='small' face='monospace'>";
		oss.write(&time_str[0], p-&time_str[0]);
		oss<<"</span>\ntype: <span size='small' face='monospace'>"<<to_string(gapr::delta_type{info.type});
		oss<<"</span>";
	}
	return oss.str();
}
void gapr::show::Session::load_data_cb(MainWindow* win) {
	gapr::print("load data finished");
	_camera.rgt={0.0, 1.0, 0.0};
	_camera.up={1.0, 0.0, 0.0};
	_camera.center=_camera.center_init;
	_camera.zoom=_camera.diameter/2*std::sqrt(3);
	_camera.zoom_max=_camera.diameter;
	_camera.zoom_min=50*_camera.resolution;
	_camera.changed=true;
	update_camera();

	_data.ready=true;
	_data.changed=true;
	update(win);

	if(_args.playback) {
		GtkTreeIter iter;
		auto users_list=win->users_list;
		for(auto& u: _playback->_users)
			gtk_list_store_insert_with_values(users_list, &iter, -1, 0, &u[0], 1, &u[0], -1);
		auto str=format_commit_info(0, _playback->_infos);
		gtk_label_set_markup(win->commit_info, str.data());
		gtk_widget_set_visible(GTK_WIDGET(win->commit_info), true);
	}

	if(!_args.script_file.empty())
		run_script(_args.script_file);
}

void gapr::show::Session::run_script(std::string_view file) {
	assert(!_script);
	_script=std::make_shared<script_helper>();
	auto ptr=shared_from_this();
	gapr::print("asdf ", file);
	boost::asio::post(_ctx->thread_pool(), [this,ptr,s=_script,file=std::string{file}]() {
		std::string res;
		bool err{false};
		try {
			res=s->run(file.c_str(), this);
		} catch(const std::runtime_error& e) {
			res=e.what();
			err=true;
		}
		while(!res.empty() && std::isspace(res.back()))
			res.pop_back();
		boost::asio::post(_ctx->main_context(), [this,ptr,err,res=std::move(res)]() {
			if(err && _script->_pending_close)
				throw std::runtime_error{res};
			if(auto win=_win.lock(); win) {
				if(_script->_pending_close) {
					gtk_window_close(GTK_WINDOW(win));
					return;
				}
				gtk::ref dlg=gtk_message_dialog_new_with_markup(GTK_WINDOW(win), gtk::flags(GTK_DIALOG_MODAL, GTK_DIALOG_USE_HEADER_BAR, GTK_DIALOG_DESTROY_WITH_PARENT), err?GTK_MESSAGE_WARNING:GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, err?"Failed. Error msg:\n%s":(res.empty()?"Success":"Success. Returned:\n%s"), res.c_str());
				auto res=gtk_dialog_run(GTK_DIALOG(dlg));
				assert(res==GTK_RESPONSE_CLOSE || res==GTK_RESPONSE_DELETE_EVENT);
				gtk_widget_destroy(GTK_WIDGET(dlg));
			}
		});
	});
}

unsigned int gapr::show::Session::parse_catalog(std::streambuf& sb) {
	assert(_ctx->thread_pool().get_executor().running_in_this_thread());
	std::vector<gapr::mesh_info> mesh_infos;
	std::istream str{&sb};
	gapr::parse_catalog(str, _image.cube_infos, mesh_infos, _args.image_url);

	for(auto& s: _image.cube_infos) {
		if(!s.xform.update_direction_inv())
			gapr::report("no inverse");
		s.xform.update_resolution();
		gapr::print("cube: ", s.location());
	}

	std::size_t first_c{0}, first_g{0};
	for(unsigned int i=0; i<_image.cube_infos.size(); i++) {
		if(_image.cube_infos[i].is_pattern()) {
			if(!first_c)
				first_c=i+1;
		} else {
			if(!first_g)
				first_g=i+1;
		}
	}
	if(!first_c)
		return first_g;
	return first_c;
}
void gapr::show::Session::load_catalog_cb(MainWindow* win, unsigned int chan) {
	_image.closeup_chan=chan;
	_image.chan=chan;
	if(chan>0) {
		_cube_builder=std::make_shared<gapr::cube_builder>(_ctx->main_context().get_executor(), _ctx->thread_pool());
		_cube_builder->async_wait([this,wptr=weak_from_this()](std::error_code ec, int progr) {
			if(auto ptr=wptr.lock(); ptr)
				return image_progress(ec, progr);
		});
		gapr::print("catalog ready");
		auto act=g_action_map_lookup_action(G_ACTION_MAP(win), "load-image");
		g_simple_action_set_enabled(G_SIMPLE_ACTION(act), true);
	}
}

bool gapr::show::Session::do_refresh_image(MainWindow* win) {
	auto chan=_image.chan;
	assert(chan!=0);
	auto& info=_image.cube_infos[chan-1];
	if(info.is_pattern()) {
		std::array<double, 3> pos;
		for(unsigned int i=0; i<3; i++)
			pos[i]=_camera.center[i];
		auto offset=(_image.closeup_cube && _image.closeup_chan==chan)?&_image.closeup_offset:nullptr;
		if(_cube_builder->build(chan, info, pos, false, offset)) {
			gtk_spinner_start(win->spinner_img);
			return true;
		}
	} else {
		if(_image.global_cube && _image.global_chan==chan)
			return false;
		_cube_builder->build(chan, info);
		if(true) {
			gtk_spinner_start(win->spinner_img);
			return true;
		}
	}
	return false;
}

void gapr::show::Session::refresh_image(MainWindow* win) {
	if(!_cube_builder)
		return;

	auto map_c=_image.chan;
	if(map_c!=0)
		do_refresh_image(win);
}

void gapr::show::Session::image_progress(std::error_code ec, int progr) {
	auto win=_win.lock();
	if(!win)
		return;
	bool stop_spin{false};
	if(ec) {
		gapr::print("error load cube: ", ec.message());
		stop_spin=true;
		if(_script && _script->_pending_image) {
			auto prom=std::move(*_script->_pending_image);
			_script->_pending_image.reset();
			prom.set_value();
		}
	} else if(progr==1001) {
		stop_spin=true;
		auto cube=_cube_builder->get();
		while(cube.data) {
			gapr::print("cube_out: ", cube.chan);
			if(_image.cube_infos[cube.chan-1].is_pattern()) {
				_image.closeup_chan=cube.chan;
				_image.closeup_cube=cube.data;
				_image.closeup_offset=cube.offset;
			} else {
				_image.global_chan=cube.chan;
				_image.global_cube=cube.data;
			}
			_image.changed=true;
			cube=_cube_builder->get();
		}
		if(_image.changed) {
			gapr::print("image data changed");
			_camera.changed=true;
			update_camera();
			update(win);
		}
		if(_script && _script->_pending_image) {
			auto prom=std::move(*_script->_pending_image);
			_script->_pending_image.reset();
			prom.set_value();
		}
	}
	if(stop_spin) {
			gtk_spinner_stop(win->spinner_img);
	}

	_cube_builder->async_wait([this,wptr=weak_from_this()](std::error_code ec, int progr) {
		if(auto ptr=wptr.lock(); ptr)
			return image_progress(ec, progr);
	});
}
void gapr::show::Session::update_xfunc(MainWindow* win, unsigned int i, double v) {
	_image.xfunc_state[i]=v;
	update(win);
}
void gapr::show::Session::show_node(MainWindow* win, int64_t id) {
	if(id==-1) {
		gtk_label_set_markup(win->node_info, "");
		gtk_widget_set_visible(GTK_WIDGET(win->node_info), false);
		return;
	}
	gapr::print("show node: ", id);
	auto& nd=_data.nodes.at(id);

	std::ostringstream oss;
	auto start_span=[&oss](std::initializer_list<const char*> attrs) {
		oss<<"<span";
		for(auto attr: attrs) {
			oss<<' ';
			oss<<attr;
		}
		oss<<'>';
	};
	auto stop_span=[&oss]() {
		oss<<"</span>";
	};
	auto start_state=[start_span](DiffState st) {
		switch(st) {
			case DiffState::Del:
				start_span({"color=\"red\""});
				break;
			case DiffState::Add:
				start_span({"color=\"green\""});
				break;
			case DiffState::Eq:
				start_span({});
				break;
			case DiffState::Chg:
				start_span({"color=\"yellow\""});
				break;
		}
	};

	start_state(nd.state);
	oss<<'@'<<nd.id;
	stop_span();
	if(nd.attr.misc.coverage())
		oss<<" pr";
	oss<<'\n';
	oss<<'('<<nd.pos[0]<<", "<<nd.pos[1]<<", "<<nd.pos[2]<<")\n";
	oss<<"t: "<<nd.type<<", r: "<<nd.radius<<'\n';
	for(auto& a: nd.annots)
		oss<<"  @"<<a<<'\n';
	for(unsigned int i=0; i<nd.num_links; i++) {
		auto [id2, st, _inloop]=nd.link_at(i);
		oss<<"  /";
		start_state(st);
		oss<<id2;
		stop_span();
		oss<<'\n';
	}

	if(nd.state==DiffState::Chg) {
		oss<<"\n@"<<nd.id;
		if(nd.attr2.misc.coverage())
			oss<<" pr";
		oss<<'\n';
		oss<<'('<<nd.pos_diff[0]<<", "<<nd.pos_diff[1]<<", "<<nd.pos_diff[2]<<")\n";
		oss<<"t: "<<nd.type2<<", r: "<<nd.radius2<<'\n';
		for(auto& a: nd.annots2)
			oss<<"  @"<<a<<'\n';

	}
	auto str=oss.str();
	gtk_label_set_markup(win->node_info, str.data());
	gtk_widget_set_visible(GTK_WIDGET(win->node_info), true);
}
void gapr::show::Session::hide_graph(MainWindow* win, bool hide) {
	_canvas.hide_graph=hide;
	update(win);

	if(_script && _script->_pending_hide) {
		auto prom=std::move(*_script->_pending_hide);
		_script->_pending_hide.reset();
		prom.set_value(1);
	}
}
void gapr::show::Session::change_mode(MainWindow* win, const char* mode) {
	if(!mode)
		return;
	_cur_mode=mode;
	gtk_widget_set_sensitive(GTK_WIDGET(win->select_mode), false);
	gtk_spinner_start(win->spinner);
	auto ptr=shared_from_this();
	boost::asio::post(_ctx->thread_pool(), [this,ptr]() {
		_data.ids.clear();
		_data.points.clear();
		_data.vfirst.clear();
		_data.vcount.clear();
		_data.nverts=0;
		load_data_prepare_buffer();
		boost::asio::post(_ctx->main_context(), [this,ptr]() {
			if(auto win=_win.lock(); win) {
				gtk_spinner_stop(win->spinner);
				gtk_widget_set_sensitive(GTK_WIDGET(win->select_mode), true);
				gapr::print("mode change finished");
				_data.changed=true;
				update(win);

				if(_script && _script->_pending_mode) {
					auto prom=std::move(*_script->_pending_mode);
					_script->_pending_mode.reset();
					prom.set_value(1);
				}
			}
		});
	});
}
void gapr::show::Session::change_mode_s(MainWindow* win, const char* file) {
	if(_cur_mode=="script") {
		if(!gtk_combo_box_set_active_id(win->select_mode, nullptr))
			assert(0);
	}
	if(!_mod_s) {
		GtkTreeIter iter;
		gtk_list_store_insert_with_values(win->display_modes, &iter, -1,
				0, "Script", 1, "script", 2, true, -1);
	}
	_mod_s=std::make_shared<mode_helper_script>(file);
	if(!gtk_combo_box_set_active_id(win->select_mode, "script"))
		assert(0);
}
void gapr::show::Session::jump_frame1(MainWindow* win, const char* txt) {
	assert(_args.playback);
	uint64_t f;
	auto& playback=*_playback;
	std::string_view user{_cur_user};
	auto get_frame=[txt,win,&playback,user](uint64_t& f) -> int {
		if(txt) {
			errno=0;
			char* eptr;
			f=std::strtoul(txt, &eptr, 10);
			if(errno)
				return errno;
			if(*eptr)
				return EINVAL;
		} else {
			f=playback._nodes_ver+1;
			if(user!=".all") {
				while(f<=playback._infos.size() && playback._infos[f-1].who!=user)
					++f;
			}
		}
		if(f>playback._infos.size())
			return ERANGE;
		return 0;
	};
	std::error_code ec{get_frame(f), std::generic_category()};
	if(ec) {
		gtk::ref dlg=gtk_message_dialog_new(GTK_WINDOW(win), gtk::flags(GTK_DIALOG_MODAL, GTK_DIALOG_DESTROY_WITH_PARENT), GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE, "Failed to jump to specified commit.\n%s", ec.message().c_str());
		g_signal_connect(dlg, "response", G_CALLBACK(gtk_widget_destroy), nullptr);
		gtk_widget_show(GTK_WIDGET(dlg));
		return;
	}

	jump_frame(win, f);
}
void gapr::show::Session::jump_frame(MainWindow* win, uint64_t num) {
	assert(_args.playback);

	gtk_widget_set_sensitive(GTK_WIDGET(win->select_mode), false);
	gtk_widget_set_sensitive(GTK_WIDGET(win->frame_entry), false);
	gtk_widget_set_sensitive(GTK_WIDGET(win->btn_next_frame), false);
	gtk_spinner_start(win->spinner);
	auto ptr=shared_from_this();
	boost::asio::post(_ctx->thread_pool(), [this,ptr,num]() {
		auto chg=_playback->jump_to(num, _data.nodes);
		if(chg) {
			_data.ids.clear();
			_data.points.clear();
			_data.vfirst.clear();
			_data.vcount.clear();
			_data.nverts=0;
			_data.loop_cnt=loop_helper::probe_loops(_data.nodes);
			load_data_prepare_buffer();
		}
		boost::asio::post(_ctx->main_context(), [this,ptr,chg,num]() {
			if(auto win=_win.lock(); win) {
				gtk_spinner_stop(win->spinner);
				gtk_widget_set_sensitive(GTK_WIDGET(win->select_mode), true);
				gtk_widget_set_sensitive(GTK_WIDGET(win->frame_entry), true);
				gtk_widget_set_sensitive(GTK_WIDGET(win->btn_next_frame), true);
				gapr::print("frame change finished");
				if(chg) {
					_data.changed=true;
					auto& playback=*_playback;
					auto str=format_commit_info(num, playback._infos);
					gtk_label_set_markup(win->commit_info, str.data());
					gtk_widget_set_visible(GTK_WIDGET(win->commit_info), true);
				}
				update(win);

				if(_script && _script->_pending_seek) {
					auto prom=std::move(*_script->_pending_seek);
					_script->_pending_seek.reset();
					prom.set_value();
				}
			}
		});
	});
}
void gapr::show::Session::change_user(MainWindow* win, const char* user) {
	_cur_user=user;
}
void gapr::show::Session::locate_camera(MainWindow* win) {
	if(!_playback)
		return;
	auto set_camera=[this](double r, const gapr::vec3<double>& c) {
		for(unsigned int i=0; i<3; ++i)
			_camera.center[i]=c[i];
		_camera.set_zoom(r*std::sqrt(3));
	};
	if(auto v=_playback->_nodes_ver; v!=0) {
		gapr::bbox bbox{_playback->_infos[v-1].bbox};
		gapr::vec3<double> c;
		auto d=bbox.diameter_and_center(c);
		set_camera(d/2, c);
	} else {
		set_camera(_camera.diameter/2, _camera.center_init);
	}
	_camera.changed=true;
	update_camera();
	update(win);
}

#include "gapr/gui/opengl-impl.hh"
