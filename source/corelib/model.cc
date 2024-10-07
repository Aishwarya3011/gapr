/* lib/core/model.cc
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

#include "gapr/commit.hh"

//#include "gapr/parser.hh"
#include "gapr/serializer.hh"
#include "gapr/utility.hh"
#include "gapr/mt-safety.hh"

#include <cassert>
#include <chrono>
#include <cmath>

#include "config.hh"

#include "dup/save-load-helper.hh"

/*!
 * use signed in serialization predictions
 */

template<typename T>
static T failure(T res, const char* msg) {
	if(0)
		throw std::runtime_error{msg};
	gapr::print(msg);
	return res;
}

namespace {
	struct epoch_helper {
		std::chrono::system_clock::time_point value;
		epoch_helper();
	};
	std::chrono::system_clock::time_point epoch_() {
		const static epoch_helper e;
		return e.value;
	}
}

epoch_helper::epoch_helper() {
	std::tm tm0;
	tm0.tm_year=70;
	tm0.tm_mon=0;
	tm0.tm_mday=1;
	/* fix out-of-range error */
	tm0.tm_hour=32;
	tm0.tm_min=0;
	tm0.tm_sec=0;
	tm0.tm_isdst=0;
	std::tm tmbuf;

	auto lt=std::mktime(&tm0);
	auto lt2=std::mktime(gapr::gmtime_mt(&lt, &tmbuf));
	auto dif=std::difftime(lt2, lt);
	auto t0=std::chrono::system_clock::from_time_t(lt)-std::chrono::milliseconds{std::lround(dif*1000)+32*3600000};
	//gapr::print(std::chrono::duration_cast<std::milisecond>(t0-std::chrono::system_clock::time_point{}).count());
	assert(t0==std::chrono::system_clock::time_point{});
	value=t0;
}
uint64_t gapr::to_timestamp(const std::chrono::system_clock::time_point& t) {
	auto t0=epoch_();
	return std::chrono::duration_cast<std::chrono::duration<uint64_t, std::milli>>(t-t0).count();
}
std::chrono::system_clock::time_point gapr::from_timestamp(uint64_t ts) {
	std::chrono::duration<uint64_t, std::milli> dur{ts};
	auto t0=epoch_();
	auto t1=(t0+dur);
	return t1;
}

static bool cannolize_node(gapr::node_attr& node, bool as_node) noexcept {
	bool chg{false};
	auto m=node.misc;
	if(as_node) {
		m=m.cannolize();
	} else {
		for(auto& p: node.ipos) {
			if(p!=0) {
				p=0;
				chg=true;
			}
		}
		m=gapr::misc_attr{};
	}
	if(m.data!=node.misc.data) {
		node.misc=m;
		chg=true;
	}
	return chg;
}

static void fix_path(std::vector<gapr::node_attr::data_type>& nodes) noexcept {
	for(std::size_t i=0; i+2<nodes.size(); i++) {
		auto r=i%4;
		auto c="by " PACKAGE_NAME "."[(i/4)%8];
		bool b[]={r!=0, (c&(2<<(6-2*r)))!=0, (c&(1<<(6-2*r)))!=0};
		auto& n=nodes[i+1];
		for(unsigned int j=0; j<3; j++)
			n.first[j]=((n.first[j]>>1)<<1)|(b[j]?1:0);
	}
}

