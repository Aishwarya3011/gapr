#include "model-misc.hh"

#include "gapr/cube.hh"
#include "gapr/utility.hh"

#include <sstream>
#include <fstream>

#include "misc.hh"

namespace {

/*! find next position, typical order example
 * 0. error, connected to cur_pos (on a tree or not), tree distance.
 * 1. error, same tree with cur_pos, physical distance.
 * 2. error, on a tree, physical distance.
 * 3. deferred error, connected to cur_pos (on a tree or not), tree distance.
 * 4. deferred error, same tree with cur_pos, physical distance.
 * 5. deferred error, on a tree, physical distance.
 * 6. error, not on a tree, physical distance.
 * 7. deferred error, not on a tree, physical distance.
 */
//per prop => err
//per vert => end
//per edg/node => pr

struct traverse_helper {
	gapr::edge_model::view model;
	gapr::node_attr curr_pos;

	gapr::node_id best_node{};
	unsigned int best_prio{1023};
	double best_dist{0.0};
	void update_prio(unsigned int prio, double d, gapr::node_id id) {
		assert(prio<=best_prio);
		fprintf(stderr, "%u %lf @%u\n", prio, d, id.data);
		if(prio<best_prio) {
			best_prio=prio;
			best_node=id;
			best_dist=d;
		} else if(d<best_dist) {
			best_node=id;
			best_dist=d;
		}
	}

	gapr::node_id curr_root{};
	std::unordered_map<gapr::node_id, double> distmap;
	void set_current(gapr::node_id cur_node) {
		assert(distmap.empty());
		std::deque<std::pair<gapr::node_id, double>> todo;
		std::unordered_set<gapr::edge_model::edge_id> dirty;
		auto node_pos=model.nodes().at(cur_node);
		if(node_pos.edge) {
			auto& edg=model.edges().at(node_pos.edge);
			curr_root=edg.root;
			double len=edg.points.size()-1;
			auto a=node_pos.index/128.0;
			auto b=len-a;
			todo.push_back({edg.left, -a});
			todo.push_back({edg.right, -b});
		} else if(node_pos.vertex) {
			auto& vert=model.vertices().at(node_pos.vertex);
			curr_root=vert.root;
			todo.push_back({node_pos.vertex, 0.0});
		} else {
			assert(0);
		}
		assert(!todo.empty());
		do {
			auto [vid, dist]=todo.front();
			todo.pop_front();
			auto [it, ins]=distmap.emplace(vid, dist);
			if(!ins)
				continue;
			auto& vert=model.vertices().at(vid);
			for(auto [eid, dir]: vert.edges) {
				auto [it, ins]=dirty.emplace(eid);
				if(!ins)
					continue;
				auto& edg=model.edges().at(eid);
				auto v2=dir?edg.left:edg.right;
				auto dist2=std::abs(dist)+edg.points.size()-1;
				todo.push_back({v2, dist2});
			}
		} while(!todo.empty());
	}

