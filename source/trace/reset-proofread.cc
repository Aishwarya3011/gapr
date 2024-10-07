#include "gapr/commit.hh"
#include "gapr/edge-model.hh"
#include "gapr/utility.hh"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <charconv>

#include "../corelib/model-upgrade.hh"

template<int Rad>
inline static bool check_inside(const gapr::node_attr& seed, const gapr::node_attr& attr) {
	auto rad=Rad*1024;
	for(unsigned int i=0; i<3; i++) {
		auto d=seed.ipos[i]-attr.ipos[i];
		if(d<-rad || d>rad)
			return false;
	}
	return true;
}
using Vec=std::array<double, 3>;
static inline double len(const Vec& v) {
	double s=0.0;
	for(unsigned int i=0; i<3; i++) {
		auto vv=v[i];
		s+=vv*vv;
	}
	return std::sqrt(s);
}
static inline double len2(const Vec& v) {
	double s=0.0;
	for(unsigned int i=0; i<3; i++) {
		auto vv=v[i];
		s+=vv*vv;
	}
	return s;
}
static double dot(const Vec& v, const Vec& v2) {
	double s=0.0;
	for(unsigned int i=0; i<3; i++)
		s+=v[i]*v2[i];
	return s;
}
static Vec sub(const Vec& v, const Vec& v2) {
	Vec r;
	for(unsigned int i=0; i<3; i++)
		r[i]=v[i]-v2[i];
	return r;
}
static double dist_to(const std::array<double, 3>& p,
		const std::array<double, 3>& p2,
		const std::array<double, 3>& p3, double l23, double ub) {
	auto d2=sub(p, p2);
	auto l1=len(d2);
	if(l1-l23>=ub)
		return ub;
	if(l23<0.01)
		return l1;
	auto d23=sub(p3, p2);
	auto ll=dot(d2, d23)/l23;
	if(ll<0)
		return l1;
	if(ll>l23)
		return len(sub(p, p3));
	return std::sqrt(l1*l1-ll*ll);
}

