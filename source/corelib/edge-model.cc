#include "gapr/edge-model.hh"

#include "gapr/utility.hh"
#include "gapr/mem-file.hh"
#include "gapr/streambuf.hh"
#include "gapr/commit.hh"

#include <fstream>
#include <queue>
#include <charconv>

#include "../corelib/model-upgrade.hh"

constexpr static char ROOT_KEY[]{"root"};
//#define DEBUG_MODEL

template<typename T>
static unsigned int find_and_move_to_back(std::vector<T>& vec, const T& val) {
	unsigned int hits{0};
	if(vec.empty())
		return hits;
	auto i=vec.size()-1;
	auto& back=vec[i];
	if(back==val)
		hits++;
	while(i-->0) {
		if(vec[i]==val) {
			hits++;
			vec[i]=back;
			back=val;
		}
	}
	return hits;
};

enum VAL_STATE {
	PRE_ADD,
	PRE_DEL,
	PRE_CHG,
};

struct gapr::edge_model::PRIV {
	template<gapr::delta_type Typ>
		void load(std::istream& str) {
			gapr::delta<Typ> delta;
			if(!delta.load(str))
				gapr::report("failed to load delta");
#if 0
			if(!do_prepare<T>(model, std::move(delta)))
				gapr::report("failed to prepare");
#endif
		}

	static bool add_node(edge_model& model, gapr::node_id id, position pos) {
		auto [it, ins]=model._nodes_d.try_emplace(id, (int)PRE_ADD, pos);
		if(!ins) {
			if(it->second.first!=PRE_DEL)
				return false;
			it->second.second=pos;
			it->second.first=PRE_CHG;
		} else {
			if(model._nodes.find(id)!=model._nodes.end())
				return false;
		}
		return true;
	}
	static bool del_node(edge_model& model, gapr::node_id id) {
		auto [it, ins]=model._nodes_d.try_emplace(id, PRE_DEL, position{});
		if(!ins) {
			switch(it->second.first) {
				case PRE_DEL:
					return false;
				case PRE_ADD:
					model._nodes_d.erase(it);
					break;
				case PRE_CHG:
					it->second.first=PRE_DEL;
					break;
				default:
					assert(0);
			}
		} else {
			if(model._nodes.find(id)==model._nodes.end())
				return false;
		}
		return true;
	}
	static bool chg_node(edge_model& model, gapr::node_id id, position pos) {
		auto [it, ins]=model._nodes_d.try_emplace(id, PRE_CHG, pos);
		if(!ins) {
			if(it->second.first==PRE_DEL)
				return false;
			it->second.second=pos;
		} else {
			if(model._nodes.find(id)==model._nodes.end())
				return false;
		}
		return true;
	}
	static bool add_vert(edge_model& model, vertex_id id, vertex&& vert) {
		auto [it, ins]=model._vertices_d.try_emplace(id, PRE_ADD, std::move(vert));
		if(!ins) {
			if(it->second.first!=PRE_DEL)
				return false;
			it->second.second=std::move(vert);
			it->second.first=PRE_CHG;
		} else {
			if(model._vertices.find(id)!=model._vertices.end())
				return false;
		}
		return true;
	}
	static bool del_vert(edge_model& model, vertex_id id) {
		auto [it, ins]=model._vertices_d.try_emplace(id, PRE_DEL, vertex{nullptr});
		if(!ins) {
			switch(it->second.first) {
				case PRE_DEL:
					return false;
				case PRE_ADD:
					model._vertices_d.erase(it);
					break;
				case PRE_CHG:
					it->second.first=PRE_DEL;
					break;
				default:
					assert(0);
			}
		} else {
			if(model._vertices.find(id)==model._vertices.end())
				return false;
		}
		return true;
	}
	static vertex* chg_vert(edge_model& model, vertex_id id) {
		auto [it, ins]=model._vertices_d.try_emplace(id, PRE_CHG, vertex{nullptr});
		if(!ins) {
			if(it->second.first==PRE_DEL)
				return nullptr;
			return &it->second.second;
		}
		auto it2=model._vertices.find(id);
		if(it2==model._vertices.end())
			return nullptr;
		it->second.second=it2->second;
		return &it->second.second;
	}
	static edge* chg_edge(edge_model& model, edge_id id) {
		auto it=model._edges_d.find(id);
		if(it!=model._edges_d.end()) {
			if(it->second.first!=PRE_ADD)
				return nullptr;
			return &it->second.second;
		}
		auto it2=model._edges.find(id);
		if(it2==model._edges.end())
			return nullptr;
		auto edg=&it2->second;

		auto vid0=edg->left;
		auto vert0=chg_vert(model, vid0);
		if(!vert0)
			return nullptr;
		auto vid1=edg->right;
		auto vert1=chg_vert(model, vid1);
		if(!vert1)
			return nullptr;
		edge edg1{vid0, vid1};
		copy_nodes(edg1, *edg, 0, edg->nodes.size());
		auto eid1=add_edge(model, std::move(edg1));
		if(!eid1)
			return nullptr;
		if(find_and_move_to_back(vert0->edges, std::pair{id, false})!=1)
			return nullptr;
		vert0->edges.back().first=eid1;
		if(find_and_move_to_back(vert1->edges, std::pair{id, true})!=1)
			return nullptr;
		vert1->edges.back().first=eid1;
		auto& nodes=edg->nodes;
		for(uint32_t i=1; i+1<edg->nodes.size(); i++) {
			if(!chg_node(model, gapr::node_id{nodes[i]}, position{eid1, i*128}))
				return nullptr;
		}
		if(!del_edge(model, id))
			return nullptr;
		return &model._edges_d.at(eid1).second;
	}
	static const vertex* get_vert(edge_model& model, vertex_id id) {
		auto it=model._vertices_d.find(id);
		if(it!=model._vertices_d.end()) {
			if(it->second.first==PRE_DEL)
				return nullptr;
			return &it->second.second;
		}
		auto it2=model._vertices.find(id);
		if(it2!=model._vertices.end())
			return &it2->second;
		return nullptr;
	}
	static const std::string* get_prop(edge_model& model, prop_id id) {
		auto it=model._props_d.find(id);
		if(it!=model._props_d.end()) {
			if(it->second.first==PRE_DEL)
				return nullptr;
			return &it->second.second;
		}
		auto it2=model._props.find(id);
		if(it2!=model._props.end())
			return &it2->second;
		return nullptr;
	}
	static edge_id alloc_edge_id(edge_model& model) {
		if(!model._eid_avail.empty()) {
			auto id=model._eid_avail.back();
			model._eid_avail.pop_back();
			return id;
		}
		return ++model._eid_alloc;
	}
	static void free_edge_id(edge_model& model, edge_id id) {
		model._eid_free.push_back(id);
	}
	static edge_id add_edge(edge_model& model, edge&& edg) {
		auto id=alloc_edge_id(model);
		auto [it, ins]=model._edges_d.try_emplace(id, PRE_ADD, std::move(edg));
		if(!ins) {
			return 0;
		} else {
			if(model._edges.find(id)!=model._edges.end())
				return 0;
		}
		return id;
	}
	static bool del_edge(edge_model& model, edge_id id) {
		auto [it, ins]=model._edges_d.try_emplace(id, PRE_DEL, edge{nullptr});
		if(!ins) {
			if(it->second.first==PRE_DEL)
				return false;
			model._edges_d.erase(it);
		} else {
			if(model._edges.find(id)==model._edges.end())
				return false;
		}
		free_edge_id(model, id);
		return true;
	}
	static bool add_prop(edge_model& model, prop_id&& id, std::string&& prop) {
		auto [it, ins]=model._props_d.try_emplace(id, PRE_ADD, std::move(prop));
		if(!ins) {
			if(it->second.first!=PRE_DEL)
				return false;
			it->second.second=std::move(prop);
			it->second.first=PRE_CHG;
		} else {
			if(model._props.find(id)!=model._props.end())
				return false;
		}
		return true;
	}
	static bool add_log(edge_model& model, std::string&& log) {
		model._logs_d.push_back(std::move(log));
		return true;
	}
	static bool del_prop(edge_model& model, prop_id&& id) {
		auto [it, ins]=model._props_d.try_emplace(id, PRE_DEL, std::string{});
		if(!ins) {
			switch(it->second.first) {
				case PRE_DEL:
					return false;
				case PRE_ADD:
					model._props_d.erase(it);
					break;
				case PRE_CHG:
					it->second.first=PRE_DEL;
					break;
				default:
					assert(0);
			}
		} else {
			if(model._props.find(id)==model._props.end())
				return false;
		}
		return true;
	}
	static bool chg_prop(edge_model& model, prop_id&& id, std::string&& prop) {
		auto [it, ins]=model._props_d.try_emplace(id, PRE_CHG, std::move(prop));
		if(!ins) {
			if(it->second.first==PRE_DEL)
				return false;
			it->second.second=std::move(prop);
		} else {
			if(model._props.find(id)==model._props.end())
				return false;
		}
		return true;
	}

	static vertex_id gen_vert(edge_model& model, gapr::node_id id, gapr::node_attr node) {
		if(!add_node(model, id, position{id}))
			return {};
		if(!add_vert(model, id, vertex{node, {}}))
			return {};
		return id;
	}
	static const edge* get_edge(edge_model& model, edge_id id) {
		auto it=model._edges_d.find(id);
		if(it!=model._edges_d.end()) {
			if(it->second.first!=PRE_ADD)
				return nullptr;
			return &it->second.second;
		}
		auto it2=model._edges.find(id);
		if(it2!=model._edges.end())
			return &it2->second;
		return nullptr;
	}
	static void copy_nodes(edge& edg0, const edge& edg, std::size_t i0, std::size_t i1) {
		auto n=i1-i0;
		edg0.points.reserve(n);
		edg0.nodes.reserve(n);
		for(std::size_t i=i0; i<i1; i++) {
			edg0.points.push_back(edg.points[i]);
			edg0.nodes.push_back(edg.nodes[i]);
		}
	}
	static vertex_id gen_vert_split(edge_model& model, edge_id eid, uint32_t idx) {
		auto edg=get_edge(model, eid);
		if(!edg)
			return {};
		assert(idx>0 && idx+1<edg->nodes.size());
		auto vid0=edg->left;
		auto vert0=chg_vert(model, vid0);
		if(!vert0)
			return {};
		auto vid1=edg->right;
		auto vert1=chg_vert(model, vid1);
		if(!vert1)
			return {};
		vertex_id vid=edg->nodes[idx];

		edge edg0{vid0, vid};
		copy_nodes(edg0, *edg, 0, idx+1);
		auto eid0=add_edge(model, std::move(edg0));
		if(!eid0)
			return {};
		edge edg1{vid, vid1};
		copy_nodes(edg1, *edg, idx, edg->points.size());
		auto eid1=add_edge(model, std::move(edg1));
		if(!eid1)
			return {};

		vertex vert{gapr::node_attr{edg->points[idx]}, {{eid0, true}, {eid1, false}}};
		if(!add_vert(model, vid, std::move(vert)))
			return {};

		if(find_and_move_to_back(vert0->edges, std::pair{eid, false})!=1)
			return {};
		vert0->edges.back().first=eid0;
		if(find_and_move_to_back(vert1->edges, std::pair{eid, true})!=1)
			return {};
		vert1->edges.back().first=eid1;

		auto& nodes=edg->nodes;
		for(uint32_t i=1; i<idx; i++) {
			if(!chg_node(model, gapr::node_id{nodes[i]}, position{eid0, i*128}))
				return {};
		}
		if(!chg_node(model, gapr::node_id{nodes[idx]}, position{vid}))
			return {};
		for(uint32_t i=idx+1; i+1<nodes.size(); i++) {
			if(!chg_node(model, gapr::node_id{nodes[i]}, position{eid1, (i-idx)*128}))
				return {};
		}

		if(!del_edge(model, eid))
			return {};
		return vid;
	}