	struct cond_conn {
		traverse_helper& helper;
		decltype(distmap.begin()) it1, it2;
		bool test_edge(const gapr::edge_model::edge& edg) {
			it1=helper.distmap.find(edg.left);
			if(it1==helper.distmap.end())
				return false;
			it2=helper.distmap.find(edg.right);
			if(it2==helper.distmap.end())
				return false;
			return true;
		}
		double dist_edge(const gapr::edge_model::edge& edg, std::size_t idx) const {
			unsigned int nn=0;
			auto a=it1->second;
			if(a<0) {
				a=-a;
				++nn;
			}
			auto b=it2->second;
			if(b<0) {
				b=-b;
				++nn;
			}
			if(nn==2)
				return std::abs(a-idx);
			return a+(b-a)*idx/(edg.points.size()-1);
		}
		bool test_vert(gapr::node_id vid, const gapr::edge_model::vertex&) {
			it1=helper.distmap.find(vid);
			return it1!=helper.distmap.end();
		}
		double dist_vert(const gapr::edge_model::vertex&) {
			return std::abs(it1->second);
		}
	};
	struct cond_same_root {
		traverse_helper& helper;
		decltype(distmap.begin()) it1, it2;
		bool test_edge(const gapr::edge_model::edge& edg) {
			return helper.curr_root==edg.root;
		}
		double dist_edge(const gapr::edge_model::edge& edg, std::size_t idx) {
			gapr::node_attr a{edg.points[idx]};
			return a.dist_to(helper.curr_pos);
		}
		bool test_vert(gapr::node_id vid, const gapr::edge_model::vertex& vert) {
			return helper.curr_root==vert.root;
		}
		double dist_vert(const gapr::edge_model::vertex& vert) {
			return helper.curr_pos.dist_to(vert.attr);
		}
	};
	struct cond_tree {
		traverse_helper& helper;
		decltype(distmap.begin()) it1, it2;
		bool test_edge(const gapr::edge_model::edge& edg) {
			if(helper.model.raised() && !edg.raised)
				return false;
			return edg.root!=gapr::node_id{};
		}
		double dist_edge(const gapr::edge_model::edge& edg, std::size_t idx) {
			gapr::node_attr a{edg.points[idx]};
			return a.dist_to(helper.curr_pos);
		}
		bool test_vert(gapr::node_id vid, const gapr::edge_model::vertex& vert) {
			if(helper.model.raised() && !vert.raised)
				return false;
			return vert.root!=gapr::node_id{};
		}
		double dist_vert(const gapr::edge_model::vertex& vert) {
			return helper.curr_pos.dist_to(vert.attr);
		}
	};
	struct cond_non_tree {
		traverse_helper& helper;
		decltype(distmap.begin()) it1, it2;
		bool test_edge(const gapr::edge_model::edge& edg) {
			if(helper.model.raised() && !edg.raised)
				return false;
			return edg.root==gapr::node_id{};
		}
		double dist_edge(const gapr::edge_model::edge& edg, std::size_t idx) {
			gapr::node_attr a{edg.points[idx]};
			return a.dist_to(helper.curr_pos);
		}
		bool test_vert(gapr::node_id vid, const gapr::edge_model::vertex& vert) {
			if(helper.model.raised() && !vert.raised)
				return false;
			return vert.root==gapr::node_id{};
		}
		double dist_vert(const gapr::edge_model::vertex& vert) {
			return helper.curr_pos.dist_to(vert.attr);
		}
	};
	static unsigned int refine_prio(bool res, unsigned int max_prio) {
		return res?max_prio:1024;
	}
	static unsigned int refine_prio(unsigned int res, unsigned int max_prio) {
		return res;
	}

	template<typename Cond, typename PerProp, typename Filter>
	void traverse_props_impl(unsigned int max_prio, PerProp per_prop, Filter filter) {
		if(best_prio<max_prio)
			return;
		for(auto& [key, val]: model.props()) {
			if(key.key[0]=='.')
				continue;
			auto pos=model.nodes().at(key.node);
			Cond cond{*this};
			if(pos.edge) {
				auto& edg=model.edges().at(pos.edge);
				if(!cond.test_edge(edg))
					continue;
				auto res=per_prop(key, val);
				auto prio=refine_prio(res, max_prio);
				if(prio>best_prio)
					continue;
				auto idx=pos.index/128;
				if constexpr(!std::is_same_v<Filter, std::nullptr_t>) {
					if(!filter(edg.nodes[idx], gapr::node_attr{edg.points[idx]}))
						continue;
				}
				auto d=cond.dist_edge(edg, idx);
				update_prio(prio, d, edg.nodes[idx]);
			} else if(pos.vertex) {
				auto& vert=model.vertices().at(pos.vertex);
				if(!cond.test_vert(pos.vertex, vert))
					continue;
				auto res=per_prop(key, val);
				auto prio=refine_prio(res, max_prio);
				if(prio>best_prio)
					continue;
				if constexpr(!std::is_same_v<Filter, std::nullptr_t>) {
					if(!filter(pos.vertex, vert.attr))
						continue;
				}
				auto d=cond.dist_vert(vert);
				update_prio(prio, d, pos.vertex);
			} else {
				assert(0);
			}
		}
	}
	template<typename PerProp, typename Filter> void traverse_conn_props(unsigned int max_prio, PerProp per_prop, Filter filter) {
		if(distmap.empty())
			return;
		return traverse_props_impl<cond_conn>(max_prio, per_prop, filter);
	}
	template<typename PerProp, typename Filter> void traverse_same_root_props(unsigned int max_prio, PerProp per_prop, Filter filter) {
		if(distmap.empty())
			return;
		if(!curr_root)
			return;
		return traverse_props_impl<cond_same_root>(max_prio, per_prop, filter);
	}
	template<typename PerProp, typename Filter> void traverse_tree_props(unsigned int max_prio, PerProp per_prop, Filter filter) {
		return traverse_props_impl<cond_tree>(max_prio, per_prop, filter);
	}
	template<typename PerProp, typename Filter> void traverse_non_tree_props(unsigned int max_prio, PerProp per_prop, Filter filter) {
		return traverse_props_impl<cond_non_tree>(max_prio, per_prop, filter);
	}

