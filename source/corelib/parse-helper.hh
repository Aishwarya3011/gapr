#include "gapr/utility.hh"
#include <functional>

std::pair<const std::string, std::function<bool(std::string&&)>> host_port_cfg(const std::string& key, std::string& host, unsigned short& port) {
	return {key, [&host,&port](std::string&& val) ->bool {
		if(!val.empty()) {
			auto i=val.rfind(':');
			if(i==std::string::npos)
				gapr::report("Failed to parse `client.server': ", val);
			if(i<=0)
				gapr::report("Failed to parse `client.server', empty HOST: ", val);
			if(i+1>=val.length())
				gapr::report("Failed to parse `client.server', empty PORT: ", val);
			host=val.substr(0, i);
			port=gapr::parse_port(&val[i+1], val.size()-i-1);
		}
		return !host.empty() && port!=0;
	}};
}
std::pair<const std::string, std::function<bool(std::string&&)>> string_cfg(const std::string& key, std::string& str) {
	return {key, [&str](std::string&& val) ->bool {
		if(!val.empty()) {
			str=std::move(val);
		}
		return !str.empty();
	}};
}

static void load_configs(const std::filesystem::path& cfg_file, std::initializer_list<std::pair<const std::string, std::function<bool(std::string&&)>>> cfgs) {
	int cfg_type=0;
	do {
		std::filesystem::path fn;
		if(cfg_type) {
			continue;
			std::filesystem::path home{gapr::get_homedir()};
			fn=home/"." PACKAGE_TARNAME/"config";
			if(!is_regular_file(fn))
				fn.clear();
		} else {
			fn=std::move(cfg_file);
		}
		if(fn.empty())
			continue;

		bool finished{true};
		try {
			auto cfg=gapr::load_config(fn.c_str());
			for(auto& [key, cb]: cfgs) {
				if(!cb({})) {
					bool hit{false};
					auto it=cfg.find(key);
					if(it!=cfg.end())
						hit=cb(std::move(it->second));
					finished=finished&&hit;
				}
			}
		} catch(const std::exception& e) {
			gapr::str_glue err{"in config file: ", e.what()};
			throw gapr::reported_error{err.str()};
		}
		if(finished)
			break;
	} while(++cfg_type<2);
}
