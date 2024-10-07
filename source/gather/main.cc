/* gather/main.cc
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

#include "server.hh"

#include "gapr/utility.hh"
#include "gapr/parser.hh"
#include "gapr/exception.hh"
#include "gapr/str-glue.hh"

#include <iostream>
#include <cassert>

#include <getopt.h>

#include "config.hh"

constexpr static const char* opts=":b:d:";
constexpr static const struct option opts_long[]={
	{"bind", 1, nullptr, 'b'},
	{"dir", 1, nullptr, 'd'},
	{"help", 0, nullptr, 1000+'h'},
	{"version", 0, nullptr, 1000+'v'},
	{nullptr, 0, nullptr, 0}
};

static void usage(const char* argv0) {
	std::cout<<
		"usage: "<<argv0<<" [-b <host:port>] [-d <dir>]\n"
		"       start the server\n\n"
		"   or: "<<argv0<<" --help|--version\n"
		"       display this help or version information, and exit\n\n"
		"Server for distributed tracing.\n\n"
		"Options:\n"
		"   -b, --bind <host:port> Bind to this address.\n"
		"   -d, --dir  <dir>     Directory for configuration files and tracing results.\n\n"
		PACKAGE_NAME " " PACKAGE_VERSION "\n"
		"Homepage: <" PACKAGE_URL ">\n";
}
static void version() {
	std::cout<<
		"gather (" PACKAGE_NAME ") " PACKAGE_VERSION "\n"
		PACKAGE_COPYRIGHT "\n"
		"License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n"
		"This is free software: you are free to change and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.\n";
}

int main(int argc, char* argv[]) {
	gapr::cli_helper cli_helper{};

	gapr::gather_server::Args args{};
	try {
		int opt;
		while((opt=getopt_long(argc, argv, opts, &opts_long[0], nullptr))!=-1) {
			switch(opt) {
				case 'b':
					{
						std::string_view str{optarg};
						auto coloni=str.rfind(':');
						if(coloni==std::string::npos)
							throw gapr::reported_error{"wrong HOST:PORT"};
						if(0>=coloni)
							throw gapr::reported_error{"empty HOST"};
						if(coloni+1>=str.length())
							throw gapr::reported_error{"empty PORT"};
						args.host=str.substr(0, coloni);
						args.port=gapr::parse_port(&str[coloni+1], str.length()-coloni-1);
					}
					break;
				case 'd':
					args.cwd=optarg;
					break;
				case 1000+'h':
					usage(argv[0]);
					return EXIT_SUCCESS;
				case 1000+'v':
					version();
					return EXIT_SUCCESS;
				case '?':
					cli_helper.report_unknown_opt(argc, argv);
					break;
				case ':':
					cli_helper.report_missing_arg(argc, argv);
					break;
				default:
					cli_helper.report_unmatched_opt(argc, argv);
			}
		}
		if(optind<argc)
			throw gapr::reported_error{"too many arguments"};
	} catch(const gapr::reported_error& e) {
		gapr::str_glue msg{"error: ", e.what(), '\n',
			"try `", argv[0], " --help", "' for more information.\n"};
		std::cerr<<msg.str();
		return EXIT_FAILURE;
	}

	gapr::gather_server srv{std::move(args)};
	try {
		gapr::print("pre conf");
		srv.configure();
		gapr::print("post conf");
	} catch(const gapr::reported_error& e) {
		gapr::str_glue msg{"fatal: ", e.what(), '\n'};
		std::cerr<<msg.str();
		return EXIT_FAILURE;
	} catch(const gapr::CliErrorMsg& e) {
		gapr::CliErrorMsg msg{"error: ", e.message(), '\n'};
		std::cerr<<msg.message();
		return EXIT_FAILURE;
	}
	try {
		return srv.run();
	} catch(const std::runtime_error& e) {
		srv.emergency();
		gapr::str_glue msg{"fatal: ", e.what(), '\n'};
		std::cerr<<msg.str();
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

