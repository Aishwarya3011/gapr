/* streambuf.cc
 *
 * Copyright (C) 2019 GOU Lingfeng
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


#include "gapr/detail/streambuf.hh"
//#include "gapr/utility.hh"


#include "gapr/mem-file.hh"

#include <streambuf>
#include <cassert>
#include <cstring>
#include <filesystem>

#include "libc-wrappers.hh"

using gapr::Streambuf;

struct BufHelper {
	void* _f;
	explicit BufHelper(void* f) noexcept: _f{f} { }
	~BufHelper() noexcept { if(_f) ::free(_f); }
	BufHelper(const BufHelper&) =delete;
	BufHelper& operator=(const BufHelper&) =delete;
	void* get() noexcept { return _f; }
	void* release() noexcept { auto f=_f; _f=nullptr; return f; }
};

constexpr static std::size_t bufsiz=gapr::Streambuf::bufsiz;

class StdFile {
	public:
		StdFile(const std::filesystem::path& fn, const char* mode): _eof{false} {
			BufHelper buf{std::malloc(gapr::Streambuf::bufsiz)};
			if(!buf.get())
				throw std::bad_alloc{};
			gapr::file_stream file{fn, mode};
			if(!file)
				throw std::system_error{errno, std::generic_category()};
			if(0!=::setvbuf(file, nullptr, _IONBF, 0))
				throw std::system_error{errno, std::generic_category()};
			_file=std::move(file);
			_buf=buf.release();
		}
		~StdFile() {
			BufHelper buf{_buf};
		}
		void close(std::error_code& ec) {
			assert(_file);
			if(!_file.close())
				ec=std::error_code{errno, std::generic_category()};
		}
		bool eof() { return _eof; }
		std::size_t fread(void* ptr, std::size_t size, std::error_code& ec) {
			if(_eof)
				return 0;
			if(size<=0)
				return 0;
			auto r=::fread(ptr, 1, size, _file);
			if(r<size) {
				if(::ferror(_file)) {
					ec=std::error_code{errno, std::generic_category()};
					return r;
				}
				assert(feof(_file));
				_eof=true;
			}
			return r;
		}
		std::size_t fmap(const void** pptr, std::error_code& ec) {
			auto r=fread(_buf, gapr::Streambuf::bufsiz, ec);
			*pptr=_buf;
			return r;
		}
		std::size_t prepare(void** pptr, std::error_code& ec) {
			*pptr=_buf;
			return bufsiz;
		}
		void fwrite(const void* ptr, std::size_t size, std::error_code& ec) {
			if(size<=0)
				return;
			auto r=::fwrite(ptr, 1, size, _file);
			if(r<size) {
				assert(::ferror(_file));
				ec=std::error_code{errno, std::generic_category()};
			}
		}
		void commit(std::size_t size, std::error_code& ec) {
			return fwrite(_buf, size, ec);
		}
		void fflush(std::error_code& ec) {
			auto r=::fflush(_file);
			if(r!=0)
				ec=std::error_code{errno, std::generic_category()};
		}
		void fseek(off_t offset, int whence, std::error_code& ec) {
#ifdef _MSC_VER
			auto& fseeko=::_fseeki64;
#endif
			auto r=fseeko(_file, offset, whence);
			if(_eof)
				_eof=false;
			if(r==-1)
				ec=std::error_code{errno, std::generic_category()};
		}
		off_t ftell(std::error_code& ec) {
#ifdef _MSC_VER
			auto& ftello=::_ftelli64;
#endif
			auto r=ftello(_file);
			if(r==-1)
				ec=std::error_code{errno, std::generic_category()};
			return r;
		}
		std::pair<std::size_t, bool> remain() {
			if(_eof)
				return {0, true};
			return {0, false};
		}
	private:
		gapr::file_stream _file;
		void* _buf;
		bool _eof;
};


class MemFileIn {
	public:
		MemFileIn(gapr::mem_file&& file): _file{std::move(file)}, _off{0}, _eof{false} { }
		~MemFileIn() { }
		void close(std::error_code& ec) {
			assert(_file);
			_file=gapr::mem_file{};
		}
		bool eof() { return _eof; }
		std::size_t fread(void* ptr, std::size_t size, std::error_code& ec) {
			if(size<=0)
				return 0;
			std::size_t ret=0;
			do {
				const void* buf;
				auto n=fmap(&buf, ec);
				if(ec)
					return ret;
				if(n<=0)
					break;
				if(n<=size) {
					std::memcpy(ptr, buf, n);
					ptr=static_cast<char*>(ptr)+n;
					size-=n;
					ret+=n;
				} else {
					std::memcpy(ptr, buf, size);
					ret+=size;
					auto x=n-size;
					_off-=x;
					break;
				}
			} while(size>0);
			return ret;
		}
		std::size_t fmap(const void** pptr, std::error_code& ec) {
			if(_eof)
				return 0;
			auto r=_file.map(_off);
			if(r.size()>0) {
				_off+=r.size();
				*pptr=r.data();
			} else {
				_eof=true;
			}
			return r.size();
		}
		std::size_t prepare(void** pptr, std::error_code& ec) {
			ec=std::error_code{EBADF, std::generic_category()};
			return 0;
		}
		void fwrite(const void* ptr, std::size_t size, std::error_code& ec) {
			ec=std::error_code{EBADF, std::generic_category()};
		}
		void commit(std::size_t size, std::error_code& ec) {
			ec=std::error_code{EBADF, std::generic_category()};
		}
		void fflush(std::error_code& ec) {
			ec=std::error_code{EBADF, std::generic_category()};
		}
		void fseek(off_t offset, int whence, std::error_code& ec) {
			auto fsize=_file.size();
			switch(whence) {
				case SEEK_SET:
					_off=offset;
					break;
				case SEEK_CUR:
					_off=_off+offset;
					break;
				case SEEK_END:
					_off=fsize+offset;
					break;
				default:
					assert(0);
			}
			if(_off<fsize) {
				_eof=false;
			} else if(_off==fsize) {
				_eof=true;
			} else {
				ec=std::error_code{EINVAL, std::generic_category()};
			}
		}
		off_t ftell(std::error_code& ec) {
			return _off;
		}
		std::pair<std::size_t, bool> remain() {
			auto n=_file.size();
			return {n-_off, true};
		}
	private:
		gapr::mem_file _file;
		std::size_t _off;
		bool _eof;
};

template class gapr::Streambuf_Input<StdFile>;
template class gapr::Streambuf_Output<StdFile>;
template class gapr::Streambuf_Input<MemFileIn>;

std::unique_ptr<Streambuf> gapr::make_streambuf(gapr::mem_file&& file) {
	return std::make_unique<gapr::Streambuf_Input<MemFileIn>>(std::move(file));
}
std::unique_ptr<Streambuf> gapr::make_streambuf(const char* path) {
	return std::make_unique<gapr::Streambuf_Input<StdFile>>(path, "rb");
}
