
namespace {

[[maybe_unused]] bool filter_next_pos(gapr::node_attr pos, const std::array<double, 6>& _filter_bbox) noexcept {
	for(unsigned int i=0; i<3; i++) {
		auto x=pos.pos(i);
		if(x<=_filter_bbox[i]+6)
			return false;
		if(x>=_filter_bbox[3+i]-6)
			return false;
	}
	return true;
}

struct pred_empty_err {
	bool operator()(const gapr::prop_id& key, const std::string& val) {
		return key.key=="error" && val=="";
	}
};

struct pred_empty_end {
	bool operator()(gapr::edge_model::view model, gapr::edge_model::vertex_id vid, const gapr::edge_model::vertex& vert) {
		if(vert.edges.size()>=2)
			return false;
		if(model.props().find(gapr::prop_id{vid, "state"})!=model.props().end())
			return false;
		return true;
	}
};

struct pred_no_coverage {
	bool operator()(const gapr::edge_model::edge& edg, std::size_t& idx) {
		gapr::node_attr a{edg.points[idx]};
		if(a.misc.coverage())
			return false;
		if(idx>0) {
			gapr::node_attr b{edg.points[idx-1]};
			if(b.misc.coverage()) {
				--idx;
				return true;
			}
		} else {
			return true;
		}
		if(idx+1<edg.points.size()) {
			gapr::node_attr b{edg.points[idx+1]};
			if(b.misc.coverage()) {
				++idx;
				return true;
			}
		} else {
			return true;
		}
		return false;
	}
};

template<unsigned int V1, unsigned int V2> struct pred_defer_err {
	gapr::edge_model::view model;
	unsigned int operator()(const gapr::prop_id& key, const std::string& val) {
		if(key.key=="state" && val!="end") {
			gapr::prop_id ekey{key.node, "error"};
			if(model.props().find(ekey)==model.props().end())
				return V2;
			return 1024;
		}
		if(key.key!="error")
			return 1024;
		if(val=="")
			return V1;
		if(val=="deferred")
			return V2;
		return 1024;
	}
};

}

gapr::node_id find_next_node(
		gapr::edge_model::view model,
		gapr::node_id cur_node,
		const gapr::node_attr& cur_pos);
gapr::node_id find_next_error(
		gapr::edge_model::view model,
		gapr::node_id cur_node,
		const gapr::node_attr& cur_pos);

gapr::node_id find_next_node(
		gapr::edge_model::view model,
		gapr::node_id cur_node,
		const gapr::node_attr& cur_pos,
		bool include_err,
		const std::unordered_set<gapr::node_id>& masked,
		const std::array<double, 6>& bbox);
gapr::node_id get_seed_pos_random(gapr::edge_model::view model, bool include_err, unsigned long rand);

struct FixPos {
	enum {
		Empty,
		Future,
		Null,
		Node
	} state;
	union {
		unsigned int offset;
		gapr::node_id node;
	};
	FixPos() noexcept: state{Empty} { }
	FixPos(gapr::node_id node) noexcept: state{Node}, node{node} { }
	FixPos(unsigned int offset) noexcept: state{Future}, offset{offset} { }
	FixPos(std::nullptr_t) noexcept: state{Null} { }
	explicit operator bool() const noexcept { return state!=Empty; }
};

class PreLock {
	/*! eg. for graph model
	 * compute(thread_pool): read
	 * update_prepare(thread_pool): read
	 * update_apply(ui_thread): write
	 * inspect(ui_thread): read
	 * draw(ui_thread): read
	 */
	public:
		constexpr explicit PreLock() noexcept: _counter{0} { }
		bool can_read_async() const noexcept { return _counter<_max; }
		bool can_write_later() const noexcept { return _counter==0; }
		bool can_write_now() const noexcept { return _counter==0; }

		void begin_read_async() noexcept { _counter+=1; }
		void end_read_async() noexcept { _counter-=1; }

		void begin_write_later() noexcept { _counter=_max; }
		void end_write_later() noexcept { _counter=0; }

	private:
		unsigned int _counter;
		constexpr static auto _max=std::numeric_limits<unsigned int>::max();
};

