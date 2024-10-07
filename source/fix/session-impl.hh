/* fix/session-impl.hh
 *
 * Copyright (C) 2018-2021 GOU Lingfeng
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

//XXX retry only when network error. (reconnect)
#include "session.hh"

#include "gapr/cube-builder.hh"
#include "gapr/cube.hh"
#include "gapr/utility.hh"
#include "gapr/streambuf.hh"
#include "gapr/str-glue.hh"
#include "gapr/timer.hh"
#include "gapr/trace-api.hh"
#include "gapr/vec3.hh"
#include "gapr/fiber.hh"
#include "gapr/mem-file.hh"
#include "gapr/connection.hh"

#include "gapr/gui/opengl.hh"

#include <fstream>
#include <chrono>
#include <regex>
#include <charconv>
#include <random>

#include <boost/asio/ip/tcp.hpp>

#include "compute.hh"
#include "gapr/edge-model.hh"
#include "vertex-array-manager.hh"
#include "state-manager.hh"

#include "dup/serialize-delta.hh"
#include "gapr/detail/nrrd-output.hh"

using namespace std::string_view_literals;

/*! the same as gapr::node_attr::data_type,
 * use GLuint to ensure OpenGL compatibility.
 */
using PointGL=std::pair<std::array<GLint, 3>, GLuint>;
const std::vector<PointGL>& pathToPathGL(const std::vector<gapr::edge_model::point>& points) { return points; }

struct CubeTexture {
	GLuint tex;
	int refc;
	//CubeId cubeId;
	int prev_unused;
};

template<typename Funcs>
class ViewerShared: private Funcs {
	//QOpenGLShaderProgram* progs[SHADER_PROG_NUM];
	GLuint vbo_fixed{0};
	int refc;

	public:
	ViewerShared():
		//progs{nullptr,},
		cubeTextures{},
		refc{0}
	{
		CubeTexture texHead;
		texHead.tex=0;
		texHead.refc=0;
		texHead.prev_unused=0;
		cubeTextures.emplace_back(texHead);
	}
	std::vector<CubeTexture> cubeTextures;
	GLuint vao_fixed{0};
	bool initialize() {
		Funcs::initialize();
		if(refc) {
			refc++;
			return true;
		}

		std::vector<GLfloat> fixed_data{
			-1.0f, 1.0f, 0.0f, // 0 box (slice proxy)
				-1.0f, -1.0f, 0.0f,
				1.0f, 1.0f, 0.0f,
				1.0f, -1.0f, 0.0f,
				1.0f, 0, 0, // 4 line
				0, 1.0f, 0,
				-1.0f, 0.0f, 0.0f, // 6 cross
				1.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f,
				0.0f, -1.0f, 0.0f,
				-1.0f, 0.0f, 0.0f, // 10 open cross
				-.4f, 0.0f, 0.0f,
				1.0f, 0.0f, 0.0f,
				.4f, 0.0f, 0.0f,
				0.0f, -1.0f, 0.0f,
				0.0f, -0.4f, 0.0f,
				0.0f, 1.0f, 0.0f,
				0.0f, 0.4f, 0.0f,

				-.8f, 1.0f, 0.0f, // 18 X
				.8f, -1.0f, 0.0f,
				-1.0f, -1.0f, 0.0f,
				1.0f, 1.0f, 0.0f,
				-.8f, 1.0f, 0.0f, // 22 Y
				0.0f, 0.0f, 0.0f,
				-1.0f, -1.0f, 0.0f,
				1.0f, 1.0f, 0.0f,
				-1.0f, -1.0f, 0.0f, // 26 Z
				1.0f, 1.0f, 0.0f,
				-.8f, 1.0f, 0.0f,
				1.0f, 1.0f, 0.0f,
				.8f, -1.0f, 0.0f,
				-1.0f, -1.0f, 0.0f,
				-1.0f, 1.0f, 0.0f, // 32 err box
				-1.0f, -1.0f, 0.0f,
				1.0f, -1.0f, 0.0f,
				1.0f, 1.0f, 0.0f,
				-1.5f, 1.0f, 0.0f, // 36 err box
				-1.0f, -1.0f, 0.0f,
				1.5f, -1.0f, 0.0f,
				1.0f, 1.0f, 0.0f,
				-.707f, -.707f, 0.0f, // 40 cross
				.707f, .707f, 0.0f,
				-.707f, .707f, 0.0f,
				.707f, -.707f, 0.0f,
				-1.0f, -.707f, 0.0f, // 44 frame
				-1.0f, -1.0f, 0.0f,
				-.707f, -1.0f, 0.0f,
				-1.0f, -1.0f, 0.0f,
				-1.0f, .707f, 0.0f,
				-1.0f, 1.0f, 0.0f,
				-.707f, 1.0f, 0.0f,
				-1.0f, 1.0f, 0.0f,
				1.0f, -.707f, 0.0f,
				1.0f, -1.0f, 0.0f,
				.707f, -1.0f, 0.0f,
				1.0f, -1.0f, 0.0f,
				1.0f, .707f, 0.0f,
				1.0f, 1.0f, 0.0f,
				.707f, 1.0f, 0.0f,
				1.0f, 1.0f, 0.0f,
		};
		fixed_data.resize(300);
		for(int i=0; i<32; i++) {
			float x=cos(i*2*M_PI/32);
			float y=sin(i*2*M_PI/32);
			fixed_data.emplace_back(x);
			fixed_data.emplace_back(y);
			fixed_data.emplace_back(0);
		}

		this->glGenBuffers(1, &vbo_fixed);
		this->glBindBuffer(GL_ARRAY_BUFFER, vbo_fixed);
		this->glBufferData(GL_ARRAY_BUFFER, fixed_data.size()*sizeof(GLfloat), fixed_data.data(), GL_STATIC_DRAW);
		this->glGenVertexArrays(1, &vao_fixed);
		this->glBindVertexArray(vao_fixed);
		this->glEnableVertexAttribArray(0);
		this->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*3, nullptr);

		refc++;
		return true;
	}
	bool deinitialize() {
		if(!refc)
			return false;
		refc--;
		if(refc)
			return true;

		if(vbo_fixed)
			this->glDeleteBuffers(1, &vbo_fixed);
		if(vao_fixed)
			this->glDeleteVertexArrays(1, &vao_fixed);
		for(size_t i=1; i<cubeTextures.size(); i++) {
			this->glDeleteTextures(1, &cubeTextures[i].tex);
		}
		return true;
	}
	bool releaseTexture(int i) {
		if(!cubeTextures[i].refc)
			return false;
		cubeTextures[i].refc--;
		if(cubeTextures[i].refc)
			return true;

		if(cubeTextures[i].tex) {
			this->glDeleteTextures(1, &cubeTextures[i].tex);
			cubeTextures[i].tex=0;
		}
		cubeTextures[i].prev_unused=cubeTextures[0].prev_unused;
		cubeTextures[0].prev_unused=i;
		return true;
	}
	int getTexture(const gapr::cube& data) {
		for(size_t i=1; i<cubeTextures.size(); i++) {
			if(cubeTextures[i].refc && false /*cubeTextures[i].cubeId==data.cubeId()*/) {
				cubeTextures[i].refc++;
				return i;
			}
		}
		int i;
		//
		if(cubeTextures[0].prev_unused) {
			i=cubeTextures[0].prev_unused;
			cubeTextures[0].prev_unused=cubeTextures[i].prev_unused;
		} else {
			CubeTexture tex;
			tex.tex=0;
			tex.refc=0;
			cubeTextures.emplace_back(tex);
			i=cubeTextures.size()-1;
		}
		auto cube_view=data.view<const void>();
		GLsizei width=cube_view.width_adj();
		GLsizei height=cube_view.sizes(1);
		GLsizei depth=cube_view.sizes(2);
		const GLvoid* ptr=cube_view.row(0, 0);
		gapr::print("update tex ", static_cast<unsigned int>(cube_view.type()),
				':', width, ':', height, ':', depth);
		switch(cube_view.type()) {
			case gapr::cube_type::u8:
				this->glGenTextures(1, &cubeTextures[i].tex);
				this->glActiveTexture(GL_TEXTURE1);
				this->glBindTexture(GL_TEXTURE_3D, cubeTextures[i].tex);
				this->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				this->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				this->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				this->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				this->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
				this->glBindTexture(GL_TEXTURE_3D, cubeTextures[i].tex);
				gapr::print("update tex ", 8);
				this->glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, width, cube_view.sizes(1), cube_view.sizes(2), 0, GL_RED, GL_UNSIGNED_BYTE, cube_view.row(0, 0));
				break;
			case gapr::cube_type::u16:
				this->glGenTextures(1, &cubeTextures[i].tex);
				this->glActiveTexture(GL_TEXTURE1);
				this->glBindTexture(GL_TEXTURE_3D, cubeTextures[i].tex);
				this->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				this->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				this->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				this->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				this->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
				this->glBindTexture(GL_TEXTURE_3D, cubeTextures[i].tex);
				this->glTexImage3D(GL_TEXTURE_3D, 0, GL_R16, width, height, depth, 0, GL_RED, GL_UNSIGNED_SHORT, ptr);
				break;
			case gapr::cube_type::f32:
				this->glGenTextures(1, &cubeTextures[i].tex);
				this->glActiveTexture(GL_TEXTURE1);
				this->glBindTexture(GL_TEXTURE_3D, cubeTextures[i].tex);
				this->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				this->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				this->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				this->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				this->glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
				this->glBindTexture(GL_TEXTURE_3D, cubeTextures[i].tex);
				gapr::print("update tex ", 32);
				this->glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, width, cube_view.sizes(1), cube_view.sizes(2), 0, GL_RED, GL_FLOAT, cube_view.row(0, 0));
				break;
			default:
				break;
		}
		//cubeTextures[i].cubeId=data.cubeId();
		cubeTextures[i].refc++;
		return i;
	}
	~ViewerShared() {
	}
	private:
};

namespace ba=boost::asio;
namespace bs=boost::system;

static void make_brief(std::ostream& oss, const gapr::fix::Position& pos, gapr::edge_model::view model) {
	if(pos.edge) {
		auto& edg=model.edges().at(pos.edge);
		auto i=pos.index/128;
		if(pos.index%128==0) {
			oss<<"-[node:";
			oss<<edg.nodes[i].data;
			oss<<"]-";
		} else {
			oss<<"[node:";
			oss<<edg.nodes[i].data;
			oss<<"]-[node:";
			oss<<edg.nodes[i+1].data;
			oss<<"]";
		}
	} else {
		if(pos.vertex) {
			oss<<"[node:";
			oss<<pos.vertex.data;
			oss<<"]";
		} else {
			oss<<"[]";
		}
	}
}

static void make_detail(std::ostream& oss, const gapr::fix::Position& pos, gapr::edge_model::view model) {
	auto print_props=[&oss,model](gapr::node_id nid) {
		auto [it1, it2]=model.props().per_node(nid);
		gapr::prop_id id{nid, {}};
		for(auto i=it1; i!=it2; ++i) {
			id.key=i->second;
			auto& val=model.props().at(id);
			oss<<'\n'<<id.key<<'='<<val;
		}
	};
	if(pos.edge) {
		auto& edg=model.edges().at(pos.edge);
		auto i=pos.index/128;
		if(pos.index%128==0) {
			oss<<"[node:";
			oss<<edg.nodes[i-1].data;
			oss<<"]-[node:";
			oss<<edg.nodes[i].data;
			oss<<"]-[";
			oss<<edg.nodes[i+1].data;
			oss<<"]\n(lattr)-(";
			gapr::node_attr attr{edg.points[i]};
			oss<<attr.pos(0)<<','<<attr.pos(1)<<','<<attr.pos(2);
			oss<<")(t:";
			oss<<attr.misc.t()<<",r:";
			oss<<attr.misc.r();
			if(attr.misc.coverage())
				oss<<",pr";
			oss<<")-(lattr)\n[node:";
			oss<<edg.left.data;
			oss<<"]---[node:";
			oss<<edg.right.data;
			oss<<"] "<<i<<'/'<<edg.nodes.size();
			print_props(edg.nodes[i]);
		} else {
			oss<<"[node:";
			oss<<edg.nodes[i].data;
			oss<<"]-(lattr)-[node:";
			oss<<edg.nodes[i+1].data;
			oss<<"]\n[node:";
			oss<<edg.left.data;
			oss<<"]---[node:";
			oss<<edg.right.data;
			auto j=pos.index%128;
			oss<<"] "<<i<<'/'<<edg.nodes.size()<<'+'<<j<<'\n';
		}
	} else {
		if(pos.vertex) {
			oss<<"[node:";
			oss<<pos.vertex.data;
			oss<<"]\n(";
			auto& vert=model.vertices().at(pos.vertex);
			gapr::node_attr attr{vert.attr};
			oss<<attr.pos(0)<<','<<attr.pos(1)<<','<<attr.pos(2);
			oss<<")(t:";
			oss<<attr.misc.t()<<",r:";
			oss<<attr.misc.r();
			oss<<") *"<<vert.edges.size();
			print_props(pos.vertex);
		} else {
			oss<<"[]\n(";
			gapr::node_attr attr{pos.point};
			oss<<attr.pos(0)<<','<<attr.pos(1)<<','<<attr.pos(2)<<')';
		}
	}
}

#include "misc.hh"

namespace gapr::fix {
	class Session;
	class Window;

	enum class ViewMode {
		Global,
		Closeup,
		Mixed,
	};
	enum class SessionStage {
		Closed,
		Opening,
		Opened,
	};
}

enum class MouseMode {
	Nul,
	RectSel,
	Rot,
	Drag,
	Pan
};

struct MouseState {
	int xPressed;
	int yPressed;
	int x;
	int y;
	bool moved;
	bool sel_all;
	MouseMode mode{MouseMode::Nul};
};

#define PICK_SIZE 6
#define FBO_ALLOC 64
#define FBO_ALIGN 16

enum Color : int {
	C_BG=0,
	C_CHAN0=15,
	C_AXIS_X=9,
	C_AXIS_Y=10,
	C_AXIS_Z=12,
	C_ALERT=9,
	C_ATTENTION=11,
	C_TYPE_0=12,
	C_TYPE_1=14,
	C_TYPE_2=10,
	C_TYPE_3=13-8,
	C_TYPE_4=13,
};

std::array<gapr::vec3<GLfloat>, 16> colors{{
		{0.000, 0.000, 0.000},
		{0.804, 0.000, 0.000},
		{0.000, 0.804, 0.000},
		{0.804, 0.804, 0.000},
		{0.000, 0.000, 0.933},
		{0.804, 0.000, 0.804},
		{0.000, 0.804, 0.804},
		{0.898, 0.898, 0.898},

		{0.498, 0.498, 0.498},
		{1.000, 0.000, 0.000},
		{0.000, 1.000, 0.000},
		{1.000, 1.000, 0.000},
		{0.361, 0.361, 1.000},
		{1.000, 0.000, 1.000},
		{0.000, 1.000, 1.000},
		{1.000, 1.000, 1.000},
}};

gapr::vec3<GLfloat> color2vec(unsigned int code) {
	return {(code&0xff0000u)/(256*256*255.0f),
		(code&0xff00u)/(256*255.0f), (code&0xffu)/255.0f};
}
unsigned int vec2color(const gapr::vec3<GLfloat>& v) {
	unsigned int r=v[0]*255+.5f;
	unsigned int g=v[1]*255+.5f;
	unsigned int b=v[2]*255+.5f;
	return (r<<16)|(g<<8)|b;
}

namespace gapr::fix {
	template<typename Base, typename Funcs> class SessionImpl;
}

template<typename Base, typename Funcs>
class gapr::fix::SessionImpl: public gapr::fix::Session, private Base {

	using resolver=ba::ip::tcp::resolver;
	std::optional<resolver> _resolver;
	resolver::results_type _addrs{};
	resolver::endpoint_type _addr{ba::ip::address{}, 0};
	client_end _cur_conn{};
	client_end _conn_need_pw{};

	bool _initialized{false};
	std::string _data_secret;
	std::string _srv_info;
	gapr::tier _tier;
	std::string _gecos;
	gapr::stage _repo_stage;
	gapr::tier _tier2;

	SessionStage _stage{SessionStage::Closed};
	gapr::trace_api api;
	gapr::edge_model _model;
	gapr::commit_history _hist;
	uint64_t _latest_commit;

	bool _states_valid{false};
	//XXX state_section;

	ViewMode _view_mode{ViewMode::Global};

	Position _cur_pos, _tgt_pos;
	anchor_id _cur_anchor, _tgt_anchor;
	FixPos _cur_fix, _tgt_fix;
	gapr::node_id _list_sel;

	std::vector<gapr::cube_info> _cube_infos;
	bool _has_global_ch{false}, _has_closeup_ch{false};
	std::shared_ptr<gapr::cube_builder> _cube_builder{};
	std::vector<std::array<double, 4>> _xfunc_states;
	std::size_t _global_ch{0};
	gapr::cube _global_cube;
	bool _loading_global{false};
	std::size_t _closeup_ch{0};
	gapr::cube _closeup_cube;
	bool _loading_closeup{false};
	std::array<unsigned int, 3> _closeup_offset;

	std::atomic<bool> _cancel_flag{false};
	PreLock _prelock_model{}, _prelock_path{};
	gapr::delta_add_edge_ _path;
	std::vector<std::pair<gapr::node_id, gapr::node_attr>> _nodes_sel;

	const gapr::affine_xform& global_xform() const noexcept {
		assert(_global_ch>0);
		return _cube_infos[_global_ch-1].xform;
	}
	const gapr::affine_xform& closeup_xform() const noexcept {
		assert(_closeup_ch>0);
		return _cube_infos[_closeup_ch-1].xform;
	}
	std::array<unsigned int, 3> global_sizes() const noexcept {
		assert(_global_ch>0);
		return _cube_infos[_global_ch-1].sizes;
	}
	std::array<unsigned int, 3> closeup_sizes() const noexcept {
		assert(_closeup_ch>0);
		return _cube_infos[_closeup_ch-1].sizes;
	}
	std::array<unsigned int, 3> closeup_cube_sizes() const noexcept {
		assert(_closeup_ch>0);
		return _cube_infos[_closeup_ch-1].cube_sizes;
	}
	std::string _global_uri;
	std::array<double, 2> _global_xfunc;
	std::string _closeup_uri;
	std::array<double, 2> _closeup_xfunc;
	const std::vector<edge_model::point>& path_data() const noexcept {
		return _path.nodes;
	}

	int _scaling{0};
	bool _slice_mode{false};
	std::array<unsigned int, 2> _slice_pars{480, 480};
	double _global_zoom;
	double _closeup_zoom;
	std::array<double, 6> _direction{0, -1.0, 0, 0, 0, -1.0};
	bool _data_only{false};
	double _autosel_len;

	//MVP
	struct Matrices {
		gapr::mat4<GLfloat> mView, mrView, mProj, mrProj;
		gapr::mat4<GLfloat> cube_mat;
	} _mat[3]; // global closeup inset
	enum CH_NAMES: unsigned int {
		CH_GLOBAL,
		CH_CLOSEUP,
		CH_INSET,
	};
	bool funcs_ok{false};

	GLsizei fbo_width, fbo_height;
	GLsizei fbo_width_alloc, fbo_height_alloc;
	GLsizei _inset_w, _inset_h;
	double _scale_factor;
	int _wid_width, _wid_height;

	std::shared_ptr<ViewerShared<Funcs>> viewerShared;
	using Framebuffer=gapr::gl::Framebuffer<Funcs>;
	using FramebufferPick=gapr::gl::PickFramebuffer<Funcs>;
	using Program=gapr::gl::Program<Funcs>;
	Framebuffer fbo_opaque{};
	Framebuffer fbo_surface{};
	FramebufferPick fbo_pick{};
	Framebuffer fbo_cubes{};
	Framebuffer fbo_scale{};
	struct {
		Program prog;
		GLint center, color0, proj, thick, view;
		std::false_type picking;
	} draw_edge, draw_vert;
	struct {
		Program prog;
		GLint center, color, proj, thick, view;
	} draw_line;
	struct {
		Program prog;
		GLint center, color, offset, proj, thick, view;
	} draw_mark;
	struct {
		Program prog;
		GLint color, rproj, rtex, rview, xfunc, zpars;
	} draw_cube;
	struct {
		Program prog;
	} draw_sort;
	struct {
		Program prog;
		GLint center, edge_id, proj, thick, view;
		std::true_type picking;
	} pick_edge, pick_vert;

	int pbiPath{0}, pbiFixed;
	using VertexArrayMan=vertex_array_manager<Funcs, 10, &PointGL::first, &PointGL::second>;
	VertexArrayMan _vao_man;
	draw_cache<int, unsigned int, GLuint> _edge_draw_cache;
	draw_cache<unsigned int, GLuint> _vert_draw_cache;
	unsigned _vert_vaoi;
	std::vector<gapr::node_id> _vert_nodes;
	bool _edg_cache_dirty{false};

	//Inset specific
	double _inset_zoom;
	std::array<double, 3> _inset_center;
	//asis inset (closeup)

	double global_min_voxel;
	double global_max_dim;
	int global_cube_tex{0};

	double closeup_min_voxel;
	double closeup_max_dim;
	int closeup_cube_tex{0};

	// Volume
	int _slice_delta{0};

	MouseState mouse;
	gapr::mat4<GLfloat> mrViewProj;
	gapr::vec3<GLfloat> _prev_up, _prev_rgt;

	//need? init? cfg control ctor/dtor/func
	gapr::vec3<GLfloat> pickA{}, pickB{};
	//bool colorMode;

	state_manager _state_man{};
	state_manager::primary
		_state1_closeup_cube=_state_man.add_primary(),
		_state1_closeup_info=_state_man.add_primary(),
		_state1_closeup_xfunc=_state_man.add_primary(),
		_state1_closeup_zoom=_state_man.add_primary(),
		_state1_global_cube=_state_man.add_primary(),
		_state1_global_info=_state_man.add_primary(),
		_state1_global_xfunc=_state_man.add_primary(),
		_state1_global_zoom=_state_man.add_primary(),
		_state1_cur_pos=_state_man.add_primary(),
		_state1_tgt_pos=_state_man.add_primary(),
		_state1_data_only=_state_man.add_primary(),
		_state1_direction=_state_man.add_primary(),
		_state1_view_mode=_state_man.add_primary(),
		_state1_slice_mode=_state_man.add_primary(),
		_state1_slice_delta=_state_man.add_primary(),
		_state1_slice_pars=_state_man.add_primary(),
		_state1_scale_factor=_state_man.add_primary(),
		_state1_scaling=_state_man.add_primary(),
		_state1_hl_mode=_state_man.add_primary(),
		_state1_list_sel=_state_man.add_primary(),
		_state1_loading=_state_man.add_primary(),
		_state1_model_lock=_state_man.add_primary(),
		_state1_path_lock=_state_man.add_primary(),
		_state1_model=_state_man.add_primary(),
		_state1_path=_state_man.add_primary(),
		_state1_sel=_state_man.add_primary(),
		_state1_sel_rect=_state_man.add_primary(),
		_state1_stage=_state_man.add_primary();
	state_manager::secondary _state2_opened;
	state_manager::secondary _state2_can_edit;
	state_manager::secondary _state2_can_edit0;
	state_manager::secondary _state2_can_hl;
	state_manager::secondary _state2_can_goto_pos;
	state_manager::secondary _state2_can_goto_tgt;
	state_manager::secondary _state2_can_clear_end;
	state_manager::secondary _state2_can_pick_cur;
	state_manager::secondary _state2_can_resolve_err;
	state_manager::secondary _state2_can_report_err;
	state_manager::secondary _state2_can_raise_node;

	state_manager::secondary _state2_can_create_nrn;
	state_manager::secondary _state2_can_rename_nrn;
	state_manager::secondary _state2_can_remove_nrn;

	state_manager::secondary _state2_can_connect;
	state_manager::secondary _state2_can_extend;
	state_manager::secondary _state2_can_branch;
	state_manager::secondary _state2_can_attach;
	state_manager::secondary _state2_can_end;
	state_manager::secondary _state2_can_delete;
	state_manager::secondary _state2_can_examine;
	state_manager::secondary _state2_can_chg_view;

	state_manager::secondary _state2_can_open_prop;
	state_manager::secondary _state2_can_hide_edge;
	state_manager::secondary _state2_can_show_slice;
	state_manager::secondary _state2_can_close;
	state_manager::secondary _state2_can_refresh;

	state_manager _state_man2{};
	state_manager::primary
		_gl_state1_closeup_cube=_state_man2.add_primary(),
		_gl_state1_closeup_info=_state_man2.add_primary(),
		_gl_state1_global_cube=_state_man2.add_primary(),
		_gl_state1_path=_state_man2.add_primary(),
		_gl_state1_scale_factor=_state_man2.add_primary(),
		_gl_state1_scaling=_state_man2.add_primary();

	void initialize_states();

	void canvas_set_global_info() {
		//when apply
		//bbox
		//clearPrev
		_state_man.change(_state1_global_info);
	}


	void canvas_set_global_xfunc(std::array<double, 2> xfunc) {
		_global_xfunc=xfunc;
		_state_man.change(_state1_global_xfunc);
	}

	void canvas_set_closeup_info() {
		//loc off sizes
		//when apply
		//clearA
		_state_man.change(_state1_closeup_info);
		_state_man2.change(_gl_state1_closeup_info);
	}
	void canvas_set_closeup_xfunc(std::array<double, 2> xfunc) {
		_closeup_xfunc=xfunc;
		_state_man.change(_state1_closeup_xfunc);
	}
	void canvas_set_current() {
		_state_man.change(_state1_cur_pos);
	}
	void canvas_clear_path() {
		if(_path.nodes.empty())
			return;
		_path.nodes.clear();
		_state_man.change(_state1_path);
		_state_man2.change(_gl_state1_path);
	}
	void canvas_set_path_data(gapr::delta_add_edge_&& pth) {
		if(pth.nodes.size()<2) {
			if(_path.nodes.empty())
				return;
			_path.nodes.clear();
		} else {
			_path=std::move(pth);
		}
		_state_man.change(_state1_path);
		_state_man2.change(_gl_state1_path);
	}
	void canvas_set_view_mode() {
		_state_man.change(_state1_view_mode);
	}
	void canvas_set_scaling(int factor) {
		if(_scaling==factor)
			return;
		_scaling=factor;
		_state_man.change(_state1_scaling);
		_state_man2.change(_gl_state1_scaling);
	}
	void canvas_set_slice_mode(bool enabled) {
		_slice_mode=enabled;
		_state_man.change(_state1_slice_mode);
	}
	void canvas_set_slice_pars(std::array<unsigned int, 2> pars) {
		_slice_pars=pars;
		_state_man.change(_state1_slice_pars);
	}
	void canvas_set_global_zoom(double zoom) {
		_global_zoom=zoom;
		_state_man.change(_state1_global_zoom);
	}
	void canvas_set_closeup_zoom(double zoom) {
		_closeup_zoom=zoom;
		_state_man.change(_state1_closeup_zoom);
	}
	void canvas_set_directions(const std::array<double, 6>& dirs) {
		_direction=dirs;
		_state_man.change(_state1_direction);
	}
	void canvas_set_data_only(bool data_only) {
		_data_only=data_only;
		_state_man.change(_state1_data_only);
	}
	void canvas_model_changed(const std::vector<edge_model::edge_id>& edges_del) {
		_state_man.change(_state1_model);
		if(!edges_del.empty())
			edges_removed(edges_del);
	}
	
