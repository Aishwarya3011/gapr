/* brief/main.cc
 *
 * Copyright (C) 2021 GOU Lingfeng
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



#include "gapr/bbox.hh"
#include "gapr/utility.hh"
#include "gapr/exception.hh"
#include "gapr/str-glue.hh"
#include "gapr/swc-helper.hh"

#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include <iostream>
#include <cassert>
#include <vector>

#include <getopt.h>

#include "config.hh"

#include "util.hh"
#include "f1s.hh"
#include "dist.hh"


static void brief_impl(const char* fn) {
	SwcData dat;
	dat.read(fn);

	double total_len=0.0;
	for(auto& n: dat.nodes) {
		if(n.par_idx!=gapr::swc_node::par_idx_null) {
			auto& nn=dat.nodes[n.par_idx];
			total_len+=(n.pos-nn.pos).mag();
		}
	}
	std::cout.precision(2);
	std::cout<<std::fixed<<total_len<<" "<<fn<<std::endl;
}

static void sparsity_impl(const SwcData& dat, double rad) {
	const static double bin_size{32.0};
	struct BinKey {
		std::array<int32_t, 3> p;
		BinKey next(unsigned int i, int32_t d) const {
			BinKey r=*this;
			r.p[i]+=d;
			return r;
		}
		gapr::bbox bbox() const {
			gapr::vec3<double> r;
			for(unsigned int i=0; i<3; ++i)
				r[i]=p[i]*bin_size;
			gapr::bbox b;
			b.add(r);
			b.grow(bin_size/2);
			return b;
		}
		BinKey(const gapr::vec3<double>& v) {
			for(unsigned int i=0; i<3; ++i)
				p[i]=lround(v[i]/bin_size);
		}
		struct Hash {
			std::size_t operator()(const BinKey& a) const {
				std::size_t r=a.p[0];
				r^=a.p[1]<<15;
				r^=a.p[2]<<31;
				return r;
			}
		};
		struct Pred {
			bool operator()(const BinKey& a, const BinKey& b) const {
				return a.p==b.p;
			}
		};
	};
	struct BinVal {
		std::size_t cnt{0};
		std::size_t idx_head{SIZE_MAX};
		gapr::bbox bbox{};
		BinVal() { }
	};
	struct AltVal {
		std::size_t idx_next{SIZE_MAX};
		unsigned int nadj{0};
		double lenadj{0.0};
		AltVal() { }
	};
	std::unordered_map<BinKey, BinVal, BinKey::Hash, BinKey::Pred> bins;
	std::vector<AltVal> alt;
	alt.resize(dat.nodes.size(), AltVal{});
	auto bin_add=[&bins,&alt](BinKey k, std::size_t idx, const gapr::swc_node& n) {
		auto [it,ins]=bins.emplace(k, BinVal{});
		auto& bin=it->second;
		++bin.cnt;
		alt[idx].idx_next=bin.idx_head;
		bin.idx_head=idx;
		bin.bbox.add(n.pos);
	};
	for(std::size_t i=0; i<dat.nodes.size(); ++i) {
		auto& n=dat.nodes[i];
		BinKey k{n.pos};
		bin_add(k, i, n);
	}
	if(1) {
		std::size_t max_cnt=0;
		for(auto& [k, b]: bins) {
			if(b.cnt>max_cnt)
				max_cnt=b.cnt;
		}
		std::cerr<<"max cnt: "<<max_cnt<<std::endl;
	}

	std::unordered_multimap<std::size_t, std::size_t> adjs;
	std::unordered_set<std::size_t> cur_adjs;
	std::unordered_set<BinKey, BinKey::Hash, BinKey::Pred> bins_skip;
	std::vector<BinKey> bins_todo;
	auto rad2=rad*rad;
	for(auto& [k1, bin1]: bins) {
		adjs.clear();
		bins_skip.clear();
		bins_todo.clear();
		auto bbox1=bin1.bbox;
		bbox1.grow(rad);
		bins_todo.push_back(k1);
		do {
			auto k2=bins_todo.back();
			bins_todo.pop_back();
			if(auto [it, ins]=bins_skip.emplace(k2); !ins)
				continue;
			if(bbox1.hit_test(k2.bbox())) {
				for(unsigned int i=0; i<3; ++i) {
					bins_todo.push_back(k2.next(i, -1));
					bins_todo.push_back(k2.next(i, 1));
				}
			}

			auto it=bins.find(k2);
			if(it==bins.end())
				continue;
			auto& bin2=it->second;
			if(!bbox1.hit_test(bin2.bbox))
				continue;

			auto idx1=bin1.idx_head;
			do {
				auto p1=dat.nodes[idx1].pos;
				auto idx2=bin2.idx_head;
				do {
					auto p2=dat.nodes[idx2].pos;
					p2-=p1;
					if(p2.mag2()<rad2)
						adjs.emplace(idx1, idx2);

					idx2=alt[idx2].idx_next;
				} while(idx2!=SIZE_MAX);

				idx1=alt[idx1].idx_next;
			} while(idx1!=SIZE_MAX);
		} while(!bins_todo.empty());

		auto idx1=bin1.idx_head;
		do {
			cur_adjs.clear();
			auto& cur_alt=alt[idx1];
			auto [it1, it2]=adjs.equal_range(idx1);
			for(auto it=it1; it!=it2; ++it) {
				cur_adjs.emplace(it->second);
				++cur_alt.nadj;
			}
			assert(cur_alt.nadj==cur_adjs.size());
			for(auto idx2: cur_adjs) {
				auto& n2=dat.nodes[idx2];
				if(n2.par_idx==n2.par_idx_null)
					continue;
				if(cur_adjs.find(n2.par_idx)==cur_adjs.end())
					continue;
				auto d=n2.pos-dat.nodes[n2.par_idx].pos;
				cur_alt.lenadj+=d.mag();
			}

			idx1=alt[idx1].idx_next;
		} while(idx1!=SIZE_MAX);
	}

	if(1) {
		gapr::swc_output swc{std::cout};
		for(std::size_t idx1=0; idx1<dat.nodes.size(); ++idx1) {
			auto n1=dat.nodes[idx1];
			if(n1.par_idx!=n1.par_idx_null)
				n1.par_id=dat.nodes[n1.par_idx].id;
			n1.type=alt[idx1].nadj;
			n1.radius=alt[idx1].lenadj;
			swc.node(n1);
		}
	} else {
		long max_len{0};
		std::unordered_map<long, double> cnts;
		for(std::size_t i=0; i<dat.nodes.size(); ++i) {
			auto len=std::lround(alt[i].lenadj);
			assert(len>=0);
			auto [it, ins]=cnts.emplace(len, 0.0);
			auto pidx=dat.nodes[i].par_idx;
			if(pidx!=gapr::swc_node::par_idx_null) {
				auto d=dat.nodes[i].pos-dat.nodes[pidx].pos;
				it->second+=d.mag();
			}
			if(len>max_len)
				max_len=len;
		}
		for(long l=0; l<=max_len; ++l) {
			std::cout<<l<<' ';
			if(auto it=cnts.find(l); it!=cnts.end())
				std::cout<<it->second<<"\n";
			else
				std::cout<<"0.0\n";
		}
	}
}

constexpr static const char* opts=":";
constexpr static const struct option opts_long[]={
	{"sparsity", 1, nullptr, 1000+'s'},
	{"f1score", 1, nullptr, 1000+'f'},
	{"dist", 2, nullptr, 1000+'d'},
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

int main(int argc, char* argv[]) {
	gapr::cli_helper cli_helper{};

	double sparsity_r{0.0};
	const char* ref_swc{nullptr};
	bool dist_mode{false};
	bool dist_test{false};
	double dist_max_segm{INFINITY};

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
				case 1000+'s':
					{
						char* eptr;
						sparsity_r=strtod(optarg, &eptr);
						if(errno || *eptr!='\x00')
							throw gapr::reported_error{"failed to parse number"};
						if(sparsity_r<=0)
							throw gapr::reported_error{"positive number expected for <r>"};
					}
					break;
				case 1000+'f':
					ref_swc=optarg;
					break;
				case 1000+'d':
					dist_mode=true;
					if(optarg) {
						if(strcmp(optarg, "test")==0) {
							dist_test=true;
							break;
						}
						char* eptr;
						dist_max_segm=strtod(optarg, &eptr);
						if(errno || *eptr!='\x00')
							throw gapr::reported_error{"failed to parse number"};
					}
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
	} catch(const gapr::reported_error& e) {
		gapr::str_glue msg{"error: ", e.what(), '\n',
			"try `", argv[0], " --help", "' for more information.\n"};
		std::cerr<<msg.str();
		return EXIT_FAILURE;
	}

	try {

		if(sparsity_r>0) {
			SwcData dat;
			for(int i=optind; i<argc; ++i)
				dat.read(argv[i]);
			sparsity_impl(dat, sparsity_r);
			return 0;
		}

		if(dist_mode) {
			if(dist_test) {
				dist_impl_test();
				return 0;
			}
			dist_impl(argc-optind, &argv[optind], dist_max_segm);
			return 0;
		}

		if(ref_swc) {
			SwcData dat_ref;
			dat_ref.read(ref_swc);
			std::shared_ptr<void> aux;
			for(int i=optind; i<argc; ++i) {
				SwcData dat;
				dat.read(argv[i]);
				aux=f1score_impl(dat_ref.nodes, aux, dat.nodes, nullptr)[0];
			}
			return 0;
		}

		for(int i=optind; i<argc; ++i)
			brief_impl(argv[i]);
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

