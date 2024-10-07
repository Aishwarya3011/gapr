#include <memory>

#include <zlib.h>

namespace gapr {

	template<typename Next> class zlib_output {
		public:
			//zlib_output() noexcept: _f{nullptr} { }
			explicit zlib_output(Next& f): _f{&f}, _buf{new char[BUFSIZ+128]} {
				_o=0;
				strm.next_in=reinterpret_cast<unsigned char*>(&_buf[0]);
				strm.avail_in=0;
				auto [ptr, len]=_f->buffer();
				strm.next_out=reinterpret_cast<unsigned char*>(ptr);
				strm.avail_out=len;
				strm.zalloc=nullptr;
				strm.zfree=nullptr;
				strm.opaque=nullptr;
				auto windowBits=15+16;
				auto r=deflateInit2(&strm, 5, Z_DEFLATED, windowBits, 9, Z_FILTERED);
				if(r!=Z_OK)
					throw std::runtime_error{"error Init2"};
			}
			~zlib_output() {
			}
			zlib_output(const zlib_output&) =delete;
			zlib_output& operator=(const zlib_output&) =delete;

			std::pair<char*, std::size_t> buffer() {
				assert(_i<BUFSIZ);
				return {&_buf[_i], BUFSIZ+128-_i};
			}
			void commit(std::size_t n) {
				assert(_i+n<=BUFSIZ+128);
				_i+=n;
				if(_i>=BUFSIZ) {
					if(do_sync(_buf.get(), BUFSIZ, false)!=BUFSIZ)
						throw std::runtime_error{"error sync"};
					_o+=BUFSIZ;
					_i-=BUFSIZ;
					memcpy(_buf.get(), _buf.get()+BUFSIZ, _i);
				}
			}
			std::size_t offset() {
				return _o+_i;
			}
			void flush() {
				if(_i>0) {
					if(do_sync(_buf.get(), _i, false)!=_i)
						throw std::runtime_error{"error sync2"};
					_o+=_i;
					_i=0;
				}
				if(!finish_filter())
					throw std::runtime_error{"err finish"};
				auto r=deflateEnd(&strm);
				if(r!=Z_OK) {
					throw std::runtime_error{"error End"};
				}
			}

		private:
			Next* _f;
			std::unique_ptr<char[]> _buf;
			z_stream strm;
			std::size_t _i=0;
			std::size_t _o;
			std::size_t do_sync(const char* s, std::size_t n, bool flush) {
#if 0
				set_in_buf(ptr, cnt);
				do {
					auto [buf, siz]=_f.reserve();
					set_out_buf(buf, siz);
					defalate();
					if(err) {
						throw;
					}
					out.commit(...);
					if(in_end) {
						return ...;
					}
				} while(true);
				return cnt;
#endif
				if(n<=0)
					return 0;
				strm.next_in=reinterpret_cast<unsigned char*>(const_cast<char*>(s));
				strm.avail_in=n;
				bool stop=false;
				do {
					int flush=Z_NO_FLUSH; // Z_SYNC_FLUSH, Z_FINISH
					auto out_delta=strm.avail_out;
					if(true || out_delta==0) {
						auto [ptr, len]=_f->buffer();
						strm.next_out=reinterpret_cast<unsigned char*>(ptr);
						out_delta=strm.avail_out=len;
					}
					auto r=deflate(&strm, flush);
					out_delta-=strm.avail_out;
					_f->commit(out_delta);
					switch(r) {
						case Z_OK:
							// continue
							break;
						case Z_BUF_ERROR:
							stop=true;
							break;
						case Z_STREAM_END:
						default:
							throw std::runtime_error{"error deflate"};
					}
				} while(strm.avail_in>0 && !stop);
				auto delta_in=n-strm.avail_in;
				return delta_in;
			}
			bool finish_filter() {
				bool ret=false;
				auto toflush=_i;
				strm.next_in=reinterpret_cast<unsigned char*>(&_buf[0]);
				strm.avail_in=toflush;
				bool flushed=false;
				do {
					int flush=Z_FINISH;
					auto out_delta=strm.avail_out;
					if(true || out_delta==0) {
						auto [buf, len]=_f->buffer();
						strm.next_out=reinterpret_cast<unsigned char*>(buf);
						out_delta=strm.avail_out=len;
					}
					auto r=deflate(&strm, flush);
					out_delta-=strm.avail_out;
					_f->commit(out_delta);
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
							throw std::runtime_error{"error deflate2"};
					}
				} while(!flushed);
				ret=true;
				_i=0;
				return ret;
			}
	};

	template<typename Next> class zlib_input {
		public:
			explicit zlib_input(Next& f): _f{&f}, _buf{new char[BUFSIZ+128]} {
				strm.next_in=nullptr;
				strm.avail_in=0;
				strm.next_out=nullptr;
				strm.avail_out=0;
				strm.zalloc=nullptr;
				strm.zfree=nullptr;
				strm.opaque=0;
				auto windowBits=15+16;
				auto r=inflateInit2(&strm, windowBits);
				if(r!=Z_OK)
					throw std::system_error{std::make_error_code(std::io_errc::stream), strm.msg};
			}
			~zlib_input() { }
			zlib_input(const zlib_input&) =delete;
			zlib_input& operator=(const zlib_input&) =delete;

			std::pair<const char*, std::size_t> buffer() {
				if(!_f) {
					if(_idx<_siz)
						return {_buf.get()+_idx, _siz-_idx};
					return {nullptr, 0};
				}
				if(_idx+128<=_siz)
					return {_buf.get()+_idx, _siz-_idx};
				if(_idx>=128) {
					std::memcpy(_buf.get(), _buf.get()+_idx, _siz-_idx);
					_siz-=_idx;
					_idx=0;
				}
				auto k=BUFSIZ+128-_siz;

				auto r=read_inflate(_buf.get()+_siz, k);
				_siz+=r;
				return {_buf.get()+_idx, _siz-_idx};
			}
			void consume(std::size_t n) {
				_idx+=n;
			}

			std::size_t offset() const noexcept { return strm.total_out; }
		private:
			Next* _f;
			std::unique_ptr<char[]> _buf;
			z_stream strm;
			std::size_t _idx{0};
			std::size_t _siz{0};
			std::size_t read_inflate(char* buf, std::size_t n) {
				auto [ptr, siz]=_f->buffer();
				strm.next_in=const_cast<Bytef*>(reinterpret_cast<const Bytef*>(ptr));
				strm.avail_in=siz;
				strm.next_out=reinterpret_cast<Bytef*>(buf);
				strm.avail_out=n;
				auto r=inflate(&strm, true?Z_FINISH:Z_NO_FLUSH);
				_f->consume(siz-strm.avail_in);
				switch(r) {
					case Z_OK:
					case Z_BUF_ERROR:
						break;
					case Z_STREAM_END:
						if(auto r=inflateEnd(&strm); r!=Z_OK) {
							auto ec=std::make_error_code(std::io_errc::stream);
							throw std::system_error{ec};
							//print("Failed to destroy gzip input filter: ", strm.msg);
						}
						_f=nullptr;
						break;
					default:
						{
							auto ec=std::make_error_code(std::io_errc::stream);
							throw std::system_error{ec};
						}
				}
				return n-strm.avail_out;
			}
	};

}
