/* fix-lite/canvas.hh
 *
 * Copyright (C) 2020 GOU Lingfeng
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

//@@@@
#ifndef _GAPR_PROGRAM_FIX_LITE_CANVAS_
#define _GAPR_PROGRAM_FIX_LITE_CANVAS_

#include "../fix/model-misc.hh"

#include "gapr/cube.hh"

#include <QOpenGLWidget>


namespace gapr::proofread {

	using Position=gapr::fix::Position;

	class Canvas final: public QOpenGLWidget {
		Q_OBJECT
		public:
			Canvas(QWidget* parent=nullptr);
			~Canvas() override;

			void apply_changes();

			void set_closeup_info(const gapr::affine_xform& xform, std::array<unsigned int, 3> sizes, std::array<unsigned int, 3> cube_sizes, std::array<unsigned int, 3> offset);
			void set_closeup_cube(gapr::cube cube, std::string&& uri, std::array<unsigned int, 3> offset);
			void set_closeup_xfunc(std::array<double, 2> xfunc);

			void set_model(const gapr::edge_model& model);
			void set_current(const Position& pos);
			void set_target(const Position& pos);
			void set_path_data(std::vector<edge_model::point>&& points);

			const Position& pick() const noexcept { return _pick_pos; }
			const std::vector<std::pair<gapr::node_id, gapr::node_attr>>& selection() const noexcept { return _nodes_sel; }
			void selection(std::vector<std::pair<gapr::node_id, gapr::node_attr>>&& nodes_sel);
			void clear_selection() noexcept;
			Q_SIGNAL void pick_changed();
			Q_SIGNAL void selection_changed();

			void set_scale_factor(int factor);
			void set_slice_mode(bool enabled);
			void set_slice_pars(std::array<unsigned int, 2> pars);
			void set_closeup_zoom(double zoom);
			void set_directions(const std::array<double, 6>& dirs);
			void set_data_only(bool data_only);

			int scale_factor() const noexcept { return _scale_factor; }
			std::array<unsigned int, 2> slice_pars() const noexcept {
				return _slice_pars;
			}
			double closeup_zoom() const noexcept { return _closeup_zoom; }
			const std::array<double, 6>& directions() const noexcept {
				return _direction;
			}
			//get colors...;

			void model_changed(const std::vector<edge_model::edge_id>& edges_del);

			Q_SIGNAL void ready(std::error_code ec);

		private:
			void initializeGL() override;
			void paintGL() override;
			void resizeGL(int w, int h) override;
			void wheelEvent(QWheelEvent* event) override;
			void mousePressEvent(QMouseEvent* event) override;
			void mouseReleaseEvent(QMouseEvent* event) override;
			void mouseMoveEvent(QMouseEvent* event) override;
			void apply_changes_stage1();
			void edges_removed(const std::vector<edge_model::edge_id>& edges_del);
			Q_SLOT void handle_screen_changed(QScreen* screen);

			struct PRIV;
			const std::unique_ptr<PRIV> _priv;

			gapr::affine_xform _closeup_xform;
			std::array<unsigned int, 3> _closeup_sizes;
			std::array<unsigned int, 3> _closeup_cube_sizes;
			gapr::cube _closeup_cube;
			std::string _closeup_uri;
			std::array<double, 2> _closeup_xfunc;
			std::array<unsigned int, 3> _closeup_offset;

			const gapr::edge_model* _model{nullptr};
			Position _cur_pos, _tgt_pos, _pick_pos;
			std::vector<edge_model::point> _path_data;
			std::vector<std::pair<gapr::node_id, gapr::node_attr>> _nodes_sel;

			int _scale_factor{0};
			bool _slice_mode{false};
			std::array<unsigned int, 2> _slice_pars{480, 480};
			double _closeup_zoom;
			std::array<double, 6> _direction{0, -1.0, 0, 0, 0, -1.0};
			bool _data_only{false};

			enum class chg: unsigned int {
				closeup_info,
				closeup_cube,
				closeup_xfunc,
				model,
				model_update,
				cur_pos,
				tgt_pos,
				view_mode,
				path_data,
				scale_factor,
				slice_mode,
				slice_pars,
				closeup_zoom,
				direction,
				data_only,
				dpr,
				slice_delta,
				sel,
				sel_rect,
			};
			template<chg... c> constexpr unsigned int static
				mask=(0u | ... | (1u<<static_cast<unsigned int>(c)));
			unsigned int _changes0{0};
	};

	inline void Canvas::apply_changes() {
		if(_changes0)
			apply_changes_stage1();
	}
	inline void Canvas::set_closeup_info(const gapr::affine_xform& xform, std::array<unsigned int, 3> sizes, std::array<unsigned int, 3> cube_sizes, std::array<unsigned int, 3> offset) {
		//loc off sizes
		//when apply
		//clearA
		_closeup_xform=xform;
		_closeup_sizes=sizes;
		_closeup_cube_sizes=cube_sizes;
		_changes0|=mask<chg::closeup_info>;
	}
	inline void Canvas::set_closeup_cube(gapr::cube cube, std::string&& uri, std::array<unsigned int, 3> offset) {
		//when cube ready
		//showB
		_closeup_cube=std::move(cube);
		_closeup_uri=std::move(uri);
		_closeup_offset=offset;
		_changes0|=mask<chg::closeup_cube>;
	}
	inline void Canvas::set_closeup_xfunc(std::array<double, 2> xfunc) {
		_closeup_xfunc=xfunc;
		_changes0|=mask<chg::closeup_xfunc>;
	}
	inline void Canvas::set_model(const gapr::edge_model& model) {
		_model=&model;
		_changes0|=mask<chg::model>;
	}
	inline void Canvas::set_current(const Position& pos) {
		_cur_pos=pos;
		_changes0|=mask<chg::cur_pos>;
	}
	inline void Canvas::set_target(const Position& pos) {
		_tgt_pos=pos;
		_changes0|=mask<chg::tgt_pos>;
	}
	inline void Canvas::clear_selection() noexcept {
		if(!_nodes_sel.empty()) {
			_nodes_sel.clear();
			_changes0|=mask<chg::sel>;
		}
	}
	inline void Canvas::selection(std::vector<std::pair<gapr::node_id, gapr::node_attr>>&& nodes_sel) {
		_nodes_sel=std::move(nodes_sel);
		_changes0|=mask<chg::sel>;
	}
	inline void Canvas::set_path_data(std::vector<edge_model::point>&& points) {
		if(points.size()<2)
			points.clear();
		if(_path_data.empty() && points.empty())
			return;
		_path_data=std::move(points);
		_changes0|=mask<chg::path_data>;
	}
	inline void Canvas::set_scale_factor(int factor) {
		if(_scale_factor==factor)
			return;
		_scale_factor=factor;
		_changes0|=mask<chg::scale_factor>;
	}
	inline void Canvas::set_slice_mode(bool enabled) {
		_slice_mode=enabled;
		_changes0|=mask<chg::slice_mode>;
	}
	inline void Canvas::set_slice_pars(std::array<unsigned int, 2> pars) {
		_slice_pars=pars;
		_changes0|=mask<chg::slice_pars>;
	}
	inline void Canvas::set_closeup_zoom(double zoom) {
		_closeup_zoom=zoom;
		_changes0|=mask<chg::closeup_zoom>;
	}
	inline void Canvas::set_directions(const std::array<double, 6>& dirs) {
		_direction=dirs;
		_changes0|=mask<chg::direction>;
	}
	inline void Canvas::set_data_only(bool data_only) {
		_data_only=data_only;
		_changes0|=mask<chg::data_only>;
	}
	inline void Canvas::model_changed(const std::vector<edge_model::edge_id>& edges_del) {
		_changes0|=mask<chg::model_update>;
		if(!edges_del.empty())
			edges_removed(edges_del);
	}

}

#endif