	static bool rm_subedge(edge_model& model, edge_id eid, uint32_t idx0, uint32_t idx1) {
		auto edg=get_edge(model, eid);
		if(!edg)
			return false;
		assert(idx0<idx1 && idx1<edg->nodes.size());
		auto vid0=edg->left;
		auto vert0=chg_vert(model, vid0);
		if(!vert0)
			return false;
		auto vid1=edg->right;
		auto vert1=chg_vert(model, vid1);
		if(!vert1)
			return false;
		vertex_id vida{0};
		edge_id eid0{0};
		if(idx0>0) {
			vida=edg->nodes[idx0];
			edge edg0{vid0, vida};
			copy_nodes(edg0, *edg, 0, idx0+1);
			eid0=add_edge(model, std::move(edg0));
			if(!eid0)
				return false;
			vertex vert{gapr::node_attr{edg->points[idx0]}, {{eid0, true}}};
			if(!add_vert(model, vida, std::move(vert)))
				return false;
		}
		vertex_id vidb{0};
		edge_id eid1{0};
		if(idx1+1<edg->nodes.size()) {
			vidb=edg->nodes[idx1];
			edge edg1{vidb, vid1};
			copy_nodes(edg1, *edg, idx1, edg->points.size());
			eid1=add_edge(model, std::move(edg1));
			if(!eid1)
				return false;
			vertex vert{gapr::node_attr{edg->points[idx1]}, {{eid1, false}}};
			if(!add_vert(model, vidb, std::move(vert)))
				return false;
		}
		if(find_and_move_to_back(vert0->edges, std::pair{eid, false})!=1)
			return false;
		if(vida) {
			vert0->edges.back().first=eid0;
		} else {
			vert0->edges.pop_back();
		}
		if(find_and_move_to_back(vert1->edges, std::pair{eid, true})!=1)
			return false;;
		if(vidb) {
			vert1->edges.back().first=eid1;
		} else {
			vert1->edges.pop_back();
		}

		auto& nodes=edg->nodes;
		for(uint32_t i=1; i<idx0; i++) {
			if(!chg_node(model, gapr::node_id{nodes[i]}, position{eid0, i*128}))
				return false;
		}
		if(vida) {
			if(!chg_node(model, gapr::node_id{nodes[idx0]}, position{vida}))
				return false;
		}
		for(uint32_t i=idx0+1; i<idx1; i++) {
			if(!del_node(model, gapr::node_id{nodes[i]}))
				return false;
		}
		if(vidb) {
			if(!chg_node(model, gapr::node_id{nodes[idx1]}, position{vidb}))
				return false;
		}
		for(uint32_t i=idx1+1; i+1<nodes.size(); i++) {
			//i>idx1 i+1<s
			if(!chg_node(model, gapr::node_id{nodes[i]}, position{eid1, (i-idx1)*128}))
				return false;
		}
		/////////////////////////
		/////////////
		if(!del_edge(model, eid))
			return false;
		return true;
	}

	static position get_position(edge_model& model, gapr::node_id::data_type id) {
		auto it=model._nodes_d.find(gapr::node_id{id});
		if(it!=model._nodes_d.end()) {
			if(it->second.first==PRE_DEL)
				return {};
			return it->second.second;
		}
		auto it2=model._nodes.find(gapr::node_id{id});
		if(it2==model._nodes.end())
			return {};
		return it2->second;
	}
	static vertex_id gen_vert(edge_model& model, gapr::link_id link, gapr::node_id::data_type nid, gapr::node_attr node) {
		if(!link)
			return gen_vert(model, gapr::node_id{nid}, node);
		if(!link.on_node()) {
			//XXX link
			//do_rm_link(...);
			//do_add_node(...);
			//do_add_link();
			//do_add_link();
			return {};
		} else {
			auto pos=get_position(model, link.nodes[0].data);
			if(!pos.edge) {
				if(!pos.vertex)
					return {};
				return pos.vertex;
			}
			return gen_vert_split(model, pos.edge, pos.index/128);
		}
	}
	static bool gen_edge_maybe_loop(edge_model& model, vertex_id vid0, vertex_id vid1, std::vector<point>&& points, std::vector<node_id>&& nodes) {
		if(vid0!=vid1)
			return gen_edge(model, vid0, vid1, std::move(points), std::move(nodes));
		assert(nodes.size()>=2);
		if(nodes.size()==2) {
			gapr::print(1, "**** skipped x-x loop, at ", vid0.data);
			return true;
		}
		if(nodes.size()==3) {
			auto n1=nodes[1];
			assert(n1!=vid0);
			gapr::print(1, "**** fixed x-y-x loop, at ", vid0.data, ' ', n1.data);
			auto vn1=gen_vert(model, link_id{}, n1.data, gapr::node_attr{points[1]});
			if(!vn1)
				return false;
			points.pop_back();
			nodes.pop_back();
			return gen_edge(model, vid0, vn1, std::move(points), std::move(nodes));
		}
		std::size_t n1idx=nodes.size()-2;
		auto n1=nodes[n1idx];
		assert(n1!=vid0);
		gapr::print(1, "**** fixed long loop 4+, ", vid0.data, ' ', n1.data);
		auto vn1=gen_vert(model, link_id{}, n1.data, gapr::node_attr{points[n1idx]});
		if(!vn1)
			return false;
		if(!gen_edge(model, vn1, vid1, {points[n1idx], points[n1idx+1]}, {n1, vid1}))
			return false;
		points.pop_back();
		nodes.pop_back();
		return gen_edge(model, vid0, vn1, std::move(points), std::move(nodes));
	}
	static bool gen_edge(edge_model& model, vertex_id vid0, vertex_id vid1, std::vector<point>&& points, std::vector<node_id>&& nodes) {
		assert(vid0!=vid1);
		auto vert0=chg_vert(model, vid0);
		if(!vert0)
			return false;
		auto vert1=chg_vert(model, vid1);
		if(!vert1)
			return false;

		// XXX fix ends??????
		auto n=nodes.size();
		auto p=&nodes[0];
		edge edg{vid0, vid1};
		points.front()=vert0->attr.data();
		nodes.front()=vid0;
		gapr::node_attr attr{vert1->attr};
		auto misc0=attr.misc.cannolize();
		auto misc1=gapr::misc_attr{};
		attr.misc.data=misc0.data|misc1.data;
		points.back()=attr.data();
		nodes.back()=vid1;
		edg.points=std::move(points);
		edg.nodes=std::move(nodes);

		auto eid=add_edge(model, std::move(edg));
		if(!eid)
			return false;

		vert0->edges.emplace_back(eid, false);
		vert1->edges.emplace_back(eid, true);

		for(uint32_t i=1; i<n-1; i++) {
			gapr::node_id id{p[i]};
			if(!add_node(model, id, position{eid, i*128}))
				return false;
		}
		return true;
	}
	static bool gen_edge(edge_model& model, vertex_id vid0, vertex_id vid1, const std::vector<point>& _points, gapr::node_id nid0) {
		if(vid0==vid1 && _points.size()==2) {
			gapr::print(1, "**** skipped x-x loop, at ", vid0.data);
			return true;
		}
		assert(vid0!=vid1);
		auto vert0=chg_vert(model, vid0);
		if(!vert0)
			return false;
		auto vert1=chg_vert(model, vid1);
		if(!vert1)
			return false;

		std::vector<point> points;
		std::vector<node_id> nodes;
		auto n=_points.size();
		points.reserve(n);
		nodes.reserve(n);
		points.push_back(vert0->attr.data());
		nodes.push_back(vid0);
		for(std::size_t i=1; i<n-1; i++) {
			points.push_back(_points[i]);
			nodes.push_back(nid0.offset(i));
		}
		gapr::node_attr attr{vert1->attr};
		auto misc0=attr.misc.cannolize();
		auto misc1=gapr::misc_attr{};
		attr.misc.data=misc0.data|misc1.data;
		points.push_back(attr.data());
		nodes.push_back(vid1);
		return gen_edge(model, vid0, vid1, std::move(points), std::move(nodes));
	}
	static bool try_merge(edge_model& model);
	static bool update_aux(edge_model& model, std::unordered_set<gapr::node_id>&& trees_changed);
	static bool apply(edge_model& model) {
		gapr::print("apply chk0");
		if(!try_merge(model))
			return false;
		gapr::print("mergered");
		for(auto [id, chg]: model._nodes_d) {
			switch(chg.first) {
				case PRE_ADD:
					if(auto [it, ins]=model._nodes.try_emplace(id, chg.second); ins)
						break;
					else {
						gapr::print("node: ", id.data, ' ', it->second.edge, ',', it->second.index, ',', chg.second.edge, ',', chg.second.index);
					}
					gapr::print("apply err 0");
					return false;
				case PRE_DEL:
					if(auto it=model._nodes.find(id); it!=model._nodes.end()) {
						model._nodes.erase(it);
						break;
					}
					gapr::print("apply err 1");
					return false;
				case PRE_CHG:
					if(auto it=model._nodes.find(id); it!=model._nodes.end()) {
						it->second=chg.second;
						break;
					}
					gapr::print("apply err 2");
					return false;
				default:
					return false;
			}
		}
		std::unordered_set<gapr::node_id> trees_changed;
		gapr::print("apply chk1");
		for(auto& [id, chg]: model._vertices_d) {
			switch(chg.first) {
				case PRE_ADD:
					trees_changed.emplace(chg.second.root);
					if(auto [it, ins]=model._vertices.try_emplace(id, std::move(chg.second)); ins)
						break;
					return false;
				case PRE_DEL:
					if(auto it=model._vertices.find(id); it!=model._vertices.end()) {
						trees_changed.emplace(it->second.root);
						model._vertices.erase(it);
						break;
					}
					return false;
				case PRE_CHG:
					trees_changed.emplace(chg.second.root);
					if(auto it=model._vertices.find(id); it!=model._vertices.end()) {
						trees_changed.emplace(it->second.root);
						it->second=std::move(chg.second);
						break;
					}
					return false;
				default:
					return false;
			}
		}
		gapr::print("apply chk2");
		for(auto& [id, chg]: model._edges_d) {
			switch(chg.first) {
				case PRE_ADD:
					trees_changed.emplace(chg.second.root);
					if(auto [it, ins]=model._edges.try_emplace(id, std::move(chg.second)); ins)
						break;
					return false;
				case PRE_DEL:
					if(auto it=model._edges.find(id); it!=model._edges.end()) {
						trees_changed.emplace(it->second.root);
						model._edges_del.push_back(id);
						model._edges.erase(it);
						break;
					}
					return false;
				default:
					return false;
			}
		}
		gapr::print("apply chk3");
		for(auto& [id, chg]: model._props_d) {
			switch(chg.first) {
				case PRE_ADD:
					if(auto [it, ins]=model._props.try_emplace(id, std::move(chg.second)); ins) {
						if(id.key==ROOT_KEY) {
							trees_changed.emplace(id.node);
							model._trees_add.emplace_back(id.node, it->second);
						}
						break;
					}
					return false;
				case PRE_DEL:
					if(auto it=model._props.find(id); it!=model._props.end()) {
						if(id.key==ROOT_KEY) {
							trees_changed.emplace(id.node);
							model._trees_del.push_back(gapr::node_id{id.node});
						}
						model._props.erase(it);
						break;
					}
					return false;
				case PRE_CHG:
					if(auto it=model._props.find(id); it!=model._props.end()) {
						if(id.key==ROOT_KEY) {
							trees_changed.emplace(id.node);
							model._trees_chg.emplace_back(id.node, chg.second);
						}
						it->second=std::move(chg.second);
						break;
					}
					return false;
				default:
					return false;
			}
		}
		for(auto& log: model._logs_d)
			model._logs.push_back(std::move(log));
		gapr::print("apply chk4");
		while(!model._eid_free.empty()) {
			auto id=model._eid_free.back();
			model._eid_free.pop_back();
			model._eid_avail.push_back(id);
		}
		if(model._filter_stat) {
			gapr::print(1, "apply chk5 upd filter");
			if(model._filter_stat==1) {
				model._edges_filter.clear();
				model._vertices_filter.clear();
				model._props_filter.clear();
			}
			for(auto [id, chg]: model._vertices_filter_d) {
				switch(chg) {
				case PRE_ADD:
					if(auto [it, ins]=model._vertices_filter.insert(id); ins)
						break;
					return false;
				case PRE_DEL:
					if(auto it=model._vertices_filter.find(id); it!=model._vertices_filter.end()) {
						model._vertices_filter.erase(it);
						break;
					}
					return false;
				default:
					return false;
				}
			}
			for(auto [id, chg]: model._edges_filter_d) {
				switch(chg) {
					case PRE_ADD:
						if(auto [it, ins]=model._edges_filter.emplace(id); ins)
							break;
						return false;
					case PRE_DEL:
						gapr::print(1, "del edge filter: ", id);
						if(auto it=model._edges_filter.find(id); it!=model._edges_filter.end()) {
							model._edges_filter.erase(it);
							break;
						}
						return false;
					default:
						return false;
				}
			}
			for(auto& [id, chg]: model._props_filter_d) {
				switch(chg) {
				case PRE_ADD:
					if(auto [it, ins]=model._props_filter.emplace(id); ins)
						break;
					return false;
				case PRE_DEL:
					if(auto it=model._props_filter.find(id); it!=model._props_filter.end()) {
						model._props_filter.erase(it);
						break;
					}
					return false;
				default:
					return false;
				}
			}
		}
		if(!update_aux(model, std::move(trees_changed)))
			return false;
		return true;
	}
	static bool filter_edges(gapr::edge_model& model, const std::array<gapr::node_attr, 2>& pts);
	static bool filter_vertices(gapr::edge_model& model, const std::array<gapr::node_attr, 2>& pts);
	static bool filter_props(gapr::edge_model& model, const std::array<gapr::node_attr, 2>& pts);
	static bool highlight_extend_and_finish(gapr::edge_model& model, std::deque<gapr::node_id>& todo, std::unordered_set<vertex_id>& verts, bool allow_loop);
	static bool highlight_finish(gapr::edge_model& model, std::unordered_set<vertex_id>& verts);
};

