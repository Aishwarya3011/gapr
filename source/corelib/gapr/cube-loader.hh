/* cube-loader.hh
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

//@@@@
#ifndef _GAPR_INCLUDE_CUBE_LOADER_HH_
#define _GAPR_INCLUDE_CUBE_LOADER_HH_

#include "gapr/config.hh"

#include <array>
#include <memory>
#include <cassert>
#include <string_view>

namespace gapr {

	enum class cube_type: unsigned int;
	class Streambuf;

	class cube_loader {
		public:
			virtual ~cube_loader() { }

			cube_type type() const { assert(_valid); return _type; }
			const std::array<int32_t, 3>& sizes() const {
				assert(_valid);
				return _sizes;
			}

			void load(char* buf, int64_t ystride, int64_t zstride) {
				assert(_valid);
				do_load(buf, ystride, zstride);
				_valid=false;
			}

		protected:
			cube_loader(Streambuf& file): _file{file}, _valid{false} { }
			Streambuf& file() { return _file; }
			void set_info(cube_type type, const std::array<int32_t, 3>& sizes) {
				assert(!_valid);
				_valid=true;
				_type=type;
				_sizes=sizes;
			}
			virtual void do_load(char* buf, int64_t ystride, int64_t zstride) =0;

		private:
			Streambuf& _file;
			std::array<int32_t, 3> _sizes;
			cube_type _type;
			bool _valid;
	};

	GAPR_CORE_DECL std::unique_ptr<cube_loader> make_cube_loader(std::string_view type_hint, Streambuf& file);

}

#endif
