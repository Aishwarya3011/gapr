/* core/program.cc
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


#include "gapr/program.hh"

#include "gapr/utility.hh"
#include "gapr/plugin-loader.hh"

#include <iostream>
#include <unordered_set>

#include <getopt.h>

#include "config.hh"


static inline std::string formatOption(const gapr::CliOption& opt) {
	std::string fmt{};
	if(opt.val>=0 && opt.val<256) {
		// short opt
		fmt+='-'; fmt+=static_cast<char>(opt.val);
		if(!opt.name.empty()) {
			// and long
			fmt+=", --"; fmt+=opt.name;
			if(!opt.arg.name.empty()) {
				// with arg
				if(opt.arg.required) {
					fmt+=' '; fmt+=opt.arg.name;
				} else {
					fmt+="[="; fmt+=opt.arg.name; fmt+=']';
				}
			}
			if(opt.repeatable)
				fmt+=" ...";
		} else {
			if(!opt.arg.name.empty()) {
				if(opt.arg.required) {
					fmt+=' '; fmt+=opt.arg.name;
				} else {
					fmt+=" ["; fmt+=opt.arg.name; fmt+=']';
				}
			}
			if(opt.repeatable)
				fmt+=" ...";
		}
	} else {
		if(!opt.name.empty()) {
			// long opt
			fmt+="--"; fmt+=opt.name;
			if(!opt.arg.name.empty()) {
				if(opt.arg.required) {
					fmt+=' '; fmt+=opt.arg.name;
				} else {
					fmt+="[="; fmt+=opt.arg.name; fmt+=']';
				}
			}
			if(opt.repeatable)
				fmt+=" ...";
		} else {
			// positional
			if(!opt.arg.name.empty()) {
				if(opt.required) {
					fmt+=opt.arg.name;
					if(opt.repeatable)
						fmt+="...";
				} else {
					fmt+='['; fmt+=opt.arg.name;
					if(opt.repeatable)
						fmt+="...]";
					else
						fmt+=']';
				}
			} else {
				gapr::report("invalid option");
			}
		}
	}
	return fmt;
}
static inline std::string formatOptionShort(const gapr::CliOption& opt) {
	std::string fmt{};
	if(opt.val>=0 && opt.val<256) {
		// short opt
		fmt+='-'; fmt+=static_cast<char>(opt.val);
		if(!opt.arg.name.empty()) {
			if(opt.arg.required) {
				fmt+=opt.arg.name;
			} else {
				fmt+='['; fmt+=opt.arg.name; fmt+=']';
			}
		}
		if(opt.repeatable)
			fmt+="...";
	} else {
		if(!opt.name.empty()) {
			fmt+="--"; fmt+=opt.name;
			if(!opt.arg.name.empty()) {
				if(opt.arg.required) {
					fmt+='='; fmt+=opt.arg.name;
				} else {
					fmt+="[="; fmt+=opt.arg.name; fmt+=']';
				}
			}
			if(opt.repeatable)
				fmt+=" ...";
		} else {
			// positional
			if(!opt.arg.name.empty()) {
				if(opt.required) {
					fmt+=opt.arg.name;
					if(opt.repeatable)
						fmt+="...";
				} else {
					fmt+='['; fmt+=opt.arg.name;
					if(opt.repeatable)
						fmt+="...]";
					else
						fmt+=']';
				}
			} else {
				gapr::report("invalid option");
			}
		}
	}
	return fmt;
}

bool gapr::cli_parse_options(gapr::Program* prog, const std::vector<gapr::CliOption>& cli_options, int argc, char* argv[]) {
#if 0
	//int minarg=0, maxarg=0; // XXX needed?
	std::unordered_set<int> required_opts{};

	std::vector<char> opts{':'};
	std::vector<struct option> opts_long;
	for(auto& opt: cli_options) {
		if(opt.val>=0 && opt.val<256) {
			opts.push_back(opt.val);
			if(!opt.arg.name.empty()) {
				opts.push_back(':');
				if(!opt.arg.required)
					opts.push_back(':');
			}
		}
		if(!opt.name.empty()) {
			opts_long.push_back(option{opt.name.c_str(), opt.arg.name.empty()?0:(opt.arg.required?1:2), nullptr, opt.val});
		}
		if(opt.required) {
			required_opts.insert(opt.val);
		}
	}
	opts.push_back('\x00');
	opts_long.push_back(option{nullptr, 0, nullptr, 0});


	for(auto& opt: cli_options) {
		if(optind>=argc)
			break;
		if((opt.val<0 || opt.val>=256) && opt.name.empty()) {
			auto i=required_opts.find(opt.val);
			if(i!=required_opts.end())
				required_opts.erase(i);
			if(opt.repeatable) {
				do {
					options.emplace_back(opt.val, argv[optind++]);
				} while(optind<argc);
				break;
			} else {
				options.emplace_back(opt.val, argv[optind++]);
			}
		}
	}

	for(auto& opt: cli_options) {
		auto i=required_opts.find(opt.val);
		if(i!=required_opts.end()) {
			errors.emplace_back(opt.val, "required, but missing");
		}
	}

	for(int i=optind; i<argc; i++) {
		std::string err{"extra argument `"};
		err+=argv[i];
		err+='\'';
		errors.emplace_back(-1, std::move(err));
	}

	if(errors.empty()) {
		errors=prog->set_options(std::move(options));
		if(errors.empty())
			return true;
	}

	for(auto& e: errors) {
		std::string msg{"error: "};
		const CliOption* popt{nullptr};
		for(auto& opt: cli_options) {
			if(opt.val==e.opt) {
				popt=&opt;
				break;
			}
		}

		if(popt) {
			if(popt->val>=0 && popt->val<256) {
				msg+="option `-"; msg+=static_cast<char>(popt->val);
				if(!popt->name.empty()) {
					msg+="', `--"; msg+=popt->name;
				}
			} else {
				if(!popt->name.empty()) {
					msg+="option `--"; msg+=popt->name;
				} else {
					msg+="argument `"; msg+=popt->arg.name;
				}
			}
			msg+="': ";
		}
		msg+=e.str;
		msg+='\n';
		std::cerr<<msg;
	}
	// XXX
#if 0
	if(2!=-1 && argc-optind>maxarg) {
		std::cerr<<"Too many arguments.\n";
		return false;
	}
#endif
#endif
	return false;
}

void gapr::cli_version() {
	std::cout<<
		PACKAGE_NAME " " PACKAGE_VERSION "\n"
		PACKAGE_COPYRIGHT "\n"
		"License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n"
		"This is free software: you are free to change and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.\n";
}

#define OPT_INDENT 26
static void indent(std::string& str, bool* first) {
	if(*first && str.length()<OPT_INDENT) {
		str.resize(OPT_INDENT, ' ');
	} else {
		str+='\n'; str.append(OPT_INDENT, ' ');
	}
	if(*first)
		*first=false;
}
static void append_help(std::string& str, const std::string& help) {
	bool first{true};
	size_t pnl=0;
	auto nl=help.find('\n', pnl);
	while(nl!=std::string::npos) {
		indent(str, &first);
		str.append(&help[pnl], nl-pnl);
		pnl=nl+1;
		nl=help.find('\n', pnl);
	}
	if(pnl<help.size()) {
		indent(str, &first);
		str.append(&help[pnl], help.size()-pnl);
	}
	str+='\n';
}
static void print_usage(const char* launcher, const char* id, const gapr::Program* prog) {
	std::vector<const gapr::CliOption*> opts;
	std::vector<const gapr::CliOption*> opts_req;
	std::vector<const gapr::CliOption*> args;
	for(auto& opt: prog->options()) {
		if((opt.val>=0 && opt.val<256) || !opt.name.empty()) {
			opts.push_back(&opt);
			if(opt.required)
				opts_req.push_back(&opt);
		} else {
			args.push_back(&opt);
		}
	}

	{
		std::string head{"usage: "};
		head+=launcher; head+=' '; head+=id;
		if(opts.size()>opts_req.size())
			head+=" [<options>]";
		for(auto popt: opts_req) {
			head+=' '; head+=formatOptionShort(*popt);
		}
		for(auto popt: args) {
			head+=' '; head+=formatOptionShort(*popt);
		}
		head+="\n\n"; head+=prog->brief(); head+='\n';
		std::cout<<head;
	}

	if(!opts.empty()) {
		std::cout<<"\nOptions:\n";
		for(auto popt: opts) {
			std::string str{"    "};
			str+=formatOption(*popt);
			append_help(str, popt->help);
			std::cout<<str;
		}
	}

	if(!args.empty()) {
		std::cout<<"\nArguments:\n";
		for(auto popt: args) {
			std::string str{"    "};
			str+=popt->arg.name;
			append_help(str, popt->help);
			std::cout<<str;
		}
	}

	std::cout<<"\n" PACKAGE_NAME " " PACKAGE_VERSION "\n"
		      "Homepage: <" PACKAGE_URL ">\n";
}

void gapr::cli_usage(const char* name) {
	std::cout<<
		"usage: "<<name<<" <program> [<program-arguments>]\n"
		"       to invoke a program\n\n"
		"   or: "<<name<<" --list\n"
		"       to list all available programs\n\n"
		"   or: "<<name<<" --help\n"
		"       to print this help\n\n"
		"   or: "<<name<<" --help <program>\n"
		"   or: "<<name<<" <program> --help\n"
		"       to print program specific usage\n\n"
		"   or: "<<name<<" --version\n"
		"       to display version information\n\n"
		PACKAGE_NAME " home page: <" PACKAGE_URL ">\n";
}

void gapr::cli_help(const char* argv0, const char* name) {
	gapr::PluginLoader::Enabler _loader;
	auto loader=gapr::PluginLoader::instance();
	gapr::Program* factory{nullptr};
	try {
		auto plugin=loader.load(name+gapr::Program_TAG);
		if(!plugin)
			return;
		factory=dynamic_cast<gapr::Program*>(plugin);
		if(!factory)
			gapr::report("wrong type");
	} catch(const std::exception& e) {
		gapr::report("Failed to load program: ", e.what());
		return;
	} catch(...) {
		gapr::report("Failed to load program: unknown error");
		return;
	}
	print_usage(argv0, name, factory);
}

void gapr::cli_refer_usage(const char* name, const char* id) {
	std::cerr<<"Try '"<<name<<" --help "<<id<<"' for more information.\n";
}
void gapr::cli_refer_usage(const char* name) {
	std::cerr<<"Try '"<<name<<" --help' for more information.\n";
}

