/* detail/fixed-float.hh
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
#ifndef _GAPR_INCLUDE_DETAIL_FIXED_POINT_HH_
#define _GAPR_INCLUDE_DETAIL_FIXED_POINT_HH_


#include <type_traits>
#include <cmath>

namespace gapr {

	template<typename T, unsigned int LSB, unsigned int MSB, int RSHIFT>
		struct fixed_point {
			constexpr static double get(T host);
			constexpr static T enc(double value);
			constexpr static void set(T& host, double value) {
				host=(host&(~(_sgn_bit|_sb_mask)))|enc(value);
			}

			private:
			static_assert(std::is_integral_v<T>);
			static_assert(MSB<sizeof(T)*8);
			static_assert(LSB<MSB);
			using UT=std::make_unsigned_t<T>;
			constexpr static double to_float(UT v) {
				//XXX slower than direct division
				//return std::ldexp(static_cast<double>(v>>LSB), -RSHIFT);
				return static_cast<double>(v>>LSB)/(1<<RSHIFT);
			}
			constexpr static UT from_float(double v) {
				auto l=std::lround(std::ldexp(v, RSHIFT));
				return static_cast<UT>(l)<<LSB;
			}
			constexpr static UT _sb_001=UT{1}<<LSB;
			constexpr static UT _sgn_bit=UT{1}<<MSB;
			constexpr static UT _sb_mask=_sgn_bit-_sb_001;
			constexpr static double _rt_max_f=to_float(_sb_mask);
			constexpr static double _inf=INFINITY;
			constexpr static double _nan=NAN;
		};


	template<typename T, unsigned int LSB, unsigned int MSB, int RSHIFT>
		inline constexpr double fixed_point<T, LSB, MSB, RSHIFT>::get(T host_) {
			auto host=static_cast<UT>(host_);
			auto sgn=host&_sgn_bit;
			if(sgn) {
				auto sb=(_sb_001-host)&_sb_mask;
				if(sb>_sb_001)
					return -to_float(sb-_sb_001);
				if(sb==0)
					return -_inf;
				return _nan;
			} else {
				auto sb=host&_sb_mask;
				if(sb<_sb_mask)
					return to_float(sb);
				return _inf;
			}
		}
	template<typename T, unsigned int LSB, unsigned int MSB, int RSHIFT>
		inline constexpr T fixed_point<T, LSB, MSB, RSHIFT>::enc(double value) {
			switch(std::fpclassify(value)) {
				case FP_INFINITE:
					if(std::signbit(value))
						return _sgn_bit|_sb_001;
					else
						return _sb_mask;
				case FP_NAN:
					return _sgn_bit;
				case FP_ZERO:
					return 0;
				default:
					break;
			}
			if(std::signbit(value)) {
				if(value<=-_rt_max_f)
					return _sgn_bit|_sb_001;
				return from_float(value)&(_sgn_bit|_sb_mask);
			} else {
				if(value>=_rt_max_f)
					return _sb_mask;
				return from_float(value);
			}
		}

}

#endif
