/* gapr/config.hh
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
#ifndef _GAPR_INCLUDE_CONFIG_HH_
#define _GAPR_INCLUDE_CONFIG_HH_

#if !defined(_WIN32) && !defined(__CYGWIN__)
#define GAPR_CORE_DECL
#elif defined(GAPR_CORE_COMPILATION)
#define GAPR_CORE_DECL __declspec(dllexport)
#else
#define GAPR_CORE_DECL __declspec(dllimport)
#endif

#if !defined(_WIN32) && !defined(__CYGWIN__)
#define GAPR_GUI_DECL
#elif defined(GAPR_GUI_COMPILATION)
#define GAPR_GUI_DECL __declspec(dllexport)
#else
#define GAPR_GUI_DECL __declspec(dllimport)
#endif

#endif
