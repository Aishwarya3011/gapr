#include "canvas.hh"

#include "gapr/edge-model.hh"

#include "gapr/utility.hh"
#include "gapr/cube.hh"

#include <chrono>
#include <regex>

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QGuiApplication>
#include <QtGui/QtGui>

using OpenGLFunctions=QOpenGLFunctions_3_3_Core;
using Canvas=gapr::proofread::Canvas;

/*
l click pickpos/picknode/subedge_sel(chg tgt)
l click-drag-release rotate/?drag_obj
m rot zoom/scroll
r click popup
m click-drag-release ?Pan(chg pos)
*/

/* ???
 * multisampling
 * offscreen rendering on worker threads, to gen. texture
 * shared texture
 * asynchronous texture uploads
 */
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
	MouseMode mode{MouseMode::Nul};
};

#include "canvas-helper.hh"

/*! the same as gapr::node_attr::data_type,
 * use GLuint to ensure OpenGL compatibility.
 */
using PointGL=std::pair<std::array<GLint, 3>, GLuint>;
const std::vector<PointGL>& pathToPathGL(const std::vector<gapr::edge_model::point>& points) {
	return points;
#if 0
	// XXX extra point????
	pathGL.back().p[0]=points.back()._x;
	pathGL.back().p[1]=points.back()._y;
	pathGL.back().p[2]=points.back()._z;
	pathGL.back().r=0;
	pathGL.back().m=0;
	return pathGL;
#endif
}

static inline QVector3D color2vec3(const QColor& c) {
	return QVector3D(c.redF(), c.greenF(), c.blueF());
}

#define PICK_SIZE 5
#define FBO_ALLOC 64
#define FBO_ALIGN 16

struct Canvas::PRIV {
	Canvas& canvas;
	OpenGLFunctions funcs;
	bool funcs_ok{false};
	ViewerShared* viewerShared{nullptr};

	GLsizei fbo_width, fbo_height;
	GLsizei fbo_width_alloc, fbo_height_alloc;

	FramebufferOpaque fbo_opaque{funcs};
	FramebufferEdges fbo_edges{funcs};
	FramebufferCubes fbo_cubes{funcs};
	FramebufferScale fbo_scale{funcs};

	int pbiPath{0}, pbiFixed;

	std::vector<PathBuffer> pathBuffers;

	QColor colors[COLOR_NUM];

	//Inset specific
	double _inset_zoom;
	std::array<double, 3> _inset_center;
	//asis inset (closeup)

	//MVP
	struct Matrices {
		QMatrix4x4 mView, mrView, mProj, mrProj;
		QMatrix4x4 cube_mat;
	} _mat;

	double global_min_voxel;
	double global_max_dim;
	int global_cube_tex{0};

	double closeup_min_voxel;
	double closeup_max_dim;
	int closeup_cube_tex{0};

	/////////////////////////////////////

	double _dpr;
	int _wid_width, _wid_height;


	// Volume
	int slice_delta{0};

	MouseState mouse;
	QMatrix4x4 mrViewProj;
	QVector3D _prev_up, _prev_rgt;

	//need? init? cfg control ctor/dtor/func
	QVector3D pickA{}, pickB{};
	//bool colorMode;
	unsigned int _changes1{0};


	explicit PRIV(Canvas& canvas): canvas{canvas} {
		viewerShared=new ViewerShared{};
		//viewerShared{Tracer::instance()->viewerShared()},
		//colors{}, colorMode{false},
		//curPos{}, tgtPos{},
		//mView{}, mrView{}, mProj{}, mrProj{},
		//scale_factor{0}, fbo_width{0}, fbo_height{0},
		//funcs{nullptr},
		//pathBuffers{}, 
		PathBuffer pbHead;
		pbHead.vao=0;
		pbHead.vbo=0;
		pbHead.prev_unused=0;
		pathBuffers.emplace_back(pbHead);
		// XXX color config
		for(int i=0; i<COLOR_NUM; i++) {
			//
			colors[i]=colorData[i].def;
		}
	}
	~PRIV() {
		if(!funcs_ok)
			return;

			gl(BindBuffer, funcs)(GL_ARRAY_BUFFER, 0);
			gl(BindVertexArray, funcs)(0);
			for(size_t i=1; i<pathBuffers.size(); i++) {
				gl(DeleteBuffers, funcs)(1, &pathBuffers[i].vbo);
				gl(DeleteVertexArrays, funcs)(1, &pathBuffers[i].vao);
			}
			//gl(DeleteBuffers, funcs)(1, &vbo_progr);

			gl(ActiveTexture, funcs)(GL_TEXTURE0);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, 0);

