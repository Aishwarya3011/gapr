/* gapr/mem-file.hh
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
#ifndef _GAPR_INCLUDE_MEM_FILE_HH_
#define _GAPR_INCLUDE_MEM_FILE_HH_

#include "gapr/config.hh"

#include "gapr/buffer-view.hh"

//#include <utility>
#include <atomic>
#include <cassert>

/*! two modes
 * flat:     data        |info
 * non-flat: info|data ptr1   |data ptr 2
 */

namespace gapr {

	class mem_file_PRIV {
		static constexpr std::size_t BLK_SIZ1=4*1024; // XXX hold most deltas
		static constexpr std::size_t BLK_SIZ2=256*1024;

		struct Head {
			std::atomic<unsigned int> refc;
			std::size_t count;
			std::size_t max_count;
			std::size_t len;
			std::size_t ntail;
			void* _ptr;

			Head(std::size_t ntail, void* buf_ptr) noexcept: // flat
				refc{1}, count{0}, max_count{0}, len{0}, ntail{ntail}, _ptr{buf_ptr} { }
			Head(void* arr_ptr, std::size_t arr_siz) noexcept: // !flat
				refc{1}, count{0}, max_count{arr_siz}, len{0}, ntail{0}, _ptr{arr_ptr} { }
			bool is_flat() const noexcept { return max_count==0; }

			GAPR_CORE_DECL buffer_view map(std::size_t off) const noexcept;
			void add_tail(std::size_t n) noexcept {
				assert(n<=ntail);
				len+=n;
				ntail-=n;
			}
		};

		static constexpr std::size_t ALIGN_OPT=1024*1024;
		static constexpr std::size_t SIZE_OPT=((BLK_SIZ1-sizeof(Head))/sizeof(char*)*BLK_SIZ1+ALIGN_OPT-1)/ALIGN_OPT*ALIGN_OPT;
		static constexpr std::size_t COUNT_OPT=SIZE_OPT/BLK_SIZ1;

		GAPR_CORE_DECL static Head* alloc(bool flat);
		GAPR_CORE_DECL static void destroy(Head* p) noexcept;
		static void try_ref(Head* p) noexcept {
			if(p)
				p->refc.fetch_add(1);
		}
		static void try_unref(Head* p) noexcept {
			if(p && p->refc.fetch_sub(1)==1)
				destroy(p);
		}
	////////////////////////////////////////////

#if 0
		//mem_file_PRIV(s);
		//mem_file_PRIV(std::size_t hint, void* ptr) noexcept:
			//refc{1}, count{0}, max_count{hint}, len{0}, _ptr{ptr} { }


		void shrink(std::size_t s) noexcept;
		void extend(std::size_t s);

#endif
#if 0
		char* row(std::size_t i) {
			assert(i<count);
			auto base=reinterpret_cast<char*>(this)+sizeof(mem_file_PRIV);
			return reinterpret_cast<char**>(base)[i];
		}
#endif

		friend class mutable_mem_file;
		friend class mem_file;
	};

	class mutable_mem_file {
		public:
			explicit mutable_mem_file() noexcept: _p{nullptr} { }
			explicit mutable_mem_file(bool flat):
				_p{mem_file_PRIV::alloc(flat)} { }
			~mutable_mem_file() { if(_p) mem_file_PRIV::destroy(_p); }
			mutable_mem_file(const mutable_mem_file&) =delete;
			mutable_mem_file& operator=(const mutable_mem_file&) =delete;
			mutable_mem_file(mutable_mem_file&& r) noexcept:
				_p{r._p} { r._p=nullptr; }
			mutable_mem_file& operator=(mutable_mem_file&& r) noexcept {
				std::swap(_p, r._p);
				return *this;
			}

			GAPR_CORE_DECL buffer_view map_tail();
			void add_tail(std::size_t n) const noexcept {
				return _p->add_tail(n);
			}
			void reset() noexcept {
				mem_file_PRIV::destroy(_p);
				_p=mem_file_PRIV::alloc(true);
				// XXX
				//_p->len=0;
				//_p->ntail=ntail-=n;
			}

			explicit operator bool() const noexcept { return _p; }
			gapr::buffer_view map(std::size_t offset) noexcept {
				return _p->map(offset);
			}
			std::size_t size() const noexcept { return _p->len; }

		private:
			mem_file_PRIV::Head* _p;
			friend class mem_file;
	};

	class mem_file {
		public:
			explicit mem_file() noexcept: _p{nullptr} { }
			~mem_file() { mem_file_PRIV::try_unref(_p); }
			mem_file(const mem_file& r) noexcept:
				_p{r._p} { mem_file_PRIV::try_ref(_p); }
			mem_file& operator=(const mem_file& r) noexcept {
				auto ptr=_p;
				_p=r._p;
				mem_file_PRIV::try_ref(_p);
				mem_file_PRIV::try_unref(ptr);
				return *this;
			}
			mem_file(mem_file&& r) noexcept: _p{r._p} { r._p=nullptr; }
			mem_file& operator=(mem_file&& r) noexcept {
				std::swap(_p, r._p);
				return *this;
			}

			mem_file(mutable_mem_file&& r) noexcept: _p{r._p} { r._p=nullptr; }
			mem_file& operator=(mutable_mem_file&& r) noexcept {
				auto ptr=_p;
				_p=r._p;
				r._p=nullptr;
				mem_file_PRIV::try_unref(ptr);
				return *this;
			}

			explicit operator bool() const noexcept { return _p; }
			std::string_view map(std::size_t offset) const noexcept {
				return _p->map(offset);
			}
			std::size_t size() const noexcept { return _p->len; }
			// XXX

		private:
			mem_file_PRIV::Head* _p;
	};

}

#endif
