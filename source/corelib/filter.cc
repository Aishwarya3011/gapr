/* filter.cc
 *
 * Copyright (C) 2018 GOU Lingfeng
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

#include <cstring>

#include <zlib.h>

struct BufHelper {
	void* _f;
	explicit BufHelper(void* f) noexcept: _f{f} { }
	~BufHelper() noexcept { if(_f) ::free(_f); }
	BufHelper(const BufHelper&) =delete;
	BufHelper& operator=(const BufHelper&) =delete;
	void* get() noexcept { return _f; }
	void* release() noexcept { auto f=_f; _f=nullptr; return f; }
};

enum class FilterRes {
	Ok,
	BufErr,
	End,
	Err
};

namespace gapr {
template<typename T> class FilterInput {
	public:
		template<typename... Args>
			FilterInput(gapr::Streambuf& next, Args&&... args):
				_next{next}, _filter{std::forward<Args>(args)...}, _eof{false}
		{
			BufHelper buf{std::malloc(gapr::Streambuf::bufsiz)};
			if(!buf.get())
				throw std::bad_alloc{};
			_buf=buf.release();
		}
		~FilterInput() {
			BufHelper buf{_buf};
		}

		void close(std::error_code& ec) {
			if(_eof) {
				bool error=false;
				std::size_t bytes_skipped=0;
				do {
					_filter.setobuf(reinterpret_cast<unsigned char*>(_buf), BUFSIZ);
					bool flush=false;
					auto in_delta=_filter.avail_in();
					if(in_delta==0) {
						auto c=_next.underflow();
						if(c==EOF) {
							flush=true;
						} else {
							in_delta=_next.egptr()-_next.gptr();
							_filter.setibuf(reinterpret_cast<unsigned char*>(_next.gptr()), in_delta);
						}
					}
					auto res=_filter.run(flush, ec);
					in_delta-=_filter.avail_in();
					_next.gbump(in_delta);
					switch(res) {
						case FilterRes::Ok:
							break;
						case FilterRes::BufErr:
							error=true;
							break;
						case FilterRes::End:
							_eof=true;
							break;
						case FilterRes::Err:
							return;
					}
					bytes_skipped+=BUFSIZ-_filter.avail_out();
				} while(!_eof && !error);
				if(error) {
					ec=std::make_error_code(std::io_errc::stream);
					return;
				}

#if 0
				if(bytes_skipped>0)
					print("Skipped ", bytes_skipped, " bytes.");
#endif
			}

			_filter.close(ec);
		}
		bool eof() { return _eof; }

		std::size_t fread(void* ptr, std::size_t size, std::error_code& ec) {
			if(_eof)
				return 0;
			if(size<=0)
				return 0;
			_filter.setobuf(ptr, size);
			bool stop=false;
			do {
				bool flush=false;
				auto in_delta=_filter.avail_in();
				if(in_delta==0) {
					auto c=_next.underflow();
					if(c==EOF) {
						flush=true;
					} else {
						in_delta=_next.egptr()-_next.gptr();
						_filter.setibuf(reinterpret_cast<unsigned char*>(_next.gptr()), in_delta);
					}
				}
				auto res=_filter.run(flush, ec);
				in_delta-=_filter.avail_in();
				_next.gbump(in_delta);
				switch(res) {
					case FilterRes::Ok:
						break;
					case FilterRes::BufErr:
						stop=true;
						break;
					case FilterRes::End:
						_eof=true;
						break;
					case FilterRes::Err:
						return size-_filter.avail_out();
				}
			} while(_filter.avail_out()>0 && !_eof && !stop);
			auto out_delta=size-_filter.avail_out();
			return out_delta;
		}
		std::size_t fmap(const void** pptr, std::error_code& ec) {
			auto r=fread(_buf, gapr::Streambuf::bufsiz, ec);
			*pptr=_buf;
			return r;
		}
		void fseek(off_t offset, int whence, std::error_code& ec) {
			ec=std::error_code{ESPIPE, std::generic_category()};
		}
		off_t ftell(std::error_code& ec) {
			return _filter.total_out();
		}
		std::pair<std::size_t, bool> remain() {
			if(_eof)
				return {0, true};
			return {0, false};
		}
	private:
		gapr::Streambuf& _next;
		T _filter;
		void* _buf;
		bool _eof;
};

}

//////////////////////////////


#if 0
template<typename T>
class FilterOutput: public Buffer {
	Buffer*const parent;
	T filter;
	char buffer[BUFSIZ];
	bool is_open;
	FilterOutput(Buffer*const p): Buffer{}, parent{p}, filter{}, buffer{}, is_open{false} {
		setp(buffer, buffer+BUFSIZ);
	}

	bool flush_buffer() {
		bool ret=true;
		if(pbase()<pptr()) {
			ret=false;
			auto towrite=pptr()-pbase();
			auto r=write_to_downstream(pbase(), towrite);
			if(r>0) {
				if(r<towrite) {
					setp(pbase()+r, epptr());
					pbump(towrite-r);
				} else {
					ret=true;
					setp(buffer, buffer+BUFSIZ);
				}
			}
		}
		return ret;
	}
	bool flush_filter();
	bool finish_filter();
	std::streamsize write_to_downstream(const char* s, std::streamsize n);
	std::streamoff position();

	protected:
	std::streampos seekoff(std::streamoff off, std::ios_base::seekdir way, std::ios_base::openmode which) override {
		std::streamoff ret=-1;
		if(!is_open)
			return ret;
		if(!(std::ios_base::out&which))
			return ret;
		if(way==std::ios_base::cur) {
			if(off<=0 && pptr()+off>=pbase()) {
				pbump(off);
				ret=position()+pptr()-pbase();
			}
		}
		return ret;
	}
	std::streampos seekpos(std::streampos sp, std::ios_base::openmode which) override {
		return -1;
	}
	int sync() override {
		int ret=-1;
		if(!is_open)
			return ret;
		if(flush_filter()) {
			ret=parent->sync();
		}
		return ret;
	}
	std::streamsize showmanyc() override { return -1; }
	std::streamsize xsgetn(char* s, std::streamsize n) override { return 0; }
	int underflow() override { return EOF; }
	int pbackfail(int c) override { return EOF; }
	std::streamsize xsputn(const char* s, std::streamsize n) override {
		if(!is_open)
			return 0;
		std::streamsize total=n;
		auto slot=epptr()-pptr();
		if(n<BUFSIZ+slot) {
			n-=Buffer::xsputn(s, n);
		} else {
			traits_type::copy(pptr(), s, slot);
			pbump(slot);
			s+=slot;
			n-=slot;
			if(flush_buffer()) {
				auto towrite=n/BUFSIZ*BUFSIZ;
				auto r=write_to_downstream(s, towrite);
				s+=r;
				n-=r;
				if(auto rem=r%BUFSIZ)
					setp(buffer+rem, buffer+BUFSIZ);
				auto tocopy=epptr()-pptr();
				if(tocopy>n)
					tocopy=n;
				traits_type::copy(pptr(), s, tocopy);
				s+=tocopy;
				n-=tocopy;
				pbump(tocopy);
			}
		}
		return total-n;
	}
	int overflow(int c) override {
		int ret=EOF;
		if(!is_open)
			return ret;
		if(flush_buffer()) {
			if(c==EOF) {
				ret=0;
			} else {
				*pptr()=traits_type::to_char_type(c);
				pbump(1);
				ret=c;
			}
		}
		return ret;
	}

	public:
	FilterOutput(Buffer*const p, const typename T::Params& par);
	~FilterOutput();
	bool close() override;
};

/* Gzip */

