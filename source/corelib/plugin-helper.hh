/* gapr/plugin-helper.hh
 *
 * Copyright (C) 2017 GOU Lingfeng
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
#ifndef _GAPR_INCLUDE_PLUGIN_HELPER_HH_
#define _GAPR_INCLUDE_PLUGIN_HELPER_HH_

#include "gapr/version.hh"

#define _GAPR_HELPER_PASTE2(a, b) a ## b
#define _GAPR_HELPER_PASTE(a, b) _GAPR_HELPER_PASTE2(a, b)

#define GAPR_EXPORT_PLUGIN(cls) \
	extern "C" gapr::Plugin* \
	_GAPR_HELPER_PASTE(gapr_create_plugin_v, GAPR_API_VERSION)() { \
		return new cls{}; \
	}

#endif
