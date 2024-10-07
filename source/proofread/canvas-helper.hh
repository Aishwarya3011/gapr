std::ostream& operator<<(std::ostream& str, const QString& s) {
	return str<<s.toStdString();
}

namespace {

	template<typename Res, typename T, typename... Args>
		struct glCallAndCheckHelper {
			T* p;
			Res (T::*f)(Args...);
			const char* file;
			int line;
			glCallAndCheckHelper(T* _p, Res (T::*_f)(Args...), const char* _file, int _line): p{_p}, f{_f}, file{_file}, line{_line} { }
			Res operator()(Args... args) {
				Res r=(p->*f)(args...);
				auto e=p->glGetError();
				if(e!=GL_NO_ERROR)
					gapr::report(file, ':', line, ": GL Error ", e);
				return r;
			}
		};

	template<typename T, typename... Args>
		struct glCallAndCheckHelper<void, T, Args...> {
			T* p;
			void (T::*f)(Args...);
			const char* file;
			int line;
			glCallAndCheckHelper(T* _p, void (T::*_f)(Args...), const char* _file, int _line): p{_p}, f{_f}, file{_file}, line{_line} { }
			void operator()(Args... args) {
				(p->*f)(args...);
				auto e=p->glGetError();
				if(e!=GL_NO_ERROR)
					gapr::report(file, ':', line, ": GL Error ", e);
			}
		};

	// XXX
	template<typename Res, typename T, typename... Args>
		inline glCallAndCheckHelper<Res, T, Args...> glCallAndCheck(Res (T::*func)(Args...), T* ptr, const char* file, int line) {
			return glCallAndCheckHelper<Res, T, Args...>{ptr, func, file, line};
		}
	template<typename Res, typename T, typename... Args>
		inline glCallAndCheckHelper<Res, T, Args...> glCallAndCheck(Res (T::*func)(Args...), T& ptr, const char* file, int line) {
			return glCallAndCheckHelper<Res, T, Args...>{&ptr, func, file, line};
		}
#define gl(suffix, ptr) glCallAndCheck(&OpenGLFunctions::gl##suffix, ptr, __FILE__, __LINE__)


	struct FramebufferScale {
		GLuint fbo{0};
		GLuint tex[2]{0, 0};
		OpenGLFunctions& funcs;
		FramebufferScale(OpenGLFunctions& funcs) noexcept: funcs{funcs} { }

