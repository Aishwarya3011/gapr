/* gapr/cube.hh
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
#ifndef _GAPR_INCLUDE_CUBE_HH_
#define _GAPR_INCLUDE_CUBE_HH_


#include "gapr/affine-xform.hh"

#include <array>
#include <vector>
#include <atomic>
#include <string>

namespace gapr {

	enum class cube_type: unsigned int {
		unknown=0,
		/*! {
		 * int(0) <-> float(0)
		 * int(65535) <-> float(1)
		 * supported by OpenGL: */
		u8=0x11, i8=0x21,
		u16=0x12, i16=0x22,
		u32=0x14, i32=0x24,
		f16=0x42, f32=0x44,
		/*! } */
		u64=0x18, i64=0x28,
		f64=0x48,
	};

	template<typename T> struct cube_type_from;
	template<> struct cube_type_from<float> {
		constexpr static gapr::cube_type value=gapr::cube_type::f32;
	};
	template<> struct cube_type_from<uint8_t> {
		constexpr static gapr::cube_type value=gapr::cube_type::u8;
	};
	template<> struct cube_type_from<uint16_t> {
		constexpr static gapr::cube_type value=gapr::cube_type::u16;
	};

	inline constexpr unsigned int voxel_size(cube_type type) noexcept {
		return static_cast<unsigned int>(type)&0x0F;
	}
	inline constexpr bool is_float(cube_type type) noexcept {
		return (static_cast<unsigned int>(type)&0xF0)==0x40;
	}
	inline constexpr bool is_signed(cube_type type) noexcept {
		return (static_cast<unsigned int>(type)&0xF0)==0x20;
	}
	inline constexpr bool is_unsigned(cube_type type) noexcept {
		return (static_cast<unsigned int>(type)&0xF0)==0x10;
	}

	class cube_PRIV {
		struct Head {
			std::atomic<unsigned int> refc;
			cube_type type;
			std::array<unsigned int, 3> sizes;
			std::size_t ystride;
			void* _unused{nullptr};
			constexpr Head(cube_type type, std::array<unsigned int, 3> sizes, std::size_t ystride) noexcept:
				refc{1}, type{type}, sizes{sizes}, ystride{ystride} { }
		};

		GAPR_CORE_DECL static Head* alloc(cube_type type, std::array<unsigned int, 3> sizes);
		GAPR_CORE_DECL static void destroy(Head* p) noexcept;
		static void try_ref(Head* p) noexcept {
			if(p)
				p->refc.fetch_add(1);
		}
		static void try_unref(Head* p) noexcept {
			if(p && p->refc.fetch_sub(1)==1)
				destroy(p);
		}
		friend class mutable_cube;
		friend class cube;
	};

	template<typename T> class cube_view {
		public:
			cube_view() =delete;

			cube_type type() const noexcept {
				return _type;
			}
			const std::array<unsigned int, 3>& sizes() const noexcept {
				return _sizes;
			};
			unsigned int sizes(unsigned int i) const noexcept {
				return _sizes[i];
			}
			std::size_t ystride() const noexcept { return _ystride; }
			unsigned int width_adj() const noexcept {
				return _ystride/voxel_size(_type);
			}
			std::size_t zstride() const noexcept { return _ystride*_sizes[1]; }
			T* row(unsigned int y, unsigned int z) const noexcept {
				using Tc=std::conditional_t<std::is_const_v<T>, const char, char>;
				auto p=reinterpret_cast<Tc*>(_data_start);
				auto off=_ystride*(y+z*std::size_t{_sizes[1]});
				return reinterpret_cast<T*>(p+off);
			}

			template<typename Fn> constexpr decltype(auto) visit(Fn&& fn) {
				auto rebind=[this](auto v) ->const auto& {
					using TT=decltype(v);
					using TTT=std::conditional_t<std::is_const_v<T>, std::add_const_t<TT>, std::remove_const_t<TT>>;
					return reinterpret_cast<const gapr::cube_view<TTT>&>(*this);
				};
				switch(_type) {
					case gapr::cube_type::u8:
						return std::forward<Fn>(fn)(rebind(uint8_t{}));
					case gapr::cube_type::u16:
						return std::forward<Fn>(fn)(rebind(uint16_t{}));
						break;
					default:
						// XXX assert(0);
						break;
				}
				return std::forward<Fn>(fn)(*this);
			}
#if 0

			////////////////////
			//aligner

			//int32_t limit(int d) const { return id.offsets[D]+sizes[D]; }
#if 0
			int64_t ystride() const noexcept {
				return _p->ystride;
			}
#endif

	// -> CubeDataRef
#endif

#if 0
			explicit operator bool() const noexcept { return _p; }
			const void* data() const noexcept { return _p->_ptr; }
			uint32_t widthAdj() const noexcept { return _p->ystride; }
			uint32_t heightAdj() const noexcept { return _p->sizes[1]; }
			uint32_t depthAdj() const noexcept { return _p->sizes[2]; }
			uint32_t width() const noexcept { return _p->sizes[0]; }
			uint32_t height() const noexcept { return _p->sizes[1]; }
			uint32_t depth() const noexcept { return _p->sizes[2]; }
			uint32_t size(int i) const noexcept { return _p->sizes[i]; }
#if 0
			std::array<uint32_t, 3> sizes;
			std::size_t ystride;
			template<typename T>
				const T* row(int32_t y, int32_t z) {
					return static_cast<T*>(_p->row(y, z));
				}
		int64_t sizeAdj(int d) const {
			return d==0?(sizes[d]+7)/8*8:sizes[d];
		}
		//int32_t limit(int d) const { return id.offsets[D]+sizes[D]; }
#endif
#endif
		private:
		cube_type _type; // XXX?
			T* _data_start;
			std::array<unsigned int, 3> _sizes;
			std::size_t _ystride;
			cube_view(cube_type type, void* data_start, std::array<unsigned int, 3> sizes, std::size_t ystride) noexcept:
				_type{type}, _data_start{static_cast<T*>(data_start)}, _sizes{sizes}, _ystride{ystride} { }
			friend class mutable_cube;
			friend class cube;
	};

	class mutable_cube {
		public:
			constexpr mutable_cube() noexcept: _p{nullptr} { }
			mutable_cube(cube_type type, std::array<unsigned int, 3> sizes):
				_p{cube_PRIV::alloc(type, sizes)} { }
			~mutable_cube() { if(_p) cube_PRIV::destroy(_p); }
			mutable_cube(const mutable_cube&) =delete;
			mutable_cube& operator=(const mutable_cube&) =delete;
			mutable_cube(mutable_cube&& r) noexcept: _p{r._p} { r._p=nullptr; }
			mutable_cube& operator=(mutable_cube&& r) noexcept {
				std::swap(_p, r._p);
				return *this;
			}

			constexpr explicit operator bool() const noexcept { return _p; }
			template<typename T> cube_view<T> view() const noexcept {
				auto start=reinterpret_cast<char*>(_p)+sizeof(cube_PRIV::Head);
				return cube_view<T>{_p->type, start, _p->sizes, _p->ystride};
			}

		private:
			cube_PRIV::Head* _p;
			friend class cube;
	};

	class cube {
		public:
			constexpr cube() noexcept: _p{nullptr} { }
			~cube() { cube_PRIV::try_unref(_p); }
			cube(const cube& r) noexcept: _p{r._p} { cube_PRIV::try_ref(_p); }
			cube& operator=(const cube& r) noexcept {
				auto p=_p;
				_p=r._p;
				cube_PRIV::try_ref(_p);
				cube_PRIV::try_unref(p);
				return *this;
			}
			cube(cube&& r) noexcept: _p{r._p} { r._p=nullptr; }
			cube& operator=(cube&& r) noexcept {
				std::swap(_p, r._p);
				return *this;
			}

			cube(mutable_cube&& r) noexcept: _p{r._p} { r._p=nullptr; }
			cube& operator=(mutable_cube&& r) noexcept {
				auto p=_p;
				_p=r._p;
				r._p=nullptr;
				cube_PRIV::try_unref(p);
				return *this;
			}

			constexpr explicit operator bool() const noexcept { return _p; }
			template<typename T> cube_view<std::add_const_t<T>> view() const noexcept {
				auto start=reinterpret_cast<char*>(_p)+sizeof(cube_PRIV::Head);
				return cube_view<std::add_const_t<T>>{_p->type, start, _p->sizes, _p->ystride};
			}

		private:
			cube_PRIV::Head* _p;
	};

	//////////////////////////// ..._

	struct cube_info {
		const std::string& name() const noexcept { return _name; }
		const std::string& location() const noexcept { return _location; }
		bool is_pattern() const noexcept { return _is_pattern; }
		std::array<uint32_t, 3> sizes; //*
		std::array<uint32_t, 3> cube_sizes; //*
		affine_xform xform;
		std::array<double, 2> range;

		cube_info(std::string&& name, std::string&& location, bool is_pattern)
			noexcept: _name{std::move(name)}, _location{std::move(location)}, _is_pattern{is_pattern} { }

		std::array<int, 3> to_offseti(const std::array<double, 3>& cent, bool big) const noexcept {
			auto off=xform.to_offset_f(cent);
			auto nn=big?3u:2u;
			std::array<int, 3> res;
			for(unsigned int i=0; i<3; ++i)
				res[i]=std::lround((2*off[i]/cube_sizes[i]-nn)/2)*cube_sizes[i];
			return res;
		}

		private:
		std::string _name;
		std::string _location; //*
		bool _is_pattern;
	};

	struct mesh_info {
		const std::string& name() const noexcept { return _name; }
		const std::string& location() const noexcept { return _location; }
		std::array<int, 4> color; //*
		affine_xform xform;

		mesh_info(std::string&& name, std::string&& location) noexcept:
			_name{std::move(name)}, _location{std::move(location)} { }

		private:
		std::string _name;
		std::string _location; //*
	};

	/*! file:// or http:// url to catalog file
	 * local paths are converted to file://
	 */
	GAPR_CORE_DECL void parse_catalog(std::istream& ifs, std::vector<cube_info>& cube_infos, std::vector<mesh_info>& mesh_infos, std::string_view url);

	GAPR_CORE_DECL std::string pattern_subst(const std::string& pattern, std::array<uint32_t, 3> offsets);
#if 0
	{
		offset;
		xform_func;

	}
#endif

}

#endif