template<>
GAPR_CORE_DECL bool gapr::edge_model::load(gapr::node_id nid0, gapr::delta<gapr::delta_type::add_edge_>&& delta) {
	auto& nodes=delta.nodes;
	if(nodes.size()<2)
		return false;

	gapr::print("apply chk0");
	gapr::link_id left{delta.left};
	if(left.cannolize()!=left)
		return false;
	gapr::print("apply chk1");
	gapr::link_id right{delta.right};
	if(right.cannolize()!=right)
		return false;
	gapr::print("apply chk2");

	auto vert_left=PRIV::gen_vert(*this, left, nid0.data, gapr::node_attr{nodes.front()});
	if(!vert_left)
		return false;
	gapr::print("apply chk3");
	if(vert_left.data!=nid0.data)
		nid0.data--;
	auto vert_right=PRIV::gen_vert(*this, right, nid0.offset(nodes.size()-1).data, gapr::node_attr{nodes.back()});
	if(!vert_right)
		return false;
	gapr::print("apply chk4");
	return PRIV::gen_edge(*this, vert_left, vert_right, delta.nodes, nid0);
	//////////////////

	//////////////////////////
	///////////////////////////////////////////////
#if 0
	double x=120, y=120, z=120;
	auto gen_vert=[this](vertex_id id, node_attr attr) {
		auto ins1=_vertices.emplace(id, vertex{attr, {}});
		_nodes.emplace(id, position{id});
		return ins1.first;
	};
	auto gen_edge=[this](edge_id id, auto it_vert1, auto it_vert2) {
		auto& vert1=it_vert1->second;
		auto& vert2=it_vert2->second;
		auto x0=vert1.attr.pos(0);
		auto y0=vert1.attr.pos(1);
		auto z0=vert1.attr.pos(2);
		auto x1=vert2.attr.pos(0);
		auto y1=vert2.attr.pos(1);
		auto z1=vert2.attr.pos(2);
		std::vector<point> pts;
		std::vector<node_id::data_type> nodes;
		pts.reserve(11);
		nodes.reserve(11);
		pts.push_back(vert1.attr);
		nodes.push_back(it_vert1->first);

		for(std::size_t i=1; i<5; i++) {
			auto d=std::sin(2*M_PI*(i/5.0))*10;
			gapr::node_attr pt{x0+i*(x1-x0)/5, y0+i*(y1-y0)/5, z0+i*(z1-z0)/5+d};
			pts.push_back(pt);
			nodes.push_back(it_vert1->first+i);
		}
		pts.push_back(vert2.attr);
		nodes.push_back(it_vert2->first);
		for(unsigned int i=0; i<nodes.size(); i++) {
			_nodes.emplace(nodes[i], position{id, i});
		}
		auto ins1=_edges.emplace(id, edge{it_vert1->first, it_vert2->first, nodes, pts});
		return ins1;
	};
	auto vert1=gen_vert(1, gapr::node_attr{x-100, y, z});
	auto vert2=gen_vert(100, gapr::node_attr{x, y, z});
	auto vert3=gen_vert(200, gapr::node_attr{x, y+100, z});
	auto vert4=gen_vert(201, gapr::node_attr{x+100, y, z});

	gen_edge(1, vert1, vert2);
	gen_edge(2, vert2, vert3);
	_props.emplace(prop_id{vert3->first, "a"}, "xxx=yyy");
	_props.emplace(prop_id{vert4->first, "x"}, "zzz=st");
#endif
}

template<>
GAPR_CORE_DECL bool gapr::edge_model::load(gapr::node_id nid0, gapr::delta<gapr::delta_type::add_prop_>&& delta) {
	gapr::link_id link{delta.link};
	if(link.cannolize()!=link)
		return false;

	std::string key{delta.prop};
	auto i=key.find('=');
	std::string val{};
	if(i!=std::string::npos) {
		val.assign(key, i+1);
		key.erase(i);
	}

	gapr::node_id nid;
	if(!link) {
		nid=PRIV::gen_vert(*this, nid0, gapr::node_attr{delta.node});
	} else {
		bool is_root=(key==ROOT_KEY);
		if(!link.on_node()) {
#if 0
			//link
			if(is_root) {
				//*gen link vert
			} else {
				//*gen link node
				//do not split and merge
			}
#endif
			return false;
		} else {
			if(is_root) {
				auto pos=PRIV::get_position(*this, link.nodes[0].data);
				if(!pos.edge) {
					if(!pos.vertex)
						return {};
					nid=pos.vertex;
				} else {
					nid=PRIV::gen_vert_split(*this, pos.edge, pos.index/128);
				}
			} else {
				nid=link.nodes[0];
			}
		}
	}

	if(!nid)
		return false;

	gapr::prop_id id{nid, std::move(key)};
	return PRIV::add_prop(*this, std::move(id), std::move(val));
}

template<>
GAPR_CORE_DECL bool gapr::edge_model::load(gapr::node_id nid0, gapr::delta<gapr::delta_type::chg_prop_>&& delta) {
	if(delta.node==0)
		return false;

	std::string key{delta.prop};
	auto i=key.find('=');
	std::string val{};
	if(i!=std::string::npos) {
		val.assign(key, i+1);
		key.erase(i);
	}

	gapr::node_id nid{delta.node};
	gapr::prop_id id{nid, std::move(key)};
	return PRIV::chg_prop(*this, std::move(id), std::move(val));
}
void gapr::edge_model::gen_(double x, double y, double z) {
}
bool gapr::edge_model::updater::apply() {
	return PRIV::apply(const_cast<edge_model&>(_model));
}
gapr::edge_model::updater::~updater() {
	auto& model=const_cast<edge_model&>(_model);
	model._edges_del.clear();
	model._trees_del.clear();
	model._trees_add.clear();
	model._trees_chg.clear();
	model._nodes_d.clear();
	model._vertices_d.clear();
	model._edges_d.clear();
	model._props_d.clear();
	model._logs_d.clear();
	model._nid0={};
	model._filter_stat=0;
	model._edges_filter_d.clear();
	model._vertices_filter_d.clear();
	model._props_filter_d.clear();
	model.release_write(std::move(_lock));
}

struct EdgePathItem {
	const gapr::edge_model::edge* edg;
	const gapr::edge_model::vertex* vert;
	gapr::edge_model::edge_id eid;
	bool inv;
};
static bool check_links(std::deque<EdgePathItem>& path, std::size_t k) {
	gapr::misc_attr attr[2];
	for(unsigned int i=0; i<2; i++) {
		auto item=path[k+i];
		if(!i)
			item.inv=!item.inv;
		gapr::node_attr n{item.edg->points[item.inv?(item.edg->points.size()-1):1]};
		attr[i]=gapr::misc_attr{};
	}
	return attr[0]==attr[1];
}
static bool check_loop(std::deque<EdgePathItem>& path) {
	auto item0=path.front();
	auto v0=item0.inv?item0.edg->right:item0.edg->left;
	auto item1=path.back();
	auto v1=item1.inv?item1.edg->left:item1.edg->right;
#ifdef DEBUG_MODEL
	{
		std::ostringstream oss;
		oss<<v0.data;
		for(auto item: path) {
			auto v1=item.inv?item.edg->left:item.edg->right;
			oss<<" "<<v1.data;
		}
		gapr::print("----------", oss.str());
	}
#endif
	return v0!=v1;
}

