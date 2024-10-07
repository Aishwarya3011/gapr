/* fix-pr4m/main.cc
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


#include "gapr/utility.hh"
#include "gapr/parser.hh"
#include "gapr/str-glue.hh"
#include "gapr/exception.hh"
#include "gapr/gui/application.hh"

#include <optional>
#include <iostream>
#include <cassert>

#include <QTranslator>
#include <QCoreApplication>
#include <QLocale>
#include <QMainWindow>

#include <getopt.h>

#include "window.hh"

#include "config.hh"


constexpr static const char* opts=":c:d:";
constexpr static const struct option opts_long[]={
	{"config", 1, nullptr, 'c'},
	{"data", 1, nullptr, 'd'},
	{"help", 0, nullptr, 1000+'h'},
	{"version", 0, nullptr, 1000+'v'},
	{nullptr, 0, nullptr, 0}
};

static void usage(const char* argv0) {
	std::cout<<
		"usage: "<<argv0<<" [-c <cfg-file>] [-d <data>] <repo>\n"
		"       edit the repository\n\n"
		"   or: "<<argv0<<" --help|--version\n"
		"       display this help or version information, and exit\n\n"
		"Manually edit tracing results.\n\n"
		"Options:\n"
		"   -c, --config <cfg-file> Load extra configuration file.\n"
		"   -d, --data   <data>     Imaging data to use.\n\n"
		"Arguments:\n"
		"   <repo>               Upload to this repository.\n"
		"                        :format: [[user@]host:port/]repo-id\n\n"
		PACKAGE_NAME " " PACKAGE_VERSION "\n"
		"Homepage: <" PACKAGE_URL ">\n";
}
static void version() {
	std::cout<<
		"fix (" PACKAGE_NAME ") " PACKAGE_VERSION "\n"
		PACKAGE_COPYRIGHT "\n"
		"License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n"
		"This is free software: you are free to change and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.\n";
}

int main(int argc, char* argv[]) {
	gapr::cli_helper cli_helper{};

	std::optional<gapr::fix::Session::Args> args;
	std::string cfg_file;
	if(argc>1) try {
		int opt;
		args.emplace();
		while((opt=getopt_long(argc, argv, opts, &opts_long[0], nullptr))!=-1) {
			switch(opt) {
				case 'c':
					cfg_file=optarg;
					break;
				case 'd':
					// XXX
					args->data=optarg;
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
		if(optind+1>argc)
			throw gapr::reported_error{"argument <repo> missing"};
		if(optind+1<argc)
			throw gapr::reported_error{"too many arguments"};
		gapr::parse_repo(argv[optind], args->user, args->host, args->port, args->group);

		int cfg_type=0;
		do {
			//////////////////////////
			std::string fn;
			if(cfg_type) {
				// XXX
				fn="home/." PACKAGE_TARNAME "/config";
				if(!gapr::test_file('f', fn.c_str()))
					fn.clear();
			} else {
				fn=std::move(cfg_file);
			}
			if(fn.empty())
				continue;

			try {
				auto cfg=gapr::load_config(fn.c_str());
				if(args->host.empty() || args->port==0) {
					auto is=cfg.find("client.server");
					if(is!=cfg.end()) {
						auto i=is->second.rfind(':');
						if(i==std::string::npos)
							gapr::report("Failed to parse `client.server': ", is->second);
						if(i<=0)
							gapr::report("Failed to parse `client.server', empty HOST: ", is->second);
						if(i+1>=is->second.length())
							gapr::report("Failed to parse `client.server', empty PORT: ", is->second);
						args->host=is->second.substr(0, i);
						args->port=gapr::parse_port(&is->second[i+1], is->second.size()-i-1);
					}
				}
				if(args->user.empty()) {
					auto iu=cfg.find("client.user");
					if(iu!=cfg.end())
						args->user=std::move(iu->second);
				}
				if(args->passwd.empty()) {
					auto ip=cfg.find("client.password");
					if(ip!=cfg.end())
						args->passwd=std::move(ip->second);
				}
			} catch(const std::exception& e) {
				gapr::str_glue err{"in config file: ", e.what()};
				throw gapr::reported_error{err.str()};
			}
		} while(++cfg_type<2 &&
				(args->host.empty()||args->port==0||args->user.empty()||args->passwd.empty()));

		if(args->host.empty())
			throw gapr::reported_error{"in <repo>: missing HOST"};
		if(args->port==0)
			throw gapr::reported_error{"in <repo>: missing PORT"};
		if(args->user.empty())
			throw gapr::reported_error{"in <repo>: missing USER"};
		if(!args->data.empty() && !gapr::test_file('f', args->data.c_str()))
			throw gapr::reported_error{"non-existent <data>"};

	} catch(const gapr::reported_error& e) {
		gapr::str_glue msg{"error: ", e.what(), '\n',
			"try `", argv[0], " --help", "' for more information.\n"};
		std::cerr<<msg.str();
		return EXIT_FAILURE;
	}

	gapr::Application::Enabler app_;

	auto translator=new QTranslator{QCoreApplication::instance()};
	auto dir=gapr::datadir();
	auto tspath=QString::fromUtf8(dir.data(), dir.size());
	tspath+="/" PACKAGE_TARNAME "/translations";
	if(translator->load(QLocale{}, QString{"fix"}, QString{"."}, tspath))
		QCoreApplication::installTranslator(translator);
	//QCoreApplication::removeTranslator(translator);
	//delete translator;
	//Q_CLEANUP_RESOURCE(fix_pr4m);

	auto win=gapr::fix::Window::create(std::move(args));
	win->show();

	auto& app=gapr::app();
	return app.exec();
}

