/* start/main.cc
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


#include "start-window.hh"

//#include "gapr/utility.hh"
#include "gapr/str-glue.hh"
#include "gapr/exception.hh"
#include "gapr/gui/application.hh"
#include "gapr/utility.hh"

#include <iostream>
#include <cassert>

#include <QTranslator>
#include <QCoreApplication>
#include <QLocale>

#include <getopt.h>

#include "config.hh"

constexpr static const char* opts=":";
constexpr static const struct option opts_long[]={
	{"help", 0, nullptr, 1000+'h'},
	{"version", 0, nullptr, 1000+'v'},
	{nullptr, 0, nullptr, 0}
};

static void usage(const char* argv0) {
	std::cout<<
		"usage: "<<argv0<<"\n"
		"       show the start window\n\n"
		"   or: "<<argv0<<" --help|--version\n"
		"       display this help or version information, and exit\n\n"
		"Show start window.\n\n"
		PACKAGE_NAME " " PACKAGE_VERSION "\n"
		"Homepage: <" PACKAGE_URL ">\n";
}
static void version() {
	std::cout<<
		"" PACKAGE_NAME " " PACKAGE_VERSION "\n"
		PACKAGE_COPYRIGHT "\n"
		"License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n"
		"This is free software: you are free to change and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.\n";
}

int main(int argc, char* argv[]) {
	gapr::cli_helper cli_helper{};

	try {
		int opt;
		while((opt=getopt_long(argc, argv, opts, &opts_long[0], nullptr))!=-1) {
			switch(opt) {
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

	try {

		gapr::Application::Enabler app_;

		auto translator=new QTranslator{QCoreApplication::instance()};
		auto dir=gapr::datadir();
		auto tspath=QString::fromUtf8(dir.data(), dir.size());
		tspath+="/" PACKAGE_TARNAME "/translations";
		if(translator->load(QLocale{}, QString{"start"}, QString{"."}, tspath))
			QCoreApplication::installTranslator(translator);
		//QCoreApplication::removeTranslator(translator);
		//delete translator;
		//Q_CLEANUP_RESOURCE(fix_pr4m);

		auto win=new gapr::start_window{};
		win->show();

		auto& app=gapr::app();
		return app.exec();
		//return EXIT_SUCCESS;

	} catch(const gapr::reported_error& e) {
		gapr::str_glue msg{"fatal: ", e.what(), '\n'};
		std::cerr<<msg.str();
		return EXIT_FAILURE;
	}

}

#ifdef _MSC_VER
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
	int argc;
	char* argv[]={nullptr};
	return main(argc, argv);
}
#endif
