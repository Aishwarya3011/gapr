namespace gapr::gl {

#define wrap_gl_func(FUNC) \
	template<typename... Args> \
	static auto FUNC(Args... args) { \
		return ::FUNC(args...); \
	} static_assert(1)

	class GlobalFunctions {
		protected:
			static void initialize() { }
			wrap_gl_func(glActiveTexture);
			wrap_gl_func(glAttachShader);
			wrap_gl_func(glBindBuffer);
			wrap_gl_func(glBindFramebuffer);
			wrap_gl_func(glBindTexture);
			wrap_gl_func(glBindVertexArray);
			wrap_gl_func(glBlendFuncSeparate);
			wrap_gl_func(glBlitFramebuffer);
			wrap_gl_func(glBufferData);
			wrap_gl_func(glBufferSubData);
			wrap_gl_func(glCheckFramebufferStatus);
			wrap_gl_func(glClear);
			wrap_gl_func(glClearColor);
#ifndef GL_ES_VERSION_3_0
			wrap_gl_func(glClearDepth);
#else
			wrap_gl_func(glClearDepthf);
#endif
			wrap_gl_func(glCompileShader);
			wrap_gl_func(glCreateProgram);
			wrap_gl_func(glCreateShader);
			wrap_gl_func(glDeleteBuffers);
			wrap_gl_func(glDeleteFramebuffers);
			wrap_gl_func(glDeleteProgram);
			wrap_gl_func(glDeleteShader);
			wrap_gl_func(glDeleteTextures);
			wrap_gl_func(glDeleteVertexArrays);
			wrap_gl_func(glDetachShader);
			wrap_gl_func(glDisable);
			wrap_gl_func(glDrawArrays);
			wrap_gl_func(glDrawBuffers);
			wrap_gl_func(glDrawElements);
			wrap_gl_func(glEnable);
			wrap_gl_func(glEnableVertexAttribArray);
			wrap_gl_func(glFlush);
			wrap_gl_func(glFramebufferTexture2D);
			wrap_gl_func(glGenBuffers);
			wrap_gl_func(glGenFramebuffers);
			wrap_gl_func(glGenTextures);
			wrap_gl_func(glGenVertexArrays);
			wrap_gl_func(glGetError);
			wrap_gl_func(glGetProgramInfoLog);
			wrap_gl_func(glGetProgramiv);
			wrap_gl_func(glGetShaderInfoLog);
			wrap_gl_func(glGetShaderiv);
			wrap_gl_func(glGetUniformLocation);
			wrap_gl_func(glLinkProgram);
			wrap_gl_func(glMapBufferRange);
#ifndef GL_ES_VERSION_3_0
			wrap_gl_func(glPointSize);
			wrap_gl_func(glPrimitiveRestartIndex);
#endif
			wrap_gl_func(glReadBuffer);
			wrap_gl_func(glReadPixels);
			wrap_gl_func(glShaderSource);
			wrap_gl_func(glStencilFunc);
			wrap_gl_func(glStencilOp);
			wrap_gl_func(glTexImage2D);
			wrap_gl_func(glTexImage3D);
			wrap_gl_func(glTexParameteri);
			wrap_gl_func(glUniform1f);
			wrap_gl_func(glUniform1i);
			wrap_gl_func(glUniform1ui);
			wrap_gl_func(glUniform2fv);
			wrap_gl_func(glUniform3fv);
			wrap_gl_func(glUniform3iv);
			wrap_gl_func(glUniform4i);
			wrap_gl_func(glUniformMatrix4fv);
			wrap_gl_func(glUnmapBuffer);
			wrap_gl_func(glUseProgram);
			wrap_gl_func(glVertexAttribIPointer);
			wrap_gl_func(glVertexAttribPointer);
			wrap_gl_func(glViewport);
	};

#undef wrap_gl_func

}
