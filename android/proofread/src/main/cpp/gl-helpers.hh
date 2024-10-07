#include "gapr/gui/opengl.hh"
#include "gapr/gui/opengl-funcs.hh"

struct AssetSource {
	AAssetManager* mgr;
	struct AssetDeletor {
		void operator()(AAsset* a) const {
			AAsset_close(a);
		}
	};
	struct Asset: std::unique_ptr<AAsset, AssetDeletor> {
		Asset(AAsset* a): unique_ptr{a} { }
		const char* data() {
			return static_cast<const char*>(AAsset_getBuffer(get()));
		}
		GLint size() {
			return AAsset_getLength(get());
		}
	};
	Asset load(const char* path) {
		return Asset{AAssetManager_open(mgr, path, AASSET_MODE_BUFFER)};
	}
};

using GlFuncs=gapr::gl::GlobalFunctions;

using Program=gapr::gl::Program<GlFuncs>;
using Framebuffer=gapr::gl::Framebuffer<GlFuncs>;
using FramebufferScale=gapr::gl::Framebuffer<GlFuncs>;

//using FramebufferOpaque=gapr::gl::Framebuffer<std::tuple<>>;
		//gl(ActiveTexture, funcs)(GL_TEXTURE12);
		//gl(ActiveTexture, funcs)(GL_TEXTURE13);

using FramebufferEdges=gapr::gl::MixedPickFramebuffer<GlFuncs>;
		//gl(ActiveTexture, funcs)(GL_TEXTURE8);
		//gl(ActiveTexture, funcs)(GL_TEXTURE9);
		//gl(ActiveTexture, funcs)(GL_TEXTURE10);
		//gl(ActiveTexture, funcs)(GL_TEXTURE11);

//using FramebufferCubes=gapr::gl::Framebuffer<std::tuple<>>;
		//gl(ActiveTexture, funcs)(GL_TEXTURE2);
		//gl(ActiveTexture, funcs)(GL_TEXTURE3);

using VertexArray=gapr::gl::VertexArray<GlFuncs>;
using Texture3D=gapr::gl::Texture3D<GlFuncs>;

class ViewerShared {
	public:
		template<typename F>
			std::shared_ptr<Program> get_prog(std::string&& key, F&& factory) {
				return get_sync<Program, &ViewerShared::_progs, &ViewerShared::_progs_mtx>(std::move(key), std::forward<F>(factory));
			}
		template<typename F>
			std::shared_ptr<VertexArray> get_vao(std::string&& key, F&& factory) {
				return get_sync<VertexArray, &ViewerShared::_vaos, &ViewerShared::_vaos_mtx>(std::move(key), std::forward<F>(factory));
			}
		template<typename F>
			std::shared_ptr<Texture3D> get_tex3d(std::string&& key, F&& factory) {
				// XXX async tex loading? and return future
				return get_sync<Texture3D, &ViewerShared::_tex3ds, &ViewerShared::_tex3ds_mtx>(std::move(key), std::forward<F>(factory));
			}
		void recycle(std::shared_ptr<Texture3D>&& tex) {
			if(!tex)
				return;
			auto t=std::move(tex);
			bool last{false};
			{ std::lock_guard lck{_tex3ds_mtx};
				if(tex.use_count()==2) {
					auto it=_tex3ds.begin();
					while(it!=_tex3ds.end()) {
						if(it->second==tex) {
							_tex3ds.erase(it);
							break;
						}
						++it;
					}
					last=true;
				}
			}
			if(last)
				t->destroy();
		}

		void create() {
			std::lock_guard lck{_mtx};
			if(++_refc==1) {
				// XXX init
			}
		}
		void destroy() {
			std::lock_guard lck{_mtx};
			if(--_refc==0) {
				// XXX deinit
				// how to destroy???
			}
		}

		// XXX
		std::shared_ptr<VertexArray> get_vao_fixed();

	private:
		std::mutex _mtx;
		unsigned int _refc{0};
		std::unordered_map<std::string, std::shared_ptr<Program>> _progs;
		std::mutex _progs_mtx;
		std::unordered_map<std::string, std::shared_ptr<VertexArray>> _vaos;
		std::mutex _vaos_mtx;
		std::unordered_map<std::string, std::shared_ptr<Texture3D>> _tex3ds;
		std::mutex _tex3ds_mtx;

		template<typename T, auto MAP, auto MTX, typename F>
			std::shared_ptr<T> get_sync(std::string&& key, F&& factory) {
				std::lock_guard lck{this->*MTX};
				auto [it, ins]=(this->*MAP).emplace(std::move(key), nullptr);
				if(it->second)
					return it->second;
				auto p=std::make_shared<T>();
				std::forward<F>(factory)(*p);
				it->second=p;
				return p;
			}
};