inline std::streamsize FilterInput<GzipInfl>::read_from_upstream(char* s, std::streamsize n) {
}

struct GzipCompress {
	z_stream strm;
	struct Params {
		int  level;
		int  method;
		int  windowBits;
		int  memLevel;
		int  strategy;
	};
};

template<>
inline bool FilterOutput<GzipCompress>::finish_filter() {
	bool ret=false;
	auto toflush=pptr()-pbase();
	filter.strm.next_in=reinterpret_cast<unsigned char*>(pbase());
	filter.strm.avail_in=toflush;
	bool flushed=false;
	do {
		int flush=Z_FINISH;
		auto out_delta=filter.strm.avail_out;
		if(out_delta==0) {
			auto c=parent->overflow(EOF);
			if(c==EOF) {
				// XXX
			} else {
				filter.strm.next_out=reinterpret_cast<unsigned char*>(parent->pptr());
				out_delta=filter.strm.avail_out=parent->epptr()-parent->pptr();
			}
		}
		auto r=deflate(&filter.strm, flush);
		out_delta-=filter.strm.avail_out;
		parent->pbump(out_delta);
		switch(r) {
			case Z_OK:
				// continue
				break;
			case Z_STREAM_END:
				flushed=true;
				break;
			case Z_BUF_ERROR:
				break;
			default:
				report("failed to deflate");
		}
	} while(!flushed);
	auto delta_in=toflush-filter.strm.avail_in;
	if(delta_in<toflush) {
		if(delta_in>0) {
			setp(pbase()+delta_in, epptr());
			pbump(toflush-delta_in);
		}
	} else {
		ret=true;
		setp(buffer, buffer+BUFSIZ);
	}
	return ret;
}
template<>
inline bool FilterOutput<GzipCompress>::close() {
	if(!is_open)
		return true;
	if(!finish_filter())
		return false;
	auto r=deflateEnd(&filter.strm);
	if(r!=Z_OK) {
		report("Failed to destroy gzip output filter: ", filter.strm.msg);
	}
	is_open=false;
	return true;
}
template<>
FilterOutput<GzipCompress>::~FilterOutput() {
	if(is_open) {
		finish_filter();
		auto r=deflateEnd(&filter.strm);
		if(r!=Z_OK) {
			report("Failed to destroy gzip output filter: ", filter.strm.msg);
		}
	}
}
template<>
FilterOutput<GzipCompress>::FilterOutput(Buffer*const p, const GzipCompress::Params& pars): FilterOutput{p} {
	filter.strm.next_in=reinterpret_cast<unsigned char*>(pbase());
	filter.strm.avail_in=0;
	filter.strm.next_out=reinterpret_cast<unsigned char*>(parent->pptr());
	filter.strm.avail_out=parent->epptr()-parent->pptr();
	filter.strm.zalloc=nullptr;
	filter.strm.zfree=nullptr;
	filter.strm.opaque=nullptr;
	auto r=deflateInit2(&filter.strm, pars.level, pars.method, pars.windowBits, pars.memLevel, pars.strategy);
	if(r!=Z_OK)
		report("Failed to init gzip output filter: ", filter.strm.msg);
	is_open=true;
}

