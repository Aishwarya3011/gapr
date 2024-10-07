/* gapr/edge-model.hh
 *
 * Copyright (C) 2019 GOU Lingfeng
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

//@@@@
#ifndef _GAPR_INCLUDE_EDGE_MODEL_HH_
#define _GAPR_INCLUDE_EDGE_MODEL_HH_

#include "gapr/config.hh"
#include "gapr/commit.hh"

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "gapr/detail/model-props.hh"


/*! Edge model: NEEDED for convenient rending and manipulation.
 *
 * threads:
 *   - prepare(thread_pool) read
 *   - update(ui) write
 *   - *render(ui) read
 *   - *query(ui) read
 *
 * manipulations happen mostly at (sub)edge level
 *
 * props: nid
 * position: edge+index (maybe vertex)
 * delta.anchor: anchor
 * 
 * commit apply?
 * delayed merge
 * vector<node_attr>GL_LINE_STRIP
 *   glDrawArrays *   
 *   most op handle sub_edge
 *
 *
 * XXX
 * - two copies (with shared edges) and alternate?
 *   easy to manip. hard to incremental upd.
 * - base and delta
 *   easy for incremental ops.
 * so underlying base and delta, but interface to change as a copy.
 *
 */

namespace gapr {

	class mem_file;

	class edge_model {
		public:
			class view;
			class reader;
			class loader;
			class updater;
			using point=gapr::node_attr::data_type;
			using edge_id=uint32_t;
			using vertex_id=gapr::node_id;
			using prop_id=gapr::prop_id;

			struct vertex {
				gapr::node_attr attr;
				std::vector<std::pair<edge_id, bool>> edges;
				explicit vertex(std::nullptr_t) noexcept { }
				vertex(gapr::node_attr attr, std::vector<std::pair<edge_id, bool>>&& edges) noexcept:
					attr{attr}, edges{std::move(edges)},
					root{}, parent_e{0}, loop{false}, raised{false}, complete{false}
				{ }

				vertex_id root;
				edge_id parent_e;
				bool loop;
				bool raised;
				bool complete;
			};

			struct edge {
				vertex_id left, right;
				std::vector<gapr::node_id> nodes;
				std::vector<point> points;
				explicit edge(std::nullptr_t) noexcept { }
				edge(vertex_id left, vertex_id right) noexcept:
					left{left}, right{right}, nodes{}, points{},
					root{}, parent_v{}, loop{false}, raised{false}
				{ }

				vertex_id root;
				vertex_id parent_v;
				bool loop;
				bool raised;
#if 0
				int vaoi;
				std::size_t length() const;
#endif
			};

			struct position {
				edge_id edge;
				union {
					uint32_t index;
					vertex_id vertex;
				};

				constexpr position() noexcept: edge{0}, vertex{0} { }
				constexpr explicit position(vertex_id vert) noexcept:
					edge{0}, vertex{vert} { }
				constexpr position(edge_id edge, uint32_t index) noexcept:
					edge{edge}, index{index} { }

				constexpr bool on_edge() const noexcept { return edge!=0; }
			};

			void gen_(double x, double y, double z);
			GAPR_CORE_DECL void apply_attach();

			std::size_t nocache_inc(std::size_t inc) noexcept {
				return _nocache_siz.fetch_add(inc)+inc;
			}
			void nocache_dec(std::size_t inc) noexcept {
				_nocache_siz.fetch_sub(inc);
			}

		private:

			std::unordered_map<gapr::node_id, position> _nodes; //XXX maybe vector???
			std::unordered_map<vertex_id, vertex> _vertices;
			std::unordered_map<edge_id, edge> _edges;
			gapr::node_props _props;
			std::vector<std::string> _logs;
			bool _raised{false};
			std::atomic<std::size_t> _nocache_siz{0};

			std::unordered_set<edge_id> _edges_filter;
			std::unordered_set<vertex_id> _vertices_filter;
			std::unordered_set<prop_id> _props_filter;

