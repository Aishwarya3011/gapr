/* lib/gui/start-window.hh
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

//@@@@
#ifndef _GAPR_PRIVATE_GUI_START_WINDOW_
#define _GAPR_PRIVATE_GUI_START_WINDOW_

#include <memory>

#include <QMainWindow>

namespace gapr {

	class start_window final: public QMainWindow {
		Q_OBJECT
		public:
			explicit start_window();
			~start_window();

		private:
			Q_SLOT void on_list_view_activated(const QModelIndex& index);

			Q_SLOT void on_file_run_triggered();
			Q_SLOT void on_file_options_triggered();
			Q_SLOT void on_file_quit_triggered();
			Q_SLOT void on_help_manual_triggered();
			Q_SLOT void on_help_about_triggered();

			struct PRIV;
			std::unique_ptr<PRIV> _priv;
	};

}

#endif
