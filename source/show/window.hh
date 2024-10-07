#include <thread>
#include <vector>
#include <filesystem>


#include <gtk/gtk.h>
#include <epoxy/gl.h>

#include "gtk.hh"
#include "gapr/utility.hh"
#include "gapr/cube-builder.hh"
#include "gapr/model.hh"
#define GAPR_OPENGL_USE_THAT
#include "gapr/gui/opengl.hh"
#include "gapr/gui/opengl-funcs.hh"

namespace gapr::show {

	typedef struct _MainWindow MainWindow;

	class Session: public std::enable_shared_from_this<Session> {
		public:
			struct Args {
				std::vector<std::filesystem::path> swc_files;
				std::vector<std::filesystem::path> swc_files_cmp;
				std::string image_url;
				std::filesystem::path mesh_path;
				std::string repo_file;
				std::string script_file;
				bool playback{false};
			};

			explicit Session(Context& ctx, Args&& args):
				_ctx{ctx.shared_from_this()}, _args{std::move(args)} {
				}
			~Session() { }
			Session(const Session& r) =delete;
			Session& operator=(const Session& r) =delete;

			GtkWindow* create_window(GtkApplication* app);

		private:
			std::shared_ptr<Context> _ctx;
			Args _args;
			gtk::weak_ref<MainWindow> _win;

			using Funcs=gapr::gl::GlobalFunctions;
			using Framebuffer=gapr::gl::Framebuffer<Funcs>;
			using PickFramebuffer=gapr::gl::PickFramebuffer<Funcs>;
			using VertexArray=gapr::gl::VertexArray<Funcs>;
			using Program=gapr::gl::Program<Funcs>;
			using Texture3D=gapr::gl::Texture3D<Funcs>;

			using VertexArrayMesh=gapr::gl::VertexArrayElem<Funcs>;
			struct MeshVert {
				std::array<GLint, 3> ipos;
				gapr::gl::Packed<GLuint, GL_INT_2_10_10_10_REV> norm;
			};
			struct {
				// device independent
				int width;
				int height;

				Framebuffer fbo_volume;

				VertexArray vao_quad;
				VertexArray vao_edges;
				GLuint vbo_idx{0};

				Program prog_edges;
				GLint prog_edges_thick;
				GLint prog_edges_center;
				GLint prog_edges_proj;
				GLint prog_edges_view;
				GLint prog_edges_color0;
				Program prog_verts;
				GLint prog_verts_thick;
				GLint prog_verts_center;
				GLint prog_verts_proj;
				GLint prog_verts_view;
				GLint prog_verts_color0;
				Program prog_volume;
				GLint prog_volume_xfunc;
				GLint prog_volume_mat_inv;
				GLint prog_volume_zpars;
				GLint prog_volume_color;
				Program prog_mesh;
				GLint prog_mesh_center;
				GLint prog_mesh_view;
				GLint prog_mesh_proj;
				GLint prog_mesh_color;
				Program prog_sort;
				GLint prog_sort_vol_scale;
				GLint prog_sort_offset;

				struct Mesh {
					VertexArrayMesh vao;
					GLsizei count;
					std::array<GLfloat, 4> color;
					bool changed{true};
					std::vector<MeshVert> verts;
					std::vector<GLuint> idxes;
				};
				std::vector<Mesh> meshes;
				bool meshes_changed{false};

				Texture3D cube_tex;
				bool hide_graph{false};
			} _canvas;
			struct {
				int scale;
				int width_alloc;
				int height_alloc;

				Framebuffer fbo_opaque, fbo_edges;

				PickFramebuffer fbo_pick;
				struct {
					Program prog;
					GLint center, id, proj, thick, view;
				} pick_vert, pick_edge;
			} _canvas_s;
			struct {
				int width{0}, height{0};
				int scale{1};
				int width_alloc;
				int height_alloc;

				Framebuffer fbo_opaque, fbo_edges;
				Framebuffer fbo_offscreen, fbo_resize;
				bool has_fbos{false};
			} _canvas_os;
			struct {
				vec3<double> center;
				vec3<double> rgt;
				vec3<double> up;
				double zoom, zoom_max, zoom_min;

				mat4<GLfloat> view, rview;
				mat4<GLfloat> proj, rproj;
				mat4<GLfloat> cube_mat_inv;
				std::array<GLint, 3> icenter;
				GLfloat thickness;
				std::array<GLint, 2> offset{0, 0};

				vec3<double> center_init;
				double diameter, resolution;

				double zoom_save;
				vec3<double> rgt_save, up_save;
				mat4<double> rview_rproj_save;
				vec3<double> center_save;

