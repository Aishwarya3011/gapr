/* gapr/timer.hh
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

//@@@
#ifndef _GAPR_INCLUDE_TIMER_HH_
#define _GAPR_INCLUDE_TIMER_HH_

//#include <ostream>

namespace gapr {

	template<std::size_t N>
	class timer {
		public:
			explicit constexpr timer(): _tp{now()}, _vals{} { }
			timer(const timer&) =delete;
			timer& operator=(const timer&) =delete;

			template<std::size_t I> void mark() {
				static_assert(I<N);
				auto t1=now();
				_vals[I]+=t1-_tp;
				_tp=t1;
			}
		private:
			using clock=std::chrono::steady_clock;
			clock::time_point _tp;
			std::array<clock::duration, N> _vals;
			static clock::time_point now() noexcept {
				return clock::now();
			}

			template<std::size_t N1> friend std::ostream&
				operator<<(std::ostream& str, const timer<N1>& tmr);
	};

	template<std::size_t N> inline std::ostream&
		operator<<(std::ostream& str, const timer<N>& tmr) {
			for(std::size_t i=0; i<N; i++) {
				if(i!=0)
					str<<' ';
				auto dur=std::chrono::duration_cast<std::chrono::microseconds>(tmr._vals[i]).count();
				str<<i<<':'<<dur<<"us";
			}
			return str;
		}

}

#endif