			// staging
			using node_mod=std::pair<int, position>;
			using vertex_mod=std::pair<int, vertex>;
			using edge_mod=std::pair<int, edge>;
			using prop_mod=std::pair<int, std::string>;

#if 0
		struct prop_mod {
			int state;
			decltype(_props.end()) iter;
			std::string val;
		};
		struct nref_mod {
			decltype(_nodes.end()) iter;
			unsigned int nref;
		};
#endif
		std::unordered_map<gapr::node_id, node_mod> _nodes_d;
		std::unordered_map<vertex_id, vertex_mod> _vertices_d;
		std::unordered_map<edge_id, edge_mod> _edges_d;
		std::unordered_map<prop_id, prop_mod> _props_d;
		std::vector<std::string> _logs_d;

		std::array<double, 6> _filter_bbox{0.0, 0.0, 0.0, -1.0};
		int _filter_stat{0};
		std::unordered_map<edge_id, int> _edges_filter_d;
		std::unordered_map<vertex_id, int> _vertices_filter_d;
		std::unordered_map<prop_id, int> _props_filter_d;

		edge_id _eid_alloc{0};
		std::vector<edge_id> _eid_avail;
		std::vector<edge_id> _eid_free;

		//update info
		gapr::node_id _nid0;
		std::vector<edge_id> _edges_del;
		std::vector<gapr::node_id> _trees_del;
		std::vector<std::pair<gapr::node_id, std::string>> _trees_add;
		std::vector<std::pair<gapr::node_id, std::string>> _trees_chg;
		std::unordered_multimap<gapr::node_id, gapr::node_id> _cc_helper;
			
			position to_position(gapr::node_id id) const {
				// XXX
				auto it=_nodes.find(id);
				if(it==_nodes.end())
					return {};
				return it->second;
#if 0
				auto& pos=_nodes.at(id);
				return pos;
#endif
			}
			gapr::node_attr get_attr(position pos) const {
				if(pos.edge) {
					auto e=_edges.at(pos.edge);
					return gapr::node_attr{e.points[pos.index/128]};
				} else {
					if(pos.vertex) {
						auto v=_vertices.at(pos.vertex);
						return v.attr;
					} else {
						assert(0);
						return {};
					}
				}
			}

			anchor_id to_anchor(position pos) const {
				if(pos.edge) {
					auto& edge=_edges.at(pos.edge);
					auto i=pos.index/128;
					uint8_t j=pos.index%128;
					if(j==0)
						return node_id{edge.nodes[i]};
					return anchor_id{edge.nodes[i], edge.nodes[i+1], j};
				} else if(pos.vertex) {
					return node_id{pos.vertex};
				} else {
					return {};
				}
			}
			mutable std::shared_mutex _mutex;
			mutable unsigned int _pending_write{0};
			mutable std::mutex _pending_mtx;
			mutable std::condition_variable _pending_cv;
			std::shared_lock<std::shared_mutex> lock_read() const {
				std::unique_lock lck{_pending_mtx};
				while(_pending_write>0)
					_pending_cv.wait(lck);
				return std::shared_lock{_mutex};
			}
			std::unique_lock<std::shared_mutex> lock_write() const {
				{
					std::unique_lock lck{_pending_mtx};
					++_pending_write;
				}
				return std::unique_lock{_mutex};
			}
			void release_write(std::unique_lock<std::shared_mutex>&& lck2) const {
				lck2.unlock();
				{
					std::unique_lock lck{_pending_mtx};
					if(--_pending_write==0)
						_pending_cv.notify_all();
				}
			}
			mutable std::mutex _mtx_d;
			template<gapr::delta_type Typ> GAPR_CORE_DECL bool load(gapr::node_id nid0, gapr::delta<Typ>&& delta);
			struct PRIV;
	};

