/* serializer.hh
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

/*! serializer
 *
 * Boost.Serialization has no binary archive
 * ProtoBuf not C++ friendly
 *
 * * adapt any type for serialization
 * * can resume
 * * take difference
 * * reduce leading 0 and f of integers
 * * take difference for custom types
 */

//@@@
#ifndef _GAPR_INCLUDE_SERIALIZER_HH_
#define _GAPR_INCLUDE_SERIALIZER_HH_

#include "gapr/detail/serializer.hh"

namespace gapr {

	template<typename ST, std::size_t SI> struct SerializerAdaptor {
		template<typename T> static auto map(T& obj) {
			return Serializer_PRIV_v0::void_t{};
		}
	};
	template<typename ST, std::size_t SI> struct SerializerPredictor {
		template<typename T>
			static auto sub(T obj, T obj0) {
				return static_cast<std::make_signed_t<T>>(obj-obj0);
			}
		template<typename T, typename Td>
			static T add(Td d, T obj0) {
				return static_cast<T>(d+obj0);
			}
	};
	template<typename ST, std::size_t SI> struct SerializerUsePredictor {
		constexpr static bool value=false;
	};

	template<typename VT> struct SerializeAsVector {
		template<typename T> static std::size_t count(T& obj) { return 0; }
		template<typename T> static auto begin(T& obj) {
			return Serializer_PRIV_v0::void_t{};
		}
	};

	void dump_stack_(void** ptr, std::size_t n);

	template<typename T> class Serializer_v0 {
		public:
			std::size_t save(const T& obj, void* ptr, std::size_t len) noexcept {
				//dump_stack_(&_st.__stk[0], _st.__stk.size());
				return Serializer_PRIV_v0::do_save<T, stk_size>(_st, obj, static_cast<unsigned char*>(ptr), len);
			}
			explicit operator bool() const noexcept {
				return _st.bufn<Serializer_PRIV_v0::BUF_SIZ || !_st.finished;
			}
		private:
			using traits=Serializer_PRIV_v0::st_traits<T, 0>;
			static_assert(traits::tup_size>0);
			constexpr static std::size_t stk_size=traits::stk_size;
			Serializer_PRIV_v0::State<stk_size> _st;
	};
	template<typename T> class Deserializer_v0 {
		public:
			std::size_t load(T& obj, const void* ptr, std::size_t len) noexcept {
				//dump_stack_(&_st.__stk[0], _st.__stk.size());
				return Serializer_PRIV_v0::do_load<T, stk_size>(_st, obj, static_cast<const unsigned char*>(ptr), len);
			}
			explicit operator bool() const noexcept {
				return !_st.finished;
			}
		private:
			using traits=Serializer_PRIV_v0::st_traits<T, 0>;
			static_assert(traits::tup_size>0);
			constexpr static std::size_t stk_size=traits::stk_size;
			Serializer_PRIV_v0::State<stk_size> _st;
	};

	template<typename T> class Serializer: public Serializer_v0<T> { };
	template<typename T> class Deserializer: public Deserializer_v0<T> { };

}


///////////////////////////////





struct _Commit {
	private:
		std::string who{"hello"};
		int64_t when{3334};
		template<typename T, std::size_t I> friend struct gapr::SerializerAdaptor;
};
template<> struct gapr::SerializerAdaptor<_Commit, 0> {
	template<typename T> auto& operator()(T& obj) { return obj.who; }
};
template<> struct gapr::SerializerAdaptor<_Commit, 1> {
	template<typename T> auto& operator()(T& obj) { return obj.when; }
};

#if 0
int changed_nodes_size() const;
void clear_changed_nodes();
static const int kChangedNodesFieldNumber = 3;
const ::gapr::pb::Node& changed_nodes(int index) const;
::gapr::pb::Node* mutable_changed_nodes(int index);
::gapr::pb::Node* add_changed_nodes();
::google::protobuf::RepeatedPtrField< ::gapr::pb::Node >*
mutable_changed_nodes();
const ::google::protobuf::RepeatedPtrField< ::gapr::pb::Node >&
changed_nodes() const;
#endif
#endif
