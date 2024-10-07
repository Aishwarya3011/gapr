/* gapr/mt-safety.hh
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

//@@@@@
#ifndef _GAPR_INCLUDE_MT_SAFETY_HH_
#define _GAPR_INCLUDE_MT_SAFETY_HH_

#include <mutex>
#include <ctime>

namespace gapr {

	struct MT_SAFETY_LOCKS {
		GAPR_CORE_DECL static std::mutex tmbuf;
	};
	inline std::tm* localtime_mt(const std::time_t* time, std::tm* buf) {
		std::lock_guard{MT_SAFETY_LOCKS::tmbuf};
		*buf=*std::localtime(time);
		return buf;
	}
	inline std::tm* gmtime_mt(const std::time_t* time, std::tm* buf) {
		std::lock_guard{MT_SAFETY_LOCKS::tmbuf};
		*buf=*std::gmtime(time);
		return buf;
	}

}

#endif
