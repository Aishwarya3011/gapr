/* gapr/archive.hh
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

//@@@@
#ifndef _GAPR_INCLUDE_ARCHIVE_HH_
#define _GAPR_INCLUDE_ARCHIVE_HH_

#include "gapr/config.hh"

#include <array>
#include <memory>
#include <cassert>
#include <string_view>

/*! archive-like api based on lmdb
 *
 * to write file:
 * (in a work thread) {
 *   get_writer;
 *   get_buffer;
 *   write;
 *   flush;
 * }
 *
 * to read file:
 * (in any thread, and may span threads) {
 *   get_reader;
 *   get_buffer; (block by io)
 *   read; (block by io)
 *   end;
 * }
 *
 * the archive is more compact if files are added in lexical order.
 *
 */

namespace gapr {

	class GAPR_CORE_DECL archive {
		public:
			constexpr archive() noexcept: _p{} { }
			explicit archive(const char* path, bool rdonly=false);

			class writer;
			writer get_writer(std::string_view key);
			class reader;
			reader get_reader(std::string_view key) const;

			std::unique_ptr<std::streambuf> reader_streambuf(std::string_view key) const;

			void begin_buffered(unsigned int level) const;
			bool end_buffered();

		private:
			static constexpr size_t CHUNK_SIZE{64*1024-16};
			struct PRIV;
			struct writer_DATA {
				std::size_t _i;
				std::size_t _o;
				std::array<char, CHUNK_SIZE+128> _buf;
			};
			struct reader_DATA {
				const char* _buf;
				std::size_t _idx;
				std::size_t _siz;
			};
			std::shared_ptr<PRIV> _p;
	};

	class GAPR_CORE_DECL archive::writer {
		public:
			~writer();
			writer(writer&& r) noexcept =delete;
			writer& operator=(writer&& r) noexcept =delete;

			explicit operator bool() const noexcept { return _p.get(); }

			std::pair<char*, std::size_t> buffer() {
				auto i=_p->_i;
				assert(i>0 && i<=CHUNK_SIZE);
				return {&_p->_buf[i], CHUNK_SIZE+128-i};
			}
			void commit(std::size_t n) {
				auto i=(_p->_i+=n);
				assert(i<=CHUNK_SIZE+128);
				if(i>CHUNK_SIZE)
					do_commit();
			}
			std::size_t offset() {
				return _p->_o+_p->_i-1;
			}
			bool flush();

		private:
			std::unique_ptr<writer_DATA> _p;
			explicit writer(const std::shared_ptr<PRIV>& apriv, std::string_view key);
			void do_commit();
			friend class archive;
	};

	class GAPR_CORE_DECL archive::reader {
		public:
			~reader();
			reader(reader&& r) noexcept =default;
			reader& operator=(reader&& r) noexcept =default;

			explicit operator bool() const noexcept { return _p.get(); }

			std::pair<const char*, std::size_t> buffer() {
				auto buf=_p->_buf;
				if(buf) {
					auto idx=_p->_idx;
					return {buf+idx, _p->_siz-idx};
				}
				auto siz=_p->_siz;
				if(!siz)
					return {nullptr, 0};
				return do_buffer();
			}
			void consume(std::size_t n) {
				auto idx=(_p->_idx+=n);
				if(idx+128>_p->_siz)
					_p->_buf=nullptr;
			}
			std::size_t offset() const noexcept { return 0; }

		private:
			std::unique_ptr<reader_DATA> _p;
			explicit reader(const std::shared_ptr<PRIV>& apriv, std::string_view key);
			std::pair<const char*, std::size_t> do_buffer();
			friend class archive;
	};

	inline archive::writer archive::get_writer(std::string_view key) {
		return writer{_p, key};
	}
	inline archive::reader archive::get_reader(std::string_view key) const {
		return reader{_p, key};
	}

}

#endif