	class edge_model::view {
		public:
			const std::unordered_map<edge_id, edge>& edges() const noexcept {
				return _model._edges;
			}
			const std::unordered_map<vertex_id, vertex>& vertices() const noexcept {
				return _model._vertices;
			}
			const auto& props() const noexcept {
				return _model._props;
			}
			const std::vector<std::string>& logs() const noexcept {
				return _model._logs;
			}
			position to_position(gapr::node_id id) const {
				return _model.to_position(id);
			}
			gapr::node_attr get_attr(position pos) const {
				return _model.get_attr(pos);
			}
			anchor_id to_anchor(position pos) const {
				return _model.to_anchor(pos);
			}
			const std::unordered_map<gapr::node_id, position>& nodes() const noexcept {
			  return _model._nodes;
			}

			const std::unordered_set<edge_id>& edges_filter() const noexcept {
			  	return _model._edges_filter;
			}
			const auto& vertices_filter() const noexcept {
				return _model._vertices_filter;
			}
			const auto& props_filter() const noexcept {
				return _model._props_filter;
			}

			bool raised() const noexcept { return _model._raised; }

			//use const edge_model*, not this
			const edge_model* operator->() const {
				return &_model;
			}

			GAPR_CORE_DECL gapr::mem_file dump_state(int64_t ncommits) const;
			GAPR_CORE_DECL bool equal(const view other) const;

		private:
			const edge_model& _model;
			constexpr explicit view(const edge_model& model) noexcept:
				_model{model} { }
			friend class edge_model;
			//friend class edge_model::updater;
	};

	/*! only in UI thread, and read-only */
	class edge_model::reader: public edge_model::view {
		public:
			explicit reader(const edge_model& model):
				view{model}, _lock{model.lock_read()} { }
			~reader() { }
			reader(const reader&) =delete;
			reader& operator=(const reader&) =delete;

		private:
			std::shared_lock<std::shared_mutex> _lock;
	};

	class GAPR_CORE_DECL edge_model::loader: public edge_model::reader {
		public:
			explicit loader(edge_model& model):
				reader{model}, _lock_d{model._mtx_d} { }
			~loader() { }
			loader(const loader&) =delete;
			loader& operator=(const loader&) =delete;

			//add&&/del&/chg&
			template<bool Leader=false, gapr::delta_type Typ>
			bool load(gapr::node_id nid0, gapr::delta<Typ>&& delta) {
				auto& model=const_cast<edge_model&>(_model);
				if constexpr(Leader) {
					model._nid0=nid0;
				}
				return const_cast<edge_model&>(_model).load(nid0, std::move(delta));
			}
			bool load_file(gapr::mem_file file);

			uint64_t init(std::streambuf& file);

			bool filter(const std::array<double, 6>& bbox);
			bool filter();
			bool merge();
			bool highlight_loop(edge_id edg);
			bool highlight_neuron(edge_id edg, int direction);
			bool highlight_raised();
			bool highlight_orphan(edge_id edg);
		private:
			std::unique_lock<std::mutex> _lock_d;
	};

	class GAPR_CORE_DECL edge_model::updater: public edge_model::view {
		public:
			explicit updater(edge_model& model):
				view{model}, _lock{model.lock_write()}, _lock2{model._mtx_d} { }
			~updater();
			updater(const updater&) =delete;
			updater& operator=(const updater&) =delete;

			bool empty() const noexcept {
				return _model._vertices_d.empty()
					&& _model._edges_d.empty()
					&& _model._props_d.empty()
					&& _model._filter_stat==0;
			}

			bool apply();

			const std::vector<edge_id>& edges_del() const noexcept {
				return _model._edges_del;
			}
			const std::vector<gapr::node_id>& trees_del() const noexcept {
				return _model._trees_del;
			}
			const std::vector<std::pair<gapr::node_id, std::string>>& trees_add() const noexcept {
				return _model._trees_add;
			}
			const std::vector<std::pair<gapr::node_id, std::string>>& trees_chg() const noexcept {
				return _model._trees_chg;
			}
			gapr::node_id nid0() const noexcept {
				return _model._nid0;
			}
			void reset_filter();

		private:
			std::unique_lock<std::shared_mutex> _lock;
			std::unique_lock<std::mutex> _lock2;
	};
}

#endif

