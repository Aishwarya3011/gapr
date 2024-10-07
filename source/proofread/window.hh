/* fix-lite/window.hh
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


#ifndef _GAPR_PROGRAM_FIX_LITE_WINDOW_
#define _GAPR_PROGRAM_FIX_LITE_WINDOW_

#include <memory>
#include <system_error>

#include <QMainWindow>

namespace gapr::fix {
	struct Position;
}

namespace gapr::proofread {

	class Window final: public QMainWindow {
		Q_OBJECT
		public:
			struct Args {
				//std::string config{};
				std::string user{};
				std::string passwd{};
				std::string host{};
				unsigned short port{0};
				std::string group{};
				bool proofread;
			};

			explicit Window();
			~Window();

			void set_args(Args&& args);

		private:
			Q_SLOT void critical_error_cb(int result);
			Q_SLOT void ask_password_cb(int result);
			Q_SLOT void show_retry_dlg_cb(int result);

			Q_SLOT void on_xfunc_closeup_changed(double low, double up);
			Q_SLOT void on_select_closeup_currentIndexChanged(int index);

			Q_SLOT void on_select_scale_currentIndexChanged(int index);
			Q_SLOT void on_total_slices_valueChanged(int value);
			Q_SLOT void on_shown_slices_valueChanged(int value);
			Q_SLOT void on_quality_button_box_accepted();
			Q_SLOT void on_end_as_dialog_finished(int result);
			Q_SLOT void on_login_dialog_finished(int result);

			Q_SLOT void on_canvas_ready(std::error_code err);
			Q_SLOT void on_canvas_pick_changed();
			Q_SLOT void on_canvas_selection_changed();
			Q_SLOT void on_canvas_customContextMenuRequested(const QPoint& pos);

			Q_SLOT void on_file_open_triggered();
			Q_SLOT void on_file_close_triggered();
			Q_SLOT void on_file_launch_triggered();
			Q_SLOT void on_file_options_triggered();
			Q_SLOT void on_file_quit_triggered();

			Q_SLOT void on_goto_target_triggered();
			Q_SLOT void on_pick_current_triggered();
			Q_SLOT void on_goto_next_node_triggered();
			Q_SLOT void on_goto_next_cube_triggered();
			Q_SLOT void on_neuron_create_triggered();
			Q_SLOT void on_report_error_triggered();
			Q_SLOT void on_reopen_error_triggered();
			Q_SLOT void on_resolve_error_triggered();
			Q_SLOT void error_state_dialog_finished(int result);

			Q_SLOT void on_view_refresh_triggered();
			Q_SLOT void on_view_slice_toggled(bool checked);
			Q_SLOT void on_view_data_only_toggled(bool checked);
			Q_SLOT void on_view_config_triggered();

			Q_SLOT void on_tracing_connect_triggered();
			Q_SLOT void on_tracing_extend_triggered();
			Q_SLOT void on_tracing_branch_triggered();
			Q_SLOT void on_tracing_end_triggered();
			Q_SLOT void on_tracing_end_as_triggered();
			Q_SLOT void on_tracing_delete_triggered();
			Q_SLOT void on_tracing_examine_triggered();

			Q_SLOT void on_help_manual_triggered();
			Q_SLOT void on_help_about_triggered();

			//XXX async cb takes shared_ptr
			struct PRIV;
			std::shared_ptr<PRIV> _priv;

			void critical_error(const QString& err, const QString& info, const QString& detail);
			void warning_msg(const QString& err, const QString& info);
			void ask_password(const QString& str, const QString& err);
			void show_message(const QString& str);
			void show_retry_dlg(const QString& err, const QString& info, const QString& detail);

			void enter_stage0();
			void enter_stage1();
			void enter_stage2();
			void enter_stage3();

			void closeEvent(QCloseEvent* event) override;
			void changeEvent(QEvent* event) override;
			//state???
	};

}

#endif