template<>
std::streamoff FilterOutput<GzipCompress>::position() {
	std::streamoff pos=filter.strm.total_in;
	return pos;
}

template<>
inline std::streamsize FilterOutput<GzipCompress>::write_to_downstream(const char* s, std::streamsize n) {
	if(n<=0)
		return 0;
	filter.strm.next_in=reinterpret_cast<unsigned char*>(const_cast<char*>(s));
	filter.strm.avail_in=n;
	bool stop=false;
	do {
		int flush=Z_NO_FLUSH; // Z_SYNC_FLUSH, Z_FINISH
		auto out_delta=filter.strm.avail_out;
		if(out_delta==0) {
			auto c=parent->overflow(EOF);
			if(c==EOF) {
				// XXX
			} else {
				filter.strm.next_out=reinterpret_cast<unsigned char*>(parent->pptr());
				out_delta=filter.strm.avail_out=parent->epptr()-parent->pptr();
			}
		}
		auto r=deflate(&filter.strm, flush);
		out_delta-=filter.strm.avail_out;
		parent->pbump(out_delta);
		switch(r) {
			case Z_OK:
				// continue
				break;
			case Z_BUF_ERROR:
				stop=true;
				break;
			case Z_STREAM_END:
			default:
				report("failed to deflate");
		}
	} while(filter.strm.avail_in>0 && !stop);
	auto delta_in=n-filter.strm.avail_in;
	return delta_in;
}
template<>
inline bool FilterOutput<GzipCompress>::flush_filter() {
	bool ret=false;
	auto toflush=pptr()-pbase();
	filter.strm.next_in=reinterpret_cast<unsigned char*>(pbase());
	filter.strm.avail_in=toflush;
	bool flushed=false;
	do {
		int flush=Z_SYNC_FLUSH; //, Z_FINISH
		auto out_delta=filter.strm.avail_out;
		if(out_delta==0) {
			auto c=parent->overflow(EOF);
			if(c==EOF) {
				// XXX
			} else {
				filter.strm.next_out=reinterpret_cast<unsigned char*>(parent->pptr());
				out_delta=filter.strm.avail_out=parent->epptr()-parent->pptr();
			}
		}
		auto r=deflate(&filter.strm, flush);
		out_delta-=filter.strm.avail_out;
		parent->pbump(out_delta);
		switch(r) {
			case Z_OK:
				// continue
				break;
			case Z_BUF_ERROR:
				if(filter.strm.avail_out>0 && filter.strm.avail_in==0)
					flushed=true;
				break;
			case Z_STREAM_END:
			default:
				report("failed to deflate");
		}
	} while(!flushed);
	auto delta_in=toflush-filter.strm.avail_in;
	if(delta_in<toflush) {
		if(delta_in>0) {
			setp(pbase()+delta_in, epptr());
			pbump(toflush-delta_in);
		}
	} else {
		ret=true;
		setp(buffer, buffer+BUFSIZ);
	}
	return ret;
}

