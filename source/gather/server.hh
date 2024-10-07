/* gather/server.hh
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


#ifndef _GAPR_GATHER_SERVER_HH__
#define _GAPR_GATHER_SERVER_HH__

#include <string>
#include <memory>


namespace gapr {

	class gather_server {
		public:
			struct Args {
				std::string cwd{};
				std::string host{};
				unsigned short port{0};
			};
			explicit gather_server(Args&& args);
			~gather_server();
			gather_server(const gather_server&) =delete;
			gather_server& operator=(const gather_server&) =delete;

			void configure();
			int run();
			void emergency();

		private:
			Args _args;
			struct PRIV;
			std::shared_ptr<PRIV> _ptr;
	};

}

#endif
