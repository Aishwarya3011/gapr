/* gapr/model.hh
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

//@@@@@
#ifndef _GAPR_INCLUDE_MODEL_HH_
#define _GAPR_INCLUDE_MODEL_HH_


#include "gapr/detail/fixed-point.hh"
#include "gapr/detail/mini-float.hh"
#include "gapr/vec3.hh"

#include <cstdint>
#include <array>
#include <ostream>


namespace gapr {

	struct node_id {
		using data_type=uint32_t;
		data_type data;

		constexpr node_id() noexcept: data{0} { }
		constexpr explicit node_id(data_type r) noexcept: data{r} { }

		constexpr explicit operator bool() const noexcept { return data; }
		constexpr node_id offset(std::ptrdiff_t off) const noexcept {
			return node_id{static_cast<data_type>(data+off)};
		}

		constexpr static node_id max() noexcept {
			return node_id{std::numeric_limits<data_type>::max()};
		}
	};

	struct link_id {
		using data_type=std::array<node_id::data_type, 2>;
		std::array<node_id, 2> nodes;

		constexpr link_id() noexcept: nodes{} { }
		constexpr link_id(node_id n0, node_id n1) noexcept: nodes{n0, n1} { }
		constexpr explicit link_id(data_type r) noexcept:
			nodes{node_id{r[0]}, node_id{r[1]}} { }
		constexpr data_type data() const noexcept {
			return {nodes[0].data, nodes[1].data};
		}

		constexpr link_id cannolize() const noexcept {
			if(nodes[0].data<nodes[1].data)
				return {nodes[1], nodes[0]};
			if(nodes[0].data==nodes[1].data)
				return {nodes[0], {}};
			return {nodes[0], nodes[1]};
		}
		constexpr explicit operator bool() const noexcept { return !!nodes[0]; }
		constexpr bool on_node() const noexcept { return !nodes[1]; }
	};

	struct prop_id {
		using data_type=std::pair<node_id::data_type, std::string>;
		node_id node;
		std::string key;

		prop_id() noexcept: node{}, key{} { }
		prop_id(node_id node, std::string&& key) noexcept:
			node{node}, key{std::move(key)} { }
		explicit prop_id(data_type&& r) noexcept:
			node{r.first}, key{std::move(r.second)} { }
		data_type data() && noexcept { return {node.data, std::move(key)}; }
	};

	struct anchor_id {
		using data_type=std::pair<link_id::data_type, uint8_t>;
		link_id link;
		uint8_t ratio;

		constexpr anchor_id() noexcept: link{}, ratio{0} { }
		constexpr anchor_id(node_id id) noexcept:
			link{id, {}}, ratio{0} { }
		constexpr anchor_id(node_id id0, node_id id1, unsigned int r) noexcept:
			link{id0, id1}, ratio{static_cast<uint8_t>(r>128?128:r)} { }
		constexpr explicit anchor_id(data_type r) noexcept:
			link{r.first}, ratio{r.second} { }
		constexpr data_type data() const noexcept {
			return {link.data(), ratio};
		}

		constexpr anchor_id cannolize() const noexcept {
			auto [n0, n1]=link.cannolize().nodes;
			if(!n1)
				return {n0, {}, 0};
			if(ratio<=0)
				return {link.nodes[0], {}, 0};
			if(ratio>=128)
				return {link.nodes[1], {}, 0};
			return {n0, n1, n0.data==link.nodes[0].data?ratio:128u-ratio};
		}
	};

	struct misc_attr {
		using data_type=uint32_t;
		data_type data;

		constexpr misc_attr() noexcept: data{0} { }
		constexpr explicit misc_attr(data_type r) noexcept: data{r} { }

		constexpr int t() const noexcept { return get_t(data); }
		constexpr void t(int t) noexcept { set_t(data, t); }

		constexpr double r() const noexcept { return get_r(data); }
		constexpr void r(double r) noexcept { set_r(data, r); }

		constexpr bool coverage() const noexcept { return get_cnt(data); }
		constexpr void coverage(bool v) noexcept { set_cnt(data, v); }

		constexpr bool extended() const noexcept { return data&_ext_mask; }

		constexpr misc_attr cannolize() const noexcept {
			return misc_attr{data&_node_mask};
		}

		private:
		//From LSB to MSB:
		//  radius
		//  type
		//  proofread mark
		constexpr static int NB_T=5;
		constexpr static int NB_R=11;
		constexpr static int NB_R_FRAC=6;
		constexpr static int NB_R_BIAS=8;
		constexpr static int NB_CNT=1;
		//[NB_BOT, NB_TOP) never used
		constexpr static int NB_BOT=NB_T+NB_R+NB_CNT;
		constexpr static int NB_TOP=sizeof(data_type)*8;
		static_assert(NB_BOT<=NB_TOP);

		constexpr static data_type _t_mask=(~((~data_type{0})<<NB_T));
		constexpr static data_type _cnt_mask=((~((~data_type{0})<<NB_CNT))<<(NB_T+NB_R));
		constexpr static data_type _node_mask=(~((~data_type{0})<<NB_BOT));
		constexpr static data_type _ext_mask=((~data_type{0})<<(NB_T+NB_R));

		using mfr=mini_float_nn<data_type, NB_T, (NB_T+NB_R)-1, NB_R_FRAC, NB_R_BIAS>;
		constexpr static double get_r(data_type v) noexcept {
			return mfr::get(v);
		}
		constexpr static data_type enc_r(double v) noexcept {
			return mfr::enc(v);
		}
		constexpr static void set_r(data_type& m, double v) noexcept {
			mfr::set(m, v);
		}
		constexpr static int get_t(data_type v) noexcept {
			return static_cast<int>(v&_t_mask);
		}
		constexpr static data_type enc_t(int vv) noexcept {
			auto v=static_cast<unsigned int>(vv);
			return static_cast<data_type>((v<(1<<NB_T))?v:((1<<NB_T)-1));
		}
		constexpr static void set_t(data_type& m, int v) noexcept {
			m=(m&(~_t_mask))|enc_t(v);
		}
		constexpr static bool get_cnt(data_type v) noexcept {
			return static_cast<bool>((v&_cnt_mask)>>(NB_T+NB_R));
		}
		constexpr static data_type enc_cnt(bool v_) noexcept {
			auto v=static_cast<unsigned int>(v_?1:0);
			return static_cast<data_type>(v<<(NB_T+NB_R));
		}
		constexpr static void set_cnt(data_type& m, bool v) noexcept {
			m=(m&(~_cnt_mask))|enc_cnt(v);
		}
		//constexpr static value_type _ext_mask=(~value_type{0})<<15;
		friend struct node_attr;
	};

	struct node_attr {
		using ipos_type=std::array<int32_t, 3>;
		using data_type=std::pair<ipos_type, misc_attr::data_type>;
		ipos_type ipos;
		misc_attr misc;

		constexpr node_attr() noexcept: ipos{0, 0, 0}, misc{} { }
		constexpr node_attr(double x, double y, double z) noexcept:
			ipos{enc_pos(x), enc_pos(y), enc_pos(z)}, misc{} { }
		constexpr node_attr(double x, double y, double z, double r, int t) noexcept:
			ipos{enc_pos(x), enc_pos(y), enc_pos(z)},
			misc{misc_attr::enc_r(r)|misc_attr::enc_t(t)} { }
		constexpr node_attr(ipos_type ipos, misc_attr misc) noexcept:
			ipos{ipos}, misc{misc} { }
		constexpr explicit node_attr(data_type r) noexcept:
			ipos{r.first},
			misc{r.second} { }
		constexpr data_type data() const noexcept { return {ipos, misc.data}; }

		constexpr double pos(unsigned int i) const noexcept {
			return get_pos(ipos[i]);
		}
		constexpr void pos(unsigned int i, double v) noexcept {
			ipos[i]=enc_pos(v);
		}
		constexpr gapr::vec3<double> pos() const noexcept {
			gapr::vec3<double> r;
			for(unsigned int i=0; i<3; ++i)
				r[i]=get_pos(ipos[i]);
			return r;
		}
		constexpr void pos(const gapr::vec3<double>& p) noexcept {
			for(unsigned int i=0; i<3; ++i)
				ipos[i]=enc_pos(p[i]);
		}

		constexpr bool at_origin() const noexcept {
			return ipos[0]==0 && ipos[1]==0 && ipos[2]==0;
		}
		double dist_to(node_attr p) const {
			// XXX optimize (less fp op)
			auto dx=pos(0)-p.pos(0);
			auto dy=pos(1)-p.pos(1);
			auto dz=pos(2)-p.pos(2);
			return std::sqrt(dx*dx+dy*dy+dz*dz);
		}
		double dist_to(const std::array<double, 3>& p) const {
			auto dx=pos(0)-p[0];
			auto dy=pos(1)-p[1];
			auto dz=pos(2)-p[2];
			return std::sqrt(dx*dx+dy*dy+dz*dz);
		}

		private:
		using host_type=ipos_type::value_type;
		using fppos=fixed_point<host_type, 0, sizeof(host_type)*8-1, 10>;
		constexpr static host_type enc_pos(double v) noexcept {
			return fppos::enc(v);
		}
		constexpr static double get_pos(host_type v) noexcept {
			return fppos::get(v);
		}
	};


	inline constexpr bool operator==(node_id a, node_id b) noexcept {
		return a.data==b.data;
	}
	inline constexpr bool operator!=(node_id a, node_id b) noexcept {
		return !(a==b);
	}
	inline constexpr bool operator>(node_id a, node_id b) noexcept {
		return a.data>b.data;
	}
	inline constexpr bool operator<(node_id a, node_id b) noexcept {
		return b>a;
	}
	inline constexpr node_id operator+(node_id id, int inc) noexcept {
		return node_id{id.data+inc};
	}
	inline constexpr node_id operator-(node_id id, int dec) noexcept {
		return id+(-dec);
	}
	inline constexpr node_id operator++(node_id& id, int) noexcept {
		return node_id{id.data++};
	}
	inline constexpr bool operator==(link_id a, link_id b) noexcept {
		return a.nodes[0]==b.nodes[0] && a.nodes[1]==b.nodes[1];
	}
	inline constexpr bool operator!=(link_id a, link_id b) noexcept {
		return !(a==b);
	}
	inline constexpr bool operator==(anchor_id a, anchor_id b) noexcept {
		return a.link==b.link && a.ratio==b.ratio;
	}
	inline constexpr bool operator!=(anchor_id a, anchor_id b) noexcept {
		return !(a==b);
	}
	inline constexpr bool operator==(const prop_id& a, const prop_id& b) noexcept {
		return a.node==b.node && a.key==b.key;
	}
	inline constexpr bool operator!=(const prop_id& a, const prop_id& b) noexcept {
		return !(a==b);
	}
	inline constexpr bool operator==(misc_attr a, misc_attr b) noexcept {
		return a.data==b.data;
	}
	inline constexpr bool operator!=(misc_attr a, misc_attr b) noexcept {
		return !(a==b);
	}
	inline constexpr misc_attr& operator|=(misc_attr& a, misc_attr b) noexcept {
		a.data|=b.data;
		return a;
	}
	inline constexpr misc_attr operator-(misc_attr a, misc_attr b) noexcept {
		return misc_attr{a.data^b.data};
	}
	inline constexpr misc_attr operator+(misc_attr a, misc_attr b) noexcept {
		return misc_attr{a.data^b.data};
	}
	inline constexpr bool operator==(node_attr a, node_attr b) noexcept {
		return a.ipos[0]==b.ipos[0] && a.ipos[1]==b.ipos[1] && a.ipos[2]==b.ipos[2] && a.misc==b.misc;
	}
	inline constexpr bool operator!=(node_attr a, node_attr b) noexcept {
		return !(a==b);
	}
	inline constexpr node_attr operator-(node_attr a, node_attr b) noexcept {
		return {node_attr::ipos_type{a.ipos[0]-b.ipos[0], a.ipos[1]-b.ipos[1], a.ipos[2]-b.ipos[2]}, a.misc-b.misc};
	}
	inline constexpr node_attr operator+(node_attr a, node_attr b) noexcept {
		return {node_attr::ipos_type{a.ipos[0]+b.ipos[0], a.ipos[1]+b.ipos[1], a.ipos[2]+b.ipos[2]}, a.misc+b.misc};
	}

	inline std::ostream& operator<<(std::ostream& str, gapr::node_id id) {
		return str<<'@'<<id.data;
	}
	inline std::ostream& operator<<(std::ostream& str, gapr::link_id link) {
		if(!link)
			return str<<'@'<<link.nodes[1].data;
		str<<'@'<<link.nodes[0].data;
		if(!link.on_node())
			str<<'-'<<link.nodes[1].data;
		return str;
	}
	inline std::ostream& operator<<(std::ostream& str, gapr::node_attr node) {
		str<<'('<<node.pos(0)<<','<<node.pos(1)<<','<<node.pos(2);
		return str<<';'<<node.misc.t()<<','<<node.misc.r()<<')';
	}

}

template<>
struct std::hash<gapr::node_id> {
	std::size_t operator()(gapr::node_id id) const noexcept {
		return std::hash<gapr::node_id::data_type>{}(id.data);
	}
};
template<>
struct std::hash<gapr::link_id> {
	std::size_t operator()(gapr::link_id id) const noexcept {
		static_assert(4==sizeof(id.nodes[0]));
		uint64_t x=(uint64_t{id.nodes[0].data}<<32)|id.nodes[1].data;
		return std::hash<uint64_t>{}(x);
	}
};
template<>
struct std::hash<gapr::prop_id> {
	std::size_t operator()(const gapr::prop_id& id) const noexcept {
		auto h1=std::hash<gapr::node_id>{}(id.node);
		auto h2=std::hash<std::string>{}(id.key);
		return h1^h2;
	}
};

#endif
