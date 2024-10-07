namespace {
	class DeltaStats {
		public:
			constexpr DeltaStats() noexcept { }
			void add(const gapr::delta_add_edge_& delta) noexcept {
				_num_add_edge+=1;
				for(auto t_: {delta.left, delta.right}) {
					gapr::link_id t{t_};
					if(t) {
						if(t.on_node()) {
							_num_add_edge_term_node+=1;
						} else {
							_num_add_edge_term_link+=1;
						}
					} else {
						_num_add_edge_term_void+=1;
					}
				}
				_num_add_edge_nodes+=delta.nodes.size();
			}
			void add(const gapr::delta_add_prop_& delta) noexcept {
				_num_add_prop+=1;
				gapr::link_id t{delta.link};
				if(t) {
					if(t.on_node()) {
						_num_add_prop_node+=1;
					} else {
						_num_add_prop_link+=1;
					}
				} else {
					_num_add_prop_void+=1;
				}
			}
			void add(const gapr::delta_chg_prop_& delta) noexcept {
				_num_chg_prop+=1;
			}
			void add(const gapr::delta_add_patch_& delta) noexcept {
				_num_add_patch+=1;
			}
			void add(const gapr::delta_del_patch_& delta) noexcept {
				_num_del_patch+=1;
				_num_del_patch_props+=delta.props.size();
				auto nn=delta.nodes.size();
				switch(nn) {
					case 0:
						break;
					case 1:
						_num_del_patch_terms=1;
						break;
					case 2:
						assert(delta.nodes[0]!=delta.nodes[1]);
						_num_del_patch_links+=1;
						break;
					default:
						if(delta.nodes[nn-1]==delta.nodes[nn-2]) {
							_num_del_patch_terms+=1;
							nn-=1;
						}
						if(delta.nodes[0]==delta.nodes[1]) {
							_num_del_patch_terms+=1;
							nn-=1;
						}
						_num_del_patch_links+=nn-1;
						break;
				}
			}
			void add(const gapr::delta_proofread_& delta) noexcept {
				_num_proofread+=1;
				_num_proofread_nodes+=delta.nodes.size();
			}
			void add(const gapr::delta_reset_proofread_0_& delta) noexcept { }
			void add(const gapr::delta_reset_proofread_& delta) noexcept { }
			double score() const noexcept {
				return 0.433*_num_add_edge_nodes
					+2.820*_num_add_prop
					+0.696*_num_del_patch_links
					+0.145*_num_proofread_nodes;
			}
			unsigned int score_pr() const noexcept {
				return _num_proofread_nodes;
			};
			unsigned int score_rep() const noexcept {
				return _num_add_prop;
			}
		private:
			unsigned int _num_add_edge{0};
			unsigned int _num_add_edge_term_node{0};
			unsigned int _num_add_edge_term_link{0};
			unsigned int _num_add_edge_term_void{0};
			unsigned int _num_add_edge_nodes{0};
			unsigned int _num_add_prop{0};
			unsigned int _num_add_prop_node{0};
			unsigned int _num_add_prop_link{0};
			unsigned int _num_add_prop_void{0};
			unsigned int _num_chg_prop{0};
			unsigned int _num_add_patch{0};
			unsigned int _num_del_patch{0};
			unsigned int _num_del_patch_props{0};
			unsigned int _num_del_patch_terms{0};
			unsigned int _num_del_patch_links{0};
			unsigned int _num_proofread{0};
			unsigned int _num_proofread_nodes{0};
			friend std::ostream& operator<<(std::ostream& ost, const DeltaStats& stats);
	};
	std::ostream& operator<<(std::ostream& ost, const DeltaStats& stats) {
		char buf[512];
		snprintf(buf, 512,
				"stats: %.2lf %u %u %u %u %u; %u %u %u %u; %u; %u; %u %u %u %u; %u %u",
				stats.score(),
				stats._num_add_edge,
				stats._num_add_edge_term_node,
				stats._num_add_edge_term_link,
				stats._num_add_edge_term_void,
				stats._num_add_edge_nodes,
				stats._num_add_prop,
				stats._num_add_prop_node,
				stats._num_add_prop_link,
				stats._num_add_prop_void,
				stats._num_chg_prop,
				stats._num_add_patch,
				stats._num_del_patch,
				stats._num_del_patch_props,
				stats._num_del_patch_terms,
				stats._num_del_patch_links,
				stats._num_proofread,
				stats._num_proofread_nodes);
		return ost<<buf;
	}
}