bool gapr::edge_model::PRIV::try_merge(edge_model& model) {
	gapr::print(1, "begin try_merge");
	std::vector<gapr::node_id> todo;
	for(auto& [id, chg]: model._vertices_d) {
		switch(chg.first) {
		case PRE_ADD:
		case PRE_CHG:
			if(chg.second.edges.size()!=2)
				break;
			if(get_prop(model, prop_id{id, ROOT_KEY}))
				break;
			todo.push_back(id);
			break;
		case PRE_DEL:
			break;
		default:
			return false;
		}
	}
	std::vector<gapr::node_id> todo_p;
	for(auto& [id, chg]: model._props_d) {
		if(id.key==ROOT_KEY)
			todo_p.push_back(id.node);
	}
	std::deque<EdgePathItem> path{};
	do {
#if 0
		{
			std::ostringstream oss;
			oss<<"**********\n";
			for(auto& [vid, vert]: model._vertices_d) {
				for(auto p: vert.second.edges) {
					oss<<vert.first<<": "<<vid.data<<"- "<<p.first<<':'<<p.second<<'\n';
				}
			}
			for(auto& [eid, edg]: model._edges_d) {
				oss<<edg.first<<": "<<edg.second.left.data<<" <"<<eid<<"> "<<edg.second.right.data<<'\n';
			}
			oss<<"/*********\n";
			gapr::print(oss.str());
		}
#endif

		while(!todo.empty()) {
			auto id=todo.back();
			todo.pop_back();
			auto it_vert=model._vertices_d.find(id);
			if(it_vert==model._vertices_d.end())
				continue;
			auto& chg=it_vert->second;
			switch(chg.first) {
				case PRE_ADD:
				case PRE_CHG:
					assert(chg.second.edges.size()==2);
					assert(!get_prop(model, prop_id{id, ROOT_KEY}));
					{
						auto [eid, r]=chg.second.edges[0];
						path.push_back(EdgePathItem{get_edge(model, eid), nullptr, eid, !r});
					}
					{
						auto [eid, r]=chg.second.edges[1];
						path.push_back(EdgePathItem{get_edge(model, eid), &chg.second, eid, r});
					}
					break;
				case PRE_DEL:
					break;
				default:
					return false;
			}
			if(!path.empty()) {
				if(check_links(path, 0) && check_loop(path))
					break;
				path.clear();
			}
		}
		while(path.empty() && !todo_p.empty()) {
			auto id=todo_p.back();
			todo_p.pop_back();
			auto vert=get_vert(model, id);
			if(!vert)
				continue;
			if(vert->edges.size()!=2)
				continue;
			if(get_prop(model, prop_id{id, ROOT_KEY}))
				continue;
			{
				auto [eid, r]=vert->edges[0];
				path.push_back(EdgePathItem{get_edge(model, eid), nullptr, eid, !r});
			}
			{
				auto [eid, r]=vert->edges[1];
				path.push_back(EdgePathItem{get_edge(model, eid), vert, eid, r});
			}
			if(check_links(path, 0) && check_loop(path))
				break;
			path.clear();
		}
		if(path.empty())
			break;


		std::size_t nnodes=path[0].edg->points.size()+path[1].edg->points.size()-1;
		do {
			auto item=path.front();
			auto vid=item.inv?item.edg->right:item.edg->left;
			auto vert=get_vert(model, vid);
			if(vert->edges.size()!=2)
				break;
			if(get_prop(model, prop_id{vid, ROOT_KEY}))
				break;
			auto [eid1, r1]=vert->edges[0];
			if(item.eid==eid1)
				std::tie(eid1, r1)=vert->edges[1];
			path.front().vert=vert;
			path.push_front(EdgePathItem{get_edge(model, eid1), nullptr, eid1, !r1});
			if(!check_links(path, 0) || !check_loop(path)) {
				path.pop_front();
				break;
			}
			nnodes+=path.front().edg->points.size()-1;
		} while(true);
		do {
			auto item=path.back();
			auto vid=item.inv?item.edg->left:item.edg->right;
			auto vert=get_vert(model, vid);
			if(vert->edges.size()!=2)
				break;
			if(get_prop(model, prop_id{vid, ROOT_KEY}))
				break;
			auto [eid1, r1]=vert->edges[0];
			if(item.eid==eid1)
				std::tie(eid1, r1)=vert->edges[1];
			path.push_back(EdgePathItem{get_edge(model, eid1), vert, eid1, r1});
			if(!check_links(path, path.size()-2) || !check_loop(path)) {
				path.pop_back();
				break;
			}
			nnodes+=path.back().edg->points.size()-1;
		} while(true);

		auto item0=path.front();
		auto vid0=item0.inv?item0.edg->right:item0.edg->left;
		auto vert0=chg_vert(model, vid0);
		if(!vert0)
			return false;
		auto item1=path.back();
		auto vid1=item1.inv?item1.edg->left:item1.edg->right;
		auto vert1=chg_vert(model, vid1);
		if(!vert1)
			return false;

#ifdef DEBUG_MODEL
		{
			std::ostringstream oss;
			oss<<vid0.data;
			for(auto item: path) {
				auto v1=item.inv?item.edg->left:item.edg->right;
				oss<<" "<<v1.data;
			}
			oss<<" *"<<vid1.data;
			gapr::print("*******", oss.str());
		}
#endif


		edge edgx{vid0, vid1};
		edgx.points.reserve(nnodes);
		edgx.nodes.reserve(nnodes);

		path.front().vert=vert0;
		auto nid_prev{vid0};
		gapr::misc_attr lattr{};
		for(std::size_t idx=0; idx<path.size(); idx++) {
			auto item=path[idx];
			gapr::node_attr attr{item.vert->attr};
			edgx.nodes.push_back(nid_prev);
			attr.misc.data=attr.misc.cannolize().data|lattr.data;
			edgx.points.push_back(attr.data());
			auto n=item.edg->points.size();
			if(item.inv) {
				lattr=gapr::misc_attr{};
				for(std::size_t i=n-2; i>0; i--) {
					attr=gapr::node_attr{item.edg->points[i]};
					auto lattr_s=gapr::misc_attr{};
					attr.misc.data=attr.misc.cannolize().data|lattr.data;
					edgx.nodes.push_back(item.edg->nodes[i]);
					edgx.points.push_back(attr.data());
					lattr=lattr_s;
				}
				nid_prev=item.edg->nodes[0];
			} else {
				for(std::size_t i=1; i<n-1; i++) {
					edgx.nodes.push_back(item.edg->nodes[i]);
					edgx.points.push_back(item.edg->points[i]);
				}
				nid_prev=item.edg->nodes[n-1];
				lattr=gapr::misc_attr{};
			}
			if(!del_edge(model, item.eid))
				return false;
			if(idx>0) {
				auto item=path[idx];
				auto vid=item.inv?item.edg->right:item.edg->left;
				if(!del_vert(model, vid))
					return false;
			}
		}
		{
			auto attr=vert1->attr;
			edgx.nodes.push_back(nid_prev);
			attr.misc.data=attr.misc.cannolize().data|lattr.data;
			edgx.points.push_back(attr.data());
		}

		nnodes=edgx.nodes.size();
		auto pnodes=edgx.nodes.data();
		auto eidx=add_edge(model, std::move(edgx));
		if(!eidx)
			return false;

		std::size_t hit0=0, hit1=0;
		for(auto& p: vert0->edges) {
			if(p.first==path.front().eid && p.second==path.front().inv) {
				p.first=eidx;
				p.second=false;
				hit0++;
			}
		}
		for(auto& p: vert1->edges) {
			if(p.first==path.back().eid && p.second==!path.back().inv) {
				p.first=eidx;
				p.second=true;
				hit1++;
			}
		}
		if(hit0!=1 || hit1!=1)
			return false;

		for(uint32_t i=2; i<nnodes; i++) {
			gapr::node_id id{pnodes[i-1]};
			if(!chg_node(model, id, position{eidx, (i-1)*128}))
				return false;
		}

		path.clear();
	} while(true);
	gapr::print(1, "end try_merge");
	return true;
}

static int compare_tag(const std::string& a, const std::string& b) noexcept {
	for(std::size_t i=0; ; i++) {
		int aa=i<a.size()?a[i]:'=';
		int bb=i<b.size()?b[i]:'=';
		auto r=aa-bb;
		if(r!=0)
			return r;
		if(aa=='=')
			return 0;
	}
}
bool is_key_val_valid(const std::string& str) {
	if(str.empty())
		return false;
	if(str[0]=='=')
		return false;
	//XXX check if key is valid name
	return true;
}

template<>
GAPR_CORE_DECL bool gapr::edge_model::load(gapr::node_id _nid0, gapr::delta<gapr::delta_type::add_patch_>&& delta) {
	auto N=delta.nodes.size();
	if(N<1) {
		assert(delta.links.empty());
		if(delta.props.empty())
			return false;
	}
	struct Info {
		node_id id;
		unsigned int degree{0};
		unsigned int link_idx{0};
		bool is_root{false};
		bool has_vert{false};
	};
	std::vector<Info> infos;
	infos.reserve(N);

	std::size_t j=0, k=0;
	for(std::size_t i=0; i<N; i++) {
		auto par=delta.nodes[i].second;
		if(par>i)
			return false;
		Info info{};
		if(par) {
			infos[par-1].degree++;
			info.degree=1;
		}
		if(j<delta.links.size()) {
			auto& linkj=delta.links[j];
			if(linkj.first<i+1)
				return false;
			if(linkj.first==i+1) {
				gapr::link_id link{linkj.second};
				if(!link)
					return false;
				if(link.cannolize().data()!=linkj.second)
					return false;
				if(link.on_node())
					info.id=link.nodes[0];
				info.link_idx=j+1;
				j++;
			}
		}
		if(!info.id) {
			info.id=_nid0;
			_nid0=_nid0.offset(1);
		}

		while(k<delta.props.size()) {
			auto& propk=delta.props[k];
			if(propk.first<i+1)
				return false;
			if(propk.first>i+1)
				break;
			if(k>0) {
				auto& prev_prop=delta.props[k-1];
				if(propk.first==prev_prop.first) {
					if(compare_tag(propk.second, prev_prop.second)<=0)
						return false;
				}
			}
			if(!is_key_val_valid(propk.second))
				return false;

			auto key=propk.second;
			std::string val{};
			if(auto i=key.find('='); i!=std::string::npos) {
				val.assign(key, i+1);
				key.erase(i);
			}
			if(key==ROOT_KEY)
				info.is_root=true;

			gapr::prop_id p_id{info.id, std::move(key)};
			if(!PRIV::add_prop(*this, std::move(p_id), std::move(val)))
				return false;
			k++;
		}
		infos.push_back(info);
	}
	if(j<delta.links.size())
		return false;
	while(k<delta.props.size()) {
		auto& propk=delta.props[k];
		if(propk.first!=gapr::node_id::max().data)
			return false;
		if(!is_key_val_valid(propk.second))
			return false;

		if(!PRIV::add_log(*this, std::move(propk.second)))
			return false;
		++k;
	}
	if(k<delta.props.size())
		return false;
	for(std::size_t i=N; i-->0;) {
		if(!infos[i].id)
			continue;
		if(infos[i].degree>0) {
			auto par=delta.nodes[i].second;
			if(!par) {
				if(!infos[i].has_vert)
					return false;
				continue;
			}
			std::vector<node_id> nodes;
			nodes.push_back(infos[i].id);
			do {
				auto& par_info=infos[par-1];
				if(par_info.degree!=2)
					break;
				if(par_info.link_idx!=0)
					break;
				if(par_info.is_root)
					break;
				auto ppar=delta.nodes[par-1].second;
				if(!ppar)
					break;
				nodes.push_back(par_info.id);
				par_info.id=gapr::node_id{};
				par=ppar;
			} while(true);
			nodes.push_back(infos[par].id);
			vertex_id verts[2];
			for(unsigned int k=0; k<2; k++) {
				auto idx=k==0?(par-1):i;
				if(infos[idx].has_vert) {
					verts[k]=infos[idx].id;
				} else {
					auto link_idx=infos[idx].link_idx;
					auto link=link_idx?gapr::link_id{delta.links[link_idx-1].second}:link_id{};
					verts[k]=PRIV::gen_vert(*this, link, infos[idx].id.data, gapr::node_attr{delta.nodes[idx].first});
					if(!verts[k])
						return false;
					infos[idx].has_vert=true;
				}
			}
			std::vector<point> points;
			auto sz=nodes.size();
			points.resize(sz);
			auto idx=i;
			for(std::size_t j=0; j<sz; j++) {
				points[sz-j-1]=delta.nodes[idx].first;
				if(2*j<sz)
					std::swap(nodes[sz-j-1], nodes[j]);
				idx=delta.nodes[idx].second-1;
			}
			if(!PRIV::gen_edge_maybe_loop(*this, verts[0], verts[1], std::move(points), std::move(nodes)))
				return false;
		} else {
			auto link_idx=infos[i].link_idx;
			gapr::node_id nid;
			if(!link_idx) {
				nid=PRIV::gen_vert(*this, infos[i].id, gapr::node_attr{delta.nodes[i].first});
			} else {
				bool is_root=infos[i].is_root;
				gapr::link_id link{delta.links[link_idx-1].second};
				if(!link.on_node()) {
#if 0
					//link
					if(is_root) {
						//*gen link vert
					} else {
						//*gen link node
						//do not split and merge
					}
#endif
					return false;
				} else {
					if(is_root) {
						auto pos=PRIV::get_position(*this, link.nodes[0].data);
						if(!pos.edge) {
							if(!pos.vertex)
								return false;
							nid=pos.vertex;
						} else {
							nid=PRIV::gen_vert_split(*this, pos.edge, pos.index/128);
						}
					} else {
						nid=link.nodes[0];
					}
				}
			}
			if(!nid)
				return false;
		}
	}
	return true;
}

