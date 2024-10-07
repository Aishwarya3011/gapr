#ifndef _GAPR_INCLUDE_UTILITY_H_
#define _GAPR_INCLUDE_UTILITY_H_

#include "gapr/config.hh"

//#include <string>
#include <cstring>
#include <iostream>
#include <system_error>
#include <sstream>
#include <unordered_map>
#include <filesystem>

#if defined(__LITTLE_ENDIAN__) && !defined(__BIG_ENDIAN__)
#define GAPR_BYTE_ORDER 4321
#elif !defined(__LITTLE_ENDIAN__) && defined(__BIG_ENDIAN__)
#define GAPR_BYTE_ORDER 1234
#elif defined(__x86_64) || defined(__x86_64__)
#define GAPR_BYTE_ORDER 4321
#elif defined(_M_AMD64) || defined(_M_IX86) || defined(_M_X64)
#define GAPR_BYTE_ORDER 4321
#else
#error Failed to detect endianness at compile time.
#endif

namespace std {
	inline std::ostream& operator<<(std::ostream& os, std::pair<const char*, std::size_t> p) {
		return os.write(p.first, p.second);
	}
}

namespace gapr {

	/*! always non-empty */
	GAPR_CORE_DECL std::string_view selfdir();
	/*! non-empty, installed or relocated;
	 * otherwise, maybe in build directory.
	 */
	GAPR_CORE_DECL std::string_view bindir();
	GAPR_CORE_DECL std::string_view datadir();
	GAPR_CORE_DECL std::string_view includedir();
	GAPR_CORE_DECL std::string_view infodir();
	GAPR_CORE_DECL std::string_view libdir();
	GAPR_CORE_DECL std::string_view libexecdir();
	GAPR_CORE_DECL std::string_view localedir();
	GAPR_CORE_DECL std::string_view localstatedir();
	GAPR_CORE_DECL std::string_view mandir();
	GAPR_CORE_DECL std::string_view sbindir();
	GAPR_CORE_DECL std::string_view sharedstatedir();
	GAPR_CORE_DECL std::string_view sysconfdir();

	namespace {

		template <typename T, bool=std::is_floating_point<T>::value, bool=std::is_integral<T>::value, bool=std::is_signed<T>::value, bool=std::is_same<T, std::string>::value>
			struct parseValue;
		template <typename T>
			struct parseValue<T, true, false, true, false> {
				inline T operator()(const char* str, char** endptr) { return strtod(str, endptr); }
			};
		template <typename T>
			struct parseValue<T, false, true, true, false> {
				inline T operator()(const char* str, char** endptr) { return strtol(str, endptr, 10); }
			};
		template <typename T>
			struct parseValue<T, false, true, false, false> {
				inline T operator()(const char* str, char** endptr) { return strtoul(str, endptr, 10); }
			};
		template <typename T>
			struct parseValue<T, false, false, false, true> {
				inline T operator()(const char* str, char** endptr) {
					auto n=strcspn(str, ":");
					*endptr=const_cast<char*>(str+n);
					return T{str, n};
				}
			};

		inline int parse_tuple([[maybe_unused]] const char* str) {
			return 0;
		}

		inline void printToStream([[maybe_unused]] std::ostream& s) { }
		template<typename Arg, typename... Args> inline void printToStream(std::ostream& s, Arg&& a, Args&&... args) {
			return printToStream(s<<std::forward<Arg>(a), std::forward<Args>(args)...);
		}

		template<typename T> class scope_exit_helper {
			T _f;
			public:
			scope_exit_helper(const T& f): _f{f} { }
			~scope_exit_helper() {
				_f();
			}
		};
	}

	const char* _get_thread_color();
	const char* _restore_color();
	GAPR_CORE_DECL void _do_print(const std::string& s);
	template<typename... Args> inline void print(Args&&... args) {
#ifndef GAPR_NO_PRINT
		std::ostringstream oss;
		printToStream(oss, std::forward<Args>(args)...);
		_do_print(oss.str());
#endif
	}

	class Error: public std::exception {
		public:
			enum class Domain {
				None,
				Gapr,
				Sys,
				Uv,
				Ssl,
				Gsl,
				Curl,
				CurlM,
				Zip,
				Gl,
				Qt,
			};

			Error(const char* source, int line, const char* what=nullptr,
					Domain domain=Domain::None, int code=0):
				_source{source}, _line{line}, _domain{domain},
				_code{code}, _what{what}
			{ }
			~Error() {
			}

			const char* what() const noexcept override {
				return _what.c_str();
			}

			const char* source() const noexcept { return _source; }
			int line() const noexcept { return _line; }
			Domain domain() const noexcept { return _domain; }
			int code() const noexcept { return _code; }

		private:
			const char* _source;
			int _line;
			Domain _domain;
			int _code;
			std::string _what;
	};