std::shared_ptr<VertexArray> ViewerShared::get_vao_fixed() {
	return get_vao("...fixed_data", [](VertexArray& vao) {
		LOGV("generate vao.fixed");
		GLfloat cross_a=1.2f, cross_b=0.5f/10, cross_c=4.0f/10;
		GLfloat cursor_a=30.0f, cursor_b=0.5f, cursor_c=3.0f;
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

				cursor_c-cursor_b, cursor_c+cursor_b, 0.0f, // 60 cursor
				cursor_c+cursor_b, cursor_c-cursor_b, 0.0f,
				cursor_a-cursor_b, cursor_a+cursor_b, 0.0f,
				cursor_a+cursor_b, cursor_a-cursor_b, 0.0f,
				cursor_a-cursor_b, cursor_a+cursor_b, 0.0f,
				cursor_c+cursor_b, cursor_c-cursor_b, 0.0f,
				-cursor_c-cursor_b, cursor_c-cursor_b, 0.0f,
				-cursor_c+cursor_b, cursor_c+cursor_b, 0.0f,
				-cursor_a-cursor_b, cursor_a-cursor_b, 0.0f,
				-cursor_a+cursor_b, cursor_a+cursor_b, 0.0f,
				-cursor_a-cursor_b, cursor_a-cursor_b, 0.0f,
				-cursor_c+cursor_b, cursor_c+cursor_b, 0.0f,
				-cursor_c+cursor_b, -cursor_c-cursor_b, 0.0f,
				-cursor_c-cursor_b, -cursor_c+cursor_b, 0.0f,
				-cursor_a+cursor_b, -cursor_a-cursor_b, 0.0f,
				-cursor_a-cursor_b, -cursor_a+cursor_b, 0.0f,
				-cursor_a+cursor_b, -cursor_a-cursor_b, 0.0f,
				-cursor_c-cursor_b, -cursor_c+cursor_b, 0.0f,
				cursor_c+cursor_b, -cursor_c+cursor_b, 0.0f,
				cursor_c-cursor_b, -cursor_c-cursor_b, 0.0f,
				cursor_a+cursor_b, -cursor_a+cursor_b, 0.0f,
				cursor_a-cursor_b, -cursor_a-cursor_b, 0.0f,
				cursor_a+cursor_b, -cursor_a+cursor_b, 0.0f,
				cursor_c-cursor_b, -cursor_c-cursor_b, 0.0f,

				-cross_a, cross_b, 0.0f, // 84 cross
				-cross_a, -cross_b, 0.0f,
				cross_a, -cross_b, 0.0f,
				cross_a, -cross_b, 0.0f,
				cross_a, cross_b, 0.0f,
				-cross_a, cross_b, 0.0f,
				cross_b, cross_a, 0.0f,
				-cross_b, cross_a, 0.0f,
				-cross_b, -cross_a, 0.0f,
				-cross_b, -cross_a, 0.0f,
				cross_b, -cross_a, 0.0f,
				cross_b, cross_a, 0.0f,

				-cross_a, cross_b, 0.0f, // 96 cross open
				-cross_a, -cross_b, 0.0f,
				-cross_c, -cross_b, 0.0f,
				-cross_c, -cross_b, 0.0f,
				-cross_c, cross_b, 0.0f,
				-cross_a, cross_b, 0.0f,
				cross_c, cross_b, 0.0f,
				cross_c, -cross_b, 0.0f,
				cross_a, -cross_b, 0.0f,
				cross_a, -cross_b, 0.0f,
				cross_a, cross_b, 0.0f,
				cross_c, cross_b, 0.0f,
				cross_b, cross_a, 0.0f,
				-cross_b, cross_a, 0.0f,
				-cross_b, cross_c, 0.0f,
				-cross_b, cross_c, 0.0f,
				cross_b, cross_c, 0.0f,
				cross_b, cross_a, 0.0f,
				cross_b, -cross_c, 0.0f,
				-cross_b, -cross_c, 0.0f,
				-cross_b, -cross_a, 0.0f,
				-cross_b, -cross_a, 0.0f,
				cross_b, -cross_a, 0.0f,
				cross_b, -cross_c, 0.0f,
		};
		fixed_data.resize(600);
		for(int i=0; i<32; i++) {
			float x=cos(i*2*M_PI/32);
			float y=sin(i*2*M_PI/32);
			fixed_data.emplace_back(x);
			fixed_data.emplace_back(y);
			fixed_data.emplace_back(0);
		}
		struct Point3 {
			std::array<GLfloat, 3> pt;
		};
		vao.create<>(&Point3::pt);
		glBufferData(GL_ARRAY_BUFFER, fixed_data.size()*sizeof(GLfloat), fixed_data.data(), GL_STATIC_DRAW);
	});
}

