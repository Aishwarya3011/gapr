/* gapr/bbox.hh
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

#ifndef _GAPR_INCLUDE_BBOX_HH_
#define _GAPR_INCLUDE_BBOX_HH_

#include "gapr/vec3.hh"
#include "gapr/model.hh"


namespace gapr {

	class bbox_int {
		public:
			constexpr bbox_int() noexcept:
				_min{inf(), inf(), inf()}, _max{ninf(), ninf(), ninf()} { }

			void add(gapr::node_attr::ipos_type pt) noexcept {
				for(unsigned int i=0; i<3; ++i) {
					auto v=pt[i];
					set_min(i, v);
					set_max(i, v);
				}
			}
			void grow(int r) noexcept {
				for(unsigned int i=0; i<3; ++i) {
					_min[i]=_min[i]-r;
					_max[i]=_max[i]+r;
				}
			}
			bool hit_test(gapr::node_attr::ipos_type pt) const noexcept {
				for(unsigned int i=0; i<3; ++i) {
					if(pt[i]<=_min[i])
						return false;
					if(pt[i]>=_max[i])
						return false;
				}
				return true;
			}

		private:
			gapr::node_attr::ipos_type _min, _max;
			using type=std::decay_t<decltype(_min[0])>;
			constexpr static type inf() noexcept {
				return std::numeric_limits<type>::max();
			}
			constexpr static type ninf() noexcept {
				return std::numeric_limits<type>::min();
			}
			void set_min(unsigned int i, type v) noexcept {
				if(v<_min[i])
					_min[i]=v;
			}
			void set_max(unsigned int i, type v) noexcept {
				if(v>_max[i])
					_max[i]=v;
			}
			friend class bbox;
			constexpr std::array<double, 3> get(bool max) const noexcept {
				gapr::node_attr attr{max?_max:_min, gapr::misc_attr{}};
				return {attr.pos(0), attr.pos(1), attr.pos(2)};
			}
	};

	class bbox {
		public:
			constexpr bbox() noexcept:
				_min{inf(), inf(), inf()}, _max{ninf(), ninf(), ninf()} { }
			explicit constexpr bbox(const bbox_int& box) noexcept:
				_min{box.get(false)}, _max{box.get(true)} { }

			template<typename T> void add(const gapr::vec3<T>& pt) noexcept {
				for(unsigned int i=0; i<3; ++i) {
					double v=pt[i];
					set_min(i, v);
					set_max(i, v);
				}
			}
			void grow(double r) noexcept {
				for(unsigned int i=0; i<3; ++i) {
					_min[i]=_min[i]-r;
					_max[i]=_max[i]+r;
				}
			}
			void add(const bbox& box) noexcept {
				for(unsigned int i=0; i<3; i++) {
					auto v=box._min[i];
					auto v2=box._max[i];
					if(v<=v2) {
						set_min(i, v);
						set_max(i, v2);
					}
				}
			}
			double diameter_and_center(gapr::vec3<double>& center) const noexcept {
				double max_d{1.0};
				for(unsigned int i=0; i<3; i++) {
					auto d=_max[i]-_min[i];
					if(d>max_d)
						max_d=d;
					center[i]=(_min[i]+_max[i])/2;
				}
				return max_d;
			}

			bool hit_test(const bbox& box) const noexcept {
				for(unsigned int i=0; i<3; ++i) {
					if(box._max[i]<=_min[i])
						return false;
					if(box._min[i]>=_max[i])
						return false;
				}
				return true;
			}
			void intersect(const bbox& b) noexcept;
			template<typename T>
			bool hit_test(const gapr::vec3<T>& pt) const noexcept {
				for(unsigned int i=0; i<3; ++i) {
					if(pt[i]<=_min[i])
						return false;
					if(pt[i]>=_max[i])
						return false;
				}
				return true;
			}

		private:
			std::array<double, 3> _min, _max;
			constexpr static double inf() noexcept {
				return std::numeric_limits<double>::infinity();
			}
			constexpr static double ninf() noexcept {
				return -std::numeric_limits<double>::infinity();
			}
			void set_min(unsigned int i, double v) noexcept {
				if(v<_min[i])
					_min[i]=v;
			}
			void set_max(unsigned int i, double v) noexcept {
				if(v>_max[i])
					_max[i]=v;
			}
	};

}

#endif
