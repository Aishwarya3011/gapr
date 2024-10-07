/* gapr/gui/opengl.hh
 *
 * Copyright (C) 2020 GOU Lingfeng
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


#ifndef _GAPR_GUI_OPENGL_HH_
#define _GAPR_GUI_OPENGL_HH_

#include "gapr/vec3.hh"

/*! bind to different gl impl. at compile time:
 * epoxy gles qopenglfunctions...
 */
#if defined(GL_ES_VERSION_3_0)
#elif defined(GL_VERSION_3_3)
#else
#error "include GL headers before this file"
#endif

namespace gapr::gl {
	enum class errc;
}
template<> struct std::is_error_code_enum<gapr::gl::errc>: true_type { };

namespace gapr::gl {

	enum class errc {
		invalid_enum=GL_INVALID_ENUM,
		invalid_value=GL_INVALID_VALUE,
		invalid_op=GL_INVALID_OPERATION,
		oom=GL_OUT_OF_MEMORY
	};
	const std::error_category& error_category() noexcept;
	inline std::error_code make_error_code(errc e) noexcept {
		return std::error_code{static_cast<int>(e), error_category()};
	}
#if 0
	inline std::error_condition make_error_condition(errc e) noexcept {
		return std::error_condition{static_cast<int>(e), error_category()};
	}
#endif
	inline void check_error(GLenum err, const char* msg) {
		if(err==GL_NO_ERROR)
			return;
		std::error_code ec{static_cast<errc>(err)};
		if(0)
			throw std::system_error{ec, msg};
		gapr::print(msg, ": ", ec.message());
	}

				//
				//XXX
				//raii<has_context>
				//raii<has_no_context>

	template<typename Funcs>
		class Framebuffer_base: private Funcs {
			protected:
				constexpr Framebuffer_base() noexcept: Funcs{}, _fbo{0} { }
				~Framebuffer_base() {
					if(_fbo)
						gapr::print("fbo not deleted");
				}
				template<std::size_t N> static void init_tex(GLuint (&tex)[N]) {
					for(auto& t: tex)
						t=0;
				}
				template<std::size_t N> static void check_tex(GLuint (&tex)[N]) {
					for(auto t: tex) {
						if(t)
							gapr::print("fbo.tex not deleted");
					}
				}
				template<std::size_t N> void gen_objs(GLuint (&tex)[N]) {
					Funcs::initialize();
					this->glGenFramebuffers(1, &_fbo);
					this->glGenTextures(N, tex);
				}
				template<std::size_t N> void del_objs(GLuint (&tex)[N]) {
					this->glDeleteTextures(N, tex);
					for(auto& t: tex)
						t=0;
					if(_fbo!=0) {
						this->glDeleteFramebuffers(1, &_fbo);
						_fbo=0;
					}
					check_error(this->glGetError(), "destroy fbo");
				}
				void setup_depth(GLsizei width, GLsizei height, GLuint tex) {
					this->glBindTexture(GL_TEXTURE_2D, tex);
					this->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
					this->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
					//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
					//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
					this->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8/*GL_DEPTH_STENCIL*/, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
					check_error(this->glGetError(), "err tex2d depth");
				}
				void resize_depth(GLsizei width, GLsizei height, GLuint tex) {
					this->glBindTexture(GL_TEXTURE_2D, tex);
					this->glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8/*GL_DEPTH_STENCIL*/, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
				}
#ifndef GL_ES_VERSION_3_0
				void setup_depth(GLsizei width, GLsizei height, GLsizei samples, GLuint tex) {
					this->glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tex);
					this->glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_DEPTH_STENCIL/*GL_DEPTH_COMPONENT*/, width, height, /*GL_TRUE*/GL_FALSE);
				}
				void resize_depth(GLsizei width, GLsizei height, GLsizei samples, GLuint tex) {
					this->glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tex);
					this->glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_DEPTH_STENCIL/*GL_DEPTH_COMPONENT*/, width, height, GL_FALSE/*GL_TRUE*/);
				}
#endif
				void setup_color(GLsizei width, GLsizei height, GLuint tex) {
					this->glBindTexture(GL_TEXTURE_2D, tex);
					this->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
					this->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
					//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
					//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
					this->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE/*GL_UNSIGNED_INT_8_8_8_8*/, nullptr);
					check_error(this->glGetError(), "err tex2d col");
				}
				void resize_color(GLsizei width, GLsizei height, GLuint tex) {
					this->glBindTexture(GL_TEXTURE_2D, tex);
					this->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE/*GL_UNSIGNED_INT_8_8_8_8*/, nullptr);
				}
