#ifndef _GAPR_FIX_SESSION_HH_
#define _GAPR_FIX_SESSION_HH_

#include "gapr/model.hh"

#include <optional>
#include <array>

namespace gapr::fix {

	constexpr static int MAX_SLICES=480;
	constexpr static std::array SCALE_FACTORS{0, 1, 2, 3, 5, 7};

	class Session: public std::enable_shared_from_this<Session> {
		public:
			struct Args {
				std::string config;
				std::string user;
				std::string passwd;
				std::string host;
				unsigned short port;
				std::string group;
				std::string data;
				Args() noexcept: port{0} { }
			};
			Session(const Session&) =delete;
			Session& operator=(const Session&) =delete;

			void args(Args&& args) { _args.emplace(std::move(args)); }

			virtual void login() =0;
			virtual void got_passwd(std::string&& pw) =0;
			virtual void retry_connect() =0;
			virtual void change_total_slices(int value) =0;
			virtual void change_shown_slices(int value) =0;
			virtual void toggle_slice(bool state) =0;
			virtual void toggle_data_only(bool state) =0;
			virtual void xfunc_global_changed(double low, double up) =0;
			virtual void xfunc_closeup_changed(double low, double up) =0;
			virtual void activate_global_view() =0;
			virtual void activate_closeup_view() =0;
			virtual void highlight_loop() =0;
			virtual void highlight_neuron(int direction) =0;
			virtual void highlight_raised() =0;
			virtual void highlight_orphan() =0;
			virtual void select_closeup_changed(unsigned int index) =0;
			virtual void highlight_reset() =0;
			virtual void select_global_changed(unsigned int index) =0;
			virtual void change_scale(int factor) =0;
			virtual void goto_position(std::string_view pos_str) =0;
			virtual void goto_next_error() =0;
			virtual void clear_end_state() =0;
			virtual void resolve_error(std::string_view state) =0;
			virtual void report_error(std::string_view state) =0;
			virtual void raise_node() =0;
			virtual void create_neuron(std::string&& name) =0;
			virtual void rename_neuron(std::string&& name) =0;
			virtual void remove_neuron() =0;
			virtual void tracing_extend() =0;
			virtual void tracing_branch() =0;
			virtual void tracing_attach(int typ) =0;
			virtual void tracing_end() =0;
			virtual void tracing_end_as(std::string&& st) =0;
			virtual void tracing_delete() =0;
			virtual void tracing_examine() =0;
			virtual void view_refresh() =0;
			virtual void update_details() =0;
			virtual void select_node(gapr::node_id nid) =0;
			virtual void select_noise(std::string_view spec) =0;

			virtual void save_img(std::string&& file) =0;


			virtual void goto_target() =0;
			virtual void tracing_connect() =0;

			/*! canvas */
			virtual void init_opengl(int scale, int width, int height) =0;
			virtual void deinit_opengl() =0;
			virtual void canvas_resize(int w, int h) =0;
			virtual void canvas_scale_factor_changed(int scale_factor) =0;
			virtual void canvas_render() =0;

			virtual void canvas_pressed(int btn, double _x, double _y, bool shift, bool ctrl, bool alt, bool single) =0;
			virtual void canvas_released(int btn, double _x, double _y) =0;
			virtual void canvas_motion(double _x, double _y) =0;
			virtual void canvas_scroll(double d) =0;

			virtual void set_autosel_len(double len) =0;

		protected:
			explicit constexpr Session() noexcept: _args{} { }
			virtual ~Session() { }

			bool has_args() const noexcept { return _args.has_value(); }
			const Args& args() const noexcept { return *_args; }
			void passwd(std::string&& pw) { _args->passwd=std::move(pw); }

		private:
			std::optional<Args> _args;

	};

}

#endif