/* Bzip2
 * gzip is better

struct Bzip2Decompress {
	bz_stream strm;
	struct Params {
		int small;
	};
};
template<>
inline bool FilterInput<Bzip2Decompress>::close() {
	if(!is_open)
		return true;
	if(eof && gptr()==egptr()) {
		auto r=BZ2_bzDecompressEnd(&filter.strm);
		if(r!=BZ_OK) {
			report("Failed to destroy bzip2 input filter: ", r);
		}
		is_open=false;
		return true;
	}
	return false;
}
template<>
inline FilterInput<Bzip2Decompress>::~FilterInput() {
	if(is_open) {
		auto r=BZ2_bzDecompressEnd(&filter.strm);
		if(r!=BZ_OK) {
			report("Failed to destroy bzip2 input filter: ", r);
		}
	}
}
template<>
inline FilterInput<Bzip2Decompress>::FilterInput(Buffer*const p, const Bzip2Decompress::Params& args): FilterInput{p} {
	filter.strm.next_in=parent->gptr();
	filter.strm.avail_in=parent->egptr()-parent->gptr();
	filter.strm.next_out=buffer;
	filter.strm.avail_out=0;
	filter.strm.bzalloc=nullptr;
	filter.strm.bzfree=nullptr;
	filter.strm.opaque=0;
	auto r=BZ2_bzDecompressInit(&filter.strm, 0, args.small);
	if(r!=BZ_OK) {
		report("Failed to init bzip2 input filter: ", r);
	}
	is_open=true;
}
template<>
inline std::streamsize FilterInput<Bzip2Decompress>::read_from_upstream(char* s, std::streamsize n) {
	if(eof)
		return 0;
	if(n<=0)
		return 0;
	filter.strm.next_out=s;
	filter.strm.avail_out=n;
	do {
		auto in_delta=filter.strm.avail_in;
		if(in_delta==0) {
			auto c=parent->underflow();
			if(c==EOF) {
			} else {
				filter.strm.next_in=parent->gptr();
				in_delta=filter.strm.avail_in=parent->egptr()-parent->gptr();
			}
		}
		auto r=BZ2_bzDecompress(&filter.strm);
		in_delta-=filter.strm.avail_in;
		parent->gbump(in_delta);
		switch(r) {
			case BZ_OK:
				// continue
				break;
			//case Z_BUF_ERROR:
				// ???
				//break;
			case BZ_STREAM_END:
				eof=true;
				break;
			default:
				report("failed to inflate");
		}
	} while(filter.strm.avail_out>0 && !eof);
	auto out_delta=n-filter.strm.avail_out;
	return out_delta;
}
template<>
std::streamoff FilterInput<Bzip2Decompress>::position() {
	std::streamoff pos=filter.strm.total_out_hi32;
	pos=pos<<32+filter.strm.total_out_lo32;
	return pos;
}


struct Bzip2Compress {
	bz_stream strm;
	struct Params {
		int blockSize100k;
		int workFactor;
	};
};

template<>
inline bool FilterOutput<Bzip2Compress>::finish_filter() {
	bool ret=false;
	auto toflush=pptr()-pbase();
	filter.strm.next_in=pbase();
	filter.strm.avail_in=toflush;
	bool flushed=false;
	do {
		int flush=BZ_FINISH;
		auto out_delta=filter.strm.avail_out;
		if(out_delta==0) {
			auto c=parent->overflow(EOF);
			if(c==EOF) {
				// XXX
			} else {
				filter.strm.next_out=parent->pptr();
				out_delta=filter.strm.avail_out=parent->epptr()-parent->pptr();
			}
		}
		auto r=BZ2_bzCompress(&filter.strm, flush);
		out_delta-=filter.strm.avail_out;
		parent->pbump(out_delta);
		switch(r) {
			case BZ_FINISH_OK:
				// continue
				break;
			case BZ_STREAM_END:
				flushed=true;
				break;
			default:
				report("failed to deflate");
		}
	} while(!flushed);
	auto delta_in=toflush-filter.strm.avail_in;
	if(delta_in<toflush) {
		if(delta_in>0) {
			setp(pbase()+delta_in, epptr());
			pbump(toflush-delta_in);
		}
	} else {
		ret=true;
		setp(buffer, buffer+BUFSIZ);
	}
	return ret;
}
template<>
inline bool FilterOutput<Bzip2Compress>::close() {
	if(!is_open)
		return true;
	if(!finish_filter())
		return false;
	auto r=BZ2_bzCompressEnd(&filter.strm);
	if(r!=BZ_OK) {
		report("Failed to destroy bzip2 output filter: ", r);
	}
	is_open=false;
	return true;
}
template<>
FilterOutput<Bzip2Compress>::~FilterOutput() {
	if(is_open) {
		finish_filter();
		auto r=BZ2_bzCompressEnd(&filter.strm);
		if(r!=BZ_OK) {
			report("Failed to destroy bzip2 output filter: ", r);
		}
	}
}
template<>
FilterOutput<Bzip2Compress>::FilterOutput(Buffer*const p, const Bzip2Compress::Params& pars): FilterOutput{p} {
	filter.strm.next_in=pbase();
	filter.strm.avail_in=0;
	filter.strm.next_out=parent->pptr();
	filter.strm.avail_out=parent->epptr()-parent->pptr();
	filter.strm.bzalloc=nullptr;
	filter.strm.bzfree=nullptr;
	filter.strm.opaque=nullptr;
	auto r=BZ2_bzCompressInit(&filter.strm, pars.blockSize100k, 0, pars.workFactor);
	if(r!=BZ_OK)
		report("Failed to init gzip output filter: ", r);
	is_open=true;
}

template<>
std::streamoff FilterOutput<Bzip2Compress>::position() {
	std::streamoff pos=filter.strm.total_in_hi32;
	pos=pos<<32+filter.strm.total_in_lo32;
	return pos;
}

template<>
inline std::streamsize FilterOutput<Bzip2Compress>::write_to_downstream(const char* s, std::streamsize n) {
	if(n<=0)
		return 0;
	filter.strm.next_in=const_cast<char*>(s);
	filter.strm.avail_in=n;
	do {
		int flush=BZ_RUN;
		auto out_delta=filter.strm.avail_out;
		if(out_delta==0) {
			auto c=parent->overflow(EOF);
			if(c==EOF) {
				// XXX
			} else {
				filter.strm.next_out=parent->pptr();
				out_delta=filter.strm.avail_out=parent->epptr()-parent->pptr();
			}
		}
		auto r=BZ2_bzCompress(&filter.strm, flush);
		out_delta-=filter.strm.avail_out;
		parent->pbump(out_delta);
		switch(r) {
			case BZ_RUN_OK:
				// continue
				break;
			default:
				report("failed to deflate");
		}
	} while(filter.strm.avail_in>0);
	auto delta_in=n-filter.strm.avail_in;
	return delta_in;
}

template<>
inline bool FilterOutput<Bzip2Compress>::flush_filter() {
	bool ret=false;
	auto toflush=pptr()-pbase();
	filter.strm.next_in=pbase();
	filter.strm.avail_in=toflush;
	bool flushed=false;
	do {
		int flush=BZ_FLUSH;
		auto out_delta=filter.strm.avail_out;
		if(out_delta==0) {
			auto c=parent->overflow(EOF);
			if(c==EOF) {
				// XXX
			} else {
				filter.strm.next_out=parent->pptr();
				out_delta=filter.strm.avail_out=parent->epptr()-parent->pptr();
			}
		}
		auto r=BZ2_bzCompress(&filter.strm, flush);
		out_delta-=filter.strm.avail_out;
		parent->pbump(out_delta);
		switch(r) {
			case BZ_FLUSH_OK:
				// continue
				break;
			case BZ_RUN_OK:
				flushed=true;
				break;
			default:
				report("failed to deflate");
		}
	} while(!flushed);
	auto delta_in=toflush-filter.strm.avail_in;
	if(delta_in<toflush) {
		if(delta_in>0) {
			setp(pbase()+delta_in, epptr());
			pbump(toflush-delta_in);
		}
	} else {
		ret=true;
		setp(buffer, buffer+BUFSIZ);
	}
	return ret;
}
*/