template<>
GAPR_CORE_DECL bool gapr::edge_model::load(gapr::node_id _nid0, gapr::delta<gapr::delta_type::del_patch_>&& delta) {
	for(std::size_t i=0; i<delta.props.size(); i++) {
		auto id=delta.props[i].first;
		if(i>0) {
			auto& prev_prop=delta.props[i-1];
			if(id<prev_prop.first)
				return false;
			if(id==prev_prop.first) {
				if(compare_tag(delta.props[i].second, prev_prop.second)<=0)
					return false;
			}
		} else if(id==0)
			return false;
		if(!is_key_val_valid(delta.props[i].second))
			return false;
		auto key=delta.props[i].second;
		//if(key==ROOT_KEY)
			//;
		gapr::prop_id p_id{gapr::node_id{id}, std::move(key)};
		if(!PRIV::del_prop(*this, std::move(p_id)))
			return false;
	}
	if(delta.nodes.empty())
		return true;

	std::size_t i=0;
	enum {
		Start,
		Open,
		Inside,
	} state=Start;
	gapr::node_id::data_type id_prev{0};
	gapr::node_id::data_type dup_open{0};
	bool dup_close{false};
	const edge* edg{nullptr};
	edge_id eid{0};
	uint32_t idx0{0}, idx_prev{0};
	auto remove_vert=[this](gapr::node_id::data_type id) {
		auto pos=PRIV::get_position(*this, id);
		if(pos.edge)
			return false;
		if(!pos.vertex)
			return false;
		if(!PRIV::del_node(*this, pos.vertex))
			return false;
		return PRIV::del_vert(*this, pos.vertex);
	};
	auto remove_edge=[this](edge_id eid, uint32_t idx0, uint32_t idx1) {
		if(idx0>idx1)
			std::swap(idx0, idx1);
		return PRIV::rm_subedge(*this, eid, idx0, idx1);
	};
	do {
		auto id=delta.nodes[i];
		switch(state) {
			case Start:
				if(!id)
					return false;
				state=Open;
				id_prev=id;
				dup_open=0;
				dup_close=false;
				eid=0;
				break;
			case Open:
				if(!id) {
					if(dup_open)
						return false;
					if(!remove_vert(id_prev))
						return false;
					state=Start;
				} else if(id==id_prev) {
					if(dup_open)
						return false;
					dup_open=id;
				} else {
					auto pos=PRIV::get_position(*this, id_prev);
					if(pos.edge) {
						eid=pos.edge;
						edg=PRIV::get_edge(*this, pos.edge);
						if(!edg)
							return false;
						idx0=pos.index/128;
						unsigned int hits{0};
						assert(idx0>0);
						if(edg->nodes[idx0-1].data==id) {
							idx_prev=idx0-1;
							hits++;
						}
						assert(idx0+1<edg->nodes.size());
						if(edg->nodes[idx0+1].data==id) {
							idx_prev=idx0+1;
							hits++;
						}
						if(hits!=1)
							return false;
					} else {
						auto vid=pos.vertex;
						if(!vid)
							return false;
						auto vert=PRIV::get_vert(*this, vid);
						if(!vert)
							return false;
						unsigned int hits{0};
						for(auto [eid2, dir]: vert->edges) {
							auto edg2=PRIV::get_edge(*this, eid2);
							if(!edg2)
								return false;
							uint32_t idxa=dir?edg2->nodes.size()-1:0;
							uint32_t idxb=dir?edg2->nodes.size()-2:1;
							assert(edg2->nodes[idxa].data==id_prev);
							if(edg2->nodes[idxb].data==id) {
								edg=edg2;
								eid=eid2;
								idx0=idxa;
								idx_prev=idxb;
								hits++;
							}
						}
						if(hits!=1)
							return false;
					}
					state=Inside;
					id_prev=id;
				}
				break;
			case Inside:
				if(!id) {
					if(!remove_edge(eid, idx0, idx_prev))
						return false;
					if(dup_open)
						if(!remove_vert(dup_open))
							return false;
					state=Start;
				} else if(id==id_prev) {
					if(!remove_edge(eid, idx0, idx_prev))
						return false;
					if(dup_open)
						if(!remove_vert(dup_open))
							return false;
					if(!remove_vert(id))
						return false;
					state=Start;
					dup_close=true;
				} else {
					assert(idx0!=idx_prev);
					if(idx0<idx_prev) {
						if(idx_prev+1>=edg->nodes.size()) {
							if(!remove_edge(eid, idx0, idx_prev))
								return false;
							if(dup_open)
								if(!remove_vert(dup_open))
									return false;
							dup_open=id_prev;
							eid=0;
							state=Open;
							continue;
						}
						idx_prev=idx_prev+1;
					} else {
						if(idx_prev<1) {
							if(!remove_edge(eid, idx0, idx_prev))
								return false;
							if(dup_open)
								if(!remove_vert(dup_open))
									return false;
							dup_open=id_prev;
							eid=0;
							state=Open;
							continue;
						}
						idx_prev=idx_prev-1;
					}
					if(edg->nodes[idx_prev].data!=id)
						return false;
					id_prev=id;
				}
				break;
		}
	gapr::print(1, "prop added", id);
		i++;
	} while(i<delta.nodes.size());
	switch(state) {
		case Start:
			if(!dup_close)
				return false;
			break;
		case Open:
			if(dup_open)
				return false;
			if(!remove_vert(id_prev))
				return false;
			break;
		case Inside:
			if(!remove_edge(eid, idx0, idx_prev))
				return false;
			if(dup_open)
				if(!remove_vert(dup_open))
					return false;
			break;
	}
	return true;
}

template<>
GAPR_CORE_DECL bool gapr::edge_model::load(gapr::node_id _nid0, gapr::delta<gapr::delta_type::proofread_>&& delta) {
	const edge* edg{nullptr};
	edge_id eid{0};
	uint32_t idx_prev;
	std::unordered_set<vertex_id> vids;
	std::vector<std::pair<edge_id, uint32_t>> edgidx;
	for(std::size_t i=0; i<delta.nodes.size(); i++) {
		auto id=delta.nodes[i];
		if(!id)
			return false;
		vertex_id vid{};
		do {
			if(!eid)
				break;
			if(idx_prev>0 && edg->nodes[idx_prev-1].data==id) {
				idx_prev=idx_prev-1;
				if(idx_prev==0)
					vid=edg->left;
				break;
			}
			if(idx_prev+1<edg->nodes.size() && edg->nodes[idx_prev+1].data==id) {
				idx_prev=idx_prev+1;
				if(idx_prev==edg->nodes.size()-1)
					vid=edg->right;
				break;
			}
			eid=0;
		} while(false);
		if(vid) {
			eid=0;
			vids.emplace(vid);
			continue;
		}
		if(eid) {
			edgidx.emplace_back(eid, idx_prev);
			continue;
		}
		auto pos=PRIV::get_position(*this, id);
		if(pos.edge) {
			eid=pos.edge;
			edg=PRIV::get_edge(*this, pos.edge);
			if(!edg)
				return false;
			idx_prev=pos.index/128;
			edgidx.emplace_back(eid, idx_prev);
		} else {
			auto vid=pos.vertex;
			if(!vid)
				return false;
			vids.emplace(vid);
		}
	}
	for(auto vid: vids) {
		auto vert=PRIV::chg_vert(*this, vid);
		if(!vert)
			return false;
		vert->attr.misc.coverage(true);
		for(auto [eid, dir]: vert->edges) {
			uint32_t idx=0;
			if(dir)
				idx--;
			edgidx.emplace_back(eid, idx);
		}
	}
	std::unordered_map<edge_id, edge*> edgs;
	for(auto [eid, idx]: edgidx) {
		auto [it, ins]=edgs.emplace(eid, nullptr);
		if(ins) {
			auto edg=PRIV::chg_edge(*this, eid);
			if(!edg)
				return false;
			it->second=edg;
		}
		auto edg=it->second;
		if(idx+1==0)
			idx=edg->nodes.size()-1;
		gapr::node_attr pt{edg->points[idx]};
		pt.misc.coverage(1);
		edg->points[idx]=pt.data();
	}
	return true;
}

template<>
GAPR_CORE_DECL bool gapr::edge_model::load(gapr::node_id _nid0, gapr::delta<gapr::delta_type::reset_proofread_>&& delta) {
	for(std::size_t i=0; i<delta.props.size(); i++) {
		auto id=delta.props[i].first;
		if(i>0) {
			auto& prev_prop=delta.props[i-1];
			if(id<prev_prop.first)
				return false;
			if(id==prev_prop.first) {
				if(compare_tag(delta.props[i].second, prev_prop.second)<=0)
					return false;
			}
		} else if(id==0)
			return false;
		if(!is_key_val_valid(delta.props[i].second))
			return false;
		auto key=delta.props[i].second;
		//if(key==ROOT_KEY)
			//;
		gapr::prop_id p_id{gapr::node_id{id}, std::move(key)};
		if(!PRIV::del_prop(*this, std::move(p_id)))
			return false;
	}
	if(delta.nodes.empty())
		return true;
	const edge* edg{nullptr};
	edge_id eid{0};
	uint32_t idx_prev;
	std::unordered_set<vertex_id> vids;
	std::vector<std::pair<edge_id, uint32_t>> edgidx;
	AscendingSequenceDecoder dec{delta.nodes};
	while(true) {
		auto [id, end]=dec.next();
		if(end)
			break;
		if(!id)
			return false;
		vertex_id vid{};
		do {
			if(!eid)
				break;
			if(idx_prev>0 && edg->nodes[idx_prev-1].data==id) {
				idx_prev=idx_prev-1;
				if(idx_prev==0)
					vid=edg->left;
				break;
			}
			if(idx_prev+1<edg->nodes.size() && edg->nodes[idx_prev+1].data==id) {
				idx_prev=idx_prev+1;
				if(idx_prev==edg->nodes.size()-1)
					vid=edg->right;
				break;
			}
			eid=0;
		} while(false);
		if(vid) {
			eid=0;
			vids.emplace(vid);
			continue;
		}
		if(eid) {
			edgidx.emplace_back(eid, idx_prev);
			continue;
		}
		auto pos=PRIV::get_position(*this, id);
		if(pos.edge) {
			eid=pos.edge;
			edg=PRIV::get_edge(*this, pos.edge);
			if(!edg)
				return false;
			idx_prev=pos.index/128;
			edgidx.emplace_back(eid, idx_prev);
		} else {
			auto vid=pos.vertex;
			if(!vid)
				return false;
			vids.emplace(vid);
		}
	}
	for(auto vid: vids) {
		auto vert=PRIV::chg_vert(*this, vid);
		if(!vert)
			return false;
		vert->attr.misc.coverage(false);
		for(auto [eid, dir]: vert->edges) {
			uint32_t idx=0;
			if(dir)
				idx--;
			edgidx.emplace_back(eid, idx);
		}
	}
	std::unordered_map<edge_id, edge*> edgs;
	for(auto [eid, idx]: edgidx) {
		auto [it, ins]=edgs.emplace(eid, nullptr);
		if(ins) {
			auto edg=PRIV::chg_edge(*this, eid);
			if(!edg)
				return false;
			it->second=edg;
		}
		auto edg=it->second;
		if(idx+1==0)
			idx=edg->nodes.size()-1;
		gapr::node_attr pt{edg->points[idx]};
		pt.misc.coverage(false);
		edg->points[idx]=pt.data();
	}
	return true;
}
template<>
GAPR_CORE_DECL bool gapr::edge_model::load(gapr::node_id _nid0, gapr::delta<gapr::delta_type::reset_proofread_0_>&& delta) {
	return load(_nid0, upgrade_delta(std::move(delta)));
}

		//gather_model_PRIV::do_apply(*this);
				//id++;



