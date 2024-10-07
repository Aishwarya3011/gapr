/* gapr/commit.hh
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
#ifndef _GAPR_INCLUDE_COMMIT_HH_
#define _GAPR_INCLUDE_COMMIT_HH_

#include "gapr/config.hh"

#include "gapr/model.hh"

#include <ctime>
#include <deque>
#include <cassert>
#include <vector>
#include <chrono>


namespace gapr {

	using nid_t=node_id::data_type;

	using link_t=link_id;

	using anchor_t=anchor_id;

	struct commit_id {
		/*! no more commits than nodes */
		using data_type=uint32_t;
		data_type data;
	};

	class commit_history {
		public:
			commit_history() noexcept: _base{0}, _sparse{} { }

			commit_id::data_type body_count() const noexcept { return _base; }
			void try_body_count(commit_id::data_type v) noexcept {
				assert(v!=0);
				assert(v>=_base);
				if(v==_base)
					return;
				body_count(v);
			}
			void body_count(commit_id::data_type v) noexcept {
				assert(v!=0);
				assert(v>_base);
				while(!_sparse.empty()) {
					auto v1=_sparse.back();
					if(v1>v)
						break;
					_sparse.pop_back();
					if(v1==v)
						v=v1+1;
				}
				_base=v;
			}
			const std::deque<commit_id::data_type>& tail() const noexcept {
				return _sparse;
			}
			commit_id::data_type tail_tip() const noexcept {
				return _sparse.empty()?0:_sparse.front();
			}
			void add_tail(commit_id::data_type v) {
				if(_sparse.empty()) {
					assert(v>=_base);
					if(v==_base) {
						_base+=1;
						return;
					}
				} else {
					assert(v>_sparse.front());
				}
				_sparse.push_front(v);
			}
			void try_add_tail(commit_id::data_type v) {
				if(_sparse.empty()) {
					assert(v>_base);
				} else {
					assert(v>=_sparse.front());
					if(v==_sparse.front())
						return;
				}
				_sparse.push_front(v);
			}

			GAPR_CORE_DECL bool save(std::streambuf& str) const;
			GAPR_CORE_DECL bool load(std::streambuf& str);

		private:
			commit_id::data_type _base;
			std::deque<commit_id::data_type> _sparse;
	};

	enum class delta_type: uint16_t {
			invalid=0,
			//Stable

			___UNSTABLE_TYPES___=1024,
			add_edge_,
			add_prop_,
			chg_prop_,
			add_patch_,
			del_patch_,
			proofread_,
			reset_proofread_0_,
			reset_proofread_,
#if 0
			add_node_,
			del_edge_,
			del_annot_,
			chg_node_attr_,
			chg_link_attr_,
			//
			add_node_in_link_,
			densify_edge_,
			sparsify_edge_,
#endif
	};
	GAPR_CORE_DECL std::string_view to_string(delta_type type) noexcept;

	struct commit_info {
		commit_id::data_type id;
		std::string who;
		uint64_t when;
		/*! stays here, not in deltas
		 * 1. deltas then have no unfixed fields.
		 * 2. not necessary in some deltas, but useful to validate node id.
		 */
		node_id::data_type nid0;
		std::underlying_type_t<delta_type> type;

		GAPR_CORE_DECL bool save(std::streambuf& str) const;
		GAPR_CORE_DECL bool load(std::streambuf& str);
	};

	GAPR_CORE_DECL uint64_t to_timestamp(const std::chrono::system_clock::time_point& t);
	GAPR_CORE_DECL std::chrono::system_clock::time_point from_timestamp(uint64_t ts);
	GAPR_CORE_DECL char* timestamp_to_chars(char* first, char* last, uint64_t ts, bool full);

	class mem_file;

	struct model_state {
		struct edge {
			std::vector<node_id> nodes;
			std::vector<node_attr::data_type> points;
		};
		commit_id::data_type size;
		std::vector<edge> edges;
		std::vector<std::pair<node_id::data_type, std::string>> props;
		std::vector<std::string> logs;

		GAPR_CORE_DECL bool save(std::streambuf& str) const;
		GAPR_CORE_DECL bool load(std::streambuf& str);
		int cannolize() noexcept;
	};

	template<gapr::delta_type Typ> struct delta;

	/*! for all delta types
	 * put lengthy members after simple ones.
	 * serialize by the order of member decl.
	 * do not take difference between commit and model.
	 * for *dense* vector members, take difference and use prediction.
	 * vector members that is sparse, do not take difference.
	 */

	template<> struct delta<delta_type::add_edge_> {
		/*! terminal
		 * none: new terminal
		 * node: attach
		 * on link: new node
		 */
		/*! finished terminals are tracked with whitelist,
		 * so no changing props here
		 */
		link_id::data_type left, right;
		std::vector<node_attr::data_type> nodes;
	};

	template<> struct delta<delta_type::add_prop_> {
		/*! like add_edge terminal */
		link_id::data_type link;
		node_attr::data_type node;
		std::string prop;
		delta<delta_type::add_prop_>() noexcept { }
	};

	template<> struct delta<delta_type::chg_prop_> {
		node_id::data_type node;
		std::string prop;
	};

	template<> struct delta<delta_type::add_patch_> {
		/*! new nodes attached to links */
		std::vector<std::pair<node_id::data_type, link_id::data_type>> links;
		/*! new node properties
		 * XXX if node_id is type_max, add a log message.  eg. if a cube is processed.
		 */
		std::vector<std::pair<node_id::data_type, std::string>> props;
		/*! new nodes (temp id 1 to N), and parent node id */
		std::vector<std::pair<node_attr::data_type, node_id::data_type>> nodes;
	};

	template<> struct delta<delta_type::del_patch_> {
		// **edge // **graph // ***link // ***node // ***tree
		// **nodes/**links/**props
		std::vector<std::pair<node_id::data_type, std::string>> props;
		std::vector<node_id::data_type> nodes;
	};

	template<> struct delta<delta_type::proofread_> {
		std::vector<node_id::data_type> nodes;
	};

	template<> struct delta<delta_type::reset_proofread_0_> {
		std::vector<node_id::data_type> nodes;
		std::vector<std::pair<node_id::data_type, std::string>> props;
	};

	template<> struct delta<delta_type::reset_proofread_> {
		std::vector<node_id::data_type> nodes;
		std::vector<std::pair<node_id::data_type, std::string>> props;
	};

	GAPR_CORE_DECL std::ostream& operator<<(std::ostream& str, const commit_info& info);

	template<typename T, delta_type Typ=delta_type::invalid>
		struct delta_traits;
	template<delta_type Typ>
		struct GAPR_CORE_DECL delta_traits<void, Typ> {
			static constexpr std::integral_constant<delta_type, Typ> type{};
			//bool save(gapr::mem_file&& file) noexcept;
			static bool save(const delta<Typ>& delta, std::streambuf& str);
			static bool load(delta<Typ>& delta, std::streambuf& str);
			/*! <0, fail; 0: already ok; >0, fixed */
			static int cannolize(delta<Typ>& delta) noexcept;
			static std::ostream& dump(const delta<Typ>& delta, std::ostream& str, unsigned int level, node_id nid0);
		};
	template<delta_type Typ>
		struct delta_traits<delta<Typ>, delta_type::invalid>:
		delta_traits<void, Typ> { };

	template<delta_type Typ>
		inline bool save(const delta<Typ>& delta, std::streambuf& str) {
			return delta_traits<void, Typ>::save(delta, str);
		}
	template<delta_type Typ>
		inline bool load(delta<Typ>& delta, std::streambuf& str) {
			return delta_traits<void, Typ>::load(delta, str);
		}
	template<delta_type Typ>
		inline int cannolize(delta<Typ>& delta) noexcept {
			return delta_traits<void, Typ>::cannolize(delta);
		}
	template<delta_type Typ>
		inline std::ostream& dump(const delta<Typ>& delta, std::ostream& str, unsigned int level, node_id nid0) {
			return delta_traits<void, Typ>::dump(delta, str, level, nid0);
		}

	using delta_add_edge_=delta<delta_type::add_edge_>;
	using delta_add_prop_=delta<delta_type::add_prop_>;
	using delta_chg_prop_=delta<delta_type::chg_prop_>;
	using delta_add_patch_=delta<delta_type::add_patch_>;
	using delta_del_patch_=delta<delta_type::del_patch_>;
	using delta_proofread_=delta<delta_type::proofread_>;
	using delta_reset_proofread_0_=delta<delta_type::reset_proofread_0_>;
	using delta_reset_proofread_=delta<delta_type::reset_proofread_>;

	class delta_variant_PRIV {
		friend class delta_variant;
		template<typename... Ts> struct Var;
		template<typename T, typename V>
			static constexpr decltype(auto) get_var(V&& v) noexcept {
				if constexpr(v.Idx==0) {
				} else if constexpr(std::is_same_v<T, decltype(v.head)>) {
					return std::forward<V>(v);
				} else {
					return get_var<T>(std::forward<V>(v).tail);
				}
			}
		template<unsigned int I, typename V>
			static constexpr decltype(auto) get_var(V&& v) noexcept {
				if constexpr(v.Idx==0) {
				} else if constexpr(I==v.Idx) {
					return std::forward<V>(v);
				} else {
					return get_var<I>(std::forward<V>(v).tail);
				}
			}
	};
	template<> struct delta_variant_PRIV::Var<> {
		constexpr static unsigned int Idx=0;
	};
	template<typename T, typename... Ts>
		struct delta_variant_PRIV::Var<T, Ts...> {
			using Tail=Var<Ts...>;
			constexpr static unsigned int Idx=Tail::Idx+1;
			union { T head; Tail tail; };
			//static_assert(std::is_nothrow_default_constructible_v<T>);
//#ifndef __APPLE__
#ifndef __clang__
			static_assert(noexcept(T{}));
#endif
//#endif
			static_assert(std::is_nothrow_move_assignable_v<T>);
			static_assert(std::is_nothrow_move_constructible_v<T>);
			static_assert(std::is_nothrow_swappable_v<T>);
			//static_assert(std::is_nothrow_constructible_v<T_j, T>);
			//static_assert(std::is_nothrow_assignable_v<T_j&, T>);
			//static_assert(std::is_nothrow_constructible_v<T_j, T>);
		};
	class delta_variant: private delta_variant_PRIV {
		public:
			constexpr delta_variant() noexcept;
			~delta_variant();
			delta_variant(const delta_variant&) =delete;
			delta_variant& operator=(const delta_variant&) =delete;
			constexpr delta_variant(delta_variant&& r) noexcept;
			constexpr delta_variant& operator=(delta_variant&& r) noexcept;
			template<typename T>
				constexpr delta_variant(T&& t) noexcept;
			template<typename T>
				delta_variant& operator=(T&& t) noexcept;

			constexpr gapr::delta_type type() const noexcept;
			constexpr operator bool() const noexcept;
			template<typename T>
				constexpr bool holds() const noexcept;

			template<typename T, typename... Args>
				T& emplace(Args&&... args);
			void swap(delta_variant& r) noexcept;

			template<typename T>
				constexpr T* get_if() noexcept;
			template<typename T>
				constexpr const T* get_if() const noexcept;
			template<typename T>
				constexpr T& get() &;
			template<typename T>
				constexpr const T& get() const&;
			template<typename T>
				constexpr T&& get() &&;
			template<typename T>
				constexpr const T&& get() const&&;

			template<typename R, typename F>
				constexpr R visit(F&& f) {
					if(_t==delta_type::invalid)
						throw;
					return R{};
				}

			template<typename R, typename F>
				static constexpr R visit(delta_type t, F&& f) {
					return do_select<R>(t, std::forward<F>(f));
				}

		private:
			Var<delta_add_edge_,
				delta_add_prop_,
				delta_chg_prop_,
				delta_add_patch_,
				delta_del_patch_,
				delta_proofread_,
				delta_reset_proofread_0_,
				delta_reset_proofread_> _v;
			delta_type _t;
			template<typename R, typename F>
				constexpr static R do_select(delta_type t, F&& f) {
					switch(t) {
						default:
							throw std::logic_error{"invalid delta type2"};
						case delta_type::invalid:
							throw std::logic_error{"invalid delta type"};
						case delta_type::add_edge_:
							return f(delta_traits<void, delta_type::add_edge_>::type);
						case delta_type::add_prop_:
							return f(delta_traits<void, delta_type::add_prop_>::type);
						case delta_type::chg_prop_:
							return f(delta_traits<void, delta_type::chg_prop_>::type);
						case delta_type::add_patch_:
							return f(delta_traits<void, delta_type::add_patch_>::type);
						case delta_type::del_patch_:
							return f(delta_traits<void, delta_type::del_patch_>::type);
						case delta_type::proofread_:
							return f(delta_traits<void, delta_type::proofread_>::type);
						case delta_type::reset_proofread_0_:
							return f(delta_traits<void, delta_type::reset_proofread_0_>::type);
						case delta_type::reset_proofread_:
							return f(delta_traits<void, delta_type::reset_proofread_>::type);
					}
				}
	};

	enum class tier: unsigned int {
		root=0,
		admin,
		annotator,
		proofreader,
		restricted,
		locked=10,
		nobody=99
	};
	GAPR_CORE_DECL std::string_view to_string(tier v) noexcept;

	enum class stage: unsigned int {
		initial=0,
		open,
		guarded,
		closed,
		frozen,
	};
	GAPR_CORE_DECL std::string_view to_string(stage v) noexcept;

}

#if 0

add_node {
	//nid 1
	//nattr
	//sattr
};

#endif

//#include "model-obsol.hh"



#if 0

Command* Command::changeType(SessionPriv* tp, Edge e, int16_t t) {
	return new CommandChangeType{tp, e, t};
}

Command* Command::changeMark(SessionPriv* tp, Edge e, size_t ep, int16_t m) {
	return new CommandChangeMark{tp, e, ep, m};
}

Command* Command::changeSize(SessionPriv* tp, Edge e, size_t ep, uint16_t m) {
	return new CommandChangeSize{tp, e, ep, m};
}

Command* Command::removeNeurons(SessionPriv* tp, const std::vector<size_t>& idxes) {
	return new CommandRemoveNeurons{tp, idxes};
}


Command* Command::removeEdge(SessionPriv* tp, Edge e0, size_t ei0, Edge e1, size_t ei1) {
	return new CommandRemoveEdge{tp, e0, ei0, e1, ei1};
}


Command* Command::importSwc(SessionPriv* tp, const std::vector<Point>& points, const std::vector<int64_t>& parents) {
	return new CommandImportSwc{tp, points, parents};
}
#endif


#endif
