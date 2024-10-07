/* gapr/swc-helper.hh
 *
 * Copyright (C) 2019,2020 GOU Lingfeng
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
#ifndef _GAPR_INCLUDE_SWC_HELPER_HH_
#define _GAPR_INCLUDE_SWC_HELPER_HH_

#include "gapr/config.hh"
#include "gapr/model.hh"
#include "gapr/vec3.hh"

#include <unordered_set>
#include <ostream>
#include <istream>
#include <cassert>


namespace gapr {

	struct swc_node {
		int64_t id;
		int type;
		gapr::vec3<double> pos;
		double radius;
		union {
			/*! SWC file uses par_id.
			 * In a vector, par_idx is a more efficient replacement.
			 */
			int64_t par_id;
			std::size_t par_idx;
		};
		static constexpr std::size_t par_idx_null{SIZE_MAX};
		static constexpr int64_t par_id_null{-1};
	};

	class GAPR_CORE_DECL swc_output {
		public:
			explicit swc_output(std::ostream& base):
				_base{base}, _ids{} { }
			~swc_output() { }
			swc_output(const swc_output&) =delete;
			swc_output& operator=(const swc_output&) =delete;

			void header();
			void comment(const char* str);
			void node(const swc_node& n);
			void misc_attr(int64_t id, gapr::misc_attr attr);
			void annot(int64_t id, const char* keyval);
			void loop(int64_t id, int64_t id2);

			void node(gapr::node_id id, gapr::node_id par, gapr::node_attr attr);
			void misc_attr(gapr::node_id id, gapr::misc_attr attr);
			void annot(gapr::node_id id, const char* key, const char* val);
			void loop(gapr::node_id id, gapr::node_id id2);

		private:
			std::ostream& _base;
			std::unordered_set<int64_t> _ids;
	};

	class GAPR_CORE_DECL swc_input {
		public:
			explicit swc_input(std::istream& base):
				_base{base} { }
			~swc_input() { }
			swc_input(const swc_input&) =delete;
			swc_input& operator=(const swc_input&) =delete;

			enum class tags {
				comment=1,
				node,
				misc_attr,
				annot,
				loop,
			};
			bool read();
			tags tag() const noexcept { return _tag; }
			bool eof() const noexcept { return _buf_st==_buf_eof; }

			int64_t id() const noexcept {
				assert(_tag>=tags::node);
				return _cur_node.id;
			}
			gapr::misc_attr misc_attr() const noexcept {
				assert(_tag==tags::node || _tag==tags::misc_attr);
				return _cur_attr;
			}
			std::string_view annot() const noexcept {
				assert(_tag==tags::annot);
				return {&_buf[_annot_skip], _buf.size()-_annot_skip};
			}
			std::string_view annot_key() const noexcept {
				assert(_tag==tags::annot);
				return {&_buf[_annot_skip], _annot_keyl};
			}
			std::string_view annot_val() const noexcept {
				assert(_tag==tags::annot);
				return {&_buf[_annot_skip2], _buf.size()-_annot_skip2};
			}
			int64_t loop() const noexcept {
				assert(_tag==tags::loop);
				return _cur_node.par_id;
			}
			std::string_view comment() const noexcept {
				assert(_tag==tags::comment);
				return _buf;
			}
			const swc_node& node() const noexcept {
				assert(_tag==tags::node);
				return _cur_node;
			}

		private:
			std::istream& _base;
			tags _tag{_tag_empty};
			static constexpr tags _tag_empty{0};
			int _buf_st{_buf_empty};
			static constexpr int _buf_empty{0};
			static constexpr int _buf_avail{1};
			static constexpr int _buf_bad{2};
			static constexpr int _buf_eof{3};
			std::string _buf;
			int64_t _prev_id{-1};
			std::unordered_set<int64_t> _ids;
			gapr::misc_attr _cur_attr;
			std::size_t _annot_skip, _annot_keyl, _annot_skip2;
			swc_node _cur_node;
			std::size_t _line_no{0};
	};

}

#endif
