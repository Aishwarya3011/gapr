/* map/main.cc
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

#include "map.hh"

#include "gapr/utility.hh"
#include "gapr/exception.hh"
#include "gapr/str-glue.hh"
#include "gapr/swc-helper.hh"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#ifdef __MINGW64__
#define DWORD boost::winapi::DWORD_
#define __kernel_entry
#endif

#include <boost/process/child.hpp>
#include <boost/process/io.hpp>
#include <boost/process/pipe.hpp>

#include <getopt.h>

#include "config.hh"

static std::filesystem::path output_path(const char* out_pat, const std::filesystem::path& pth) {
	std::ostringstream outfn{};
	std::filesystem::path pp{};
	for(auto p=out_pat; *p; ++p) {
		if(*p!='%') {
			outfn<<*p;
			continue;
		}
		++p;
		int upp{0};
		switch(*p) {
		case '%':
			outfn<<'%';
			break;
		case 'n': ++upp;
		case 'N': ++upp;
			pp=pth.filename();
			break;
		case 'p': ++upp;
		case 'P': ++upp;
			pp=pth.parent_path();
			break;
		case 's': ++upp;
		case 'S': ++upp;
			pp=pth.stem();
			break;
		case 'e': ++upp;
		case 'E': ++upp;
			pp=pth.extension();
			break;
		default:
			return {};
		}
		if(upp) {
			if(!pp.empty()) {
				outfn<<pp.u8string();
			} else if(upp==2) {
				throw gapr::reported_error{gapr::str_glue{"empty for `%", *p, "'"}.str()};
			}
		}
	}
	return std::filesystem::u8path(outfn.str());
}

namespace {
using Filter=std::function<int(std::vector<JobData>&)>;
}

static void map_impl(std::vector<JobData>&& jobs, const std::vector<Filter>& filters) {
	for(auto& j: jobs)
		j.read(j.fn.c_str());

	for(auto f: filters) {
		if(f(jobs)!=0)
			throw gapr::reported_error{"failed to run filter"};
	}

	auto write_swc=[](auto& nodes, const auto& roots, std::ostream& os) {
		gapr::swc_output swc{os};
		for(auto& n: nodes) {
			if(n.par_idx!=n.par_idx_null)
				n.par_id=nodes[n.par_idx].id;
			swc.node(n);
		}
		std::string ann{"root="};
		for(auto& [id, n]: roots) {
			ann.resize(5);
			ann.append(n);
			swc.annot(id, ann.c_str());
		}
	};

	for(auto& j: jobs) {
		if(j.fn_out.empty()) {
			write_swc(j.nodes, j.roots, std::cout);
		} else {
			std::ofstream fs{j.fn_out, std::ios_base::binary};
			if(!fs) {
				gapr::str_glue err{"cannot open file: ", j.fn_out};
				throw gapr::reported_error{err.str()};
			}
			boost::iostreams::filtering_ostream filter{};
			if(j.fn_out.extension()==".gz")
				filter.push(boost::iostreams::gzip_compressor{});
			filter.push(fs);
			write_swc(j.nodes, j.roots, filter);
			filter.reset();
			fs.close();
			if(!fs) {
				gapr::str_glue err{"failed to write file: ", j.fn_out};
				throw gapr::reported_error{err.str()};
			}
		}
	}
}

constexpr static const char* opts=":x:o:";
constexpr static const struct option opts_long[]={
	{"xform", 1, nullptr, 'x'},
	{"output", 1, nullptr, 'o'},
	{"decimate", 1, nullptr, 1000+'d'},
	{"help", 0, nullptr, 1000+'h'},
	{"version", 0, nullptr, 1000+'v'},
	{nullptr, 0, nullptr, 0}
};

static void usage(const char* argv0) {
	std::cout<<
		"usage: "<<argv0<<" [-o <pat>] <swc-file> ...\n"
		"       transform SWC files\n\n"
		"   or: "<<argv0<<" --help|--version\n"
		"       display this help or version information, and exit\n\n"
		"Transform SWC files.\n\n"
		"Options:\n\n"
		"   -o, --output <pat>   Output file pattern. Rules:\n"
		"                          %%: %\n"
		"                          %n: <filename>\n"
		"                          %p: <parent path>\n"
		"                          %s: <stem>\n"
		"                          %e: <extension>\n"
		"                        Use uppercase to allow empty.\n"
		"   -x, --xform <cmd>    Run <cmd> to transform coordinates.\n"
		"   --decimate <d>:<h>   Reduce number of nodes.\n"
		"Arguments:\n"
		"   <swc-file>           Input file.\n\n"
		PACKAGE_NAME " " PACKAGE_VERSION "\n"
		"Homepage: <" PACKAGE_URL ">\n";
}

static void version() {
	std::cout<<
		"map (" PACKAGE_NAME ") " PACKAGE_VERSION "\n"
		PACKAGE_COPYRIGHT "\n"
		"License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n"
		"This is free software: you are free to change and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted by law.\n";
}

static int filter_xform(const char* cmd, std::vector<JobData>& jobs) {
	namespace bp=boost::process;

	bp::child c;
	bp::ipstream in;
	bp::opstream out;
	c=bp::child{cmd, bp::std_out>in, bp::std_in<out};
	if(!c.valid())
		throw gapr::reported_error{"failed to launch"};
	std::thread thrw{[&jobs=std::as_const(jobs),&out]() {
		for(auto& j: jobs) {
			for(auto& n: j.nodes) {
				for(unsigned int i=0; i<3; ++i) {
					if(i!=0)
						out<<' ';
					out<<n.pos[i];
				}
				out<<'\n';
			}
		}
		out.close();
		out.pipe().close();
	}};
	std::string skipl;
	for(auto& j: jobs) {
		for(auto& n: j.nodes) {
			in>>n.pos[0]>>n.pos[1]>>n.pos[2];
			std::getline(in, skipl);
		}
	}
	thrw.join();
	c.wait();
	return c.exit_code();
}

int filter_decimate(double maxd, double maxh, std::vector<JobData>& jobs) {
	for(auto& j: jobs) {
#if 0
		preserve:::
#Gapr!ddd/ddd
#Gapr!ddd@error=
#Gapr!ddd@raise=
#Gapr!ddd@root=
#Gapr!ddd@state=
#Gapr!ddd@.traced=notfound
#Gapr!ddd@.traced=patchddd[ddd][ddd]
#endif

		auto nodes=decimate_impl(std::move(j.nodes), maxd, maxh);
		std::swap(j.nodes, nodes);
	}
	return 0;
}

int main(int argc, char* argv[]) {
	gapr::cli_helper cli_helper{};

	std::vector<Filter> filters;
	std::vector<JobData> jobs;

	try {
		const char* out_pat{nullptr};

		int opt;
		double d, h;
		auto parse_tuple=[](const char* str, double& d, double &h) {
			char* eptr;
			errno=0;
			d=strtod(str, &eptr);
			if(errno || *eptr!=':')
				return false;
			h=strtod(eptr+1, &eptr);
			if(errno || *eptr!='\x00')
				return false;
			return true;
		};
		while((opt=getopt_long(argc, argv, opts, &opts_long[0], nullptr))!=-1) {
			switch(opt) {
			case 1000+'h':
				usage(argv[0]);
				return EXIT_SUCCESS;
			case 1000+'v':
				version();
				return EXIT_SUCCESS;
			case 'o':
				if(optarg[0]=='\x00')
					throw gapr::reported_error{"empty <pattern>"};
				out_pat=optarg;
				break;
			case 'x':
				filters.push_back(std::bind(filter_xform, optarg, std::placeholders::_1));
				break;
			case 1000+'d':
				if(!parse_tuple(optarg, d, h))
					throw gapr::reported_error{"failed to parse <d>:<h>"};
				if(d<=0.0)
					throw gapr::reported_error{"need positive <d>"};
				if(h<=0.0)
					throw gapr::reported_error{"need positive <h>"};
				filters.push_back(std::bind(filter_decimate, d, h, std::placeholders::_1));
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
		if(optind+1>argc)
			throw gapr::reported_error{"argument <swc-file> missing"};
		for(int i=optind; i<argc; ++i) {
			auto& j=jobs.emplace_back();
			j.fn=std::filesystem::u8path(argv[i]);
			if(out_pat) {
				j.fn_out=output_path(out_pat, j.fn);
				if(j.fn_out.empty()) {
					gapr::str_glue err{"invalid <pattern>: `", out_pat, "'"};
					throw gapr::reported_error{err.str()};
				}
			}
		}
		if(filters.empty())
			throw gapr::reported_error{"nothing to do"};
	} catch(const gapr::reported_error& e) {
		gapr::str_glue msg{"error: ", e.what(), '\n',
			"try `", argv[0], " --help", "' for more information.\n"};
		std::cerr<<msg.str();
		return EXIT_FAILURE;
	}

	try {
		map_impl(std::move(jobs), filters);
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

