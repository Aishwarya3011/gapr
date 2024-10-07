#include "gapr/plugin-loader.hh"

#include "gapr/version.hh"
#include "gapr/utility.hh"
#include "gapr/plugin.hh"

#include <memory>
#include <unordered_map>
#include <algorithm>
#include <cassert>
#include <filesystem>

#ifdef _MSC_VER
#include "detail/dlfcn.h"
#else
#include <dlfcn.h>
#endif
#include <sys/stat.h>

#include "config.hh"

#define _GAPR_HELPER_TOSTRING2(x) #x
#define _GAPR_HELPER_TOSTRING(x) _GAPR_HELPER_TOSTRING2(x)
#define GAPR_PLUGIN_ENTRY ("gapr_create_plugin_v" _GAPR_HELPER_TOSTRING(GAPR_API_VERSION))


gapr::PluginLoader_PRIV* gapr::PluginLoader::_instance{nullptr};
int gapr::PluginLoader::_inst_refc{0};

namespace {

	void* open_so(const char* pth) {
		auto so=dlopen(pth, RTLD_LAZY);
		if(!so)
			gapr::report("dlopen(\"", pth, "\"): ", dlerror());
		return so;
	}
	void* resolve_entry_so(void* so, const char* sym) {
		auto func=dlsym(so, sym);
		if(!func)
			gapr::report("dlsym(\"", sym, "\"): ", dlerror());
		return func;
	}
	void close_so(void* so) {
		dlclose(so);
	}
	///
	////

	class Library {
		public:
		Library(): _so{nullptr}, _plugin{nullptr}, _path{} { }
		Library(const char* path): _so{nullptr}, _plugin{nullptr}, _path{path} { }

		Library(const Library&) =delete;
		Library& operator=(const Library&) =delete;
		Library(Library&& r): _so{r._so}, _plugin{r._plugin}, _path{std::move(r._path)} {
			r._so=nullptr;
			r._plugin=nullptr;
		}
		Library& operator=(Library&& r) {
			std::swap(_so, r._so);
			std::swap(_plugin, r._plugin);
			std::swap(_path, r._path);
			return *this;
		}

		~Library() {
			if(_plugin)
				delete _plugin;
			if(_so)
				close_so(_so);
		}

		gapr::Plugin* plugin() {
			if(!_path.empty())
				load();
			return _plugin;
		}

		private:
		void* _so;
		gapr::Plugin* _plugin;
		std::string _path;


		void load() {
			std::string pth{std::move(_path)};
			_so=open_so(pth.c_str());
			auto sym=reinterpret_cast<gapr::Plugin* (*)()>(resolve_entry_so(_so, GAPR_PLUGIN_ENTRY));
			_plugin=(*sym)();
		}
	};
}

struct gapr::PluginLoader_PRIV {
	bool probed{false};
	std::unordered_map<std::string, Library> libraries{};

	void probe() {
#if 0
		auto pth=get_moduledir();
		std::filesystem::directory_iterator dir{pth};
		if(dir==end(dir))
			gapr::report("opendir(\"", pth, "\"): ", strerror(errno));
		auto pthl=pth.length();

		errno=0;
#ifdef _WIN64
		std::string suffix{".dll"};
#else
		std::string suffix{".so"};
#endif
		auto l=suffix.length();
		for(auto& e: dir) {
			if(is_regular_file(e.status())) {
				std::string f{e.path().filename().string()};
				if(f.size()>l && std::strncmp(&f[f.size()-l], &suffix[0], l)==0) {
					std::string id{&f[0], f.size()-l};
					auto ins=libraries.insert(std::pair<std::string, Library>{id, Library{}});
					if(ins.second) {
						(pth+='/')+=f;
						ins.first->second=Library{pth.c_str()};
						pth.resize(pthl);
					}
				}
			}
		}
		if(errno) {
			gapr::report("readdir(): ", strerror(errno));
		}
		probed=true;
#endif
	}
	std::vector<std::string> list(const std::string& tag) {
		if(!probed)
			probe();
		auto l=tag.length();
		std::vector<std::string> ids;
		for(auto& p: libraries) {
			auto& id=p.first;
			auto ll=id.size();
			if(ll>l && std::strncmp(&id[ll-l], &tag[0], l)==0) {
				ids.push_back(id);
			}
		}
		std::sort(ids.begin(), ids.end());
		return ids;
	}
	Plugin* load(const std::string& id) {
		auto ins=libraries.insert(std::pair<std::string, Library>{id, Library{}});
		if(!ins.second)
			return ins.first->second.plugin();
		if(probed)
			return nullptr;

#ifdef _WIN64
		auto pth=std::string{gapr::libdir()}+"/" PACKAGE_TARNAME "/"+id+".dll";
#else
		auto pth=std::string{gapr::libdir()}+"/" PACKAGE_TARNAME "/"+id+".so";
#endif
		struct stat buf;
		if(stat(pth.c_str(), &buf)==-1)
			gapr::report("stat(\"", pth, "\"): ", strerror(errno));

			//if(S_ISREG(buf.st_mode)) {
		ins.first->second=Library{pth.c_str()};
			//}

		return ins.first->second.plugin();
	}

};

std::vector<std::string> gapr::PluginLoader::list(const std::string& tag) {
	return _ptr->list(tag);
}
gapr::Plugin* gapr::PluginLoader::load(const std::string& id) {
	return _ptr->load(id);
}
void gapr::PluginLoader::create_instance() {
	assert(!_instance && _inst_refc==1);
	_instance=new PluginLoader_PRIV{};
}
void gapr::PluginLoader::destroy_instance() {
	assert(_instance && _inst_refc==0);
	delete _instance;
}
#ifdef _MSC_VER
#include "detail/dlfcn.c"
#endif
