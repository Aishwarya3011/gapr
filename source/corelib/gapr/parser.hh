/* parser.hh
 *
 * Copyright (C) 2018 GOU Lingfeng
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


#ifndef _GAPR_INCLUDE_PARSER_HH_
#define _GAPR_INCLUDE_PARSER_HH_

#include "gapr/config.hh"

#include "gapr/detail/ascii-tables.hh"

#include <string>
//#include <type_traits>
//#include <atomic>
//#include <array>


namespace gapr {

	namespace Parser_PRIV {
		enum CHR_TYPE: unsigned int {
			TYPE_ALPHA,
			TYPE_NUM,
			TYPE_DOT,
		};

		enum ErrorCode {
			NO_ERR=0,
			ERR_CHR_NOT_HEX,
			ERR_CHR_NOT_DEC,
			ERR_CHR_NOT_B64,
			ERR_EMPTY,

			ERR_NAME_BEGIN_NOT_ALPHA,
			ERR_NAME_DUP_DOT,
			ERR_NAME_END_WITH_DOT,
			NUM_ERRS
		};
		class ErrorStrs {
			public:
				static const char* get(ErrorCode c) {
					return _error_strs._strs[c];
				}
			private:
				std::array<const char*, NUM_ERRS> _strs;
				ErrorStrs();
				GAPR_CORE_DECL const static ErrorStrs _error_strs;
		};

		template<typename T, std::size_t N>
			ErrorCode int_from_hex_fixed(T& outval, const char* s) {
				T val=0;
				std::size_t i=0;
				do {
					auto v=AsciiTables::hex_from_char(s[i]);
					if(v>=16)
						return ERR_CHR_NOT_HEX;
					val|=v;
					if(i>=N-1) {
						outval=val;
						return NO_ERR;
					}
					i++;
					val<<=4;
				} while(true);
			}

		template<typename T, typename Func>
		std::pair<std::size_t, ErrorCode> int_from_dec(T& outval, const char* s, Func func) {
			std::size_t i=0;
			if(!func(i))
				return {i, ERR_EMPTY};
			auto v=AsciiTables::hex_from_char(s[i]);
			bool sgn{false};
			T val;
			if(v>=10) {
				if(s[i]!='-')
					return {i, ERR_CHR_NOT_DEC};
				i++;
				if(!func(i))
					return {i, ERR_EMPTY};
				v=AsciiTables::hex_from_char(s[i]);
				if(v>=10)
					return {i, ERR_CHR_NOT_DEC};
				sgn=true;
			}
			i++;
			val=v;
			while(func(i)) {
				v=AsciiTables::hex_from_char(s[i]);
				//fprintf(stderr, "%lu %hu\n", i, v);
				if(v>=10)
					break;
				// overflow??
				val=val*10+v;
				i++;
			}
			outval=sgn?-val:val;
			return {i, NO_ERR};
		}

		template<typename T, typename Func>
		std::pair<std::size_t, ErrorCode> int_from_b64(T& outval, const char* s, Func func) {
			std::size_t i=0;
			if(!func(i))
				return {i, ERR_EMPTY};
			auto v=AsciiTables::b64_from_char(s[i]);
			T val;
			if(v>=64)
				return {i, ERR_CHR_NOT_B64};
			i++;
			val=v;
			while(func(i)) {
				v=AsciiTables::b64_from_char(s[i]);
				//fprintf(stderr, "%lu %hu\n", i, v);
				if(v>=64)
					break;
				// overflow??
				val=val+(T{v}<<(i*6));
				i++;
			}
			outval=val;
			return {i, NO_ERR};
		}

		template<typename Func>
			inline std::pair<std::size_t, ErrorCode> parse_name(const char* s, Func func) {
				std::size_t i=0;
				if(!func(i))
					return {i, ERR_EMPTY};
				auto v=AsciiTables::type_from_char(s[i]);
				if(v!=TYPE_ALPHA)
					return {i, ERR_NAME_BEGIN_NOT_ALPHA};
				i++;
				bool prev_dot{false};
				while(func(i)) {
					v=AsciiTables::type_from_char(s[i]);
					bool ok{false};
					switch(v) {
						case TYPE_ALPHA:
						case TYPE_NUM:
							ok=true;
							prev_dot=false;
							break;
						case TYPE_DOT:
							if(prev_dot)
								return {i, ERR_NAME_DUP_DOT};
							ok=true;
							prev_dot=true;
					}
					if(!ok)
						break;
					i++;
				}
				if(prev_dot)
					return {i, ERR_NAME_END_WITH_DOT};
				return {i, NO_ERR};
			}
	}

	class ParserRes {
		public:
			explicit ParserRes() noexcept: _ec{Parser_PRIV::NO_ERR} { }
			explicit operator bool() const noexcept {
				return _ec==Parser_PRIV::NO_ERR;
			}
			const char* error() const noexcept {
				return Parser_PRIV::ErrorStrs::get(_ec);
			}

			ParserRes(Parser_PRIV::ErrorCode ec): _ec{ec} { } // XXX priv?
		private:
			Parser_PRIV::ErrorCode _ec;

			template<typename, typename> friend class Parser;
	};

	template<typename T, typename=void>
		class Parser;

	template<typename T>
		class Parser<T, std::enable_if_t<std::is_integral<T>::value>> {
			public:
				explicit Parser(T& v): _v{v} { }

				template<std::size_t N>
					ParserRes from_hex(const char* s) {
						static_assert(N>0, "");
						static_assert(sizeof(T)*8>=N*4, "N too big");
						return Parser_PRIV::int_from_hex_fixed<T, N>(_v, s);
					}

				std::pair<std::size_t, ParserRes> from_dec(const char* s, std::size_t l) {
					auto r=Parser_PRIV::int_from_dec(_v, s, [l](std::size_t i) ->bool { return i<l; });
					// XXX !strto... or *scratch?
					return {r.first, ParserRes{r.second}};
				}
				std::pair<std::size_t, ParserRes> from_b64(const char* s, std::size_t l) {
					auto r=Parser_PRIV::int_from_b64(_v, s, [l](std::size_t i) ->bool { return i<l; });
					return {r.first, ParserRes{r.second}};
				}
			private:
				T& _v;
		};

	template<typename T>
		Parser<T> make_parser(T& v) {
			return Parser<T>{v};
		}

	inline std::pair<std::size_t, ParserRes> parse_name(const char* s, std::size_t l) {
		auto r=Parser_PRIV::parse_name(s, [l](std::size_t i) ->bool { return i<l; });
		return {r.first, ParserRes{r.second}};
	}

	// XXX
	GAPR_CORE_DECL unsigned short parse_port(const char* s, std::size_t n);
	GAPR_CORE_DECL void parse_repo(const std::string& str, std::string& user, std::string& host, unsigned short& port, std::string& group);

	//////////
	//in:hex,b64,dec,name; ct len/0 term/rt len
	//*out:int,array,port...
	//////////
	//parse_hex_int<8>(&x, s);
		//
		//
		//
		//
		//
		//
		//
		//
		//
		//
		//
		//

}

#endif
