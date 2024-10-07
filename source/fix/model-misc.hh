#ifndef _FIX_MODEL_MISC_HH_
#define _FIX_MODEL_MISC_HH_

#include "gapr/edge-model.hh"

namespace gapr::fix {

	struct Position: gapr::edge_model::position {
		gapr::edge_model::point point;

		constexpr Position() noexcept: position{}, point{{0, 0, 0}, 0} { }
		constexpr explicit Position(edge_model::point pt) noexcept:
			position{}, point{pt} { point.second=1; }
		constexpr Position(edge_model::vertex_id vert, edge_model::point pt) noexcept:
			position{vert}, point{pt} { point.second=1; }
		constexpr Position(edge_model::edge_id edge, uint32_t index, edge_model::point pt) noexcept:
			position{edge, index}, point{pt} { point.second=1; }

		bool valid() const noexcept { return gapr::node_attr{point}.misc.data!=0; }
	};
	inline bool operator==(const Position& a, const Position& b) noexcept {
		return a.edge==b.edge && a.vertex==b.vertex && a.point==b.point;
	}

}

std::string get_graph_details(gapr::edge_model::view model);

#endif