#ifndef GL_ES_VERSION_3_0
				void setup_color(GLsizei width, GLsizei height, GLsizei samples, GLuint tex) {
					this->glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tex);
					this->glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGBA8, width, height, GL_FALSE/*GL_TRUE*/);
				}
				void resize_color(GLsizei width, GLsizei height, GLsizei samples, GLuint tex) {
					this->glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, tex);
					this->glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples, GL_RGBA8, width, height, GL_FALSE/*GL_TRUE*/);
				}
#endif
				void setup_int(GLsizei width, GLsizei height, GLuint tex, GLint format, GLenum type) {
					this->glBindTexture(GL_TEXTURE_2D, tex);
					//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
					//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
					this->glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RED_INTEGER, type, nullptr);
					check_error(this->glGetError(), "err tex2d name");
				}
				void resize_int(GLsizei width, GLsizei height, GLuint tex, GLint format, GLenum type) {
					this->glBindTexture(GL_TEXTURE_2D, tex);
					this->glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RED_INTEGER, type, nullptr);
				}
				void finish_resize() {
					check_error(this->glGetError(), "err tex2d");
					// XXX bind and check framebuffer status again???
				}
				//GL_TEXTURE_2D
				template<std::size_t N> void attach(GLuint (&tex)[N], GLenum textarget) {
					this->glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
					this->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, textarget, tex[0], 0);
					//glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, textarget, tex[0], 0);
					std::array<GLenum, N-1> bufs;
					for(unsigned int i=1; i<N; i++)
						this->glFramebufferTexture2D(GL_FRAMEBUFFER, bufs[i-1]=GL_COLOR_ATTACHMENT0+i-1, textarget, tex[i], 0);
					this->glDrawBuffers(bufs.size(), bufs.data());
					auto r=this->glCheckFramebufferStatus(GL_FRAMEBUFFER);
					if(r!=GL_FRAMEBUFFER_COMPLETE)
						throw std::runtime_error{"err check fbo "+std::to_string(r)};
				}
				GLuint _fbo;
		};

	template<typename Funcs>
		class Framebuffer: private Framebuffer_base<Funcs> {
			public:
				constexpr Framebuffer() noexcept: Framebuffer_base<Funcs>{} {
					this->init_tex(_tex);
				}
				~Framebuffer() { this->check_tex(_tex); }
				void create(GLsizei width, GLsizei height) {
					this->gen_objs(_tex);
					this->setup_depth(width, height, _tex[0]);
					this->setup_color(width, height, _tex[1]);
					this->attach(_tex, GL_TEXTURE_2D);
				}
				void resize(GLsizei width, GLsizei height) {
					this->resize_depth(width, height, _tex[0]);
					this->resize_color(width, height, _tex[1]);
					this->finish_resize();
				}
				void destroy() { this->del_objs(_tex); }
				operator GLuint() const noexcept { return this->_fbo; }
				GLuint color() const noexcept { return _tex[1]; }
				GLuint depth() const noexcept { return _tex[0]; }
			private:
				GLuint _tex[2];
		};

#ifndef GL_ES_VERSION_3_0
	template<typename Funcs>
		class MultisampleFramebuffer: private Framebuffer_base<Funcs> {
			public:
				constexpr MultisampleFramebuffer() noexcept: Framebuffer_base<Funcs>{} {
					this->init_tex(_tex);
				}
				~MultisampleFramebuffer() { this->check_tex(_tex); }
				void create(GLsizei width, GLsizei height, GLsizei samples) {
					this->gen_objs(_tex);
					this->setup_depth(width, height, samples, _tex[0]);
					this->setup_color(width, height, samples, _tex[1]);
					this->attach(_tex, GL_TEXTURE_2D_MULTISAMPLE);
				}
				void resize(GLsizei width, GLsizei height, GLsizei samples) {
					this->resize_depth(width, height, samples, _tex[0]);
					this->resize_color(width, height, samples, _tex[1]);
					this->finish_resize();
				}
				void destroy() { this->del_objs(_tex); }
				operator GLuint() const noexcept { return this->_fbo; }
				GLuint color() const noexcept { return _tex[1]; }
				GLuint depth() const noexcept { return _tex[0]; }
			private:
				GLuint _tex[2];
		};
