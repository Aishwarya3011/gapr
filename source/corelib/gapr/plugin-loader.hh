/* gapr/plugin-loader.hh
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
#ifndef _GAPR_INCLUDE_PLUGIN_LOADER_HH_
#define _GAPR_INCLUDE_PLUGIN_LOADER_HH_


#include <vector>
#include <string>

namespace gapr {

	class Plugin;

	struct PluginLoader_PRIV;

	/*! Singleton pattern
	 * Enabler, to manage instance creation and destruction
	 * Interface, instance API, grouped invocation
	 * Priv, implementation
	 */

	class PluginLoader {
		public:
			struct Enabler {
				explicit Enabler() { if(0==_inst_refc++) create_instance(); }
				~Enabler() noexcept { if(1==_inst_refc--) destroy_instance(); }
				Enabler(const Enabler&) =delete;
				Enabler& operator=(const Enabler&) =delete;
			};
			~PluginLoader() { }

			PluginLoader(const PluginLoader&) =delete;
			PluginLoader& operator=(const PluginLoader&) =delete;
			PluginLoader(PluginLoader&& r): _ptr{r._ptr} { r._ptr=nullptr; }
			PluginLoader& operator=(PluginLoader&& r) {
				std::swap(_ptr, r._ptr);
				return *this;
			}

			static PluginLoader instance() { return PluginLoader{_instance}; }

			std::vector<std::string> list(const std::string& tag);
			Plugin* load(const std::string& id);

		private:
			PluginLoader_PRIV* _ptr;
			PluginLoader(PluginLoader_PRIV* p): _ptr{p} { }

			static PluginLoader_PRIV* _instance;
			static int _inst_refc;
			static void create_instance();
			static void destroy_instance();
	};

}

#endif
