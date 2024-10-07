/* convert/main.cc
 *
 * Copyright (C) 2022 GOU Lingfeng
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
#include "gapr/exception.hh"
#include "gapr/str-glue.hh"
#include "gapr/parser.hh"

#include <filesystem>
#include <iostream>
#include <cassert>
#include <fstream>
#include <thread>

#include <getopt.h>

#include "config.hh"

#include "savewebm.hh"
#include "loadtiff.hh"
#include "split.hh"

#include "../corelib/parse-helper.hh"
#include "logger.cc"


constexpr static const char* opts=":c:f:i:r:s:d:w:j::B:C::Y";
constexpr static const struct option opts_long[]={
	/*convert*/
	{"config", 1, nullptr, 'c'},
	{"file", 1, nullptr, 'f'},
	{"input-list", 1, nullptr, 'i'},
	{"resolution", 1, nullptr, 'r'},
	{"cube-size", 1, nullptr, 's'},
	{"downsample-factor", 1, nullptr, 'd'},
	{"downsample-ratio", 1, nullptr, 1000+'r'},
	{"workdir", 1, nullptr, 'w'},
	{"jobs", 2, nullptr, 'j'},
	{"buffer-size", 1, nullptr, 'B'},
	{"continue", 2, nullptr, 'C'},
	{"yes", 0, nullptr, 'Y'},
	/*! if no arg, same dir, delete original; otherwise, copy to target dir */
	{"tiled", 2, nullptr, 1000+'t'},
	/*codec*/
	{"cq-level", 1, nullptr, 7000+'q'},
	{"cpu-used", 1, nullptr, 7000+'c'},
	{"threads", 1, nullptr, 7000+'t'},
	{"passes", 1, nullptr, 7000+'p'},
	{"filter", 1, nullptr, 7000+'f'},
	/*visor-like*/
	{"plugin", 1, nullptr, 2000+'p'},
	{"plugin-args", 1, nullptr, 2000+'a'},
	/*misc*/
	{"compare", 0, nullptr, 1100+'c'},
	{"compress", 0, nullptr, 1000+'c'},
	{"to-tiled", 2, nullptr, 1300+'t'},
	{"hash", 0, nullptr, 1400+'h'},
	{"to-nrrd", 0, nullptr, 1500+'n'},
	{"help", 0, nullptr, 1000+'h'},
	{"version", 0, nullptr, 1000+'v'},
	{nullptr, 0, nullptr, 0}
};

static void usage(const char* argv0) {
	std::cout<<
		"usage: "<<argv0<<" <swc-file> ...\n"
		"       show summary statistics\n\n"
		"   or: "<<argv0<<" --help|--version\n"
		"       display this help or version information, and exit\n\n"
		"Show summary statistics.\n\n"
		"Options:\n\n"
		"   --sparsity <r>       Calculate sparsity histogram.\n"
		"                        Density estimated in spheres with\n"
		"                        radius <r>.\n\n"
		"   --f1score <ref>      Calculate F1 score with <ref>.\n"
		"Arguments:\n"
		"   <swc-file>           Input file.\n\n"
		PACKAGE_NAME " " PACKAGE_VERSION "\n"
		"Homepage: <" PACKAGE_URL ">\n";
}

static void version() {
	std::cout<<
		"brief (" PACKAGE_NAME ") " PACKAGE_VERSION "\n"
		PACKAGE_COPYRIGHT "\n"
		"License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n"
		"This is free software: you are free to change and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.\n";
}


static bool get_confirmation() {
	std::cerr<<"Continue? [Y/N]: ";
	std::string l;
	while(std::getline(std::cin, l)) {
		if(l[0]=='y' || l[0]=='Y')
			return true;
		if(l[0]=='n' || l[0]=='N')
			return false;
		std::cerr<<"Continue? [Y/N]: ";
	}
	return false;
}

