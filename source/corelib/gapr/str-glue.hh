/* gapr/str-glue.hh
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
#ifndef _GAPR_INCLUDE_STR_GLUE_HH_
#define _GAPR_INCLUDE_STR_GLUE_HH_

#include <sstream>

namespace gapr {

	class str_glue {
		public:
			explicit str_glue(): _ss{} { }
			// XXX deprecate
			explicit str_glue(std::nullptr_t, std::string_view sep): _sep{sep} { }
			template<typename T, typename... Args>
				explicit str_glue(T&& v, Args&&... args): str_glue{} {
					do_any(std::forward<T>(v), std::forward<Args>(args)...);
				}
			~str_glue() { }

			str_glue(const str_glue&) =delete;
			str_glue& operator=(const str_glue&) =delete;
			str_glue(str_glue&&) =delete;
			str_glue& operator=(str_glue&&) =delete;

			template<typename T, typename... Args>
			str_glue& operator()(T&& v, Args&&... args) {
				do_any(std::forward<T>(v), std::forward<Args>(args)...);
				return *this;
			}
			std::string str() const { return _ss.str(); }

		private:
			std::string_view _sep;
			std::ostringstream _ss;
			template<unsigned int I=0>
			void do_any() const noexcept { }
			template<unsigned int I=0, typename T, typename... Args>
				void do_any(T&& v, Args&&... args) {
					if(I>0)
						_ss<<_sep;
					_ss<<std::forward<T>(v);
					do_any<I+1>(std::forward<Args>(args)...);
				}
	};

}

#endif