static bool is_key_val_valid(const std::string& str) {
	if(str.empty())
		return false;
	if(str[0]=='=')
		return false;
	//XXX check if key is valid name
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

#include "gapr/detail/delta-stats.hh"

template<typename E0> struct gapr::SerializeAsVector<std::deque<E0>> {
	template<typename T> static std::size_t count(T& obj) { return obj.size(); }
	template<typename T> static auto begin(T& obj) { return obj.begin(); }
	template<typename T> static void resize(T& obj, std::size_t s) { return obj.resize(s); }
};

namespace gapr {

	namespace {
		struct hist_helper {
			commit_id::data_type& base;
			std::deque<commit_id::data_type>& sparse;
		};
	}
	template<> struct SerializerAdaptor<hist_helper, 0> {
		template<typename T> static auto& map(T& obj) { return obj.base; }
	};
	template<> struct SerializerAdaptor<hist_helper, 1> {
		template<typename T> static auto& map(T& obj) { return obj.sparse; }
	};

	bool commit_history::load(std::streambuf& str) {
		hist_helper h{_base, _sparse};
		return load_impl<hist_helper>(h, str);
	}
	bool commit_history::save(std::streambuf& str) const {
		auto p=const_cast<commit_history*>(this);
		hist_helper h{p->_base, p->_sparse};
		return save_impl<hist_helper>(h, str);
	}

	template<> struct SerializerAdaptor<gapr::commit_info, 0> {
		template<typename T> static auto& map(T& obj) { return obj.id; }
	};
	template<> struct SerializerAdaptor<gapr::commit_info, 1> {
		template<typename T> static auto& map(T& obj) { return obj.who; }
	};
	template<> struct SerializerAdaptor<gapr::commit_info, 2> {
		template<typename T> static auto& map(T& obj) { return obj.when; }
	};
	template<> struct SerializerAdaptor<gapr::commit_info, 3> {
		template<typename T> static auto& map(T& obj) { return obj.nid0; }
	};
	template<> struct SerializerAdaptor<gapr::commit_info, 4> {
		template<typename T> static auto& map(T& obj) { return obj.type; }
	};
	bool commit_info::load(std::streambuf& str) {
		return load_impl<commit_info>(*this, str);
	}
	bool commit_info::save(std::streambuf& str) const {
		return save_impl<commit_info>(*this, str);
	}
	char* timestamp_to_chars(char* first, char* last, uint64_t ts, bool full) {
		auto whentp=gapr::from_timestamp(ts);
		auto when=std::chrono::system_clock::to_time_t(whentp);
		std::tm tmbuf;
		auto n=std::strftime(first, last-first, full?"%Y-%m-%dT%H:%M:%S.sss%z":"%H:%M:%S %m/%d/%y", gapr::localtime_mt(&when, &tmbuf));
		if(n>0) {
			if(full) {
				unsigned int ms=ts%1000;
				first[n-8]=ms/100+'0';
				first[n-7]=(ms/10)%10+'0';
				first[n-6]=(ms)%10+'0';
			}
			return first+n;
		}
		return nullptr;
	}
	std::ostream& operator<<(std::ostream& str, const commit_info& info) {
		std::array<char, 128> time_str;
		str<<"id: "<<info.id<<"; author: "<<info.who<<"; time: ";
		auto p=timestamp_to_chars(time_str.begin(), time_str.end(), info.when, true);
		if(p) {
			str.write(&time_str[0], p-&time_str[0]);
		} else {
			str<<"ERR";
		}
		str<<"; type: "<<to_string(gapr::delta_type{info.type});
		return str;
	}

	template<> struct SerializerAdaptor<gapr::model_state, 0> {
		template<typename T> static auto& map(T& obj) { return obj.size; }
	};
	template<> struct SerializerAdaptor<gapr::model_state, 1> {
		template<typename T> static auto& map(T& obj) { return obj.edges; }
	};
	template<> struct SerializerAdaptor<gapr::model_state, 2> {
		template<typename T> static auto& map(T& obj) { return obj.logs; }
	};
	template<> struct SerializerAdaptor<gapr::model_state, 3> {
		template<typename T> static auto& map(T& obj) { return obj.props; }
	};
	template<> struct SerializerAdaptor<gapr::model_state::edge, 0> {
		template<typename T> static auto& map(T& obj) { return obj.nodes; }
	};
	template<> struct SerializerAdaptor<gapr::model_state::edge, 1> {
		template<typename T> static auto& map(T& obj) { return obj.points; }
	};
	template<> struct SerializerAdaptor<gapr::node_id, 0> {
		template<typename T> static auto& map(T& obj) { return obj.data; }
	};
	template<> struct SerializerUsePredictor<gapr::model_state::edge, 0> {
		constexpr static bool value=true;
	};
	template<> struct SerializerPredictor<gapr::model_state::edge, 0> {
		template<typename T> static auto sub(T obj, T obj0) {
			using Td=std::make_signed_t<T>;
			return static_cast<Td>(obj-obj0);
		}
		template<typename T, typename Td> static T add(Td d, T obj0) {
			return d+obj0;
		}
	};
	template<> struct SerializerUsePredictor<gapr::model_state::edge, 1> {
		constexpr static bool value=true;
	};
	template<> struct SerializerPredictor<gapr::model_state::edge, 1> {
		template<typename T> static auto sub(T obj, T obj0) {
			return gapr::node_attr{obj}-gapr::node_attr{obj0};
		}
		template<typename T, typename Td> static T add(Td d, T obj0) {
			return gapr::node_attr{d}+gapr::node_attr{obj0};
		}
	};
	bool model_state::load(std::streambuf& str) {
		return load_impl<model_state>(*this, str);
	}
	bool model_state::save(std::streambuf& str) const {
		return save_impl<model_state>(*this, str);
	}
	int model_state::cannolize() noexcept {
		return 0;
	}

	template<> struct SerializerAdaptor<gapr::delta_add_edge_, 0> {
		template<typename T> static auto& map(T& obj) { return obj.left; }
	};
	template<> struct SerializerAdaptor<gapr::delta_add_edge_, 1> {
		template<typename T> static auto& map(T& obj) { return obj.right; }
	};
	template<> struct SerializerAdaptor<gapr::delta_add_edge_, 2> {
		template<typename T> static auto& map(T& obj) { return obj.nodes; }
	};
	template<> struct SerializerUsePredictor<gapr::delta_add_edge_, 2> {
		constexpr static bool value=true;
	};
	template<> struct SerializerPredictor<gapr::delta_add_edge_, 2> {
		template<typename T> static auto sub(T obj, T obj0) {
			return gapr::node_attr{obj}-gapr::node_attr{obj0};
		}
		template<typename T, typename Td> static T add(Td d, T obj0) {
			return gapr::node_attr{d}+gapr::node_attr{obj0};
		}
	};
	int cannolize_impl(delta_add_edge_& delta) noexcept {
		return [](auto& left, auto& right, auto& nodes) ->int {
		if(nodes.size()<2)
			return -1;
		int chg{0};
		auto left1=gapr::link_id{left}.cannolize();
		if(left1.data()!=left) {
			left=left1.data();
			chg=1;
		}
		auto right1=gapr::link_id{right}.cannolize();
		if(right1.data()!=right) {
			right=right1.data();
			chg=1;
		}
		gapr::node_attr node0{nodes.front()};
		if(cannolize_node(node0, !left1 || !left1.on_node())) {
			nodes.front()=node0.data();
			chg=1;
		}
		gapr::node_attr node1{nodes.back()};
		if(cannolize_node(node1, !right1 || !right1.on_node())) {
			nodes.back()=node1.data();
			chg=1;
		}
		for(std::size_t i=1; i+1<nodes.size(); i++) {
			auto& node_=nodes[i];
			gapr::node_attr node{node_};
			if(cannolize_node(node, true)) {
				node_=node.data();
				chg=1;
			}
		}
		fix_path(nodes);
		return chg;
		}(delta.left, delta.right, delta.nodes);
	}
	std::ostream& dump_impl(const delta<delta_type::add_edge_>& delta, std::ostream& str, unsigned int level, gapr::node_id nid0) {
		return [&delta,&str,level,nid0](auto& left, auto& right, auto& nodes) ->std::ostream& {
		DeltaStats stats;
		stats.add(delta);
		str<<stats;
		if(level<1)
			return str<<'\n';
		str<<"\nleft: ";
		gapr::link_id ll{left};
		auto k=nid0.data;
		if(ll)
			str<<ll;
		else
			str<<"+@"<<k++;
		str<<' '<<gapr::node_attr{nodes[0]};
		str<<"\nright: ";
		gapr::link_id lr{right};
		if(lr)
			str<<lr;
		else
			str<<"+@"<<k+nodes.size()-2;
		str<<' '<<gapr::node_attr{nodes.back()};
		str<<"\nnodes:";
		for(unsigned int i=1; i+1<nodes.size(); i++) {
			auto& node=nodes[i];
			str<<"\n  - +@";
			str<<k+i-1<<' ';
			str<<gapr::node_attr{node};
		}
		return str<<'\n';
		}(delta.left, delta.right, delta.nodes);
	}

	template<> struct SerializerAdaptor<gapr::delta_add_prop_, 0> {
		template<typename T> static auto& map(T& obj) { return obj.link; }
	};
	template<> struct SerializerAdaptor<gapr::delta_add_prop_, 1> {
		template<typename T> static auto& map(T& obj) { return obj.node; }
	};
	template<> struct SerializerAdaptor<gapr::delta_add_prop_, 2> {
		template<typename T> static auto& map(T& obj) { return obj.prop; }
	};
	int cannolize_impl(delta_add_prop_& delta) noexcept {
		return [](auto& link, auto& node, auto& prop) ->int {
		int chg{0};
		auto link1=gapr::link_id{link}.cannolize();
		if(link1.data()!=link) {
			link=link1.data();
			chg=1;
		}
		gapr::node_attr node0{node};
		if(cannolize_node(node0, !link1 || !link1.on_node())) {
			node=node0.data();
			chg=1;
		}
		if(!is_key_val_valid(prop))
			return -1;
		return chg;
		}(delta.link, delta.node, delta.prop);
	}
	std::ostream& dump_impl(const delta<delta_type::add_prop_>& delta, std::ostream& str, unsigned int level, gapr::node_id nid0) {
		return [&delta,&str,level,nid0](auto& link, auto& node, auto& prop) ->std::ostream& {
		DeltaStats stats;
		stats.add(delta);
		str<<stats;
		if(level<1)
			return str<<'\n';
		gapr::link_id l{link};
		str<<"\nlink: ";
		if(l)
			str<<l;
		else
			str<<"+@"<<nid0.data;
		str<<"\nnode: "<<gapr::node_attr{node};
		str<<"\nprop: "<<prop<<'\n';
		return str;
		}(delta.link, delta.node, delta.prop);
	}

	template<> struct SerializerAdaptor<gapr::delta_chg_prop_, 0> {
		template<typename T> static auto& map(T& obj) { return obj.node; }
	};
	template<> struct SerializerAdaptor<gapr::delta_chg_prop_, 1> {
		template<typename T> static auto& map(T& obj) { return obj.prop; }
	};
	int cannolize_impl(delta_chg_prop_& delta) noexcept {
		return [](auto& node, auto& prop) ->int {
		if(!gapr::node_id{node})
			return -1;
		if(!is_key_val_valid(prop))
			return -1;
		return 0;
		}(delta.node, delta.prop);
	}
	std::ostream& dump_impl(const delta<delta_type::chg_prop_>& delta, std::ostream& str, unsigned int level, gapr::node_id nid0) {
		return [&delta,&str,level](auto& node, auto& prop) ->std::ostream& {
		DeltaStats stats;
		stats.add(delta);
		str<<stats;
		if(level<1)
			return str<<'\n';
		str<<"\nnode: "<<node;
		str<<"\nprop: "<<prop<<'\n';
		return str;
		}(delta.node, delta.prop);
	}

	template<> struct SerializerAdaptor<gapr::delta_add_patch_, 0> {
		template<typename T> static auto& map(T& obj) { return obj.links; }
	};
	template<> struct SerializerAdaptor<gapr::delta_add_patch_, 1> {
		template<typename T> static auto& map(T& obj) { return obj.props; }
	};
	template<> struct SerializerAdaptor<gapr::delta_add_patch_, 2> {
		template<typename T> static auto& map(T& obj) { return obj.nodes; }
	};
	template<> struct SerializerUsePredictor<gapr::delta_add_patch_, 2> {
		constexpr static bool value=true;
	};
	template<> struct SerializerPredictor<gapr::delta_add_patch_, 2> {
		template<typename T> static auto sub(T obj, T obj0) {
			auto first=gapr::node_attr{obj.first}-gapr::node_attr{obj0.first};
			return std::pair{first, obj.second};
		}
		template<typename T, typename Td> static T add(Td d, T obj0) {
			auto first=gapr::node_attr{d.first}+gapr::node_attr{obj0.first};
			return {first, d.second};
		}
	};
	int cannolize_impl(delta_add_patch_& delta) noexcept {
		return [](auto& nodes, auto& links, auto& props) ->int {
		auto N=nodes.size();
		if(N<1 && props.size()<1)
			return failure(-1, "empty patch");
		int chg{0};
		//assume links and props sorted
		std::size_t j=0;
		for(std::size_t i=0; i<nodes.size(); i++) {
			auto par=nodes[i].second;
			if(par>i)
				return failure(-1, "undefined parent");
			bool has_node{true};
			if(j<links.size()) {
				if(links[j].first<i+1)
					return failure(-1, "unordered links");
				if(links[j].first==i+1) {
					auto link=gapr::link_id{links[j].second}.cannolize();
					if(!link)
						return failure(-1, "invalid link");
					if(link.data()!=links[j].second) {
						links[j].second=link.data();
						chg=1;
					}
					if(link.on_node())
						has_node=false;
					j++;
				}
			}
			gapr::node_attr node{nodes[i].first};
			if(cannolize_node(node, has_node)) {
				nodes[i].first=node.data();
				chg=1;
			}
		}
		if(j<links.size())
			return failure(-1, "unused links");

		std::size_t i;
		for(i=0; i<props.size(); ++i) {
			auto id=props[i].first;
			if(i>0) {
				auto& prev_prop=props[i-1];
				if(id<prev_prop.first)
					return failure(-1, "unordered props");
				if(id==prev_prop.first) {
					if(compare_tag(props[i].second, prev_prop.second)<=0)
						return failure(-1, "unordered props");
				}
			} else if(id==0)
				return failure(-1, "invalid id");
			if(id==gapr::node_id::max().data)
				break;
			if(id>N)
				return failure(-1, "invalid id");
			if(!is_key_val_valid(props[i].second))
				return failure(-1, "invalid key");
		}
		for(; i<props.size(); ++i) {
			auto id=props[i].first;
			if(id!=gapr::node_id::max().data)
				return failure(-1, "invalid placeholder id");
			if(!is_key_val_valid(props[i].second))
				return failure(-1, "invalid log");
		}
		return chg;
		}(delta.nodes, delta.links, delta.props);
	}
	std::ostream& dump_impl(const delta<delta_type::add_patch_>& delta, std::ostream& str, unsigned int level, gapr::node_id nid0) {
		return [&delta,&str,level,nid0](auto& links, auto& props, auto& nodes) ->std::ostream& {
		DeltaStats stats;
		stats.add(delta);
		str<<stats;
		if(level<1)
			return str<<'\n';
		str<<"\nlinks:";
		for(auto link: links)
			str<<"\n  - "<<link.first<<'/'<<gapr::link_id{link.second};
		str<<"\nprops:";
		for(auto& prop: props)
			str<<"\n  - +"<<prop.first<<'/'<<prop.second;
		str<<"\nnodes:";
		unsigned int idx=0;
		auto k=nid0.data;
		for(unsigned int j=0; j<nodes.size(); j++) {
			auto& node=nodes[j];
			gapr::link_id l;
			if(idx<links.size()) {
				if(links[idx].first==j+1)
					l=gapr::link_id{links[idx++].second};
			}
			str<<"\n  - ";
			if(l)
				str<<l;
			else
				str<<"+@"<<(k++);
			str<<' '<<gapr::node_attr{node.first}<<'/'<<node.second;
		}
		return str<<'\n';
		}(delta.links, delta.props, delta.nodes);
	}

	template<> struct SerializerAdaptor<gapr::delta_del_patch_, 0> {
		template<typename T> static auto& map(T& obj) { return obj.props; }
	};
	template<> struct SerializerAdaptor<gapr::delta_del_patch_, 1> {
		template<typename T> static auto& map(T& obj) { return obj.nodes; }
	};
	template<> struct SerializerUsePredictor<gapr::delta_del_patch_, 1> {
		constexpr static bool value=true;
	};
	template<> struct SerializerPredictor<gapr::delta_del_patch_, 1> {
		template<typename T> static auto sub(T obj, T obj0) {
			using Td=std::make_signed_t<T>;
			return static_cast<Td>(obj-obj0);
		}
		template<typename T, typename Td> static T add(Td d, T obj0) {
			return d+obj0;
		}
	};
	int cannolize_impl(delta_del_patch_& delta) noexcept {
		return [](auto& nodes, auto& props) ->int {
		for(std::size_t i=0; i<props.size(); i++) {
			auto id=props[i].first;
			if(i>0) {
				auto& prev_prop=props[i-1];
				if(id<prev_prop.first)
					return -1;
				if(id==prev_prop.first) {
					if(compare_tag(props[i].second, prev_prop.second)<=0)
						return -1;
				}
			} else if(id==0)
				return -1;
			if(!is_key_val_valid(props[i].second))
				return -1;
		}
		if(nodes.empty())
			return 0;
		// XXX strict checking?
		// eg. 1,2 to remove (1-2)
		//     2,1,1 to remove (2-1]
		//     1 to remove [1]
		//     2,1,3 to remove (2-1-3)
		//     2,2,1,3,3 to remove [2-1-3]
		// no leading 0
		// no trailing 0
		// no consecutive 0
		// no consecutive equal id, except the two ends.
		return 0;
		}(delta.nodes, delta.props);
	}
	std::ostream& dump_impl(const delta<delta_type::del_patch_>& delta, std::ostream& str, unsigned int level, gapr::node_id nid0) {
		return [&delta,&str,level](auto& nodes, auto& props) ->std::ostream& {
		DeltaStats stats;
		stats.add(delta);
		str<<stats;
		if(level<1)
			return str<<'\n';
		str<<"\nprops:";
		for(auto& prop: props)
			str<<"\n  - -"<<prop.first<<'/'<<prop.second;
		str<<"\nnodes:";
		for(auto node: nodes)
			str<<"\n  - -@"<<node;
		return str<<'\n';
		}(delta.nodes, delta.props);
	}

	template<> struct SerializerAdaptor<gapr::delta_proofread_, 0> {
		template<typename T> static auto& map(T& obj) { return obj.nodes; }
	};
	template<> struct SerializerUsePredictor<gapr::delta_proofread_, 0> {
		constexpr static bool value=true;
	};
	template<> struct SerializerPredictor<gapr::delta_proofread_, 0> {
		template<typename T> static auto sub(T obj, T obj0) {
			using Td=std::make_signed_t<T>;
			return static_cast<Td>(obj-obj0);
		}
		template<typename T, typename Td> static T add(Td d, T obj0) {
			return d+obj0;
		}
	};
	int cannolize_impl(delta_proofread_& delta) noexcept {
		return [](auto& nodes) ->int {
	////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////
		if(nodes.empty())
			return 0;
		// XXX
		// eg. 1,2 to proofread [1-2]
		//     1 to proofread [1]
		//     2,1,3 to proofread [2-1-3]
		// no 0
		return 0;
		}(delta.nodes);
	}
	std::ostream& dump_impl(const delta_proofread_& delta, std::ostream& str, unsigned int level, gapr::node_id nid0) {
		return [&delta,&str,level](auto& nodes) ->std::ostream& {
		DeltaStats stats;
		stats.add(delta);
		str<<stats;
		if(level<1)
			return str<<'\n';
		str<<"\nnodes:";
		for(auto node: nodes)
			str<<"\n  - @"<<node;
		return str<<'\n';
		}(delta.nodes);
	}

	template<> struct SerializerAdaptor<gapr::delta_reset_proofread_0_, 0> {
		template<typename T> static auto& map(T& obj) { return obj.nodes; }
	};
	template<> struct SerializerUsePredictor<gapr::delta_reset_proofread_0_, 0> {
		constexpr static bool value=true;
	};
	template<> struct SerializerPredictor<gapr::delta_reset_proofread_0_, 0> {
		template<typename T> static auto sub(T obj, T obj0) {
			using Td=std::make_signed_t<T>;
			return static_cast<Td>(obj-obj0);
		}
		template<typename T, typename Td> static T add(Td d, T obj0) {
			return d+obj0;
		}
	};
	template<> struct SerializerAdaptor<gapr::delta_reset_proofread_0_, 1> {
		template<typename T> static auto& map(T& obj) { return obj.props; }
	};
	int cannolize_impl(delta_reset_proofread_0_& delta) noexcept {
		return [](auto& nodes, auto& props) ->int {
		for(std::size_t i=0; i<props.size(); i++) {
			auto id=props[i].first;
			if(i>0) {
				auto& prev_prop=props[i-1];
				if(id<prev_prop.first)
					return -1;
				if(id==prev_prop.first) {
					if(compare_tag(props[i].second, prev_prop.second)<=0)
						return -1;
				}
			} else if(id==0)
				return -1;
			if(!is_key_val_valid(props[i].second))
				return -1;
		}
		if(nodes.empty())
			return 0;
		// XXX
		return 0;
		}(delta.nodes, delta.props);
	}
	std::ostream& dump_impl(const delta_reset_proofread_0_& delta, std::ostream& str, unsigned int level, gapr::node_id nid0) {
		return [&delta,&str,level](auto& nodes, auto& props) ->std::ostream& {
		DeltaStats stats;
		stats.add(delta);
		str<<stats;
		if(level<1)
			return str<<'\n';
		str<<"\nnodes:";
		for(auto node: nodes)
			str<<"\n  - @"<<node;
		str<<"\nprops:";
		for(auto& prop: props)
			str<<"\n  - -"<<prop.first<<'/'<<prop.second;
		return str<<'\n';
		}(delta.nodes, delta.props);
	}

	template<> struct SerializerAdaptor<gapr::delta_reset_proofread_, 0> {
		template<typename T> static auto& map(T& obj) { return obj.nodes; }
	};
	template<> struct SerializerAdaptor<gapr::delta_reset_proofread_, 1> {
		template<typename T> static auto& map(T& obj) { return obj.props; }
	};
	int cannolize_impl(delta_reset_proofread_& delta) noexcept {
		return [](auto& nodes, auto& props) ->int {
		for(std::size_t i=0; i<props.size(); i++) {
			auto id=props[i].first;
			if(i>0) {
				auto& prev_prop=props[i-1];
				if(id<prev_prop.first)
					return -1;
				if(id==prev_prop.first) {
					if(compare_tag(props[i].second, prev_prop.second)<=0)
						return -1;
				}
			} else if(id==0)
				return -1;
			if(!is_key_val_valid(props[i].second))
				return -1;
		}
		if(nodes.empty())
			return 0;
		// XXX
		return 0;
		}(delta.nodes, delta.props);
	}
	std::ostream& dump_impl(const delta_reset_proofread_& delta, std::ostream& str, unsigned int level, gapr::node_id nid0) {
		return [&delta,&str,level](auto& nodes, auto& props) ->std::ostream& {
		DeltaStats stats;
		stats.add(delta);
		str<<stats;
		if(level<1)
			return str<<'\n';
		str<<"\nnodes:";
		for(auto node: nodes)
			str<<"\n  - @"<<node;
		str<<"\nprops:";
		for(auto& prop: props)
			str<<"\n  - -"<<prop.first<<'/'<<prop.second;
		return str<<'\n';
		}(delta.nodes, delta.props);
	}

}

void gapr::dump_stack_(void** ptr, std::size_t n) {
	std::ostringstream oss;
	const char* s="0123456789abcdef";
	auto x=(unsigned char*)ptr;
	for(std::size_t i=0; i<n*sizeof(void*); i++) {
		if(i%4==0)
			oss<<' ';
		oss<<s[x[i]>>4]<<s[x[i]&0xf];
	}
	gapr::print("ser stack:", oss.str());
}

std::string_view gapr::to_string(gapr::delta_type type) noexcept {
	switch(type) {
		default:
			return std::string_view{"invalid2"};
		case delta_type::invalid:
			return std::string_view{"invalid"};
		case delta_type::add_edge_:
			return std::string_view{"add_edge_"};
		case delta_type::add_patch_:
			return std::string_view{"add_patch_"};
		case delta_type::add_prop_:
			return std::string_view{"add_prop_"};
		case delta_type::chg_prop_:
			return std::string_view{"chg_prop_"};
		case delta_type::del_patch_:
			return std::string_view{"del_patch_"};
		case delta_type::proofread_:
			return std::string_view{"proofread_"};
		case delta_type::reset_proofread_0_:
			return std::string_view{"reset_proofread_0_"};
		case delta_type::reset_proofread_:
			return std::string_view{"reset_proofread_"};
	}
}

std::string_view gapr::to_string(gapr::tier v) noexcept {
	switch(v) {
		case gapr::tier::root:
			return std::string_view{"root"};
		case gapr::tier::admin:
			return std::string_view{"admin"};
		case gapr::tier::annotator:
			return std::string_view{"annotator"};
		case gapr::tier::proofreader:
			return std::string_view{"proofreader"};
		case gapr::tier::restricted:
			return std::string_view{"restricted"};
		case gapr::tier::locked:
			return std::string_view{"locked"};
		case gapr::tier::nobody:
			return std::string_view{"nobody"};
	}
	return std::string_view{"invalid"};
}

std::string_view gapr::to_string(gapr::stage v) noexcept {
	switch(v) {
		case gapr::stage::initial:
			return std::string_view{"initial"};
		case gapr::stage::open:
			return std::string_view{"open"};
		case gapr::stage::guarded:
			return std::string_view{"guarded"};
		case gapr::stage::closed:
			return std::string_view{"closed"};
		case gapr::stage::frozen:
			return std::string_view{"frozen"};
	}
	return std::string_view{"invalid"};
}

namespace gapr {
	template<delta_type Typ>
		bool delta_traits<void, Typ>::load(delta<Typ>& delta, std::streambuf& str) {
			return load_impl(delta, str);
		}
	template<delta_type Typ>
		bool delta_traits<void, Typ>::save(const delta<Typ>& delta, std::streambuf& str) {
			return save_impl(delta, str);
		}
	template<delta_type Typ>
		int delta_traits<void, Typ>::cannolize(delta<Typ>& delta) noexcept {
			return cannolize_impl(delta);
		}
	template<delta_type Typ>
		std::ostream& delta_traits<void, Typ>::dump(const delta<Typ>& delta, std::ostream& str, unsigned int level, node_id nid0) {
			return dump_impl(delta, str, level, nid0);
		}
	template struct delta_traits<void, delta_type::add_edge_>;
	template struct delta_traits<void, delta_type::add_prop_>;
	template struct delta_traits<void, delta_type::chg_prop_>;
	template struct delta_traits<void, delta_type::add_patch_>;
	template struct delta_traits<void, delta_type::del_patch_>;
	template struct delta_traits<void, delta_type::proofread_>;
	template struct delta_traits<void, delta_type::reset_proofread_0_>;
	template struct delta_traits<void, delta_type::reset_proofread_>;
}