Buffer* Buffer::outputFilter(Buffer* buf, const char* method) {
	if(!buf)
		return nullptr;

	try {
		std::string type;
		auto n=parse_tuple(method, &type);
		if(n!=1)
			return nullptr;
		const char* parstr=nullptr;
		if(*method==':') {
			parstr=method+1;
		} else if(*method!='\0') {
			return nullptr;
		}

		if(type=="gzip") {
			GzipCompress::Params pars;
			pars.level=5;
			pars.method=Z_DEFLATED;
			pars.windowBits=15+16;
			pars.memLevel=9;
			pars.strategy=Z_FILTERED;
			if(parstr)
				parse_tuple(parstr, &pars.level, &pars.strategy);

			return new FilterOutput<GzipCompress>{buf, pars};
		}
	} catch(...) {
		return nullptr;
	}
	return nullptr;
}
}
#endif

struct GzipInfl {
	z_stream zstrm;

	GzipInfl(int windowBits) {
		zstrm.next_in=nullptr;
		zstrm.avail_in=0;
		zstrm.next_out=nullptr;
		zstrm.avail_out=0;
		zstrm.zalloc=nullptr;
		zstrm.zfree=nullptr;
		zstrm.opaque=0;
		auto r=inflateInit2(&zstrm, windowBits);
		if(r!=Z_OK)
			throw std::system_error{std::make_error_code(std::io_errc::stream), zstrm.msg};
	}
	~GzipInfl() {
		auto r=inflateEnd(&zstrm);
		if(r!=Z_OK) {
			//print("Failed to destroy gzip input filter: ", zstrm.msg);
		}
	}

