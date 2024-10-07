#include <cstdio>
#include <cassert>
#include <filesystem>

#include "windows-compat.hh"

namespace gapr {

class file_stream {
public:
	constexpr file_stream() noexcept: _f{nullptr} { }
	file_stream(const std::filesystem::path& path, const char* mode): _f{::fopen(path.c_str(), mode)} { }
	//FILE *fdopen(int fd, const char *mode);
	~file_stream() {
		if(_f) ::fclose(_f);
	}
	file_stream(const file_stream&) =delete;
	file_stream& operator=(const file_stream&) =delete;
	file_stream(file_stream&& r) noexcept: _f{r._f} {
		r._f=nullptr;
	}
	file_stream& operator=(file_stream&& r) noexcept {
		std::swap(_f, r._f);
		return *this;
	}

	explicit operator bool() const noexcept { return _f; }
	/*explicit*/ operator FILE*() const noexcept { return _f; }
	FILE* lower() const noexcept { return _f; }
	bool close() {
		assert(_f);
		auto r=::fclose(_f);
		_f=nullptr;
		return r==0;
	}
	FILE* release() noexcept {
		auto f=_f;
		_f=nullptr;
		return f;
	}

private:
	FILE* _f;
	//explicit file_stream(FILE* f) noexcept: _f{f} { }
	//FILE* get() const noexcept { return _f; }
};

}