		void create(GLsizei fbo_width, GLsizei fbo_height) {
			gl(GenFramebuffers, funcs)(1, &fbo);
			gl(GenTextures, funcs)(2, tex);
			gl(BindFramebuffer, funcs)(GL_FRAMEBUFFER, fbo);
			gl(ActiveTexture, funcs)(GL_TEXTURE0);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, tex[0]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_DEPTH_STENCIL, fbo_width, fbo_height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
			gl(FramebufferTexture2D, funcs)(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, tex[0], 0);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, tex[1]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_RGBA8, fbo_width, fbo_height, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, nullptr);
			gl(FramebufferTexture2D, funcs)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[1], 0);
			gl(DrawBuffer, funcs)(GL_COLOR_ATTACHMENT0);
			auto check_fbo=gl(CheckFramebufferStatus, funcs)(GL_FRAMEBUFFER);
			if(check_fbo!=GL_FRAMEBUFFER_COMPLETE) {
				gapr::report("Failed to create framebuffer fbo_scale: ", check_fbo);
			}
			gapr::print("sizeof OpenGLFunctions: ", sizeof(OpenGLFunctions));
		}
		~FramebufferScale() {
			gl(DeleteFramebuffers, funcs)(1, &fbo);
			gl(DeleteTextures, funcs)(2, tex);
		}
	};

	struct FramebufferOpaque {
		GLuint fbo{0};
		GLuint tex[2]{0, 0};
		OpenGLFunctions& funcs;
		FramebufferOpaque(OpenGLFunctions& funcs) noexcept: funcs{funcs} { }

		void create(GLsizei fbo_width, GLsizei fbo_height) {
			gl(GenFramebuffers, funcs)(1, &fbo);
			gl(GenTextures, funcs)(2, tex);
			gl(BindFramebuffer, funcs)(GL_FRAMEBUFFER, fbo);
			gl(ActiveTexture, funcs)(GL_TEXTURE12);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, tex[0]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_DEPTH_STENCIL, fbo_width, fbo_height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			gl(FramebufferTexture2D, funcs)(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, tex[0], 0);
			gl(ActiveTexture, funcs)(GL_TEXTURE13);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, tex[1]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_RGBA8, fbo_width, fbo_height, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, nullptr);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			gl(FramebufferTexture2D, funcs)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[1], 0);
			gl(DrawBuffer, funcs)(GL_COLOR_ATTACHMENT0);
			auto check_fbo=funcs.glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if(check_fbo!=GL_FRAMEBUFFER_COMPLETE) {
				gapr::report("Failed to create framebuffer fbo_opaque: %1", check_fbo);
			}
		}
		~FramebufferOpaque() {
			gl(ActiveTexture, funcs)(GL_TEXTURE12);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, 0);
			gl(ActiveTexture, funcs)(GL_TEXTURE13);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, 0);
			gl(DeleteFramebuffers, funcs)(1, &fbo);
			gl(DeleteTextures, funcs)(2, tex);
		}
	};

	struct FramebufferEdges {
		GLuint fbo{0};
		/*! pick buffer goes here.
		 * generalize picking??? <u32 name, i32 pos>
		 */
		GLuint tex[4]{0, 0, 0, 0};
		OpenGLFunctions& funcs;
		FramebufferEdges(OpenGLFunctions& funcs) noexcept: funcs{funcs} { }

		void create(GLsizei fbo_width, GLsizei fbo_height) {
			gl(GenFramebuffers, funcs)(1, &fbo);
			gl(GenTextures, funcs)(4, tex);
			gl(BindFramebuffer, funcs)(GL_FRAMEBUFFER, fbo);
			gl(ActiveTexture, funcs)(GL_TEXTURE8);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, tex[0]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, fbo_width, fbo_height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			gl(FramebufferTexture2D, funcs)(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex[0], 0);
			gl(ActiveTexture, funcs)(GL_TEXTURE9);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, tex[1]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_R32I, fbo_width, fbo_height, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			gl(FramebufferTexture2D, funcs)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[1], 0);
			gl(ActiveTexture, funcs)(GL_TEXTURE10);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, tex[2]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_R32I, fbo_width, fbo_height, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			gl(FramebufferTexture2D, funcs)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, tex[2], 0);
			gl(ActiveTexture, funcs)(GL_TEXTURE11);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, tex[3]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_RGBA8, fbo_width, fbo_height, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, nullptr);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			gl(FramebufferTexture2D, funcs)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, tex[3], 0);
			GLenum bufs[]={GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
			gl(DrawBuffers, funcs)(3, bufs);
			auto check_fbo=funcs.glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if(check_fbo!=GL_FRAMEBUFFER_COMPLETE) {
				gapr::report("Failed to create framebuffer fbo_edges %1", check_fbo);
			}
		}
		~FramebufferEdges() {
			gl(ActiveTexture, funcs)(GL_TEXTURE8);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, 0);
			gl(ActiveTexture, funcs)(GL_TEXTURE9);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, 0);
			gl(ActiveTexture, funcs)(GL_TEXTURE10);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, 0);
			gl(ActiveTexture, funcs)(GL_TEXTURE11);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, 0);
			gl(DeleteFramebuffers, funcs)(1, &fbo);
			gl(DeleteTextures, funcs)(4, tex);
		}
	};

	struct FramebufferCubes {
		GLuint fbo{0};
		GLuint tex[2]{0, 0};
		OpenGLFunctions& funcs;
		FramebufferCubes(OpenGLFunctions& funcs) noexcept: funcs{funcs} { }

		void create(GLsizei fbo_width, GLsizei fbo_height) {
			gl(GenFramebuffers, funcs)(1, &fbo);
			gl(GenTextures, funcs)(2, tex);
			gl(BindFramebuffer, funcs)(GL_FRAMEBUFFER, fbo);
			gl(ActiveTexture, funcs)(GL_TEXTURE2);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, tex[0]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_DEPTH_STENCIL, fbo_width, fbo_height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			gl(FramebufferTexture2D, funcs)(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, tex[0], 0);
			gl(ActiveTexture, funcs)(GL_TEXTURE3);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, tex[1]);
			gl(TexImage2D, funcs)(GL_TEXTURE_2D, 0, GL_RGBA8, fbo_width, fbo_height, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, nullptr);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			gl(TexParameteri, funcs)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			gl(FramebufferTexture2D, funcs)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[1], 0);
			gl(DrawBuffer, funcs)(GL_COLOR_ATTACHMENT0);
			auto check_fbo=funcs.glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if(check_fbo!=GL_FRAMEBUFFER_COMPLETE) {
				gapr::report("Failed to create framebuffer fbo_cubes: %1", check_fbo);
			}
		}
		~FramebufferCubes() {
			gl(ActiveTexture, funcs)(GL_TEXTURE2);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, 0);
			gl(ActiveTexture, funcs)(GL_TEXTURE3);
			gl(BindTexture, funcs)(GL_TEXTURE_2D, 0);
			gl(DeleteFramebuffers, funcs)(1, &fbo);
			gl(DeleteTextures, funcs)(2, tex);
		}
	};

	struct PathBuffer {
		GLuint vao;
		GLuint vbo;
		int prev_unused;
	};
	struct VertBuffer {
	};
	struct CubeTexture {
		GLuint tex;
		int refc;
		//CubeId cubeId;
		int prev_unused;
	};


	enum ShaderProg {
		// Volume
		P_VOLUME=0,
		// misc
		// Edges
		P_EDGE,
		P_VERT,
		// Misc
		P_LINE,
		P_MESH,
		P_MARK,
		P_SORT,
		P_EDGE_PR,
		SHADER_PROG_NUM
	};

	struct {
		ShaderProg prog;
		const char* vert;
		const char* geom;
		const char* frag;
	} shaderSrcs[]= {
		{P_VOLUME, ":/glsl/volume.vert", nullptr, ":/glsl/volume.frag"},
		{P_EDGE_PR, ":/glsl/edge.pr.vert", ":/glsl/edge.pr.geom", ":/glsl/edge.pr.frag"},
		{P_LINE, ":/glsl/line2.vert", ":/glsl/line2.geom", ":/glsl/line2.frag"},
		{P_MESH, ":/glsl/mesh.vert", nullptr, ":/glsl/mesh.frag"},
		{P_MARK, ":/glsl/mark.vert", ":/glsl/mark.geom", ":/glsl/mark.frag"},
		{P_VERT, ":/glsl/vert.vert", nullptr, ":/glsl/vert.frag"},
		{P_SORT, ":/glsl/sort.vert", nullptr, ":/glsl/sort.pr.frag"},
	};

	struct ViewerShared {
		QOpenGLShaderProgram* progs[SHADER_PROG_NUM];
		std::vector<VertBuffer> vertBuffers;
		std::vector<CubeTexture> cubeTextures;
		GLuint vao_fixed{0};
		GLuint vbo_fixed{0};
		int refc;

		ViewerShared():
			progs{nullptr,},
			vertBuffers{},
			cubeTextures{},
			refc{0}
		{
			CubeTexture texHead;
			texHead.tex=0;
			texHead.refc=0;
			texHead.prev_unused=0;
			cubeTextures.emplace_back(texHead);
		}
		bool initialize(OpenGLFunctions* funcs) {
			if(refc) {
				refc++;
				return true;
			}

			for(auto& ss: shaderSrcs) {
				auto t0=std::chrono::steady_clock::now();
				gapr::print(ss.vert, " ", ss.frag);
				progs[ss.prog]=new QOpenGLShaderProgram{QCoreApplication::instance()};
				if(!progs[ss.prog])
					gapr::report("Failed to allocate shader program");
				if(!progs[ss.prog]->create())
					gapr::report("Failed to create shader program");
				if(!progs[ss.prog]->addShaderFromSourceFile(QOpenGLShader::Vertex, ss.vert))
					gapr::report("Failed to compile vertex shader: "+progs[ss.prog]->log());
				if(!progs[ss.prog]->addShaderFromSourceFile(QOpenGLShader::Fragment, ss.frag))
					gapr::report("Failed to compile fragment shader: "+progs[ss.prog]->log());
				if(ss.geom) {
					if(!progs[ss.prog]->addShaderFromSourceFile(QOpenGLShader::Geometry, ss.geom))
						gapr::report("Failed to compile geom shader: "+progs[ss.prog]->log());
				}
				if(!progs[ss.prog]->link())
					gapr::report("Failed to link shaders: "+progs[ss.prog]->log());
				auto t1=std::chrono::steady_clock::now();
				gapr::print("   ", std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count());
			}
			progs[P_SORT]->bind();
			progs[P_SORT]->setUniformValue("tex_edges_depth", 8);
			progs[P_SORT]->setUniformValue("tex_edges_color", 11);
			progs[P_SORT]->setUniformValue("tex_opaque_depth", 12);
			progs[P_SORT]->setUniformValue("tex_opaque_color", 13);
			progs[P_SORT]->setUniformValue("tex_volume0_depth", 2);
			progs[P_SORT]->setUniformValue("tex_volume0_color", 3);
			progs[P_VOLUME]->bind();
			progs[P_VOLUME]->setUniformValue("tex3d_cube", 1);

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
			fixed_data.resize(420+(2+4)*3);

			gl(GenBuffers, funcs)(1, &vbo_fixed);
			gl(BindBuffer, funcs)(GL_ARRAY_BUFFER, vbo_fixed);
			gl(BufferData, funcs)(GL_ARRAY_BUFFER, fixed_data.size()*sizeof(GLfloat), fixed_data.data(), GL_STATIC_DRAW);
			gl(GenVertexArrays, funcs)(1, &vao_fixed);
			gl(BindVertexArray, funcs)(vao_fixed);
			gl(EnableVertexAttribArray, funcs)(0);
			gl(VertexAttribPointer, funcs)(0, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat)*3, 0);

			refc++;
			return true;
		}
		bool deinitialize(OpenGLFunctions* funcs) {
			if(!refc)
				return false;
			refc--;
			if(refc)
				return true;

			if(vbo_fixed)
				gl(DeleteBuffers, funcs)(1, &vbo_fixed);
			if(vao_fixed)
				gl(DeleteVertexArrays, funcs)(1, &vao_fixed);
			for(auto& prog: progs) {
				if(prog) delete prog;
			}
			for(size_t i=1; i<cubeTextures.size(); i++) {
				gl(DeleteTextures, funcs)(1, &cubeTextures[i].tex);
			}
			return true;
		}
		//

		//
		//...
		//

		~ViewerShared() {
		}

		bool releaseTexture(OpenGLFunctions* funcs, int i) {
			if(!cubeTextures[i].refc)
				return false;
			cubeTextures[i].refc--;
			if(cubeTextures[i].refc)
				return true;

			if(cubeTextures[i].tex) {
				gl(DeleteTextures, funcs)(1, &cubeTextures[i].tex);
				cubeTextures[i].tex=0;
			}
			cubeTextures[i].prev_unused=cubeTextures[0].prev_unused;
			cubeTextures[0].prev_unused=i;
			return true;
		}
		int getTexture(OpenGLFunctions* funcs, const gapr::cube& data) {
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
					gl(GenTextures, funcs)(1, &cubeTextures[i].tex);
					gl(ActiveTexture, funcs)(GL_TEXTURE1);
					gl(BindTexture, funcs)(GL_TEXTURE_3D, cubeTextures[i].tex);
					gl(TexParameteri, funcs)(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					gl(TexParameteri, funcs)(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					gl(TexParameteri, funcs)(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					gl(TexParameteri, funcs)(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
					gl(TexParameteri, funcs)(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
					gl(BindTexture, funcs)(GL_TEXTURE_3D, cubeTextures[i].tex);
					gapr::print("update tex ", 8);
					gl(TexImage3D, funcs)(GL_TEXTURE_3D, 0, GL_R8, width, cube_view.sizes(1), cube_view.sizes(2), 0, GL_RED, GL_UNSIGNED_BYTE, cube_view.row(0, 0));
					break;
				case gapr::cube_type::u16:
					gl(GenTextures, funcs)(1, &cubeTextures[i].tex);
					gl(ActiveTexture, funcs)(GL_TEXTURE1);
					gl(BindTexture, funcs)(GL_TEXTURE_3D, cubeTextures[i].tex);
					gl(TexParameteri, funcs)(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					gl(TexParameteri, funcs)(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					gl(TexParameteri, funcs)(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					gl(TexParameteri, funcs)(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
					gl(TexParameteri, funcs)(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
					gl(BindTexture, funcs)(GL_TEXTURE_3D, cubeTextures[i].tex);
					gl(TexImage3D, funcs)(GL_TEXTURE_3D, 0, GL_R16, width, height, depth, 0, GL_RED, GL_UNSIGNED_SHORT, ptr);
					break;
				case gapr::cube_type::f32:
					gl(GenTextures, funcs)(1, &cubeTextures[i].tex);
					gl(ActiveTexture, funcs)(GL_TEXTURE1);
					gl(BindTexture, funcs)(GL_TEXTURE_3D, cubeTextures[i].tex);
					gl(TexParameteri, funcs)(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					gl(TexParameteri, funcs)(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					gl(TexParameteri, funcs)(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					gl(TexParameteri, funcs)(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
					gl(TexParameteri, funcs)(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
					gl(BindTexture, funcs)(GL_TEXTURE_3D, cubeTextures[i].tex);
					gapr::print("update tex ", 32);
					gl(TexImage3D, funcs)(GL_TEXTURE_3D, 0, GL_R32F, width, cube_view.sizes(1), cube_view.sizes(2), 0, GL_RED, GL_FLOAT, cube_view.row(0, 0));
					break;
				default:
					break;
			}
			//cubeTextures[i].cubeId=data.cubeId();
			cubeTextures[i].refc++;
			return i;
		}
	};

	struct VertexArrayProgr {
		GLuint vao{0};
		GLuint vbo{0};
		OpenGLFunctions& funcs;
		VertexArrayProgr(OpenGLFunctions& funcs) noexcept: funcs{funcs} { }

		void create() {
			gl(GenVertexArrays, funcs)(1, &vao);
			gl(BindVertexArray, funcs)(vao);
			gl(GenBuffers, funcs)(1, &vbo);
			gl(BindBuffer, funcs)(GL_ARRAY_BUFFER, vbo);
			gl(EnableVertexAttribArray, funcs)(0);
			gl(VertexAttribIPointer, funcs)(0, 3, GL_INT, sizeof(GLint)*3, nullptr);
		}
		~VertexArrayProgr() {
			gl(DeleteVertexArrays, funcs)(1, &vao);
		}
	};

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
		QColor def;
		const char* title;
		const char* desc;
	} colorData[]={
		{C_CHAN0, QColor{255, 255, 255}, "Channel 1", "Channel 1"},
		{C_CHAN1, QColor{0, 255, 0}, "Channel 2", "Channel 2"},
		{C_CHAN2, QColor{0, 0, 255}, "Channel 3", "Channel 3"},
		{C_AXIS, QColor{40, 255, 255}, "Axis", "Axis lines and labels"},
		{C_ALERT, QColor{255, 0, 0}, "Warning", "Indication of loops or unfinished vertices"},
		{C_ATTENTION, QColor{255, 127, 0}, "Attention", "Current and target position, tracing progress, and candidate path"},
		{C_TYPE_0, QColor{0, 127, 255}, "Color0", "Type 0 (Undefined) or neuron number 0 (modulo 8)"},
		{C_TYPE_1, QColor{0, 0, 255}, "Color1", "Type 1 (Soma) or neuron number 1 (modulo 8)"},
		{C_TYPE_2, QColor{0, 255, 0}, "Color2", "Type 2 (Dendrite) or neuron number 2 (modulo 8)"},
		{C_TYPE_3, QColor{0, 0, 0}, "Color3", "Type 3 (Apical Dendrite) or neuron number 3 (modulo 8)"},
		{C_TYPE_4, QColor{0, 255, 255}, "Color4", "Type 4 (Axon) or neuron number 4 (modulo 8)"},
		{C_TYPE_5, QColor{127, 0, 255}, "Color5", "Type 5 (Mark 1) or neuron number 5 (modulo 8)"},
		{C_TYPE_6, QColor{127, 127, 255}, "Color6", "Type 6 (Mark 2) or neuron number 6 (modulo 8)"},
		{C_TYPE_7, QColor{255, 255, 127}, "Color7", "Type 7 (Mark 3) or neuron number 7 (modulo 8)"},
		{C_TYPE_OTHER, QColor{255, 127, 127}, "ColorX", "Other types or not part of a neuron"}
	};

}