	template<typename Cond, typename PerVert, typename Filter> void traverse_verts_impl(unsigned int max_prio, PerVert per_vert, Filter filter) {
		if(best_prio<max_prio)
			return;
		for(auto& [vid, vert]: model.vertices()) {
			Cond cond{*this};
			if(!cond.test_vert(vid, vert))
				continue;
			auto res=per_vert(model, vid, vert);
			auto prio=refine_prio(res, max_prio);
			if(prio>best_prio)
				continue;
			if constexpr(!std::is_same_v<Filter, std::nullptr_t>) {
				if(!filter(vid, vert.attr))
					continue;
			}
			auto d=cond.dist_vert(vert);
			update_prio(prio, d, vid);
		}
	}
	template<typename PerVert, typename Filter> void traverse_conn_verts(unsigned int max_prio, PerVert per_vert, Filter filter) {
		if(distmap.empty())
			return;
		return traverse_verts_impl<cond_conn>(max_prio, per_vert, filter);
	}
	template<typename PerVert, typename Filter> void traverse_same_root_verts(unsigned int max_prio, PerVert per_vert, Filter filter) {
		if(distmap.empty())
			return;
		if(!curr_root)
			return;
		return traverse_verts_impl<cond_same_root>(max_prio, per_vert, filter);
	}
	template<typename PerVert, typename Filter> void traverse_tree_verts(unsigned int max_prio, PerVert per_vert, Filter filter) {
		return traverse_verts_impl<cond_tree>(max_prio, per_vert, filter);
	}
	template<typename PerVert, typename Filter> void traverse_non_tree_verts(unsigned int max_prio, PerVert per_vert, Filter filter) {
		return traverse_verts_impl<cond_non_tree>(max_prio, per_vert, filter);
	}

	template<typename Cond, typename PerNode, typename Filter> void traverse_edges_impl(unsigned int max_prio, PerNode per_node, Filter filter) {
		if(best_prio<max_prio)
			return;
		for(auto& [eid, edg]: model.edges()) {
			Cond cond{*this};
			if(!cond.test_edge(edg))
				continue;
			for(std::size_t i=0; i<edg.points.size(); ++i) {
				auto idx=i;
				auto res=per_node(edg, idx);
				auto prio=refine_prio(res, max_prio);
				if(prio>best_prio)
					continue;
				if constexpr(!std::is_same_v<Filter, std::nullptr_t>) {
					if(!filter(edg.nodes[idx], gapr::node_attr{edg.points[idx]}))
						continue;
				}
				auto d=cond.dist_edge(edg, idx);
				update_prio(prio, d, edg.nodes[idx]);
			}
		}
	}

	template<typename PerNode, typename Filter> void traverse_conn_edges(unsigned int max_prio, PerNode per_node, Filter filter) {
		if(distmap.empty())
			return;
		return traverse_edges_impl<cond_conn>(max_prio, per_node, filter);
	}
	template<typename PerNode, typename Filter> void traverse_same_root_edges(unsigned int max_prio, PerNode per_node, Filter filter) {
		if(distmap.empty())
			return;
		if(!curr_root)
			return;
		return traverse_edges_impl<cond_same_root>(max_prio, per_node, filter);
	}
	template<typename PerNode, typename Filter> void traverse_tree_edges(unsigned int max_prio, PerNode per_node, Filter filter) {
		return traverse_edges_impl<cond_tree>(max_prio, per_node, filter);
	}
	template<typename PerNode, typename Filter> void traverse_non_tree_edges(unsigned int max_prio, PerNode per_node, Filter filter) {
		return traverse_edges_impl<cond_non_tree>(max_prio, per_node, filter);
	}
};

// XXX
gapr::node_id get_seed_pos_random_err(gapr::edge_model::view model, unsigned long rand) {
	std::vector<gapr::node_id> errs{};
	for(auto& [pid, pval]: model.props()) {
		if(pred_empty_err{}(pid, pval)) {
			if(model.raised()) {
				auto pos=model.nodes().at(pid.node);
				if(pos.edge) {
					auto& e=model.edges().at(pos.edge);
					if(!e.raised)
						continue;
				} else {
					auto& v=model.vertices().at(pos.vertex);
					if(!v.raised)
						continue;
				}
			}
			errs.push_back(pid.node);
		}
	}
	if(errs.empty())
		return {};
	auto idx=rand%errs.size();
	auto nid=errs[idx];

	auto check_nid=[model](gapr::node_id nid) ->bool {
		auto pos=model.nodes().at(nid);
		gapr::node_attr cent;
		if(pos.edge) {
			auto& e=model.edges().at(pos.edge);
			cent=gapr::node_attr{e.points[pos.index/128]};
		} else {
			auto& v=model.vertices().at(pos.vertex);
			cent=v.attr;
		}
		auto check_proximity=[cent](gapr::node_attr a) ->bool {
			constexpr int32_t max=40*1024;
			for(unsigned int i=0; i<3; ++i) {
				auto d=cent.ipos[i]-a.ipos[i];
				if(d>max)
					return false;
				if(d<-max)
					return false;
			}
			return true;
		};

		for(auto& [eid, edg]: model.edges()) {
			for(std::size_t i=0; i<edg.nodes.size(); i++) {
				gapr::node_attr a{edg.points[i]};
				if(!a.misc.coverage()) {
					if(check_proximity(a))
						return false;
				}
			}
		}
		for(auto& [vid, vert]: model.vertices()) {
			if(!pred_empty_end{}(model, vid, vert))
				continue;
			if(check_proximity(vert.attr))
				return false;
		}
		return true;
	};
	if(check_nid(nid))
		return nid;
	return {};
}
std::vector<gapr::node_id> get_seed_pos_random_edg(gapr::edge_model::view model) {
	std::vector<gapr::node_id> candidates{};
	for(auto& [eid, edg]: model.edges()) {
		if(model.raised() && !edg.raised)
			continue;
		gapr::node_id nn{};
		for(std::size_t i=0; i<edg.nodes.size(); i++) {
			gapr::node_attr a{edg.points[i]};
			if(!a.misc.coverage()) {
				nn=edg.nodes[i];
				break;
			}
		}
		if(!nn)
			continue;
		candidates.push_back(nn);
	}
	return candidates;
}

}