bool gapr::edge_model::loader::load_file(gapr::mem_file file) {
	auto buf=gapr::make_streambuf(std::move(file));
	gapr::commit_info info;
	if(!info.load(*buf))
		gapr::report("commit file no commit info");
#if 0
	//start0
	if(info.id!=id)
		gapr::report("commit file wrong id");
#endif

	auto& model=const_cast<edge_model&>(_model);
	return gapr::delta_variant::visit<bool>(gapr::delta_type{info.type},
			[buf=std::move(buf),&model,&info](auto typ) {
				gapr::delta<typ> delta;
				if(!gapr::load(delta, *buf))
					gapr::report("commit file no delta");
				return model.load(gapr::node_id{info.nid0}, std::move(delta));
			});
}
uint64_t gapr::edge_model::loader::init(std::streambuf& buf) {
	assert(_model._edges.empty());
	assert(_model._vertices.empty());
	assert(_model._props.empty());
	assert(_model._nodes.empty());

	gapr::model_state st;
	if(!st.load(buf))
		gapr::report("model state file load err");

	auto& model=const_cast<edge_model&>(_model);
	//clear staging data
	auto add_vert=[&model](gapr::node_id id, gapr::node_attr attr) -> vertex_id {
		auto it=model._vertices_d.find(id);
		if(it!=model._vertices_d.end())
			return id;
		auto nid=PRIV::gen_vert(model, id, attr);
		return nid;
	};
	{
#if 0
		gapr::print("st sz: ", st.edges.size());
		int i=0;
		for(auto& edg: st.edges) {
			gapr::print("edge sizes: ", edg.nodes.size(), ':', edg.points.size());
			if(i++>20)
				break;
		}
#endif
	}
	for(auto& edg: st.edges) {
		//gapr::print("edge sizes: ", edg.nodes.size(), ':', edg.points.size());
		assert(edg.nodes.size()==edg.points.size());
		assert(edg.nodes.size()>0);
		if(edg.nodes.size()==1) {
			auto nid=PRIV::gen_vert(model, edg.nodes[0], gapr::node_attr{edg.points[0]});
			if(!nid)
				return 0;
			continue;
		}
		auto vert_left=add_vert(edg.nodes.front(), gapr::node_attr{edg.points.front()});
		if(!vert_left)
			return 0;
		auto vert_right=add_vert(edg.nodes.back(), gapr::node_attr{edg.points.back()});
		if(!vert_right)
			return 0;
		if(!PRIV::gen_edge(model, vert_left, vert_right, std::move(edg.points), std::move(edg.nodes)))
			return 0;
	}

	for(auto& [id, str]: st.props) {
		std::string key{str};
		auto i=key.find('=');
		std::string val{};
		if(i!=std::string::npos) {
			val.assign(key, i+1);
			key.erase(i);
		}

		gapr::node_id nid;
		bool is_root=(key==ROOT_KEY);
		if(is_root) {
			auto pos=PRIV::get_position(model, id);
			if(!pos.edge) {
				if(!pos.vertex)
					return {};
				nid=pos.vertex;
			} else {
				nid=PRIV::gen_vert_split(model, pos.edge, pos.index/128);
			}
		} else {
			nid=gapr::node_id{id};
		}

		if(!nid)
			return 0;

		gapr::prop_id pid{nid, std::move(key)};
		if(!PRIV::add_prop(model, std::move(pid), std::move(val)))
			return 0;
	}
	for(auto& log: st.logs) {
		if(!is_key_val_valid(log))
			return 0;
		if(!PRIV::add_log(model, std::move(log)))
			return 0;
	}
	return st.size;
}

gapr::mem_file gapr::edge_model::view::dump_state(int64_t ncommits) const {
	gapr::model_state st;
	st.size=ncommits;
	for(auto& [vid, vert]: _model._vertices) {
		if(vert.edges.empty()) {
			auto& edg=st.edges.emplace_back();
			edg.nodes.push_back(vid);
			edg.points.push_back(vert.attr.data());
		}
	}
	std::vector<std::pair<edge_id, model_state::edge>> tmp_edges;
	for(auto& [eid0, edg0]: _model._edges) {
		auto& [eid, edg]=tmp_edges.emplace_back();
		eid=eid0;
		edg.nodes=edg0.nodes;
		edg.points=edg0.points;
	}
	std::sort(tmp_edges.begin(), tmp_edges.end(), [](auto& a, auto& b) {
		return a.first<b.first;
	});
	for(auto& [eid, edg]: tmp_edges)
		st.edges.emplace_back(std::move(edg));
	for(auto& [pid, val0]: _model._props) {
		auto prop=pid.key;
		prop.reserve(prop.size()+1+val0.size());
		(prop+="=")+=val0;
		st.props.emplace_back(pid.node.data, std::move(prop));
	}
	std::sort(st.props.begin(), st.props.end());
	for(auto& log: _model._logs)
		st.logs.emplace_back(log);

	gapr::mutable_mem_file file{true};
	std::ostringstream oss;
	if(!st.save(*oss.rdbuf()))
		gapr::report("failed to save delta");
	auto str=oss.str();
	std::size_t i=0;
	while(i<str.size()) {
		auto buf=file.map_tail();
		auto n=str.size()-i;
		if(n>buf.size())
			n=buf.size();
		std::copy(&str[i], &str[i+n], buf.data());
		i+=n;
		file.add_tail(n);
	}
	return file;
}

bool gapr::edge_model::view::equal(const view other) const {
	auto check_fail=[]() {
		throw;
		return false;
	};
	auto& verts=_model._vertices;
	auto& verts2=other._model._vertices;
	if(verts.size()!=verts2.size())
		return check_fail();
	for(auto& [vid, vert]: verts) {
		auto it2=verts2.find(vid);
		if(it2==verts2.end())
			return check_fail();
		if(it2->second.attr!=vert.attr)
			return check_fail();
		if(it2->second.edges.size()!=vert.edges.size())
			return check_fail();
	}
	auto get_ordered_ptrs=[](auto& edges) {
		std::vector<std::pair<edge_id, const edge*>> res;
		for(auto& [eid, edg]: edges) {
			res.emplace_back(eid, &edg);
		}
		std::sort(res.begin(), res.end(), [](auto a, auto b) {
			return a.first<b.first;
		});
		return res;
	};
	auto edges=get_ordered_ptrs(_model._edges);
	auto edges2=get_ordered_ptrs(other._model._edges);
	if(edges.size()!=edges2.size())
		return check_fail();
	for(std::size_t i=0; i<edges.size(); ++i) {
		auto edg=edges[i].second;
		auto edg2=edges2[i].second;
		if(edg->left!=edg2->left)
			return check_fail();
		if(edg->right!=edg2->right)
			return check_fail();
		if(edg->nodes!=edg2->nodes)
			return check_fail();
		if(edg->points!=edg2->points)
			return check_fail();
	}
	auto& props=_model._props;
	auto& props2=other._model._props;
	if(props.size()!=props2.size())
		return check_fail();
	for(auto& [pid, val]: props) {
		auto it2=props2.find(pid);
		if(it2==props2.end())
			return check_fail();
		if(it2->second!=val) {
			gapr::print(it2->second, "!=", val);
			return check_fail();
		}
	}
	return true;
}

inline static bool filter_node(const gapr::node_attr& pos, const std::array<gapr::node_attr, 2>& pts) {
	for(unsigned int i=0; i<3; i++) {
		if(pos.ipos[i]<pts[0].ipos[i])
			return false;
		if(pos.ipos[i]>pts[1].ipos[i])
			return false;
	}
	return true;
}
template<typename T1, typename T2, typename T3, typename T4>
static bool filter_objects(T1& objects, T2& objects_d, T3& filter_d, const T4 cond) {
	gapr::print(1, "filter_objects");
	for(auto& [id, obj]: objects) {
		// XXX
		//if(id.key[0]=='.')
			//continue;
		auto it2=objects_d.find(id);
		if(it2==objects_d.end()) {
			if(cond(id, obj))
				filter_d.emplace(id, PRE_ADD);
		}
	}
	gapr::print(1, "filter_objects mid");

	for(auto& [id, chg]: objects_d) {
		bool chk{false};
		switch(chg.first) {
		case PRE_ADD:
			if(objects.find(id)!=objects.end())
				return false;
			chk=true;
			break;
		case PRE_DEL:
			if(objects.find(id)==objects.end())
				return false;
			break;
		case PRE_CHG:
			if(objects.find(id)==objects.end())
				return false;
			chk=true;
			break;
		default:
			return false;
		}
		if(chk) {
			if(cond(id, chg.second))
				filter_d.emplace(id, PRE_ADD);
		}
	}
	gapr::print(1, "filter_objects end");
	return true;
}
template<typename T1, typename T2, typename T3, typename T4, typename T5>
static bool filter_objects_delta(T1& objects, T2& objects_d, T3& filter, T4& filter_d, const T5 cond) {
	gapr::print(1, "filter_objects_delta");
	for(auto& [id, chg]: objects_d) {
		bool ins{false}, del{false};
		switch(chg.first) {
		case PRE_ADD:
			if(objects.find(id)!=objects.end())
				return false;
			if(cond(id, chg.second))
				ins=true;
			break;
		case PRE_DEL:
			if(objects.find(id)==objects.end())
				return false;
			del=true;
			break;
		case PRE_CHG:
			if(objects.find(id)==objects.end())
				return false;
			if(cond(id, chg.second)) {
				if(auto it=filter.find(id); it==filter.end())
					ins=true;
			} else {
				del=true;
			}
			break;
		default:
			return false;
		}
		if(ins)
			filter_d.emplace(id, PRE_ADD);
		if(del) {
			if(auto it=filter.find(id); it!=filter.end())
				filter_d.emplace(id, PRE_DEL);
		}
	}
	gapr::print(1, "filter_objects_delta end");
	return true;
}
bool gapr::edge_model::PRIV::filter_edges(gapr::edge_model& model, const std::array<gapr::node_attr, 2>& pts) {
	auto cond=[&pts](auto eid, const auto& edg) ->bool {
		for(auto& pt: edg.points) {
			gapr::node_attr pos{pt};
			if(filter_node(pos, pts))
				return true;
		}
		return false;
	};
	switch(model._filter_stat) {
	case 1:
		return filter_objects(model._edges, model._edges_d, model._edges_filter_d, cond);
	case 2:
		return filter_objects_delta(model._edges, model._edges_d, model._edges_filter, model._edges_filter_d, cond);
	}
	assert(0);
	return false;
}
bool gapr::edge_model::PRIV::filter_vertices(gapr::edge_model& model, const std::array<gapr::node_attr, 2>& pts) {
	auto cond=[&pts](auto vid, const auto& vert) ->bool {
		return filter_node(vert.attr, pts);
	};
	switch(model._filter_stat) {
	case 1:
		return filter_objects(model._vertices, model._vertices_d, model._vertices_filter_d, cond);
	case 2:
		return filter_objects_delta(model._vertices, model._vertices_d, model._vertices_filter, model._vertices_filter_d, cond);
	}
	assert(0);
	return false;
}
bool gapr::edge_model::PRIV::filter_props(gapr::edge_model& model, const std::array<gapr::node_attr, 2>& pts) {
	gapr::print(1, "filter_props");
	auto filter_prop=[&pts,&model](const auto& prop, const auto& val) ->bool {
		gapr::node_id id=prop.node;
		auto p=get_position(model, id.data);
		if(p.edge) {
			auto e=get_edge(model, p.edge);
			if(e)
				return filter_node(gapr::node_attr{e->points[p.index/128]}, pts);
		} else {
			if(p.vertex) {
				auto v=get_vert(model, p.vertex);
				if(v)
					return filter_node(v->attr, pts);
			}
		}
		assert(0);
		return false;
	};
	switch(model._filter_stat) {
	case 1:
		return filter_objects(model._props, model._props_d, model._props_filter_d, filter_prop);
	case 2:
		return filter_objects_delta(model._props, model._props_d, model._props_filter, model._props_filter_d, filter_prop);
	}
	assert(0);
	return false;
}

