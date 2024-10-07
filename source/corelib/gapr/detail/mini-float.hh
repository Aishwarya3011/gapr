/* detail/mini-float.hh
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


#ifndef _GAPR_INCLUDE_DETAIL_MINI_FLOAT_HH_
#define _GAPR_INCLUDE_DETAIL_MINI_FLOAT_HH_


#include <type_traits>
#include <cmath>

// XXX enable mf with no sign (always positive)

namespace gapr {

	namespace MiniFloat_PRIV {

		template<typename T, int LSB, int NFRAC, int NEXP, int BIAS>
			struct mf {
				using UT=std::make_unsigned_t<T>;
				constexpr static double get(T host) {
					auto uhost=static_cast<UT>(host);
					auto e=(uhost-e_01)&e_mask;
					if(e<e_FE) {
						auto sgn=uhost&sgn_mask;
						auto sb=((uhost&sb_mask)|e_01)>>LSB;
						auto sbf=static_cast<double>(sb);
						if(sgn)
							sbf=-sbf;
						return std::ldexp(sbf, static_cast<int>(e>>(LSB+NFRAC))-(vv_126+NFRAC));
					}
					if(e>e_FE) {
						auto sgn=uhost&sgn_mask;
						auto sb=(uhost&sb_mask)>>LSB;
						return sb*(sgn?exp2_126_neg:exp2_126);
					}
					auto sb=uhost&sb_mask;
					if(sb)
						return nan;
					return (uhost&sgn_mask)?inf_neg:inf;
				}

				constexpr static T enc(double value) {
					UT ret;
					switch(std::fpclassify(value)) {
						case FP_INFINITE:
							if(std::signbit(value))
								ret=e_mask|sgn_mask;
							else
								ret=e_mask;
							break;
						case FP_NAN:
							ret=e_mask|(UT{1}<<LSB);
							break;
						case FP_ZERO:
							if(std::signbit(value))
								ret=sgn_mask;
							else
								ret=0;
							break;
						default:
							{
								int e0{0};
								auto v1=std::frexp(value, &e0);
								if(e0<(1-vv_126)) {
									auto l=std::lround(std::ldexp(v1, e0+(vv_126+NFRAC)));
									if(std::signbit(value))
										ret=(static_cast<UT>(-l)<<LSB)|sgn_mask;
									else
										ret=static_cast<UT>(l)<<LSB;
								} else if(e0<=(int(e_FE>>(NFRAC+LSB))-vv_126)) {
									auto l=std::lround(std::ldexp(v1, 1+NFRAC));
									UT sb;
									if(std::signbit(value)) {
										sb=static_cast<UT>(-l)<<LSB;
										ret=(sb&sb_mask)|sgn_mask;
									} else {
										sb=static_cast<UT>(l)<<LSB;
										ret=(sb&sb_mask);
									}
									UT e;
									if(sb&(e_mask<<1))
										e=static_cast<UT>(e0+(vv_126+1))<<(LSB+NFRAC);
									else
										e=static_cast<UT>(e0+vv_126)<<(LSB+NFRAC);
									ret|=e;
								} else {
									if(std::signbit(value))
										ret=e_mask|sgn_mask;
									else
										ret=e_mask;
								}
							}
					}
					return static_cast<T>(ret);
				}
				constexpr static void set(T& host, double value) {
					host=(host&(~(e_mask|sb_mask|sgn_mask)))|enc(value);
				}
				constexpr static UT e_01=UT{1}<<(LSB+NFRAC);
				constexpr static UT e_mask=((~UT{0})<<(sizeof(UT)*8-NEXP))>>(sizeof(UT)*8-NEXP-LSB-NFRAC);
				constexpr static UT e_FE=((~UT{0})&e_mask)-e_01;
				constexpr static UT sgn_mask=UT{1}<<(LSB+NFRAC+NEXP);
				constexpr static UT sb_mask=((~UT{0})<<(sizeof(UT)*8-NFRAC))>>(sizeof(UT)*8-NFRAC-LSB);
				constexpr static int vv_126=(int{1}<<(NEXP-1))-2-BIAS;
				inline const static double exp2_126=1.0/std::exp2(vv_126+NFRAC);
				inline const static double exp2_126_neg=-exp2_126;
				constexpr static double inf=INFINITY;
				constexpr static double inf_neg=-inf;
				constexpr static double nan=NAN;
			};
	}

	template<typename T, int LSB, int MSB, int NFRAC, int BIAS=0>
		struct MiniFloat {
			static_assert(std::is_integral<T>::value);
			static_assert(MSB<sizeof(T)*8);
			static_assert(MSB>LSB+NFRAC);
			static_assert(0<=LSB);
			static_assert(NFRAC>0);

			constexpr static double get(T host) {
				return MiniFloat_PRIV::mf<T, LSB, NFRAC, NEXP, BIAS>::get(host);
			}
			constexpr static T enc(double value) {
				return MiniFloat_PRIV::mf<T, LSB, NFRAC, NEXP, BIAS>::enc(value);
			}
			constexpr static void set(T& host, double value) {
				return MiniFloat_PRIV::mf<T, LSB, NFRAC, NEXP, BIAS>::set(host, value);
			}
			private:
			constexpr static int NEXP=MSB-LSB-NFRAC;
		};

	template<typename T, unsigned int LSB, unsigned int MSB, unsigned int NFRAC, int BIAS=0>
		struct mini_float_nn {
			constexpr static double get(T host);
			constexpr static T enc(double value);
			constexpr static void set(T& host, double value) {
				host=(host&(~(_e_mask|_sb_mask)))|enc(value);
			}

			private:
			static_assert(std::is_integral_v<T>);
			static_assert(MSB+1>LSB+NFRAC);
			static_assert(MSB<sizeof(T)*8);
			static_assert(NFRAC>0);
			using UT=std::make_unsigned_t<T>;
			constexpr static unsigned int NEXP=MSB-LSB+1-NFRAC;
			constexpr static UT _e_01=UT{1}<<(LSB+NFRAC);
			constexpr static UT _e_mask=((~UT{0})<<(sizeof(UT)*8-NEXP))>>(sizeof(UT)*8-NEXP-LSB-NFRAC);
			constexpr static UT _e_FE=((~UT{0})&_e_mask)-_e_01;
			constexpr static UT _sb_mask=((~UT{0})<<(sizeof(UT)*8-NFRAC))>>(sizeof(UT)*8-NFRAC-LSB);
			constexpr static int _vv_126=(int{1}<<(NEXP-1))-2-BIAS;
			inline const static double _exp2_126=1.0/std::exp2(_vv_126+NFRAC);
			constexpr static double _inf=INFINITY;
			constexpr static double _nan=NAN;
		};


	template<typename T, unsigned int LSB, unsigned int MSB, unsigned int NFRAC, int BIAS>
		constexpr double mini_float_nn<T, LSB, MSB, NFRAC, BIAS>::get(T host_) {
			auto host=static_cast<UT>(host_);
			auto e=(host-_e_01)&_e_mask;
			if(e<_e_FE) {
				auto sb=((host&_sb_mask)|_e_01)>>LSB;
				return std::ldexp(static_cast<double>(sb), static_cast<int>(e>>(LSB+NFRAC))-(_vv_126+NFRAC));
			}
			if(e>_e_FE) {
				auto sb=(host&_sb_mask)>>LSB;
				return sb*_exp2_126;
			}
			auto sb=host&_sb_mask;
			if(sb)
				return _nan;
			else
				return _inf;
		}
	template<typename T, unsigned int LSB, unsigned int MSB, unsigned int NFRAC, int BIAS>
		constexpr T mini_float_nn<T, LSB, MSB, NFRAC, BIAS>::enc(double value) {
			switch(std::fpclassify(value)) {
				case FP_INFINITE:
					return _e_mask;
				case FP_NAN:
					return _e_mask|(UT{1}<<LSB);
				case FP_ZERO:
					return 0;
				default:
					break;
			}
			int e0{0};
			auto v1=std::frexp(value, &e0);
			if(e0<(1-_vv_126)) {
				auto l=std::lround(std::ldexp(v1, e0+(_vv_126+NFRAC)));
				return static_cast<UT>(std::signbit(value)?-l:l)<<LSB;
			}
			if(e0<=(int(_e_FE>>(NFRAC+LSB))-_vv_126)) {
				auto l=std::lround(std::ldexp(v1, 1+NFRAC));
				auto sb=static_cast<UT>(std::signbit(value)?-l:l)<<LSB;
				sb=(sb&_sb_mask);
				UT e;
				if(sb&(_e_mask<<1))
					e=static_cast<UT>(e0+(_vv_126+1))<<(LSB+NFRAC);
				else
					e=static_cast<UT>(e0+_vv_126)<<(LSB+NFRAC);
				return sb|e;
			}
			return _e_mask;
		}

}

#endif