gapr::node_id get_seed_pos_random(gapr::edge_model::view model, bool include_err, unsigned long rand) {
	if(include_err)
		if(auto n=get_seed_pos_random_err(model, rand); n)
			return n;
	auto candidates=get_seed_pos_random_edg(model);
	for(auto& [vid, vert]: model.vertices()) {
		if(model.raised() && !vert.raised)
			continue;
		if(!pred_empty_end{}(model, vid, vert))
			continue;
		candidates.push_back(vid);
	}
	if(candidates.size()==0)
		return {};
	unsigned int idx=rand%candidates.size();
	auto n=candidates[idx];
	gapr::print(2, "random seed ", n.data, "/", candidates.size());
	return n;
}
gapr::node_id find_next_error(
		gapr::edge_model::view model,
		gapr::node_id cur_node,
		const gapr::node_attr& cur_pos) {
	traverse_helper helper{model, cur_pos};
	if(cur_node)
		helper.set_current(cur_node);
	helper.traverse_conn_props(0, pred_defer_err<0, 3>{model}, nullptr);
	helper.traverse_same_root_props(1, pred_defer_err<1, 4>{model}, nullptr);
	helper.traverse_tree_props(2, pred_defer_err<2, 5>{model}, nullptr);
	helper.traverse_non_tree_props(6, pred_defer_err<6, 7>{model}, nullptr);
	return helper.best_node;
}
gapr::node_id find_next_node(
		gapr::edge_model::view model,
		gapr::node_id cur_node,
		const gapr::node_attr& cur_pos) {
	traverse_helper helper{model, cur_pos};
	if(cur_node)
		helper.set_current(cur_node);
	helper.traverse_conn_verts(0, pred_empty_end{}, nullptr);
	// ? skip_conn_verts
	helper.traverse_conn_edges(0, pred_no_coverage{}, nullptr);
	// if(!mask_cur) pred_empty_end(cur_node);
	helper.traverse_same_root_verts(2, pred_empty_end{}, nullptr);
	helper.traverse_same_root_edges(2, pred_no_coverage{}, nullptr);
	helper.traverse_tree_verts(4, pred_empty_end{}, nullptr);
	helper.traverse_tree_edges(4, pred_no_coverage{}, nullptr);
	helper.traverse_non_tree_verts(6, pred_empty_end{}, nullptr);
	helper.traverse_non_tree_edges(6, pred_no_coverage{}, nullptr);
	if(true) {
		helper.traverse_conn_props(7, pred_defer_err<1, 7>{model}, nullptr);
		helper.traverse_same_root_props(8, pred_defer_err<3, 8>{model}, nullptr);
		helper.traverse_tree_props(9, pred_defer_err<5, 9>{model}, nullptr);
		helper.traverse_non_tree_props(10, pred_defer_err<7, 10>{model}, nullptr);
	}
	return helper.best_node;
}

