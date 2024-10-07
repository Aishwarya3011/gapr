/* detail/finally.hh
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


//@@@@@
#ifndef _GAPR_INCLUDE_DETAIL_FINALLY_HH_
#define _GAPR_INCLUDE_DETAIL_FINALLY_HH_

//#include <type_traits>

namespace gapr {

	template<typename Cb> class finally_type {
		public:
			finally_type() =delete;
			explicit finally_type(Cb&& cb) noexcept(noexcept(Cb{std::move(cb)})):
				_cb{std::move(cb)}, _abort{false} { }
			~finally_type() {
				static_assert(noexcept(_cb()));
				if(!_abort)
					_cb();
			}
			finally_type(const finally_type&) =delete;
			finally_type& operator=(const finally_type&) =delete;
			finally_type(finally_type&& r)
				noexcept(noexcept(Cb{std::move(r._cb)})):
					_cb{std::move(r._cb)}, _abort{r._abort} { r._abort=true; }
			finally_type& operator=(finally_type&&) =delete;

			void abort() noexcept { _abort=true; }

		private:
			Cb _cb;
			bool _abort;
	};

	template<typename Cb>
		inline auto make_finally(Cb&& cb) {
			using CbType=std::decay_t<Cb>;
			return finally_type<CbType>(std::move(cb));
		}

}

#endif