				bool changed{false};

				void set_zoom(double z) {
					//if(z>zoom_max)
						//z=zoom_max;
					if(z<zoom_min)
						z=zoom_min;
					zoom=z;
				}
			} _camera;
			enum {
				MouseEmpty,
				MouseMenu,
				MouseCenter,
				MouseRotSel,
				MousePanSel,
				MouseZoom,
				MouseZoomRst,
				MouseIgnore
			};
			struct {
				int state{MouseEmpty};
				unsigned int button;
				unsigned int press_cnt;
				double press_x;
				double press_y;
			} _mouse;
			void scroll_event(MainWindow* win, double delta);
			void button_press_event(MainWindow* win, unsigned int button, double x, double y, bool shift, bool ctrl, bool simple);
			void button_release_event(MainWindow* win, unsigned int button, double x, double y);
			void motion_notify_event(MainWindow* win, double x, double y);
			void update_camera();

			void set_scale_factor(MainWindow* win, int scale) noexcept { _canvas_s.scale=scale; }
			void resize(MainWindow* win, int width, int height);
			void render(MainWindow* win);
			void update(MainWindow* win);
			void init_opengl(MainWindow* win, int scale, int width, int height);
			void deinit_opengl(MainWindow* win);
			void change_mode(MainWindow* win, const char* mode);
			void change_mode_s(MainWindow* win, const char* file);
			void jump_frame1(MainWindow* win, const char* txt);
			void jump_frame(MainWindow* win, uint64_t num);
			void change_user(MainWindow* win, const char* user);
			void locate_camera(MainWindow* win);

			struct PointGL {
				std::array<GLint, 3> ipos;
				GLuint misc;
				//std::array<GLint, 3>...;
			};
			enum class DiffState { Del, Add, Eq, Chg };
			struct LinkData {
				int64_t id2;
				DiffState state;
				bool in_loop{false};
			};
			struct NodeData {
				DiffState state;
				gapr::node_attr attr, attr2;
				int type, type2;
				double radius, radius2;
				std::array<double, 3> pos;
				std::array<double, 3> pos_diff;
				std::vector<std::string> annots, annots2;
				bool is_root{false};
				int64_t root_id{-1};
				int64_t par_id;
				int64_t id;
				std::size_t num_links;
				std::array<LinkData, 2> links;
				std::vector<LinkData> more_links;
				LinkData& link_at(std::size_t i);
				std::pair<LinkData*, bool> link_ins(int64_t id2);
			};
			struct {
				std::unordered_map<int64_t, NodeData> nodes;
				std::vector<PointGL> points;
				std::vector<int64_t> ids;
				std::vector<const GLvoid*> vfirst;
				std::vector<GLsizei> vcount;
				std::size_t nverts{0};
				bool ready{false};
				bool changed{false};
				std::vector<vec3<GLfloat>> colors;
				unsigned int loop_cnt;
			} _data;

			struct {
				gapr::cube closeup_cube;
				std::array<unsigned int, 3> closeup_offset;
				unsigned int closeup_chan;
				gapr::cube global_cube;
				unsigned int global_chan;
				std::array<double, 2> xfunc_state{0.0, 1.0};
				std::vector<gapr::cube_info> cube_infos;

				unsigned int chan{0};
				bool changed{false};
			} _image;

			std::string _cur_mode{};
			std::string _cur_user{};

			std::shared_ptr<gapr::cube_builder> _cube_builder;

			void start(MainWindow* win);
			void load_data();
			void load_data_repo();
			void load_data_swc();
			void load_data_prepare_buffer();
			void dump_diff();
			void load_data_cb(MainWindow* win);
			void run_script(std::string_view file);
			unsigned int parse_catalog(std::streambuf& sb);
			void load_catalog_cb(MainWindow* win, unsigned int chan);
			bool do_refresh_image(MainWindow* win);
			void refresh_image(MainWindow* win);
			void image_progress(std::error_code ec, int progr);
			void update_xfunc(MainWindow* win, unsigned int, double v);
			int64_t pick_node(MainWindow* win, int x, int y);
			void show_node(MainWindow* win, int64_t id);
			void hide_graph(MainWindow* win, bool hide);

			friend struct _MainWindow;

			struct mode_helper_diff;
			struct mode_helper_recon;
			struct mode_helper_result;
			struct mode_helper_script;
			std::shared_ptr<mode_helper_script> _mod_s;
			struct playback_helper;
			std::shared_ptr<playback_helper> _playback;
			struct loop_helper;
			struct script_helper;
			std::shared_ptr<script_helper> _script;
	};

}
