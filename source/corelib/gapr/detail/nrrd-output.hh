/* gapr/detail/nrrd-output.hh
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

//@@@@@
#ifndef _GAPR_INCLUDE_NRRD_OUTPUT_HH_
#define _GAPR_INCLUDE_NRRD_OUTPUT_HH_


#include "gapr/cube.hh"

namespace gapr {

	class GAPR_CORE_DECL nrrd_output {
		public:
			constexpr explicit nrrd_output(std::ostream& base, bool gzip=true) noexcept:
				_base{base}, _gzip{gzip} { }
			~nrrd_output() =default;
			nrrd_output(const nrrd_output&) =delete;
			nrrd_output& operator=(const nrrd_output&) =delete;

			void header() {
				_base<<"NRRD0004\n";
			}
			template<typename... Args>
				void comment(const char* str, Args&&... args) {
					_base<<"# "<<str;
					if constexpr(sizeof...(args)>0)
						(_base<<...<<std::forward<Args>(args));
					_base<<'\n';
				}

			void finish(gapr::cube cube, const gapr::affine_xform* xform=nullptr) {
				auto view=cube.view<const void>();
				return finish(view, xform);
			}
			void finish(const gapr::cube_view<const void>& view, const gapr::affine_xform* xform=nullptr) {
				return finish_impl(view.row(0, 0), view.type(), view.sizes(), view.ystride(), view.zstride(), xform?&xform->origin:nullptr, xform?&xform->direction:nullptr);
			}
			template<typename T>
				void finish(const T* ptr, unsigned int w,  unsigned int h, unsigned int d) {
					return finish(ptr, gapr::cube_type_from<T>::value, w, h, d);
				}
			void finish(const void* ptr, gapr::cube_type type, unsigned int w,  unsigned int h, unsigned int d) {
				std::size_t es=voxel_size(type);
				return finish_impl(ptr, type, {w, h, d}, w*es, w*es*h);
			}

			template<typename T>
			static void save(const std::string& file, const T* ptr, std::array<unsigned int, 3> sizes) {
				return save_impl(file, ptr, gapr::cube_type_from<T>::value, sizes);
			}

		private:
			std::ostream& _base;
			bool _gzip;

			void finish_impl(const void* ptr, gapr::cube_type type, std::array<unsigned int, 3> sizes, std::size_t ystride, std::size_t zstride, const std::array<double, 3>* origin=nullptr, const std::array<double, 9>* direction=nullptr);
			static void save_impl(const std::string& file, const void* ptr, gapr::cube_type type, std::array<unsigned int, 3> sizes);
	};

}

#endif
