/* cube-loader.cc
 *
 * Copyright (C) 2018 GOU Lingfeng
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


#include "gapr/cube-loader.hh"

#include <cstring>
#include <cassert>

namespace gapr {
	std::unique_ptr<cube_loader> make_cube_loader_nrrd(Streambuf& file);
	std::unique_ptr<cube_loader> make_cube_loader_tiff(Streambuf& file);
	std::unique_ptr<cube_loader> make_cube_loader_webm(Streambuf& file);
	std::unique_ptr<cube_loader> make_cube_loader_hevc(Streambuf& file);
	std::unique_ptr<cube_loader> make_cube_loader_v3d(Streambuf& file, bool compressed);
}

std::unique_ptr<gapr::cube_loader> gapr::make_cube_loader(std::string_view type_hint, Streambuf& file) {
	const char* url=type_hint.data();
	auto l=type_hint.size();
#ifdef _MSC_VER
	auto& strncasecmp=::_strnicmp;
#endif
	if(l>5 && strncasecmp(url+l-5, ".nrrd", 5)==0) {
		return make_cube_loader_nrrd(file);
	} else if(l>5 && strncasecmp(url+l-5, ".tiff", 5)==0) {
		return make_cube_loader_tiff(file);
	} else if(l>4 && strncasecmp(url+l-4, ".tif", 4)==0) {
		return make_cube_loader_tiff(file);
	} else if(l>7 && strncasecmp(url+l-7, ".v3draw", 7)==0) {
		return make_cube_loader_v3d(file, false);
	} else if(l>7 && strncasecmp(url+l-7, ".v3dpbd", 7)==0) {
		return make_cube_loader_v3d(file, true);
#ifdef WITH_VP9
	} else if(l>5 && strncasecmp(url+l-5, ".webm", 5)==0) {
		return make_cube_loader_webm(file);
#endif
#ifdef WITH_HEVC
	} else if(l>5 && strncasecmp(url+l-5, ".hevc", 5)==0) {
		return make_cube_loader_hevc(file);
#endif
	}
	return nullptr;
}

