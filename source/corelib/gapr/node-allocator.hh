/* gapr/node-allocator.hh
 *
 * Copyright (C) 2021 GOU Lingfeng
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

//@@@
#ifndef _GAPR_INCLUDE_NODE_ALLOCATOR_HH_
#define _GAPR_INCLUDE_NODE_ALLOCATOR_HH_

#include <memory>

namespace gapr {

	namespace detail {
		struct MemPool {
			std::size_t siz;
			//siz>=sizoef(Piece)
			union Chunk {
				struct {
					Chunk* next;
					std::size_t alloc;
				} heads[2];
				std::array<void*, 1024*4> buf;
			};
			Chunk* chunks;
			struct Piece {
				Piece* next;
			};
			Piece* avail;
			MemPool(std::size_t s): siz{s}, chunks{nullptr}, avail{nullptr} {
				if(siz<sizeof(Piece))
					siz=sizeof(Piece);
			}
			~MemPool() {
				while(chunks) {
					std::unique_ptr<Chunk> p{chunks};
					chunks=p->heads[0].next;
				}
			}
			void* malloc(std::size_t s) {
				if(s>siz) {
					//fprintf(stderr, "allocate: %zd %zd\n", s, siz);
					return new char[s];
				}
				if(avail) {
					auto p=avail;
					avail=p->next;
					return p;
				}
				if(chunks) {
					auto d=chunks->heads[0].alloc;
					if(d+siz<=sizeof(Chunk)) {
						auto p=reinterpret_cast<char*>(chunks)+d;
						chunks->heads[0].alloc=d+siz;
						return p;
					}
				}
				auto chk=std::make_unique<Chunk>();
				chk->heads[0].next=chunks;
				chk->heads[0].alloc=sizeof(chk->heads)/2+siz;
				auto p=reinterpret_cast<char*>(chk.get())+sizeof(chk->heads)/2;
				chunks=chk.release();
				return p;
			}
			void free(void* p, std::size_t s) {
				if(s>siz) {
					//fprintf(stderr, "free: %zd %zd\n", s, siz);
					char* pp=static_cast<char*>(p);
					delete[] pp;
					return;
				}
				auto pp=static_cast<Piece*>(p);
				pp->next=avail;
				avail=pp;
			}
		};
	}

	template<typename T> struct node_allocator {
		using value_type=T;
		using propagate_on_container_move_assignment=std::true_type;

		T* allocate(std::size_t n) {
			if(n>std::numeric_limits<std::size_t>::max()/sizeof(T))
				throw std::bad_alloc{};
			return static_cast<T*>(_pool->malloc(n*sizeof(T)));
		}
		void deallocate(T* p, std::size_t n) noexcept {
			_pool->free(p, n*sizeof(T));
		}
		bool operator==(const node_allocator<T>& r) const noexcept {
			return _pool==r._pool;
		}
		bool operator!=(const node_allocator<T>& r) const noexcept {
			return !(*this==r);
		}

		node_allocator(): _pool{std::make_shared<detail::MemPool>(sizeof(T))} {
		}
		template<typename T2>
			node_allocator(const node_allocator<T2>& b) noexcept: _pool{b._pool} {
			}

		private:
		std::shared_ptr<detail::MemPool> _pool;
		template<typename T2>
			friend struct node_allocator;
	};

}

#endif