bool gapr::edge_model::loader::filter(const std::array<double, 6>& bbox) {
	gapr::print(1, "begin filter (.)");
	auto& model=const_cast<edge_model&>(_model);
	model._filter_bbox=bbox;
	model._edges_filter_d.clear();
	model._vertices_filter_d.clear();
	model._props_filter_d.clear();
	model._filter_stat=1;

	std::array<gapr::node_attr, 2> pts;
	for(unsigned int i=0; i<3; i++) {
		auto a=bbox[i], b=bbox[3+i];
		if(a>b)
			return true;
		pts[0].pos(i, a);
		pts[1].pos(i, b);
	}

	if(!PRIV::filter_edges(model, pts))
		return false;
	if(!PRIV::filter_vertices(model, pts))
		return false;
	return PRIV::filter_props(model, pts);
}
bool gapr::edge_model::loader::filter() {
	gapr::print(1, "begin filter ()");
	auto& model=const_cast<edge_model&>(_model);
	const auto& bbox=_model._filter_bbox;

	model._edges_filter_d.clear();
	model._vertices_filter_d.clear();
	model._props_filter_d.clear();
	model._filter_stat=2;

	std::array<gapr::node_attr, 2> pts;
	for(unsigned int i=0; i<3; i++) {
		auto a=bbox[i], b=bbox[3+i];
		if(a>b)
			return true;
		pts[0].pos(i, a);
		pts[1].pos(i, b);
	}

	gapr::print(1, "do filter");
	if(!PRIV::filter_edges(model, pts))
		return false;
	if(!PRIV::filter_vertices(model, pts))
		return false;
	return PRIV::filter_props(model, pts);
}
bool gapr::edge_model::loader::merge() {
	auto& model=const_cast<edge_model&>(_model);
	return PRIV::try_merge(model);
}

namespace {
struct loop_cache {
	std::unordered_set<gapr::edge_model::vertex_id> path1_verts;
	std::vector<std::pair<gapr::edge_model::edge*, gapr::edge_model::vertex*>> path1;
};
}

template<typename TVerts, typename TEdgs>
static void handle_loop_condition(TVerts& mdl_verts, TEdgs& mdl_edgs,
		gapr::edge_model::vertex_id vid,
		gapr::edge_model::vertex* vert,
		gapr::edge_model::vertex* nvert,
		loop_cache& cache
) {
	assert(cache.path1.empty());
	assert(cache.path1_verts.empty());
	nvert->loop=true;
	for(auto pare=nvert->parent_e; pare; pare=nvert->parent_e) {
		auto& pedg=mdl_edgs.at(pare);
		auto parv=pedg.parent_v;
		assert(parv);
		nvert=&mdl_verts.at(parv);
		cache.path1_verts.emplace(parv);
		cache.path1.emplace_back(&pedg, nvert);
	}
	gapr::edge_model::vertex_id commp{};
	do {
		if(cache.path1_verts.find(vid)!=cache.path1_verts.end()) {
			commp=vid;
			break;
		}
		vert->loop=true;
		auto pare=vert->parent_e;
		if(!pare)
			break;
		auto& pedg=mdl_edgs.at(pare);
		pedg.loop=true;
		vid=pedg.parent_v;
		assert(vid);
		vert=&mdl_verts.at(vid);
	} while(true);
	for(auto [pedg, pvert]: cache.path1) {
		pedg->loop=true;
		pvert->loop=true;
		if(pedg->parent_v==commp)
			break;
	}
	cache.path1.clear();
	cache.path1_verts.clear();
	if(commp) {
		auto& comm=mdl_verts.at(commp);
		assert(comm.loop);
	}
}

bool gapr::edge_model::PRIV::update_aux(edge_model& model, std::unordered_set<gapr::node_id>&& trees_changed) {
	auto& cc_helper=model._cc_helper;
	if(!trees_changed.empty()) {
		{
			std::vector<gapr::node_id> extra;
			for(auto n: trees_changed) {
				if(auto it=model._vertices.find(n); it!=model._vertices.end())
					extra.push_back(it->second.root);
			}
			for(auto n: extra)
				trees_changed.emplace(n);
		}
		// propagate
		unsigned int chgs;
		do {
			chgs=0;
			for(auto it=cc_helper.begin(); it!=cc_helper.end(); ) {
				if(auto it2=trees_changed.find(it->first); it2!=trees_changed.end()) {
					trees_changed.emplace(it->second);
					it=cc_helper.erase(it);
					++chgs;
					continue;
				}
				++it;
			}
		} while(chgs>0);
	}
	gapr::print(1, "finish handle cc 555");
	for(auto& [vid, vert]: model._vertices) {
		if(vert.root==node_id{}) {
			assert(vert.parent_e==0);
			continue;
		}
		// ??????????????????
		if(cc_helper.find(vert.root)!=cc_helper.end())
			continue;
		vert.root=node_id{};
		vert.loop=false;
		vert.raised=false;
		vert.parent_e=0;
	}
	//0.034251s
	gapr::print(1, "finish handle cc xxx 333"); // XXX slow !!!!!
	for(auto& [eid, edg]: model._edges) {
		if(edg.root==node_id{}) {
			assert(edg.parent_v==vertex_id{});
			continue;
		}
		// ????????????????????
		if(cc_helper.find(edg.root)!=cc_helper.end())
			continue;
		edg.root=node_id{};
		edg.loop=false;
		edg.raised=false;
		edg.parent_v=vertex_id{};
	}
	//77
	gapr::print(1, "finish handle cc xxx"); // XXX slow !!
	auto is_finished=[&model](vertex_id vid, const vertex& vert) -> bool {
		if(vert.edges.size()>1)
			return true;
		if(model._props.find({vid, "state"})==model._props.end())
			return false;
		return true;
	};
	std::queue<vertex_id> que{};
	std::queue<vertex_id> que_raised{};
	auto& all_roots=model._props.per_key("root");
	for(auto nid: all_roots) {
		if(cc_helper.find(nid)==cc_helper.end()) {
			auto& vert=model._vertices.at(nid);
			vert.complete=is_finished(nid, vert);
			//vert.index=i;
			vert.root=nid;
			que.push(nid);
			cc_helper.emplace(nid, nid);
		}
	}
	auto it_no_root=model._vertices.begin();
	auto& all_raised=model._props.per_key("raise");
	auto it_raise=all_raised.begin();
	//std::unordered_set<gapr::node_id> raised_roots;
	//43
	gapr::print(1, "finish handle cc 444"); // XXX slow !!!!
	loop_cache lcache;
	// this loop can be very slow if not updated incrementally!
	while(true) {
		if(que.empty()) {
			while(false && it_no_root!=model._vertices.end()) {
				auto it=it_no_root++;
				if(!it->second.root) {
					it->second.root=it->first;
					que.push(it->first);
					cc_helper.emplace(it->first, it->first);
					break;
				}
			}
			while(true && it_raise!=all_raised.end()) {
				auto it=it_raise++;
					auto pos=PRIV::get_position(model, it->data);
					if(pos.edge) {
						auto& e=model._edges.at(pos.edge);
						e.raised=true;
						que_raised.push(e.left);
						que_raised.push(e.right);
						pos.vertex=e.left;
					} else {
						que_raised.push(pos.vertex);
					}
				if(cc_helper.find(pos.vertex)==cc_helper.end()) {
					auto& v=model._vertices.at(pos.vertex);
					if(!v.root) {
						v.root=pos.vertex;
						que.push(pos.vertex);
						cc_helper.emplace(pos.vertex, pos.vertex);
						//raised_roots.emplace(pos.vertex);
						break;
					}
				}
			}
			if(que.empty())
				break;
		}
		auto vid=que.front();
		que.pop();
		auto& vert=model._vertices.at(vid);
		auto& vert_root=model._vertices.at(vert.root);
		for(auto [eid, dir]: vert.edges) {
			auto& edg=model._edges.at(eid);
			if(edg.root)
				continue;
			/*
			if(vert.parent_e)
				edg.type=vert.parent_e.type();
				*/
			auto nvid=(dir?edg.left:edg.right);
			auto& nvert=model._vertices.at(nvid);
			edg.root=vert.root;
			edg.parent_v=vid;
			if(!is_finished(nvid, nvert))
				vert_root.complete=false;
			if(!nvert.root) {
				nvert.root=vert.root;
				nvert.parent_e=eid;
				que.push(nvid);
				continue;
			}

			// loop found
			cc_helper.emplace(nvert.root, vert.root);
			cc_helper.emplace(vert.root, nvert.root);
			auto& vert_root2=model._vertices.at(nvert.root);
			vert_root.complete=false;
			vert_root2.complete=false;
			edg.loop=true;
			handle_loop_condition(model._vertices, model._edges, vid, &vert, &nvert, lcache);
			assert(vert.loop && nvert.loop);
		}
	}
	//33
	gapr::print(1, "finish handle cc 333"); // XXX slow !!!
	//////
#if 0
	do {
		unsigned int chg=0;
		for(auto [f, t]: cc_helper) {
			if(raised_roots.find(f)!=raised_roots.end()) {
				auto [it, ins]=raised_roots.emplace(t);
				chg+=ins?1:0;
			}
		}
		if(chg==0)
			break;
	} while(true);
#endif
	gapr::print(1, "finish handle cc 222");
	while(!que_raised.empty()) {
		auto vid=que_raised.front();
		que_raised.pop();
		auto& vert=model._vertices.at(vid);
		if(vert.raised)
			continue;
		vert.raised=true;
		if(!vert.attr.misc.coverage())
			continue;
		for(auto [eid, dir]: vert.edges) {
			auto& edg=model._edges.at(eid);
			if(edg.raised)
				continue;
			edg.raised=vert.raised;
			auto nvid=(dir?edg.left:edg.right);
			que_raised.push(nvid);
		}
	}
#if 0
	for(auto& [eid, edg]: model._edges) {
		if(edg.root && raised_roots.find(edg.root)!=raised_roots.end())
			edg.raised=true;
	}
	//22
	gapr::print(1, "finish handle cc 111"); // XXX slow !!
	for(auto& [vid, vert]: model._vertices) {
		if(vert.root && raised_roots.find(vert.root)!=raised_roots.end())
			vert.raised=true;
	}
#endif
	model._raised=!all_raised.empty();
	gapr::print(1, "finish handle cc"); // XXX slow !!!!!!
	return true;
}