	void canvas_edges_removed(const std::vector<edge_model::edge_id>& edges_del);
	////////////////////////////////////////////////////////////////////

	public:
	template<typename... T>
		explicit SessionImpl(T&&... pars):
			Base{std::forward<T>(pars)...}
	{
	}

	template<typename... T> void constructed(T&&... pars) {
		Base::constructed(std::forward<T>(pars)...);
		/*! stage 0, window constructed */

#if XXX_WAS_IN_SETUP_UI
		canvas_set_scaling(scaling);
		canvas_set_slice_pars(slice_params);
		_state_man.propagate();
#endif

		viewerShared=std::make_shared<ViewerShared<Funcs>>();
		//viewerShared{Tracer::instance()->viewerShared()},
		//colors{}, colorMode{false},
		//curPos{}, tgtPos{},
		//mView{}, mrView{}, mProj{}, mrProj{},
		//scaling{0}, fbo_width{0}, fbo_height{0},
		//funcs{nullptr},
		//pathBuffers{}, 
		// XXX color config


		if(auto dbg_flags=getenv("GAPR_DEBUG"); dbg_flags) {
			std::regex r{"\\bfps\\b", std::regex::icase};
			if(std::regex_search(dbg_flags, r)) {
				_debug_fps=true;
			}
		}

		if(has_args()) {
			gapr::str_glue title{args().user, '@', args().host, ':', args().port, '/', args().group, "[*]"};
			this->window_title(title.str());
		}
		initialize_states();
	}

	~SessionImpl() {
		if(!funcs_ok)
			return;
	}

	private:
	std::array<double, 2> calc_xfunc(std::size_t ch) {
		auto& st=_xfunc_states[ch-1];
		auto& range=_cube_infos[ch-1].range;
		auto d=range[1]-range[0];
		return {range[0]+st[0]*d, range[1]+(st[1]-1)*d};
	}
	void calc_center(Position& pos, std::size_t ch) {
		auto& info=_cube_infos[ch-1];
		gapr::node_attr pt;
		for(unsigned int i=0; i<3; i++) {
			double x{0.0};
			for(unsigned int j=0; j<3; j++)
				x+=info.xform.direction[i+j*3]*info.sizes[j];
			pt.pos(i, x/2+info.xform.origin[i]);
		}
		pt.misc.data=1;
		pos.point=pt.data();
	}
	template<bool closeup>
	double calc_defult_zoom(std::size_t ch) {
		auto& info=_cube_infos[ch-1];
		double max_d{0.0};
		for(unsigned int i=0; i<3; i++) {
			auto d=info.xform.resolution[i]*(closeup?info.cube_sizes[i]*2:info.sizes[i]);
			if(d>max_d)
				max_d=d;
		}
		return max_d*2/3;
	}


	void postConstruct() {
		_cube_builder=std::make_shared<gapr::cube_builder>(this->ui_executor(), this->thread_pool());

		this->ui_init_list();

		//QObject::connect(_cube_builder, &LoadThread::threadWarning, session, &Session::loadThreadWarning);
		//QObject::connect(_cube_builder, &LoadThread::threadError, session, &Session::loadThreadError);
		_cube_builder->async_wait([this](std::error_code ec, int progr) {
			return cubeFinished(ec, progr);
		});
		//QObject::connect(_cube_builder, &LoadThread::updateProgress, session, &Session::updateProgress);

		//_cube_builder->start();

		//_cube_builder->getBoundingBox(&bbox);
		//_cube_builder->getMinVoxelSize(&mvol);

		_cur_anchor=gapr::anchor_id{gapr::node_id{1}, {}, 0};
		// XXX
		_tgt_anchor=gapr::anchor_id{gapr::node_id{4}, {}, 0};
		canvas_set_current();

		// XXX parse states right after receiving
		// put parsing results in mem. var.
		// do check while parsing.
		// XXX restore _xfunc_states
		// "xfunc_states.I"

		{
			// Assume parsing done and values in range.
			auto global_h=this->ui_combo_global_init();
			auto closeup_h=this->ui_combo_closeup_init();
			std::size_t first_c{0}, first_g{0};
			for(unsigned int i=0; i<_cube_infos.size(); i++) {
				if(_cube_infos[i].is_pattern()) {
					if(!first_c) {
						first_c=i+1;
						if(!_states_valid)
							_closeup_ch=first_c;
					}
					closeup_h.add(_cube_infos[i].name(), i+1, _closeup_ch==i+1);
				} else {
					if(!first_g) {
						first_g=i+1;
						if(!_states_valid)
							_global_ch=first_g;
					}
					global_h.add(_cube_infos[i].name(), i+1, _global_ch==i+1);
				}
			}
			this->ui_enable_channels(first_g, first_c);
			_has_global_ch=first_g;
			_has_closeup_ch=first_c;
		}
		if(_closeup_ch) {
			this->ui_enable_closeup_xfunc(_xfunc_states[_closeup_ch-1]);
			auto& info=_cube_infos[_closeup_ch-1];
			canvas_set_closeup_info();
			canvas_set_closeup_xfunc(calc_xfunc(_closeup_ch));
			if(!_cur_pos.valid()) {
				calc_center(_cur_pos, _closeup_ch);
			}
			canvas_set_closeup_zoom(calc_defult_zoom<true>(_closeup_ch));
			//??**void set_closeup_cube(gapr::cube cube, std::string&& uri, std::array<unsigned int, 3> offset);
		} else {
			this->ui_disable_closeup_xfunc();
		}
		if(_global_ch) {
			this->ui_enable_global_xfunc(_xfunc_states[_global_ch-1]);
			auto& info=_cube_infos[_global_ch-1];
			canvas_set_global_info();
			canvas_set_global_xfunc(calc_xfunc(_global_ch));
			if(!_cur_pos.valid())
				calc_center(_cur_pos, _global_ch);
			canvas_set_global_zoom(calc_defult_zoom<false>(_global_ch));
			//??**void set_global_cube(gapr::cube cube, std::string&& uri);
		} else {
			this->ui_disable_global_xfunc();
		}

		if(!_states_valid)
			_view_mode=ViewMode::Global;
		canvas_set_view_mode();

		this->ui_xfunc_set_default(_view_mode!=ViewMode::Global);

		//canvas_set_directions(const std::array<double, 6>& dirs);

		// XXX no auto download at startup
		//startDownloadCube();

		_state_man.propagate();

#if 0
		// XXX init VC
		//connect(dlg.get(), &QDialog::finished, this, &Window::ask_password_cb);
		// {
		//   catalog: sources
		//   target: clear
		//   commit_id: no_config use_latest
		// }
		try {
			while(!lines.empty()) {
				auto line=std::move(lines.back());
				lines.pop_back();
				if(line.key.compare("map-global")==0) {
					// if(empty)
					//   disable;
					auto r=parseValue<std::size_t>(line.val, map_g_);
					if(!r)
						gapr::report("line ", line.lineno, ": failed to parse ...");
				} else if(line.key.compare("map-global")==0) {
				} else {
					gapr::report("line ", line.lineno, ": unknown key");
				}
			}

			if(map_g_<_cube_infos.size() && !_cube_infos[map_g_].is_pattern)
				map_g=map_g_;
			if(map_c_<_cube_infos.size() && _cube_infos[map_c_].is_pattern)
				map_c=map_c_;
		} catch(const std::runtime_error& e) {
			; // XXX report
		}
		// cur_pos zoom_global zoom_closeup ;
		// cur_nid -> _init_nid=...;
		// right up -> viewer->set_dir();
		// view_mode -> set view_mode;
		// colors -> set_colors
		// channel_xfunc -> set xfunc

		// enable global view
		if(map_c!=SIZE_MAX) {
			// enable two other modes
		}
		// viewer update
		//viewer->update();
		void ViewerColorOptions::config(ViewerPriv* _vp, Options* opt) const {
			int v;
			if(opt->getInt("viewer.color.mode", &v))
				_vp->colorMode=v;
			else
				_vp->colorMode=colorModeDef;

			for(int i=0; i<COLOR_NUM; i++) {
				auto key=QString{"viewer.color.%1"}.arg(i);
				auto vals=opt->getIntList(key);
				if(vals.size()<3) {
					_vp->colors[i]=colorsDef[i];
				} else {
					_vp->colors[i]=QColor{vals[0], vals[1], vals[2]};
				}
			}
		}
#endif
	}


	void startDownloadCube() {
		gapr::node_attr pt{_cur_pos.point};
		auto map_g=_global_ch;
		auto map_c=_closeup_ch;
		switch(_view_mode) {
			case ViewMode::Global:
				map_c=0;
				break;
			case ViewMode::Closeup:
				map_g=0;
				break;
			case ViewMode::Mixed:
				map_g=0;
				break;
		}
		unsigned int chg=0;
		if(map_g!=0) {
			if(!_global_cube) {
				_cube_builder->build(map_g, _cube_infos[map_g-1]);
				_loading_global=true;
				chg++;
			}
		}
		if(map_c!=0) {
			if(_cube_builder->build(map_c, _cube_infos[map_c-1], {pt.pos(0), pt.pos(1), pt.pos(2)}, false, _closeup_cube?&_closeup_offset:nullptr)) {
				_loading_closeup=true;
				chg++;
			}
		}
		if(chg>0)
			_state_man.change(_state1_loading);
	}

	void change_closeup_cube(gapr::cube c, std::array<unsigned int, 3> o, std::string&& u) {
		//when cube ready
		//showB
		_closeup_cube=std::move(c);
		_closeup_offset=o;
		_closeup_uri=std::move(u);
		_state_man.change(_state1_closeup_cube);
		_state_man2.change(_gl_state1_closeup_cube);
	}
	void change_global_cube(gapr::cube c, std::string&& u) {
		// when cube ready
		// showB
		_global_cube=std::move(c);
		_global_uri=std::move(u);
		_state_man.change(_state1_global_cube);
		_state_man2.change(_gl_state1_global_cube);
	}
	void cubeFinished(std::error_code ec, int progr) {
		if(ec)
			gapr::print("error load cube: ", ec.message());
		{
			auto cube=_cube_builder->get();
			while(cube.data) {
				gapr::print("cube_out: ", cube.chan);
				if(_global_ch==cube.chan) {
					change_global_cube(std::move(cube.data), std::move(cube.uri));
				} else if(_closeup_ch==cube.chan) {
					change_closeup_cube(std::move(cube.data), cube.offset, std::move(cube.uri));
				}
				cube=_cube_builder->get();
			}
		}
		if(progr==1001) {
			_loading_global=false;
			_loading_closeup=false;
			_state_man.change(_state1_loading);
		} else {
			this->ui_update_progress(progr);
		}
		_state_man.propagate();
		_cube_builder->async_wait([this](std::error_code ec, int progr) {
			return cubeFinished(ec, progr);
		});
	}
	bool _user_sel{true};

	void change_tgt_pos(const Position& pos) {
		_tgt_pos=pos;
		_state_man.change(_state1_tgt_pos);
	}
	void target_changed(const Position& pos, gapr::edge_model::view model) {
		bool chg=false;
		if(_tgt_pos.edge!=pos.edge)
			chg=true;
		if(pos.edge) {
			if(_tgt_pos.index!=pos.index)
				chg=true;
		} else {
			if(_tgt_pos.vertex!=pos.vertex)
				chg=true;
		}
		if(!(_tgt_pos.point==pos.point))
			chg=true;
		//gapr::print("tgt_chg: ", chg, ' ', pos.edge, ':', pos.index, ':');
		if(chg) {
			change_tgt_pos(pos);
			if(pos.valid()) {
				_nodes_sel.clear();
				_state_man.change(_state1_sel);
			}
			_tgt_anchor=model.to_anchor(pos);
			_tgt_fix=FixPos{};
			gapr::edge_model::vertex_id root{};
			if(_tgt_pos.edge) {
				auto& edg=model.edges().at(_tgt_pos.edge);
				if(edg.root) {
					root=edg.root;
				}
			} else if(_tgt_pos.vertex) {
				auto& vert=model.vertices().at(_tgt_pos.vertex);
				if(vert.root) {
					root=vert.root;
				}
			}
			if(root) {
				_user_sel=false;
				// XXX
				//ba::post(ui_executor(), [this,idx]() {
				this->ui_list_select(root);
				//});
				_user_sel=true;
			}
			//updateActions;
			_state_man.propagate();
		}
	}
	void pick_position(const Position& pick_pos) {
		auto pos=pick_pos;
		edge_model::reader graph{_model};
		if(!pos.edge && !pos.vertex) {
			target_changed(pos, graph);
			update_description(graph);
			return;
		}
		{
			if(pos.edge) {
				auto edge_id=pos.edge;
				auto pickPos=pos.index;
				auto& e=graph.edges().at(edge_id);
				// XXX disable between-node picking for now
				{
					auto r=pickPos%128;
					pickPos=pickPos/128*128+(r>64?128:0);
				}
				auto idx=pickPos/128;
				if(idx>e.points.size()-1)
					idx=e.points.size()-1;
				gapr::node_attr p{e.points[idx]};
				gapr::node_attr p0{e.points[0]};
				gapr::node_attr p1{e.points[e.points.size()-1]};
				do {
					if(idx==0)
						break;
					if(idx==e.points.size()-1)
						break;
					if(p.dist_to(p0)<p0.misc.r()) {
						pickPos=(idx=0)*128;
						break;
					}
					if(p.dist_to(p1)<p1.misc.r()) {
						pickPos=(idx=e.points.size()-1)*128;
						break;
					}
				} while(false);
				if(auto r=pickPos%128; r!=0) {
					gapr::node_attr p2{e.points[idx+1]};
					for(unsigned int i=0; i<3; i++) {
						auto x0=p.pos(i);
						auto x1=p2.pos(i);
						p.pos(i, (x0*(128-r)+x1*r)/128);
					}
					pos=Position{edge_id, pickPos, p.data()};
				} else {
					if(idx==0) {
						pos=Position{e.left, p0.data()};
					} else if(idx==e.points.size()-1) {
						pos=Position{e.right, p1.data()};
					} else {
						pos=Position{edge_id, pickPos, p.data()};
					}
				}
			} else if(pos.vertex) {
				auto it=graph.vertices().find(pos.vertex);
				if(it!=graph.vertices().end()) {
					pos=Position{pos.vertex, it->second.attr.data()};
				} else {
					auto pos2=graph.nodes().at(pos.vertex);
					assert(pos2.edge);
					auto& edg=graph.edges().at(pos2.edge);
					pos=Position{pos2.edge, pos2.index, edg.points[pos2.index/128]};
				}
			}
		}
		target_changed(pos, graph);
		update_description(graph);
	}
#if 0
	void pickPosition(Edge edg, size_t idx, const Point* pos) {
		gapr::print("pick: ", pos.edge, ':', pos.index, ':', pos.point[0]);
		bool chg=false;
		if(tgtPos.edge!=edg) {
			tgtPos.edge=edg;
			chg=true;
		}
		const Point* p=nullptr;
		if(edg) {
			if(tgtPos.index!=idx) {
				tgtPos.index=idx;
				chg=true;
			}
			p=&edg.points()[idx];
		} else {
			p=pos;
		}
		if(p) {
			if(!(tgtPos.point==*p)) {
				tgtPos.point=*p;
				chg=true;
			}
		}
		if(chg) {
			////viewer->updatePosition(tgtPos, false);
			if(tgtPos.point.valid() && tgtPos.edge && tgtPos.edge.tree()) {
				curRow=tgtPos.edge.tree().index();
				auto idx=model->index(curRow, 0, {});
				listView->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::Clear|QItemSelectionModel::SelectCurrent|QItemSelectionModel::Rows);
			}
		}
	}