int main(int argc, char* argv[]) {
	Logger::instance().logMessage(__FILE__, "Application started.");
	gapr::cli_helper cli_helper{};

	bool compress{false};
	bool conv_to_tiled{false};
	std::array<uint32_t, 2> tilesize{0, 0};
	bool conv_to_nrrd{false};
	bool calc_hash{false};
	bool compare_mode{false};
	bool resume{false};

	const char* input_file_list=nullptr;
	unsigned int ncpu=1;
	std::filesystem::path workdir{};
	std::optional<std::filesystem::path> tiled_dir{};
	double cachesizef=0;
	bool yes_set=false;
	SplitArgs args{};
	std::filesystem::path cfg_file{};
	ServerArgs srv{};
	cube_enc_opts enc_opts{};

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
				case 'c':
					cfg_file=optarg;
					break;
				case 1000+'c':
					compress=true;
					break;
				case 1100+'c':
					compare_mode=true;
					break;
				case 1300+'t':
					conv_to_tiled=true;
					if(optarg) {
						if(!gapr::parse_tuple(optarg, &tilesize[0], &tilesize[1]))
							throw gapr::reported_error("Please give 2 integers separated by ':'.\n");
					}
					break;
				case 1400+'h':
					calc_hash=true;
					break;
				case 1500+'n':
					conv_to_nrrd=true;
					break;
				case 'f':
					args.inputfiles.emplace_back(optarg);
					break;
				case 'i':
					input_file_list=optarg;
					break;
				case 'r':
					if(!gapr::parse_tuple(optarg, &args.xres, &args.yres, &args.zres))
						throw gapr::reported_error("Please give 3 floating-point numbers separated by ':'.\n");
					if(args.xres<=0 || args.yres<=0 || args.zres<=0)
						throw gapr::reported_error("Need positive numbers.\n");
					break;
				case 's':
					if(!gapr::parse_tuple(optarg, &args.xsize, &args.ysize, &args.zsize))
						throw gapr::reported_error("Please give 3 integers separated by ':'.\n");
					if(args.xsize<=0 || args.ysize<=0 || args.zsize<=0)
						throw gapr::reported_error("Need positive integers.\n");
					if(args.xsize%8!=0 || args.ysize%8!=0)
						throw gapr::reported_error("Width and height must be multiples of 8.\n");
					break;
				case 'd':
					if(!gapr::parse_tuple(optarg, &args.dsx, &args.dsy, &args.dsz))
						throw gapr::reported_error("Please give 3 integers separated by ':'.\n");
					if(args.dsx<=0 || args.dsy<=0 || args.dsz<=0)
						throw gapr::reported_error("Need positive integers.\n");
					break;
				case 1000+'r':
					if(!gapr::parse_tuple(optarg, &args.ds_ratio))
						throw gapr::reported_error("Please give 1 float.\n");
					if(args.ds_ratio<0 || args.ds_ratio>1)
						throw gapr::reported_error("ratio out of range.\n");
					break;
				case 1000+'t':
					tiled_dir.emplace(optarg?optarg:"");
					break;
				case 'w':
					workdir=optarg;
					break;
				case 'j':
					if(optarg) {
						if(!gapr::parse_tuple<unsigned int>(optarg, &ncpu))
							throw gapr::reported_error("Please give an integer.\n");
						if(ncpu<=0)
							throw gapr::reported_error("Need a positive integer.\n");
					} else {
						ncpu=0;
					}
					break;
				case 'B':
					if(!gapr::parse_tuple<double>(optarg, &cachesizef))
						throw gapr::reported_error("Please give a floating-point number.\n");
					if(cachesizef<=0)
						throw gapr::reported_error("Need a positive number.\n");
					break;
				case 'C':
					resume=true;
					workdir=optarg ? optarg : std::filesystem::current_path();
					break;
				case 'Y':
					yes_set=true;
					break;
				case 2000+'p':
					args.plugin=optarg;
					break;
				case 2000+'a':
					args.plugin_args.emplace_back(optarg);
					break;
				case 7000+'q':
					{
						unsigned int cq_level;
						if(!gapr::parse_tuple<unsigned int>(optarg, &cq_level))
							throw gapr::reported_error("Please give an integer.\n");
						enc_opts.cq_level=cq_level;
					}
					break;
				case 7000+'c':
					{
						int cpu_used;
						if(!gapr::parse_tuple<int>(optarg, &cpu_used))
							throw gapr::reported_error("Please give an integer.\n");
						enc_opts.cpu_used=cpu_used;
					}
					break;
				case 7000+'t':
					{
						unsigned int threads;
						if(!gapr::parse_tuple<unsigned int>(optarg, &threads))
							throw gapr::reported_error("Please give an integer.\n");
						enc_opts.threads=threads;
					}
					break;
				case 7000+'p':
					{
						unsigned int passes;
						if(!gapr::parse_tuple<unsigned int>(optarg, &passes))
							throw gapr::reported_error("Please give an integer.\n");
						enc_opts.passes=passes;
					}
					break;
				case 7000+'f':
					enc_opts.filter=optarg;
					break;
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
		//if(optind+1>argc)
			//throw gapr::reported_error{"argument <swc-file> missing"};
		if(compress) {
		} else if(conv_to_tiled) {
		} else if(conv_to_nrrd) {
		} else if(calc_hash) {
		} else if(compare_mode) {
		} else if(!resume) {
			if(args.xres<=0)
				throw gapr::reported_error("resolution not specified.\n");
			if(input_file_list) {
				std::ifstream fs{input_file_list};
				if(!fs)
					throw gapr::reported_error("Failed to open input file list.\n");
				std::string l;
				while(std::getline(fs, l))
					args.inputfiles.push_back(l);
				if(!fs.eof())
					throw gapr::reported_error("Failed to read line.\n");
			}
			if(args.inputfiles.empty())
				throw gapr::reported_error("No input files specified.\n");
			if(args.inputfiles[0]=="BLACK" || args.inputfiles[0]=="WHITE"
					|| args.inputfiles.back()=="BLACK" || args.inputfiles.back()=="WHITE")
				throw gapr::reported_error("The first and last slices must not be BLACK or WHITE.\n");
		}
		if(!calc_hash && !conv_to_tiled && !conv_to_nrrd && !compress && !compare_mode) {
			if(optind+1>argc)
				throw gapr::reported_error{"argument <repo> missing"};
			if(optind+1<argc)
				throw gapr::reported_error{"too many arguments"};
			gapr::parse_repo(argv[optind], srv.user, srv.host, srv.port, srv.group);
			load_configs(cfg_file, {
				host_port_cfg("client.server", srv.host, srv.port),
				string_cfg("client.user", srv.user),
				string_cfg("client.password", srv.passwd),
			});
			if(srv.host.empty())
				throw gapr::reported_error{"in <repo>: missing HOST"};
			if(srv.port==0)
				throw gapr::reported_error{"in <repo>: missing PORT"};
			if(srv.user.empty())
				throw gapr::reported_error{"in <repo>: missing USER"};
		}
	} catch(const gapr::reported_error& e) {
		gapr::str_glue msg{"error: ", e.what(), '\n',
			"try `", argv[0], " --help", "' for more information.\n"};
		std::cerr<<msg.str();
		return EXIT_FAILURE;
	}

	try {
		std::vector<char> buf;
		std::vector<char> zerobuf;
		if(compress) {
			convert_file(argv[optind], argv[optind+1], enc_opts, buf, zerobuf);
		} else if(conv_to_tiled) {
			tiff_to_tile(argv[optind], argv[optind+1], tilesize);
		} else if(conv_to_nrrd) {
			convert_to_nrrd(argv[optind], argv[optind+1]);
		} else if(calc_hash) {
			for(int i=optind; i<argc; ++i)
				tiff_to_hash(argv[i]);
		} else if(compare_mode) {
			compare(argv[optind], argv[optind+1]);
		} else {
			if(ncpu==0) {
				ncpu=std::thread::hardware_concurrency();
				if(ncpu<1)
					ncpu=1;
			}
			if(workdir.empty())
				workdir=std::filesystem::current_path();
			std::size_t cachesize=cachesizef*1024*1024*1024;
			if(!resume) {
				if(std::filesystem::exists(workdir/"state") || std::filesystem::exists(workdir/"downsample")) {
					std::cerr<<"overwrite existing states???\n";
					if(!get_confirmation())
						return 0;
				}
				prepare_conversion(args, workdir, ncpu, cachesize);
				if(!yes_set) {
					if(get_confirmation())
						resume=true;
				} else {
					resume=true;
				}
			}
			if(resume)
				resume_conversion(workdir, tiled_dir, ncpu, cachesize, std::move(srv), enc_opts);
		}
		return 0;
	} catch(const gapr::reported_error& e) {
		gapr::str_glue msg{"fatal: ", e.what(), '\n'};
		std::cerr<<msg.str();
		return EXIT_FAILURE;
	} catch(const gapr::CliErrorMsg& e) {
		gapr::CliErrorMsg msg{"error: ", e.message(), '\n'};
		std::cerr<<msg.message();
		return EXIT_FAILURE;
	}

}