struct PathBuffer: VertexArray {
	unsigned int prev_unused;
};

///////////////////////////////////
	////////////////////////////

#if 000
	bool initialize() {

		/* after create
		progs[P_SORT]->bind();
		progs[P_SORT]->setUniformValue("tex_edges_depth", 8);
		progs[P_SORT]->setUniformValue("tex_edges_color", 11);
		progs[P_SORT]->setUniformValue("tex_opaque_depth", 12);
		progs[P_SORT]->setUniformValue("tex_opaque_color", 13);
		progs[P_SORT]->setUniformValue("tex_volume0_depth", 2);
		progs[P_SORT]->setUniformValue("tex_volume0_color", 3);
		progs[P_VOLUME]->bind();
		progs[P_VOLUME]->setUniformValue("tex3d_cube", 1);
		*/
}
#endif

static const int pickBuffOrder[]={
	40,31,39,41,49,30,32,48,50,22,
	38,42,58,21,23,29,33,47,51,57,
	59,20,24,56,60,13,37,43,67,12,
	14,28,34,46,52,66,68,11,15,19,
	25,55,61,65,69,4,36,44,76,3,
	5,27,35,45,53,75,77,10,16,64,70};

enum Color : int {
	C_CHAN0=0,
	C_CHAN1,
	C_CHAN2,
	C_AXIS,
	C_ALERT,
	C_ATTENTION,
	C_TYPE_0,
	C_TYPE_1,
	C_TYPE_2,
	C_TYPE_3,
	C_TYPE_4,
	C_TYPE_5,
	C_TYPE_6,
	C_TYPE_7,
	C_TYPE_OTHER,
	COLOR_NUM
};

struct {
	Color col;
	gapr::vec3<GLfloat> def;
	const char* title;
	const char* desc;
} colorData[]={
	{C_CHAN0, gapr::vec3<GLfloat>{1.0, 1.0, 1.0}, "Channel 1", "Channel 1"},
	{C_CHAN1, gapr::vec3<GLfloat>{0, 1.0, 0}, "Channel 2", "Channel 2"},
	{C_CHAN2, gapr::vec3<GLfloat>{0, 0, 1.0}, "Channel 3", "Channel 3"},
	{C_AXIS, gapr::vec3<GLfloat>{0.16, 1.0, 1.0}, "Axis", "Axis lines and labels"},
	{C_ALERT, gapr::vec3<GLfloat>{1.0, 0, 0}, "Warning", "Indication of loops or unfinished vertices"},
	{C_ATTENTION, gapr::vec3<GLfloat>{1.0, 0.5, 0}, "Attention", "Current and target position, tracing progress, and candidate path"},
	{C_TYPE_0, gapr::vec3<GLfloat>{0, 0.5, 1.0}, "Color0", "Type 0 (Undefined) or neuron number 0 (modulo 8)"},
	{C_TYPE_1, gapr::vec3<GLfloat>{0, 0, 1.0}, "Color1", "Type 1 (Soma) or neuron number 1 (modulo 8)"},
	{C_TYPE_2, gapr::vec3<GLfloat>{0, 1.0, 0}, "Color2", "Type 2 (Dendrite) or neuron number 2 (modulo 8)"},
	{C_TYPE_3, gapr::vec3<GLfloat>{0, 0, 0}, "Color3", "Type 3 (Apical Dendrite) or neuron number 3 (modulo 8)"},
	{C_TYPE_4, gapr::vec3<GLfloat>{0, 1.0, 1.0}, "Color4", "Type 4 (Axon) or neuron number 4 (modulo 8)"},
	{C_TYPE_5, gapr::vec3<GLfloat>{0.5, 0, 1.0}, "Color5", "Type 5 (Mark 1) or neuron number 5 (modulo 8)"},
	{C_TYPE_6, gapr::vec3<GLfloat>{0.5, 0.5, 1.0}, "Color6", "Type 6 (Mark 2) or neuron number 6 (modulo 8)"},
	{C_TYPE_7, gapr::vec3<GLfloat>{1.0, 1.0, 0.5}, "Color7", "Type 7 (Mark 3) or neuron number 7 (modulo 8)"},
	{C_TYPE_OTHER, gapr::vec3<GLfloat>{1.0, 0.5, 0.5}, "ColorX", "Other types or not part of a neuron"}
};


#define PICK_SIZE 5
#define FBO_ALLOC 64
#define FBO_ALIGN 16

#if 0
	{
		GLint v;
		glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &v);
		LOGV("MAX-3d_TEX_SIZE %d\n", v);
	}
#endif
