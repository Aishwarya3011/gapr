/* gapr/buffer.hh
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

//@@@@@
#ifndef _GAPR_INCLUDE_BUFFER_HH_
#define _GAPR_INCLUDE_BUFFER_HH_


#include <atomic>

namespace gapr {

	struct buffer_PRIV {
		struct Head {
			std::atomic<int> _refc;
			uint32_t _len;
			void* _ptr;
			Head(uint32_t len, void* ptr) noexcept:
				_refc{1}, _len{len}, _ptr{ptr} { }
		};

		Head* _ptr;
		uint32_t _off;
		uint32_t _len;

		explicit buffer_PRIV() noexcept: _ptr{nullptr} { }
		buffer_PRIV(Head* ptr, uint32_t off, uint32_t len) noexcept:
			_ptr{ptr}, _off{off}, _len{len} { }
		explicit buffer_PRIV(uint32_t len):
			buffer_PRIV{alloc(len), sizeof(Head), len} { }
		~buffer_PRIV() { if(_ptr) unref(_ptr); }

		buffer_PRIV(const buffer_PRIV& r) noexcept: _ptr{r._ptr} {
			if(_ptr) {
				_off=r._off;
				_len=r._len;
				ref(_ptr);
			}
		}
		buffer_PRIV& operator=(const buffer_PRIV& r) noexcept {
			auto ptr=_ptr;
			_ptr=r._ptr;
			if(_ptr) {
				_off=r._off;
				_len=r._len;
				ref(_ptr);
			}
			if(ptr)
				unref(ptr);
			return *this;
		}
		buffer_PRIV(buffer_PRIV&& r) noexcept: _ptr{r._ptr} {
			if(_ptr) {
				_off=r._off;
				_len=r._len;
				r._ptr=nullptr;
			}
		}
		buffer_PRIV& operator=(buffer_PRIV&& r) noexcept {
			auto ptr=_ptr;
			_ptr=r._ptr;
			if(_ptr) {
				_off=r._off;
				_len=r._len;
				r._ptr=nullptr;
			}
			if(ptr)
				unref(ptr);
			return *this;
		}

		bool to_bool() const noexcept { return _ptr; }
		char* do_base() const noexcept {
			return _ptr?reinterpret_cast<char*>(_ptr)+_off:nullptr;
		}
		uint32_t do_len() const noexcept { return _ptr?_len:0; }
		void do_shrink(uint32_t len) noexcept { _len=len; }
		void do_skip(uint32_t len) noexcept { _off+=len; _len-=len; }
		buffer_PRIV do_split(uint32_t len) noexcept {
			buffer_PRIV buf{_ptr, _off, len};
			_off+=len; _len-=len;
			ref(_ptr);
			return buf;
		}

		static void ref(Head* p) noexcept {
			p->_refc.fetch_add(1);
		}
		static void unref(Head* p) noexcept {
			if(p->_refc.fetch_sub(1)==1)
				destroy(p);
		}
		static Head* alloc(uint32_t len);
		static void destroy(Head* p) noexcept;
	};

	class buffer final: private buffer_PRIV {
		public:
			buffer() noexcept: buffer_PRIV{} { }
			explicit buffer(uint32_t len): buffer_PRIV{len} { }
			~buffer() { }

			buffer(const buffer&) =delete;
			buffer& operator=(const buffer&) =delete;

			buffer(buffer&& r) noexcept: buffer_PRIV{std::move(r)} { }
			buffer& operator=(buffer&& r) noexcept {
				buffer_PRIV::operator=(std::move(r));
				return *this;
			}

			explicit operator bool() const noexcept { return to_bool(); }
			char* base() const noexcept { return do_base(); }
			uint32_t len() const noexcept { return do_len(); }

			void shrink(uint32_t len) noexcept { return do_shrink(len); }
			void skip(uint32_t len) noexcept { return do_skip(len); }
			buffer split(uint32_t len) noexcept { return do_split(len); }

		private:
			buffer(buffer_PRIV&& r) noexcept: buffer_PRIV{std::move(r)} { }
			friend class cbuffer;
	};

	class cbuffer final: private buffer_PRIV {
		public:
			cbuffer() noexcept: buffer_PRIV{} { }
			~cbuffer() { }

			cbuffer(const cbuffer& r) noexcept: buffer_PRIV{r} { }
			cbuffer& operator=(const cbuffer& r) noexcept {
				buffer_PRIV::operator=(r);
				return *this;
			}
			cbuffer(cbuffer&& r) noexcept: buffer_PRIV{std::move(r)} { }
			cbuffer& operator=(cbuffer&& r) noexcept {
				buffer_PRIV::operator=(std::move(r));
				return *this;
			}
			cbuffer(buffer&& r) noexcept: buffer_PRIV{std::move(r)} { }
			cbuffer& operator=(buffer&& r) noexcept {
				buffer_PRIV::operator=(std::move(r));
				return *this;
			}

			explicit operator bool() const noexcept { return to_bool(); }
			const char* base() const noexcept { return do_base(); }
			uint32_t len() const noexcept { return do_len(); }

			void shrink(uint32_t len) noexcept { return do_shrink(len); }
			void skip(uint32_t len) noexcept { return do_skip(len); }
			cbuffer split(uint32_t len) noexcept { return do_split(len); }

		private:
			cbuffer(buffer_PRIV&& r) noexcept: buffer_PRIV{std::move(r)} { }
	};

}

#endif
