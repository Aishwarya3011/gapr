#include "compute.hh"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "gapr/commit.hh"
#include "utils.hh"

struct EvaluateHelper {
	struct {
		Evaluator& evaluator;
		const gapr::affine_xform& xform;
		const gapr::edge_model& graph;
		std::unordered_set<gapr::node_id>& dirty;
		gapr::cube cube;
		std::array<unsigned int, 3> offset;
		std::atomic<bool>* cancel;
	} args;
	gapr::delta_proofread_ evaluate(gapr::delta_add_patch_* delta2);
};

static std::shared_ptr<Evaluator> create_evaluator(const std::string& params) {
	assert(!params.empty());
	if(!std::filesystem::exists(params))
		return nullptr;
	fprintf(stderr, "Using resnet: %s\n", params.c_str());
	auto evaluator=Evaluator::create();
	std::ifstream str{params};
	if(!str)
		throw std::runtime_error{"failed to open parameter file"};
	evaluator->load(str);
	return evaluator;
}
static std::shared_ptr<Detector> create_detector(const std::string& params) {
	assert(!params.empty());
	if(!std::filesystem::exists(params))
		return nullptr;
	fprintf(stderr, "Using unet: %s\n", params.c_str());
	auto detector=Detector::create();
	std::ifstream str{params};
	if(!str)
		throw std::runtime_error{"failed to open parameter file 2"};
	detector->load(str);
	return detector;
}

gapr::trace::ConnectAlg::ConnectAlg(const std::string& params, const gapr::affine_xform& xform, const edge_model& graph):
	_xform{xform}, _graph{graph}, _dirty{}, _evaluator{}
{
}

void gapr::trace::ConnectAlg::evaluator(const std::string& params) {
	if(!params.empty())
		_evaluator=create_evaluator(params+".eval");
	if(!params.empty())
		_detector=create_detector(params+".pr");
}

gapr::trace::EvaluateAlg::EvaluateAlg(const std::string& params, const gapr::affine_xform& xform, const gapr::edge_model& graph):
	_xform{xform}, _graph{graph}, _dirty{}, _evaluator{}
{
	_evaluator=create_evaluator(params);
}

void gapr::trace::EvaluateAlg::Job::operator()(gapr::trace::EvaluateAlg& alg) {
	EvaluateHelper helper{*alg._evaluator, alg._xform, alg._graph, alg._dirty, cube, offset};
	delta=helper.evaluate(nullptr);
}

void gapr::trace::ConnectAlg::Job::operator()(gapr::trace::ConnectAlg& alg) {
	alg.impl(*this);
	if(alg._evaluator) {
		EvaluateHelper helper{*alg._evaluator, alg._xform, alg._graph, alg._dirty, cube, offset};
		helper.args.cancel=cancel;
		delta2=helper.evaluate(&delta);
	}
	std::sort(delta.props.begin(), delta.props.end(), [](auto& a, auto& b) {
		if(a.first<b.first)
			return true;
		if(a.first>b.first)
			return false;
		return a.second<b.second;
	});
	dump(delta, std::cerr, 1, gapr::node_id{});
}

static std::mutex dirty_mtx;
gapr::delta_proofread_ EvaluateHelper::evaluate(gapr::delta_add_patch_* delta2) {
	struct Node {
		gapr::node_id id;
		gapr::node_id tmpid;
		std::array<double, 3> pos;
	};
	std::unordered_set<gapr::node_id> dedup;
	std::vector<Node> nodes_in;
	{
		std::array<unsigned int, 3> cube_sizes;
		{
			auto view=args.cube.view<char>();
			for(unsigned int i=0; i<3; i++)
				cube_sizes[i]=view.sizes(i);
		}
		gapr::edge_model::reader model{args.graph};
		std::lock_guard dirty_lck{dirty_mtx};
		for(auto& [eid, edg]: model.edges()) {
			for(unsigned int idx=0; idx<edg.points.size(); idx++) {
				auto id=edg.nodes[idx];
				if(args.dirty.find(id)!=args.dirty.end())
					continue;
				gapr::node_attr attr{edg.points[idx]};
				if(attr.misc.coverage())
					continue;
				std::array<double, 3> pos{attr.pos(0), attr.pos(1), attr.pos(2)};
				auto node_off=args.xform.to_offset(pos);
				if(!check_inside<48, 48, 16>(node_off, cube_sizes, args.offset))
					continue;
				if(dedup.find(id)!=dedup.end())
					continue;
				dedup.insert(id);
				nodes_in.emplace_back(Node{id, gapr::node_id{}, pos});
			}
		}
		if(delta2) {
			unsigned int li=0;
			for(uint32_t k=0; k<delta2->nodes.size(); k++) {
				if(li<delta2->links.size()) {
					if(delta2->links[li].first==k+1) {
						++li;
						continue;
					}
				}
				std::array<double, 3> pos;
				gapr::node_attr attr{delta2->nodes[k].first};
				for(unsigned int i=0; i<3; i++)
					pos[i]=attr.pos(i);
				auto node_off=args.xform.to_offset(pos);
				if(!check_inside<48, 48, 16>(node_off, cube_sizes, args.offset))
					continue;
				nodes_in.emplace_back(Node{gapr::node_id{}, gapr::node_id{k+1}, pos});
			}
		}
	}

	std::valarray<float> samples(nodes_in.size()*16*48*48);
	std::valarray<float> temp(16*48*48);
	auto view=args.cube.view<uint16_t>();
	assert(view.type()==gapr::cube_type::u16);
	for(std::size_t k=0; k<nodes_in.size(); k++) {
		auto& node=nodes_in[k];
		auto node_off=args.xform.to_offset(node.pos);
		for(unsigned int i=0; i<3; i++)
			node_off[i]-=args.offset[i];
		for(unsigned int z=0; z<16; z++) {
			for(unsigned int y=0; y<48; y++) {
				auto p=view.row(y+node_off[1]-24, z+node_off[2]-8);
				for(unsigned int x=0; x<48; x++) {
					auto v=p[x+node_off[0]-24];
					temp[x+y*48+z*(48*48)]=v/65535.0;
				}
			}
		}
		auto scale=get_scale_factor(temp);
		for(unsigned int i=0; i<16*48*48; i++)
			samples[i+k*(48*48*16)]=temp[i]*scale;
	}

	auto pred=args.evaluator.predict(samples);
	gapr::delta_proofread_ delta;
	std::lock_guard dirty_lck{dirty_mtx};
	for(unsigned int k=0; k<nodes_in.size(); k++) {
		auto v1=pred[0+k*3];
		auto v2=pred[1+k*3];
		auto v3=pred[2+k*3];
		bool ok=(v3-v1>.5 && v3-v2>.5);

		if(nodes_in[k].id) {
			if(ok)
				delta.nodes.push_back(nodes_in[k].id.data);
			else
				args.dirty.insert(nodes_in[k].id);
		} else {
			assert(delta2 && nodes_in[k].tmpid);
			std::ostringstream oss{};
			oss<<".eval=("<<v1<<','<<v2<<','<<v3<<')';
			auto kk=nodes_in[k].tmpid.data-1;
			if(false)
				delta2->props.emplace_back(kk+1, oss.str());
			if(ok) {
				gapr::node_attr attr{delta2->nodes[kk].first};
				attr.misc.coverage(true);
				delta2->nodes[kk].first=attr.data();
			} else {
				// mark dirty?
			}
		}
	}
	return delta;
}

