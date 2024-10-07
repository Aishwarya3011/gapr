/* gapr/affine-xform.hh
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
#ifndef _GAPR_INCLUDE_AFFINE_XFORM_HH_
#define _GAPR_INCLUDE_AFFINE_XFORM_HH_

#include "gapr/config.hh"

#include <array>
#include <cmath>

namespace gapr {

	struct affine_xform {

		std::array<double, 3> origin; //*
		std::array<double, 9> direction; //*

		mutable std::array<double, 9> direction_inv;
		mutable std::array<double, 3> resolution;

		explicit affine_xform() noexcept: origin{0.0, },
					direction{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0} { }
		explicit affine_xform(double scale) noexcept: origin{0.0, },
					direction{scale, 0.0, 0.0, 0.0, scale, 0.0, 0.0, 0.0, scale} { }

		GAPR_CORE_DECL bool update_direction_inv() const noexcept;

		void update_resolution() const noexcept {
			auto& res=resolution;
			auto& d=direction;
			for(int i=0; i<3; i++) {
				auto r2=d[0+i*3]*d[0+i*3]+d[1+i*3]*d[1+i*3]+d[2+i*3]*d[2+i*3];
				res[i]=std::sqrt(r2);
			}
		}

		std::array<double, 3> to_offset_f(const std::array<double, 3>& pos) const noexcept {
			auto [x, y, z]=pos;
			double dx=x-origin[0];
			double dy=y-origin[1];
			double dz=z-origin[2];
			auto xp=direction_inv[0]*dx+direction_inv[3]*dy+direction_inv[6]*dz;
			auto yp=direction_inv[1]*dx+direction_inv[4]*dy+direction_inv[7]*dz;
			auto zp=direction_inv[2]*dx+direction_inv[5]*dy+direction_inv[8]*dz;
			return {xp, yp, zp};
		}
		// XXX deprecate
		std::array<unsigned int, 3> to_offset(const std::array<double, 3>& pos) const noexcept {
			std::array<unsigned int, 3> r;
			auto rf=to_offset_f(pos);
			for(unsigned int i=0; i<3; ++i)
				r[i]=rf[i];
			return r;
		}
		std::array<int, 3> to_offseti(const std::array<double, 3>& pos) const noexcept {
			std::array<int, 3> r;
			auto rf=to_offset_f(pos);
			for(unsigned int i=0; i<3; ++i)
				r[i]=rf[i];
			return r;
		}
		std::array<double, 3> from_offset(std::array<unsigned int, 3> offset) const noexcept {
			std::array<double, 3> f;
			for(unsigned int i=0; i<3; ++i)
				f[i]=offset[i];
			return from_offset_f(f);
		}
		std::array<double, 3> from_offset_f(const std::array<double, 3>& offset) const noexcept {
			std::array<double, 3> res;
			for(unsigned int i=0; i<3; ++i) {
				double v=origin[i];
				for(unsigned int j=0; j<3; ++j)
					v+=offset[j]*direction[i+3*j];
				res[i]=v;
			}
			return res;
		}
#if 0
		bool operator==(const CubeXform& r) const {
			for(int i=0; i<3; i++)
				if(origin[i]!=r.origin[i])
					return false;
			for(int i=0; i<9; i++)
				if(direction[i]!=r.direction[i])
					return false;
			return true;
		}
#endif

	};

	struct affine_xform_ext {
	};

}

#endif