gapr::delta_reset_proofread_ compute_reset_proofread(const gapr::edge_model& model, const std::vector<std::string>& diffs, bool reset_all) {
	bool reset_loop=true;
	bool reset_root=true;
	bool reset_error=true;
	bool reset_false_term=true;
	bool reset_near=true;
	bool reset_branch=true;
	bool clear_hidden_props=true;
	bool clear_term=true;
	bool clear_term_skip_triv=true;
	bool clear_term_skip_has_root=false;

	std::unordered_set<gapr::node_id> to_reset;
	std::unordered_set<gapr::prop_id> to_clear;

	gapr::edge_model::reader reader{model};

	if(reset_all) {
		reset_loop=false;
		reset_root=false;
		reset_error=false;
		reset_false_term=false;
		reset_near=false;
		reset_branch=false;
		clear_hidden_props=true;
		clear_term=false;
		clear_term_skip_triv=true;
		clear_term_skip_has_root=true;
	}
	if(!diffs.empty()) {
		reset_loop=false;
		reset_root=false;
		reset_error=false;
		reset_false_term=true;
		reset_near=false;
		reset_branch=false;
		clear_hidden_props=true;
		clear_term=true;
		clear_term_skip_triv=false;
		clear_term_skip_has_root=false;

		for(auto& diff: diffs) {
			std::ifstream fs{diff};
			std::string line;
			while(std::getline(fs, line)) {
				if(!line.empty() && line.back()=='\r')
					line.pop_back();
				if(line.empty())
					continue;
				gapr::node_id id1, id2;
				auto eptr=line.data()+line.size();
				auto res=std::from_chars(line.data()+1, eptr, id1.data);
				if(res.ec!=std::errc{})
					throw std::runtime_error{"cannot parse line"};
				if(res.ptr!=eptr) {
					if(*res.ptr!='/')
						throw std::runtime_error{"cannot parse line 2"};
					res=std::from_chars(res.ptr+1, eptr, id2.data);
					if(res.ec!=std::errc{})
						throw std::runtime_error{"cannot parse line 3"};
					if(res.ptr!=eptr)
						throw std::runtime_error{"cannot parse line 4"};
					// link
					switch(line[0]) {
						case '+':
							break;
						case '-':
							break;
						default:
							throw std::runtime_error{"wrong diff format"};
					}
					to_reset.emplace(id1);
					to_reset.emplace(id2);
				} else {
					// node
					switch(line[0]) {
						case '+':
							break;
						case '-':
							break;
						case '!':
							break;
						default:
							throw std::runtime_error{"wrong diff format"};
					}
					to_reset.emplace(id1);
				}
			}
			if(!fs.eof())
				throw std::runtime_error{"failed to read file"};
		}
	}

	/*! reset loops */
	if(reset_loop) {
		for(auto& [eid, edg]: reader.edges()) {
			if(edg.loop) {
				for(auto n: edg.nodes)
					to_reset.emplace(n);
			}
		}
		for(auto& [vid, vert]: reader.vertices()) {
			if(vert.loop)
				to_reset.emplace(vid);
		}
	}

	/*! reset root */
	if(reset_root) {
		for(auto& [vid, vert]: reader.vertices()) {
			gapr::prop_id pid{vid, "root"};
			if(reader.props().find(pid)!=reader.props().end())
				to_reset.emplace(vid);
		}
	}

	/*! reset error site */
	if(reset_error) {
		for(auto& [pid, val]: reader.props()) {
			if(pid.key=="error")
				to_reset.emplace(gapr::node_id{pid.node});
		}
	}

	/*! reset terminal marks */
	if(reset_false_term) {
		for(auto& [pid, val]: reader.props()) {
			if(pid.key=="state") {
				auto pos=reader.nodes().at(pid.node);
				bool term{true};
				if(pos.edge) {
					term=false;
				} else {
					auto& vert=reader.vertices().at(pos.vertex);
					if(vert.edges.size()>=2) {
						term=false;
					}
				}
				if(!term) {
					to_reset.emplace(gapr::node_id{pid.node});
					if(val=="end")
						to_clear.emplace(pid);
				}
			}
		}
	}

	/*! reset colliding edges */
	// XXX 
	if(reset_near) {
		std::vector<std::pair<gapr::edge_model::edge_id, unsigned int>> edges_todo;
		std::unordered_set<gapr::node_id> dirty;

		for(auto& [eid, edg]: reader.edges())
			edges_todo.emplace_back(eid, edg.points.size());

		struct NodeInfo {
			gapr::edge_model::edge_id eid;
			uint32_t idx;
			gapr::node_id nid;
			std::array<double, 3> pos;
			std::array<double, 3> pos2;
			double ll;
			bool boundary;
		};
		while(!edges_todo.empty()) {
			gapr::node_attr seed;
			{
				auto [eid, idx]=edges_todo.back();
				auto& edg=reader.edges().at(eid);
				while(idx-->0) {
					if(dirty.find(edg.nodes[idx])==dirty.end())
						break;
				}
				if(idx>=edg.points.size()) {
					edges_todo.pop_back();
					continue;
				}
				edges_todo.back().second=idx;
				seed=gapr::node_attr{edg.points[idx]};
			}

			std::vector<NodeInfo> nodes_in;
			for(auto& [eid, edg]: reader.edges()) {
				for(unsigned int idx=0; idx<edg.points.size(); idx++) {
					gapr::node_attr attr{edg.points[idx]};
					if(check_inside<64>(seed, attr)) {
						std::array<double, 3> pos{attr.pos(0), attr.pos(1), attr.pos(2)};
						unsigned int idx2=idx;
						if(idx+1<edg.points.size())
							idx2=idx+1;
						gapr::node_attr attr2{edg.points[idx2]};
						std::array<double, 3> pos2{attr2.pos(0), attr2.pos(1), attr2.pos(2)};
						nodes_in.emplace_back(NodeInfo{eid, idx, edg.nodes[idx], pos, pos2, len(sub(pos2, pos)), !check_inside<60>(seed, attr)});
					}
				}
			}
			gapr::print("batch size: ", nodes_in.size(), ' ', edges_todo.size());
			for(auto& node: nodes_in) {
				if(dirty.find(node.nid)!=dirty.end())
					continue;
				if(node.boundary)
					continue;
				dirty.insert(node.nid);

				double mdist{INFINITY};
				for(auto& node2: nodes_in) {
					if(node2.eid==node.eid)
						continue;
					auto d=dist_to(node.pos, node2.pos, node2.pos2, node2.ll, 5);
					if(d<mdist)
						mdist=d;
				}
				if(mdist<3)
					to_reset.emplace(node.nid);
			}
		}
	}

	// XXX optimize end short edge
	std::unordered_set<gapr::edge_model::edge_id> triv_edges;
	for(auto& [eid, edg]: reader.edges()) {
		if(edg.points.size()>4)
			continue;
		unsigned int has_term_end=0;
		for(auto vid: {edg.left, edg.right}) {
			auto& vert=reader.vertices().at(vid);
			if(vert.edges.size()<2) {
				gapr::prop_id pid{vid, "state"};
				auto it=reader.props().find(pid);
				if(it!=reader.props().end() && it->second=="end")
					has_term_end+=1;
			}
		}
		if(has_term_end!=1)
			continue;
		// XXX chk length
		// XXX check collide
		//triv edg
		//col 234 col 1
		triv_edges.emplace(eid);
	}

	/*! clear hidden props */
	if(clear_hidden_props) {
		for(auto& [pid, val]: reader.props()) {
			if(pid.key[0]=='.')
				to_clear.emplace(pid);
		}
	}

	/*! reset abrupt change */
	// XXX

	/*! reset branch point and clear term end */
	if(reset_branch || clear_term) {
		for(auto& [vid, vert]: reader.vertices()) {
			if(vert.edges.size()<2) {
				if(!clear_term)
					continue;
				gapr::prop_id pid{vid, "state"};
				auto it=reader.props().find(pid);
				if(it!=reader.props().end() && it->second=="end") {
					if(vert.edges.size()==1) {
						if(triv_edges.find(vert.edges[0].first)!=triv_edges.end() && clear_term_skip_triv)
							continue;
					}
					if(vert.root && clear_term_skip_has_root)
						continue;
					to_clear.emplace(pid);
				}
			} else if(reset_branch) {
				to_reset.emplace(vid);
			}
		}
	}

	if(reset_all) {
		for(auto& [eid, edg]: reader.edges()) {
			for(unsigned int i=0; i<edg.nodes.size(); i++)
				to_reset.insert(edg.nodes[i]);
		}
		for(auto& [vid, vert]: reader.vertices()) {
			if(vert.edges.size()==0)
				to_reset.insert(vid);
		}
	}

	/*! build delta */
	gapr::delta_reset_proofread_0_ delta;
	auto add_node=[&to_reset,&delta](gapr::node_id n, gapr::node_attr attr) {
		auto it=to_reset.find(n);
		if(it!=to_reset.end()) {
			if(attr.misc.coverage())
				delta.nodes.push_back(n.data);
			to_reset.erase(it);
		}
	};
	for(auto& [eid, edg]: reader.edges()) {
		for(unsigned int i=0; i<edg.nodes.size(); i++)
			add_node(edg.nodes[i], gapr::node_attr{edg.points[i]});
	}
	for(auto& [vid, vert]: reader.vertices()) {
		if(vert.edges.size()==0)
			add_node(vid, vert.attr);
	}
	for(auto& pid: to_clear) {
		delta.props.emplace_back(pid.node.data, std::move(pid.key));
	}
	std::sort(delta.props.begin(), delta.props.end(), [](auto& a, auto& b) {
		if(a.first<b.first)
			return true;
		if(a.first>b.first)
			return false;
		return a.second<b.second;
	});
	//delta.dump(std::cout, 2);
	return upgrade_delta(std::move(delta));
}

