/* detail/streambuf.hh
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


#ifndef _GAPR_INCLUDE_DETAIL_STREAMBUF_HH_
#define _GAPR_INCLUDE_DETAIL_STREAMBUF_HH_

#include "gapr/streambuf.hh"

#include <cassert>

namespace gapr {

	template<typename T> class Streambuf_Output: public Streambuf {
		public:
			template<typename... Args>
				Streambuf_Output(Args&&... args):
					Streambuf{}, _impl{std::forward<Args>(args)...}
			{ }
			~Streambuf_Output() {
				if(pbase()<pptr()) {
					std::error_code ec{};
					_impl.commit(pptr()-pbase(), ec);
				}
			}

			void close() override {
				std::error_code ec{};
				flush_buffer(ec);
				_impl.close(ec);
				if(ec)
					throw std::system_error{ec};
			}

		protected:
			std::streampos seekoff(std::streamoff off, std::ios_base::seekdir way, std::ios_base::openmode which) override {
				std::error_code ec{};
				if(!(std::ios_base::out&which))
					return -1;
				int whence=SEEK_SET;
				if(way==std::ios_base::cur) {
					if(off==0)
						return ftell(ec)+pptr()-pbase();
					whence=SEEK_CUR;
				} else if(way==std::ios_base::end) {
					whence=SEEK_END;
				}

				flush_buffer(ec);

				fseek(off, whence, ec);
				return ftell(ec);
			}

			std::streampos seekpos(std::streampos sp, std::ios_base::openmode which) override {
				std::error_code ec{};
				if(!(std::ios_base::out&which))
					return -1;

				flush_buffer(ec);

				fseek(sp, SEEK_SET, ec);

				return ftell(ec);
			}

			int sync() override {
				std::error_code ec{};
				flush_buffer(ec);
				_impl.fflush(ec);
				if(ec)
					throw std::system_error{ec};
				return 0;
			}
			std::streamsize showmanyc() override { return -1; }
			std::streamsize xsgetn(char* s, std::streamsize n) override { return 0; }
			int underflow() override { return EOF; }
			int pbackfail(int c) override { return EOF; }

			std::streamsize xsputn(const char* s, std::streamsize n) override {
				std::size_t slot=epptr()-pptr();
				if(static_cast<std::size_t>(n)<bufsiz+slot)
					return Streambuf::xsputn(s, n);
				std::error_code ec{};
				std::streamsize total=n;

				traits_type::copy(pptr(), s, slot);
				pbump(slot);
				s+=slot;
				n-=slot;
				flush_buffer(ec);
				auto towrite=n/bufsiz*bufsiz;
				_impl.fwrite(s, towrite, ec);
				if(ec)
					throw std::system_error{ec};
				s+=towrite;
				n-=towrite;
				prepare(ec);
				auto tocopy=epptr()-pptr();
				if(tocopy>n)
					tocopy=n;
				traits_type::copy(pptr(), s, tocopy);
				s+=tocopy;
				n-=tocopy;
				pbump(tocopy);
				return total-n;
			}

			int overflow(int c) override {
				std::error_code ec{};
				flush_buffer(ec);
				prepare(ec);
				if(c==EOF)
					return 0;
				*pptr()=traits_type::to_char_type(c);
				pbump(1);
				return c;
			}

		private:
			void flush_buffer(std::error_code& ec) {
				if(pbase()<pptr()) {
					_impl.commit(pptr()-pbase(), ec);
					setp(nullptr, nullptr);
					if(ec)
						throw std::system_error{ec};
				}
			}
			void fseek(off_t off, int whence, std::error_code& ec) {
				_impl.fseek(off, whence, ec);
				if(ec)
					throw std::system_error{ec};
			}
			off_t ftell(std::error_code& ec) {
				auto ret=_impl.ftell(ec);
				if(ec)
					throw std::system_error{ec};
				return ret;
			}
			void prepare(std::error_code& ec) {
				void* ptr;
				auto l=_impl.prepare(&ptr, ec);
				if(ec)
					throw std::system_error{ec};
				auto p=static_cast<char*>(ptr);
				setp(p, p+l);
			}
			T _impl;
	};

	template<typename T> class Streambuf_Input: public Streambuf {
		public:
			template<typename... Args>
				Streambuf_Input(Args&&... args):
					Streambuf{}, _impl{std::forward<Args>(args)...}
			{ }
			~Streambuf_Input() {
			}

			void close() override {
				std::error_code ec;
				_impl.close(ec);
				if(ec)
					throw std::system_error{ec};
			}

		protected:
			std::streampos seekoff(std::streamoff off, std::ios_base::seekdir way, std::ios_base::openmode which) override {
				std::error_code ec{};
				if(!(std::ios_base::in&which))
					return -1;
				int whence=SEEK_SET;
				if(way==std::ios_base::cur) {
					if(off+gptr()<=egptr() && off+gptr()>=eback()) {
						gbump(off);
						return ftell(ec)+gptr()-egptr();
					}
					whence=SEEK_CUR;
					off-=egptr()-gptr();
				} else if(way==std::ios_base::end) {
					whence=SEEK_END;
				}
				fseek(off, whence, ec);
				setg(nullptr, nullptr, nullptr);
				return ftell(ec);
			}
			std::streampos seekpos(std::streampos sp, std::ios_base::openmode which) override {
				std::error_code ec{};
				if(!(std::ios_base::in&which))
					return -1;
				fseek(sp, SEEK_SET, ec);
				setg(nullptr, nullptr, nullptr);
				return ftell(ec);
			}
			int sync() override { return 0; }
			std::streamsize showmanyc() override {
				assert(egptr()==gptr());
				auto rem=_impl.remain();
				std::streamsize ret{0};
				if(rem.second) {
					ret+=rem.first;
					if(ret==0)
						ret=-1;
				} else {
					if(ret==0)
						ret=16*1024;
				}
				return ret;
			}

			std::streamsize xsgetn(char* s, std::streamsize n) override {
				std::size_t avail=egptr()-gptr();
				if(static_cast<std::size_t>(n)<bufsiz+avail)
					return Streambuf::xsgetn(s, n);

				std::error_code ec{};
				std::streamsize total=n;
				if(avail) {
					traits_type::copy(s, gptr(), avail);
					setg(eback(), egptr(), egptr());
					s+=avail;
					n-=avail;
				}
				// just read blocks directly
				auto toread=n/bufsiz*bufsiz;
				auto r=_impl.fread(s, toread, ec);
				if(ec)
					throw std::system_error{ec};
				s+=r;
				n-=r;
				if(r==toread && n>0) {
					n-=Streambuf::xsgetn(s, n);
				}
				return total-n;
			}

			int underflow_(std::error_code& ec) {
				int ret=EOF;
				if(gptr()<egptr()) {
					ret=traits_type::to_int_type(*gptr());
				} else {
					const void* ptr;
					auto r=_impl.fmap(&ptr, ec);
					if(ec)
						throw std::system_error{ec};
					if(r!=0) {
						auto p=static_cast<char*>(const_cast<void*>(ptr));
						setg(p, p, p+r);
						ret=traits_type::to_int_type(*gptr());
					}
				}
				return ret;
			}
			int underflow() override {
				std::error_code ec{};
				return underflow_(ec);
			}
			int pbackfail(int c) override {
				std::error_code ec{};
				int ret=EOF;
				if(eback()<gptr()) {
					gbump(-1);
					if(c==EOF)
						ret=0;
					else
						ret=c;
				} else {
					fseek(gptr()-egptr()-1, SEEK_CUR, ec);
					setg(nullptr, nullptr, nullptr);
					auto r=underflow_(ec);
					if(r!=EOF) {
						if(c==EOF)
							ret=0;
						else
							ret=c;
					}
				}
				return ret;
			}
			std::streamsize xsputn(const char* s, std::streamsize n) override {
				return 0;
			}
			int overflow(int c) override { return EOF; }

		private:
			T _impl;

			void fseek(off_t off, int whence, std::error_code& ec) {
				_impl.fseek(off, whence, ec);
				if(ec)
					throw std::system_error{ec};
			}
			off_t ftell(std::error_code& ec) {
				auto ret=_impl.ftell(ec);
				if(ec)
					throw std::system_error{ec};
				return ret;
			}
	};

}

#endif