			viewerShared->deinitialize(&funcs);
			gl(BindFramebuffer, funcs)(GL_FRAMEBUFFER, 0);
	}

	int createPbi() {
		if(pathBuffers[0].prev_unused) {
			auto i=pathBuffers[0].prev_unused;
			pathBuffers[0].prev_unused=pathBuffers[i].prev_unused;
			return i;
		}
		PathBuffer pbItem;
		gl(GenVertexArrays, funcs)(1, &pbItem.vao);
		gl(BindVertexArray, funcs)(pbItem.vao);
		gl(GenBuffers, funcs)(1, &pbItem.vbo);
		gl(BindBuffer, funcs)(GL_ARRAY_BUFFER, pbItem.vbo);

		gl(EnableVertexAttribArray, funcs)(0);
		gl(VertexAttribIPointer, funcs)(0, 3, GL_INT, sizeof(PointGL), &static_cast<PointGL*>(nullptr)->first);
		gl(EnableVertexAttribArray, funcs)(1);
		gl(VertexAttribIPointer, funcs)(1, 1, GL_UNSIGNED_INT, sizeof(PointGL), &static_cast<PointGL*>(nullptr)->second);

		pathBuffers.emplace_back(pbItem);
		return pathBuffers.size()-1;
	}
	void freePbi(int i) {
		pathBuffers[i].prev_unused=pathBuffers[0].prev_unused;
		pathBuffers[0].prev_unused=i;
	}
	void initializeGL() {
		if(!funcs.initializeOpenGLFunctions()) {
			QOpenGLFunctions f;
			f.initializeOpenGLFunctions();
			auto v=f.glGetString(GL_VERSION);
			gapr::report("OpenGL 3.3 Core not available. (", v, ')');
		}
		funcs_ok=true;

		// XXX
		viewerShared->initialize(&funcs);

		_dpr=canvas.devicePixelRatio();
		_wid_width=canvas.width();
		_wid_height=canvas.height();
		auto scale_factor=canvas._scale_factor;
		fbo_width=(_wid_width*_dpr+scale_factor)/(1+scale_factor);
		fbo_height=(_wid_height*_dpr+scale_factor)/(1+scale_factor);

		fbo_width_alloc=(fbo_width+FBO_ALLOC)/FBO_ALIGN*FBO_ALIGN;
		fbo_height_alloc=(fbo_height+FBO_ALLOC)/FBO_ALIGN*FBO_ALIGN;
		fbo_opaque.create(fbo_width_alloc, fbo_height_alloc);
		fbo_edges.create(fbo_width_alloc, fbo_height_alloc);
		fbo_cubes.create(fbo_width_alloc, fbo_height_alloc);
		fbo_scale.create(fbo_width_alloc, fbo_height_alloc);

		gl(ClearColor, funcs)(0.0, 0.0, 0.0, 1.0);
		gl(ClearDepth, funcs)(1.0);
		gl(Enable, funcs)(GL_DEPTH_TEST);
		gl(Enable, funcs)(GL_BLEND);
		gl(BlendFuncSeparate, funcs)(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

		pbiFixed=createPbi();
		pbiPath=createPbi();
		gl(BindBuffer, funcs)(GL_ARRAY_BUFFER, pathBuffers[pbiFixed].vbo);
		gl(BufferData, funcs)(GL_ARRAY_BUFFER, (9*24+2)*sizeof(PointGL), nullptr, GL_STATIC_DRAW);

	}
	void paintFinish(GLuint fbo) {
		auto scale_factor=canvas._scale_factor;
		bool scale=scale_factor!=0;
		if(scale) {
			gl(BindFramebuffer, funcs)(GL_DRAW_FRAMEBUFFER, fbo_scale.fbo);
			gl(Viewport, funcs)(0, 0, fbo_width, fbo_height);
			gl(Clear, funcs)(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
		} else {
			gl(BindFramebuffer, funcs)(GL_DRAW_FRAMEBUFFER, fbo);
			gl(Viewport, funcs)(0, 0, fbo_width, fbo_height);
		}
		gl(BindVertexArray, funcs)(viewerShared->vao_fixed);
		viewerShared->progs[P_SORT]->bind();
		gl(DrawArrays, funcs)(GL_TRIANGLE_STRIP, 0, 4);
		if(scale) {
			gl(BindFramebuffer, funcs)(GL_READ_FRAMEBUFFER, fbo_scale.fbo);
			gl(BindFramebuffer, funcs)(GL_DRAW_FRAMEBUFFER, fbo);
			int width, height;
			width=fbo_width*(1+scale_factor);
			height=fbo_height*(1+scale_factor);
			gl(Viewport, funcs)(0, 0, width, height);
			gl(BlitFramebuffer, funcs)(0, 0, fbo_width, fbo_height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}
		if(_debug_fps) {
			//gl(Finish, funcs)();
			printFPS();
		}
	}
	std::unordered_map<edge_model::edge_id, int> _edge_vaoi;
	void paintEdge() {
		gl(BindFramebuffer, funcs)(GL_FRAMEBUFFER, fbo_edges.fbo);
		gl(Viewport, funcs)(0, 0, fbo_width, fbo_height);
		gl(Clear, funcs)(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
		if(canvas._data_only)
			return;

		auto scale_factor=canvas._scale_factor;
		auto radU=canvas._closeup_zoom;
		auto& mat=_mat;
		float umpp;
		if(scale_factor+1<_dpr) {
			umpp=radU*_dpr/(scale_factor+1)/fbo_width;
		} else {
			umpp=radU/fbo_width;
		}

		gl(Disable, funcs)(GL_BLEND);

		viewerShared->progs[P_EDGE_PR]->bind();
		auto locp=viewerShared->progs[P_EDGE_PR]->uniformLocation("p0int");
		gapr::node_attr p{canvas._cur_pos.point};
		gl(Uniform3i, funcs)(locp, p.ipos[0], p.ipos[1], p.ipos[2]);
		viewerShared->progs[P_EDGE_PR]->setUniformValue("mView", mat.mView);
		viewerShared->progs[P_EDGE_PR]->setUniformValue("mProj", mat.mProj);
		auto idx_loc=viewerShared->progs[P_EDGE_PR]->uniformLocation("idx");
		auto num_path=canvas._path_data.size();
		if(num_path) {
			viewerShared->progs[P_EDGE_PR]->setUniformValue("umpp", umpp);
			viewerShared->progs[P_EDGE_PR]->setUniformValue("color_edge[0]", colors[C_ATTENTION]);
			gl(Uniform1ui, funcs)(idx_loc, GLuint{0});
			gl(BindVertexArray, funcs)(pathBuffers[pbiPath].vao);
			gl(DrawArrays, funcs)(GL_LINE_STRIP, 0, num_path);
		}

		edge_model::reader reader{*canvas._model};

#if 0
		Tree curTree{};
		if(curPos.edge && curPos.edge.tree())
			curTree=curPos.edge.tree();
#endif
		for(auto eid: reader.edges_filter()) {
			auto& e=reader.edges().at(eid);
			QColor color=colors[C_TYPE_0];
#if 0
			if(/*e.type()*/0!=0) {
				color=colors[C_TYPE_1];
			} else if(e.inLoop() || !e.tree()) {
				color=colors[C_ATTENTION];
			} else if(e.tree() /*&& e.tree()==curTree*/) {
				color=colors[C_TYPE_2];
			}
#endif

			//if(e.tree() && e.tree().selected()) {
				viewerShared->progs[P_EDGE_PR]->setUniformValue("umpp", 2*umpp);
			//} else {
				//viewerShared->progs[P_EDGE_PR]->setUniformValue("umpp", umpp);
			//}

#if 0
			if(curPos.edge && curPos.edge==e) {
				if(tgtPos.edge && tgtPos.edge==e) {
					auto ep=EdgePriv::get(e);
					gl(Uniform1ui, funcs)(idx_loc, static_cast<GLuint>(ep->index+1));
					gl(BindVertexArray, funcs)(pathBuffers[ep->vaoi].vao);
					auto a=curPos.index;
					auto b=tgtPos.index;
					if(a>b)
						std::swap(a, b);
					if(b-a>0) {
						viewerShared->progs[P_EDGE_PR]->setUniformValue("color_edge[0]", color);
						gl(DrawArrays, funcs)(GL_LINE_STRIP, a, b-a+1);

					}
					if(a>0 || ep->points.size()-b>1) {
						viewerShared->progs[P_EDGE_PR]->setUniformValue("color_edge[0]", color.darker(180));
						if(a>0) {
							gl(DrawArrays, funcs)(GL_LINE_STRIP, 0, a+1);
						}
						if(ep->points.size()-b>1) {
							gl(DrawArrays, funcs)(GL_LINE_STRIP, b, ep->points.size()-b);
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
				int vaoi=createPbi();
				gl(BindBuffer, funcs)(GL_ARRAY_BUFFER, pathBuffers[vaoi].vbo);
				gl(BufferData, funcs)(GL_ARRAY_BUFFER, sizeof(PointGL)*pathGL.size(), pathGL.data(), GL_STATIC_DRAW);
				it_vaoi->second=vaoi;
			}
			//auto ep=EdgePriv::get(e);
			gl(Uniform1ui, funcs)(idx_loc, static_cast<GLuint>(eid+1));
			viewerShared->progs[P_EDGE_PR]->setUniformValue("color_edge[0]", color);
			gl(BindVertexArray, funcs)(pathBuffers[it_vaoi->second].vao);
			gl(DrawArrays, funcs)(GL_LINE_STRIP, 0, e.points.size());
		}

		gl(BindVertexArray, funcs)(viewerShared->vao_fixed);
		viewerShared->progs[P_VERT]->bind();
		viewerShared->progs[P_VERT]->setUniformValue("mView", mat.mView);
		viewerShared->progs[P_VERT]->setUniformValue("mProj", mat.mProj);
		viewerShared->progs[P_VERT]->setUniformValue("umpp", umpp);
		viewerShared->progs[P_VERT]->bind();
		locp=viewerShared->progs[P_VERT]->uniformLocation("p0int");
		gl(Uniform3i, funcs)(locp, p.ipos[0], p.ipos[1], p.ipos[2]);
		auto cent_loc=viewerShared->progs[P_VERT]->uniformLocation("center");
		idx_loc=viewerShared->progs[P_VERT]->uniformLocation("idx");
		gl(Uniform1ui, funcs)(idx_loc, GLuint{1});
		auto nid_loc=viewerShared->progs[P_VERT]->uniformLocation("nid");

		for(auto& pid: reader.props_filter()) {
			if(pid.key=="error") {
				auto pos=reader.to_position(pid.node);
				gapr::node_attr::data_type pt;
				if(pos.edge) {
					auto& e=reader.edges().at(pos.edge);
					pt=e.points[pos.index/128];
				} else if(pos.vertex) {
					auto& v=reader.vertices().at(pid.node);
					pt=v.attr.data();
				} else {
					continue;
				}
				QColor color=colors[C_ALERT];
				int rad=6;
				auto& val=reader.props().at(pid);
				if(val!="") {
					color=color.darker(160);
					rad=4+2;
				}
				viewerShared->progs[P_VERT]->setUniformValue("umpp", umpp);
				viewerShared->progs[P_VERT]->setUniformValue("color", color);
				gl(Uniform1ui, funcs)(nid_loc, GLuint{pid.node.data});
				gl(Uniform4i, funcs)(cent_loc, pt.first[0], pt.first[1], pt.first[2], rad);
				gl(DrawArrays, funcs)(GL_TRIANGLE_FAN, 100, 32);
			}
		}

		for(auto vid: reader.vertices_filter()) {
			auto& v=reader.vertices().at(vid);
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
			QColor color=colors[C_TYPE_0];
			auto it_state=reader.props().find(gapr::prop_id{vid, "state"});
			if(v.edges.size()<2) {
				if(it_state==reader.props().end()) {
					color=colors[C_ATTENTION];
				} else if(it_state->second!="end") {
					color=colors[C_ALERT];
				}
			}

			int rad=8;
			auto it_root=reader.props().find(gapr::prop_id{vid, "root"});
			if(it_root!=reader.props().end())
				rad=12;
#if 0
			if(v.tree() && v.tree().root()==v) {
				rad=12;
			} else if(v.neighbors().size()>1) {
				rad=6;
			}
#endif

			viewerShared->progs[P_VERT]->setUniformValue("umpp", umpp);
			viewerShared->progs[P_VERT]->setUniformValue("color", color);
			gl(Uniform1ui, funcs)(nid_loc, GLuint{vid.data});
			auto& p=v.attr;
			gl(Uniform4i, funcs)(cent_loc, p.ipos[0], p.ipos[1], p.ipos[2], rad);
			gl(DrawArrays, funcs)(GL_TRIANGLE_FAN, 100, 32);
			//if(v.tree() && v.tree().selected()) {
			//gl(DrawArrays, funcs)(GL_LINES, 44, 16);
			//}
		}

		gl(Enable, funcs)(GL_BLEND);
	}
	void paintOpaqueInset() {
	}
	void paintOpaque() {
		gl(BindFramebuffer, funcs)(GL_FRAMEBUFFER, fbo_opaque.fbo);
		gl(Viewport, funcs)(0, 0, fbo_width, fbo_height);
		gl(Clear, funcs)(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);



		gl(Viewport, funcs)(0, 0, fbo_width, fbo_height);
		float umpp;
		auto scale_factor=canvas._scale_factor;
		auto radU=canvas._closeup_zoom;
		if(scale_factor+1<_dpr) {
			umpp=radU*_dpr/(scale_factor+1)/fbo_width;
		} else {
			umpp=radU/fbo_width;
		}
		auto& mat=_mat;
		gapr::node_attr p{canvas._cur_pos.point};

		//pick
		viewerShared->progs[P_LINE]->bind();
		viewerShared->progs[P_LINE]->setUniformValue("mView", mat.mView);
		viewerShared->progs[P_LINE]->setUniformValue("mProj", mat.mProj);
		int locp;
		viewerShared->progs[P_LINE]->setUniformValue("pixel_size", GLfloat(umpp/radU), GLfloat(umpp/radU*fbo_width/fbo_height));
		if(!(pickA-pickB).isNull()) {
			viewerShared->progs[P_LINE]->setUniformValue("color", color2vec3(colors[C_ATTENTION]));
			gl(DrawArrays, funcs)(GL_LINES, 140, 2);
		}
		if(mouse.mode==MouseMode::RectSel) {
			viewerShared->progs[P_LINE]->setUniformValue("color", color2vec3(colors[C_ATTENTION]));
			gl(DrawArrays, funcs)(GL_LINE_LOOP, 142, 4);
		}

		gl(BindVertexArray, funcs)(viewerShared->vao_fixed);
		viewerShared->progs[P_MARK]->bind();
		viewerShared->progs[P_MARK]->setUniformValue("mView", mat.mView);
		viewerShared->progs[P_MARK]->setUniformValue("mProj", mat.mProj);
		viewerShared->progs[P_MARK]->setUniformValue("umpp", umpp);
		locp=viewerShared->progs[P_MARK]->uniformLocation("p0int");
		gl(Uniform3i, funcs)(locp, p.ipos[0], p.ipos[1], p.ipos[2]);
		auto cent_loc=viewerShared->progs[P_MARK]->uniformLocation("center");
		auto& tgtPos=canvas._tgt_pos;
		if(tgtPos.valid()) {
			viewerShared->progs[P_MARK]->setUniformValue("color", color2vec3(colors[C_ATTENTION]));
			gapr::node_attr pt{tgtPos.point};
			gl(Uniform4i, funcs)(cent_loc, pt.ipos[0], pt.ipos[1], pt.ipos[2], 20);
			if(tgtPos.edge) {
				gl(DrawArrays, funcs)(GL_LINES, 10, 8);
			} else {
				gl(DrawArrays, funcs)(GL_LINES, 6, 4);
			}
			// Neurons, in circle, mark
		}
		auto& curPos=canvas._cur_pos;
		if(curPos.valid()) {
			viewerShared->progs[P_MARK]->setUniformValue("color", color2vec3(colors[C_ATTENTION]));
			gapr::node_attr pt{curPos.point};
			gl(Uniform4i, funcs)(cent_loc, pt.ipos[0], pt.ipos[1], pt.ipos[2], 16);
			gl(DrawArrays, funcs)(GL_LINE_LOOP, 100, 32);
			// Neurons, in circle, mark
		}
		if(!canvas._nodes_sel.empty()) {
			viewerShared->progs[P_MARK]->setUniformValue("color", color2vec3(colors[C_AXIS]));
			//viewerShared->progs[P_MARK]->setUniformValue("umpp", umpp);
			for(auto& [n, pt]: canvas._nodes_sel) {
				gl(Uniform4i, funcs)(cent_loc, pt.ipos[0], pt.ipos[1], pt.ipos[2], 4);
				gl(DrawArrays, funcs)(GL_LINES, 100, 32);
				//gl(DrawArrays, funcs)(GL_LINES, 44, 16);
			}
		}
	}
	void paintBlank() {
		int width, height;
		auto scale_factor=canvas._scale_factor;
		width=fbo_width*(1+scale_factor);
		height=fbo_height*(1+scale_factor);
		gl(Viewport, funcs)(0, 0, width, height);
		gl(Clear, funcs)(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
	}

	void paintVolumeImpl(Matrices& mat, int tex, std::array<double, 2>& _xfunc, double min_voxel, double zoom) {
		viewerShared->progs[P_VOLUME]->setUniformValue("mrView", mat.mrView);
		viewerShared->progs[P_VOLUME]->setUniformValue("mrProj", mat.mrProj);
		if(tex) {
			gl(BindTexture, funcs)(GL_TEXTURE_3D, viewerShared->cubeTextures[tex].tex);
			QVector2D xfunc(_xfunc[0], _xfunc[1]);
			viewerShared->progs[P_VOLUME]->setUniformValue("xfunc_cube", xfunc);
			viewerShared->progs[P_VOLUME]->setUniformValue("mrTex", mat.cube_mat);
			auto mvs=min_voxel;
			auto radU=zoom;
			auto slice_pars=canvas._slice_pars;
			if(canvas._slice_mode) {
				viewerShared->progs[P_VOLUME]->setUniformValue("zparsCube", QVector3D(
							-1.0*slice_delta/slice_pars[1],
							-1.0*slice_delta/slice_pars[1],
							qMax(1.0/slice_pars[1], 9*mvs/radU/8)));
			} else {
				viewerShared->progs[P_VOLUME]->setUniformValue("zparsCube", QVector3D(
							-1.0*slice_pars[0]/slice_pars[1],
							1.0*slice_pars[0]/slice_pars[1],
							qMax(1.0/slice_pars[1], 9*mvs/radU/8)));
			}
			gl(DrawArrays, funcs)(GL_TRIANGLE_STRIP, 0, 4);
		}
	}
	void paintVolume() {
		gl(BindFramebuffer, funcs)(GL_FRAMEBUFFER, fbo_cubes.fbo);
		gl(Viewport, funcs)(0, 0, fbo_width, fbo_height);
		gl(Clear, funcs)(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

		viewerShared->progs[P_VOLUME]->bind();
		viewerShared->progs[P_VOLUME]->setUniformValue("color_volume", color2vec3(colors[C_CHAN0]));
		gl(BindVertexArray, funcs)(viewerShared->vao_fixed);
		gl(ActiveTexture, funcs)(GL_TEXTURE1);
		paintVolumeImpl(_mat, closeup_cube_tex, canvas._closeup_xfunc, closeup_min_voxel, canvas._closeup_zoom);
	}

	void updateCubeMatrix() {
		// Depends on: *_cur_pos, cube.xform, cube.data
		std::array<double, 3> p0;
		gapr::affine_xform* xform;
		std::array<unsigned int, 3> p1;
		std::array<std::size_t, 3> size;
			gapr::node_attr pt{canvas._cur_pos.point};
			for(unsigned int i=0; i<3; i++)
				p0[i]=pt.pos(i);
			xform=&canvas._closeup_xform;
			p1=canvas._closeup_offset;
			auto cube_view=canvas._closeup_cube.view<const char>();
			size={cube_view.width_adj(), cube_view.sizes(1), cube_view.sizes(2)};
			//cl cl
		auto& mat=_mat;
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
		auto& xform=canvas._closeup_xform;
		auto sizes=canvas._closeup_sizes;
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
	void updateZoomMinMax() {
		gapr::affine_xform* xform;
		std::array<unsigned int, 3> size;
		double* outa;
		double* outb;
			xform=&canvas._closeup_xform;
			for(unsigned int i=0; i<3; i++)
				size[i]=canvas._closeup_cube_sizes[i]*3;
			outa=&closeup_min_voxel;
			outb=&closeup_max_dim;

		double vb=0;
		double va=qInf();
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
		void updateMVPMatrices() {
			// Depends on: *rgt, *radU, *up, *fbo_height, *fbo_width
			double radU;
			GLsizei w, h;
			double z_clip;
				radU=canvas._closeup_zoom;
			w=fbo_width;
			h=fbo_height;
			z_clip=.5;
			double z_factor=2.0;
		auto& mat=_mat;
		mat.mView.setToIdentity();
		auto& dir=canvas._direction;
		QVector3D rgt{(float)dir[3], (float)dir[4], (float)dir[5]};
		QVector3D up{(float)dir[0], (float)dir[1], (float)dir[2]};
		mat.mView.lookAt(rgt*z_factor*radU, QVector3D{0, 0, 0}, up);
		mat.mrView=mat.mView.inverted();

		float vspan=radU*h/w;
		mat.mProj.setToIdentity();
		// TODO frustum
		mat.mProj.ortho(-radU, radU, -vspan, vspan, (z_factor-z_clip)*radU, (z_factor+z_clip)*radU);
		mat.mrProj=mat.mProj.inverted();
	}
	void updateCubeTexture() {
		gapr::print("update cube ", 1);
		int* pi;
		const gapr::cube* cub;
			pi=&closeup_cube_tex;
			cub=&canvas._closeup_cube;
		if(*pi)
			viewerShared->releaseTexture(&funcs, *pi);
		*pi=viewerShared->getTexture(&funcs, *cub);
	}
	void updateInsetAxis() {
		int ch=0;
		gl(BindBuffer, funcs)(GL_ARRAY_BUFFER, pathBuffers[pbiFixed].vbo);
		auto ptr=static_cast<PointGL*>(funcs.glMapBufferRange(GL_ARRAY_BUFFER, (2+ch*24)*sizeof(PointGL), 24*sizeof(PointGL), GL_MAP_WRITE_BIT|GL_MAP_INVALIDATE_RANGE_BIT));

		auto size=canvas._closeup_sizes;
		auto& xform=canvas._closeup_xform;
		uint32_t p0[3]={0, 0, 0};
		//for(int i=0; i<3; i++) {
			//gapr::print(size[i]);
		//}
		for(int dir=0; dir<3; dir++) {
			for(int i=0; i<2; i++) {
				for(int j=0; j<2; j++) {
					for(int k=0; k<2; k++) {
						std::array<unsigned int, 3> ppp;
						ppp[dir]=(k*size[dir]+p0[dir]);
						ppp[(dir+1)%3]=(j*size[(dir+1)%3]+p0[(dir+1)%3]);
						ppp[(dir+2)%3]=(i*size[(dir+2)%3]+p0[(dir+2)%3]);
						auto pp=xform.from_offset(ppp);
						gapr::node_attr p{pp[0], pp[1], pp[2]};
						ptr[dir*8+i*4+j*2+k]=p.data();
					}
				}
			}
		}
		gl(UnmapBuffer, funcs)(GL_ARRAY_BUFFER);
	}
	void resizeFrameBuffers() {
		auto ww=_dpr*_wid_width;
		auto hh=_dpr*_wid_height;
		int width, height;
		auto scale_factor=canvas._scale_factor;
		width=(ww+scale_factor)/(1+scale_factor);
		height=(hh+scale_factor)/(1+scale_factor);
		fbo_width=width;
		fbo_height=height;
		if(fbo_width>fbo_width_alloc || fbo_width+2*FBO_ALLOC<fbo_width_alloc ||
				fbo_height>fbo_height_alloc || fbo_height+2*FBO_ALLOC<fbo_height_alloc) {
			fbo_width_alloc=(fbo_width+FBO_ALLOC)/FBO_ALIGN*FBO_ALIGN;
			fbo_height_alloc=(fbo_height+FBO_ALLOC)/FBO_ALIGN*FBO_ALIGN;

			//gl(ActiveTexture, funcs)(GL_TEXTURE0);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, fbo_scale.tex[0]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_DEPTH_STENCIL, fbo_width_alloc, fbo_height_alloc, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, fbo_scale.tex[1]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_RGBA8, fbo_width_alloc, fbo_height_alloc, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, nullptr);

			gl(BindTexture, funcs)(GL_TEXTURE_2D, fbo_opaque.tex[0]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_DEPTH_STENCIL, fbo_width_alloc, fbo_height_alloc, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, fbo_opaque.tex[1]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_RGBA8, fbo_width_alloc, fbo_height_alloc, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, nullptr);

			gl(BindTexture, funcs)(GL_TEXTURE_2D, fbo_edges.tex[0]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, fbo_width_alloc, fbo_height_alloc, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, fbo_edges.tex[1]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_R32I, fbo_width_alloc, fbo_height_alloc, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, fbo_edges.tex[2]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_R32I, fbo_width_alloc, fbo_height_alloc, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, fbo_edges.tex[3]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_RGBA8, fbo_width_alloc, fbo_height_alloc, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, nullptr);

			for(int i=0; i<1; i++) {
				gl(BindTexture, funcs)(GL_TEXTURE_2D, fbo_cubes.tex[0]);
				gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_DEPTH_STENCIL, fbo_width_alloc, fbo_height_alloc, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
				gl(BindTexture, funcs)(GL_TEXTURE_2D, fbo_cubes.tex[1]);
				gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_RGBA8, fbo_width_alloc, fbo_height_alloc, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, nullptr);
			}
			//updateMVPMatrices();
		}
	}
	void set_dpr(double dpr) {
		_dpr=dpr;
		_changes1|=mask<chg::dpr>;
	}
	std::array<double, 6> handleRotate() {
		QVector3D a((mouse.xPressed*2+1.0)/_wid_width-1, 1-(mouse.yPressed*2+1.0)/_wid_height, -1);
		QVector3D b((mouse.x*2+1.0)/_wid_width-1,1-(mouse.y*2+1.0)/_wid_height, -1);
		a=mrViewProj*a;
		a.normalize();
		b=mrViewProj*b;
		b.normalize();
		QVector3D norm=QVector3D::crossProduct(a, b);
		QMatrix4x4 mat{};
		float proj=QVector3D::dotProduct(a, b);
		if(proj<-1) proj=-1;
		if(proj>1) proj=1;
		float r=360*acos(proj)/2/M_PI;
		mat.rotate(-r, norm);
		auto up=mat*_prev_up;
		auto rgt=mat*_prev_rgt;
		return {up[0], up[1], up[2], rgt[0], rgt[1], rgt[2]};
	}
	QVector3D toLocal(const gapr::node_attr& p) const {
		gapr::node_attr d;
		for(unsigned int i=0; i<3; ++i)
			d.ipos[i]=p.ipos[i]-canvas._cur_pos.point.first[i];
		return QVector3D(d.pos(0), d.pos(1), d.pos(2));
	}
	gapr::node_attr toGlobal(const QVector3D& p) const {
		gapr::node_attr pt{canvas._cur_pos.point};
		auto x=p.x()+pt.pos(0);
		auto y=p.y()+pt.pos(1);
		auto z=p.z()+pt.pos(2);
		return {x, y, z};
	}

	/*! enable snapping, by *pixel. XXX not radius or distance.
	 * 
	 */
	bool pickPoint(int x, int y, Position& pos) {
		int pick_size;
		int fbx, fby;
		auto scale_factor=canvas._scale_factor;
		if(scale_factor+1<_dpr) {
			pick_size=PICK_SIZE*_dpr/(scale_factor+1);
			fbx=x/(1+scale_factor);
			fby=y/(1+scale_factor);
		} else {
			pick_size=PICK_SIZE;
			fbx=x/(1+scale_factor);
			fby=y/(1+scale_factor);
		}

		float px=(fbx*2+1.0)/fbo_width-1;
		float py=1-(fby*2+1.0)/fbo_height;
		auto& m=_mat;
		if(canvas._slice_mode) {
			auto slice_pars=canvas._slice_pars;
			float depth=-1.0*slice_delta/slice_pars[1];
			QVector3D lp=m.mrView*m.mrProj*QVector3D(px, py, depth);
			auto p=toGlobal(lp);
			pos=Position{p.data()};
			return true;
		}

		bool ret=false;
		canvas.makeCurrent();
		if(fbx-pick_size>=0 && fby-pick_size>=0
				&& fby+pick_size<fbo_height
				&& fbx+pick_size<fbo_width) {
			int pickdr2=pick_size*pick_size+1;
			int pickI=-1;
			std::vector<GLuint> bufIdx((2*pick_size+1)*(2*pick_size+1));
			gl(BindFramebuffer, funcs)(GL_READ_FRAMEBUFFER, fbo_edges.fbo);
			gl(ReadBuffer, funcs)(GL_COLOR_ATTACHMENT0);
			gl(ReadPixels, funcs)(fbx-pick_size, fbo_height-1-fby-pick_size, pick_size*2+1, pick_size*2+1, GL_RED_INTEGER, GL_UNSIGNED_INT, &bufIdx[0]);

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
				edge_model::edge_id edge_id=bufIdx[pickI]-1;
				gl(ReadBuffer, funcs)(GL_COLOR_ATTACHMENT1);
				std::vector<GLuint> bufPos((2*pick_size+1)*(2*pick_size+1));
				gl(ReadPixels, funcs)(fbx-pick_size, fbo_height-1-fby-pick_size, pick_size*2+1, pick_size*2+1, GL_RED_INTEGER, GL_UNSIGNED_INT, &bufPos[0]);
				canvas.doneCurrent();

				if(edge_id==0) {
					edge_model::vertex_id vid{bufPos[pickI]};
					pos=Position{vid, {}};
					return true;
				}

				uint32_t pickPos=bufPos[pickI]*8;
				pos=Position{edge_id, pickPos, {}};

				return true;
			}

			pick_size/=2;
			int mdx=0, mdy=0;
			GLuint maxv=0;
			std::vector<GLuint> bufRed((2*pick_size+1)*(2*pick_size+1));
			gl(BindFramebuffer, funcs)(GL_READ_FRAMEBUFFER, fbo_cubes.fbo);
			gl(ReadBuffer, funcs)(GL_COLOR_ATTACHMENT0);
			gl(ReadPixels, funcs)(fbx-pick_size, fbo_height-1-fby-pick_size, pick_size*2+1, pick_size*2+1, GL_RED, GL_UNSIGNED_INT, &bufRed[0]);
			for(int dy=-pick_size; dy<=pick_size; dy++) {
				for(int dx=-pick_size; dx<=pick_size; dx++) {
					int i=dx+pick_size+(dy+pick_size)*(2*pick_size+1);
					auto v=bufRed[i];
					//printMessage(dx, ' ', dy, ": ", v);
					if(v>maxv) {
						maxv=v;
						mdx=dx;
						mdy=dy;
					}
				}
			}
			if(maxv>0) {
				GLfloat mvaldepth;
				//gl(ReadBuffer, funcs)(GL_DEPTH_STENCIL_ATTACHMENT);
				gl(ReadPixels, funcs)(fbx+mdx, fbo_height-1-fby+mdy, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &mvaldepth);
				//canvas.doneCurrent();
				auto xx=mdx+pick_size+fbx-pick_size;
				auto yy=-mdy-pick_size+fby+pick_size;
				QVector3D pp((xx*2+1.0)/fbo_width-1, 1-(yy*2+1.0)/fbo_height, mvaldepth*2-1);
				pp=m.mrView*m.mrProj*pp;
				auto p=toGlobal(pp);
				pos=Position{p.data()};
				ret=true;
			}
		}

		QVector3D a(px, py, -1);
		QVector3D b(px, py, 1);
		a=m.mrView*m.mrProj*a;
		b=m.mrView*m.mrProj*b;
		QVector3D c=a-b;
		auto lenc=c.length();
		c=c/lenc;
		QVector3D selC=pickA-pickB;
		selC.normalize();
		QVector3D d=QVector3D::crossProduct(c, selC);

		QMatrix4x4 mat{};
		mat.setColumn(0, QVector4D(c, 0));
		mat.setColumn(1, QVector4D(-selC, 0));
		mat.setColumn(2, QVector4D(d, 0));
		QMatrix4x4 matr=mat.inverted();
		QVector3D s=matr*(pickB-b);
		//printMessage("pickpoint %1 %2 %3", d.length(), s.x(), lenc);
		if(d.length()<0.1 || s.x()<0 || s.x()>lenc) {
			pickA=a;
			pickB=b;
			gl(BindBuffer, funcs)(GL_ARRAY_BUFFER, viewerShared->vao_fixed);
			auto ptr=static_cast<GLfloat*>(funcs.glMapBufferRange(GL_ARRAY_BUFFER, 420*sizeof(GLfloat), 6*sizeof(GLfloat), GL_MAP_WRITE_BIT|GL_MAP_INVALIDATE_RANGE_BIT));
			auto copy=[](const QVector3D& a, GLfloat* b) {
				b[0]=a.x();
				b[1]=a.y();
				b[2]=a.z();
			};
			copy(a, ptr);
			copy(b, ptr+3);
			gl(UnmapBuffer, funcs)(GL_ARRAY_BUFFER);
			canvas.doneCurrent();
			canvas._changes0|=mask<chg::sel_rect>;
			canvas.apply_changes();
			return ret;
		}
		canvas.doneCurrent();
		QVector3D t=b+s.x()*c;
		auto gt=toGlobal(t);
		pickA=pickB=QVector3D();
		pos=Position{gt.data()};
		return true;
	}
	std::vector<GLuint> _sel_buf_idx;
	std::vector<GLint> _sel_buf_pos;
	bool selectNodes(int x0, int y0, int x, int y, std::vector<std::pair<gapr::node_id, gapr::node_attr>>& nodes_sel) {
		int fbx0, fby0, fbx, fby;
		auto scale_factor=canvas._scale_factor;
		fbx0=std::max(std::min(x0, x)/(1+scale_factor), 0);
		fby0=std::max(std::min(y0, y)/(1+scale_factor), 0);
		fbx=std::min(std::max(x0, x)/(1+scale_factor), fbo_width-1);
		fby=std::min(std::max(y0, y)/(1+scale_factor), fbo_height-1);
		auto& m=_mat;

		canvas.makeCurrent();
		gl(BindFramebuffer, funcs)(GL_READ_FRAMEBUFFER, fbo_edges.fbo);
		gl(ReadBuffer, funcs)(GL_COLOR_ATTACHMENT0);
		_sel_buf_idx.resize((fbx-fbx0+1)*(fby-fby0+1));
		gl(ReadPixels, funcs)(fbx0, fbo_height-1-fby, fbx-fbx0+1, fby-fby0+1, GL_RED_INTEGER, GL_UNSIGNED_INT, &_sel_buf_idx[0]);
		std::vector<unsigned int> tmp{};
		for(int dy=fby0; dy<=fby; ++dy) {
			for(int dx=fbx0; dx<=fbx; ++dx) {
				int i=dx-fbx0+(dy-fby0)*(fbx-fbx0+1);
				if(_sel_buf_idx[i]>0)
					tmp.push_back(i);
			}
		}
		if(tmp.empty()) {
			canvas.doneCurrent();
			return false;
		}
		_sel_buf_pos.resize((fbx-fbx0+1)*(fby-fby0+1));
		gl(ReadBuffer, funcs)(GL_COLOR_ATTACHMENT1);
		gl(ReadPixels, funcs)(fbx0, fbo_height-1-fby, fbx-fbx0+1, fby-fby0+1, GL_RED_INTEGER, GL_UNSIGNED_INT, &_sel_buf_pos[0]);
		canvas.doneCurrent();

		edge_model::reader reader{*canvas._model};
		std::unordered_map<gapr::node_id, bool> status;
		for(auto i: tmp) {
			edge_model::edge_id edge_id=_sel_buf_idx[i]-1;
			if(edge_id==0) {
				edge_model::vertex_id vid(_sel_buf_pos[i]);
				status.emplace(vid, false);
			} else {
				auto& e=reader.edges().at(edge_id);
				status.emplace(e.nodes[_sel_buf_pos[i]*8/128], false);
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
			if(lp.x()<px0 || lp.x()>px)
				return false;
			if(lp.y()<py0 || lp.y()>py)
				return false;
			if(lp.z()<-1.0 || lp.z()>1.0)
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
		auto add_cc=[&sel,&sel_top,&todo]() {
			if(sel.size()>sel_top.size())
				std::swap(sel_top, sel);
			sel.clear();
			todo.clear();
		};
		for(auto i: tmp) {
			edge_model::edge_id edge_id=_sel_buf_idx[i]-1;
			if(edge_id==0) {
				edge_model::vertex_id vid(_sel_buf_pos[i]);
				if(cc_from_vert(vid))
					add_cc();
			} else {
				if(cc_from_edge(edge_id, _sel_buf_pos[i]*8/128))
					add_cc();
			}
		}
		nodes_sel=std::move(sel_top);
		return true;
	}
	void updateRectSel(int x0, int y0, int x, int y) {
		int fbx0, fby0, fbx, fby;
		auto scale_factor=canvas._scale_factor;
		fbx0=std::max(std::min(x0, x)/(1+scale_factor), 0);
		fby0=std::max(std::min(y0, y)/(1+scale_factor), 0);
		fbx=std::min(std::max(x0, x)/(1+scale_factor), fbo_width-1);
		fby=std::min(std::max(y0, y)/(1+scale_factor), fbo_height-1);
		auto& m=_mat;
		std::array<float, 2> xvals{(fbx0*2+1.0f)/fbo_width-1, (fbx*2+1.0f)/fbo_width-1};
		std::array<float, 2> yvals{1-(fby*2+1.0f)/fbo_height, 1-(fby0*2+1.0f)/fbo_height};
		canvas.makeCurrent();
		gl(BindBuffer, funcs)(GL_ARRAY_BUFFER, viewerShared->vbo_fixed);
		auto ptr=static_cast<GLfloat*>(funcs.glMapBufferRange(GL_ARRAY_BUFFER, 426*sizeof(GLfloat), 12*sizeof(GLfloat), GL_MAP_WRITE_BIT|GL_MAP_INVALIDATE_RANGE_BIT));
		for(unsigned int k=0; k<4; ++k) {
			QVector3D a(xvals[k<2?0:1], yvals[(k+1)%4<2?0:1], .99);
			a=m.mrView*m.mrProj*a;
			ptr[k*3+0]=a.x();
			ptr[k*3+1]=a.y();
			ptr[k*3+2]=a.z();
		}
		gl(UnmapBuffer, funcs)(GL_ARRAY_BUFFER);
		canvas.doneCurrent();
	}
	void clearSelection() {
		pickA=pickB=QVector3D();
	}
	void edges_removed(const std::vector<edge_model::edge_id>& edges_del) {
		for(auto eid: edges_del) {
			auto it=_edge_vaoi.find(eid);
			if(it!=_edge_vaoi.end()) {
				auto vaoi=it->second;
				if(vaoi!=0)
					freePbi(vaoi);
				_edge_vaoi.erase(it);
			}
		}
	}

	qint64 prevt=0;
	int prevc=0;
	bool _debug_fps{false};
	void printFPS() {
		qint64 nowt=QDateTime::currentMSecsSinceEpoch();
		prevc++;
		if(nowt>prevt+1000) {
			gapr::print(1, "FPS ", 1000.0*prevc/(nowt-prevt));
			prevt=nowt;
			prevc=0;
		}
	}

};

Canvas::Canvas(QWidget* parent):
	QOpenGLWidget{parent}, _priv{std::make_unique<PRIV>(*this)}
{
	gapr::print("canvas ctor");
	//setContextMenuPolicy(PreventContextMenu);

	//setMinimumSize(QSize(480, 320));
	//setFocusPolicy(Qt::WheelFocus);

	//setAcceptDrops(true);

	if(auto dbg_flags=getenv("GAPR_DEBUG"); dbg_flags) {
		std::regex r{"\\bfps\\b", std::regex::icase};
		if(std::regex_search(dbg_flags, r)) {
			auto timer=new QTimer{this};
			timer->start(10);
			connect(timer, &QTimer::timeout, this, static_cast<void (Canvas::*)()>(&Canvas::repaint));
			_priv->_debug_fps=true;
		}
	}
}
Canvas::~Canvas() {
	/*! gl context n.a. (available in QOpenGLWidget::~QOpenGLWidget()) */
}

void Canvas::initializeGL() {
	/*! gl context available */
	try {
		_priv->initializeGL();
		auto par=nativeParentWidget();
		connect(par->windowHandle(), &QWindow::screenChanged,
				this, &Canvas::handle_screen_changed);
		Q_EMIT ready({});
	//} catch(const std::runtime_error& e) {
	} catch(const std::system_error& e) {
		Q_EMIT ready(e.code());
	}
}
void Canvas::paintGL() {
	/*! gl context available
	 * viewport ready;
	 * glClear() asap.
	 */
	if(!_priv->funcs_ok)
		return;

	//gapr::print("paint GL");

//#define USE_PAINTER
#ifdef USE_PAINTER
	QPainter painter;
	if(!painter.begin(this))
		return;
	painter.drawText(QPointF{100.0, 100.0}, QStringLiteral("hello"));
	painter.beginNativePainting();

	/*! reset by painter */
	gl(Enable, _priv->funcs)(GL_BLEND);
	gl(Enable, _priv->funcs)(GL_DEPTH_TEST);
	gl(ClearDepth, _priv->funcs)(1.0);
	gl(ClearColor, _priv->funcs)(0.0, 0.0, 0.0, 1.0);
#endif

	gl(Clear, _priv->funcs)(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

	// XXX move these to apply_changes_stage1 and use makeCurrent...
	auto chg=_priv->_changes1;
	if(chg) {
		_priv->_changes1=0;
		if(chg&mask<chg::closeup_info>) {
			_priv->updateInsetAxis();
		}
		if(chg&mask<chg::closeup_cube>) {
			QGuiApplication::setOverrideCursor(Qt::WaitCursor);
			_priv->updateCubeTexture();
			QGuiApplication::restoreOverrideCursor();
		}
		if(chg&mask<chg::scale_factor, chg::dpr>) {
			if(_priv->funcs_ok)
				_priv->resizeFrameBuffers();
		}
		if(chg&mask<chg::path_data>) {
			auto pathGL=pathToPathGL(_path_data);
			gl(BindBuffer, _priv->funcs)(GL_ARRAY_BUFFER, _priv->pathBuffers[_priv->pbiPath].vbo);
			gl(BufferData, _priv->funcs)(GL_ARRAY_BUFFER, sizeof(PointGL)*pathGL.size(), pathGL.data(), GL_STATIC_DRAW);
		}
	}

	if(_model) {
		_priv->paintEdge();
		_priv->paintOpaque();
		_priv->paintVolume();
		_priv->paintFinish(defaultFramebufferObject());
	} else {
		_priv->paintBlank();
	}
#ifdef USE_PAINTER
	painter.endNativePainting();
	painter.drawText(QPointF{100.0, 200.0}, QStringLiteral("world"));
	//painter.end();
#endif
	//gapr::print("paint GL end");

}

void Canvas::resizeGL(int w, int h) {
	/*! gl context available */
	_priv->_wid_width=w;
	_priv->_wid_height=h;
	_priv->resizeFrameBuffers();
	_priv->updateMVPMatrices();
	//update();
}

void Canvas::wheelEvent(QWheelEvent* event) {
	if(!_model)
		return;
	double d=event->angleDelta().y()/120.0;
	if(_slice_mode) {
		auto radU=_closeup_zoom;
		d=d/radU;
		if(d>0 && d<1) d=1;
		if(d<0 && d>-1) d=-1;
		_priv->slice_delta+=d;
		_changes0|=mask<chg::slice_delta>;
	} else {
		auto dd=std::pow(0.96, d);
		set_closeup_zoom(_closeup_zoom*dd);
	}
	apply_changes();
}
void Canvas::mousePressEvent(QMouseEvent* event) {
	auto x=event->x();
	auto y=event->y();
	if(event->buttons()==Qt::RightButton) {
		return;
	}
	auto& m=_priv->mouse;
	m.xPressed=m.x=x;
	m.yPressed=m.y=y;
	m.moved=false;
	if(event->buttons()!=Qt::LeftButton) {
		m.mode=MouseMode::Pan;
		return;
	}
	if(auto modifs=event->modifiers(); modifs!=Qt::NoModifier) {
		if(modifs==Qt::AltModifier) {
			m.mode=MouseMode::RectSel;
			return;
		}
	}
	auto& mat=_priv->_mat;
	_priv->mrViewProj=mat.mrView*mat.mrProj;
	//gapr::print("mrViewProj", _priv->mrViewProj);
	auto& dir=_direction;
	_priv->_prev_up={(float)dir[0], (float)dir[1], (float)dir[2]};
	_priv->_prev_rgt={(float)dir[3], (float)dir[4], (float)dir[5]};
	m.mode=MouseMode::Rot;
}
void Canvas::mouseReleaseEvent(QMouseEvent* event) {
	auto& m=_priv->mouse;
	// XXX
	if(event->buttons()==Qt::RightButton) {
		return;
	}
	if(!m.moved && _model && m.mode==MouseMode::Rot) {
		// XXX
		auto x=event->x();
		auto y=event->y();
		auto dpr=devicePixelRatio();
		if(_priv->pickPoint(x*dpr, y*dpr, _pick_pos)) {
			Q_EMIT pick_changed();
		}
		m.mode=MouseMode::Nul;
		return;
	}
	switch(m.mode) {
		case MouseMode::Rot:
			break;
		case MouseMode::Pan:
			break;
		case MouseMode::Drag:
			//unsetCursor();
			//Q_EMIT endDrag();
			break;
		case MouseMode::Nul:
			return;
		case MouseMode::RectSel:
			if(_model) {
				if(!m.moved) {
					if(!_nodes_sel.empty()) {
						_nodes_sel.clear();
						_changes0|=mask<chg::sel>;
						Q_EMIT selection_changed();
					}
				} else {
					auto dpr=devicePixelRatio();
					if(_priv->selectNodes(_priv->mouse.xPressed*dpr, _priv->mouse.yPressed*dpr, event->x()*dpr, event->y()*dpr, _nodes_sel)) {
						_changes0|=mask<chg::sel>;
						Q_EMIT selection_changed();
					}
				}
			}
			break;
	}
	m.mode=MouseMode::Nul;
}
void Canvas::mouseMoveEvent(QMouseEvent* event) {
	auto x=event->x();
	auto y=event->y();
	auto& m=_priv->mouse;
	if(m.x!=x || m.y!=y) {
		m.x=x;
		m.y=y;
		m.moved=true;
		switch(m.mode) {
			case MouseMode::Rot:
				set_directions(_priv->handleRotate());
				break;
			case MouseMode::Pan:
				//handlePan(x, y);
				break;
			case MouseMode::Drag:
				//handleDrag(x, y);
				break;
			case MouseMode::RectSel:
				if(_model) {
					auto dpr=devicePixelRatio();
					_priv->updateRectSel(_priv->mouse.xPressed*dpr, _priv->mouse.yPressed*dpr, event->x()*dpr, event->y()*dpr);
					_changes0|=mask<chg::sel_rect>;
				}
				break;
			case MouseMode::Nul:
				return;
		}
	}
	apply_changes();
}

void Canvas::apply_changes_stage1() {
	auto chg=_changes0;
	_changes0=0;
	//fprintf(stderr, "Chg %08X\n", chg);
	_priv->_changes1|=chg;
	bool upd=false;
	if(chg&mask<chg::data_only>) {
		upd=true;
	}
	if(chg&mask<chg::slice_mode>) {
		if(_slice_mode)
			_priv->slice_delta=0;
		upd=true;
	}
	if(chg&mask<chg::slice_delta>) {
		auto slice_delta=_priv->slice_delta;
		int n=_slice_pars[1];
		if(slice_delta<-n) _priv->slice_delta=-n;
		if(slice_delta>n) _priv->slice_delta=n;
	}
	if(chg&mask<chg::closeup_info, chg::direction>) {
		_priv->updateInsetCenter();
		_priv->updateZoomMinMax();
		upd=true;
	}
	if(chg&mask<chg::closeup_zoom, chg::scale_factor, chg::dpr, chg::direction>) {
		if(_closeup_zoom>1*_priv->closeup_max_dim) {
			_closeup_zoom=1*_priv->closeup_max_dim;
		} else if(_closeup_zoom<64*_priv->closeup_min_voxel) {
			_closeup_zoom=64*_priv->closeup_min_voxel;
		}
		_priv->updateMVPMatrices();
		upd=true;
	}
	if(chg&mask<chg::cur_pos>) {
		_priv->clearSelection();
		upd=true;
	}
	if(chg&mask<chg::closeup_cube, chg::cur_pos>) {
		if(_closeup_cube) {
			_priv->updateCubeMatrix();
			upd=true;
		}
	}
	if(chg&mask<chg::slice_delta, chg::closeup_zoom, chg::closeup_xfunc, chg::tgt_pos, chg::path_data, chg::model_update>)
		upd=true;
	if(chg&mask<chg::sel, chg::sel_rect>)
		upd=true;
	if(chg&mask<chg::model>) {
		auto curs=Qt::CrossCursor;
		if(!_model)
			curs=Qt::ForbiddenCursor;
		else if(0/*_busy*/)
			curs=Qt::BusyCursor;
		setCursor(curs);
	}
	if(upd)
		update();
}

void Canvas::edges_removed(const std::vector<edge_model::edge_id>& edges_del) {
	_priv->edges_removed(edges_del);
}

void Canvas::handle_screen_changed(QScreen* screen) {
	assert(_changes0==0);
	auto dpr=screen->devicePixelRatio();
	_priv->set_dpr(dpr);
	update();
}




#if 0
bool Viewer::sliceMode() {
	return priv->slice_mode;
}
bool Viewer::edgeVisibility() {
	return priv->show_edges;
}

	void initGraphPbi() {
		for(auto e: graph.edges()) {
			auto ep=EdgePriv::get(e);
			if(ep->vaoi<=0) {
				auto pathGL=pathToPathGL(ep->points);
				int vaoi=createPbi();
				gl(BindBuffer, funcs)(GL_ARRAY_BUFFER, pathBuffers[vaoi].vbo);
				gl(BufferData, funcs)(GL_ARRAY_BUFFER, sizeof(PointGL)*pathGL.size(), pathGL.data(), GL_STATIC_DRAW);
				ep->vaoi=vaoi;
			}
		}
	}



void Viewer::dropEvent(QDropEvent* drop) {
	auto data=drop->mimeData();
	if(data->hasFormat("text/uri-list")) {
		auto urls=data->urls();
		if(urls.size()==1 && urls[0].isLocalFile()) {
			auto file=urls[0].toLocalFile();
			if(file.endsWith(QLatin1String{".txt"}, Qt::CaseInsensitive)) {
				priv->loadColormap(file);
				update();
			} else if(file.endsWith(QLatin1String{".obj"}, Qt::CaseInsensitive)) {
				priv->loadSurface(file);
				update();
			}
		}
	}
	/*
		if(urls.size()==1) {
		QFile f{urls[0].toLocalFile()};
		if(!f.open(QIODevice::ReadOnly))
		throwError("Failed to open");
		QTextStream fs{&f};
		int group;
		std::vector<int> groups{};
		while(true) {
		fs>>group;
		auto stat=fs.status();
		if(stat!=QTextStream::Ok) {
		if(stat==QTextStream::ReadPastEnd)
		break;
		throwError("Failed to read line");
		}
		printMessage("COLOR ", group);
		groups.push_back(group);
		}
		std::swap(priv->colormap, groups);
		for(auto i: groups) {
		printMessage("COLOR ", i);
		}
		update();
		}
		*/
}
void Viewer::dragEnterEvent(QDragEnterEvent* drop) {
	if(drop->mimeData()->hasFormat("text/uri-list")) {
		auto urls=drop->mimeData()->urls();
		if(urls.size()==1 && urls[0].isLocalFile()) {
			auto file=urls[0].toLocalFile();
			if(file.endsWith(QLatin1String{".txt"}, Qt::CaseInsensitive)
					|| file.endsWith(QLatin1String{".obj"}, Qt::CaseInsensitive)) {
				drop->accept();
			}
		}
	}
}

QImage Viewer::takeScreenshot() {
	if(!priv->graph)
		return {};

	makeCurrent();
	priv->paintEdge();
	priv->paintOpaque();
	priv->paintSurface();
	priv->paintVolume();
	priv->paintSorted(priv->fbo_scale);
	gl(Finish, priv->funcs)();

	QImage img{priv->fbo_width, priv->fbo_height, QImage::Format_ARGB32};
	gl(BindFramebuffer, priv->funcs)(GL_READ_FRAMEBUFFER, priv->fbo_scale);
	gl(ReadBuffer, priv->funcs)(GL_COLOR_ATTACHMENT0);
	gl(ReadPixels, priv->funcs)(0, 0, priv->fbo_width, priv->fbo_height, GL_BGRA, GL_UNSIGNED_BYTE, img.bits());
	doneCurrent();
	return img.mirrored(false, true);
}

#endif

#if 0
class ViewerColorOptions: public OptionsPage {
	ViewerPriv* vp;

	QColor colorsSet[COLOR_NUM];
	QColor colorsDef[COLOR_NUM];
	bool colorsNotDef[COLOR_NUM];

	bool colorModeSet;
	bool colorModeDef;

	bool disabled;
	bool noupdate;

	QCheckBox* checkMode;
	ColorWidget* colorWidgets[COLOR_NUM];

	static ViewerColorOptions* _instance;

	ViewerColorOptions();
	~ViewerColorOptions();

	void getState(bool* a, bool* b, bool* c, bool* d) const override;
	void setStateImp(SessionState ts) override;
	void useDefaultImp() override;
	void saveDefaultImp() override;
	void applyChangesImp() override { }
	void resetChangesImp() override { }

	private Q_SLOTS:
	void checkModeToggled(bool s);
	void colorChanged(int i, const QColor& c);

	public:
	void attach(ViewerPriv* _vp);

	static ViewerColorOptions* instance() {
		if(!_instance)
			_instance=new ViewerColorOptions{};
		return _instance;
	}
};

#endif
#ifdef _FNT_VIEWER_OPTIONS_H_


ViewerColorOptions* ViewerColorOptions::_instance{nullptr};
OptionsPage* OptionsPage::viewerColorOptions() {
	return ViewerColorOptions::instance();
}

void ViewerColorOptions::attach(ViewerPriv* _vp) {
	vp=_vp;
	if(vp) {
		checkMode->setChecked(vp->colorMode);
		for(int i=0; i<COLOR_NUM; i++) {
			colorWidgets[i]->setColor(vp->colors[i]);
		}
	} else {
		checkMode->setChecked(colorModeDef);
		for(int i=0; i<COLOR_NUM; i++) {
			colorWidgets[i]->setColor(colorsDef[i]);
		}
	}
	notifyChange();
}
ViewerColorOptions::ViewerColorOptions():
	OptionsPage{"Color", "Color Settings"}, vp{nullptr}, disabled{true},
	noupdate{false}
{
	auto options=Tracer::instance()->options();

	int v;
	if(!options->getInt("viewer.color.mode", &v)) {
		v=0;
	}
	colorModeSet=colorModeDef=v;

	checkMode=new QCheckBox{"Different colors for each neuron", this};
	layout()->addWidget(checkMode, 0);
	checkMode->setChecked(colorModeDef);
	checkMode->setToolTip("If checked, colors are used to denote different neurons.\nOtherwise, colors are used to denote types of branches.");
	connect(checkMode, &QCheckBox::stateChanged, this, &ViewerColorOptions::checkModeToggled);

	auto flayout=new QFormLayout{};
	layout()->addLayout(flayout, 0);
	for(int i=0; i<COLOR_NUM; i++) {
		auto vals=Tracer::instance()->options()->getIntList(QString{"viewer.color.%1"}.arg(i));
		QColor color{colorData[i].def};
		if(vals.size()>=3) {
			color=QColor{vals[0], vals[1], vals[2]};
		}

		colorsSet[i]=colorsDef[i]=color;
		colorsNotDef[i]=false;

		colorWidgets[i]=new ColorWidget{colorData[i].title, colorData[i].desc, this};
		colorWidgets[i]->setColor(color);
		connect(colorWidgets[i], &ColorWidget::colorChanged, [this, i](const QColor& c) { colorChanged(i, c); });
		flayout->addRow(colorData[i].title, colorWidgets[i]);
	}
	layout()->addStretch(1);
}

ViewerColorOptions::~ViewerColorOptions() {
}
void ViewerColorOptions::getState(bool* a, bool* b, bool* c, bool* d) const {
	*a=false;
	*b=false;
	bool notDefault=colorModeSet!=colorModeDef;
	for(int i=0; i<COLOR_NUM; i++) {
		if(colorsNotDef[i]) {
			notDefault=true;
			break;
		}
	}
	*c=notDefault;
	*d=notDefault;
}
void ViewerColorOptions::setStateImp(SessionState ts) {
	switch(ts) {
		case SessionState::Invalid:
		case SessionState::LoadingCatalog:
		case SessionState::Readonly:
			disabled=true;
			break;
		case SessionState::Ready:
		case SessionState::LoadingCubes:
		case SessionState::Computing:
			disabled=false;
			break;
	}
}
void ViewerColorOptions::useDefaultImp() {
	auto oldv=noupdate;
	noupdate=true;
	if(colorModeDef!=colorModeSet) {
		checkMode->setCheckState(colorModeDef?Qt::Checked:Qt::Unchecked);
	}
	for(int i=0; i<COLOR_NUM; i++) {
		if(colorsNotDef[i]) {
			colorWidgets[i]->setColor(colorsDef[i]);
		}
	}
	noupdate=oldv;
	if(!noupdate && vp && !disabled)
		vp->viewer->update();
}
void ViewerColorOptions::saveDefaultImp() {
	if(colorModeSet!=colorModeDef) {
		colorModeDef=colorModeSet;
		if(colorModeSet) {
			Tracer::instance()->options()->setInt("viewer.color.mode", colorModeSet);
		} else {
			Tracer::instance()->options()->removeKey("viewer.color.mode");
		}
	}
	for(int i=0; i<COLOR_NUM; i++) {
		if(colorsNotDef[i]) {
			colorsNotDef[i]=false;
			colorsDef[i]=colorsSet[i];
			auto key=QString{"viewer.color.%1"}.arg(i);
			if(colorsSet[i]!=colorData[i].def) {
				Tracer::instance()->options()->setIntList(key, {colorsSet[i].red(), colorsSet[i].green(), colorsSet[i].blue()});
			} else {
				Tracer::instance()->options()->setIntList(key, {});
			}
		}
	}
}
void ViewerColorOptions::checkModeToggled(bool s) {
	colorModeSet=s;
	if(vp && !disabled) {
		vp->colorMode=colorModeSet;
		if(!noupdate)
			vp->viewer->update();
	}
	notifyChange();
}
void ViewerColorOptions::colorChanged(int i, const QColor& col) {
	if(col!=colorsSet[i]) {
		colorsSet[i]=col;
		colorsNotDef[i]=col!=colorsDef[i];
		if(vp && !disabled) {
			vp->colors[i]=col;
			if(!noupdate)
				vp->viewer->update();
		}
		notifyChange();
	}
}


#endif
