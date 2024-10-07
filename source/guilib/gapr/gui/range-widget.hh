/* gapr/gui/range-widget.hh
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
#ifndef _GAPR_INCLUDE_GUI_RANGE_WIDGET_H_
#define _GAPR_INCLUDE_GUI_RANGE_WIDGET_H_

#include "gapr/config.hh"

#include <memory>
#include <array>

#include <QWidget>

namespace gapr {

	class range_widget_slave;

	class range_widget final: public QWidget {
		Q_OBJECT
		public:
			GAPR_GUI_DECL range_widget(QWidget* parent=nullptr);
			~range_widget() override;

			GAPR_GUI_DECL void set_state(const std::array<double, 4>& state);
			double minimum() const noexcept { return _limits[0]; }
			double maximum() const noexcept { return _limits[1]; }
			double lower() const noexcept { return _range[0]; }
			double upper() const noexcept { return _range[1]; }

			Q_SIGNAL void changed(double low, double up);

		private:
			Q_SLOT void on_line0_editingFinished();
			Q_SLOT void on_line1_editingFinished();
			Q_SLOT void on_slider0_valueChanged(int x);
			Q_SLOT void on_slider1_valueChanged(int x);
			Q_SLOT void on_shrink_clicked(bool);
			Q_SLOT void on_reset_clicked(bool);
			Q_SLOT void slave_slider0_changed(int x);
			Q_SLOT void slave_slider1_changed(int x);

			struct PRIV;
			const std::unique_ptr<PRIV> _priv;
			std::array<double, 2> _limits;
			std::array<double, 2> _range;
			std::array<int, 2> _rangei;

			int to_int(double x) const noexcept;
			constexpr double to_float(int x) const noexcept;
			template<int I> bool set_range(double v) noexcept;
			template<int I> bool set_rangei(int v) noexcept;
			bool can_shrink() const noexcept;
			bool can_reset() const noexcept;
			void slider0_changed(int x);
			void slider1_changed(int x);

			friend class range_widget_slave;
	};

	class range_widget_slave final: public QWidget {
		Q_OBJECT
		public:
			GAPR_GUI_DECL range_widget_slave(QWidget* parent=nullptr);
			~range_widget_slave() override;

			GAPR_GUI_DECL void set_master(range_widget* master);

		private:
			Q_SLOT void on_slider0_valueChanged(int x);
			Q_SLOT void on_slider1_valueChanged(int x);

			struct PRIV;
			const std::unique_ptr<PRIV> _priv;
			range_widget::PRIV* _master;
	};

}

#endif
