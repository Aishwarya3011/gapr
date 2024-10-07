/* detail/ascii-tables.hh
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


#ifndef _GAPR_INCLUDE_ASCII_TABLES_HH_
#define _GAPR_INCLUDE_ASCII_TABLES_HH_

//#include <type_traits>
//#include <atomic>
#include <array>


namespace gapr {

		class AsciiTables {
			public:
				// XXX use name like??? [0-51] [0-61] [0-61]...
				static unsigned int b64_from_char(char c) {
					return _tables._from_char[static_cast<unsigned char>(c)][0];
				}
				static char b64_to_char(unsigned int v) {
					return _tables._b64_to_char[v];
				}

				static unsigned int hex_from_char(char c) {
					return _tables._from_char[static_cast<unsigned char>(c)][1];
				}
				static unsigned int type_from_char(char c) {
					return _tables._from_char[static_cast<unsigned char>(c)][2];
				}

				constexpr static unsigned int out_of_range() { return 255; }

			private:
				std::array<std::array<unsigned char, 4>, 256> _from_char;
				std::array<char, 64> _b64_to_char;

				AsciiTables();
				GAPR_CORE_DECL static AsciiTables _tables;
		};

}

#endif
