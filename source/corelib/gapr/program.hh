/* gapr/program.hh
 *
 * Copyright (C) 2017 GOU Lingfeng
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
#ifndef _GAPR_INCLUDE_PROGRAM_HH_
#define _GAPR_INCLUDE_PROGRAM_HH_


#include "gapr/plugin.hh"

namespace gapr {

	/*! command line options
	 * if 0<=val<256 || !name.empty(), it's named; otherwise, positional.
	 * the order of 2 options with diff. vals should not be used.
	 */
	struct CliOption {
		// XXX XXX enable multiple modes of invocation
		// each option has a mode mask (set to be allowed in that mode)
		int val;
		bool required;
		bool repeatable;
		std::string name;
		std::string title;
		std::string help;
		struct Arg {
			bool required;
			std::string name;
			std::string spec;
		} arg;
		CliOption(int v, bool req, bool rep, const char* n, const char* t, const char* h, bool arg_r, const char* arg_n, const char* arg_s):
			val{v}, required{req}, repeatable{rep},
			name{n}, title{t}, help{h},
			arg{arg_r, arg_n, arg_s} { }
		// e.g.
		// short opt: {'o', nullptr, ...}
		// long opt: {1024, "output", ...}
		// positional: {1025, nullptr, true, true, true, "INPUT", "FILE_IN", ...}
	};

	/*! for options handling
	 * as input, opt and arg
	 * as output, opt and err
	 */
	struct OptStr {
		int opt;
		std::string str;
		OptStr(int o, const char* s): opt{o}, str{s?s:""} { }
		OptStr(int o, std::string&& s) noexcept: opt{o}, str{std::move(s)} { }
	};

	class Program: public Plugin {
		public:
			~Program() override { }

			const std::vector<CliOption>& options() const { return _options; }

			/*! check and set options
			 * return errors
			 */
			virtual std::vector<gapr::OptStr> set_options(std::vector<gapr::OptStr>&& options) =0;

			virtual int run() =0;

		protected:
			void add_opt_bool(int val, const char* name, const char* title, const char* help) {
				_options.emplace_back(val, false, false, name?name:"", title?title:"", help?help:"", false, "", "");
			}
			void add_opt_generic(int val, bool required, bool repeatable, const char* name, const char* title, const char* help, bool arg_required, const char* arg_name, const char* arg_spec) {
				_options.emplace_back(val, required, repeatable, name?name:"", title?title:"", help?help:"", arg_required, arg_name?arg_name:"", arg_spec?arg_spec:"");
			}

			Program(const char* n): Plugin{n}, _options{} { }

		private:
			std::vector<CliOption> _options;
	};


	const std::string Program_TAG{"-pr4m"};


	void cli_usage(const char* name);
	bool cli_parse_options(gapr::Program* prog, const std::vector<CliOption>& cli_options, int argc, char* argv[]);
	void cli_version();
	void cli_help(const char* argv0, const char* name);
	void cli_refer_usage(const char* name, const char* id);
	void cli_refer_usage(const char* name);

}

#endif
