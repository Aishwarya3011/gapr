/* gapr/likely.hh
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
#ifndef _GAPR_INCLUDE_LIKELY_HH_
#define _GAPR_INCLUDE_LIKELY_HH_

#include "gapr/config.hh"

#include <system_error>
#include <cassert>

namespace gapr {

	template<typename T>
		class likely;
	template<typename T>
		class likely_noexcept;

	class likely_PRIV {
		struct dumb_cat: std::error_category {
			const char* name() const noexcept override { return nullptr; }
			std::string message(int) const override { return {}; }
		};
		GAPR_CORE_DECL const static dumb_cat ecat_eptr;
		friend class error_mix;
		template<typename> friend class likely;
	};

	class error_mix {
		public:
			error_mix() =delete;
			error_mix(std::error_code ecode) noexcept:
				_ecat{&ecode.category()}, _eval{ecode.value()} { }
			error_mix(std::exception_ptr eptr) noexcept:
				_ecat{&likely_PRIV::ecat_eptr}, _eptr{std::move(eptr)} { }
			~error_mix() {
				if(_ecat==&likely_PRIV::ecat_eptr)
					_eptr.~exception_ptr();
			}
			error_mix(const error_mix&) =delete;
			error_mix& operator=(const error_mix&) =delete;
			error_mix(error_mix&& r) noexcept: _ecat{r._ecat} {
				if(_ecat!=&likely_PRIV::ecat_eptr)
					_eval=r._eval;
				else
					new(&_eptr) std::exception_ptr{std::move(r._eptr)};
			}
			error_mix& operator=(error_mix&& r) noexcept {
				std::swap(_ecat, r._ecat);
				if(_ecat!=&likely_PRIV::ecat_eptr)
					std::swap(_eval, r._eval);
				else
					std::swap(_eptr, r._eptr);
				return *this;
			}

			operator bool() const noexcept {
				if(_ecat!=&likely_PRIV::ecat_eptr)
					return _eval;
				return _eptr?true:false;
			}
			std::error_code code() const {
				if(_ecat!=&likely_PRIV::ecat_eptr)
					return {_eval, *_ecat};
				std::rethrow_exception(_eptr);
			}

		private:
			const std::error_category* _ecat;
			union {
				std::exception_ptr _eptr;
				int _eval;
			};
			template<typename> friend class likely;
	};

	template<typename T> class likely_noexcept {
		public:
			likely_noexcept() =delete;
			template<typename... Args>
				constexpr likely_noexcept(Args&&... args) noexcept:
				_ecat{nullptr}, _val{std::forward<Args>(args)...} {
					static_assert(noexcept(T{std::forward<Args>(args)...}));
				}
			likely_noexcept(std::error_code code) noexcept:
				_ecat{&code.category()}, _eval{code.value()} { }
			~likely_noexcept() {
				if(_ecat==nullptr)
					_val.~T();
			}
			likely_noexcept(const likely_noexcept&) =delete;
			likely_noexcept& operator=(const likely_noexcept&) =delete;
			constexpr likely_noexcept(likely_noexcept&& r) noexcept:
				_ecat{r._ecat} {
					static_assert(noexcept(T{std::move(r._val)}));
					if(_ecat==nullptr)
						new(&_val) T{std::move(r._val)};
					else
						_eval=r._eval;
				}
#if 0
			likely_noexcept& operator=(likely_noexcept&& r) cno {
			}
#endif

			constexpr explicit operator bool() const noexcept {
				return _ecat==nullptr;
			}
			constexpr auto& get() noexcept {
				assert(_ecat==nullptr);
				return _val;
			}
			constexpr const auto& get() const noexcept {
				return const_cast<likely_noexcept*>(this)->get();
			}

			std::error_code error() const noexcept {
				assert(_ecat!=nullptr);
				return {_eval, *_ecat};
			}

		private:
			const std::error_category* _ecat;
			union {
				T _val;
				int _eval;
			};
	};

	template<typename T> class likely {
		public:
			likely() =delete;
			template<typename... Args>
				constexpr likely(Args&&... args)
				noexcept(noexcept(T{std::forward<Args>(args)...})):
					_ecat{nullptr}, _val{std::forward<Args>(args)...} { }
			likely(std::error_code ecode) noexcept:
				_ecat{&ecode.category()}, _eval{ecode.value()} { }
			likely(std::exception_ptr eptr) noexcept:
				_ecat{&likely_PRIV::ecat_eptr}, _eptr{std::move(eptr)} { }
			constexpr likely(error_mix&& r) noexcept: _ecat{r._ecat} {
				if(_ecat!=&likely_PRIV::ecat_eptr)
					_eval=r._eval;
				else
					new(&_eptr) std::exception_ptr{std::move(r._eptr)};
			}
			~likely() {
				if(_ecat==nullptr)
					_val.~T();
				else if(_ecat==&likely_PRIV::ecat_eptr)
					_eptr.~exception_ptr();
			}
			likely(const likely&) =delete;
			likely& operator=(const likely&) =delete;
			constexpr likely(likely&& r) noexcept(noexcept(T{std::move(r._val)})):
				_ecat{r._ecat} {
					if(_ecat==nullptr)
						new(&_val) T{std::move(r._val)};
					else if(_ecat!=&likely_PRIV::ecat_eptr)
						_eval=r._eval;
					else
						new(&_eptr) std::exception_ptr{std::move(r._eptr)};
				}
#if 0
			likely& operator=(likely&& r) cno {
			}
#endif

			constexpr explicit operator bool() const noexcept {
				return _ecat==nullptr;
			}
			constexpr auto& get() noexcept {
				assert(_ecat==nullptr);
				return _val;
			}
			constexpr const auto& get() const noexcept {
				return const_cast<likely*>(this)->get();
			}

			constexpr auto& value() {
				if(_ecat==nullptr)
					return _val;
				if(_ecat!=&likely_PRIV::ecat_eptr)
					throw std::system_error{_eval, *_ecat};
				std::rethrow_exception(_eptr);
			}
			constexpr const auto& value() const {
				return const_cast<likely*>(this)->value();
			}

			std::error_code error_code() const {
				assert(_ecat!=nullptr);
				if(_ecat!=&likely_PRIV::ecat_eptr)
					return {_eval, *_ecat};
				std::rethrow_exception(_eptr);
			}

			error_mix error() && noexcept {
				assert(_ecat!=nullptr);
				if(_ecat!=&likely_PRIV::ecat_eptr)
					return std::error_code{_eval, *_ecat};
				return std::move(_eptr);
			}

		private:
			const std::error_category* _ecat;
			union {
				T _val;
				int _eval;
				std::exception_ptr _eptr;
			};
	};

}

#endif