#endif

	void change_cur_pos(const Position& pos) {
		_cur_pos=pos;
		_state_man.change(_state1_cur_pos);
		canvas_set_current();
	}

	// XXX do this when graph changed(edge changed): priv->path.path.clear();
	// change current edge binding
	// change target edge binding
	void jumpToPosition(const Position& pos, gapr::edge_model::view model) {
		change_cur_pos(pos);
		canvas_clear_path(); // path is valid only when it's in current cube

		_cur_anchor=model.to_anchor(_cur_pos);
		_cur_fix=FixPos{};

		_state_man.propagate();
	}

	void get_passwd(const std::string& err) const {
		gapr::print("ask_passwd");
		gapr::str_glue msg{args().user, '@', args().host, ':', args().port};
		this->post_ask_password(msg.str(), std::string{err});
	}
	void post_ask_password(std::string&& msg, std::string&& err) const {
		ba::post(this->ui_executor(), [this,
				err=std::move(err),msg=std::move(msg)]() {
					this->ui_ask_password(err, msg);
				});
	}
	void post_critical_error(std::string&& err, std::string&& info, std::string&& detail) const {
		assert(!this->ui_executor().running_in_this_thread());
		ba::post(this->ui_executor(), [this,
				err=std::move(err),
				info=std::move(info),
				detail=std::move(detail)
		]() {
			this->ui_critical_error(err, info, detail);
		});
	}
	void post_message(std::string&& msg) const {
		assert(!this->ui_executor().running_in_this_thread());
		ba::post(this->ui_executor(), [this,msg=std::move(msg)]() {
			this->ui_message(msg);
		});
	}
	void post_ask_retry(std::string&& err, std::string&& info, std::string&& detail) const {
		ba::post(this->ui_executor(), [this,
				err=std::move(err),
				info=std::move(info),
				detail=std::move(detail)
		]() {
			this->ui_ask_retry(err, info, detail);
		});
	}

	void start() {
		bs::error_code ec;
		auto addr=ba::ip::make_address(args().host, ec);
		if(ec)
			return resolve();
		gapr::print("resolve");
		_addr.address(addr);
		_addr.port(args().port);
		if(args().passwd.empty())
			get_passwd({});
		else
			connect();
	}

	void resolve() {
		//*skip,*reuse
		if(!_resolver)
			_resolver.emplace(this->io_context());
		_resolver->async_resolve(args().host, "0", [this](bs::error_code ec, const resolver::results_type& res) {
			if(ec) {
				gapr::str_glue err{"Unable to look up `", args().host, "'."};
				return post_critical_error(err.str(), ec.message(), {});
			}
			assert(!res.empty());
			_addrs=res;
			if(args().passwd.empty())
				get_passwd({});
			else
				connect();
		});
	}



	void range_connect(resolver::results_type::const_iterator it, client_end&& conn) {
		resolver::endpoint_type addr{it->endpoint().address(), args().port};
		gapr::print("range_connect: ", addr);
		conn.async_connect(addr, [this,conn,addr,it](bs::error_code ec) mutable {
			if(ec) {
				gapr::str_glue msg{"Unable to connect to [", addr, "]: ", ec.message()};
				post_message(msg.str());
				++it;
				if(it!=_addrs.end())
					return range_connect(it, std::move(conn));
				gapr::str_glue err{"Unable to connect to `", args().host, ':', args().port, "'."};
				post_ask_retry(err.str(), "Tried each resolved address.", {});
				return;
			}
			_addr.address(it->endpoint().address());
			_addr.port(args().port);
			handshake(std::move(conn));
		});
	}

	void handshake(client_end&& conn) {
		auto fut=api.handshake(conn);
		auto ex=this->io_context().get_executor();
		std::move(fut).async_wait(ex, [this,conn=std::move(conn)](auto&& res) mutable {
			if(!res) try {
				auto ec=res.error_code();
				return post_ask_retry("Unable to handshake.", ec.message(), {});
			} catch(const std::runtime_error& e) {
				return post_critical_error("Unable to handshake.", e.what(), {});
			}
			_srv_info=std::move(res.get().banner);
			return login(std::move(conn));
		});
	}

	void login(client_end&& msg) {
		auto fut=api.login(msg, args().user, args().passwd);
		auto ex=this->io_context().get_executor();
		std::move(fut).async_wait(ex, [this,msg=std::move(msg)](auto&& res) mutable {
			if(!res) try {
				auto ec=res.error_code();
				return post_ask_retry("Login error: network failure.", ec.message(), {});
			} catch(const std::runtime_error& e) {
				return post_critical_error("Login error.", e.what(), {});
			}
			if(res.get().tier==gapr::tier::locked)
				return post_critical_error("Login error.", "User locked.", {});
			if(res.get().tier>gapr::tier::locked) {
				_conn_need_pw=std::move(msg);
				return get_passwd(res.get().gecos);
			}
			_tier=res.get().tier;
			_gecos=std::move(res.get().gecos);
			return select(std::move(msg));
		});
	}

	void select(client_end&& msg) {
		auto fut=api.select(msg, args().group);
		auto ex=this->io_context().get_executor();
		std::move(fut).async_wait(ex, [this,msg=std::move(msg)](auto&& res) mutable {
			if(!res) try {
				auto ec=res.error_code();
				return post_ask_retry("Select error: network failure.", ec.message(), {});
			} catch(const std::runtime_error& e) {
				return post_critical_error("Select error.", e.what(), {});
			}
			_latest_commit=res.get().commit_num;
			_data_secret=res.get().secret;
			_repo_stage=res.get().stage;
			_tier2=res.get().tier2;
			if(!_initialized)
				return get_catalog(std::move(msg));
			_cur_conn=std::move(msg);
			ba::post(this->ui_executor(), [this,file=std::move(res.get())]() mutable {
				return enter_stage3();
			});
		});
	}
	void change_stage(SessionStage stg) {
		_stage=stg;
		_state_man.change(_state1_stage);
	}
	void enter_stage3() {
		if(_tier2>gapr::tier::annotator)
			return this->ui_critical_error("Permission error.", "Using Gapr Fix not allowed.", {});
		/*! catalog and state loaded */
		postConstruct();
		/////////////////////////////////////////////
#if 0
		do {
			auto fut=_priv->start_load_cube();
			if(!fut)
				break;
			auto ex2=gapr::app().get_executor();
			std::move(fut).async_wait(ex2, [priv=_priv](gapr::likely<gapr::delta_add_edge_>&& res) {
				priv->finish_load_cube(std::move(res));
				auto fut=_priv->start_load_cube();
				if(!fut)
					break;
			});
		} while(false);
		//start_xxx();
		if(!fut)
			return;
		fut.wait(...);
		//
		//////////////
		//////


		_state_man.propagate();
		////////////////////////
		//
		//graph
		//

		/////////////
#endif
		auto fut=load_latest_commits();
		if(!fut)
			return;
		auto ex2=this->ui_executor();
		std::move(fut).async_wait(ex2, [this](gapr::likely<bool>&& res) {
			latest_commits_loaded(std::move(res));
		});
		gapr::str_glue addr{_addr};
		gapr::str_glue user{args().user, " (", _gecos, ')'};
		this->show_login_info(addr.str(), user.str(), to_string(_tier),
				to_string(_repo_stage), to_string(_tier2));
		change_stage(SessionStage::Opened);
		_state_man.propagate();
	}

	//XXX WindowModified: operation not finished
	void get_catalog(client_end&& msg) {
		auto fut=api.get_catalog(msg);
		auto ex=this->io_context().get_executor();
		std::move(fut).async_wait(ex, [this,msg=std::move(msg)](auto&& res) mutable {
			if(!res) try {
				auto ec=res.error_code();
				return post_ask_retry("Unable to load catalog: network failure.", ec.message(), {});
			} catch(const std::runtime_error& e) {
				return post_critical_error("Unable to load catalog.", e.what(), {});
			}
			load_catalog(std::move(res.get().file));
			return get_state(std::move(msg));
		});
	}

	void load_catalog(gapr::mem_file&& file) {
		auto f=gapr::make_streambuf(std::move(file));
		std::istream str{f.get()};
		// XXX mesh in viewer
		std::vector<gapr::mesh_info> mesh_infos;
		auto base_url="https://"+args().host+":"+std::to_string(args().port)+"/api/data/"+args().group+"/";
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

	void get_state(client_end&& msg) {

		// XXX
		//srand(static_cast<unsigned int>(time(nullptr)));
		//test_commit(client_end::msg{msg}.recycle().alloc(), 0);


		auto fut=api.get_state(msg);
		auto ex=this->io_context().get_executor();
		std::move(fut).async_wait(ex, [this,msg=std::move(msg)](auto&& res) {
			if(!res) try {
				auto ec=res.error_code();
				return post_ask_retry("Unable to load state: network failure.", ec.message(), {});
			} catch(const std::runtime_error& e) {
				return post_critical_error("Unable to load state.", e.what(), {});
			}
			_cur_conn=std::move(msg);
			_initialized=true;

#if 0
			if(file) {
				auto f=gapr::make_streambuf(std::move(file));
				std::istream str{f.get()};
				state_section=load_section(str, "fix"); //check ini format, and return section lines
			}
#endif
			ba::post(this->ui_executor(), [this,file=std::move(res.get().file)]() mutable {
				enter_stage3();
			});
		});

	}

	template<gapr::delta_type Typ>
		bool model_prepare(gapr::node_id nid0, gapr::delta<Typ>&& delta) {
			auto ex1=this->thread_pool().get_executor();
			assert(ex1.running_in_this_thread());

			edge_model::loader loader{_model};
			return loader.load<true>(nid0, std::move(delta));
		}

	void update_description(gapr::edge_model::view model) {
		std::ostringstream oss;
		if(_cur_pos.valid()) {
			make_brief(oss, _cur_pos, model);
		}
		if(_tgt_pos.valid()) {
			oss<<"\ndist: ";
			gapr::node_attr pos0{_cur_pos.point};
			gapr::node_attr pos1{_tgt_pos.point};
			oss<<pos0.dist_to(pos1)<<'\n';
			make_detail(oss, _tgt_pos, model);
		}
		this->ui_set_description(oss.str());
	}
	void update_model_stats(gapr::edge_model::view model) {
		std::ostringstream oss;
		oss<<"Number of commits: "<<_hist.body_count()<<'/'<<_hist.tail_tip();
		oss<<"\nNumber of nodes: "<<model.nodes().size();
		oss<<"\nNumber of vertices: "<<model.vertices().size();
		oss<<"\nNumber of edges: "<<model.edges().size();
		oss<<"\nNumber of annotations: "<<model.props().size()<<'\n';
		this->ui_set_statistics(oss.str());
	}

	Position fix_pos(const Position& pos, anchor_id anchor, gapr::edge_model::view model) {
		if(anchor.link) {
			if(anchor.link.on_node()) {
				auto p=model.to_position(anchor.link.nodes[0]);
				if(p.edge) {
					auto e=model.edges().at(p.edge);
					return Position{p.edge, p.index, e.points[p.index/128]};
				} else {
					if(p.vertex) {
						auto v=model.vertices().at(p.vertex);
						return Position{p.vertex, v.attr.data()};
					} else {
						return Position{pos.point};
					}
				}
			} else {
				// XXX rebind link
				return pos;
			}
		} else {
			return pos;
		}
	}
	Position fix_pos(const Position& pos, FixPos fix, gapr::edge_model::view model, gapr::node_id nid0) {
		gapr::node_id nid;
		switch(fix.state) {
			case FixPos::Empty:
				return pos;
			case FixPos::Future:
				nid=nid0.offset(fix.offset);
				break;
			case FixPos::Null:
				break;
			case FixPos::Node:
				nid=fix.node;
				break;
		}
		if(!nid)
			return {};

		auto p=model.to_position(nid);
		if(p.edge) {
			auto e=model.edges().at(p.edge);
			return Position{p.edge, p.index, e.points[p.index/128]};
		} else {
			if(p.vertex) {
				auto v=model.vertices().at(p.vertex);
				return Position{p.vertex, v.attr.data()};
			} else {
				return Position{pos.point};
			}
		}
	}

	auto get_state_cachepath() const {
		auto tag_str=gapr::str_glue{nullptr, "\x01"sv}("model_cache2", args().host, args().port, args().group).str();
		return gapr::get_cachepath(tag_str);
	}
	bool model_apply(bool last) {
		auto ex2=this->ui_executor();
		assert(ex2.running_in_this_thread());

		edge_model::updater updater{_model};
		if(!updater.empty()) {
			if(!updater.apply())
				return false;

			canvas_model_changed(updater.edges_del());

			this->ui_list_update(updater);

			updater.nid0();
			//this{model}
			if(!last || !_cur_fix)
				jumpToPosition(fix_pos(_cur_pos, _cur_anchor, updater), updater);
			if(!last || !_tgt_fix)
				target_changed(fix_pos(_tgt_pos, _tgt_anchor, updater), updater);

			//upgrade cur/tgt;
		}
		if(last) {
			auto nid0=updater.nid0();
			if(_cur_fix && nid0) {
				jumpToPosition(fix_pos(_cur_pos, _cur_fix, updater, nid0), updater);
				startDownloadCube();
			}
			if(_tgt_fix && nid0)
				target_changed(fix_pos(_tgt_pos, _tgt_fix, updater, nid0), updater);
			_cur_anchor=updater.to_anchor(_cur_pos);
			_tgt_anchor=updater.to_anchor(_tgt_pos);
			_cur_fix=FixPos{};
			_tgt_fix=FixPos{};
			update_description(updater);
			update_model_stats(updater);
		}

		std::size_t nocache_siz;
		if(last && (nocache_siz=_model.nocache_inc(0))>16*1024*1024) {
			assert(_hist.tail_tip()==0);
			auto state_cnt=_hist.body_count();
			auto ex1=gapr::app().thread_pool().get_executor();
			gapr::print("state_cache cache overflow: ", nocache_siz, " dumping...");
			ba::post(ex1, [this,state_cnt,nocache_siz]() mutable {
				gapr::edge_model::reader reader{_model};
				auto file=reader.dump_state(state_cnt);
				auto buf=gapr::make_streambuf(std::move(file));
				// ZZZ
				if(true) {
					gapr::edge_model model2;
					{
						edge_model::loader loader2{model2};
						auto r=loader2.init(*buf);
						assert(r);
						buf->pubseekpos(0);
					}
					{
						edge_model::updater updater2{model2};
						updater2.apply();
					}
					{
						edge_model::reader reader2{model2};
						auto r=reader2.equal(reader);
						assert(r);
					}
				}
				auto cachepath=get_state_cachepath();
				save_cache_file(cachepath, *buf);
				_model.nocache_dec(nocache_siz);
			});
		}
		return true;
	}
	bool load_commits(gapr::fiber_ctx& ctx, client_end msg, uint64_t upto) {
		//ensure conn
		assert(!_prelock_model.can_read_async());
		if(_hist.body_count()>=upto)
			return true;
		gapr::timer<4> timer;
		gapr::mem_file commits_file;
		{
			gapr::promise<gapr::mem_file> prom{};
			gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
			auto ex1=this->thread_pool().get_executor();
			ba::post(ex1, [hist=_hist,prom=std::move(prom)]() mutable {
				gapr::mem_file hist_data;
				try {
					hist_data=serialize(hist);
					std::move(prom).set(std::move(hist_data));
				} catch(const std::runtime_error& e) {
					return unlikely(std::move(prom), std::current_exception());
				}
			});
			auto hist_data=std::move(fib2).async_wait(gapr::yield{ctx});
			timer.mark<0>();
			gapr::fiber fib{ctx.get_executor(), api.get_commits(msg, std::move(hist_data), upto)};
			auto cmts=std::move(fib).async_wait(gapr::yield{ctx});
			commits_file=std::move(cmts.file);
			timer.mark<1>();
		}
		std::size_t total_size=commits_file.size();
		auto strmbuf=gapr::make_streambuf(std::move(commits_file));
		unsigned int cur_percent=0;
		unsigned int inc_percent=5;
		if(total_size<1*1024*1024)
			inc_percent=100;
		else if(total_size<10*1024*1024)
			inc_percent=25;

		while(_hist.body_count()<upto) {
			gapr::promise<uint64_t> prom{};
			gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
			auto ex1=this->thread_pool().get_executor();
			ba::post(ex1, [this,prom=std::move(prom),strmbuf=strmbuf.get()]() mutable {
				gapr::commit_info info;
				if(!info.load(*strmbuf))
					gapr::report("commit file no commit info");
				auto ex1=this->thread_pool().get_executor();
				assert(ex1.running_in_this_thread());
				(void)ex1;
				gapr::edge_model::loader loader{_model};
				auto r=gapr::delta_variant::visit<bool>(gapr::delta_type{info.type},
						[&loader,&info,strmbuf](auto typ) {
							gapr::delta<typ> delta;
							if(!gapr::load(delta, *strmbuf))
								gapr::report("commit file no delta");
							if(!loader.load(gapr::node_id{info.nid0}, std::move(delta)))
								return false;
							return true;
						});
				if(!r)
					return std::move(prom).set(std::numeric_limits<uint64_t>::max());
				return std::move(prom).set(info.id);
			});

			auto next_id=std::move(fib2).async_wait(gapr::yield{ctx});
			if(next_id!=_hist.body_count())
				return false;
			timer.mark<2>();
			_hist.body_count(next_id+1);

			auto pct=strmbuf->pubseekoff(0, std::ios::cur, std::ios::in)*100.0/total_size;
			if(pct>=cur_percent+inc_percent) {
				do {
					cur_percent+=inc_percent;
				} while(pct>=cur_percent+inc_percent);

				gapr::promise<bool> prom{};
				gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
				auto ex2=this->ui_executor();
				ba::post(ex2, [this,prom=std::move(prom)]() mutable {
					std::move(prom).set(model_apply(false));
				});
				if(!std::move(fib2).async_wait(gapr::yield{ctx}))
					return false;
			}
			timer.mark<3>();
		}
		_model.nocache_inc(total_size);
		gapr::print("load commits timming: ", timer);
		return true;
	}
	bool load_model_state(gapr::fiber_ctx& ctx, client_end msg) {
		//ensure conn
		assert(!_prelock_model.can_read_async());
		gapr::timer<3> timer;
		timer.mark<0>();

		auto cachepath=get_state_cachepath();
		bool use_existing{false};
		std::unique_ptr<std::streambuf> cachebuf;
		if(std::filesystem::is_regular_file(cachepath)) {
			auto sb=std::make_unique<std::filebuf>();
			if(sb->open(cachepath, std::ios::in|std::ios::binary)) {
				gapr::print("state_cache using cached state... ", cachepath);
				use_existing=true;
				cachebuf=std::move(sb);
			}
		}
		if(!use_existing) {
			gapr::fiber fib{ctx.get_executor(), api.get_model(msg)};
			auto cmt=std::move(fib).async_wait(gapr::yield{ctx});
			if(!cmt.file)
				return true;
			gapr::print("state_cache using downloaded state...");
			cachebuf=gapr::make_streambuf(std::move(cmt.file));
		}
		timer.mark<1>();

		gapr::promise<uint64_t> prom{};
		gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
		auto ex1=this->thread_pool().get_executor();
		ba::post(ex1, [this,use_existing,prom=std::move(prom),buf=std::move(cachebuf),cachepath=std::move(cachepath)]() mutable {
			auto ex1=this->thread_pool().get_executor();
			assert(ex1.running_in_this_thread());
			edge_model::loader loader{_model};
			auto r=loader.init(*buf);
			std::move(prom).set(r);
			if(r && !use_existing && buf->pubseekoff(0, std::ios::cur, std::ios::in)>16*1024*1024) {
				buf->pubseekpos(0);
				save_cache_file(cachepath, *buf);
 			}
		});
		auto r=std::move(fib2).async_wait(gapr::yield{ctx});
		if(!r)
			return false;
		timer.mark<2>();
		_hist.body_count(r);
		gapr::print("load model_state timming: ", timer);
		return true;
	}
	bool do_load_latest(gapr::fiber_ctx& ctx) {
		if(_latest_commit>100 && _hist.body_count()==0 && _hist.tail().empty()) {
			if(!load_model_state(ctx, _cur_conn))
				return false;
		}
		return load_commits(ctx, _cur_conn, _latest_commit);
	}
	gapr::future<bool> load_latest_commits() {
		// cur_conn
		if(!_prelock_model.can_write_later())
			return {};

		_prelock_model.begin_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();

		gapr::fiber fib{this->io_context().get_executor(), [this](gapr::fiber_ctx& ctx) {
			return do_load_latest(ctx);
		}};
		return fib.get_future();
	}
	void latest_commits_loaded(gapr::likely<bool>&& res) {
		if(!res)
			this->critical_error("Error", "load commit error", "");
		if(!res.get())
			this->critical_error("Error", "load commit error2", "");
		if(!model_apply(true))
			this->critical_error("Error", "model apply error", "");
		_prelock_model.end_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();
	}

	enum class SubmitRes {
		Deny,
		Accept,
		Retry
	};
	template<gapr::delta_type Typ>
		std::pair<SubmitRes, std::string> submit_commit(gapr::fiber_ctx& ctx, gapr::delta<Typ>&& delta) {
			gapr::promise<gapr::mem_file> prom{};
			gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
			auto ex1=this->thread_pool().get_executor();
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
			auto payload_size=payload.size();

			auto msg=_cur_conn;
			gapr::fiber fib{ctx.get_executor(), api.commit(msg, Typ, std::move(payload), _hist.body_count(), _hist.tail_tip())};
			auto res=std::move(fib).async_wait(gapr::yield{ctx});
#if 0
			if(err)
				return {SubmitRes::Deny, "msg"};
#endif
			auto [nid0, cmt_id, upd]=res;
			gapr::print("commit res: ", nid0, cmt_id, upd);
			auto r=load_commits(ctx, _cur_conn, upd);
			if(!r)
				return {SubmitRes::Deny, "err load"};
			if(!nid0) {
				if(cmt_id) {
					return {SubmitRes::Retry, {}};
				} else {
					return {SubmitRes::Deny, {}};
				}
			}
			gapr::promise<bool> prom2{};
			gapr::fiber fib3{ctx.get_executor(), prom2.get_future()};
			auto ex2=this->thread_pool().get_executor();
			ba::post(ex2, [this,nid0=nid0,prom2=std::move(prom2),&delta]() mutable {
				auto r=model_prepare(gapr::node_id{nid0}, std::move(delta));
				return std::move(prom2).set(r);
			});
			// XXX join edges
			if(!std::move(fib3).async_wait(gapr::yield{ctx}))
				return {SubmitRes::Deny, "err load2"};
			gapr::print("prepare ok");
			_model.nocache_inc(payload_size);
			_hist.add_tail(cmt_id);
			return {SubmitRes::Accept, {}};
		}

	gapr::future<std::pair<SubmitRes, std::string>> start_extend() {
		if(!_state_man.get(_state2_can_extend))
			return {};
#if 0
		if(priv->path.edge0 && priv->path.edge1) {
			Vertex v0{};
			if(priv->path.index0==0) {
				v0=priv->path.edge0.leftVertex();
			} else if(priv->path.index0==priv->path.edge0.points().size()-1) {
				v0=priv->path.edge0.rightVertex();
			}
			Vertex v1{};
			if(priv->path.index1==0) {
				v1=priv->path.edge1.leftVertex();
			} else if(priv->path.index1==priv->path.edge1.points().size()-1) {
				v1=priv->path.edge1.rightVertex();
			}
			if(v0 && v1 && v0==v1) {
				showWarning("Extend branch", "Loops not allowed", this);
				return;
			} else if(!v0 && !v1 && priv->path.edge0==priv->path.edge1
					&& priv->path.index0==priv->path.index1) {
				showWarning("Extend branch", "Loops not allowed", this);
				return;
			}
		}
#endif
		_prelock_path.begin_write_later();
		_prelock_model.begin_write_later();
		_state_man.change({_state1_model_lock, _state1_path_lock});
		_state_man.propagate();
		{
			gapr::link_id left{_path.left};
			unsigned int off=_path.nodes.size()-2;
			if(!left || !left.on_node())
				off+=1;
			gapr::link_id right{_path.right};
			if(!right || !right.on_node())
				_cur_fix=FixPos{off};
			else
				_cur_fix=FixPos{right.nodes[0]};
			_tgt_fix=FixPos{nullptr};
		}

		gapr::fiber fib{this->io_context().get_executor(), [this](gapr::fiber_ctx& ctx) {
			return submit_commit(ctx, gapr::delta_add_edge_{_path});
		}};
		return fib.get_future();
	}
	gapr::future<std::pair<SubmitRes, std::string>> start_branch() {
		if(!_state_man.get(_state2_can_extend))
			return {};
#if 0
		if(priv->path.edge0 && priv->path.edge1) {
			Vertex v0{};
			if(priv->path.index0==0) {
				v0=priv->path.edge0.leftVertex();
			} else if(priv->path.index0==priv->path.edge0.points().size()-1) {
				v0=priv->path.edge0.rightVertex();
			}
			Vertex v1{};
			if(priv->path.index1==0) {
				v1=priv->path.edge1.leftVertex();
			} else if(priv->path.index1==priv->path.edge1.points().size()-1) {
				v1=priv->path.edge1.rightVertex();
			}
			if(v0 && v1 && v0==v1) {
				showWarning("Create branch", "Loops not allowed", this);
				return;
			} else if(!v0 && !v1 && priv->path.edge0==priv->path.edge1
					&& priv->path.index0==priv->path.index1) {
				showWarning("Create branch", "Loops not allowed", this);
				return;
			}
		}
#endif
		_prelock_path.begin_write_later();
		_prelock_model.begin_write_later();
		_state_man.change({_state1_model_lock, _state1_path_lock});
		_state_man.propagate();
		{
			gapr::link_id left{_path.left};
			if(!left || !left.on_node()) {
				_cur_fix=FixPos{0u};
			} else {
				_cur_fix=FixPos{left.nodes[0]};
			}
			_tgt_fix=FixPos{nullptr};
		}

		gapr::fiber fib{this->io_context().get_executor(), [this](gapr::fiber_ctx& ctx) {
			return submit_commit(ctx, gapr::delta_add_edge_{_path});
		}};
		return fib.get_future();
	}
	void finish_extend(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			this->critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			this->critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				canvas_clear_path();
				if(!res.get().second.empty())
					this->critical_error("Error", res.get().second, "");
				break;
			case SubmitRes::Accept:
				canvas_clear_path();
				break;
			case SubmitRes::Retry:
#if 0
				if(can_reuse) {
					fix;
				}
#endif
				canvas_clear_path();
				break;
		}
		_prelock_path.end_write_later();
		_prelock_model.end_write_later();
		_state_man.change({_state1_model_lock, _state1_path_lock});
		_state_man.propagate();
	}

	gapr::future<std::pair<SubmitRes, std::string>> start_attach(int type) {
		if(!_state_man.get(_state2_can_attach))
			return {};

		_prelock_path.begin_write_later();
		_prelock_model.begin_write_later();
		_state_man.change({_state1_model_lock, _state1_path_lock});
		_state_man.propagate();

		gapr::delta_add_patch_ delta;
		{
			gapr::link_id left{_path.left};
			unsigned int off=_path.nodes.size()-2;
			if(!left || !left.on_node()) {
				_cur_fix=FixPos{0u};
				off+=1;
			} else {
				_cur_fix=FixPos{left.nodes[0]};
			}
			gapr::link_id right{_path.right};
			if(!right || !right.on_node())
				_tgt_fix=FixPos{off};
			else
				_tgt_fix=FixPos{right.nodes[0]};

			if(left)
				delta.links.emplace_back(1, left.data());
			unsigned int i;
			for(i=0; i<_path.nodes.size(); i++) {
				gapr::node_attr n{_path.nodes[i]};
				n.misc.t(type);
				delta.nodes.emplace_back(n.data(), i);
			}
			std::ostringstream oss;
			oss<<"root=attach"<<right;
			delta.props.emplace_back(i, oss.str());
		}

		gapr::fiber fib{this->io_context().get_executor(), [this,delta=std::move(delta)](gapr::fiber_ctx& ctx) mutable {
			return submit_commit(ctx, std::move(delta));
		}};
		return fib.get_future();
	}
	void finish_attach(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			this->critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			this->critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				canvas_clear_path();
				if(!res.get().second.empty())
					this->critical_error("Error", res.get().second, "");
				break;
			case SubmitRes::Accept:
				canvas_clear_path();
				break;
			case SubmitRes::Retry:
#if 0
				if(can_reuse) {
					fix;
				}
#endif
				canvas_clear_path();
				break;
		}
		_prelock_path.end_write_later();
		_prelock_model.end_write_later();
		_state_man.change({_state1_model_lock, _state1_path_lock});
		_state_man.propagate();
	}


	bool do_refresh(gapr::fiber_ctx& ctx) {
		auto msg=_cur_conn;
		gapr::fiber fib{ctx.get_executor(), api.select(msg, args().group)};
		auto upd=std::move(fib).async_wait(gapr::yield{ctx});
#if 0
		if(err)
			return {SubmitRes::Deny, "msg"};
#endif

		return load_commits(ctx, _cur_conn, upd.commit_num);
	}
	gapr::future<bool> start_refresh() {
		if(!_state_man.get(_state2_can_refresh))
			return {};
		_prelock_model.begin_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();

		gapr::fiber fib{this->io_context().get_executor(), [this](gapr::fiber_ctx& ctx) {
			return do_refresh(ctx);
		}};
		return fib.get_future();
	}
	void finish_refresh(gapr::likely<bool>&& res) {
		if(!res)
			this->critical_error("Error", "load commit error", "");
		if(!res.get())
			this->critical_error("Error", "load commit error2", "");
		if(!model_apply(true))
			this->critical_error("Error", "model apply error", "");
		_prelock_model.end_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();
	}

	void autosel_for_proofread(gapr::edge_model::view reader) {
		auto sel_nodes=[this](auto&& nodes_sel) {
			_nodes_sel=std::move(nodes_sel);
			_state_man.change(_state1_sel);
		};
		auto pick_tgt=[this,&reader](const auto& pos) {
			target_changed(pos, reader);
		};
		struct boundary_checker {
			gapr::affine_xform xform;
			std::array<unsigned int, 3> offset;
			std::array<unsigned int, 3> size;
			bool valid;
			boundary_checker(const gapr::cube_info& cubeinfo, const std::array<unsigned int, 3>& offset, const gapr::cube& cube):
				xform{cubeinfo.xform}, offset{offset}, size{0,0,0}, valid{false}
			{
				if(cube) {
					auto cube_view=cube.view<void>();
					size=cube_view.sizes();
					valid=true;
				}
			}
			bool operator()(const gapr::vec3<double>& pos) {
				if(!valid)
					return true;
				auto off=xform.to_offset_f(pos);
				for(unsigned int i=0; i<off.size(); ++i) {
					auto oi=off[i]-offset[i];
					if(oi<0)
						return false;
					if(oi>size[i])
						return false;
				}
				return true;
			}
		};
		boundary_checker chk_rng{_cube_infos[_closeup_ch-1], _closeup_offset, _closeup_cube};

		sel_proofread_nodes(reader, _cur_pos, pick_tgt, sel_nodes, chk_rng, _autosel_len);
	}

	gapr::future<std::pair<SubmitRes, std::string>> start_end_as(std::string&& val) {
		// XXX
		if(!_state_man.get(_state2_can_end))
			return {};

		gapr::node_id nid{};
		gapr::edge_model::reader reader{_model};
		do {
			if(_cur_pos.edge) {
				auto& edg=reader.edges().at(_cur_pos.edge);
				nid=edg.nodes[_cur_pos.index/128];
				break;
			}
			nid=_cur_pos.vertex;
			if(!nid)
				break;
			auto vert=&reader.vertices().at(nid);
			if(!pred_empty_end{}(reader, nid, *vert))
				break;

			_prelock_model.begin_write_later();
			_state_man.change(_state1_model_lock);
			_state_man.propagate();

			gapr::fiber fib{this->io_context().get_executor(), [this,nid,val=std::move(val)](gapr::fiber_ctx& ctx) {
				gapr::delta_add_prop_ delta;
				delta.link[0]=nid.data;
				delta.link[1]=0;
				delta.node=_cur_pos.point;
				delta.prop="state="+val;
				return submit_commit(ctx, std::move(delta));
			}};
			return fib.get_future();
		} while(false);

		if(auto nid_next=find_next_node(reader, nid, gapr::node_attr{_cur_pos.point})) {
			auto pos=reader.to_position(nid_next);
			if(pos.edge) {
				auto& e=reader.edges().at(pos.edge);
				jumpToPosition(Position{pos.edge, pos.index, e.points[pos.index/128]}, reader);
			} else {
				auto& v=reader.vertices().at(nid_next);
				jumpToPosition(Position{nid_next, v.attr.data()}, reader);
			}
			target_changed(Position{}, reader);
			autosel_for_proofread(reader);
			update_description(reader);
			startDownloadCube();
			_state_man.propagate();
		}
		return {};
	}

	void finish_terminal(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			this->critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			this->critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				if(!res.get().second.empty())
					this->critical_error("Error", res.get().second, "");
				break;
			case SubmitRes::Accept:
				break;
			case SubmitRes::Retry:
				break;
		}
		_prelock_model.end_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();
	}
	struct sub_edge {
		gapr::edge_model::edge_id edge;
		uint32_t index0, index1;
	};
	sub_edge get_sub_edge() {
		// XXX move to edge_model
		gapr::edge_model::reader reader{_model};
		edge_model::edge_id id{0};
		do {
			if(_cur_pos.edge) {
				if(_tgt_pos.edge) {
					if(_cur_pos.edge!=_tgt_pos.edge)
						break;
					id=_cur_pos.edge;
				} else if(_tgt_pos.vertex) {
					id=_cur_pos.edge;
				} else {
					break;
				}
			} else if(_cur_pos.vertex) {
				if(_tgt_pos.edge) {
					id=_tgt_pos.edge;
				} else if(_tgt_pos.vertex) {
					auto& vert1=reader.vertices().at(_cur_pos.vertex);
					auto& vert2=reader.vertices().at(_tgt_pos.vertex);
					unsigned int hits=0;
					for(auto [eid1, dir1]: vert1.edges) {
						for(auto [eid2, dir2]: vert2.edges) {
							if(eid1==eid2) {
								id=eid1;
								hits++;
							}
						}
					}
					if(hits!=1) {
						id=0;
						break;
					}
				} else {
					break;
				}
			} else {
				break;
			}
		} while(false);
		if(id==0)
			return sub_edge{0, 0, 0};
		uint32_t idx0{0}, idx1{0};
		auto& edg=reader.edges().at(id);
		if(_cur_pos.edge) {
			idx0=_cur_pos.index/128;
		} else if(_cur_pos.vertex) {
			if(_cur_pos.vertex==edg.left) {
				idx0=0;
			} else if(_cur_pos.vertex==edg.right) {
				idx0=edg.nodes.size()-1;
			} else {
				return sub_edge{0, 0, 0};
			}
		} else {
			assert(0);
		}
		if(_tgt_pos.edge) {
			idx1=_tgt_pos.index/128;
		} else if(_tgt_pos.vertex) {
			if(_tgt_pos.vertex==edg.left) {
				idx1=0;
			} else if(_tgt_pos.vertex==edg.right) {
				idx1=edg.nodes.size()-1;
			} else {
				return sub_edge{0, 0, 0};
			}
		} else {
			assert(0);
		}
		return sub_edge{id, idx0, idx1};
	}
	gapr::future<std::pair<SubmitRes, std::string>> start_delete() {
		//XXX delete vertex???
		//vv 0/not/delv 1dele
		//ev dele
		//ee 0/not/breake 1+/dele
		//e0 not/breake/dele
		//0v not/delv
		//0e not/breake/dele
		if(!_state_man.get(_state2_can_delete))
			return {};

		gapr::edge_model::reader reader{_model};
		auto sub_edge=get_sub_edge();
		std::vector<node_id::data_type> nodes;
		std::unordered_map<gapr::node_id, unsigned int> nodes_del;
		if(auto& sel=_nodes_sel; sel.empty()) {
			// XXX
			if(sub_edge.index0==sub_edge.index1)
				return {};
			bool rm_left{false}, rm_right{false};
			if(_cur_pos.edge) {
			} else if(_cur_pos.vertex) {
				auto& vert=reader.vertices().at(_cur_pos.vertex);
				if(vert.edges.size()<2)
					rm_left=true;
			} else {
			}
			if(_tgt_pos.edge) {
			} else if(_tgt_pos.vertex) {
				auto& vert=reader.vertices().at(_tgt_pos.vertex);
				if(vert.edges.size()<2)
					rm_right=true;
			} else {
			}
			if(rm_left)
				nodes.push_back(0);
			auto& edg=reader.edges().at(sub_edge.edge);
			gapr::print("del edge: ", sub_edge.edge, ':', sub_edge.index0, '.', sub_edge.index1);
			uint32_t idx=sub_edge.index0;
			do {
				nodes.push_back(edg.nodes[idx].data);
				if(idx==sub_edge.index1)
					break;
				if(sub_edge.index1>sub_edge.index0)
					idx++;
				else
					idx--;
			} while(true);
			if(rm_left)
				nodes[0]=nodes[1];
			if(rm_right) {
				auto last=nodes.back();
				nodes.push_back(last);
			}
			for(std::size_t i=1; i+1<nodes.size(); i++) {
				nodes_del.emplace(nodes[i], 0);
			}
		} else {
			nodes.clear();
			nodes_del.clear();
			for(auto& [n, attr]: sel) {
				auto [it,ins]=nodes_del.emplace(n, 1);
				if(!ins)
					continue;
				auto pos=reader.nodes().at(n);
				if(pos.edge) {
					it->second+=2;
				} else {
					auto& vert=reader.vertices().at(pos.vertex);
					it->second+=vert.edges.size();
				}
			}
			auto handle_edge=[&nodes_del,&nodes](auto& edg, unsigned int idx) ->bool {
				unsigned int left=idx;
				while(left>0) {
					auto n=edg.nodes[--left];
					auto it=nodes_del.find(n);
					if(it==nodes_del.end())
						break;
					if(it->second<=0) {
						++left;
						break;
					}
					it->second-=left>0?3:1;
				}
				unsigned int rgt=idx+1;
				while(rgt<edg.nodes.size()) {
					auto n=edg.nodes[rgt++];
					auto it=nodes_del.find(n);
					if(it==nodes_del.end())
						break;
					if(it->second<=0) {
						--rgt;
						break;
					}
					it->second-=rgt<edg.nodes.size()?3:1;
				}
				if(left+1>=rgt)
					return false;
				if(left==0) {
					auto it=nodes_del.find(edg.left);
					if(it!=nodes_del.end()) {
						assert(it->second>0);
						if(it->second==1) {
							assert(edg.left==edg.nodes[0]);
							it->second-=1;
							nodes.push_back(edg.left.data);
						}
					}
				}
				for(auto i=left; i<rgt; ++i) {
					nodes.push_back(edg.nodes[i].data);
				}
				bool closed{false};
				if(rgt==edg.nodes.size()) {
					auto it=nodes_del.find(edg.right);
					if(it!=nodes_del.end()) {
						assert(it->second>0);
						if(it->second==1) {
							assert(edg.right==edg.nodes[edg.nodes.size()-1]);
							it->second-=1;
							nodes.push_back(edg.right.data);
							closed=true;
						}
					}
				}
				if(!closed)
					nodes.push_back(gapr::node_id{}.data);
				return true;
			};
			for(auto& [n, attr]: sel) {
				auto it=nodes_del.find(n);
				if(it->second<=0)
					continue;
				auto pos=reader.nodes().at(n);
				if(pos.edge) {
					it->second-=3;
					handle_edge(reader.edges().at(pos.edge), pos.index/128);
				} else {
					auto& vert=reader.vertices().at(pos.vertex);
					if(vert.edges.empty()) {
						it->second-=1;
						nodes.push_back(n.data);
						nodes.push_back(gapr::node_id{}.data);
					} else {
						for(auto [eid, dir]: vert.edges) {
							it->second-=1;
							auto& edg=reader.edges().at(eid);
							if(!handle_edge(edg, dir?edg.nodes.size()-1:0))
								it->second+=1;
						}
					}
				}
			}
			assert(nodes.size()>0);
			if(nodes.back()==gapr::node_id{}.data)
				nodes.pop_back();
			assert(nodes.size()>0);
			for(auto [n, cnt]: nodes_del) {
				assert(cnt==0);
			}
		}
		std::vector<std::pair<node_id::data_type, std::string>> props;
		for(auto& [id, val]: reader.props()) {
			if(nodes_del.find(id.node)!=nodes_del.end()) {
				props.push_back(gapr::prop_id{id}.data());
			}
		}
		std::sort(props.begin(), props.end());

		_prelock_model.begin_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();

		gapr::fiber fib{this->io_context().get_executor(), [this,nodes=std::move(nodes),props=std::move(props)](gapr::fiber_ctx& ctx) {
			gapr::delta_del_patch_ delta;
			delta.props=std::move(props);
			delta.nodes=std::move(nodes);
			for(auto& p: delta.props)
				gapr::print("del prop: ", p.first, ':', p.second);
			for(auto& n: delta.nodes)
				gapr::print("del node: ", n);
			return submit_commit(ctx, std::move(delta));
		}};
		return fib.get_future();
	}
	void finish_delete(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			this->critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			this->critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				if(!res.get().second.empty())
					this->critical_error("Error", res.get().second, "");
				break;
			case SubmitRes::Accept:
				if(!_nodes_sel.empty()) {
					_nodes_sel.clear();
					_state_man.change(_state1_sel);
				}
				break;
			case SubmitRes::Retry:
				break;
		}
		_prelock_model.end_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();
	}
	gapr::future<std::pair<SubmitRes, std::string>> start_examine() {
		if(!_state_man.get(_state2_can_examine))
			return {};

		gapr::edge_model::reader reader{_model};
		auto sub_edge=get_sub_edge();
		std::vector<node_id::data_type> nodes;
		FixPos fix_pos{};
		if(sub_edge.edge) {
			auto& edg=reader.edges().at(sub_edge.edge);
			fix_pos=FixPos{edg.nodes[sub_edge.index1]};
			uint32_t idx=sub_edge.index0;
			do {
				gapr::node_attr pt{edg.points[idx]};
				if(!pt.misc.coverage())
					nodes.push_back(edg.nodes[idx].data);
				if(idx==sub_edge.index1)
					break;
				if(sub_edge.index1>sub_edge.index0)
					idx++;
				else
					idx--;
			} while(true);
		}
		if(auto& sel=_nodes_sel; !sel.empty()) {
			fix_pos=FixPos{};
			nodes.clear();
			for(auto& [n, attr]: sel) {
				if(!attr.misc.coverage())
					nodes.push_back(n.data);
			}
		}
		if(nodes.empty()) {
			if(fix_pos) {
				jumpToPosition(_tgt_pos, reader);
				target_changed(Position{}, reader);
			}
			_state_man.propagate();
			return {};
		}

		_prelock_model.begin_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();
		_cur_fix=fix_pos;
		_tgt_fix=FixPos{nullptr};
		gapr::fiber fib{gapr::app().io_context().get_executor(), [this,nodes=std::move(nodes)](gapr::fiber_ctx& ctx) {
			gapr::delta_proofread_ delta;
			delta.nodes=std::move(nodes);
			return submit_commit(ctx, std::move(delta));
		}};
		return fib.get_future();
	}
	void finish_examine(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			this->critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			this->critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				_path.nodes.clear();
				canvas_set_path_data({});
				if(!res.get().second.empty())
					this->critical_error("Error", res.get().second, "");
				break;
			case SubmitRes::Accept:
				_path.nodes.clear();
				canvas_set_path_data({});
				_nodes_sel.clear();
				_state_man.change(_state1_sel);
				_state_man.propagate();
				break;
			case SubmitRes::Retry:
#if 0
				if(can_reuse) {
					fix;
				}
#endif
				canvas_set_path_data({});
				break;
		}
		_prelock_model.end_write_later();
		_state_man.change(_state1_model_lock);
		gapr::edge_model::reader reader{_model};
		autosel_for_proofread(reader);
		_state_man.propagate();
	}


	gapr::future<std::pair<SubmitRes, std::string>> start_neuron_create(std::string&& name) {
		if(!_state_man.get(_state2_can_create_nrn))
			return {};

		_prelock_model.begin_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();

		gapr::node_id nid{};
		gapr::edge_model::reader reader{_model};
		if(_tgt_pos.edge) {
			auto& edg=reader.edges().at(_tgt_pos.edge);
			nid=edg.nodes[_tgt_pos.index/128];
		} else {
			nid=_tgt_pos.vertex;
		}
		//???list_ptr: point to created neuron
		gapr::fiber fib{this->io_context().get_executor(), [this,nid,name=std::move(name)](gapr::fiber_ctx& ctx) {
			gapr::delta_add_prop_ delta;
			delta.link[0]=nid.data;
			delta.link[1]=0;
			delta.node=_tgt_pos.point;
			delta.prop="root";
			if(!name.empty()) {
				delta.prop.push_back('=');
				delta.prop+=name;
			}
			return submit_commit(ctx, std::move(delta));
		}};
		return fib.get_future();
	}

	void finish_neuron_create(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			this->critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			this->critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				if(!res.get().second.empty())
					this->critical_error("Error", res.get().second, "");
				break;
			case SubmitRes::Accept:
				break;
			case SubmitRes::Retry:
				break;
		}
		_prelock_model.end_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();
	}


	gapr::future<std::pair<SubmitRes, std::string>> start_neuron_rename(std::string&& name) {
		if(!_state_man.get(_state2_can_rename_nrn))
			return {};

		_prelock_model.begin_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();

		auto nid=_list_sel;
		//???list_ptr: point to created neuron
		gapr::fiber fib{this->io_context().get_executor(), [this,nid,name=std::move(name)](gapr::fiber_ctx& ctx) {
			gapr::delta_chg_prop_ delta;
			delta.node=nid.data;
			delta.prop="root";
			if(!name.empty()) {
				delta.prop.push_back('=');
				delta.prop+=name;
			}
			return submit_commit(ctx, std::move(delta));
		}};
		return fib.get_future();
	}

	void finish_neuron_rename(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			this->critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			this->critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				if(!res.get().second.empty())
					this->critical_error("Error", res.get().second, "");
				break;
			case SubmitRes::Accept:
				break;
			case SubmitRes::Retry:
				break;
		}
		_prelock_model.end_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();
	}

	gapr::future<std::pair<SubmitRes, std::string>> start_resolve_error(std::string_view state) {
		if(!_state_man.get(_state2_can_resolve_err))
			return {};

		gapr::node_id nid{};
		gapr::edge_model::reader reader{_model};
		if(_tgt_pos.edge) {
			auto& edg=reader.edges().at(_tgt_pos.edge);
			nid=edg.nodes[_tgt_pos.index/128];
		} else {
			nid=_tgt_pos.vertex;
		}
		if(!nid)
			return {};
		if(auto it=reader.props().find({nid, "error"}); it!=reader.props().end()) {
			if(it->second==state)
				return {};
		} else {
			return {};
		}

		assert(nid);
		_prelock_model.begin_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();

		gapr::fiber fib{this->io_context().get_executor(), [this,nid,state](gapr::fiber_ctx& ctx) {
			gapr::delta_chg_prop_ delta;
			delta.node=nid.data;
			delta.prop="error";
			if(!state.empty()) {
				delta.prop.push_back('=');
				delta.prop+=state;
			}
			return submit_commit(ctx, std::move(delta));
		}};
		return fib.get_future();
	}

	void finish_resolve_error(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			this->critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			this->critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				if(!res.get().second.empty())
					this->critical_error("Error", res.get().second, "");
				break;
			case SubmitRes::Accept:
				break;
			case SubmitRes::Retry:
				break;
		}
		_prelock_model.end_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();
	}

	gapr::future<std::pair<SubmitRes, std::string>> start_report_error(std::string_view state) {
		if(!_state_man.get(_state2_can_report_err))
			return {};

		gapr::node_id nid{};
		gapr::edge_model::reader reader{_model};
		if(_tgt_pos.edge) {
			auto& edg=reader.edges().at(_tgt_pos.edge);
			nid=edg.nodes[_tgt_pos.index/128];
		} else {
			nid=_tgt_pos.vertex;
		}
		auto it_root=reader.props().find(gapr::prop_id{nid, "error"});
		if(it_root!=reader.props().end())
			return {};

		_prelock_model.begin_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();
		gapr::fiber fib{gapr::app().io_context().get_executor(), [this,nid,state](gapr::fiber_ctx& ctx) {
			gapr::delta_add_prop_ delta;
			delta.link[0]=nid.data;
			delta.link[1]=0;
			delta.node=_tgt_pos.point;
			delta.prop="error";
			if(!state.empty()) {
				delta.prop.push_back('=');
				delta.prop+=state;
			}
			return submit_commit(ctx, std::move(delta));
		}};
		return fib.get_future();
	}

	void finish_report_error(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			this->critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			this->critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				if(!res.get().second.empty())
					this->critical_error("Error", res.get().second, "");
				break;
			case SubmitRes::Accept:
				break;
			case SubmitRes::Retry:
				break;
		}
		_prelock_model.end_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();
	}

	gapr::future<std::pair<SubmitRes, std::string>> start_raise_node() {
		if(!_state_man.get(_state2_can_raise_node))
			return {};

		gapr::node_id nid{};
		gapr::edge_model::reader reader{_model};
		if(_tgt_pos.edge) {
			auto& edg=reader.edges().at(_tgt_pos.edge);
			nid=edg.nodes[_tgt_pos.index/128];
		} else {
			nid=_tgt_pos.vertex;
		}
		auto it_root=reader.props().find(gapr::prop_id{nid, "raise"});
		if(it_root!=reader.props().end())
			return {};

		_prelock_model.begin_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();
		gapr::fiber fib{gapr::app().io_context().get_executor(), [this,nid](gapr::fiber_ctx& ctx) {
			gapr::delta_add_prop_ delta;
			delta.link[0]=nid.data;
			delta.link[1]=0;
			delta.node=_tgt_pos.point;
			delta.prop="raise";
			return submit_commit(ctx, std::move(delta));
		}};
		return fib.get_future();
	}
	void finish_raise_node(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			this->critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			this->critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				if(!res.get().second.empty())
					this->critical_error("Error", res.get().second, "");
				break;
			case SubmitRes::Accept:
				break;
			case SubmitRes::Retry:
				break;
		}
		_prelock_model.end_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();
	}


	bool _in_highlight{false};
	void toggle_highlight(bool value) {
		_in_highlight=value;
		_state_man.change(_state1_hl_mode);
	}

	void init_opengl_impl() {
		Funcs::initialize();
		funcs_ok=true;

		// XXX
		viewerShared->initialize();

		//
		auto scaling=_scaling;
		fbo_width=(_wid_width+scaling)/(1+scaling);
		fbo_height=(_wid_height+scaling)/(1+scaling);

		fbo_width_alloc=(fbo_width+FBO_ALLOC)/FBO_ALIGN*FBO_ALIGN;
		fbo_height_alloc=(fbo_height+FBO_ALLOC)/FBO_ALIGN*FBO_ALIGN;
		fbo_opaque.create(fbo_width_alloc, fbo_height_alloc);
		fbo_surface.create(fbo_width_alloc, fbo_height_alloc);
		fbo_pick.create(128, 128);
		fbo_cubes.create(fbo_width_alloc, fbo_height_alloc);
		fbo_scale.create(fbo_width_alloc, fbo_height_alloc);

		auto bg_c=colors[C_BG];
		this->glClearColor(bg_c[0], bg_c[1], bg_c[2], 1.0);
		this->glClearDepth(1.0);
		this->glEnable(GL_DEPTH_TEST);
		this->glEnable(GL_BLEND);
		this->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

		pbiFixed=_vao_man.alloc();
		pbiPath=_vao_man.alloc();
		this->glBindBuffer(GL_ARRAY_BUFFER, _vao_man.buffer(pbiFixed));
		this->glBufferData(GL_ARRAY_BUFFER, (9*24+2+160)*sizeof(PointGL), nullptr, GL_STATIC_DRAW);

		_vert_vaoi=_vao_man.alloc();

		auto create_prog2=[this](Program& prog, const char* pvert, const char* pfrag, std::string_view defs) {
			gapr::gl::Shader<Funcs> vert, frag;
			vert.create(this->resources(), GL_VERTEX_SHADER, pvert, defs);
			frag.create(this->resources(), GL_FRAGMENT_SHADER, pfrag, defs);
			prog.create({vert, frag});
			if(this->glGetError()!=GL_NO_ERROR)
				throw std::runtime_error{"err link2"};
			vert.destroy();
			frag.destroy();
		};
		auto create_prog=[this](Program& prog, const char* pvert, const char* pgeom, const char* pfrag, std::string_view defs) {
			gapr::gl::Shader<Funcs> vert, geom, frag;
			vert.create(this->resources(), GL_VERTEX_SHADER, pvert, defs);
			geom.create(this->resources(), GL_GEOMETRY_SHADER, pgeom, defs);
			frag.create(this->resources(), GL_FRAGMENT_SHADER, pfrag, defs);
			prog.create({vert, geom, frag});
			if(this->glGetError()!=GL_NO_ERROR)
				throw std::runtime_error{"err link2"};
			vert.destroy();
			geom.destroy();
			frag.destroy();
		};
		create_prog(draw_edge.prog, "/glsl/edge.vert", "/glsl/edge.geom", "/glsl/edge.frag", {});
		draw_edge.center=draw_edge.prog.uniformLocation("pos_offset");
		draw_edge.color0=draw_edge.prog.uniformLocation("colors[0]");
		draw_edge.proj=draw_edge.prog.uniformLocation("mProj");
		draw_edge.thick=draw_edge.prog.uniformLocation("thickness");
		draw_edge.view=draw_edge.prog.uniformLocation("mView");
		if(this->glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc"};

		create_prog(draw_vert.prog, "/glsl/vert2.vert", "/glsl/vert2.geom", "/glsl/vert2.frag", {});
		draw_vert.center=draw_vert.prog.uniformLocation("pos_offset");
		draw_vert.color0=draw_vert.prog.uniformLocation("colors[0]");
		draw_vert.proj=draw_vert.prog.uniformLocation("mProj");
		draw_vert.thick=draw_vert.prog.uniformLocation("thickness");
		draw_vert.view=draw_vert.prog.uniformLocation("mView");
		if(this->glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc"};

		std::string_view pick_defs{"#define PICK_MODE"};
		create_prog(pick_edge.prog, "/glsl/edge.vert", "/glsl/edge.geom", "/glsl/edge.frag", pick_defs);
		pick_edge.center=pick_edge.prog.uniformLocation("pos_offset");
		pick_edge.edge_id=pick_edge.prog.uniformLocation("edge_id");
		pick_edge.proj=pick_edge.prog.uniformLocation("mProj");
		pick_edge.thick=pick_edge.prog.uniformLocation("thickness");
		pick_edge.view=pick_edge.prog.uniformLocation("mView");
		if(this->glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc"};

		create_prog(pick_vert.prog, "/glsl/vert2.vert", "/glsl/vert2.geom", "/glsl/vert2.frag", pick_defs);
		pick_vert.center=pick_vert.prog.uniformLocation("pos_offset");
		pick_vert.edge_id=pick_vert.prog.uniformLocation("edge_id");
		pick_vert.proj=pick_vert.prog.uniformLocation("mProj");
		pick_vert.thick=pick_vert.prog.uniformLocation("thickness");
		pick_vert.view=pick_vert.prog.uniformLocation("mView");
		if(this->glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc"};

		create_prog(draw_line.prog, "/glsl/line.vert", "/glsl/line.geom", "/glsl/line.frag", {});
		draw_line.center=draw_line.prog.uniformLocation("p0int");
		draw_line.color=draw_line.prog.uniformLocation("color");
		draw_line.proj=draw_line.prog.uniformLocation("mProj");
		draw_line.thick=draw_line.prog.uniformLocation("umpp");
		draw_line.view=draw_line.prog.uniformLocation("mView");
		if(this->glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc"};

		create_prog(draw_mark.prog, "/glsl/mark.vert", "/glsl/mark.geom", "/glsl/mark.frag", {});
		draw_mark.center=draw_mark.prog.uniformLocation("center");
		draw_mark.color=draw_mark.prog.uniformLocation("color");
		draw_mark.offset=draw_mark.prog.uniformLocation("p0int");
		draw_mark.proj=draw_mark.prog.uniformLocation("mProj");
		draw_mark.thick=draw_mark.prog.uniformLocation("umpp");
		draw_mark.view=draw_mark.prog.uniformLocation("mView");
		if(this->glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc"};

		create_prog2(draw_cube.prog, "/glsl/volume.vert", "/glsl/volume.frag", {});
		draw_cube.color=draw_cube.prog.uniformLocation("color_volume");
		draw_cube.rproj=draw_cube.prog.uniformLocation("mrProj");
		draw_cube.rtex=draw_cube.prog.uniformLocation("mrTex");
		draw_cube.rview=draw_cube.prog.uniformLocation("mrView");
		draw_cube.xfunc=draw_cube.prog.uniformLocation("xfunc_cube");
		draw_cube.zpars=draw_cube.prog.uniformLocation("zparsCube");
		auto cube=draw_cube.prog.uniformLocation("tex3d_cube");
		if(this->glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc"};
		this->glUseProgram(draw_cube.prog);
		this->glUniform1i(cube, 1);

		create_prog2(draw_sort.prog, "/glsl/sort.vert", "/glsl/sort.frag", {});
		auto opaque_color=draw_sort.prog.uniformLocation("tex_opaque_color");
		auto opaque_depth=draw_sort.prog.uniformLocation("tex_opaque_depth");
		auto surface_color=draw_sort.prog.uniformLocation("tex_surface_color");
		auto surface_depth=draw_sort.prog.uniformLocation("tex_surface_depth");
		auto cube_color=draw_sort.prog.uniformLocation("tex_volume0_color");
		auto cube_depth=draw_sort.prog.uniformLocation("tex_volume0_depth");
		if(this->glGetError()!=GL_NO_ERROR)
			throw std::runtime_error{"err getloc"};
		this->glUseProgram(draw_sort.prog);
		this->glUniform1i(opaque_depth, 3);
		this->glUniform1i(opaque_color, 4);
		this->glUniform1i(surface_depth, 5);
		this->glUniform1i(surface_color, 6);
		this->glUniform1i(cube_depth, 7);
		this->glUniform1i(cube_color, 8);
		gapr::gl::check_error(this->glGetError(), "set uniforms");
	}

	void canvas_ready() {
		/*! stage 1, canvas initialized */

		if(has_args()) {
			change_stage(SessionStage::Opening);
			_state_man.propagate();
			return ba::post(this->io_context(), [this]() {
				start();
			});
		}

		ba::post(this->ui_executor(), [this]() {
			this->display_login();
		});
		this->enable_file_open(false);
	}

	void paintFinish() {
		auto scaling=_scaling;
		bool scale=scaling!=0;
		if(scale) {
			this->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_scale);
			this->glViewport(0, 0, fbo_width, fbo_height);
			this->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
		} else {
			this->canvas_bind_default_fb();
			//glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
			this->glViewport(0, 0, fbo_width, fbo_height);
		}
		this->glBindVertexArray(viewerShared->vao_fixed);
		this->glUseProgram(draw_sort.prog);

		this->glActiveTexture(GL_TEXTURE3);
		this->glBindTexture(GL_TEXTURE_2D, fbo_opaque.depth());
		this->glActiveTexture(GL_TEXTURE4);
		this->glBindTexture(GL_TEXTURE_2D, fbo_opaque.color());
		this->glActiveTexture(GL_TEXTURE5);
		this->glBindTexture(GL_TEXTURE_2D, fbo_surface.depth());
		this->glActiveTexture(GL_TEXTURE6);
		this->glBindTexture(GL_TEXTURE_2D, fbo_surface.color());
		this->glActiveTexture(GL_TEXTURE7);
		this->glBindTexture(GL_TEXTURE_2D, fbo_cubes.depth());
		this->glActiveTexture(GL_TEXTURE8);
		this->glBindTexture(GL_TEXTURE_2D, fbo_cubes.color());
		this->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		this->glActiveTexture(GL_TEXTURE0);
		if(scale) {
			this->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_scale);
			this->canvas_bind_default_fb();
			//glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
			int width, height;
			width=fbo_width*(1+scaling);
			height=fbo_height*(1+scaling);
			this->glViewport(0, 0, width, height);
			this->glBlitFramebuffer(0, 0, fbo_width, fbo_height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}
		if(_debug_fps) {
			//glFinish();
			printFPS();
			this->canvas_update();
		}
	}
	std::unordered_map<edge_model::edge_id, int> _edge_vaoi;

	template<typename EdgeProg, typename VertProg>
		void paintEdgeImpl(const gapr::mat4<GLfloat>& mView, const gapr::mat4<GLfloat>& mProj, float umpp, const EdgeProg& prog_edge, const VertProg& prog_vert) {
			auto f_update=[this,&prog_edge,umpp](auto& val, auto idx) {
				if constexpr(idx.value==0) {
					this->glUniform2f(prog_edge.thick, val, umpp);
				} else if constexpr(idx.value==1) {
					auto col=color2vec(val);
					if constexpr(!decltype(prog_edge.picking)::value)
						this->glUniform3fv(prog_edge.color0, 1, &col[0]);
				} else if constexpr(idx.value==2) {
					this->glBindVertexArray(val);
					if constexpr(decltype(prog_edge.picking)::value)
						this->glUniform1ui(prog_edge.edge_id, val);
				}
			};
			auto f_draw3=[this](GLuint elem, GLsizei count2, const GLvoid* indices) {
				this->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elem);
				this->glDrawElements(GL_LINE_STRIP, count2, GL_UNSIGNED_INT, indices);
			};
			auto f_update_v=[this,&prog_vert](auto& val, auto idx) {
				if constexpr(idx.value==0) {
					auto col=color2vec(val);
					if constexpr(!decltype(prog_vert.picking)::value)
						this->glUniform3fv(prog_vert.color0, 1, &col[0]);
				} else if constexpr(idx.value==1) {
					this->glBindVertexArray(val);
					if constexpr(decltype(prog_vert.picking)::value)
						this->glUniform1ui(prog_vert.edge_id, val);
				}
			};
			auto f_draw3_v=[this](GLuint elem, GLsizei count2, const GLvoid* indices) {
				this->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elem);
				this->glDrawElements(GL_POINTS, count2, GL_UNSIGNED_INT, indices);
			};

			gapr::print(1, "begin edge paint");
			this->glDisable(GL_BLEND);
			this->glEnable(GL_PRIMITIVE_RESTART);
			this->glPrimitiveRestartIndex(GLuint(-1));

			gapr::print(1, "really begin edge paint");
			this->glUseProgram(prog_edge.prog);
			gapr::node_attr p{_cur_pos.point};
			this->glUniform3iv(prog_edge.center, 1, &p.ipos[0]);
			this->glUniformMatrix4fv(prog_edge.view, 1, false, &mView(0, 0));
			this->glUniformMatrix4fv(prog_edge.proj, 1, false, &mProj(0, 0));
			_edge_draw_cache.replay(f_update, f_draw3);
			gapr::print(1, "done draw edges");

			this->glUseProgram(prog_vert.prog);
			this->glUniformMatrix4fv(prog_vert.view, 1, false, &mView(0, 0));
			this->glUniformMatrix4fv(prog_vert.proj, 1, false, &mProj(0, 0));
			this->glUniform1f(prog_vert.thick, umpp);
			this->glUniform3iv(prog_vert.center, 1, &p.ipos[0]);
			_vert_draw_cache.replay(f_update_v, f_draw3_v);
			gapr::print(1, "done draw verts");

			this->glDisable(GL_PRIMITIVE_RESTART);
			this->glEnable(GL_BLEND);
		}
	void paintEdge() {
		this->glBindFramebuffer(GL_FRAMEBUFFER, fbo_opaque);
		this->glViewport(0, 0, fbo_width, fbo_height);
		this->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
		if(_data_only)
			return;

		auto scaling=_scaling;
		auto radU=_view_mode==ViewMode::Global?_global_zoom:_closeup_zoom;
		auto& mat=_mat[_view_mode==ViewMode::Global?CH_GLOBAL:CH_CLOSEUP];
		float umpp;
		if(scaling+1<_scale_factor) {
			umpp=radU*_scale_factor/(scaling+1)/fbo_width;
		} else {
			umpp=radU/fbo_width;
		}

		this->glDisable(GL_BLEND);

		this->glUseProgram(draw_edge.prog);
		gapr::node_attr p{_cur_pos.point};
		this->glUniform3iv(draw_edge.center, 1, &p.ipos[0]);
		this->glUniformMatrix4fv(draw_edge.view, 1, false, &mat.mView(0, 0));
		this->glUniformMatrix4fv(draw_edge.proj, 1, false, &mat.mProj(0, 0));
		auto num_path=path_data().size();
		if(num_path) {
			this->glUniform2f(draw_edge.thick, 1, umpp);
			auto col=colors[C_ATTENTION];
			this->glUniform3fv(draw_edge.color0, 1, &col[0]);
			this->glBindVertexArray(_vao_man.vao(pbiPath));
			this->glDrawArrays(GL_LINE_STRIP, 0, num_path);
		}

		edge_model::reader reader{_model};

		gapr::edge_model::vertex_id cur_root{};
		auto& curPos=_cur_pos;
		if(curPos.edge) {
			auto& edg=reader.edges().at(curPos.edge);
			if(edg.root)
				cur_root=edg.root;
		} else if(curPos.vertex) {
			auto& vert=reader.vertices().at(curPos.vertex);
			if(vert.root)
				cur_root=vert.root;
		}
		auto paint_edge=[this,cur_root](edge_model::edge_id eid, const edge_model::edge& e) {
			int thickness=1;
			auto color=colors[C_TYPE_0];
			GLuint vao;
			GLint first;
			GLsizei count;

			if(/*e.type()*/0!=0) {
				color=colors[C_TYPE_1];
			} else if(e.loop) {
				color=colors[C_ATTENTION];
			} else if(!e.root) {
				color=colors[C_ATTENTION]/2;
			} else if(e.root && e.root==cur_root) {
				color=colors[C_TYPE_2];
			}

			// XXX ZZZ
			if(e.raised) {//e.root /*&& e.tree().selected()*/) {
				thickness=2;
			}

#if 0
			if(curPos.edge && curPos.edge==e) {
				if(tgtPos.edge && tgtPos.edge==e) {
					auto ep=EdgePriv::get(e);
					glUniform1ui(idx_loc, static_cast<GLuint>(ep->index+1));
					glBindVertexArray(pathBuffers[ep->vaoi].vao);
					auto a=curPos.index;
					auto b=tgtPos.index;
					if(a>b)
						std::swap(a, b);
					if(b-a>0) {
						viewerShared->progs[P_EDGE]->setUniformValue("color_edge[0]", color);
						glDrawArrays(GL_LINE_STRIP, a, b-a+1);

					}
					if(a>0 || ep->points.size()-b>1) {
						viewerShared->progs[P_EDGE]->setUniformValue("color_edge[0]", color.darker(180));
						if(a>0) {
							glDrawArrays(GL_LINE_STRIP, 0, a+1);
						}
						if(ep->points.size()-b>1) {
							glDrawArrays(GL_LINE_STRIP, b, ep->points.size()-b);
						}
					}
					continue;
				}
			} else {
				if(!tgtPos.edge || tgtPos.edge!=e) {
					color=color.darker(180);
				}
			}
#endif

			auto [it_vaoi, it_ok]=_edge_vaoi.emplace(eid, 0);
			if(it_vaoi->second==0) {
				auto& pathGL=pathToPathGL(e.points);
				int vaoi=_vao_man.alloc(pathGL.size(), eid);
				_vao_man.buffer_data(vaoi, pathGL.data(), pathGL.size());
				it_vaoi->second=vaoi;
			}
			std::tie(vao, first)=_vao_man.vao_first(it_vaoi->second);
			count=e.points.size();
			_edge_draw_cache.cache({thickness, vec2color(color), vao}, first, count);
		};
		auto vert_vao=_vao_man.vao(_vert_vaoi);
		auto paint_vert=[this,&reader,vert_vao](edge_model::vertex_id vid, const edge_model::vertex& v, std::vector<PointGL>& buffer, std::vector<gapr::node_id>& buffer2) {
#if 0
			bool darker=false;
			if(curPos.edge && (curPos.edge.leftVertex()==v || curPos.edge.rightVertex()==v)) {
				if(tgtPos.edge && (tgtPos.edge.leftVertex()==v || tgtPos.edge.rightVertex()==v)) {
					if(curPos.edge==tgtPos.edge) {
						//darker=true;
					} else {
						darker=true;
					}
				}
			} else {
				if(!tgtPos.edge || (tgtPos.edge.leftVertex()!=v && tgtPos.edge.rightVertex()!=v)) {
					darker=true;
				}
			}
			//
			QColor color;
			if(v.inLoop() || !v.finished()) {
				color=colors[C_ATTENTION];
			} else if(v.tree() /*&& v.tree()==curTree*/) {
				color=colors[C_TYPE_2];
				if(darker)
					color=color.darker(180);
			} else {
				color=colors[C_TYPE_0];
				if(darker)
					color=color.darker(180);
			}
#endif
			// draw: root, terminal and isolated verts.
			int rad=5;
			auto color=colors[C_TYPE_0];
			GLuint vao=vert_vao;
			GLint first;
			GLsizei count;
			do {
				auto recheck_root=[&reader](auto vid) {
					gapr::prop_id k{vid, "root"};
					return reader.props().find(k)!=reader.props().end();
				};
				if(v.root && v.root==vid && recheck_root(vid)) {
					rad=9;
					color=colors[C_TYPE_1];
					break;
				} else if(v.edges.size()>1) {
					rad=4;
					break;
				}
				auto it_state=reader.props().find(gapr::prop_id{vid, "state"});
				if(it_state==reader.props().end()) {
					color=colors[C_ATTENTION];
				} else if(it_state->second=="end") {
					if(v.edges.size()==1 && rad==5)
						rad=4;
				} else {
					color=colors[C_ALERT];
				}
			} while(false);

			//if(v.tree() && v.tree().selected()) {
			//glDrawArrays(GL_LINES, 44, 16);
			//}
			auto& p=v.attr;
			first=buffer.size();
			count=1;
			buffer2.push_back(vid);
			buffer.emplace_back(PointGL{p.ipos, rad});
			_vert_draw_cache.cache({vec2color(color), vao}, first, count);
		};
		auto paint_prop=[this,&reader,vert_vao](const gapr::prop_id& id, std::vector<PointGL>& buffer, std::vector<gapr::node_id>& buffer2) {
			if(id.key!="error")
				return;

			int rad=5;
			auto color=colors[C_ALERT];
			GLuint vao=vert_vao;
			GLint first;
			GLsizei count;
			auto pos=reader.nodes().at(id.node);
			if(auto& v=reader.props().at(id); v!="" && v!="deferred") {
				for(unsigned int i=0; i<3; ++i)
					color[i]*=.6;
			}
			gapr::node_attr p;
			if(pos.edge) {
				auto& edge=reader.edges().at(pos.edge);
				p=gapr::node_attr{edge.points[pos.index/128]};
			} else {
				assert(pos.vertex);
				auto& vert=reader.vertices().at(pos.vertex);
				p=vert.attr;
			}
			//id.node
			first=buffer.size();
			count=1;
			buffer2.push_back(id.node);
			buffer.emplace_back(PointGL{p.ipos, rad});
			_vert_draw_cache.cache({vec2color(color), vao}, first, count);
		};
		bool use_filter{true};
		if(reader.edges_filter().empty() && reader.vertices_filter().empty()
				&& reader.props_filter().empty()) {
			use_filter=false;
		}
		if(_edg_cache_dirty) {
			gapr::print(1, "   recache edges");
			auto del_elems=[this](GLuint elems) {
				this->glDeleteBuffers(1, &elems);
			};
			auto is_longer=[](auto& e) {
				constexpr unsigned int min_count=128;
				constexpr double min_length=min_count*2.01234;
				if(e.nodes.size()>=min_count)
					return true;
				gapr::node_attr v11{e.points[0]};
				gapr::node_attr v22{e.points[e.points.size()-1]};
				gapr::vec3<double> v1;
				gapr::vec3<double> v2;
				for(unsigned int i=0; i<3; ++i) {
					v1[i]=v11.pos(i);
					v2[i]=v22.pos(i);
				}
				if((v1-v2).mag2()>=min_length*min_length)
					return true;
				return false;
			};
			_edge_draw_cache.begin(del_elems);
			if(use_filter) {
				for(auto eid: reader.edges_filter()) {
					auto& e=reader.edges().at(eid);
					paint_edge(eid, e);
				}
			} else if(true && reader.raised() && _view_mode==ViewMode::Global) {
				for(auto& [eid, e]: reader.edges()) {
					if(e.raised || is_longer(e))
						paint_edge(eid, e);
				}
			} else if(true && reader.raised() && _view_mode!=ViewMode::Global) {
				gapr::node_attr cur_pos{_cur_pos.point};
				gapr::vec3<double> origin;
				for(unsigned int i=0; i<3; ++i)
					origin[i]=cur_pos.pos(i);
				auto near_by=[origin](auto& e) {
					static double rad=100;
					gapr::node_attr v11{e.points[0]};
					gapr::node_attr v22{e.points[e.points.size()-1]};
					gapr::vec3<double> v1;
					gapr::vec3<double> v2;
					for(unsigned int i=0; i<3; ++i) {
						v1[i]=v11.pos(i);
						v2[i]=v22.pos(i);
					}
					unsigned int s{0};
					for(auto d2: {(v1-origin).mag2(), (v2-origin).mag2()}) {
						if(d2>(rad+128*2)*(rad+128*2))
							s+=2;
						else if(d2>rad*rad)
							s+=1;
					}
					return s<3;
				};
				for(auto& [eid, e]: reader.edges()) {
					do {
						break;
						unsigned int hits=0;
						for(auto n: e.nodes) {
							if(n==gapr::node_id{31516776})
								++hits;
						}
						if(hits==0)
							break;
					// ......
					// hhhhhhh 0 38 0
					// hhhhhhh 0 38 0
					// proofread is good
						fprintf(stderr, "hhhhhhh %d %zu %d\n", (int)e.raised, e.nodes.size(), (int)near_by(e));
					} while(false);
					if(e.raised || is_longer(e) || near_by(e))
						paint_edge(eid, e);
				}
			} else {
				for(auto& [eid, e]: reader.edges()) {
					paint_edge(eid, e);
				}
			}
			auto gen_elems=[this](const GLvoid* data, GLsizeiptr size) ->GLuint {
				GLuint elems;
				this->glGenBuffers(1, &elems);
				this->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elems);
				this->glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
				return elems;
			};
			_edge_draw_cache.finish(gen_elems);

			std::vector<PointGL> buffer;
			_vert_nodes.clear();
			_vert_draw_cache.begin(del_elems);
			if(use_filter) {
				for(auto vid: reader.vertices_filter()) {
					auto& v=reader.vertices().at(vid);
					paint_vert(vid, v, buffer, _vert_nodes);
				}
			} else if(reader.raised() && _view_mode==ViewMode::Global) {
				for(auto& [vid, v]: reader.vertices()) {
					if(v.raised || v.root==vid)
						paint_vert(vid, v, buffer, _vert_nodes);
				}
			} else if(reader.raised() && _view_mode!=ViewMode::Global) {
				auto near_by=[origin=gapr::node_attr{_cur_pos.point}](auto& v) {
					static int rad=100*1024;
					for(unsigned int i=0; i<3; ++i) {
						auto o=origin.ipos[i];
						auto p=v.attr.ipos[i];
						if(p<o-rad)
							return false;
						if(p>o+rad)
							return false;
					}
					return true;
				};
				for(auto& [vid, v]: reader.vertices()) {
					if(v.raised || v.root==vid || near_by(v))
						paint_vert(vid, v, buffer, _vert_nodes);
				}
			} else {
				for(auto& [vid, v]: reader.vertices()) {
					paint_vert(vid, v, buffer, _vert_nodes);
				}
			}
			if(use_filter) {
				for(auto& pid: reader.props_filter()) {
					paint_prop(pid, buffer, _vert_nodes);
				}
			} else {
				auto& err_nodes=reader.props().per_key("error");
				gapr::prop_id id{gapr::node_id{}, "error"};
				for(auto nid: err_nodes) {
					id.node=nid;
					paint_prop(id, buffer, _vert_nodes);
				}
			}
			_vert_draw_cache.finish(gen_elems);
			_vao_man.buffer_data(_vert_vaoi, buffer.data(), buffer.size());
			_edg_cache_dirty=false;
			gapr::print(1, "   end recache edges");
		}
		paintEdgeImpl(mat.mView, mat.mProj, umpp, draw_edge, draw_vert);
	}
	void paintOpaqueInset() {
	}
	void paintOpaque() {
		this->glBindFramebuffer(GL_FRAMEBUFFER, fbo_opaque);
		this->glViewport(0, 0, fbo_width, fbo_height);
		//glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

		if(_view_mode==ViewMode::Mixed) {
			this->glViewport(0, 0, _inset_w, _inset_h);
			float umpp;
			auto scaling=_scaling;
			auto radU=_inset_zoom;
			if(scaling+1<_scale_factor) {
				umpp=radU*_scale_factor/(scaling+1)/_inset_w;
			} else {
				umpp=radU/_inset_w;
			}
			auto& mat=_mat[CH_INSET];
			gapr::node_attr p{_inset_center[0], _inset_center[1], _inset_center[2]};

			//axis
			this->glUseProgram(draw_line.prog);
			this->glUniformMatrix4fv(draw_line.view, 1, false, &mat.mView(0, 0));
			this->glUniformMatrix4fv(draw_line.proj, 1, false, &mat.mProj(0, 0));
			this->glUniform1f(draw_line.thick, umpp);
			this->glUniform3iv(draw_line.center, 1, &p.ipos[0]);
			this->glBindVertexArray(_vao_man.vao(pbiFixed));
			if(_closeup_ch) {
				this->glUniform3fv(draw_line.color, 1, &colors[C_AXIS_X][0]);
				this->glDrawArrays(GL_LINES, 2+0*24+0*2, 1*2);
				this->glUniform3fv(draw_line.color, 1, &colors[C_AXIS_Y][0]);
				this->glDrawArrays(GL_LINES, 2+0*24+1*2, 1*2);
				this->glUniform3fv(draw_line.color, 1, &colors[C_AXIS_Z][0]);
				this->glDrawArrays(GL_LINES, 2+0*24+2*2, 1*2);
				this->glUniform3fv(draw_line.color, 1, &colors[8][0]);
				this->glDrawArrays(GL_LINES, 2+0*24+3*2, 9*2);
			}

			//axis label
			this->glUseProgram(draw_mark.prog);
			this->glUniformMatrix4fv(draw_mark.view, 1, false, &mat.mView(0, 0));
			this->glUniformMatrix4fv(draw_mark.proj, 1, false, &mat.mProj(0, 0));
			this->glUniform1f(draw_mark.thick, umpp);
			this->glUniform3iv(draw_mark.offset, 1, &p.ipos[0]);
			this->glBindVertexArray(viewerShared->vao_fixed);
			if(_closeup_ch) {
				auto& xform=closeup_xform();
				auto sizes=closeup_sizes();
				double x0=xform.origin[0];
				double y0=xform.origin[1];
				double z0=xform.origin[2];
				using Par=std::tuple<int, GLint, GLsizei, int>;
				auto pars={Par{0, 18, 4, C_AXIS_X}, Par{1, 22, 4, C_AXIS_Y}, Par{2, 26, 6, C_AXIS_Z}};
				for(auto [dir, first, count, colori]: pars) {
					gapr::node_attr p{x0+xform.direction[0+dir*3]*sizes[dir],y0+xform.direction[1+dir*3]*sizes[dir],z0+xform.direction[2+dir*3]*sizes[dir]};
					this->glUniform3fv(draw_mark.color, 1, &colors[colori][0]);
					this->glUniform4i(draw_mark.center, p.ipos[0], p.ipos[1], p.ipos[2], 6);
					this->glDrawArrays(GL_LINES, first, count);
				}
			}

			//curpos
			auto& curPos=_cur_pos;
			if(curPos.valid()) {
				this->glUniform3fv(draw_mark.color, 1, &colors[C_ATTENTION][0]);
				gapr::node_attr p{curPos.point};
				this->glUniform4i(draw_mark.center, p.ipos[0], p.ipos[1], p.ipos[2], 7);
				this->glDrawArrays(GL_LINE_LOOP, 100, 32);
			}
		}

		this->glViewport(0, 0, fbo_width, fbo_height);
		float umpp;
		auto scaling=_scaling;
		auto radU=_view_mode==ViewMode::Global?_global_zoom:_closeup_zoom;
		if(scaling+1<_scale_factor) {
			umpp=radU*_scale_factor/(scaling+1)/fbo_width;
		} else {
			umpp=radU/fbo_width;
		}
		auto& mat=_mat[_view_mode==ViewMode::Global?CH_GLOBAL:CH_CLOSEUP];
		gapr::node_attr p{_cur_pos.point};

		//pick
		this->glUseProgram(draw_line.prog);
		this->glUniformMatrix4fv(draw_line.view, 1, false, &mat.mView(0, 0));
		this->glUniformMatrix4fv(draw_line.proj, 1, false, &mat.mProj(0, 0));
		this->glUniform1f(draw_line.thick, umpp);
		if((pickA-pickB).mag2()>1e-5) {
			this->glUniform3iv(draw_line.center, 1, &p.ipos[0]);
			this->glBindVertexArray(_vao_man.vao(pbiFixed));
			this->glUniform3fv(draw_line.color, 1, &colors[C_ATTENTION][0]);
			this->glPointSize(3);
			//funcs->glDisable(GL_LINE_SMOOTH);
			this->glDrawArrays(GL_LINES, 0, 2);
		}
		if(mouse.mode==MouseMode::RectSel) {
			this->glUniform3iv(draw_line.center, 1, &p.ipos[0]);
			this->glBindVertexArray(_vao_man.vao(pbiFixed));
			this->glUniform3fv(draw_line.color, 1, &colors[C_ATTENTION][0]);
			this->glPointSize(3);
			this->glDrawArrays(GL_LINE_LOOP, 300, 4);
		}

		this->glBindVertexArray(viewerShared->vao_fixed);
		this->glUseProgram(draw_mark.prog);
		this->glUniformMatrix4fv(draw_mark.view, 1, false, &mat.mView(0, 0));
		this->glUniformMatrix4fv(draw_mark.proj, 1, false, &mat.mProj(0, 0));
		this->glUniform1f(draw_mark.thick, umpp);
		this->glUniform3iv(draw_mark.offset, 1, &p.ipos[0]);
		auto& tgtPos=_tgt_pos;
		if(tgtPos.valid()) {
			this->glUniform3fv(draw_mark.color, 1, &colors[C_ATTENTION][0]);
			gapr::node_attr pt{tgtPos.point};
			this->glUniform4i(draw_mark.center, pt.ipos[0], pt.ipos[1], pt.ipos[2], 15);
			if(tgtPos.edge) {
				this->glDrawArrays(GL_LINES, 10, 8);
			} else {
				this->glDrawArrays(GL_LINES, 6, 4);
			}
			// Neurons, in circle, mark
		}
		auto& curPos=_cur_pos;
		if(curPos.valid()) {
			this->glUniform3fv(draw_mark.color, 1, &colors[C_ATTENTION][0]);
			gapr::node_attr pt{curPos.point};
			this->glUniform4i(draw_mark.center, pt.ipos[0], pt.ipos[1], pt.ipos[2], 11);
			this->glDrawArrays(GL_LINE_LOOP, 100, 32);
			// Neurons, in circle, mark
		}
		if(!_nodes_sel.empty()) {
			this->glUniform3fv(draw_mark.color, 1, &colors[C_AXIS_X][0]);
			for(auto& [n, pt]: _nodes_sel) {
				this->glUniform4i(draw_mark.center, pt.ipos[0], pt.ipos[1], pt.ipos[2], 4);
				this->glDrawArrays(GL_LINE_LOOP, 100, 32);
			}
		}

#if 0
		viewerShared->progs[P_NODE]->bind();
		locp=viewerShared->progs[P_NODE]->uniformLocation("p0int");
		glUniform3i(locp, p._x, p._y, p._z);
		viewerShared->progs[P_NODE]->setUniformValue("mView", mView);
		viewerShared->progs[P_NODE]->setUniformValue("mProj", mProj);
		viewerShared->progs[P_NODE]->setUniformValue("color[0]", colors[C_TYPE_4]);
		viewerShared->progs[P_NODE]->setUniformValue("color[1]", colors[C_TYPE_4]);
		viewerShared->progs[P_NODE]->setUniformValue("color[2]", colors[C_TYPE_4]);
		viewerShared->progs[P_NODE]->setUniformValue("color[3]", colors[C_TYPE_4]);
		viewerShared->progs[P_NODE]->setUniformValue("umpp", 10*umpp);
		for(auto e: graph.edges()) {
			auto ep=EdgePriv::get(e);
			glBindVertexArray(pathBuffers[ep->vaoi].vao);
			glDrawArrays(GL_POINTS, 0, e.points().size());
		}
#endif
	}
	void paintSurface() {
		this->glBindFramebuffer(GL_FRAMEBUFFER, fbo_surface);
		this->glViewport(0, 0, fbo_width, fbo_height);
		this->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
#if 0
		glDisable(GL_BLEND);

		QColor color{255, 255, 255, 120};
		viewerShared->progs[P_MESH]->bind();
		viewerShared->progs[P_MESH]->setUniformValue("mView", mView);
		viewerShared->progs[P_MESH]->setUniformValue("mProj", mProj);
		viewerShared->progs[P_MESH]->setUniformValue("color", color);
		auto locp=viewerShared->progs[P_MESH]->uniformLocation("p0int");
		auto& p=curPos.point;
		glUniform3i(locp, p._x, p._y, p._z);

		glBindVertexArray(vao_mesh);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);


		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo_mesh[1]);
		glDrawElements(GL_TRIANGLES, meshIdxes.size(), GL_UNSIGNED_INT, nullptr);
		this->glEnable(GL_BLEND);
#endif
	}
	void paintBlank() {
		int width, height;
		auto scaling=_scaling;
		width=fbo_width*(1+scaling);
		height=fbo_height*(1+scaling);
		this->glViewport(0, 0, width, height);
		this->glClearColor(0.0, 0.1, 0.0, 1.0);
		this->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
	}

	void paintVolumeImpl(Matrices& mat, int tex, std::array<double, 2>& _xfunc, double min_voxel, double zoom) {
		this->glUniformMatrix4fv(draw_cube.rview, 1, false, &mat.mrView(0, 0));
		this->glUniformMatrix4fv(draw_cube.rproj, 1, false, &mat.mrProj(0, 0));
		if(tex) {
			this->glBindTexture(GL_TEXTURE_3D, viewerShared->cubeTextures[tex].tex);
			vec3<GLfloat> xfunc(_xfunc[0], _xfunc[1], 0.0f);
			this->glUniform2fv(draw_cube.xfunc, 1, &xfunc[0]);
			this->glUniformMatrix4fv(draw_cube.rtex, 1, false, &mat.cube_mat(0, 0));
			auto mvs=min_voxel;
			auto radU=zoom;
			auto slice_pars=_slice_pars;
			if(_slice_mode) {
				this->glUniform3fv(draw_cube.zpars, 1, &gapr::vec3<GLfloat>(
							-1.0*_slice_delta/slice_pars[1],
							-1.0*_slice_delta/slice_pars[1],
							std::max(1.0/slice_pars[1], 9*mvs/radU/8))[0]);
			} else {
				this->glUniform3fv(draw_cube.zpars, 1, &gapr::vec3<GLfloat>(
							-1.0*slice_pars[0]/slice_pars[1],
							1.0*slice_pars[0]/slice_pars[1],
							std::max(1.0/slice_pars[1], 9*mvs/radU/8))[0]);
			}
			this->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}
	}
	void paintVolumeInset() {
		auto& mat=_mat[CH_INSET];
		this->glUniformMatrix4fv(draw_cube.rview, 1, false, &mat.mrView(0, 0));
		this->glUniformMatrix4fv(draw_cube.rproj, 1, false, &mat.mrProj(0, 0));

		if(global_cube_tex) {
			this->glBindTexture(GL_TEXTURE_3D, viewerShared->cubeTextures[global_cube_tex].tex);
			vec3<GLfloat> xfunc(_global_xfunc[0], _global_xfunc[1], 0.0f);
			this->glUniform2fv(draw_cube.xfunc, 1, &xfunc[0]);
			this->glUniformMatrix4fv(draw_cube.rtex, 1, false, &mat.cube_mat(0, 0));
			auto mvs=global_min_voxel;
			auto slice_pars=_slice_pars;
			this->glUniform3fv(draw_cube.zpars, 1, &gapr::vec3<GLfloat>(
						-1.0, 1.0,
						std::max(1.0/slice_pars[1], 9*mvs/_inset_zoom/8))[0]);
			this->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}
	}
	void paintVolume() {
		this->glBindFramebuffer(GL_FRAMEBUFFER, fbo_cubes);
		this->glViewport(0, 0, fbo_width, fbo_height);
		this->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

		this->glUseProgram(draw_cube.prog);
		this->glUniform3fv(draw_cube.color, 1, &colors[C_CHAN0][0]);
		this->glBindVertexArray(viewerShared->vao_fixed);
		this->glActiveTexture(GL_TEXTURE1);
		switch(_view_mode) {
			case ViewMode::Global:
				paintVolumeImpl(_mat[CH_GLOBAL], global_cube_tex, _global_xfunc, global_min_voxel, _global_zoom);
				break;
			case ViewMode::Closeup:
				paintVolumeImpl(_mat[CH_CLOSEUP], closeup_cube_tex, _closeup_xfunc, closeup_min_voxel, _closeup_zoom);
				break;
			case ViewMode::Mixed:
				// XXX Inset by stencil or another smaller fbo and blit?
				this->glEnable(GL_STENCIL_TEST);
				this->glStencilFunc(GL_NOTEQUAL, 1, 0xff);
				this->glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
				this->glViewport(0, 0, _inset_w, _inset_h);
				paintVolumeInset();
				this->glViewport(0, 0, fbo_width, fbo_height);
				paintVolumeImpl(_mat[CH_CLOSEUP], closeup_cube_tex, _closeup_xfunc, closeup_min_voxel, _closeup_zoom);
				this->glDisable(GL_STENCIL_TEST);
				break;
		}
	}

	template<unsigned int CH> void updateCubeMatrix() {
		// Depends on: *_cur_pos, cube.xform, cube.data
		std::array<double, 3> p0;
		const gapr::affine_xform* xform;
		std::array<unsigned int, 3> p1;
		std::array<std::size_t, 3> size;
		if constexpr(CH==CH_INSET) {
			p0=_inset_center;
		} else {
			gapr::node_attr pt{_cur_pos.point};
			for(unsigned int i=0; i<3; i++)
				p0[i]=pt.pos(i);
		}
		if constexpr(CH==CH_CLOSEUP) {
			xform=&closeup_xform();
			p1=_closeup_offset;
			auto cube_view=_closeup_cube.view<const char>();
			size={cube_view.width_adj(), cube_view.sizes(1), cube_view.sizes(2)};
			//cl cl
		} else {
			xform=&global_xform();
			p1={0, 0, 0};
			auto cube_view=_global_cube.view<const char>();
			size={cube_view.width_adj(), cube_view.sizes(1), cube_view.sizes(2)};
			//glob glob
		}
		auto& mat=_mat[CH];
		for(unsigned int i=0; i<3; i++)
			p0[i]-=xform->origin[i];
		for(int i=0; i<4; i++)
			mat.cube_mat(3, i)=i<3?0:1;
		auto& rdir=xform->direction_inv;
		for(int i=0; i<3; i++) {
			double v=0.0;
			for(int j=0; j<3; j++) {
				mat.cube_mat(i, j)=rdir[i+j*3]/size[i];
				v+=rdir[i+j*3]*p0[j];
			}
			mat.cube_mat(i, 3)=(v-p1[i])/size[i];
		}
	}
	void updateInsetCenter() {
		auto& xform=closeup_xform();
		auto sizes=closeup_sizes();
		double z=0.0;
		for(unsigned int i=0; i<3; i++) {
			double x{0.0};
			for(unsigned int j=0; j<3; j++)
				x+=xform.direction[i+j*3]*sizes[j];
			z+=x*x;
			_inset_center[i]=x/2+xform.origin[i];
		}
		_inset_zoom=std::sqrt(z)/2;
	}
	template<unsigned int CH> void updateZoomMinMax() {
		const gapr::affine_xform* xform;
		std::array<unsigned int, 3> size;
		double* outa;
		double* outb;
		if constexpr(CH==CH_CLOSEUP) {
			xform=&closeup_xform();
			auto cubesizes=closeup_cube_sizes();
			for(unsigned int i=0; i<3; i++)
				size[i]=cubesizes[i]*3;
			outa=&closeup_min_voxel;
			outb=&closeup_max_dim;
		} else {
			xform=&global_xform();
			size=global_sizes();
			outa=&global_min_voxel;
			outb=&global_max_dim;
		}
		double vb=0;
		double va{INFINITY};
		for(unsigned int i=0; i<3; i++) {
			auto res=xform->resolution[i];
			if(va>res)
				va=res;
			auto vv=res*size[i];
			if(vv>vb)
				vb=vv;
		}
		*outa=va;
		*outb=vb;
	}
	template<unsigned int CH> void updateMVPMatrices() {
		// Depends on: *rgt, *radU, *up, *fbo_height, *fbo_width
		double radU;
		GLsizei w, h;
		double z_clip;
		if constexpr(CH==CH_INSET) {
			radU=_inset_zoom;
			w=_inset_w;
			h=_inset_h;
			z_clip=1;
		} else {
			if constexpr(CH==CH_CLOSEUP) {
				radU=_closeup_zoom;
			} else {
				radU=_global_zoom;
			}
			w=fbo_width;
			h=fbo_height;
			z_clip=.5;
		}
		auto& mat=_mat[CH];
		auto& dir=_direction;
		gapr::vec3<GLfloat> rgt{(float)dir[3], (float)dir[4], (float)dir[5]};
		gapr::vec3<GLfloat> up{(float)dir[0], (float)dir[1], (float)dir[2]};
		mat.mView.look_at(rgt*radU, gapr::vec3<GLfloat>{0, 0, 0}, up, &mat.mrView);
		// XXX in detial?
		gapr::print("rgt: ", rgt[0], ",", rgt[1], ",", rgt[2]);
		gapr::print("up: ", up[0], ",", up[1], ",", up[2], " ", radU);

		float vspan=radU*h/w;
		mat.mProj.ortho(-radU, radU, -vspan, vspan, (1-z_clip)*radU, (1+z_clip)*radU, &mat.mrProj);
	}
	template<unsigned int CH> void updateCubeTexture() {
		gapr::print("update cube ", CH);
		int* pi;
		const gapr::cube* cub;
		if constexpr(CH==CH_CLOSEUP) {
			pi=&closeup_cube_tex;
			cub=&_closeup_cube;
		} else {
			pi=&global_cube_tex;
			cub=&_global_cube;
		}
		if(*pi)
			viewerShared->releaseTexture(*pi);
		if(*cub)
			*pi=viewerShared->getTexture(*cub);
		else
			*pi=0;
	}
	void updateInsetAxis() {
		int ch=0;

		auto size=closeup_sizes();
		auto& xform=closeup_xform();
		uint32_t p0[3]={0, 0, 0};
		//for(int i=0; i<3; i++) {
		//gapr::print(size[i]);
		//}
		std::array<PointGL, 24> buf;
		for(int i=0; i<2; i++) {
			for(int j=0; j<2; j++) {
				for(int dir=0; dir<3; dir++) {
					for(int k=0; k<2; k++) {
						double pp[3];
						for(int l=0; l<3; l++)
							pp[l]=xform.origin[l]
								+xform.direction[l+dir*3]*(k*size[dir]+p0[dir])
								+xform.direction[l+(dir+1)%3*3]*(j*size[(dir+1)%3]+p0[(dir+1)%3])
								+xform.direction[l+(dir+2)%3*3]*(i*size[(dir+2)%3]+p0[(dir+2)%3]);
						gapr::node_attr p{pp[0], pp[1], pp[2]};
						buf[i*12+j*6+dir*2+k]=p.data();
					}
				}
			}
		}
		this->glBindBuffer(GL_ARRAY_BUFFER, _vao_man.buffer(pbiFixed));
		this->glBufferSubData(GL_ARRAY_BUFFER, (2+ch*24)*sizeof(PointGL), 24*sizeof(PointGL), buf.data());
	}
	GLsizei fbo_nsamp;
	void resizeFrameBuffers() {
		auto ww=_wid_width;
		auto hh=_wid_height;
		int width, height;
		auto scaling=_scaling;
		auto min_dim=std::min(_wid_width, _wid_height)/_scale_factor;
		auto max_dim=std::max(_wid_width, _wid_height)/_scale_factor;
		if(min_dim>=1050 && max_dim>=1536) {
			_inset_w=_inset_h=384*_scale_factor;
		} else if(min_dim>=700 && max_dim>=1024) {
			_inset_w=_inset_h=256*_scale_factor;
		} else if(min_dim>=520 && max_dim>=768) {
			_inset_w=_inset_h=192*_scale_factor;
		} else if(min_dim>=350 && max_dim>=512) {
			_inset_w=_inset_h=128*_scale_factor;
		} else {
			_inset_w=_inset_h=96*_scale_factor;
		}
		width=(ww+scaling)/(1+scaling);
		height=(hh+scaling)/(1+scaling);
		_inset_w=(_inset_w+scaling)/(1+scaling);
		_inset_h=(_inset_h+scaling)/(1+scaling);

		fbo_width=width;
		fbo_height=height;
		if(fbo_width>fbo_width_alloc || fbo_width+2*FBO_ALLOC<fbo_width_alloc ||
				fbo_height>fbo_height_alloc || fbo_height+2*FBO_ALLOC<fbo_height_alloc ||
				fbo_nsamp!=1) {
			fbo_nsamp=1;
			fbo_width_alloc=(fbo_width+FBO_ALLOC)/FBO_ALIGN*FBO_ALIGN;
			fbo_height_alloc=(fbo_height+FBO_ALLOC)/FBO_ALIGN*FBO_ALIGN;

			fbo_scale.resize(fbo_width_alloc, fbo_height_alloc);
			fbo_opaque.resize(fbo_width_alloc, fbo_height_alloc);
			fbo_surface.resize(fbo_width_alloc, fbo_height_alloc);
			fbo_cubes.resize(fbo_width_alloc, fbo_height_alloc);
			//updateMVPMatrices();
		}
	}
	void set_scale_factor(double scale_factor) {
		_scale_factor=scale_factor;
		_state_man2.change(_gl_state1_scale_factor);
	}
	std::array<double, 6> handleRotate() {
		gapr::vec3<GLfloat> a((mouse.xPressed*2+1.0)/_wid_width-1, 1-(mouse.yPressed*2+1.0)/_wid_height, -1);
		gapr::vec3<GLfloat> b((mouse.x*2+1.0)/_wid_width-1,1-(mouse.y*2+1.0)/_wid_height, -1);
		a=mrViewProj*a;
		a/=a.mag();
		b=mrViewProj*b;
		b/=b.mag();
		gapr::vec3<GLfloat> norm=cross(a, b);
		if(norm.mag2()<1e-7)
			norm[0]+=1e-3;
		gapr::mat4<GLfloat> mat{};
		float proj=dot(a, b);
		if(proj<-1) proj=-1;
		if(proj>1) proj=1;
		float r=acos(proj);
		mat.rotate(-r, norm);
		auto up=mat*_prev_up;
		auto rgt=mat*_prev_rgt;
		return {up[0], up[1], up[2], rgt[0], rgt[1], rgt[2]};
	}
	gapr::vec3<GLfloat> toLocal(const gapr::node_attr& p) const {
		gapr::node_attr d;
		gapr::node_attr pt{_cur_pos.point};
		for(unsigned int k=0; k<3; ++k)
			d.ipos[k]=p.ipos[k]-pt.ipos[k];
		return gapr::vec3<GLfloat>(d.pos(0), d.pos(1), d.pos(2));
	}
	gapr::node_attr toGlobal(const gapr::vec3<GLfloat>& p) const {
		gapr::node_attr pt{_cur_pos.point};
		auto x=p[0]+pt.pos(0);
		auto y=p[1]+pt.pos(1);
		auto z=p[2]+pt.pos(2);
		return {x, y, z};
	}

	/*! enable snapping, by *pixel. XXX not radius or distance.
	 * 
	 */
	bool pickPoint(int x, int y, Position& pos) {
		int pick_size;
		int fbx, fby;
		auto scaling=_scaling;
		if(scaling+1<_scale_factor) {
			pick_size=PICK_SIZE*_scale_factor/(scaling+1);
			fbx=x/(1+scaling);
			fby=y/(1+scaling);
		} else {
			pick_size=PICK_SIZE;
			fbx=x/(1+scaling);
			fby=y/(1+scaling);
		}

		float px=(fbx*2+1.0)/fbo_width-1;
		float py=1-(fby*2+1.0)/fbo_height;
		auto& m=_mat[_view_mode==ViewMode::Global?CH_GLOBAL:CH_CLOSEUP];
		if(_slice_mode) {
			auto slice_pars=_slice_pars;
			float depth=-1.0*_slice_delta/slice_pars[1];
			gapr::vec3<GLfloat> lp=m.mrView*m.mrProj*gapr::vec3<GLfloat>(px, py, depth);
			auto p=toGlobal(lp);
			pos=Position{p.data()};
			return true;
		}

		bool ret=false;
		this->make_current();
		if(fbx-pick_size>=0 && fby-pick_size>=0
				&& fby+pick_size<fbo_height
				&& fbx+pick_size<fbo_width) {
			gapr::print(1, " begin pick");
			int pickdr2=pick_size*pick_size+1;
			int pickI=-1;

			do {
				this->glBindFramebuffer(GL_FRAMEBUFFER, fbo_pick);
				this->glViewport(0, 0, 2*pick_size+1, 2*pick_size+1);
				this->glClearColor(0.0, 0.0, 0.0, 1.0);
				this->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
				if(_data_only)
					break;
				auto radU=_view_mode==ViewMode::Global?_global_zoom:_closeup_zoom;
				float umpp;
				if(scaling+1<_scale_factor) {
					umpp=radU*_scale_factor/(scaling+1)/fbo_width;
				} else {
					umpp=radU/fbo_width;
				}
				gapr::mat4<GLfloat> mProj2;
				auto centx=px*radU;
				auto centy=py*radU*fbo_height/fbo_width;
				auto span=pick_size*radU/fbo_width;
				mProj2.ortho(centx-span, centx+span, centy-span, centy+span, (1-.5)*radU, (1+.5)*radU, nullptr);

			gapr::print(1, " pick begin paint");
				paintEdgeImpl(m.mView, mProj2, umpp, pick_edge, pick_vert);
			gapr::print(1, " pick end paint");

				this->glFlush();
			gapr::print(1, " pick flushed paint");

			} while(false);

			std::vector<GLuint> bufIdx((2*pick_size+1)*(2*pick_size+1));
			this->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_pick);
			gapr::print(1, " pick flushed paint bind");
			this->glReadBuffer(GL_COLOR_ATTACHMENT0);
			gapr::print(1, " pick flushed paint read");
			this->glReadPixels(0, 0, pick_size*2+1, pick_size*2+1, GL_RED_INTEGER, GL_UNSIGNED_INT, &bufIdx[0]);
			gapr::print(1, " read pick");

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
				edge_model::edge_id edge_id=bufIdx[pickI];
				this->glReadBuffer(GL_COLOR_ATTACHMENT1);
				std::vector<GLint> bufPos((2*pick_size+1)*(2*pick_size+1));
				this->glReadPixels(0, 0, pick_size*2+1, pick_size*2+1, GL_RED_INTEGER, GL_INT, &bufPos[0]);
				this->clear_current();
				gapr::print(1, " end pick", edge_id, ':', bufPos[pickI]);

				if(edge_id==_vao_man.vao(_vert_vaoi)) {
					edge_model::vertex_id vid{_vert_nodes[bufPos[pickI]]};
					pos=Position{vid, {}};
					return true;
				}

				uint32_t pickPos=bufPos[pickI]*8;
				auto [eid, idx]=_vao_man.lookup(edge_id, pickPos/128);
				pickPos=idx*128+pickPos%128;
				pos=Position{eid, pickPos, {}};

				return true;
			}

			pick_size/=2;
			int mdx=0, mdy=0;
			GLuint maxv=0;
			std::vector<GLuint> bufRed((2*pick_size+1)*(2*pick_size+1));
			this->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_cubes);
			this->glReadBuffer(GL_COLOR_ATTACHMENT0);
			this->glReadPixels(fbx-pick_size, fbo_height-1-fby-pick_size, pick_size*2+1, pick_size*2+1, GL_RED, GL_UNSIGNED_INT, &bufRed[0]);
			for(int dy=-pick_size; dy<=pick_size; dy++) {
				for(int dx=-pick_size; dx<=pick_size; dx++) {
					int i=dx+pick_size+(dy+pick_size)*(2*pick_size+1);
					auto v=bufRed[i];
					if(v>maxv) {
						maxv=v;
						mdx=dx;
						mdy=dy;
					}
				}
			}
			if(maxv>0) {
				GLfloat mvaldepth;
				//glReadBuffer(GL_DEPTH_STENCIL_ATTACHMENT);
				this->glReadPixels(fbx+mdx, fbo_height-1-fby+mdy, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &mvaldepth);
				//canvas.doneCurrent();
				auto xx=mdx+pick_size+fbx-pick_size;
				auto yy=-mdy-pick_size+fby+pick_size;
				gapr::vec3<GLfloat> pp((xx*2+1.0)/fbo_width-1, 1-(yy*2+1.0)/fbo_height, mvaldepth*2-1);
				pp=m.mrView*m.mrProj*pp;
				auto p=toGlobal(pp);
				pos=Position{p.data()};
				ret=true;
			}
		}

		gapr::vec3<GLfloat> a(px, py, -1);
		gapr::vec3<GLfloat> b(px, py, 1);
		a=m.mrView*(m.mrProj*a);
		b=m.mrView*(m.mrProj*b);
		gapr::vec3<GLfloat> c=a-b;
		auto lenc=c.mag();
		c=c/lenc;
		gapr::vec3<GLfloat> selC=pickA-pickB;
		if(auto l2=selC.mag2(); l2>1e-5)
			selC/=std::sqrt(l2);
		gapr::vec3<GLfloat> d=cross(c, selC);

		gapr::mat4<GLfloat> mat{};
		mat(3, 3)=1;
		for(unsigned int i=0; i<3; i++) {
			mat(i, 0)=c[i];
			mat(i, 1)=-selC[i];
			mat(i, 2)=d[i];
			mat(3, i)=0;
			mat(i, 3)=0;
		}
		gapr::mat4<GLfloat> matr;
		matr.inverse(mat);
		gapr::vec3<GLfloat> s=matr*(pickB-b);
		if(d.mag2()<1e-2 || s[0]<0 || s[0]>lenc) {
			pickA=a;
			pickB=b;
			std::array<PointGL, 2> buf;
			buf[0]=toGlobal(pickA).data();
			buf[1]=toGlobal(pickB).data();
			this->glBindBuffer(GL_ARRAY_BUFFER, _vao_man.buffer(pbiFixed));
			this->glBufferSubData(GL_ARRAY_BUFFER, 0*sizeof(PointGL), 2*sizeof(PointGL), buf.data());
			this->clear_current();
			return ret;
		}
		this->clear_current();
		gapr::vec3<GLfloat> t=b+s[0]*c;
		auto gt=toGlobal(t);
		pickA=pickB=gapr::vec3<GLfloat>();
		pos=Position{gt.data()};
		return true;
	}
	std::vector<GLuint> _sel_buf_idx;
	std::vector<GLint> _sel_buf_pos;
	bool selectNodes(int x0, int y0, int x, int y, std::vector<std::pair<gapr::node_id, gapr::node_attr>>& nodes_sel) {
		auto scaling=_scaling;
		int fbx0, fby0, fbx, fby;
		fbx0=std::max(std::min(x0, x)/(1+scaling), 0);
		fby0=std::max(std::min(y0, y)/(1+scaling), 0);
		fbx=std::min(std::max(x0, x)/(1+scaling), fbo_width-1);
		fby=std::min(std::max(y0, y)/(1+scaling), fbo_height-1);
		auto& m=_mat[_view_mode==ViewMode::Global?CH_GLOBAL:CH_CLOSEUP];

		this->make_current();
			do {
				this->glBindFramebuffer(GL_FRAMEBUFFER, fbo_pick);
				this->glViewport(0, 0, 128, 128);
				this->glClearColor(0.0, 0.0, 0.0, 1.0);
				this->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
				if(_data_only)
					break;
				auto radU=_view_mode==ViewMode::Global?_global_zoom:_closeup_zoom;
				float umpp;
				if(scaling+1<_scale_factor) {
					umpp=radU*_scale_factor/(scaling+1)/fbo_width;
				} else {
					umpp=radU/fbo_width;
				}
				gapr::mat4<GLfloat> mProj2;
				float px=(fbx+fbx0+1.0)/fbo_width-1;
				float py=1-(fby+fby0+1.0)/fbo_height;
				auto centx=px*radU;
				auto centy=py*radU*fbo_height/fbo_width;
				auto spanx=(fbx-fbx0)*radU/fbo_width;
				auto spany=(fby-fby0)*radU/fbo_height;
				mProj2.ortho(centx-spanx, centx+spanx, centy-spany, centy+spany, (1-.5)*radU, (1+.5)*radU, nullptr);

				paintEdgeImpl(m.mView, mProj2, umpp, pick_edge, pick_vert);

				this->glFlush();

			} while(false);

		this->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_pick);
		this->glReadBuffer(GL_COLOR_ATTACHMENT0);
		_sel_buf_idx.resize(128*128);
		this->glReadPixels(0, 0, 128, 128, GL_RED_INTEGER, GL_UNSIGNED_INT, &_sel_buf_idx[0]);
		gapr::gl::check_error(this->glGetError(), "read sel");
		std::vector<unsigned int> tmp{};
		for(std::size_t i=0; i<_sel_buf_idx.size(); ++i) {
				if(_sel_buf_idx[i]>0)
					tmp.push_back(i);
		}
		if(tmp.empty()) {
			this->clear_current();
			return false;
		}

		_sel_buf_pos.resize(128*128);
		this->glReadBuffer(GL_COLOR_ATTACHMENT1);
		this->glReadPixels(0, 0, 128, 128, GL_RED_INTEGER, GL_INT, &_sel_buf_pos[0]);
		gapr::gl::check_error(this->glGetError(), "read more");
		this->clear_current();

		edge_model::reader reader{_model};
		std::unordered_map<gapr::node_id, bool> status;
		for(auto i: tmp) {
			edge_model::edge_id edge_id=_sel_buf_idx[i];
			if(edge_id==_vao_man.vao(_vert_vaoi)) {
				edge_model::vertex_id vid{_vert_nodes[_sel_buf_pos[i]]};
				status.emplace(vid, false);
			} else {
				uint32_t pickPos=_sel_buf_pos[i]*8;
				auto [eid, idx]=_vao_man.lookup(edge_id, pickPos/128);
				auto& e=reader.edges().at(eid);
				pickPos=idx*128+pickPos%128;
				status.emplace(e.nodes[pickPos/128], false);
			}
		}

		std::deque<std::tuple<gapr::edge_model::edge_id, bool, std::size_t>> todo;
		auto cc_add_vert=[&todo,&reader](gapr::edge_model::vertex_id vid) {
			auto& v=reader.vertices().at(vid);
			for(auto [e, dir]: v.edges)
				todo.emplace_back(e, dir, dir?SIZE_MAX:0);
		};
		float px0=(fbx0*2+1.0)/fbo_width-1;
		float px=(fbx*2+1.0)/fbo_width-1;
		float py=1-(fby0*2+1.0)/fbo_height;
		float py0=1-(fby*2+1.0)/fbo_height;
		auto check_pos=[mvp=m.mProj*m.mView,px0,px,py0,py,this](gapr::node_attr node) ->bool {
			auto lp=mvp*toLocal(node);
			if(lp[0]<px0 || lp[0]>px)
				return false;
			if(lp[1]<py0 || lp[1]>py)
				return false;
			if(lp[2]<-1.0 || lp[2]>1.0)
				return false;
			return true;
		};
		std::vector<std::pair<gapr::node_id, gapr::node_attr>> sel;
		auto cc_grow=[&sel,&todo,&reader,&status,cc_add_vert,&check_pos]() {
			while(!todo.empty()) {
				auto [eid, dir, idx]=todo.front();
				todo.pop_front();
				auto& e=reader.edges().at(eid);
				if(idx==SIZE_MAX)
					idx=e.nodes.size()-1;
				do {
					if(dir) {
						if(idx==0) {
							cc_add_vert(e.left);
							break;
						}
						--idx;
					} else {
						if(idx==e.nodes.size()-1) {
							cc_add_vert(e.right);
							break;
						}
						++idx;
					}
					auto n=e.nodes[idx];
					auto [it, ins]=status.emplace(n, true);
					if(ins) {
						if(!check_pos(gapr::node_attr{e.points[idx]}))
							break;
					} else {
						if(it->second)
							break;
						it->second=true;
					}
					sel.emplace_back(n, gapr::node_attr{e.points[idx]});
				} while(true);
			}
		};
		auto cc_from_vert=[&sel,&status,&reader,cc_add_vert,cc_grow](gapr::edge_model::vertex_id vid) ->bool {
			auto& s=status.at(vid);
			if(s)
				return false;
			auto& v=reader.vertices().at(vid);
			sel.emplace_back(vid, v.attr);
			s=true;
			cc_add_vert(vid);
			cc_grow();
			return true;
		};
		auto cc_from_edge=[&todo,&sel,&status,&reader,cc_grow](gapr::edge_model::edge_id eid, std::size_t idx) ->bool {
			auto& e=reader.edges().at(eid);
			auto n=e.nodes[idx];
			auto& s=status.at(n);
			if(s)
				return false;
			sel.emplace_back(n, gapr::node_attr{e.points[idx]});
			s=true;
			todo.emplace_back(eid, false, idx);
			todo.emplace_back(eid, true, idx);
			cc_grow();
			return true;
		};
		std::vector<std::pair<gapr::node_id, gapr::node_attr>> sel_top;
		std::size_t top_score{0};
		gapr::node_id curn{};
		if(_cur_pos.edge) {
			auto& e=reader.edges().at(_cur_pos.edge);
			curn=e.nodes[_cur_pos.index/128];
		} else if(_cur_pos.vertex) {
			curn=_cur_pos.vertex;
		}
		auto add_cc=[&sel,&sel_top,&top_score,&todo,curn](bool conj) {
			if(conj) {
				for(auto v: sel)
					sel_top.push_back(v);
			} else {
				std::size_t score{sel.size()};
				for(auto& v: sel) {
					if(v.first==curn)
						score+=1'000'000'000;
				}
				if(score>top_score) {
					std::swap(sel_top, sel);
					top_score=score;
				}
			}
			sel.clear();
			todo.clear();
		};
		for(auto i: tmp) {
			edge_model::edge_id edge_id=_sel_buf_idx[i];
			gapr::edge_model::position pos{};
			if(edge_id==_vao_man.vao(_vert_vaoi)) {
				edge_model::vertex_id vid{_vert_nodes[_sel_buf_pos[i]]};
				pos=reader.nodes().at(vid);
				if(!pos.edge) {
					if(cc_from_vert(vid))
						add_cc(mouse.sel_all);
				}
			} else {
				uint32_t pickPos=_sel_buf_pos[i]*8;
				auto [eid, idx]=_vao_man.lookup(edge_id, pickPos/128);
				pos.edge=eid;
				pos.index=idx*128+pickPos%128;
			}
			if(pos.edge) {
				auto& e=reader.edges().at(pos.edge);
				if(cc_from_edge(pos.edge, pos.index/128))
					add_cc(mouse.sel_all);
			}
		}
		nodes_sel=std::move(sel_top);
		return true;
	}
	void updateRectSel(int x0, int y0, int x, int y) {
		int fbx0, fby0, fbx, fby;
		auto scaling=_scaling;
		fbx0=std::max(std::min(x0, x)/(1+scaling), 0);
		fby0=std::max(std::min(y0, y)/(1+scaling), 0);
		fbx=std::min(std::max(x0, x)/(1+scaling), fbo_width-1);
		fby=std::min(std::max(y0, y)/(1+scaling), fbo_height-1);
		auto& m=_mat[_view_mode==ViewMode::Global?CH_GLOBAL:CH_CLOSEUP];
		std::array<float, 2> xvals{(fbx0*2+1.0f)/fbo_width-1, (fbx*2+1.0f)/fbo_width-1};
		std::array<float, 2> yvals{1-(fby*2+1.0f)/fbo_height, 1-(fby0*2+1.0f)/fbo_height};
		std::array<PointGL, 4> buf;
		for(unsigned int k=0; k<4; ++k) {
			gapr::vec3<GLfloat> a{xvals[k<2?0:1], yvals[(k+1)%4<2?0:1], .99};
			a=m.mrView*(m.mrProj*a);
			gapr::node_attr pos=toGlobal(a);
			buf[k]=pos.data();
		}
		this->make_current();
		this->glBindBuffer(GL_ARRAY_BUFFER, _vao_man.buffer(pbiFixed));
		this->glBufferSubData(GL_ARRAY_BUFFER, 300*sizeof(PointGL), 4*sizeof(PointGL), buf.data());
		this->clear_current();
	}
	void clearSelection() {
		pickA=pickB=gapr::vec3<GLfloat>();
	}
	void edges_removed(const std::vector<edge_model::edge_id>& edges_del) {
		for(auto eid: edges_del) {
			auto it=_edge_vaoi.find(eid);
			if(it!=_edge_vaoi.end()) {
				auto vaoi=it->second;
				if(vaoi!=0)
					_vao_man.recycle(vaoi);
				_edge_vaoi.erase(it);
			}
		}
	}

	std::chrono::steady_clock::time_point prevt{};
	int prevc=0;
	bool _debug_fps{false};
	void printFPS() {
		auto nowt=std::chrono::steady_clock::now();
		prevc++;
		if(nowt-prevt>std::chrono::seconds{1}) {
			gapr::print(1, "FPS ", 1000.0*prevc/std::chrono::duration_cast<std::chrono::milliseconds>(nowt-prevt).count());
			prevt=nowt;
			prevc=0;
		}
	}

	private:
	void change_scale(int factor) {
		canvas_set_scaling(factor);
		_state_man.propagate();
	}
	
	void canvas_released(int btn, double _x, double _y) {
		gapr::print(1, "canvas release");
		int x=_x*_scale_factor;
		int y=_y*_scale_factor;
		auto& m=mouse;
		switch(m.mode) {
			case MouseMode::Rot:
				if(!m.moved /*&& _model*/) {
					// XXX
					Position pick_pos;
					if(pickPoint(x, y, pick_pos)) {
		gapr::print(1, "pick position");
						pick_position(pick_pos);
		gapr::print(1, "end pick position");
						//_priv->updateActions();
						//_priv->connectIfPossible();
					}
					m.mode=MouseMode::Nul;
					return;
				}
				break;
			case MouseMode::Pan:
				break;
			case MouseMode::Drag:
				//unsetCursor();
				//Q_EMIT endDrag();
				break;
			case MouseMode::RectSel:
				if(true /*&& _model*/) {
					if(!m.moved) {
						if(!_nodes_sel.empty()) {
							_nodes_sel.clear();
							_state_man.change(_state1_sel);
							_state_man.propagate();
						}
					} else {
						if(selectNodes(m.xPressed, m.yPressed, x, y, _nodes_sel)) {
							change_tgt_pos(Position{});
							_state_man.change(_state1_sel);
							_state_man.propagate();
						}
					}
				}
				break;
			case MouseMode::Nul:
				return;
		}
		m.mode=MouseMode::Nul;
	}
	void canvas_resize(int w, int h) {
		/* physical width/height */
		gapr::gl::check_error(this->glGetError(), "begin resize");
		_wid_width=w;
		_wid_height=h;
		resizeFrameBuffers();
		updateMVPMatrices<CH_GLOBAL>();
		updateMVPMatrices<CH_CLOSEUP>();
		//update();
		gapr::gl::check_error(this->glGetError(), "end resize");
	}
	void canvas_motion(double _x, double _y) {
		/*! logical coords */
		auto& m=mouse;
		int x=_x*_scale_factor;
		int y=_y*_scale_factor;
		if(m.x!=x || m.y!=y) {
			m.x=x;
			m.y=y;
			m.moved=true;
			switch(m.mode) {
				case MouseMode::Rot:
					canvas_set_directions(handleRotate());
					break;
				case MouseMode::Pan:
					//handlePan(x, y);
					break;
				case MouseMode::Drag:
					//handleDrag(x, y);
					break;
				case MouseMode::RectSel:
					if(true/* && _model*/) {
						updateRectSel(m.xPressed, m.yPressed, x, y);
						_state_man.change(_state1_sel_rect);
					}
					break;
				case MouseMode::Nul:
					return;
			}
		}
		_state_man.propagate();
	}
	void canvas_pressed(int btn, double _x, double _y, bool shift, bool ctrl, bool alt, bool single) {
		gapr::print(1, "canvas pressed");
		/*! logical coords */
		int x=_x*_scale_factor;
		int y=_y*_scale_factor;
		auto& m=mouse;
		m.xPressed=m.x=x;
		m.yPressed=m.y=y;
		m.moved=false;
		if(btn!=1) {
			m.mode=MouseMode::Pan;
			return;
		}
		if(alt) {
			if(!ctrl) {
				m.mode=MouseMode::RectSel;
				m.sel_all=shift;
				return;
			}
		}
		auto& mat=_mat[_view_mode==ViewMode::Global?CH_GLOBAL:CH_CLOSEUP];
		mrViewProj=mat.mrView*mat.mrProj;
		//gapr::print("mrViewProj", _priv->mrViewProj);
		auto& dir=_direction;
		_prev_up={(float)dir[0], (float)dir[1], (float)dir[2]};
		_prev_rgt={(float)dir[3], (float)dir[4], (float)dir[5]};
		m.mode=MouseMode::Rot;
	}
	void canvas_render() {
		if(!funcs_ok)
			return;
		gapr::print(1, "begin render");

		gapr::gl::check_error(this->glGetError(), "begin render");
		/*! reset by painter */
		this->glEnable(GL_BLEND);
		this->glEnable(GL_DEPTH_TEST);
		this->glClearDepth(1.0);
		auto bg_c=colors[C_BG];
		this->glClearColor(bg_c[0], bg_c[1], bg_c[2], 1.0);

		this->glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

		_state_man2.propagate();
		gapr::gl::check_error(this->glGetError(), "now render");

		if(_stage==SessionStage::Opened) {
		gapr::print(1, "begin edge");
			paintEdge();
		gapr::print(1, "end edge");
			paintOpaque();
			paintSurface();
			paintVolume();
			paintFinish();
		} else {
			paintBlank();
		}
		gapr::print(1, "end render");
	}
	void init_opengl(int scale, int width, int height) {
		/*! physical width/height */
		_scale_factor=scale;
		_wid_width=width;
		_wid_height=height;
		try {
			gapr::print(1, "init_opengl: ", scale, ' ', width, ' ', height);
			init_opengl_impl();
			gapr::gl::check_error(this->glGetError(), "end initgl");
		} catch(const std::system_error& e) {
			// XXX allow changing passwd when gl failed???
			// XXX map system_error to GError
			auto ec=e.code();
			gapr::print("canvas ready: ", ec.message());
			return this->opengl_error(ec, e.what());
		} catch(const std::runtime_error& e) {
			return this->opengl_error(e.what());
		}
		canvas_ready();
	}
	void deinit_opengl() {
		gapr::gl::check_error(this->glGetError(), "begin deinit opengl");
		fbo_pick.destroy();
		fbo_opaque.destroy();
		pick_vert.prog.destroy();
		pick_edge.prog.destroy();
		draw_vert.prog.destroy();
		draw_edge.prog.destroy();

		this->glBindBuffer(GL_ARRAY_BUFFER, 0);
		this->glBindVertexArray(0);
		_vao_man.destroy();
		//glDeleteBuffers(1, &vbo_progr);

		this->glActiveTexture(GL_TEXTURE0);
		this->glBindTexture(GL_TEXTURE_2D, 0);

		viewerShared->deinitialize();
		this->glBindFramebuffer(GL_FRAMEBUFFER, 0);
		gapr::gl::check_error(this->glGetError(), "end deinit opengl");
	}
	void canvas_scroll(double d) {
		if(_stage!=SessionStage::Opened)
			return;
		if(_slice_mode) {
			auto radU=_view_mode==ViewMode::Global?_global_zoom:_closeup_zoom;
			d=d/radU;
			if(d>0 && d<1) d=1;
			if(d<0 && d>-1) d=-1;
			_slice_delta+=d;
			_state_man.change(_state1_slice_delta);
		} else {
			auto dd=std::pow(0.96, d);
			if(_view_mode==ViewMode::Global) {
				canvas_set_global_zoom(_global_zoom*dd);
			} else {
				canvas_set_closeup_zoom(_closeup_zoom*dd);
			}
		}
		_state_man.propagate();
	}
	void highlight_reset() override {
		if(_state_man.get(_state2_can_hl)!=0)
			return;
		{
			assert(this->ui_executor().running_in_this_thread());
			edge_model::updater updater{_model};
			updater.reset_filter();
			if(!updater.apply())
				this->critical_error("Error", "model reset highlight error", "");
			canvas_model_changed(updater.edges_del());
		}

		_prelock_model.end_write_later();
		_state_man.change(_state1_model_lock);
		toggle_highlight(false);
		_state_man.propagate();
	}
	void highlight_neuron(int direction) {
		if(_state_man.get(_state2_can_hl)<=0)
			return;

		edge_model::edge_id edg{0};
		{
			edge_model::reader reader{_model};
			if(_tgt_pos.edge) {
				edg=_tgt_pos.edge;
			} else if(_tgt_pos.vertex) {
				auto& vert=reader.vertices().at(_tgt_pos.vertex);
				for(auto [eid, dir]: vert.edges) {
					if(_cur_pos.edge) {
						if(_cur_pos.edge==eid)
							edg=eid;
					} else if(_cur_pos.vertex) {
						auto& edg2=reader.edges().at(eid);
						if(_cur_pos.vertex==edg2.left || _cur_pos.vertex==edg2.right)
							edg=eid;
					}
				}
				if(edg==0 && !vert.edges.empty())
					edg=vert.edges[0].first;
			}
		}
		if(!edg)
			return;

		_prelock_model.begin_write_later();
		_state_man.change(_state1_model_lock);
		toggle_highlight(true);
		_state_man.propagate();

		gapr::print(1, "highlight neuron: ", edg, ' ', direction);
		gapr::promise<bool> prom{};
		auto fut=prom.get_future();
		auto ex1=this->thread_pool().get_executor();
		ba::post(ex1, [this,prom=std::move(prom),edg,ex1,direction]() mutable {
			assert(ex1.running_in_this_thread());
			edge_model::loader loader{_model};
			auto r=loader.highlight_neuron(edg, direction);
			return std::move(prom).set(r);
		});
		auto ex2=this->ui_executor();
		std::move(fut).async_wait(ex2, [this](gapr::likely<bool>&& res) {
			if(!res)
				this->critical_error("Error", "highlight loop error", "ec");
			if(!res.get())
				this->critical_error("Error", "highlight loop error", "");
			if(!model_apply(true))
				this->critical_error("Error", "model apply error", "");
			_state_man.propagate();
		});
	}
	void highlight_raised() override {
		if(_state_man.get(_state2_can_hl)<=0)
			return;

		{
			edge_model::edge_id edg{0};
			edge_model::reader reader{_model};
			if(!reader.raised())
				return;
		}

		_prelock_model.begin_write_later();
		_state_man.change(_state1_model_lock);
		toggle_highlight(true);
		_state_man.propagate();
		gapr::promise<bool> prom{};
		auto fut=prom.get_future();
		auto ex1=this->thread_pool().get_executor();
		ba::post(ex1, [this,prom=std::move(prom),ex1]() mutable {
			assert(ex1.running_in_this_thread());
			edge_model::loader loader{_model};
			auto r=loader.highlight_raised();
			return std::move(prom).set(r);
		});
		auto ex2=this->ui_executor();
		std::move(fut).async_wait(ex2, [this](gapr::likely<bool>&& res) {
			if(!res)
				this->critical_error("Error", "highlight raised error", "ec");
			if(!res.get())
				this->critical_error("Error", "highlight raised error", "");
			if(!model_apply(true))
				this->critical_error("Error", "model apply error", "");
			_state_man.propagate();
		});
	}
	void highlight_orphan() {
		if(_state_man.get(_state2_can_hl)<=0)
			return;

		edge_model::edge_id edg{0};
		{
			edge_model::reader reader{_model};
			if(_tgt_pos.edge) {
				edg=_tgt_pos.edge;
			} else if(_tgt_pos.vertex) {
				auto& vert=reader.vertices().at(_tgt_pos.vertex);
				for(auto [eid, dir]: vert.edges) {
					if(_cur_pos.edge) {
						if(_cur_pos.edge==eid)
							edg=eid;
					} else if(_cur_pos.vertex) {
						auto& edg2=reader.edges().at(eid);
						if(_cur_pos.vertex==edg2.left || _cur_pos.vertex==edg2.right)
							edg=eid;
					}
				}
				if(edg==0 && !vert.edges.empty())
					edg=vert.edges[0].first;
			}
			if(!edg)
				return;
			if(auto& e=reader.edges().at(edg); e.root)
				return;
		}

		_prelock_model.begin_write_later();
		_state_man.change(_state1_model_lock);
		toggle_highlight(true);
		_state_man.propagate();

		gapr::promise<bool> prom{};
		auto fut=prom.get_future();
		auto ex1=this->thread_pool().get_executor();
		ba::post(ex1, [this,prom=std::move(prom),edg,ex1]() mutable {
			assert(ex1.running_in_this_thread());
			edge_model::loader loader{_model};
			auto r=loader.highlight_orphan(edg);
			return std::move(prom).set(r);
		});
		auto ex2=this->ui_executor();
		std::move(fut).async_wait(ex2, [this](gapr::likely<bool>&& res) {
			if(!res)
				this->critical_error("Error", "highlight orphan error", "ec");
			if(!res.get())
				this->critical_error("Error", "highlight orphan error", "");
			if(!model_apply(true))
				this->critical_error("Error", "model apply error", "");
			_state_man.propagate();
		});
	}
	void highlight_loop() {
		if(_state_man.get(_state2_can_hl)<=0)
			return;

		edge_model::edge_id edg{0};
		{
			edge_model::reader reader{_model};
			if(_tgt_pos.edge) {
				edg=_tgt_pos.edge;
			} else if(_tgt_pos.vertex) {
				auto& vert=reader.vertices().at(_tgt_pos.vertex);
				for(auto [eid, dir]: vert.edges) {
					if(_cur_pos.edge) {
						if(_cur_pos.edge==eid)
							edg=eid;
					} else if(_cur_pos.vertex) {
						auto& edg2=reader.edges().at(eid);
						if(_cur_pos.vertex==edg2.left || _cur_pos.vertex==edg2.right)
							edg=eid;
					}
				}
				if(edg==0 && !vert.edges.empty())
					edg=vert.edges[0].first;
			}
		}

		_prelock_model.begin_write_later();
		_state_man.change(_state1_model_lock);
		toggle_highlight(true);
		_state_man.propagate();

		gapr::print(1, "highlight loop: ", edg);
		gapr::promise<bool> prom{};
		auto fut=prom.get_future();
		auto ex1=this->thread_pool().get_executor();
		ba::post(ex1, [this,prom=std::move(prom),edg,ex1]() mutable {
			assert(ex1.running_in_this_thread());
			edge_model::loader loader{_model};
			auto r=loader.highlight_loop(edg);
			return std::move(prom).set(r);
		});
		auto ex2=this->ui_executor();
		std::move(fut).async_wait(ex2, [this](gapr::likely<bool>&& res) {
			if(!res)
				this->critical_error("Error", "highlight loop error", "ec");
			if(!res.get())
				this->critical_error("Error", "highlight loop error", "");
			if(!model_apply(true))
				this->critical_error("Error", "model apply error", "");
			_state_man.propagate();
		});
	}
	void select_closeup_changed(unsigned int map_g) override {
		if(map_g==_closeup_ch)
			return;
		if(_closeup_ch) {
			auto [min, max]=this->ui_closeup_xfunc_minmax();
			_xfunc_states[_closeup_ch][2]=min;
			_xfunc_states[_closeup_ch][3]=max;
		}
		_closeup_ch=map_g;
		if(_closeup_ch) {
			this->ui_enable_closeup_xfunc(_xfunc_states[_closeup_ch-1]);
			auto& info=_cube_infos[_closeup_ch-1];
			canvas_set_closeup_info();
			canvas_set_closeup_xfunc(calc_xfunc(_closeup_ch));
			//??**void set_closeup_cube(gapr::cube cube, std::string&& uri);
			startDownloadCube();
		} else {
			this->ui_disable_closeup_xfunc();
		}
	}
	void select_global_changed(unsigned int map_g) override {
		if(map_g==_global_ch)
			return;
		if(_global_ch) {
			auto [min, max]=this->ui_global_xfunc_minmax();
			_xfunc_states[_global_ch][2]=min;
			_xfunc_states[_global_ch][3]=max;
		}
		_global_ch=map_g;
		if(_global_ch) {
			this->ui_enable_global_xfunc(_xfunc_states[_global_ch-1]);
			auto& info=_cube_infos[_global_ch-1];
			canvas_set_global_info();
			canvas_set_global_xfunc(calc_xfunc(_global_ch));
			//??**void set_global_cube(gapr::cube cube, std::string&& uri);
			startDownloadCube();
		} else {
			this->ui_disable_global_xfunc();
		}
	}
	void set_view_mode(ViewMode mode) {
		_view_mode=mode;
		startDownloadCube();
		this->ui_xfunc_set_default(_view_mode!=ViewMode::Global);
		canvas_set_view_mode();
		_state_man.propagate();
	}
	void xfunc_global_changed(double low, double up) {
		switch(_view_mode) {
			case ViewMode::Global:
			case ViewMode::Mixed:
				if(_global_ch) {
					_xfunc_states[_global_ch-1][0]=low;
					_xfunc_states[_global_ch-1][1]=up;
					canvas_set_global_xfunc(calc_xfunc(_global_ch));
				}
				break;
			default:
				break;
		}
		_state_man.propagate();
	}
	void xfunc_closeup_changed(double low, double up) {
		switch(_view_mode) {
			case ViewMode::Closeup:
			case ViewMode::Mixed:
				if(_closeup_ch) {
					_xfunc_states[_closeup_ch-1][0]=low;
					_xfunc_states[_closeup_ch-1][1]=up;
					canvas_set_closeup_xfunc(calc_xfunc(_closeup_ch));
				}
				break;
			default:
				break;
		}
		_state_man.propagate();
	}
	void connect() {
		auto conn=client_end{this->io_context().get_executor(), this->ssl_context()};
		if(!_addr.port())
			return range_connect(_addrs.begin(), std::move(conn));

		gapr::print("connect");
		conn.async_connect(_addr, [this,conn](bs::error_code ec) mutable {
			if(ec) {
				gapr::str_glue err{"Unable to connect to [", _addr, "]."};
				post_ask_retry(err.str(), ec.message(), {});
				return;
			}
			handshake(std::move(conn));
		});
	}
	void got_passwd(std::string&& pw) override {
		assert(this->ui_executor().running_in_this_thread());

		/* empty: canceled */
		if(pw.empty()) {
			// XXX clean up
			change_stage(SessionStage::Closed);
			_conn_need_pw=gapr::client_end{};
			_state_man.propagate();
			return;
		}

		ba::post(this->io_context(), [this,pw=std::move(pw)]() mutable {
			passwd(std::move(pw));
			if(_conn_need_pw)
				login(std::move(_conn_need_pw));
			else
				connect();
		});
	}
	void goto_target() override {
		////////////////////////
		/////
		//not SessionState::Invalid: SessionState::LoadingCatalog: SessionState::Readonly:
		if(!_tgt_pos.valid())
			return;
		edge_model::reader reader{_model};
		jumpToPosition(_tgt_pos, reader);
		update_description(reader);
		//_priv->updateActions();
		startDownloadCube();
	}
	void toggle_data_only(bool state) {
		canvas_set_data_only(state);
		_state_man.propagate();
	}
	void toggle_slice(bool state) {
		canvas_set_slice_mode(state);
		_state_man.propagate();
	}
	void change_total_slices(int value) {
		unsigned int v1=value;
		auto v0=_slice_pars[0];
		if(v0>v1) {
			v0=v1;
			this->ui_adjust_shown_slices(v0);
		}
		canvas_set_slice_pars({v0, v1});
		_state_man.propagate();
	}
	void change_shown_slices(int value) {
		unsigned int v0=value;
		auto v1=_slice_pars[1];
		if(v1<v0) {
			v1=v0;
			this->ui_adjust_total_slices(v1);
		}
		canvas_set_slice_pars({v0, v1});
		_state_man.propagate();
	}
	void login() override {
		gapr::str_glue title{args().user, '@', args().host, ':', args().port, '/', args().group, "[*]"};
		this->window_title(title.str());
		change_stage(SessionStage::Opening);
		_state_man.propagate();
		ba::post(this->io_context(), [this]() {
			start();
		});
	}
	void canvas_scale_factor_changed(int scale_factor) {
		assert(_state_man.changes()==0);
		set_scale_factor(scale_factor);
		this->canvas_update();
	}
	void retry_connect() override {
		ba::post(this->io_context(), [this]() { connect(); });
	}
	void activate_global_view() override {
		set_view_mode(ViewMode::Global);
	}
	void activate_closeup_view() override {
		set_view_mode(ViewMode::Mixed);
	}

	struct ConnectSt {
		std::atomic<bool> cancel_flag{false};
		Position cur_pos, tgt_pos;
		unsigned int method;
	};
	std::shared_ptr<ConnectSt> _prev_conn;
	void tracing_connect() override {
		if(!_state_man.get(_state2_can_connect))
			return;

		ConnectAlg alg{&_model, _cur_pos, _tgt_pos};
		if(_view_mode==ViewMode::Global) {
			alg.args.cube=_global_cube;
			alg.args.offset={0, 0, 0};
			alg.args.xform=&_cube_infos[_global_ch-1].xform;
			alg.args.xfunc=calc_xfunc(_global_ch);
		} else {
			alg.args.cube=_closeup_cube;
			alg.args.offset=_closeup_offset;
			alg.args.xform=&_cube_infos[_closeup_ch-1].xform;
			alg.args.xfunc=calc_xfunc(_closeup_ch);
		}
		if(_prev_conn) {
			if(alg.args.cur_pos==_prev_conn->cur_pos && alg.args.tgt_pos==_prev_conn->tgt_pos)
				alg.args.method=_prev_conn->method+1;
			_prev_conn->cancel_flag=true;
		}
		auto cur_conn=std::make_shared<ConnectSt>();
		cur_conn->method=alg.args.method;
		cur_conn->cur_pos=alg.args.cur_pos;
		cur_conn->tgt_pos=alg.args.tgt_pos;
		alg.args.cancel=&cur_conn->cancel_flag;

		_prelock_model.begin_read_async();
		_prev_conn=cur_conn;
		_state_man.change({_state1_model_lock, _state1_path_lock});
		_state_man.propagate();

		gapr::promise<gapr::delta_add_edge_> prom;
		auto fut=prom.get_future();
		ba::post(this->thread_pool(), [prom=std::move(prom),alg=std::move(alg)]() mutable {
			try {
				std::move(prom).set(alg.compute());
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});

		std::move(fut).async_wait(this->ui_executor(), [this,cur_conn](gapr::likely<gapr::delta_add_edge_>&& res) {
			//XXX if(_priv->has_binding_to_gui) ...;
			if(!cur_conn->cancel_flag.load())
			do {
				if(!res) try {
					auto ec=res.error_code();
					std::cerr<<ec.message()<<'\n';
					break;
				} catch(const std::runtime_error& e) {
					std::cerr<<e.what()<<'\n';
					break;
				}
				canvas_set_path_data(std::move(res.get()));
			} while(false);
			//priv->viewer->setProgressData({});

			_prelock_model.end_read_async();
			_state_man.change({_state1_model_lock, _state1_path_lock});
			_state_man.propagate();
		});
	}
	void goto_position(std::string_view pos_str) override {
		if(!_state_man.get(_state2_can_goto_pos))
			return;
		do {
			if(pos_str.empty())
				return;
			edge_model::reader reader{_model};
			const char* p=&pos_str[0];
			auto last=&pos_str[0]+pos_str.size();
			if(*p=='@') {
				p++;
				gapr::node_id nid;
				auto [eptr, ec]=std::from_chars(p, last, nid.data);
				if(ec!=std::errc{} || eptr!=last)
					break;
				auto it=reader.nodes().find(nid);
				if(it==reader.nodes().end())
					break;
				if(it->second.edge) {
					auto& edg=reader.edges().at(it->second.edge);
					jumpToPosition(Position{it->second.edge, it->second.index, edg.points[it->second.index/128]}, reader);
				} else {
					if(!it->second.vertex)
						break;
					auto& vert=reader.vertices().at(it->second.vertex);
					jumpToPosition(Position{it->second.vertex, vert.attr.data()}, reader);
				}
			} else if(*p=='(') {
				p++;
				std::array<double, 3> pt;
				bool err{false};
				auto from_chars=[](const char* first, const char* last, double& value) {
					char* end;
					value=std::strtod(first, &end);
					if(end==first)
						return std::from_chars_result{end, std::errc::invalid_argument};
					return std::from_chars_result{end, std::errc{}};
				};
				for(unsigned int i=0; i<3; i++) {
					auto [eptr, ec]=from_chars(p, last, pt[i]);//, std::chars_format::fixed);
					if(ec!=std::errc{}) {
						err=true;
						break;
					}
					if(i<2) {
						if(eptr>=last || *eptr!=',')
							err=true;
						p=eptr+1;
					} else {
						if(eptr+1!=last || *eptr!=')')
							err=true;
					}
					if(err)
						break;
				}
				if(err)
					break;
				gapr::node_attr pos{pt[0], pt[1], pt[2]};
				jumpToPosition(Position{pos.data()}, reader);
			} else {
				break;
			}
			update_description(reader);
			//_priv->updateActions();
			startDownloadCube();
			return;
		} while(false);
		gapr::print(1, "wrong input");
	}
	void create_neuron(std::string&& name) override {
		if(!_state_man.get(_state2_can_create_nrn))
			return;
		auto fut=start_neuron_create(std::move(name));
		if(!fut)
			return;
		std::move(fut).async_wait(this->ui_executor(), [this](auto&& res) {
			finish_neuron_create(std::move(res));
		});
	}
	void rename_neuron(std::string&& name) override {
		if(!_state_man.get(_state2_can_rename_nrn))
			return;
		auto fut=start_neuron_rename(std::move(name));
		if(!fut)
			return;
		std::move(fut).async_wait(this->ui_executor(), [this](auto&& res) {
			finish_neuron_rename(std::move(res));
		});
	}
	void remove_neuron() override {
		if(!_state_man.get(_state2_can_remove_nrn))
			return;

		_prelock_model.begin_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();
		auto nid=_list_sel;
		//???list_ptr: point to created neuron
		gapr::fiber fib{this->io_context().get_executor(), [this,nid](gapr::fiber_ctx& ctx) {
			gapr::delta_del_patch_ delta;
			delta.props.emplace_back(nid.data, "root");
			return submit_commit(ctx, std::move(delta));
		}};
		auto fut=fib.get_future();
		std::move(fut).async_wait(this->ui_executor(), [this](auto&& res) {
			if(!res)
				this->critical_error("Error", "load commit error", "");
			if(!model_apply(true))
				this->critical_error("Error", "model apply error", "");
			switch(res.get().first) {
				case SubmitRes::Deny:
					if(!res.get().second.empty())
						this->critical_error("Error", res.get().second, "");
					break;
				case SubmitRes::Accept:
					break;
				case SubmitRes::Retry:
					break;
			}
			_prelock_model.end_write_later();
			_state_man.change(_state1_model_lock);
			_state_man.propagate();
		});
	}
	void tracing_extend() override {
		auto fut=start_extend();
		gapr::print("start_extend");
		if(!fut)
			return;
		std::move(fut).async_wait(this->ui_executor(), [this](auto&& res) {
			gapr::print("finish_extend");
			finish_extend(std::move(res));
		});
	}
	void tracing_branch() override {
		auto fut=start_branch();
		gapr::print("start_branch");
		if(!fut)
			return;
		std::move(fut).async_wait(this->ui_executor(), [this](auto&& res) {
			gapr::print("finish_branch");
			finish_extend(std::move(res));
		});
	}
	void tracing_attach(int typ) override {
		if(!_state_man.get(_state2_can_attach))
			return;
		auto fut=start_attach(typ);
		if(!fut)
			return;
		std::move(fut).async_wait(this->ui_executor(), [this](auto&& res) {
			finish_attach(std::move(res));
		});
	}
	void tracing_end() override {
		auto fut=start_end_as("end");
		if(!fut)
			return;
		std::move(fut).async_wait(this->ui_executor(), [this](auto&& res) {
			finish_terminal(std::move(res));
		});
	}
	void tracing_end_as(std::string&& st) override {
		if(!_state_man.get(_state2_can_end))
			return;
		auto fut=start_end_as(std::move(st));
		if(!fut)
			return;
		std::move(fut).async_wait(this->ui_executor(), [this](auto&& res) {
			finish_terminal(std::move(res));
		});
	}
	void tracing_delete() override {
		auto fut=start_delete();
		if(!fut)
			return;
		std::move(fut).async_wait(this->ui_executor(), [this](auto&& res) {
			finish_delete(std::move(res));
		});
	}
	void tracing_examine() override {
		auto fut=start_examine();
		if(!fut)
			return;
		std::move(fut).async_wait(this->ui_executor(), [this](auto&& res) {
			finish_examine(std::move(res));
		});
	}
	void view_refresh() override {
		startDownloadCube();
		//reload meshes

		auto fut=start_refresh();
		gapr::print("start_refresh");
		if(!fut)
			return;
		std::move(fut).async_wait(this->ui_executor(), [this](auto&& res) {
			gapr::print("finish_refresh");
			finish_refresh(std::move(res));
		});
	}
	void update_details() override {
		if(!_prelock_model.can_read_async())
			return;
		_prelock_model.begin_read_async();
		_state_man.change(_state1_model_lock);
		ba::post(this->thread_pool(), [this]() {
			gapr::edge_model::reader reader{_model};
			auto details=get_graph_details(reader);
			ba::post(this->ui_executor(), [this,str=std::move(details)]() {
				_prelock_model.end_read_async();
				_state_man.change(_state1_model_lock);
				this->ui_set_detailed_statistics(str);
			});
		});
	}
	void select_node(gapr::node_id nid) override {
		_list_sel=nid;
		_state_man.change(_state1_list_sel);
		// XXX
		if(_user_sel)
			ba::post(this->ui_executor(), [this,nid]() {
				edge_model::reader graph{_model};
				auto vert_it=graph.vertices().find(nid);
				if(vert_it==graph.vertices().end())
					return;
				auto& vert=vert_it->second;
				target_changed(Position{nid, vert.attr.data()}, graph);
				update_description(graph);
				_state_man.propagate();
			});
		_state_man.propagate();
	}

	void save_img(std::string&& file) override {
		gapr::cube_info* info;
		gapr::cube cube;
		switch(_view_mode) {
			case ViewMode::Global:
				info=&_cube_infos[_global_ch-1];
				cube=_global_cube;
				break;
			case ViewMode::Closeup:
			case ViewMode::Mixed:
				info=&_cube_infos[_closeup_ch-1];
				cube=_closeup_cube;
				break;
		}
		if(!cube || !info)
			return;

		gapr::promise<int> prom;
		auto fut=prom.get_future();
		ba::post(this->thread_pool(), [prom=std::move(prom),cube=std::move(cube),info,file=std::move(file)]() mutable {
			try {
				std::ofstream fs{file, std::ios_base::binary};
				if(!fs)
					throw std::runtime_error{"Cannot open file"};
				gapr::nrrd_output nrrd{fs};
				nrrd.header();
				nrrd.finish(std::move(cube), &info->xform);
				fs.close();
				if(!fs)
					throw std::runtime_error{"Failed to close file."};
				std::move(prom).set(0);
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});
		std::move(fut).async_wait(this->ui_executor(), [this](gapr::likely<int>&& res) {
			if(!res) try {
				auto ec=res.error_code();
				return this->ui_message(ec.message());
			} catch(const std::runtime_error& e) {
				return this->ui_message(e.what());
			}
			this->ui_message("Image saved successfully");
		});
	}

	void goto_next_error() override {
		if(!_state_man.get(_state2_can_goto_pos/*can_goto_err*/))
			return;
		edge_model::reader reader{_model};
		gapr::node_id nid;
		if(_cur_pos.edge) {
			auto& edg=reader.edges().at(_cur_pos.edge);
			nid=edg.nodes[_cur_pos.index/128];
		} else if(_cur_pos.vertex) {
			nid=_cur_pos.vertex;
		}
		nid=find_next_error(reader, nid, gapr::node_attr{_cur_pos.point});
		if(!nid)
			return;
		auto pos=reader.nodes().at(nid);
		if(pos.edge) {
			auto& edg=reader.edges().at(pos.edge);
			jumpToPosition(Position{pos.edge, pos.index, edg.points[pos.index/128]}, reader);
		} else {
			assert(pos.vertex);
			auto& vert=reader.vertices().at(pos.vertex);
			jumpToPosition(Position{pos.vertex, vert.attr.data()}, reader);
		}
		update_description(reader);
		//_priv->updateActions();
		startDownloadCube();
	}
	void clear_end_state() override {
		if(!_state_man.get(_state2_can_clear_end))
			return;

		gapr::node_id nid{};
		{
			edge_model::reader reader{_model};
			if(_tgt_pos.edge) {
				auto& edg=reader.edges().at(_tgt_pos.edge);
				nid=edg.nodes[_tgt_pos.index/128];
			} else if(_tgt_pos.vertex) {
				nid=_tgt_pos.vertex;
			} else {
				return;
			}
			if(!nid)
				return;
			gapr::prop_id k{nid, "state"};
			if(reader.props().find(k)==reader.props().end())
				return;
		}

		_prelock_model.begin_write_later();
		_state_man.change(_state1_model_lock);
		_state_man.propagate();
		gapr::fiber fib{this->io_context().get_executor(), [this,nid](gapr::fiber_ctx& ctx) {
			gapr::delta_del_patch_ delta;
			delta.props.emplace_back(nid.data, "state");
			return submit_commit(ctx, std::move(delta));
		}};
		auto fut=fib.get_future();
		std::move(fut).async_wait(this->ui_executor(), [this](auto&& res) {
			if(!res)
				this->critical_error("Error", "load commit error", "");
			if(!model_apply(true))
				this->critical_error("Error", "model apply error", "");
			switch(res.get().first) {
				case SubmitRes::Deny:
					if(!res.get().second.empty())
						this->critical_error("Error", res.get().second, "");
					break;
				case SubmitRes::Accept:
					break;
				case SubmitRes::Retry:
					break;
			}
			_prelock_model.end_write_later();
			_state_man.change(_state1_model_lock);
			_state_man.propagate();
		});
	}
	void resolve_error(std::string_view state) override {
		if(!_state_man.get(_state2_can_resolve_err))
			return;
		auto fut=start_resolve_error(state);
		if(!fut)
			return;
		std::move(fut).async_wait(this->ui_executor(), [this](auto&& res) {
			finish_resolve_error(std::move(res));
		});
	}
	void report_error(std::string_view state) override {
		if(!_state_man.get(_state2_can_report_err))
			return;
		auto fut=start_report_error(state);
		if(!fut)
			return;
		std::move(fut).async_wait(this->ui_executor(), [this](auto&& res) {
			finish_report_error(std::move(res));
		});
	}
	void raise_node() override {
		if(!_state_man.get(_state2_can_raise_node))
			return;
		auto fut=start_raise_node();
		if(!fut)
			return;
		std::move(fut).async_wait(this->ui_executor(), [this](auto&& res) {
			finish_raise_node(std::move(res));
		});
	}

	void select_noise(std::string_view spec) override {
		unsigned int cnt;
		double len;
		if(2!=sscanf(spec.data(), "%u %lf", &cnt, &len)) {
			gapr::print(1, "wrong input");
			return;
		}

		std::vector<std::pair<gapr::node_id, gapr::node_attr>> nodes_to_del;
		std::size_t commited=0;
		gapr::edge_model::reader model{_model};
		for(auto& [eid, edg]: model.edges()) {
			if(edg.root!=gapr::node_id{})
				continue;
			if(edg.raised)
				continue;
			auto& v0=model.vertices().at(edg.left);
			auto& v1=model.vertices().at(edg.right);
			if(v0.edges.size()>1 && v1.edges.size()>1)
				continue;
			if(edg.points.size()>cnt)
				continue;
			if(model.props().find({edg.left, "state"})!=model.props().end())
				continue;
			if(model.props().find({edg.right, "state"})!=model.props().end())
				continue;
			nodes_to_del.resize(commited);
			unsigned int has_pr=0;
			double totlen=0.0;
			gapr::vec3<double> prev_pos;
			for(std::size_t i=0; i<edg.nodes.size(); ++i) {
				gapr::node_attr attr{edg.points[i]};
				if(attr.misc.coverage())
					++has_pr;
				nodes_to_del.emplace_back(edg.nodes[i], attr);
				auto pos=attr.pos();
				if(i>0)
					totlen+=(pos-prev_pos).mag();
				else if(v0.edges.size()>1)
					nodes_to_del.pop_back();
				prev_pos=pos;
			}
			if(has_pr)
				continue;
			if(totlen>len)
				continue;
			if(v1.edges.size()>1)
				nodes_to_del.pop_back();
			commited=nodes_to_del.size();
		}
		nodes_to_del.resize(commited);
		change_tgt_pos(Position{});
		_nodes_sel=std::move(nodes_to_del);
		_state_man.change(_state1_sel);
		_state_man.propagate();
	}

	void set_autosel_len(double len) override {
		_autosel_len=len;
	}

};

template<typename Base, typename Funcs>
void gapr::fix::SessionImpl<Base, Funcs>::initialize_states() {
	_state2_opened=_state_man.add_secondary({_state1_stage}, [this]() {
		return _stage==SessionStage::Opened?1:0;
	});
	_state2_can_edit0=_state_man.add_secondary({_state1_stage, _state1_hl_mode}, {_state2_opened}, [this](int opened) ->int {
		if(!opened)
			return 0;
		return !_in_highlight;
	});
	_state2_can_edit=_state_man.add_secondary({_state1_model_lock}, {_state2_can_edit0}, [this](int can_edit0) ->int {
		if(!can_edit0)
			return 0;
		if(!_prelock_model.can_write_later())
			return 0;
		return 1;
	});
	_state2_can_hl=_state_man.add_secondary({_state1_hl_mode, _state1_model_lock}, {_state2_opened}, [this](int opened) {
		if(!opened)
			return -1;
		if(_in_highlight)
			return 0;
		if(!_prelock_model.can_write_later())
			return -1;
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_hl}, [this](int v) {
		this->ui_enable_view_highlight(v);
	});
	_state_man.add_codes({_state1_model_lock}, {_state2_opened}, [this](int opened) {
		this->ui_canvas_cursor(opened, _prelock_model.can_write_now());
	});
	_state2_can_goto_pos=_state2_opened;
	_state_man.add_codes({}, {_state2_can_goto_pos}, [this](int v) {
		this->ui_enable_goto_position(v);
	});
	_state2_can_goto_tgt=_state_man.add_secondary({_state1_tgt_pos}, {_state2_opened}, [this](int opened) {
		if(!opened)
			return 0;
		if(!_tgt_pos.valid())
			return 0;
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_goto_tgt}, [this](int v) {
		this->ui_enable_goto_target(v);
	});
	_state2_can_pick_cur=_state_man.add_secondary({_state1_cur_pos}, {_state2_opened}, [this](int opened) {
		if(!opened)
			return 0;
		if(!_cur_pos.valid())
			return 0;
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_pick_cur}, [this](int v) {
		this->ui_enable_pick_current(v);
	});
	_state2_can_create_nrn=_state_man.add_secondary({_state1_tgt_pos}, {_state2_can_edit}, [this](int can_edit) {
		// cur_conn
		if(!can_edit)
			return 0;
		if(!_tgt_pos.valid())
			return 0;
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_create_nrn}, [this](int v) {
		this->ui_enable_create_neuron(v);
	});
	_state2_can_rename_nrn=_state_man.add_secondary({_state1_list_sel}, {_state2_can_edit}, [this](int can_edit) {
		if(!can_edit)
			return 0;
		// cur_conn
		if(_list_sel==gapr::node_id{})
			return 0;
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_rename_nrn}, [this](int v) {
		this->ui_enable_rename_neuron(v);
	});
	_state2_can_remove_nrn=_state_man.add_secondary({_state1_list_sel}, {_state2_can_edit}, [this](int can_edit) {
		if(!can_edit)
			return 0;
		// cur_conn
		if(_list_sel==gapr::node_id{})
			return 0;
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_remove_nrn}, [this](int v) {
		this->ui_enable_remove_neuron(v);
	});
	_state2_can_clear_end=_state_man.add_secondary({_state1_tgt_pos}, {_state2_can_edit}, [this](int can_edit) {
		if(!can_edit)
			return 0;
		if(!_tgt_pos.valid())
			return 0;
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_clear_end}, [this](int v) {
		this->ui_enable_clear_end(v);
	});
	_state2_can_resolve_err=_state_man.add_secondary({_state1_tgt_pos}, {_state2_can_edit}, [this](int can_edit) {
		// cur_conn
		if(!can_edit)
			return 0;
		if(!_tgt_pos.valid())
			return 0;
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_resolve_err}, [this](int v) {
		this->ui_enable_resolve_error(v);
	});
	_state2_can_report_err=_state_man.add_secondary({_state1_tgt_pos}, {_state2_can_edit}, [this](int can_edit) {
		// cur_conn
		if(!can_edit)
			return 0;
		if(!_tgt_pos.valid())
			return 0;
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_report_err}, [this](int v) {
		this->ui_enable_report_error(v);
	});
	_state2_can_raise_node=_state_man.add_secondary({_state1_tgt_pos}, {_state2_can_edit}, [this](int can_edit) {
		// cur_conn
		if(!can_edit)
			return 0;
		if(!_tgt_pos.valid())
			return 0;
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_raise_node}, [this](int v) {
		this->ui_enable_raise_node(v);
	});

	_state2_can_connect=_state_man.add_secondary({_state1_loading, _state1_model_lock, _state1_path_lock, _state1_tgt_pos, _state1_cur_pos, _state1_view_mode, _state1_closeup_cube, _state1_global_cube}, {_state2_can_edit0}, [this](int can_edit) {
		if(!can_edit)
			return 0;
		if(!_prelock_model.can_read_async())
			return 0;
		if(!_prelock_path.can_write_later())
			return 0;
		if(!_cur_pos.valid())
			return 0;
		if(!_tgt_pos.valid())
			return 0;
		switch(_view_mode) {
			case ViewMode::Global:
				if(!_global_cube)
					return 0;
				if(_loading_global)
					return 0;
				// XXX in cube
				break;
			case ViewMode::Closeup:
			case ViewMode::Mixed:
				if(!_closeup_cube)
					return 0;
				if(_loading_closeup)
					return 0;
				// XXX in cube
				break;
			default:
				return 0;
		}
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_connect}, [this](int v) {
		this->ui_enable_tracing_connect(v);
	});
	_state2_can_extend=_state_man.add_secondary({_state1_path_lock, _state1_path}, {_state2_can_edit}, [this](int can_edit) {
		if(!can_edit)
			return 0;
		// cur_conn
		if(!_prelock_path.can_write_later())
			return 0;
		if(_path.nodes.size()<2)
			return 0;
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_extend}, [this](int v) {
		this->ui_enable_tracing_extend(v);
		this->ui_enable_tracing_branch(v);
	});
	_state2_can_attach=_state_man.add_secondary({_state1_path_lock, _state1_path}, {_state2_can_edit}, [this](int can_edit) {
		if(!can_edit)
			return 0;
		// cur_conn
		if(!_prelock_path.can_write_later())
			return 0;
		if(_path.nodes.size()<2)
			return 0;
		if(gapr::link_id r{_path.right}; !r.cannolize())
			return 0;
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_attach}, [this](int v) {
		this->ui_enable_tracing_attach(v);
	});
	_state2_can_end=_state_man.add_secondary({}, {_state2_can_edit}, [this](int can_edit) {
		if(!can_edit)
			return 0;
		// cur_conn
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_end}, [this](int v) {
		this->ui_enable_tracing_end(v);
		this->ui_enable_tracing_end_as(v);
	});
	_state2_can_delete=_state_man.add_secondary({_state1_model, _state1_cur_pos, _state1_tgt_pos, _state1_sel}, {_state2_can_edit}, [this](int can_edit) {
		if(!can_edit)
			return 0;
		// cur_conn
		if(get_sub_edge().edge)
			return 1;
		if(!_nodes_sel.empty())
			return 1;
		return 0;
	});
	_state_man.add_codes({}, {_state2_can_delete}, [this](int v) {
		this->ui_enable_tracing_delete(v);
	});
	_state2_can_examine=_state_man.add_secondary({_state1_model, _state1_cur_pos, _state1_tgt_pos, _state1_sel}, {_state2_can_edit}, [this](int can_edit) {
		if(!can_edit)
			return 0;
		if(get_sub_edge().edge)
			return 1;
		if(!_nodes_sel.empty())
			return 1;
		return 0;
	});
	_state_man.add_codes({}, {_state2_can_examine}, [this](int v) {
		this->ui_enable_tracing_examine(v);
	});
	_state2_can_open_prop=_state2_opened;
	_state_man.add_codes({}, {_state2_can_open_prop}, [this](int v) {
		this->ui_enable_file_props(v);
	});
	_state2_can_hide_edge=_state2_opened;
	_state_man.add_codes({}, {_state2_can_hide_edge}, [this](int v) {
		this->ui_enable_view_data_only(v);
	});
	_state2_can_show_slice=_state2_opened;
	_state_man.add_codes({}, {_state2_can_show_slice}, [this](int v) {
		this->ui_enable_view_slice(v);
	});
	_state2_can_chg_view=_state_man.add_secondary({ }, {_state2_opened}, [this](int opened) {
		if(!opened)
			return 0;
		int r=0;
		if(_has_global_ch)
			r|=1;
		if(_has_closeup_ch)
			r|=2;
		return r;
	});
	_state_man.add_codes({}, {_state2_can_chg_view}, [this](int v) {
		this->ui_enable_view_mode(v);
		this->ui_enable_view_channels(v);
	});
	_state2_can_close=_state_man.add_secondary({_state1_stage}, [this]() {
		if(_stage==SessionStage::Closed)
			return 0;
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_close}, [this](int v) {
		this->ui_enable_file_close(v);
	});
	_state_man.add_codes({_state1_stage}, [this]() {
		this->ui_enable_file_open(_stage==SessionStage::Closed);
	});
	_state2_can_refresh=_state_man.add_secondary({_state1_model_lock}, {_state2_opened}, [this](int opened) {
		if(!opened)
			return 0;
		// cur_conn
		if(!_prelock_model.can_write_later())
			return 0;
		return 1;
	});
	_state_man.add_codes({}, {_state2_can_refresh}, [this](int v) {
		this->ui_enable_view_refresh(v);
	});
	_state_man.add_codes({}, {_state2_can_edit}, [this](int v) {
		this->ui_indicate_can_edit(v);
	});
	_state_man.add_codes({_state1_loading}, [this]() {
		this->ui_show_progress(_loading_global||_loading_closeup);
	});

	{
		_state_man.add_codes({_state1_slice_mode}, {_state2_opened}, [this](int opened) {
			if(!opened)
				return;
			if(_slice_mode)
				_slice_delta=0;
		});
		_state_man.add_codes({_state1_slice_delta}, {_state2_opened}, [this](int opened) {
			if(!opened)
				return;
			auto slice_delta=_slice_delta;
			int n=_slice_pars[1];
			if(slice_delta<-n) _slice_delta=-n;
			if(slice_delta>n) _slice_delta=n;
		});
		_state_man.add_codes({_state1_closeup_info, _state1_direction}, {_state2_opened}, [this](int opened) {
			if(!opened || !_has_closeup_ch)
				return;
			updateInsetCenter();
			updateMVPMatrices<CH_INSET>();
			updateZoomMinMax<CH_CLOSEUP>();
		});
		_state_man.add_codes({_state1_global_info}, {_state2_opened}, [this](int opened) {
			if(opened && _has_global_ch)
				updateZoomMinMax<CH_GLOBAL>();
		});
		_state_man.add_codes({_state1_closeup_zoom, _state1_scaling, _state1_scale_factor, _state1_direction}, {_state2_opened}, [this](int opened) {
			if(!opened)
				return;
			if(_closeup_zoom>3*closeup_max_dim) {
				_closeup_zoom=3*closeup_max_dim;
			} else if(_closeup_zoom<20*closeup_min_voxel) {
				_closeup_zoom=20*closeup_min_voxel;
			}
			updateMVPMatrices<CH_CLOSEUP>();
		});
		_state_man.add_codes({_state1_global_zoom, _state1_scaling, _state1_scale_factor, _state1_direction}, {_state2_opened}, [this](int opened) {
			if(!opened)
				return;
			if(_global_zoom>3*global_max_dim) {
				_global_zoom=3*global_max_dim;
			} else if(_global_zoom<20*global_min_voxel) {
				_global_zoom=20*global_min_voxel;
			}
			updateMVPMatrices<CH_GLOBAL>();
		});
		_state_man.add_codes({_state1_cur_pos}, {_state2_opened}, [this](int opened) {
			if(opened)
				clearSelection();
		});
		_state_man.add_codes({_state1_closeup_cube, _state1_cur_pos}, {_state2_opened}, [this](int opened) {
			if(!opened)
				return;
			if(_closeup_cube) {
				updateCubeMatrix<CH_CLOSEUP>();
			}
		});
		_state_man.add_codes({_state1_global_cube, _state1_cur_pos}, {_state2_opened}, [this](int opened) {
			if(!opened)
				return;
			if(_global_cube) {
				updateCubeMatrix<CH_GLOBAL>();
				updateCubeMatrix<CH_INSET>();
			}
		});
		_state_man.add_codes({_state1_model, _state1_cur_pos, _state1_tgt_pos, _state1_view_mode}, {_state2_opened}, [this](int opened) {
			if(opened)
				_edg_cache_dirty=true;
		});
		_state_man.add_codes({
			_state1_closeup_info,
				_state1_closeup_xfunc,
				_state1_closeup_zoom,
				_state1_cur_pos,
				_state1_data_only,
				_state1_direction,
				_state1_global_cube, //XXX cond
				_state1_closeup_cube, // XXX cond
				_state1_global_xfunc,
				_state1_global_zoom,
				_state1_model,
				_state1_path,
				_state1_sel,
				_state1_sel_rect,
				_state1_scale_factor,
				_state1_scaling,
				_state1_slice_delta,
				_state1_slice_mode,
				_state1_tgt_pos,
				_state1_view_mode,
		}, {_state2_opened}, [this](int opened) {
			if(opened)
				this->canvas_update();
		});
	}

	{
		// XXX move these to apply_changes_stage1 and use makeCurrent...
		_state_man2.add_codes({_gl_state1_closeup_info}, [this]() {
			if(_stage!=SessionStage::Opened)
				return;
			updateInsetAxis();
		});
		_state_man2.add_codes({_gl_state1_closeup_cube}, [this]() {
			if(_stage!=SessionStage::Opened)
				return;
			this->mark_busy();
			updateCubeTexture<CH_CLOSEUP>();
			this->unmark_busy();
		});
		_state_man2.add_codes({_gl_state1_global_cube}, [this]() {
			if(_stage!=SessionStage::Opened)
				return;
			this->mark_busy();
			updateCubeTexture<CH_GLOBAL>();
			this->unmark_busy();
		});
		_state_man2.add_codes({_gl_state1_scaling, _gl_state1_scale_factor}, [this]() {
			if(_stage!=SessionStage::Opened)
				return;
			if(funcs_ok)
				resizeFrameBuffers();
		});
		_state_man2.add_codes({_gl_state1_path}, [this]() {
			if(_stage!=SessionStage::Opened)
				return;
			auto pathGL=pathToPathGL(path_data());
			this->glBindBuffer(GL_ARRAY_BUFFER, _vao_man.buffer(pbiPath));
			this->glBufferData(GL_ARRAY_BUFFER, sizeof(PointGL)*pathGL.size(), pathGL.data(), GL_STATIC_DRAW);
		});
	}

	_state_man.propagate();
}

#include "gapr/gui/opengl-impl.hh"
