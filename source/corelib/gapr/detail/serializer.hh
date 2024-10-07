/* detail/serializer.hh
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


#ifndef _GAPR_INCLUDE_DETAIL_SERIALIZER_HH_
#define _GAPR_INCLUDE_DETAIL_SERIALIZER_HH_

#include <cassert>
#include <tuple>
#include <vector>
#include <string>
#include <array>

// XXX remove states and reduce complexity
// consider cases: mem_file file socket(use cache object)
// save: target.alloc target.commit
// load: target.map target.bump
namespace gapr {
	template<typename ST, std::size_t SI> struct SerializerAdaptor;
	template<typename ST, std::size_t SI> struct SerializerPredictor;
	template<typename ST, std::size_t SI> struct SerializerUsePredictor;
	template<typename VT> struct SerializeAsVector;
}

namespace gapr { namespace Serializer_PRIV_v0 {

	struct void_t {
		void_t operator*() { return void_t{}; }
	};
	constexpr std::size_t BUF_SIZ{8+8};
	template<std::size_t N> struct State {
		std::size_t stkn{0};
		std::size_t bufn{BUF_SIZ};
		bool finished{false};
		union {
			unsigned char buf[BUF_SIZ];
		};
		int shift{0};
		std::array<void*, N> __stk{nullptr,};
	};
	template<typename T>
		constexpr std::size_t stk_usage() {
			constexpr auto sa=sizeof(T);
			constexpr auto sb=sizeof(void*);
			return (sa+sb-1)/sb;
		}

	template<std::size_t L0, typename It, typename St>
		inline It& stk_iter(St& st) {
			return *reinterpret_cast<It*>(&st.__stk[L0]);
		}
	template<std::size_t L0, typename St>
		inline std::size_t& stk_cnt(St& st) {
			return *reinterpret_cast<std::size_t*>(&st.__stk[L0]);
		}
	template<std::size_t L0, typename It, typename St>
		inline std::size_t& stk_cnt(St& st) {
			return *reinterpret_cast<std::size_t*>(&st.__stk[L0+stk_usage<It>()]);
		}
	template<std::size_t L0, typename It, typename St>
		inline unsigned int& stk_misc(St& st) {
			return *reinterpret_cast<unsigned int*>(&st.__stk[L0+stk_usage<It>()+stk_usage<std::size_t>()]);
		}


	template<typename T> struct s_traits;
	template<typename ST, std::size_t SI> struct st_traits;
	template<typename ST, std::size_t SI, typename V> struct st_traits_aux {
		constexpr static std::size_t tup_size=1+st_traits<ST, SI+1>::tup_size;
		constexpr static std::size_t stk_size=std::max(stk_usage<std::size_t>()+s_traits<V>::stk_size, st_traits<ST, SI+1>::stk_size);
	};
	template<typename ST, std::size_t SI> struct st_traits_aux<ST, SI, void_t> {
		constexpr static std::size_t tup_size=0;
		constexpr static std::size_t stk_size=0;
	};
	template<typename ST, std::size_t SI> struct st_traits {
		using adaptor=SerializerAdaptor<ST, SI>;
		using map_type=std::decay_t<decltype(adaptor::map(*static_cast<ST*>(nullptr)))>;
		constexpr static bool use_pred=SerializerUsePredictor<ST, SI>::value;
		constexpr static std::size_t tup_size=st_traits_aux<ST, SI, map_type>::tup_size;
		constexpr static std::size_t stk_size=st_traits_aux<ST, SI, map_type>::stk_size;
	};
	template<typename VT, typename It, typename V> struct sv_traits_aux {
		constexpr static std::size_t stk_size=stk_usage<It>()+stk_usage<std::size_t>()+stk_usage<unsigned int>()+s_traits<V>::stk_size;
	};
	template<typename VT, typename It> struct sv_traits_aux<VT, It, void_t> {
		constexpr static std::size_t stk_size=0;
	};
	template<typename VT> struct sv_traits {
		using adaptor=SerializeAsVector<VT>;
		using iter_type=std::decay_t<decltype(adaptor::begin(*static_cast<VT*>(nullptr)))>;
		using citer_type=std::decay_t<decltype(adaptor::begin(*static_cast<const VT*>(nullptr)))>;
		using map_type=std::decay_t<decltype(*adaptor::begin(*static_cast<const VT*>(nullptr)))>;
		constexpr static std::size_t stk_size=sv_traits_aux<VT, iter_type, map_type>::stk_size;
	};
	template<typename T> struct s_traits {
		constexpr static std::size_t is_tuple=st_traits<T, 0>::stk_size;
		constexpr static std::size_t is_vector=sv_traits<T>::stk_size;
		static_assert(!is_tuple || !is_vector,
				"mapping T to both tuple and vector not allowed");
		constexpr static std::size_t stk_size=is_tuple?is_tuple:(is_vector?is_vector:0);
	};


	template<typename ST_, std::size_t SI_, std::size_t N_, std::size_t L0_, typename PT_=void_t>
		struct s_ctx {
			using ST=ST_;
			using PT=PT_;
			constexpr static std::size_t SI=SI_;
			constexpr static std::size_t N=N_;
			constexpr static std::size_t L0=L0_;
		};
	template<typename T, typename CTX, std::size_t S> struct tup_saver;
	template<typename T, typename CTX, typename=void> struct vec_saver;
	template<typename T, typename CTX, typename=void> struct int_saver;
	template<typename T, typename CTX, typename=void> struct saver: int_saver<T, CTX> { };

	template<typename Sv> struct tup_saver_enter;
	template<typename T, typename CTX, std::size_t S>
		struct tup_saver_enter<tup_saver<T, CTX, S>> {
			constexpr static std::size_t L1=CTX::L0+stk_usage<std::size_t>();
			tup_saver_enter(State<CTX::N>& st) {
				if(L1>st.stkn) {
					st.stkn=L1;
					stk_cnt<CTX::L0>(st)=S;
				}
			}
		};
	template<typename Sv, std::size_t I> struct tup_saver_branch;
	template<typename T, typename CTX, std::size_t S, std::size_t I>
		struct tup_saver_branch<tup_saver<T, CTX, S>, I> {
			tup_saver_branch(State<CTX::N>& st, const T& obj, unsigned char* eptr, const typename CTX::PT& obj0): _r{false} {
				if(!saver<typename st_traits<T, S-I>::map_type, s_ctx<T, S-I, CTX::N, tup_saver_enter<tup_saver<T, CTX, S>>::L1, typename st_traits<typename CTX::PT, S-I>::map_type>>::save(st, gapr::SerializerAdaptor<T, S-I>::map(obj), eptr, gapr::SerializerAdaptor<typename CTX::PT, S-I>::map(obj0)))
					return;
				if(I>1) {
					stk_cnt<CTX::L0>(st)--;
					if(st.bufn<=BUF_SIZ)
						return;
				}
				_r=true;
			}
			operator bool() const noexcept { return _r; }
			private:
			bool _r;
		};
	template<typename Sv, std::size_t I> struct tup_loader_branch;
	template<typename T, typename CTX, std::size_t S, std::size_t I>
		struct tup_loader_branch<tup_saver<T, CTX, S>, I> {
			tup_loader_branch(State<CTX::N>& st, T& obj, const unsigned char* eptr, const typename CTX::PT& obj0): _r{false} {
				if(!saver<typename st_traits<T, S-I>::map_type, s_ctx<T, S-I, CTX::N, tup_saver_enter<tup_saver<T, CTX, S>>::L1, typename st_traits<typename CTX::PT, S-I>::map_type>>::load(st, gapr::SerializerAdaptor<T, S-I>::map(obj), eptr, gapr::SerializerAdaptor<typename CTX::PT, S-I>::map(obj0)))
					return;
				if(I>1) {
					stk_cnt<CTX::L0>(st)--;
					if(st.bufn<=0)
						return;
				}
				_r=true;
			}
			operator bool() const noexcept { return _r; }
			private:
			bool _r;
		};
	template<typename Sv> struct tup_saver_leave;
	template<typename T, typename CTX, std::size_t S>
		struct tup_saver_leave<tup_saver<T, CTX, S>> {
			tup_saver_leave(State<CTX::N>& st) { st.stkn=CTX::L0; }
			operator bool() const noexcept { return true; }
		};
	template<typename T, typename CTX> struct tup_saver<T, CTX, 1> {
		static bool save(State<CTX::N>& st, const T& obj, unsigned char* eptr, const typename CTX::PT& obj0) {
			return saver<typename st_traits<T, 0>::map_type, s_ctx<T, 0, CTX::N, CTX::L0, typename st_traits<typename CTX::PT, 0>::map_type>>::save(st, gapr::SerializerAdaptor<T, 0>::map(obj), eptr, gapr::SerializerAdaptor<typename CTX::PT, 0>::map(obj0));
		}
		static bool load(State<CTX::N>& st, T& obj, const unsigned char* eptr, const typename CTX::PT& obj0) {
			return saver<typename st_traits<T, 0>::map_type, s_ctx<T, 0, CTX::N, CTX::L0, typename st_traits<typename CTX::PT, 0>::map_type>>::load(st, gapr::SerializerAdaptor<T, 0>::map(obj), eptr, gapr::SerializerAdaptor<typename CTX::PT, 0>::map(obj0));
		}
	};
	template<typename T, typename CTX> struct tup_saver<T, CTX, 2> {
		static bool save(State<CTX::N>& st, const T& obj, unsigned char* eptr, const typename CTX::PT& obj0) {
			tup_saver_enter<tup_saver>{st};
			switch(stk_cnt<CTX::L0>(st)) {
				case 2: if(!tup_saver_branch<tup_saver, 2>{st, obj, eptr, obj0})
							  return false;
				case 1: if(!tup_saver_branch<tup_saver, 1>{st, obj, eptr, obj0})
							  return false;
			}
			return tup_saver_leave<tup_saver>{st};
		}
		static bool load(State<CTX::N>& st, T& obj, const unsigned char* eptr, const typename CTX::PT& obj0) {
			tup_saver_enter<tup_saver>{st};
			switch(stk_cnt<CTX::L0>(st)) {
				case 2: if(!tup_loader_branch<tup_saver, 2>{st, obj, eptr, obj0})
							  return false;
				case 1: if(!tup_loader_branch<tup_saver, 1>{st, obj, eptr, obj0})
							  return false;
			}
			return tup_saver_leave<tup_saver>{st};
		}
	};
	template<typename T, typename CTX> struct tup_saver<T, CTX, 3> {
		static bool save(State<CTX::N>& st, const T& obj, unsigned char* eptr, const typename CTX::PT& obj0) {
			tup_saver_enter<tup_saver>{st};
			switch(stk_cnt<CTX::L0>(st)) {
				case 3: if(!tup_saver_branch<tup_saver, 3>{st, obj, eptr, obj0})
							  return false;
				case 2: if(!tup_saver_branch<tup_saver, 2>{st, obj, eptr, obj0})
							  return false;
				case 1: if(!tup_saver_branch<tup_saver, 1>{st, obj, eptr, obj0})
							  return false;
			}
			return tup_saver_leave<tup_saver>{st};
		}
		static bool load(State<CTX::N>& st, T& obj, const unsigned char* eptr, const typename CTX::PT& obj0) {
			tup_saver_enter<tup_saver>{st};
			switch(stk_cnt<CTX::L0>(st)) {
				case 3: if(!tup_loader_branch<tup_saver, 3>{st, obj, eptr, obj0})
							  return false;
				case 2: if(!tup_loader_branch<tup_saver, 2>{st, obj, eptr, obj0})
							  return false;
				case 1: if(!tup_loader_branch<tup_saver, 1>{st, obj, eptr, obj0})
							  return false;
			}
			return tup_saver_leave<tup_saver>{st};
		}
	};
	template<typename T, typename CTX> struct tup_saver<T, CTX, 4> {
		static bool save(State<CTX::N>& st, const T& obj, unsigned char* eptr, const typename CTX::PT& obj0) {
			tup_saver_enter<tup_saver>{st};
			switch(stk_cnt<CTX::L0>(st)) {
				case 4: if(!tup_saver_branch<tup_saver, 4>{st, obj, eptr, obj0})
							  return false;
				case 3: if(!tup_saver_branch<tup_saver, 3>{st, obj, eptr, obj0})
							  return false;
				case 2: if(!tup_saver_branch<tup_saver, 2>{st, obj, eptr, obj0})
							  return false;
				case 1: if(!tup_saver_branch<tup_saver, 1>{st, obj, eptr, obj0})
							  return false;
			}
			return tup_saver_leave<tup_saver>{st};
		}
		static bool load(State<CTX::N>& st, T& obj, const unsigned char* eptr, const typename CTX::PT& obj0) {
			tup_saver_enter<tup_saver>{st};
			switch(stk_cnt<CTX::L0>(st)) {
				case 4: if(!tup_loader_branch<tup_saver, 4>{st, obj, eptr, obj0})
							  return false;
				case 3: if(!tup_loader_branch<tup_saver, 3>{st, obj, eptr, obj0})
							  return false;
				case 2: if(!tup_loader_branch<tup_saver, 2>{st, obj, eptr, obj0})
							  return false;
				case 1: if(!tup_loader_branch<tup_saver, 1>{st, obj, eptr, obj0})
							  return false;
			}
			return tup_saver_leave<tup_saver>{st};
		}
	};
	template<typename T, typename CTX> struct tup_saver<T, CTX, 5> {
		static bool save(State<CTX::N>& st, const T& obj, unsigned char* eptr, const typename CTX::PT& obj0) {
			tup_saver_enter<tup_saver>{st};
			switch(stk_cnt<CTX::L0>(st)) {
				case 5: if(!tup_saver_branch<tup_saver, 5>{st, obj, eptr, obj0})
							  return false;
				case 4: if(!tup_saver_branch<tup_saver, 4>{st, obj, eptr, obj0})
							  return false;
				case 3: if(!tup_saver_branch<tup_saver, 3>{st, obj, eptr, obj0})
							  return false;
				case 2: if(!tup_saver_branch<tup_saver, 2>{st, obj, eptr, obj0})
							  return false;
				case 1: if(!tup_saver_branch<tup_saver, 1>{st, obj, eptr, obj0})
							  return false;
			}
			return tup_saver_leave<tup_saver>{st};
		}
		static bool load(State<CTX::N>& st, T& obj, const unsigned char* eptr, const typename CTX::PT& obj0) {
			tup_saver_enter<tup_saver>{st};
			switch(stk_cnt<CTX::L0>(st)) {
				case 5: if(!tup_loader_branch<tup_saver, 5>{st, obj, eptr, obj0})
							  return false;
				case 4: if(!tup_loader_branch<tup_saver, 4>{st, obj, eptr, obj0})
							  return false;
				case 3: if(!tup_loader_branch<tup_saver, 3>{st, obj, eptr, obj0})
							  return false;
				case 2: if(!tup_loader_branch<tup_saver, 2>{st, obj, eptr, obj0})
							  return false;
				case 1: if(!tup_loader_branch<tup_saver, 1>{st, obj, eptr, obj0})
							  return false;
			}
			return tup_saver_leave<tup_saver>{st};
		}
	};
	template<typename T, typename CTX> struct tup_saver<T, CTX, 6> {
		static bool save(State<CTX::N>& st, const T& obj, unsigned char* eptr, const typename CTX::PT& obj0) {
			tup_saver_enter<tup_saver>{st};
			switch(stk_cnt<CTX::L0>(st)) {
				case 6: if(!tup_saver_branch<tup_saver, 6>{st, obj, eptr, obj0})
							  return false;
				case 5: if(!tup_saver_branch<tup_saver, 5>{st, obj, eptr, obj0})
							  return false;
				case 4: if(!tup_saver_branch<tup_saver, 4>{st, obj, eptr, obj0})
							  return false;
				case 3: if(!tup_saver_branch<tup_saver, 3>{st, obj, eptr, obj0})
							  return false;
				case 2: if(!tup_saver_branch<tup_saver, 2>{st, obj, eptr, obj0})
							  return false;
				case 1: if(!tup_saver_branch<tup_saver, 1>{st, obj, eptr, obj0})
							  return false;
			}
			return tup_saver_leave<tup_saver>{st};
		}
		static bool load(State<CTX::N>& st, T& obj, const unsigned char* eptr, const typename CTX::PT& obj0) {
			tup_saver_enter<tup_saver>{st};
			switch(stk_cnt<CTX::L0>(st)) {
				case 6: if(!tup_loader_branch<tup_saver, 6>{st, obj, eptr, obj0})
							  return false;
				case 5: if(!tup_loader_branch<tup_saver, 5>{st, obj, eptr, obj0})
							  return false;
				case 4: if(!tup_loader_branch<tup_saver, 4>{st, obj, eptr, obj0})
							  return false;
				case 3: if(!tup_loader_branch<tup_saver, 3>{st, obj, eptr, obj0})
							  return false;
				case 2: if(!tup_loader_branch<tup_saver, 2>{st, obj, eptr, obj0})
							  return false;
				case 1: if(!tup_loader_branch<tup_saver, 1>{st, obj, eptr, obj0})
							  return false;
			}
			return tup_saver_leave<tup_saver>{st};
		}
	};

	template<typename T, std::size_t N>
		inline bool int_save_raw(State<N>& st, T obj, unsigned char* eptr) {
			static_assert(sizeof(T)<=BUF_SIZ+1);
			static_assert(std::is_unsigned<T>::value);
			*(eptr-(st.bufn--))=static_cast<unsigned char>(obj);
			if constexpr(sizeof(T)>1) {
				std::size_t left=sizeof(T)-1;
				while(st.bufn>BUF_SIZ && left>0) {
					obj>>=8;
					*(eptr-(st.bufn--))=static_cast<unsigned char>(obj);
					left--;
				}
				if(left>0) {
					st.bufn=BUF_SIZ-left;
					do {
						obj>>=8;
						st.buf[BUF_SIZ-(left--)]=static_cast<unsigned char>(obj);
					} while(left>0);
				}
			}
			return true;
		}
	template<typename T, std::size_t N>
		inline bool int_load_raw(State<N>& st, T& obj, const unsigned char* eptr) {
			static_assert(std::is_unsigned<T>::value);
			auto shift=st.shift;
			if(shift==0) {
				auto v=T{*(eptr-(st.bufn--))};
				shift++;
				while(st.bufn>0 && shift<static_cast<int>(sizeof(T)))
					v|=(T{*(eptr-(st.bufn--))}<<((shift++)*8));
				obj=v;
				if(shift==sizeof(T))
					return true;
			} else {
				T v{0};
				do
					v|=(T{*(eptr-(st.bufn--))}<<((shift++)*8));
				while(st.bufn>0 && shift<static_cast<int>(sizeof(T)));
				obj|=v;
				if(shift==sizeof(T)) {
					st.shift=0;
					return true;
				}
			}
			st.shift=shift;
			return false;
		}
	template<typename T, std::size_t N>
		inline bool int_save_enc(State<N>& st, T obj, unsigned char* eptr) {
			static_assert((sizeof(T)*8+6)/7<=BUF_SIZ+1);
			static_assert(std::is_unsigned<T>::value);
			static_assert(sizeof(T)>1);
			unsigned char tmp=static_cast<unsigned char>(obj);
			obj>>=7;
			if(!obj) {
				*(eptr-(st.bufn--))=tmp;
				return true;
			}
			*(eptr-(st.bufn--))=tmp|0x80;
			while(st.bufn>BUF_SIZ) {
				tmp=static_cast<unsigned char>(obj);
				obj>>=7;
				if(!obj) {
					*(eptr-(st.bufn--))=tmp;
					return true;
				}
				*(eptr-(st.bufn--))=tmp|0x80;
			}
			std::size_t left=1;
			auto obj1=obj>>7;
			while(obj1) {
				left++;
				obj1>>=7;
			}
			st.bufn=BUF_SIZ-left;
			do {
				tmp=static_cast<unsigned char>(obj);
				obj>>=7;
				if(!obj) {
					st.buf[BUF_SIZ-(left--)]=tmp;
					break;
				}
				st.buf[BUF_SIZ-(left--)]=tmp|0x80;
			} while(true);
			return true;
		}
	template<typename T, std::size_t N>
		inline bool int_load_enc(State<N>& st, T& obj, const unsigned char* eptr) {
			static_assert(std::is_unsigned<T>::value);
			static_assert(sizeof(T)>1);
			auto shift=st.shift;
			if(shift==0) {
				auto c=*(eptr-(st.bufn--));
				auto v=static_cast<T>(c&0x7f);
				shift++;
				while(st.bufn>0 && (c&0x80)) {
					c=*(eptr-(st.bufn--));
					v|=(static_cast<T>(c&0x7f)<<((shift++)*7));
				}
				obj=v;
				if(!(c&0x80))
					return true;
			} else {
				T v{0};
				unsigned char c;
				do {
					c=*(eptr-(st.bufn--));
					v|=(static_cast<T>(c&0x7f)<<((shift++)*7));
				} while(st.bufn>0 && (c&0x80));
				obj|=v;
				if(!(c&0x80)) {
					st.shift=0;
					return true;
				}
			}
			st.shift=shift;
			return false;
		}
	template<typename T, std::size_t N>
		inline bool int_save_zz(State<N>& st, T obj, unsigned char* eptr) {
			static_assert(std::is_signed<T>::value);
			static_assert(sizeof(T)>1);
			using T1=std::make_unsigned_t<T>;
			auto obj1=static_cast<T1>((obj<<1)^(obj>>(sizeof(T)*8-1)));
			return int_save_enc<T1, N>(st, obj1, eptr);
		}
	template<typename T, std::size_t N>
		inline bool int_load_zz(State<N>& st, T& obj, const unsigned char* eptr) {
			static_assert(std::is_signed<T>::value);
			static_assert(sizeof(T)>1);
			using T1=std::make_unsigned_t<T>;
			if(!int_load_enc<T1, N>(st, *reinterpret_cast<T1*>(&obj), eptr))
				return false;
			auto v=static_cast<T1>(obj);
			if(v&1) {
				v=(v>>1)^(~T1{0});
			} else {
				v=v>>1;
			}
			obj=static_cast<T>(v);
			return true;
		}

	template<typename T, typename CTX> struct int_saver<T, CTX,
		std::enable_if_t<std::is_integral<T>::value && (sizeof(T)<=1) && std::is_same<typename CTX::PT, void_t>::value>> {
			using UT=std::make_unsigned_t<T>;
			static bool save(State<CTX::N>& st, T obj, unsigned char* eptr, typename CTX::PT obj0) {
				return int_save_raw<UT, CTX::N>(st, static_cast<UT>(obj), eptr);
			}
			static bool load(State<CTX::N>& st, T& obj, const unsigned char* eptr, typename CTX::PT obj0) {
				return int_load_raw<UT, CTX::N>(st, reinterpret_cast<UT&>(obj), eptr);
			}
		};
	template<typename T, typename CTX> struct int_saver<T, CTX,
		std::enable_if_t<std::is_integral<T>::value && (sizeof(T)>1) && std::is_signed<T>::value && std::is_same<typename CTX::PT, void_t>::value>> {
			static bool save(State<CTX::N>& st, T obj, unsigned char* eptr, void_t obj0) {
				return int_save_zz<T, CTX::N>(st, obj, eptr);
			}
			static bool load(State<CTX::N>& st, T& obj, const unsigned char* eptr, void_t obj0) {
				return int_load_zz<T, CTX::N>(st, obj, eptr);
			}
		};
	template<typename T, typename CTX> struct int_saver<T, CTX,
		std::enable_if_t<std::is_integral<T>::value && (sizeof(T)>1) && !std::is_signed<T>::value && std::is_same<typename CTX::PT, void_t>::value>> {
			static bool save(State<CTX::N>& st, T obj, unsigned char* eptr, void_t obj0) {
				return int_save_enc<T, CTX::N>(st, obj, eptr);
				// unsigned
			}
			static bool load(State<CTX::N>& st, T& obj, const unsigned char* eptr, void_t obj0) {
				return int_load_enc<T, CTX::N>(st, obj, eptr);
				// unsigned
			}
		};
	template<typename T, typename CTX> struct int_saver<T, CTX,
		std::enable_if_t<std::is_integral<T>::value && !std::is_same<typename CTX::PT, void_t>::value>> {
			using Td=decltype(gapr::SerializerPredictor<typename CTX::ST, CTX::SI>::sub(T{0}, typename CTX::PT{0}));
			static bool save(State<CTX::N>& st, T obj, unsigned char* eptr, typename CTX::PT obj0) {
				Td d=gapr::SerializerPredictor<typename CTX::ST, CTX::SI>::sub(obj, obj0);
				return int_saver<Td, s_ctx<typename CTX::ST, CTX::SI, CTX::N, CTX::L0, void_t>>::save(st, d, eptr, void_t{});
				//signed
			}
			static bool load(State<CTX::N>& st, T& obj, const unsigned char* eptr, typename CTX::PT obj0) {
				if(!int_saver<Td, s_ctx<typename CTX::ST, CTX::SI, CTX::N, CTX::L0, void_t>>::load(st, reinterpret_cast<Td&>(obj), eptr, void_t{}))
					return false;
				obj=static_cast<T>(gapr::SerializerPredictor<typename CTX::ST, CTX::SI>::add(static_cast<Td>(obj), obj0));
				return true;
			}
		};

	template<typename T, typename CTX> struct vec_saver<T, CTX,
		std::enable_if_t<st_traits<typename CTX::ST, CTX::SI>::use_pred>
			> {
				using iter_type=typename sv_traits<T>::iter_type;
				using citer_type=typename sv_traits<T>::citer_type;
				constexpr static std::size_t L1=CTX::L0+stk_usage<iter_type>()+stk_usage<std::size_t>()+stk_usage<unsigned int>();
				static bool save(State<CTX::N>& st, const T& obj, unsigned char* eptr, const typename CTX::PT& obj0) {
					if(L1>st.stkn) {
						st.stkn=L1;
						stk_misc<CTX::L0, iter_type>(st)=0;
						stk_cnt<CTX::L0, iter_type>(st)=gapr::SerializeAsVector<T>::count(obj);
					}
					//const char* format="                            save %zd\n";
					//fprintf(stderr, format+20-CTX::L0, stk_cnt<CTX::L0, iter_type>(st));
					switch(stk_misc<CTX::L0, iter_type>(st)) {
						case 0:
							if(!saver<std::size_t, s_ctx<typename CTX::ST, CTX::SI, CTX::N, L1>>::save(st, stk_cnt<CTX::L0, iter_type>(st), eptr, void_t{}))
								return false;
							if(stk_cnt<CTX::L0, iter_type>(st)<=0)
								break;
							stk_misc<CTX::L0, iter_type>(st)=1;
							stk_iter<CTX::L0, citer_type>(st)=gapr::SerializeAsVector<T>::begin(obj);
							if(st.bufn<=BUF_SIZ)
								return false;
						case 1:
							//save orig
							if(!saver<typename sv_traits<T>::map_type, s_ctx<typename CTX::ST, CTX::SI, CTX::N, L1>>::save(st, *stk_iter<CTX::L0, iter_type>(st), eptr, void_t{}))
								return false;
							if(stk_cnt<CTX::L0, iter_type>(st)<=1)
								break;
							stk_cnt<CTX::L0, iter_type>(st)--;
							stk_misc<CTX::L0, iter_type>(st)=2;
							if(st.bufn<=BUF_SIZ)
								return false;
						case 2:
							do {
								auto st1=stk_iter<CTX::L0, iter_type>(st);
								++st1;
								// XXX save diff
								if(!saver<typename sv_traits<T>::map_type, s_ctx<typename CTX::ST, CTX::SI, CTX::N, L1, typename sv_traits<T>::map_type>>::save(st, *st1, eptr, *stk_iter<CTX::L0, iter_type>(st)))
									return false;
								if(stk_cnt<CTX::L0, iter_type>(st)<=1)
									break;
								stk_cnt<CTX::L0, iter_type>(st)--;
								stk_iter<CTX::L0, iter_type>(st)=st1;
								if(st.bufn<=BUF_SIZ)
									return false;
							} while(true);

					}
					st.stkn=CTX::L0;
					return true;
				}
				static bool load(State<CTX::N>& st, T& obj, const unsigned char* eptr, const typename CTX::PT& obj0) {
					if(L1>st.stkn) {
						st.stkn=L1;
						stk_misc<CTX::L0, iter_type>(st)=0;
					}
					switch(stk_misc<CTX::L0, iter_type>(st)) {
						case 0:
							if(!saver<std::size_t, s_ctx<typename CTX::ST, CTX::SI, CTX::N, L1>>::load(st, stk_cnt<CTX::L0, iter_type>(st), eptr, void_t{}))
								return false;
							gapr::SerializeAsVector<T>::resize(obj, stk_cnt<CTX::L0, iter_type>(st));
							if(stk_cnt<CTX::L0, iter_type>(st)<=0)
								break;
							stk_misc<CTX::L0, iter_type>(st)=1;
							stk_iter<CTX::L0, iter_type>(st)=gapr::SerializeAsVector<T>::begin(obj);
							if(st.bufn<=0)
								return false;
						case 1:
							//load orig
							if(!saver<typename sv_traits<T>::map_type, s_ctx<typename CTX::ST, CTX::SI, CTX::N, L1>>::load(st, *stk_iter<CTX::L0, iter_type>(st), eptr, void_t{}))
								return false;
							if(stk_cnt<CTX::L0, iter_type>(st)<=1)
								break;
							stk_cnt<CTX::L0, iter_type>(st)--;
							stk_misc<CTX::L0, iter_type>(st)=2;
							if(st.bufn<=0)
								return false;
						case 2:
							do {
								auto st1=stk_iter<CTX::L0, iter_type>(st);
								++st1;
								// XXX save diff
								if(!saver<typename sv_traits<T>::map_type, s_ctx<typename CTX::ST, CTX::SI, CTX::N, L1, typename sv_traits<T>::map_type>>::load(st, *st1, eptr, *stk_iter<CTX::L0, iter_type>(st)))
									return false;
								if(stk_cnt<CTX::L0, iter_type>(st)<=1)
									break;
								stk_cnt<CTX::L0, iter_type>(st)--;
								stk_iter<CTX::L0, iter_type>(st)=st1;
								if(st.bufn<=0)
									return false;
							} while(true);
					}
					st.stkn=CTX::L0;
					return true;
				}
			};
	template<typename T, typename CTX> struct vec_saver<T, CTX,
		std::enable_if_t<!st_traits<typename CTX::ST, CTX::SI>::use_pred>
			> {
				using iter_type=typename sv_traits<T>::iter_type;
				using citer_type=typename sv_traits<T>::citer_type;
				constexpr static std::size_t L1=CTX::L0+stk_usage<iter_type>()+stk_usage<std::size_t>()+stk_usage<unsigned int>();
				static bool save(State<CTX::N>& st, const T& obj, unsigned char* eptr, const typename CTX::PT& obj0) {
					//const char* format="                            save %zd\n";
					//const char* format2="                            end\n";
					//const char* format3="                            start\n";
					//auto indent=20-CTX::L0;
					if(L1>st.stkn) {
						st.stkn=L1;
						stk_misc<CTX::L0, iter_type>(st)=0;
						stk_cnt<CTX::L0, iter_type>(st)=gapr::SerializeAsVector<T>::count(obj);
						//fprintf(stderr, format3+indent);
					}
					//fprintf(stderr, format+indent, stk_cnt<CTX::L0, iter_type>(st));
					switch(stk_misc<CTX::L0, iter_type>(st)) {
						case 0:
							if(!saver<std::size_t, s_ctx<typename CTX::ST, CTX::SI, CTX::N, L1>>::save(st, stk_cnt<CTX::L0, iter_type>(st), eptr, void_t{}))
								return false;
							if(stk_cnt<CTX::L0, iter_type>(st)<=0)
								break;
							stk_misc<CTX::L0, iter_type>(st)=1;
							stk_iter<CTX::L0, citer_type>(st)=gapr::SerializeAsVector<T>::begin(obj);
							if(st.bufn<=BUF_SIZ)
								return false;
						case 1:
							do {
								if(!saver<typename sv_traits<T>::map_type, s_ctx<typename CTX::ST, CTX::SI, CTX::N, L1>>::save(st, *stk_iter<CTX::L0, iter_type>(st), eptr, void_t{}))
									return false;
								if(stk_cnt<CTX::L0, iter_type>(st)<=1)
									break;
								stk_cnt<CTX::L0, iter_type>(st)--;
								++stk_iter<CTX::L0, iter_type>(st);
								if(st.bufn<=BUF_SIZ)
									return false;
							} while(true);
					}
					st.stkn=CTX::L0;
					//fprintf(stderr, format2+indent);
					return true;
				}
				static bool load(State<CTX::N>& st, T& obj, const unsigned char* eptr, const typename CTX::PT& obj0) {
					if(L1>st.stkn) {
						st.stkn=L1;
						stk_misc<CTX::L0, iter_type>(st)=0;
					}
					switch(stk_misc<CTX::L0, iter_type>(st)) {
						case 0:
							if(!saver<std::size_t, s_ctx<typename CTX::ST, CTX::SI, CTX::N, L1>>::load(st, stk_cnt<CTX::L0, iter_type>(st), eptr, void_t{}))
								return false;
							gapr::SerializeAsVector<T>::resize(obj, stk_cnt<CTX::L0, iter_type>(st));
							if(stk_cnt<CTX::L0, iter_type>(st)<=0)
								break;
							stk_misc<CTX::L0, iter_type>(st)=1;
							stk_iter<CTX::L0, iter_type>(st)=gapr::SerializeAsVector<T>::begin(obj);
							if(st.bufn<=0)
								return false;
						case 1:
							do {
								if(!saver<typename sv_traits<T>::map_type, s_ctx<typename CTX::ST, CTX::SI, CTX::N, L1>>::load(st, *stk_iter<CTX::L0, iter_type>(st), eptr, void_t{}))
									return false;
								if(stk_cnt<CTX::L0, iter_type>(st)<=1)
									break;
								stk_cnt<CTX::L0, iter_type>(st)--;
								++stk_iter<CTX::L0, iter_type>(st);
								if(st.bufn<=0)
									return false;
							} while(true);
					}
					st.stkn=CTX::L0;
					return true;
				}
			};

	template<typename T, typename CTX>
		struct saver<T, CTX, std::enable_if_t<s_traits<T>::is_tuple!=0>>:
		tup_saver<T, CTX, st_traits<T, 0>::tup_size> { };
	template<typename T, typename CTX>
		struct saver<T, CTX, std::enable_if_t<s_traits<T>::is_vector!=0>>:
		vec_saver<T, CTX> { };

	template<typename T, std::size_t N> inline std::size_t do_save(State<N>& st, const T& obj, unsigned char* ptr, std::size_t len) {
		std::size_t i=0;
		while(st.bufn<BUF_SIZ && i<len)
			ptr[i++]=st.buf[st.bufn++];
		if(i>=len)
			return i;
		if(st.finished)
			return i;
		st.bufn=len+BUF_SIZ-i;
		auto eptr=ptr+len+BUF_SIZ;
		st.finished=saver<T, s_ctx<void_t, 0, N, 0>>::save(st, obj, eptr, void_t{});
		return st.bufn<=BUF_SIZ?len:(len+BUF_SIZ-st.bufn);
	}

	template<typename T, std::size_t N> inline std::size_t do_load(State<N>& st, T& obj, const unsigned char* ptr, std::size_t len) {
		if(st.finished)
			return 0;
		if(len<=0)
			return 0;
		st.bufn=len;
		auto eptr=ptr+len;
		st.finished=saver<T, s_ctx<void_t, 0, N, 0>>::load(st, obj, eptr, void_t{});
		return len-st.bufn;
	}

} }

template<typename... Ts, std::size_t I> struct gapr::SerializerAdaptor<std::tuple<Ts...>, I> {
	template<typename T> static auto& map(T& obj) {
		if constexpr(I<sizeof...(Ts)) {
			return std::get<I>(obj);
		} else {
			return *static_cast<Serializer_PRIV_v0::void_t*>(nullptr);
		}
	}
};
#if 0
template<typename... Ts> struct gapr::SerializerAdaptor<std::tuple<Ts...>, sizeof...(Ts)> {
	template<typename T> static auto& map(T& obj) {
		return *static_cast<Serializer_PRIV_v0::void_t*>(nullptr);
	}
};
#endif
template<typename E0> struct gapr::SerializeAsVector<std::vector<E0>> {
	template<typename T> static std::size_t count(T& obj) { return obj.size(); }
	template<typename T> static auto begin(T& obj) { return obj.begin(); }
	template<typename T> static void resize(T& obj, std::size_t s) { return obj.resize(s); }
};
template<> struct gapr::SerializeAsVector<std::string> {
	template<typename T> static std::size_t count(T& obj) { return obj.size(); }
	template<typename T> static auto begin(T& obj) { return obj.begin(); }
	template<typename T> static void resize(T& obj, std::size_t s) { return obj.resize(s); }
};

template<typename Ta, typename Tb> struct gapr::SerializerAdaptor<std::pair<Ta, Tb>, 0> {
	template<typename T> static auto& map(T& obj) { return obj.first; }
};
template<typename Ta, typename Tb> struct gapr::SerializerAdaptor<std::pair<Ta, Tb>, 1> {
	template<typename T> static auto& map(T& obj) { return obj.second; }
};

template<typename Ta, std::size_t N, std::size_t I> struct gapr::SerializerAdaptor<std::array<Ta, N>, I> {
	template<typename T> static auto& map(T& obj) {
		return std::get<I>(obj);
	}
};
template<typename Ta, std::size_t N> struct gapr::SerializerAdaptor<std::array<Ta, N>, N> {
	template<typename T> static auto map(T& obj) {
		return Serializer_PRIV_v0::void_t{};
	}
};


#endif
