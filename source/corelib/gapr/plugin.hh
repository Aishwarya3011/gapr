/* gapr/plugin.hh
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
#ifndef _GAPR_INCLUDE_PLUGIN_HH_
#define _GAPR_INCLUDE_PLUGIN_HH_


#include <string>
#include <vector>

namespace gapr {

	class Plugin {
		public:
			struct Author {
				std::string name;
				std::string email;
				Author(const char* n, const char* e): name{n}, email{e} { }
			};
			virtual ~Plugin() { }

			const std::string& name() const { return _name; }
			const std::string& brief() const { return _brief; }
			const std::string& detail() const { return _detail; }
			int version() const { return _version; }
			const std::vector<Author>& authors() const { return _authors; }

		protected:
			Plugin(const char* n): _name{n}, _version{0}, _brief{}, _detail{}, _authors{} {}
			void version(int v) { _version=v; }
			void brief(const char* s) { _brief=s; }
			void detail(const char* s) { _detail=s; }
			void add_author(const char* n, const char* e) {
				_authors.emplace_back(n, e);
			}

		private:
			std::string _name;
			int _version;
			std::string _brief;
			std::string _detail;
			std::vector<Author> _authors;
	};

}

#endif
