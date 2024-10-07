#include "gapr/swc-helper.hh"
#include "gapr/exception.hh"

#include <unordered_map>
#include <fstream>
#include <filesystem>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>

namespace {
struct SwcData {
	std::vector<gapr::swc_node> nodes;
	std::unordered_map<int64_t, std::string> roots;
	std::unordered_map<int64_t, std::size_t> id2idx;

	void read(const std::filesystem::path& fn) {
		std::ifstream fs{fn};
		boost::iostreams::filtering_istream filter{};
		if(fn.extension()==".gz")
			filter.push(boost::iostreams::gzip_decompressor{});
		filter.push(fs);

		gapr::swc_input swc{filter};
		while(swc.read()) {
			switch(swc.tag()) {
			case gapr::swc_input::tags::comment:
				break;
			case gapr::swc_input::tags::node:
				{
					auto& n=nodes.emplace_back(swc.node());
					auto [it, ins]=id2idx.emplace(n.id, nodes.size()-1);
					if(!ins)
						throw std::runtime_error{"dup node"};
					if(n.par_id!=gapr::swc_node::par_id_null) {
						auto it2=id2idx.find(n.par_id);
						if(it2==id2idx.end())
							throw std::runtime_error{"invalid parent id"};
						n.par_idx=it2->second;
					}
				}
				break;
			case gapr::swc_input::tags::annot:
				{
					auto id=swc.id();
					if(swc.annot_key()=="root")
						roots.emplace(id, swc.annot_val());
				}
				break;
			case gapr::swc_input::tags::misc_attr:
				break;
			case gapr::swc_input::tags::loop:
				break;
			}
		}
		if(!swc.eof())
			throw gapr::reported_error{"Failed to read swc file."};
	}
};
}