bool gapr::edge_model::PRIV::highlight_finish(gapr::edge_model& model, std::unordered_set<vertex_id>& verts) {
	assert(model._filter_stat==1);
	for(auto& [id, vert]: model._props) {
		bool add{false};
		auto p=model._nodes.at(id.node);
		if(p.edge) {
			if(model._edges_filter_d.find(p.edge)!=model._edges_filter_d.end())
				add=true;
		} else if(p.vertex) {
			if(verts.find(p.vertex)!=verts.end())
				add=true;
		} else {
			assert(0);
		}
		if(add)
			model._props_filter_d.emplace(id, PRE_ADD);
	}
	for(auto vid: verts)
		model._vertices_filter_d.emplace(vid, PRE_ADD);
	return true;
}
bool gapr::edge_model::PRIV::highlight_extend_and_finish(gapr::edge_model& model, std::deque<gapr::node_id>& todo, std::unordered_set<vertex_id>& verts, bool allow_loop) {
	while(!todo.empty()) {
		auto vid=todo.front();
		todo.pop_front();
		auto [it, ins]=verts.emplace(vid);
		if(!ins)
			continue;

		auto& vert=model._vertices.at(vid);
		for(auto [eid, dir]: vert.edges) {
			auto& edg=model._edges.at(eid);
			model._edges_filter_d.emplace(eid, PRE_ADD);
			if(edg.loop && !allow_loop)
				continue;
			auto vert2=dir?edg.left:edg.right;
			todo.push_back(vert2);
		}
	}
	for(auto [eid, junk]: model._edges_filter_d) {
		auto& edg=model._edges.at(eid);
		verts.emplace(edg.left);
		verts.emplace(edg.right);
	}
	return highlight_finish(model, verts);
}
bool gapr::edge_model::loader::highlight_loop(edge_id eid0) {
	auto& model=const_cast<edge_model&>(_model);
	assert(model._edges_d.empty());
	assert(model._vertices_d.empty());
	assert(model._props_d.empty());
	assert(model._logs_d.empty());
	assert(model._nodes_d.empty());

	model._edges_filter_d.clear();
	model._vertices_filter_d.clear();
	model._props_filter_d.clear();
	model._filter_stat=1;

	if(eid0) {
		auto& edg=edges().at(eid0);
		if(!edg.loop)
			eid0=0;
	}
	if(!eid0) {
		for(auto& [eid2, e]: edges()) {
			if(e.loop) {
				eid0=eid2;
				break;
			}
		}
	}
	if(!eid0)
		return true;

	vertex_id vid0;
	do {
		auto& edg=edges().at(eid0);
		gapr::print("cur_e: ", eid0, ' ', edg.left.data, ':', edg.right.data, ':', edg.loop, ':', edg.parent_v.data);
		{
			unsigned int hit{0};
			if(edg.parent_v==edg.left) {
				++hit;
				vid0=edg.right;
			}
			if(edg.parent_v==edg.right) {
				++hit;
				vid0=edg.left;
			}
			assert(hit==1);
		}
		auto& vert=vertices().at(vid0);
		gapr::print("next_v: ", vid0.data, ' ', vert.parent_e);
		if(vert.parent_e!=eid0)
			break;
		{
			unsigned int hit{0};
			bool child{false};
			for(auto [eid2, dir]: vert.edges) {
				auto& edg2=edges().at(eid2);
				if(eid2!=vert.parent_e && edg2.loop) {
					++hit;
					eid0=eid2;
				  	child=(edg2.parent_v==vid0);
				}
			}
			assert(hit>=1);
			if(!child)
				break;
		}
	} while(true);

	std::deque<vertex_id> todo;
	auto ascend=[&todo,this,&model](vertex_id vid) {
		do {
			todo.push_back(vid);
			auto& vert=vertices().at(vid);
			auto eid=vert.parent_e;
			if(eid==0)
				return;
			auto [it, ins]=model._edges_filter_d.emplace(eid, PRE_ADD);
			if(!ins)
				return;
			auto& edg=edges().at(eid);
			vid=edg.parent_v;
		} while(true);
	};
	{
		model._edges_filter_d.emplace(eid0, PRE_ADD);
		auto& edg=edges().at(eid0);
		ascend(edg.parent_v);
		ascend(vid0);
	}

	std::unordered_set<vertex_id> verts;
	return PRIV::highlight_extend_and_finish(model, todo, verts, false);
}
bool gapr::edge_model::loader::highlight_neuron(edge_id eid0, int direction) {
	assert(eid0!=0);
	auto& model=const_cast<edge_model&>(_model);
	assert(model._edges_d.empty());
	assert(model._vertices_d.empty());
	assert(model._props_d.empty());
	assert(model._logs_d.empty());
	assert(model._nodes_d.empty());

	model._edges_filter_d.clear();
	model._vertices_filter_d.clear();
	model._props_filter_d.clear();
	model._filter_stat=1;

	if(!eid0)
		return true;

	std::deque<vertex_id> todo;
	auto ascend=[&todo,this,&model](vertex_id vid) {
		do {
			todo.push_back(vid);
			auto& vert=vertices().at(vid);
			auto eid=vert.parent_e;
			if(eid==0)
				return;
			auto [it, ins]=model._edges_filter_d.emplace(eid, PRE_ADD);
			if(!ins)
				return;
			auto& edg=edges().at(eid);
			vid=edg.parent_v;
		} while(true);
	};
	std::unordered_set<vertex_id> verts;
	{
		model._edges_filter_d.emplace(eid0, PRE_ADD);
		auto& edg=edges().at(eid0);
		if(!edg.root)
			return highlight_orphan(eid0);
		ascend(edg.parent_v);
		if(direction!=0) {
			while(!todo.empty()) {
				auto vid=todo.front();
				todo.pop_front();
				verts.emplace(vid);
			}
		}
		todo.push_back(edg.left);
		todo.push_back(edg.right);
		if(direction<0) {
			unsigned int hits=0;
			if(edg.left==edg.parent_v) {
				hits++;
				verts.emplace(edg.right);
			}
			if(edg.right==edg.parent_v) {
				hits++;
				verts.emplace(edg.left);
			}
			assert(hits==1);
		} else if(direction>0) {
			unsigned int hits=0;
			if(edg.left==edg.parent_v) {
				hits++;
				verts.emplace(edg.left);
			}
			if(edg.right==edg.parent_v) {
				hits++;
				verts.emplace(edg.right);
			}
			assert(hits==1);
		}
	}
	return PRIV::highlight_extend_and_finish(model, todo, verts, true);
}
bool gapr::edge_model::loader::highlight_raised() {
	auto& model=const_cast<edge_model&>(_model);
	assert(model._raised==true);
	assert(model._edges_d.empty());
	assert(model._vertices_d.empty());
	assert(model._props_d.empty());
	assert(model._logs_d.empty());
	assert(model._nodes_d.empty());

	model._edges_filter_d.clear();
	model._vertices_filter_d.clear();
	model._props_filter_d.clear();
	model._filter_stat=1;

	std::unordered_set<vertex_id> verts;
	for(auto& [eid, e]: model._edges) {
		if(e.raised) {
			model._edges_filter_d.emplace(eid, PRE_ADD);
			verts.emplace(e.left);
			verts.emplace(e.right);
		}
	}
	return PRIV::highlight_finish(model, verts);
}
bool gapr::edge_model::loader::highlight_orphan(edge_id eid0) {
	assert(eid0!=0);
	auto& model=const_cast<edge_model&>(_model);
	assert(model._edges_d.empty());
	assert(model._vertices_d.empty());
	assert(model._props_d.empty());
	assert(model._logs_d.empty());
	assert(model._nodes_d.empty());

	model._edges_filter_d.clear();
	model._vertices_filter_d.clear();
	model._props_filter_d.clear();
	model._filter_stat=1;

	std::deque<vertex_id> todo;
	std::unordered_set<vertex_id> verts;
	{
		model._edges_filter_d.emplace(eid0, PRE_ADD);
		auto& edg=edges().at(eid0);
		todo.push_back(edg.left);
		todo.push_back(edg.right);
	}

	return PRIV::highlight_extend_and_finish(model, todo, verts, true);
}

void gapr::edge_model::updater::reset_filter() {
	auto& model=const_cast<edge_model&>(_model);
	model._edges_filter_d.clear();
	model._vertices_filter_d.clear();
	model._props_filter_d.clear();
	model._filter_stat=1;
}

void gapr::edge_model::apply_attach() {
	std::unordered_map<gapr::node_id, int> fix_type;
	std::string_view attach_tag{"attach@"};
	std::unordered_map<gapr::node_id, gapr::link_id> fix_conn;
	for(auto it=_props.begin(); it!=_props.end(); ++it) {
		auto& pid=it->first;
		std::string_view prop{it->second};
		if(pid.key=="root" &&
				prop.substr(0, attach_tag.size())==attach_tag) {
			auto& vert=_vertices.at(pid.node);
			fix_type.emplace(pid.node, vert.attr.misc.t());
			prop.remove_prefix(attach_tag.size());
			gapr::node_id n1, n2{};
			auto res=std::from_chars(prop.data(), &*prop.end(), n1.data);
			if(res.ec!=std::errc{})
				throw std::system_error{std::make_error_code(res.ec)};
			prop.remove_prefix(res.ptr-prop.data());
			if(prop.empty()) {
				fix_conn.emplace(pid.node, gapr::link_id{n1, n2});
			} else {
				if(prop[0]!='-')
					throw std::runtime_error{"wrong attachment format"};
				res=std::from_chars(prop.data()+1, &*prop.end(), n2.data);
				if(res.ec!=std::errc{})
					throw std::system_error{std::make_error_code(res.ec)};
				if(res.ptr!=&*prop.end())
					throw std::runtime_error{"wrong attachment format 2"};
				fix_conn.emplace(pid.node, gapr::link_id{n1, n2});
			}
		}
	}
	for(auto& [eid, e]: _edges) {
		if(auto it=fix_type.find(e.root); it!=fix_type.end()) {
			auto t=it->second;
			for(auto& n: e.points) {
				gapr::node_attr a{n};
				if(!a.misc.t()) {
					a.misc.t(t);
					n=a.data();
				}
			}
		}
	}
	for(auto& [vid, v]: _vertices) {
		if(auto it=fix_type.find(v.root); it!=fix_type.end()) {
			auto t=it->second;
			if(!v.attr.misc.t())
				v.attr.misc.t(t);
		}
	}

	for(auto [v1, l]: fix_conn) {
		assert(l);
		bool del{false};
		if(l.on_node()) {
			vertex_id v2;
			auto pos=PRIV::get_position(*this, l.nodes[0].data);
			if(pos.edge)
				v2=PRIV::gen_vert_split(*this, pos.edge, pos.index/128);
			else if(pos.vertex)
				v2=pos.vertex;
			if(v2) {
				del=true;
				auto vert1=PRIV::chg_vert(*this, v1);
				if(!vert1)
					throw std::runtime_error{"failed to chg vert"};
				auto vert2=PRIV::chg_vert(*this, v2);
				if(!vert2)
					throw std::runtime_error{"failed to chg vert 2"};
				edge edg{v1, v2};
				edg.points.resize(2);
				edg.nodes.push_back(v1);
				edg.nodes.push_back(v2);
				auto eid=PRIV::add_edge(*this, std::move(edg));
				if(!eid)
					throw std::runtime_error{"failed to connect"};
				vert1->edges.emplace_back(eid, false);
				vert2->edges.emplace_back(eid, true);
			}
		} else {
			// XXX handle link attach
			// insert vertex at the link
			assert(0);
		}
		if(del)
			_props.erase(gapr::prop_id{v1, "root"});
	}
	PRIV::apply(*this);
}

const unsigned char gapr::node_props::asso_values[256] {
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13,  5, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13,  2, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13,  5,  0, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
	13, 13, 13, 13, 13, 13
};