gapr::node_id find_next_node(
		gapr::edge_model::view model,
		gapr::node_id cur_node,
		const gapr::node_attr& cur_pos,
		bool include_err,
		const std::unordered_set<gapr::node_id>& masked,
		const std::array<double, 6>& bbox) {
	traverse_helper helper{model, cur_pos};
	if(cur_node)
		helper.set_current(cur_node);
	auto filter=[&bbox,&masked](gapr::node_id id, const gapr::node_attr& attr) {
		if(!filter_next_pos(attr, bbox))
			return false;
		if(masked.find(id)!=masked.end())
			return false;
		return true;
	};
	if(include_err) {
		helper.traverse_conn_props(0, pred_empty_err{}, filter);
		helper.traverse_same_root_props(1, pred_empty_err{}, filter);
		helper.traverse_tree_props(2, pred_empty_err{}, filter);
		// ? mask_current?
	}

	helper.traverse_conn_verts(3, pred_empty_end{}, filter);
	// mask_curr???
	helper.traverse_conn_edges(3, pred_no_coverage{}, filter);

	helper.traverse_same_root_verts(4, pred_empty_end{}, filter);
	helper.traverse_same_root_edges(4, pred_no_coverage{}, filter);

	helper.traverse_tree_verts(5, pred_empty_end{}, filter);
	// skip connected?
	helper.traverse_tree_edges(5, pred_no_coverage{}, filter);
	// mask curr?
	if(include_err) {
		helper.traverse_non_tree_props(6, pred_empty_err{}, filter);
	}
	helper.traverse_non_tree_verts(7, pred_empty_end{}, filter);
	helper.traverse_non_tree_edges(7, pred_no_coverage{}, filter);
	return helper.best_node;
}


std::string get_graph_details(gapr::edge_model::view graph) {

	std::size_t nn_tot{0};
	std::size_t nn_npr{0};
	std::size_t nv_tot{0};
	std::size_t nv_nfix{0};

	std::vector<uint32_t> eg_npr{};
	std::vector<uint32_t> eg_nfix{};
	std::vector<uint32_t> eg_0deg{};
	std::vector<std::pair<gapr::node_id, std::string>> all_roots{};

	for(auto& [eid, edg]: graph.edges()) {
		if(edg.points.size()<=2)
			continue;
		nn_tot+=edg.points.size()-2;
		for(std::size_t i=1; i+1<edg.points.size(); i++) {
			gapr::node_attr a{edg.points[i]};
			if(!a.misc.coverage()) {
				if(nn_npr++<10)
					eg_npr.push_back(edg.nodes[i].data);
			}
		}
	}
	for(auto& [vid, vert]: graph.vertices()) {
		nn_tot+=1;
		if(!vert.attr.misc.coverage()) {
			if(nn_npr++<10)
				eg_npr.push_back(vid.data);
		}
		nv_tot+=1;
		do {
			if(vert.edges.size()<1) {
				if(eg_0deg.size()<10)
					eg_0deg.push_back(vid.data);
			}
			if(!pred_empty_end{}(graph, vid, vert))
				break;
			if(nv_nfix++<10)
				eg_nfix.push_back(vid.data);
		} while(false);
	}
	for(auto& [pid, val]: graph.props()) {
		if(pid.key=="root")
			all_roots.emplace_back(pid.node, val);
	}

	std::ostringstream oss;
	oss<<"Proofreading: "<<nn_npr<<'/'<<nn_tot<<"\nEg";
	for(auto n: eg_npr)
		oss<<", "<<n;
	oss<<'\n';
	oss<<"Terminal states: "<<nv_nfix<<'/'<<nv_tot<<"\nEg";
	for(auto n: eg_nfix)
		oss<<", "<<n;
	oss<<'\n';
	if(!eg_0deg.empty()) {
		oss<<"Zero-degree vertices";
		for(auto n: eg_0deg)
			oss<<", "<<n;
		oss<<'\n';
	}

	oss<<"Bad term annot:\n";
	for(auto& [pkey, pval]: graph.props()) {
		if(pkey.key=="state") {
			auto pos=graph.nodes().at(pkey.node);
			if(pos.edge) {
			} else if(pos.vertex) {
				auto& vert=graph.vertices().at(pos.vertex);
				if(vert.edges.size()<2)
					continue;
			} else {
			}
			oss<<'@'<<pkey.node.data<<'\n';
		}
	}

	oss<<'\n';
	oss<<"All roots (N="<<all_roots.size()<<"):\n";
	for(auto& [nid, name]: all_roots)
		oss<<nid<<'='<<name<<'\n';

	oss<<'\n';
	oss<<"All logs (N="<<graph.logs().size()<<"):\n";
	for(auto& log: graph.logs())
		oss<<log<<'\n';

	return oss.str();
}