#endif

	template<typename Funcs>
		class PickFramebuffer: private Framebuffer_base<Funcs> {
			public:
				constexpr PickFramebuffer() noexcept: Framebuffer_base<Funcs>{} {
					this->init_tex(_tex);
				}
				~PickFramebuffer() { this->check_tex(_tex); }
				void create(GLsizei width, GLsizei height) {
					this->gen_objs(_tex);
					this->setup_depth(width, height, _tex[0]);
					this->setup_int(width, height, _tex[1], GL_R32UI, GL_UNSIGNED_INT);
					this->setup_int(width, height, _tex[2], GL_R32I, GL_INT);
					this->attach(_tex, GL_TEXTURE_2D);
				}
				void resize(GLsizei width, GLsizei height) {
					this->resize_depth(width, height, _tex[0]);
					this->resize_int(width, height, _tex[1], GL_R32UI, GL_UNSIGNED_INT);
					this->resize_int(width, height, _tex[2], GL_R32I, GL_INT);
					this->finish_resize();
				}
				void destroy() { this->del_objs(_tex); }
				operator GLuint() const noexcept { return this->_fbo; }
				GLuint depth() const noexcept { return _tex[0]; }
			private:
				GLuint _tex[3];
		};

	template<typename Funcs>
		class MixedPickFramebuffer: private Framebuffer_base<Funcs> {
			public:
				constexpr MixedPickFramebuffer() noexcept: Framebuffer_base<Funcs>{} {
					this->init_tex(_tex);
				}
				~MixedPickFramebuffer() { this->check_tex(_tex); }
				void create(GLsizei width, GLsizei height) {
					this->gen_objs(_tex);
					this->setup_depth(width, height, _tex[0]);
					this->setup_color(width, height, _tex[1]);
					this->setup_int(width, height, _tex[2], GL_R32UI, GL_UNSIGNED_INT);
					this->setup_int(width, height, _tex[3], GL_R32I, GL_INT);
					this->attach(_tex, GL_TEXTURE_2D);
				}
				void resize(GLsizei width, GLsizei height) {
					this->resize_depth(width, height, _tex[0]);
					this->resize_color(width, height, _tex[1]);
					this->resize_int(width, height, _tex[2], GL_R32UI, GL_UNSIGNED_INT);
					this->resize_int(width, height, _tex[3], GL_R32I, GL_INT);
					this->finish_resize();
				}
				void destroy() { this->del_objs(_tex); }
				operator GLuint() const noexcept { return this->_fbo; }
				GLuint color() const noexcept { return _tex[1]; }
				GLuint depth() const noexcept { return _tex[0]; }
			private:
				GLuint _tex[4];
		};

	template<typename Base, GLenum type>
	struct Packed {
		Base value;
	};
	template<typename Funcs>
		class VertexArray_base: private Funcs {
			/*! use interleaved format: abcabcabc */
			protected:
				constexpr VertexArray_base() noexcept: Funcs{}, _vao{0} { }
				~VertexArray_base() {
					if(_vao)
						gapr::print("vao not deleted");
				}
				template<std::size_t N> static void init_vbo(GLuint (&vbo)[N]) {
					for(auto& b: vbo)
						b=0;
				}
				template<std::size_t N> static void check_vbo(GLuint (&vbo)[N]) {
					for(auto b: vbo) {
						if(b)
							gapr::print("vao.vbo not deleted");
					}
				}

				template<std::size_t N, typename T, typename... Tm>
					void gen_objs(GLuint (&vbo)[N], Tm T::*... mop) {
						Funcs::initialize();
						static_assert(sizeof...(Tm)>=1);
						this->glGenVertexArrays(1, &_vao);
						this->glBindVertexArray(_vao);
						this->glGenBuffers(N, &vbo[0]);
						this->glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
						check_error(this->glGetError(), "err vao");
						do_attribs(0, mop...);
						check_error(this->glGetError(), "err vao attribs");
					}
				template<std::size_t N> void del_objs(GLuint (&vbo)[N]) {
					this->glDeleteBuffers(N, vbo);
					for(auto& b: vbo)
						b=0;
					if(_vao!=0) {
						this->glDeleteVertexArrays(1, &_vao);
						_vao=0;
					}
					check_error(this->glGetError(), "destroy vao");
				}
				GLuint _vao;
			private:
				template<typename T, typename Tm0, typename... Tm>
					void do_attribs(GLuint index, Tm0 T::* mop0, Tm T::*... mop) {
						this->glEnableVertexAttribArray(index);
						do_attribs2(index, mop0);
						if constexpr(sizeof...(Tm)>0) {
							do_attribs(index+1, mop...);
						}
					}
				template<typename T, std::size_t N>
					void do_attribs2(GLuint index, std::array<GLfloat, N> T::* mop) {
						glVertexAttribPointer(index, N, GL_FLOAT, GL_FALSE, sizeof(T), &(static_cast<T*>(nullptr)->*mop));
					}
				template<typename T, std::size_t N>
					void do_attribs2(GLuint index, std::array<GLint, N> T::* mop) {
						this->glVertexAttribIPointer(index, N, GL_INT, sizeof(T), &(static_cast<T*>(nullptr)->*mop));
					}
				template<typename T>
					void do_attribs2(GLuint index, GLuint T::* mop) {
						this->glVertexAttribIPointer(index, 1, GL_UNSIGNED_INT, sizeof(T), &(static_cast<T*>(nullptr)->*mop));
					}
				template<typename T>
					void do_attribs2(GLuint index, Packed<GLuint, GL_INT_2_10_10_10_REV> T::* mop) {
						this->glVertexAttribPointer(index, 4, GL_INT_2_10_10_10_REV, true, sizeof(T), &(static_cast<T*>(nullptr)->*mop));
					}
		};

	template<typename Funcs>
	class VertexArray: private VertexArray_base<Funcs> {
	public:
		/*! use interleaved format: abcabcabc */
		constexpr VertexArray() noexcept: VertexArray_base<Funcs>{} {
			this->init_vbo(_vbo);
		}
		~VertexArray() { this->check_vbo(_vbo); }

		template<typename T, typename... Tm>
			void create(Tm T::*... mop) {
				this->gen_objs(_vbo, mop...);
			}
		void destroy() { this->del_objs(_vbo); }
		operator GLuint() const noexcept { return this->_vao; }
		GLuint buffer() const noexcept { return _vbo[0]; }
	private:
		GLuint _vbo[1];
	};

	template<typename Funcs>
	class VertexArrayElem: private VertexArray_base<Funcs> {
	public:
		/*! use interleaved format: abcabcabc */
		constexpr VertexArrayElem() noexcept: VertexArray_base<Funcs>{} {
			this->init_vbo(_vbo);
		}
		~VertexArrayElem() { this->check_vbo(_vbo); }

		template<typename T, typename... Tm>
			void create(Tm T::*... mop) {
				this->gen_objs(_vbo, mop...);
			}
		void destroy() { this->del_objs(_vbo); }
		operator GLuint() const noexcept { return this->_vao; }
		GLuint buffer() const noexcept { return _vbo[0]; }
		GLuint element() const noexcept { return _vbo[1]; }
	private:
		GLuint _vbo[2];
	};

	template<typename Funcs>
		class Shader: private Funcs {
			public:
				constexpr Shader() noexcept: Funcs{}, _shader{0} { }
				~Shader() {
					if(_shader)
						gapr::print("shader not deleted");
				}

				template<typename Source>
					void create(Source source, GLenum type, const char* path, std::string_view defs={}) {
						Funcs::initialize();
						_shader=this->glCreateShader(type);
						auto buffer=source.load(path);
						if(!buffer)
							throw std::runtime_error{path};
						std::array<const GLchar*, 4> bufs;
						std::array<GLint, 4> lens;
						bufs[0]=buffer.data();
						if(!bufs[0])
							throw std::runtime_error{path};
						lens[0]=buffer.size();
						GLsizei cnt=1;
						if(defs.size()>0) {
							auto k=std::string_view{bufs[0], static_cast<std::size_t>(lens[0])}.find('\n');
							bufs[cnt]=defs.data();
							lens[cnt]=defs.size();
							cnt++;
							if(k!=std::string::npos) {
								std::string_view fix{"\n#line 2\n"};
								bufs[cnt]=fix.data();
								lens[cnt]=fix.size();
								cnt++;
								bufs[cnt]=bufs[0]+k+1;
								lens[cnt]=lens[0]-k-1;
								lens[0]=k+1;
								cnt++;
							}
						}
						this->glShaderSource(_shader, cnt, bufs.data(), lens.data());
						this->glCompileShader(_shader);
						GLint status;
						this->glGetShaderiv(_shader, GL_COMPILE_STATUS, &status);
						if(status!=GL_TRUE) {
							char log[512];
							GLsizei l;
							this->glGetShaderInfoLog(_shader, 512, &l, log);
							log[511]='\0';
							gapr::print("vshader: %s", log);
							throw std::runtime_error{log};
						}
						if(this->glGetError()!=GL_NO_ERROR)
							throw std::runtime_error{"err compile"};
					}
				//???GLenum types[]={GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
				void destroy() {
					if(_shader) {
						this->glDeleteShader(_shader);
						_shader=0;
					}
				}
				operator GLuint() const noexcept {
					return _shader;
				}

			private:
				GLuint _shader;
		};

	template<typename Funcs>
		class Program: private Funcs {
			public:
				constexpr Program() noexcept: Funcs{}, _prog{0} { }
				~Program() {
					if(_prog)
						gapr::print("prog not deleted");
				}
				void create(std::initializer_list<GLuint> shaders) {
					Funcs::initialize();
					_prog=this->glCreateProgram();
					for(auto shader: shaders)
						this->glAttachShader(_prog, shader);
					this->glLinkProgram(_prog);
					GLint status;
					this->glGetProgramiv(_prog, GL_LINK_STATUS, &status);
					if(this->glGetError()!=GL_NO_ERROR)
						throw std::runtime_error{"err get prog status"};
					if(status!=GL_TRUE) {
						char log[512];
						GLsizei l;
						this->glGetProgramInfoLog(_prog, 512, &l, log);
						log[511]='\0';
						gapr::print("prog: %s", log);
						throw std::runtime_error{log};
						//XXX glDeleteProgram();
					}
					for(auto shader: shaders)
						this->glDetachShader(_prog, shader);
#if 0
					glValidateProgram(_prog);
					if(glGetError()!=GL_NO_ERROR)
						throw std::runtime_error{"err validate"};
#endif
				}
				void destroy() {
					if(_prog) {
						this->glDeleteProgram(_prog);
						_prog=0;
					}
				}
				operator GLuint() const noexcept {
					return _prog;
				}
				GLint uniformLocation(const GLchar* name) {
					auto r=this->glGetUniformLocation(_prog, name);
					check_error(this->glGetError(), "get uniform loc");
					if(r==-1)
						gapr::print("get loc failed: ", _prog, ' ', name, ' ', r);
					return r;
				}
				void uniform(const GLchar* name, GLint v0) {
					glUniform1i(uniformLocation(name), v0);
				}
				void uniform(const GLchar* name, GLfloat v0, GLfloat v1) {
					glUniform2f(uniformLocation(name), v0, v1);
				}
				void uniform(const GLchar* name, GLfloat v0, GLfloat v1, GLfloat v2) {
					glUniform3f(uniformLocation(name), v0, v1, v2);
				}
				void uniform(const GLchar* name, const GLfloat* v) {
					glUniform3fv(uniformLocation(name), 1, v);
				}
				void uniform(const GLchar* name, const vec3<GLfloat>& v) {
					glUniform3fv(uniformLocation(name), 1, &v[0]);
				}
				void uniform(const GLchar* name, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
					glUniform3f(uniformLocation(name), v0, v1, v2, v3);
				}
				void uniform(const GLchar* name, const mat4<GLfloat>& m) {
					glUniformMatrix4fv(uniformLocation(name), 1, false, &m(0, 0));
				}

			private:
				GLuint _prog;
				//GLuint _shaders[2];
		};

	template<typename Funcs>
		class Texture3D: private Funcs {
			public:
				constexpr Texture3D() noexcept: Funcs{}, _tex{0} { }
				~Texture3D() {
					if(_tex)
						gapr::print("tex3d not deleted");
				}
				void create() {
					glGenTextures(1, &_tex);
					//glActiveTexture(GL_TEXTURE1);
					glBindTexture(GL_TEXTURE_3D, _tex);
#ifdef GL_VERSION_3_3
					glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#else
					glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
					glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#endif
					glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
					glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
					check_error(glGetError(), "err tex3d");
				}
				void destroy() {
					if(_tex) {
						glDeleteTextures(1, &_tex);
						_tex=0;
					}
				}
				operator GLuint() const noexcept {
					return _tex;
				}
			private:
				GLuint _tex;
		};

#ifdef GL_ES_VERSION_3_0
	inline void upload_texture3d(const gapr::cube& data) {
		auto view=data.view<const void>();
		auto f=[&view](GLint iformat, GLenum format, GLenum type) {
			glTexImage3D(GL_TEXTURE_3D, 0, iformat, view.width_adj(),
					view.sizes(1), view.sizes(2), 0, format, type, view.row(0, 0));
			gapr::gl::check_error(glGetError(), "err teximage3d");
		};
		switch(view.type()) {
			case gapr::cube_type::u8:
#ifdef GL_VERSION_3_3
				f(GL_R8, GL_RED, GL_UNSIGNED_BYTE);
#else
				f(GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE);
#endif
				break;
			case gapr::cube_type::u16:
#ifdef GL_VERSION_3_3
				f(GL_R16, GL_RED, GL_UNSIGNED_SHORT);
#else
				f(GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT);
#endif
				break;
			case gapr::cube_type::f32:
				f(GL_R32F, GL_RED, GL_FLOAT);
				break;
			default:
				break;
		}
	}
#endif

}

#endif
