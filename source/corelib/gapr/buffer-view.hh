/* gapr/buffer-view.hh
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

//@@@@
#ifndef _GAPR_INCLUDE_BUFFER_VIEW_HH_
#define _GAPR_INCLUDE_BUFFER_VIEW_HH_


#include <string_view>
#include <cstddef>

namespace gapr {

	class buffer_view {
		public:
			//default ctor?
			constexpr buffer_view(void* p, std::size_t n) noexcept:
				_ptr{static_cast<char*>(p)}, _cnt{n} { }
			char* data() const noexcept { return _ptr; }
			std::size_t size() const noexcept { return _cnt; }
			void skip(std::size_t n) noexcept { _ptr+=n; _cnt-=n; }

			operator std::string_view() const noexcept { return {_ptr, _cnt}; }
			std::pair<char*, std::size_t> pair() const noexcept { return {_ptr, _cnt}; }

		private:
			char* _ptr;
			std::size_t _cnt;
	};

}

#endif
