/* exception.hh
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
#ifndef _GAPR_INCLUDE_EXCEPTION_HH_
#define _GAPR_INCLUDE_EXCEPTION_HH_

#include <exception>
#include <string>


namespace gapr {

	class race_condition: public std::exception {
		public:
			explicit race_condition() noexcept: std::exception{} { }
			race_condition(const race_condition&) =default;
			race_condition& operator=(const race_condition&) =default;
			~race_condition() override { }
			const char* what() const noexcept override {
				return "race condition";
			}
		private:
	};

	class reported_error: public std::exception {
		public:
			explicit reported_error(const char* what): _what{what} { }
			explicit reported_error(const std::string& what): _what{what} { }
			explicit reported_error(std::string&& what) noexcept:
				_what{std::move(what)} { }
			~reported_error() override { }

			reported_error(const reported_error&) =delete; // XXX
			reported_error& operator=(const reported_error&) =delete;
			reported_error(reported_error&&) noexcept =default;
			reported_error& operator=(reported_error&&) noexcept =default;

			const char* what() const noexcept override { return _what.c_str(); }

		private:
			std::string _what;
	};

	//////////
#if 0
	class reported_error: public std::exception {
		public:
			explicit reported_error(const char* what):
				_stk{what, {}, }, _ec{}, _n{1} { }
			explicit reported_error(const std::string& what):
				_stk{what, {}, }, _ec{}, _n{1} { }
			explicit reported_error(std::string&& what) noexcept:
				_stk{std::move(what), {}, }, _ec{}, _n{1} { }

			explicit reported_error(std::error_code ec):
				_stk{ec.message(), {}, }, _ec{ec}, _n{1} { }
			~reported_error() override { }

			reported_error(const reported_error&) =delete;
			reported_error& operator=(const reported_error&) =delete;
			reported_error(reported_error&&) noexcept =default;
			reported_error& operator=(reported_error&&) noexcept =default;

			reported_error& operator<<(const char* what) {
				_stk[_n]=what;
				return *this;
			}
			reported_error& operator<<(const std::string& what) {
				_stk[_n]=what;
				return *this;
			}
			reported_error& operator<<(std::string&& what) noexcept {
				_stk[_n]=std::move(what);
				return *this;
			}

			const char* what() const noexcept override { return &_stk[0][0]; }
			const std::error_code& code() const noexcept { return _ec; }

	//////
	//kkkkkkkkkkkkkk
		private:
			std::array<std::string, SS> _stk;
			std::error_code _ec;
	};
#endif

}

#endif