[[maybe_unused]] static void calc_bbox(const gapr::affine_xform& xform, const std::array<uint32_t, 6> bboxi, std::array<double, 6>& bbox) {
	gapr::print(1, "calc bbox", bboxi[0], ',', bboxi[1], ',', bboxi[2], ',', bboxi[3], ',', bboxi[4], ',', bboxi[5]);
	for(unsigned int i=0; i<3; i++) {
		bbox[i]=INFINITY;
		bbox[3+i]=-INFINITY;
	}
	for(unsigned int idx=0; idx<8; idx++) {
		double x=bboxi[0], y=bboxi[1], z=bboxi[2];
		if(idx&1)
			x=bboxi[3];
		if(idx&2)
			y=bboxi[4];
		if(idx&4)
			z=bboxi[5];
		auto pos=xform.from_offset_f({x, y, z});
		for(unsigned int i=0; i<3; i++) {
			auto v=pos[i];
			if(v<bbox[i])
				bbox[i]=v;
			if(v>bbox[3+i])
				bbox[3+i]=v;
		}
	}
	gapr::print(1, "calc bbox", bbox[0], ',', bbox[1], ',', bbox[2], ',', bbox[3], ',', bbox[4], ',', bbox[5]);
}


template<typename F1, typename F2, typename F3>
static void sel_proofread_nodes(gapr::edge_model::view model, const gapr::fix::Position& cur_pos, F1 pick_tgt, F2 sel_nodes, F3 chk_rng, double max_len) {
	if(max_len==0)
		return;
	auto chk_cov=[](auto& e, unsigned int i) {
		gapr::node_attr a{e.points[i]};
		return a.misc.coverage();
	};
	auto chk_pos_rng=[&chk_rng](auto& e, unsigned int i) {
		gapr::node_attr a{e.points[i]};
		return chk_rng(a.pos());
	};
	auto cur_pos_vec=model.get_attr(cur_pos).pos();
	auto chk_len=[cur_pos_vec](auto& e, unsigned int i) {
		gapr::node_attr a{e.points[i]};
		return (a.pos()-cur_pos_vec).mag();
	};

	auto chk_end=[&model,chk_cov](auto& e, unsigned int i) {
		gapr::node_id vid{};
		if(i==0) {
			vid=e.left;
		} else if(i+1==e.nodes.size()) {
			vid=e.right;
		} else {
			return true;
		}
		unsigned int h{0};
		auto& v=model.vertices().at(vid);
		for(auto [eid, dir]: v.edges) {
			auto& ee=model.edges().at(eid);
			if(&ee==&e)
				continue;
			if(!chk_cov(ee, dir?ee.nodes.size()-2:1))
				++h;
		}
		return h<=0;
	};
	gapr::node_id v{};
	if(cur_pos.edge) {
		auto& cur_edg=model.edges().at(cur_pos.edge);
		auto sel_rng=[&cur_edg,sel_nodes](unsigned int l, unsigned int r) {
			std::vector<std::pair<gapr::node_id, gapr::node_attr>> nodes_sel;
			for(auto i=l; i<=r; ++i) {
			nodes_sel.emplace_back(cur_edg.nodes[i], gapr::node_attr{cur_edg.points[i]});
			}
			sel_nodes(std::move(nodes_sel));
		};
		auto sel_tgt=[&cur_edg,&cur_pos,pick_tgt](unsigned int i) {
			if(i==0 || i+1==cur_edg.points.size()) {
				pick_tgt(gapr::fix::Position{cur_edg.nodes[i], cur_edg.points[i]});
				return;
			}
			pick_tgt(gapr::fix::Position{cur_pos.edge, i*128, cur_edg.points[i]});
		};
		auto i=cur_pos.index/128;
		unsigned int tgt_i=i;
		if(chk_cov(cur_edg, i-1)) {
			if(chk_cov(cur_edg, i+1)) {
				if(chk_cov(cur_edg, i))
					return;
				return sel_tgt(i+1);
			} else {
				tgt_i=i+1;
				while(tgt_i+1<cur_edg.nodes.size()) {
					if(!chk_pos_rng(cur_edg, tgt_i))
						break;
					++tgt_i;
				}
				while(tgt_i>i+1) {
					if(!chk_cov(cur_edg, tgt_i-1))
						break;
					--tgt_i;
				}
				if(chk_len(cur_edg, tgt_i)>max_len+5) {
					while(tgt_i>i+1) {
						--tgt_i;
						if(chk_len(cur_edg, tgt_i)<=max_len)
							break;
					}
				}
				while(tgt_i>i+1) {
					if(!chk_cov(cur_edg, tgt_i-1))
						break;
					--tgt_i;
				}
				if(chk_end(cur_edg, tgt_i))
					return sel_tgt(tgt_i);
				v=cur_edg.right;
			}
		} else {
			if(chk_cov(cur_edg, i+1)) {
				tgt_i=i-1;
				while(tgt_i>0) {
					if(!chk_pos_rng(cur_edg, tgt_i))
						break;
					--tgt_i;
				}
				while(tgt_i<i-1) {
					if(!chk_cov(cur_edg, tgt_i))
						break;
					++tgt_i;
				}
				if(chk_len(cur_edg, tgt_i)>max_len+5) {
					while(tgt_i<i-1) {
						++tgt_i;
						if(chk_len(cur_edg, tgt_i)<=max_len)
							break;
					}
				}
				while(tgt_i<i-1) {
					if(!chk_cov(cur_edg, tgt_i))
						break;
					++tgt_i;
				}
				if(chk_end(cur_edg, tgt_i))
					return sel_tgt(tgt_i);
				v=cur_edg.left;
			} else {
				auto l=i-1;
				auto r=i+1;
				// XXX
				while(l>0) {
					--l;
					if(chk_cov(cur_edg, l))
						break;
				}
				while(r+1<cur_edg.nodes.size()) {
					++r;
					if(chk_cov(cur_edg, r))
						break;
				}
				if(chk_end(cur_edg, l)) {
					if(chk_end(cur_edg, r))
						return sel_rng(l, r);
					v=cur_edg.right;
				} else {
					v=cur_edg.left;
				}
			}
		}
	} else if(cur_pos.vertex) {
		v=cur_pos.vertex;
	} else {
		return;
	}
	auto& vv=model.vertices().at(v);
	auto vv_vec=vv.attr.pos();
	auto chk_len_alt=[cur_pos_vec,vv_vec](auto& e, unsigned int i, bool same) {
		gapr::node_attr a{e.points[i]};
		if(same) {
			return (a.pos()-cur_pos_vec).mag();
		} else {
			return (a.pos()-vv_vec).mag()+(vv_vec-cur_pos_vec).mag();
		}
	};
	std::vector<std::pair<gapr::node_id, gapr::node_attr>> nodes_sel;
	for(auto [eid, dir]: vv.edges) {
		auto& e=model.edges().at(eid);
		if(dir) {
			unsigned int tgt_i=e.nodes.size()-1;
			while(tgt_i>0) {
				if(!chk_pos_rng(e, tgt_i))
					break;
				--tgt_i;
			}
			while(tgt_i<e.nodes.size()-1) {
				if(!chk_cov(e, tgt_i))
					break;
				++tgt_i;
			}
			if(chk_len_alt(e, tgt_i, eid==cur_pos.edge)>max_len+5) {
				while(tgt_i<e.nodes.size()-1) {
					++tgt_i;
					if(chk_len_alt(e, tgt_i, eid==cur_pos.edge)<=max_len)
						break;
				}
			}
			while(tgt_i<e.nodes.size()-1) {
				if(!chk_cov(e, tgt_i))
					break;
				++tgt_i;
			}
			for(auto i=tgt_i; i<e.nodes.size()-1; ++i)
				nodes_sel.emplace_back(e.nodes[i], gapr::node_attr{e.points[i]});
		} else {
			unsigned int tgt_i=0+1;
			while(tgt_i+1<e.nodes.size()) {
				if(!chk_pos_rng(e, tgt_i))
					break;
				++tgt_i;
			}
			while(tgt_i>0+1) {
				if(!chk_cov(e, tgt_i-1))
					break;
				--tgt_i;
			}
			if(chk_len_alt(e, tgt_i, eid==cur_pos.edge)>max_len+5) {
				while(tgt_i>0+1) {
					--tgt_i;
					if(chk_len_alt(e, tgt_i, eid==cur_pos.edge)<=max_len)
						break;
				}
			}
			while(tgt_i>0+1) {
				if(!chk_cov(e, tgt_i-1))
					break;
				--tgt_i;
			}
			for(unsigned int i=1; i<=tgt_i; ++i)
				nodes_sel.emplace_back(e.nodes[i], gapr::node_attr{e.points[i]});
		}
	}
	if(nodes_sel.empty())
		return;
	for([[maybe_unused]] auto& [vid, a]: nodes_sel)
		assert(vid!=v);
	nodes_sel.emplace_back(v, vv.attr);
	sel_nodes(std::move(nodes_sel));
}

[[maybe_unused]] static void save_cache_file(const std::filesystem::path& cachepath, std::streambuf& sb) {
	gapr::print("state_cache saving cache.............");
	auto tmppath=cachepath;
	tmppath.replace_extension(".temp");
	std::ofstream ofs{tmppath, std::ios::binary};
	ofs<<&sb;
	ofs.close();
	if(ofs)
		std::filesystem::rename(tmppath, cachepath);
}