	inline void report(const std::string& msg) {
		throw std::runtime_error{msg};
	}
	inline void report(const char* msg) {
		throw std::runtime_error{msg};
	}
	template<typename... Args> inline void report(Args&&... args) {
		std::ostringstream oss;
		printToStream(oss, std::forward<Args>(args)...);
		throw std::runtime_error{oss.str()};
	}

#define make_sure(pred) \
	do { \
		if(!(pred)) { \
			gapr::report("\e[31m[" __FILE__ ":", __LINE__, "]\e[0m: " #pred); \
		} \
	} while(0)

#define GAPR_PASTE__(A, B) A ## B
#define GAPR_PASTE(A, B) GAPR_PASTE__(A, B)
#define scope_exit(cap, proc) \
	auto GAPR_PASTE(___scope_exit_temp_, __LINE__) = gapr::scope_exit_func([cap]() { proc; })

	template<typename T> scope_exit_helper<T> scope_exit_func(const T& f) {
		return scope_exit_helper<T>(f);
	}

	inline int parse_tuple(const char* str, std::string* vp) {
		*vp=str;
		return 1;
	}
	inline int parse_tuple(const char* str, std::string_view* vp) {
		*vp=str;
		return 1;
	}
	template<typename T, typename... Args> inline int parse_tuple(const char* str, T* vp, Args*... args) {
		char* endptr;
		T v=parseValue<T>{}(str, &endptr);
		if(endptr==str)
			return 0;
		str=endptr;
		*vp=v;

		if(sizeof...(args)==0) {
			return 1;
		} else {
			if(*str==':') {
				str++;
				return 1+parse_tuple(str, args...);
			} else {
				return 1;
			}
		}
	}

	inline constexpr bool little_endian() {
		return GAPR_BYTE_ORDER==4321;
	}

	template<typename T> inline T swap_bytes(T v);
#ifdef _WIN64
#include <stdlib.h>
	template<> inline uint16_t swap_bytes<uint16_t>(uint16_t v) {
		return _byteswap_ushort(v);
	}
	template<> inline uint32_t swap_bytes<uint32_t>(uint32_t v) {
		return _byteswap_ulong(v);
	}
	template<> inline uint64_t swap_bytes<uint64_t>(uint64_t v) {
		return _byteswap_uint64(v);
	}
#else
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
	template<> inline uint16_t swap_bytes<uint16_t>(uint16_t v) {
		return OSSwapInt16(v);
	}
	template<> inline uint32_t swap_bytes<uint32_t>(uint32_t v) {
		return OSSwapInt32(v);
	}
	template<> inline uint64_t swap_bytes<uint64_t>(uint64_t v) {
		return OSSwapInt64(v);
	}
#else
#include <byteswap.h>
	template<> inline uint16_t swap_bytes<uint16_t>(uint16_t v) {
		return bswap_16(v);
	}
	template<> inline uint32_t swap_bytes<uint32_t>(uint32_t v) {
		return bswap_32(v);
	}
	template<> inline uint64_t swap_bytes<uint64_t>(uint64_t v) {
		return bswap_64(v);
	}
#endif
#endif

	template<typename LE, typename T, typename HOST_LE> struct _swap_bytes_to;
	template<typename T> struct _swap_bytes_to<std::false_type, T, std::false_type> {
		inline T operator()(T v) { return v; }
	};
	template<typename T> struct _swap_bytes_to<std::true_type, T, std::true_type> {
		inline T operator()(T v) { return v; }
	};
	template<typename T> struct _swap_bytes_to<std::false_type, T, std::true_type> {
		inline T operator()(T v) { return swap_bytes(v); }
	};
	template<typename T> struct _swap_bytes_to<std::true_type, T, std::false_type> {
		inline T operator()(T v) { return swap_bytes(v); }
	};

	template<typename LE, typename T, typename HOST_LE=std::conditional<GAPR_BYTE_ORDER==4321, std::true_type, std::false_type>::type> inline T swap_bytes_to(T v) {
		return _swap_bytes_to<LE, T, HOST_LE>{}(v);
	}

	template<typename T> inline T host2le(T v) { return swap_bytes_to<std::true_type>(v); }
	template<typename T> inline T host2be(T v) { return swap_bytes_to<std::false_type>(v); }
	template<typename T> inline T le2host(T v) { return swap_bytes_to<std::true_type>(v); }
	template<typename T> inline T be2host(T v) { return swap_bytes_to<std::false_type>(v); }

	GAPR_CORE_DECL const std::filesystem::path& get_homedir();
	GAPR_CORE_DECL const std::filesystem::path& get_cachedir();
	GAPR_CORE_DECL std::filesystem::path get_cachepath(std::string_view str_tag);
	GAPR_CORE_DECL const std::string& get_moduledir();
	void enable_relocation();
	GAPR_CORE_DECL void rename_with_backup(const char* oldpath, const char* newpath);
	GAPR_CORE_DECL void dump_binary(char* buf, const unsigned char* vals, int n);
	GAPR_CORE_DECL bool parse_binary(unsigned char* vals, const char* buf, int n);
	/*! check for name validity
	 * for command/user/group...
	 * length >=1
	 * allowed chars: '.' [0-9A-Za-z]
	 * first: [A-Za-z]
	 * last: [0-9A-Za-z]
	 * no consecutive '.'
	 */
	GAPR_CORE_DECL const char* check_name(const char* ptr, int len);
	GAPR_CORE_DECL std::string calculate_sha256(std::string_view data);

	GAPR_CORE_DECL std::unordered_map<std::string, std::string> load_config(const char* cfg_file);
	GAPR_CORE_DECL std::unordered_map<std::string, std::string> load_config(const std::filesystem::path& cfg_file);
	GAPR_CORE_DECL bool test_file(char code, const char* fn);

	template<typename... Args>
		std::string _printToString(Args&&... args) {
			std::ostringstream oss;
			printToStream(oss, std::forward<Args>(args)...);
			return oss.str();
		}
	class CliErrorMsg {
		public:
			template<typename... Args>
				CliErrorMsg(Args&&... args):
					_msg{_printToString(std::forward<Args>(args)...)} { }
			CliErrorMsg(const CliErrorMsg&) =delete;
			CliErrorMsg& operator=(const CliErrorMsg&) =delete;
			CliErrorMsg(CliErrorMsg&& r): _msg{std::move(r._msg)} { }

			const std::string& message() const {
				return _msg;
			}
			std::string& message() {
				return _msg;
			}
		private:
			std::string _msg;
	};

	unsigned int nproc() noexcept;
	GAPR_CORE_DECL std::string_view to_string_lex(std::array<char,32>& buf, unsigned long v);

	GAPR_CORE_DECL std::string to_url_if_path(std::string_view path);

	struct cli_helper {
		GAPR_CORE_DECL explicit cli_helper();
		GAPR_CORE_DECL ~cli_helper();
		cli_helper(const cli_helper&) =delete;
		cli_helper& operator=(const cli_helper&) =delete;

		GAPR_CORE_DECL void report_unknown_opt(int argc, char* argv[]);
		GAPR_CORE_DECL void report_missing_arg(int argc, char* argv[]);
		GAPR_CORE_DECL void report_unmatched_opt(int argc, char* argv[]);
	};
}

#if 0
namespace std {
	template<typename T1, typename T2> struct hash<std::pair<T1, T2>> {
		size_t operator()(const std::pair<T1, T2>& s) const { return hash<T1>{}(s.first)^hash<T2>{}(s.second); }
	};
}
#endif

#define gapr_report(msg) \
	do { throw gapr::Error(__FILE__, __LINE__, msg); } while(0)

#define GAPR_REPORT(msg) \
	do { throw gapr::Error(__FILE__, __LINE__, msg); } while(0)

#define gapr_check_uv(code) \
	do { \
		if(code) gapr_report_uv(code); \
	} while(0)
#define gapr_report_uv(code) \
	do { \
		throw gapr::Error(__FILE__, __LINE__, uv_strerror(code), \
				gapr::Error::Domain::Uv, code); \
	} while(0)

#define GAPR_CHECK_GSL(code) \
	do { if(code) GAPR_REPORT_GSL(code); } while(0)
#define GAPR_CHECK_GSL_PTR(ptr) \
	do { if(ptr==nullptr) GAPR_REPORT_GSL(GSL_ENOMEM); } while(0)
#define GAPR_REPORT_GSL(code) \
	do { \
		throw gapr::Error(__FILE__, __LINE__, gsl_strerror(code), \
				gapr::Error::Domain::Gsl, code); \
	} while(0)

#define gapr_report_curl(code) \
	do { \
		throw gapr::Error(__FILE__, __LINE__, curl_easy_strerror(code), \
				gapr::Error::Domain::Curl, code); \
	} while(0)

#define gapr_report_curlm(code) \
	do { \
		throw gapr::Error(__FILE__, __LINE__, curl_multi_strerror(code), \
				gapr::Error::Domain::CurlM, code); \
	} while(0)

#define gapr_report_sys(code) \
	do { \
		throw gapr::Error(__FILE__, __LINE__, strerror(code), \
				gapr::Error::Domain::Sys, code); \
	} while(0)

//
#define GAPR_CHECK_ZIP(err) \
	do { if(zip_error_code_zip(err)) GAPR_REPORT_ZIP(err); } while(0)
#define GAPR_REPORT_ZIP(err) \
	do { \
		throw gapr::Error(__FILE__, __LINE__, zip_error_strerror(err), \
				gapr::Error::Domain::Zip, zip_error_code_zip(err)); \
	} while(0)

#endif

