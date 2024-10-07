/* streambuf.hh
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

// XXX Consider FILE like APIs, and these aspects:
//   lower layer: FILE, memory, zip_file_t, tcp stream
//   readable, writable, seekable
//   sync ops and async ops
//
// XXX API???
//   FILE / streambuf / customized?
//   open mode: r w r+ w+ a a+...???
//
// XXX XXX do not use this api?
//   so file impl. is not needed
//   for mem_input/output, just use impl. as std::streambuf
//   for filter, use simpler filter api (no seek, zero copy, statically chained)

#ifndef _GAPR_INCLUDE_STREAMBUF_HH_
#define _GAPR_INCLUDE_STREAMBUF_HH_

#include "gapr/config.hh"

#include <streambuf>
#include <memory>

namespace gapr {

	class mem_file;

	class Streambuf: public std::streambuf {
		public:
			bool is_open() { return _isopen; }
			virtual void close() =0;

			static Streambuf* memInput(gapr::mem_file&& file);
			static Streambuf* fileInput(const char* file, const char* mode);
			static Streambuf* fileOutput(const char* file, const char* mode);
			static Streambuf* inputFilter(Streambuf& buf, const char* method);
			constexpr static std::size_t bufsiz=8*1024;
		protected:
		private:
			template<typename T> friend class FilterInput;
			//template<typename T> friend class FilterOutput;
			//static Streambuf* outputFilter(Streambuf* buf, const char* method);
			bool _isopen;
	};

	GAPR_CORE_DECL std::unique_ptr<Streambuf> make_streambuf(gapr::mem_file&& file);
	GAPR_CORE_DECL std::unique_ptr<Streambuf> make_streambuf(const char* path);

}

#endif