	void setobuf(void* ptr, std::size_t n) {
		zstrm.next_out=static_cast<Bytef*>(ptr);
		zstrm.avail_out=n;
	}
	void setibuf(void* ptr, std::size_t n) {
		zstrm.next_in=static_cast<Bytef*>(ptr);
		zstrm.avail_in=n;
	}
	std::size_t avail_in() {
		return zstrm.avail_in;
	}
	std::size_t avail_out() {
		return zstrm.avail_out;
	}
				FilterRes run(bool flush, std::error_code& ec) {
					auto r=inflate(&zstrm, flush?Z_FINISH:Z_NO_FLUSH);
					switch(r) {
						case Z_OK:
							return FilterRes::Ok;
						case Z_BUF_ERROR:
							return FilterRes::BufErr;
						case Z_STREAM_END:
							return FilterRes::End;
						default:
							ec=std::make_error_code(std::io_errc::stream);
							return FilterRes::Err;
					}
				}
			void close(std::error_code& ec) {
				auto r=inflateEnd(&zstrm);
				if(r!=Z_OK) {
					ec=std::make_error_code(std::io_errc::stream);
					return;
					//report("Failed to destroy gzip input filter: ", _filter.strm.msg);
				}
			}
			std::size_t total_out() {
				return zstrm.total_out;
			}
};

gapr::Streambuf* gapr::Streambuf::inputFilter(Streambuf& buf, const char* method) {
	if(std::strcmp(method, "gzip")==0) {
		return new Streambuf_Input<FilterInput<GzipInfl>>{buf, 15+16};
	}
	return nullptr;
}

