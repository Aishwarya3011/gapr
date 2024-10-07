#ifndef _GAPR_GUI_OPENGL_IMPL_HH_
#define _GAPR_GUI_OPENGL_IMPL_HH_

#ifndef _GAPR_GUI_OPENGL_HH_
#error "interface header not included"
#endif

class opengl_category: public std::error_category {
	public:
		constexpr opengl_category() noexcept =default;
		~opengl_category() override =default;

		const char* name() const noexcept override {
			return "opengl";
		}
		std::string message(int i) const override {
			switch(i) {
				case GL_NO_ERROR:
					return "No error";
				case GL_INVALID_ENUM:
					return "GL_INVALID_ENUM";
				case GL_INVALID_VALUE:
					return "GL_INVALID_VALUE";
				case GL_INVALID_OPERATION:
					return "GL_INVALID_OPERATION";
				case GL_OUT_OF_MEMORY:
					return "GL_OUT_OF_MEMORY";
			}
			return "Unknown error";
		}

		std::error_condition default_error_condition(int i) const noexcept override {
			std::errc e;
			switch(i) {
				case GL_INVALID_OPERATION:
				case GL_INVALID_ENUM:
				case GL_INVALID_VALUE:
				default:
					return std::error_condition{i, *this};
				case GL_NO_ERROR:
					return std::error_condition{};
				case GL_OUT_OF_MEMORY:
					e=std::errc::not_enough_memory; break;
			}
			return std::make_error_condition(e);
		}
};
const std::error_category& gapr::gl::error_category() noexcept {
	static const opengl_category cat{};
	return cat;
}

#endif
