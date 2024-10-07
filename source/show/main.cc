/* show/main.cc
 *
 * Copyright (C) 2020 GOU Lingfeng
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

#include "window.hh"

#include "gapr/utility.hh"
#include "gapr/str-glue.hh"
#include "gapr/exception.hh"
#include "gapr/fixes.hh"

#include <iostream>
#include <vector>
#include <cassert>
#include <thread>

#include <glib/gi18n.h>
#include <getopt.h>

#include "config.hh"

constexpr static const char* opts=":d:i:ps:m:";
constexpr static const struct option opts_long[]={
	{"diff", 1, nullptr, 'd'},
	{"image", 1, nullptr, 'i'},
	{"mesh", 1, nullptr, 'm'},
	{"playback", 0, nullptr, 'p'},
	{"script", 1, nullptr, 's'},
	{"help", 0, nullptr, 1000+'h'},
	{"version", 0, nullptr, 1000+'v'},
	{nullptr, 0, nullptr, 0}
};

static void usage(const char* argv0) {
	std::cout<<
		"usage: "<<argv0<<" [-d <swc0>]... <swc>...\n"
		"       visualize SWC files\n\n"
		"   or: "<<argv0<<" { -p | --playback } <repo-file>\n"
		"       play back reconstruction history\n\n"
		"   or: "<<argv0<<" --help|--version\n"
		"       display this help or version information, and exit\n\n"
		"Visualize reconstruction.\n\n"
		"Options:\n"
		"   -i, --image <cat>    Load image data from <cat>.\n"
		"   -d, --diff <swc0>    Compare against <swc0>.\n"
		"   -p, --playback       Enter playback mode.\n"
		"   -s, --script <scr>   Run Lua script <scr>.\n\n"
		"Arguments:\n"
		"   <swc>                SWC files to show.\n"
		"   <repo-file>          Repository file to play back.\n\n"
		PACKAGE_NAME " " PACKAGE_VERSION "\n"
		"Homepage: <" PACKAGE_URL ">\n";
}

static void version() {
	std::cout<<
		"show (" PACKAGE_NAME ") " PACKAGE_VERSION "\n"
		PACKAGE_COPYRIGHT "\n"
		"License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n"
		"This is free software: you are free to change and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.\n";
}

static void show_inspector(GSimpleAction* act, GVariant* par, gpointer udata) {
	gtk_window_set_interactive_debugging(true);
}
static void quit_app(GSimpleAction* act, GVariant* par, gpointer udata) {
	auto app=static_cast<GtkApplication*>(udata);
	auto list=gtk_application_get_windows(app);
	while(list) {
		auto win=static_cast<GtkWindow*>(list->data);
		list=list->next;
		gtk_window_close(win);
	}
}

static void on_startup(GtkApplication* app, gpointer udata) {
	gapr::print("startup");

	std::initializer_list<GActionEntry> actions={
		{"inspector", show_inspector, nullptr, nullptr, nullptr},
		{"quit", quit_app, nullptr, nullptr, nullptr},
	};
	g_action_map_add_action_entries(G_ACTION_MAP(app), actions.begin(), actions.size(), app);

	struct AccelPair {
		const char* action;
		std::initializer_list<const char*> accels;
	};
	std::initializer_list<AccelPair> app_accels={
		{"app.inspector", {"<Primary>i", nullptr}},
		{"app.show-about-dialog", {"F1", nullptr}},
		{"win.close", {"<Primary>w", nullptr}},
		{"app.quit", {"<Primary>q", nullptr}},
	};
	for(auto [act, accels]: app_accels)
		gtk_application_set_accels_for_action(app, act, accels.begin());

	gtk::ref css_provider=gtk_css_provider_new();
	gtk_css_provider_load_from_resource(css_provider, APPLICATION_PATH "/style.css");
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

int on_command_line(GtkApplication* app, GApplicationCommandLine* cli, gapr::Context* ctx) {
	int argc;
	gtk::ref argv=g_application_command_line_get_arguments(cli, &argc);
	gapr::print("on_cli: ", argc, ' ', argv[0]);

	gapr::show::Session::Args args;
	try {
		int opt;
		while((opt=getopt_long(argc, argv, opts, &opts_long[0], nullptr))!=-1) {
			switch(opt) {
				case 'd':
					if(std::string s{optarg}; !s.empty()) {
						args.swc_files_cmp.emplace_back(std::move(s));
						break;
					}
					throw gapr::reported_error{"filename cannot be empty"};
				case 'i':
					if(std::string url{optarg}; !url.empty()) {
						if(auto u=gapr::to_url_if_path(url); !u.empty())
							url=std::move(u);
						args.image_url=std::move(url);
						break;
					}
					throw gapr::reported_error{"image url empty"};
				case 'm':
					args.mesh_path=optarg;
					break;
				case 'p':
					args.playback=true;
					break;
				case 's':
					if(std::string s{optarg}; !s.empty()) {
						args.script_file=std::move(s);
						break;
					}
					throw gapr::reported_error{"script file empty"};
				case 1000+'h':
					usage(argv[0]);
					return EXIT_SUCCESS;
				case 1000+'v':
					version();
					return EXIT_SUCCESS;
				case '?':
					{
						gapr::str_glue err{"unrecognized option `", argv[optind-1], '\''};
						throw gapr::reported_error{err.str()};
					}
					break;
				case ':':
					{
						gapr::str_glue err{"option `", argv[optind-1], "' requires an argument"};
						throw gapr::reported_error{err.str()};
					}
					break;
				default:
					assert(0);
					throw gapr::reported_error{"unknown option"};
			}
		}
		if(args.playback) {
			if(optind+1>argc)
				throw gapr::reported_error{"argument <repo-file> missing"};
			if(optind+1<argc)
				throw gapr::reported_error{"too many arguments"};
			if(!gapr::test_file('f', argv[optind]))
				throw gapr::reported_error{"non-existent <repo-file>"};
			args.repo_file=std::string{argv[optind]};
			if(args.repo_file.empty())
				throw gapr::reported_error{"filename is empty"};
		} else {
		if(optind+1>argc)
			throw gapr::reported_error{"argument <swc> missing"};
		for(auto i=optind; i<argc; i++) {
			auto s=std::filesystem::u8path(argv[i]);
			if(!is_regular_file(s))
				throw gapr::reported_error{"non-existent <swc> file"};
			if(s.empty())
				throw gapr::reported_error{"filename cannot be empty"};
			args.swc_files.emplace_back(std::move(s));
		}
		}
	} catch(const gapr::reported_error& e) {
		gapr::str_glue msg{"error: ", e.what(), '\n',
			"try `", argv[0], " --help", "' for more information.\n"};
		std::cerr<<msg.str();
		return EXIT_FAILURE;
	}

	auto ses=std::make_shared<gapr::show::Session>(*ctx, std::move(args));
	gtk_window_present(ses->create_window(app));

	return 0;
}

int main(int argc, char* argv[]) {
	gapr::fix_console();
	gapr::fix_relocate();

	setlocale(LC_ALL, "");
	bindtextdomain(GETTEXT_PACKAGE, gapr::localedir().data());
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);

	/* XXX
	 * * always unique,
	 * * always non-unique,
	 * * or a toggle?
	 */
	gtk::ref app=gtk_application_new(APPLICATION_ID, gtk::flags(G_APPLICATION_HANDLES_COMMAND_LINE, G_APPLICATION_NON_UNIQUE));
	auto ctx=std::make_shared<gapr::Context>(G_APPLICATION(app));
	g_signal_connect(app, "command-line", G_CALLBACK(on_command_line), ctx.get());
	g_signal_connect(app, "startup", G_CALLBACK(on_startup), ctx.get());
	auto ret=g_application_run(G_APPLICATION(app), argc, argv);
	ctx->join();
	return ret;
}

#ifdef _MSC_VER
#include <shellapi.h>
int wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
	int argc;
	auto argv=CommandLineToArgvW(GetCommandLineW(), &argc);
	return main(argc, (char**)argv);
}
#endif
