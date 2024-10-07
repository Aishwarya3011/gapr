/* fix/window.hh
 *
 * Copyright (C) 2018-2021 GOU Lingfeng
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


#ifndef _GAPR_FIX_WINDOW_HH_
#define _GAPR_FIX_WINDOW_HH_

#include <QInputDialog>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressBar>
#include <QSettings>
#include <QFileDialog>

#include "gapr/gui/range-widget.hh"
#include "gapr/gui/dialogs.hh"
#include "gapr/gui/login-dialog.hh"
#include "gapr/gui/application.hh"

#include "session.hh"

#include "canvas.hh"
#include "dialog.hh"
#include "dialog-alt.hh"
#include "neuron-list.hh"

#include "ui_win-main.h"
#include "ui_dlg-channels.h"
#include "ui_dlg-quality.h"
#include "ui_dlg-property.h"


namespace gapr::fix {

	class Window final: public QMainWindow { Q_OBJECT
		public:
			explicit Window(Session* session);
			~Window();

			static QMainWindow* create(std::optional<Session::Args>&& args);

		private:
			class Adapter;
			std::shared_ptr<Session> _session;

			gapr::fix::Ui::Window _ui;

			gapr::fix::Ui::ChannelsDialog _ui_channels;
			QDialog* _dlg_channels{nullptr};

			gapr::fix::Ui::QualityDialog _ui_quality;
			QDialog* _dlg_quality{nullptr};

			gapr::fix::Ui::PropertyDialog _ui_property;
			QDialog* _dlg_property{nullptr};
			Q_SLOT void on_update_details_clicked(bool checked) {
				_ui_property.update_details->setEnabled(false);
				_session->update_details();
			}

			QMenu* _popup_canvas{nullptr};
			Q_SLOT void on_canvas_customContextMenuRequested(const QPoint& pos) {
				// XXX directly connect
				_popup_canvas->popup(_ui.canvas->mapToGlobal(pos));
			}

			QMenu* _popup_list{nullptr};
			Q_SLOT void on_list_view_customContextMenuRequested(const QPoint& pos) {
				// XXX directly connect
				_popup_list->popup(_ui.list_view->mapToGlobal(pos));
			}

			std::unique_ptr<EndAsDialog> _dlg_end_as{};
			std::unique_ptr<CreateNeuronDialog> _dlg_create_neuron{};
			std::unique_ptr<RenameNeuronDialog> _dlg_rename_neuron{};
			std::unique_ptr<QInputDialog> _dlg_position{};
			std::unique_ptr<gapr::PasswordDialog> _dlg_pw{};
			std::unique_ptr<gapr::login_dialog> _dlg_login{};
			std::unique_ptr<SelectTypeDialog> _dlg_type{};
			std::unique_ptr<QFileDialog> _dlg_save{};
			std::unique_ptr<ErrorStateDialog> _dlg_err{};

			gapr::fix::NeuronList* _list_model;
			std::size_t _list_sel{SIZE_MAX};

			QLabel* _ro_indicator;
			QProgressBar* _loading_progr;



			Q_SLOT void on_file_open_triggered();
			Q_SLOT void on_file_close_triggered() { close(); }
			Q_SLOT void on_file_props_triggered() { _dlg_property->show(); }
			Q_SLOT void on_file_launch_triggered() {
				gapr::app().show_start_window(this);
			}
			Q_SLOT void on_file_options_triggered() {
				gapr::app().show_options_dialog(*this);
			}
			Q_SLOT void on_file_quit_triggered() {
				gapr::app().request_quit();
			}


			Q_SLOT void on_goto_target_triggered() { _session->goto_target(); }
			Q_SLOT void on_pick_current_triggered() { }

			Q_SLOT void on_goto_position_triggered() {
				auto dlg=std::make_unique<QInputDialog>(this);
				dlg->setInputMode(QInputDialog::TextInput);
				dlg->setLabelText(QStringLiteral("Eg. @NODE_ID or (X,Y,Z)"));
				dlg->setWindowModality(Qt::WindowModal);
				connect(dlg.get(), &QDialog::finished, this, &Window::position_dialog_finished);
				dlg->show();
				_dlg_position=std::move(dlg);
			}
			Q_SLOT void position_dialog_finished(int result) {
				auto dlg=_dlg_position.release();
				dlg->deleteLater();
				if(result!=QDialog::Accepted)
					return;
				_session->goto_position(dlg->textValue().trimmed().toStdString());
			}

			Q_SLOT void on_next_error_triggered() {
				_session->goto_next_error();
			}
			Q_SLOT void on_clear_state_triggered() {
				_session->clear_end_state();
			}
			Q_SLOT void on_resolve_error_triggered() {
				auto dlg=std::make_unique<ErrorStateDialog>(this);
				dlg->setWindowModality(Qt::WindowModal);
				connect(dlg.get(), &QDialog::finished, this, &Window::error_state_dialog_finished);
				dlg->show();
				_dlg_err=std::move(dlg);
			}
			Q_SLOT void error_state_dialog_finished(int result) {
				auto dlg=_dlg_err.release();
				dlg->deleteLater();
				if(result!=QDialog::Accepted)
					return;
				_session->resolve_error(dlg->state());
			}
			Q_SLOT void on_report_error_triggered() {
				auto dlg=std::make_unique<ErrorStateDialog>(this);
				dlg->setWindowModality(Qt::WindowModal);
				dlg->disable_negative();
				connect(dlg.get(), &QDialog::finished, this, &Window::error_state_dialog_rep_finished);
				dlg->show();
				_dlg_err=std::move(dlg);
			}
			Q_SLOT void error_state_dialog_rep_finished(int result) {
				auto dlg=_dlg_err.release();
				dlg->deleteLater();
				if(result!=QDialog::Accepted)
					return;
				_session->report_error(dlg->state());
			}
			Q_SLOT void on_raise_node_triggered() {
				_session->raise_node();
			}

			Q_SLOT void on_select_noise_triggered() {
				auto dlg=std::make_unique<QInputDialog>(this);
				dlg->setInputMode(QInputDialog::TextInput);
				dlg->setLabelText(QStringLiteral("Format: MAX_NUM_NODES MAX_LENGTH"));
				dlg->setWindowModality(Qt::WindowModal);
				connect(dlg.get(), &QDialog::finished, this, &Window::noise_spec_dialog_finished);
				dlg->show();
				_dlg_position=std::move(dlg);
			}
			Q_SLOT void noise_spec_dialog_finished(int result) {
				auto dlg=_dlg_position.release();
				dlg->deleteLater();
				if(result!=QDialog::Accepted)
					return;
				_session->select_noise(dlg->textValue().trimmed().toStdString());
			}
			Q_SLOT void on_autosel_length_triggered() {
				auto dlg=std::make_unique<QInputDialog>(this);
				dlg->setInputMode(QInputDialog::DoubleInput);
				dlg->setDoubleMaximum(100);
				dlg->setDoubleMinimum(0);
				dlg->setLabelText(QStringLiteral("Length (um) for automatic selection"));
				dlg->setWindowModality(Qt::WindowModal);
				connect(dlg.get(), &QDialog::finished, this, &Window::autosel_length_dialog_finished);
				dlg->show();
				_dlg_position=std::move(dlg);
			}
			Q_SLOT void autosel_length_dialog_finished(int result) {
				auto dlg=_dlg_position.release();
				dlg->deleteLater();
				if(result!=QDialog::Accepted)
					return;
				auto len=dlg->doubleValue();

				QSettings settings{this};
				settings.beginGroup(QStringLiteral("fix"));
				settings.setValue(QStringLiteral("autosel-length"), len);
				settings.endGroup();
				_session->set_autosel_len(len);
			}

			Q_SLOT void on_neuron_create_triggered() {
				auto dlg=std::make_unique<CreateNeuronDialog>(this);
				dlg->setWindowModality(Qt::WindowModal);
				connect(dlg.get(), &QDialog::finished, this, &Window::create_neuron_dialog_finished);
				dlg->show();
				_dlg_create_neuron=std::move(dlg);
			}
			Q_SLOT void create_neuron_dialog_finished(int result) {
				auto dlg=_dlg_create_neuron.release();
				dlg->deleteLater();
				if(result!=QDialog::Accepted)
					return;
				_session->create_neuron(dlg->name());
			}

			Q_SLOT void on_neuron_rename_triggered() {
				auto dlg=std::make_unique<RenameNeuronDialog>(this);
				dlg->setWindowModality(Qt::WindowModal);
				connect(dlg.get(), &QDialog::finished, this, &Window::rename_neuron_dialog_finished);
				auto prev_name=_list_model->get_name(_list_sel);
				dlg->name(prev_name);
				dlg->show();
				_dlg_rename_neuron=std::move(dlg);
			}
			Q_SLOT void rename_neuron_dialog_finished(int result) {
				auto dlg=_dlg_rename_neuron.release();
				dlg->deleteLater();
				if(result!=QDialog::Accepted)
					return;
				dlg->name();
				auto name=dlg->name();
				auto prev_name=_list_model->get_name(_list_sel);
				if(prev_name==name)
					return;
				_session->rename_neuron(std::move(name));
			}

			Q_SLOT void on_neuron_remove_triggered() {
				_session->remove_neuron();
			}


			Q_SLOT void on_view_global_toggled(bool checked) {
				if(checked)
					_session->activate_global_view();
			}
			Q_SLOT void on_view_closeup_toggled(bool checked) {
				if(checked)
					_session->activate_closeup_view();
			}
			Q_SLOT void on_view_mixed_toggled(bool checked) {
				if(checked)
					_session->activate_closeup_view();
			}
			Q_SLOT void on_view_slice_toggled(bool checked) {
				_session->toggle_slice(checked);
			}
			Q_SLOT void on_view_data_only_toggled(bool checked) {
				_session->toggle_data_only(checked);
			}
			Q_SLOT void on_view_refresh_triggered() {
				_session->view_refresh();
			}
			Q_SLOT void on_view_hl_loop_triggered() {
				_session->highlight_loop();
			}
			Q_SLOT void on_view_hl_upstream_triggered() {
				_session->highlight_neuron(-1);
			}
			Q_SLOT void on_view_hl_downstream_triggered() {
				_session->highlight_neuron(1);
			}
			Q_SLOT void on_view_hl_neuron_triggered() {
				_session->highlight_neuron(0);
			}
			Q_SLOT void on_view_hl_raised_triggered() {
				_session->highlight_raised();
			}
			Q_SLOT void on_view_hl_orphan_triggered() {
				_session->highlight_orphan();
			}
			Q_SLOT void on_view_hl_reset_triggered() {
				_session->highlight_reset();
			}


			Q_SLOT void on_view_channels_triggered() {
				// XXX direct connect
				_dlg_channels->show();
			}
			Q_SLOT void on_xfunc_global_changed(double low, double up) {
				_session->xfunc_global_changed(low, up);
			}
			Q_SLOT void on_xfunc_closeup_changed(double low, double up) {
				_session->xfunc_closeup_changed(low, up);
			}
			Q_SLOT void on_select_global_currentIndexChanged(int index) {
				auto ch=_ui_channels.select_global->itemData(index).value<unsigned int>();
				_session->select_global_changed(ch);
			}
			Q_SLOT void on_select_closeup_currentIndexChanged(int index) {
				auto ch=_ui_channels.select_closeup->itemData(index).value<unsigned int>();
				_session->select_closeup_changed(ch);
			}


			Q_SLOT void on_view_quality_triggered() { _dlg_quality->show(); }
			Q_SLOT void on_select_scale_currentIndexChanged(int index) {
				auto f=_ui_quality.select_scale->itemData(index).value<int>();
				_session->change_scale(f);
			}
			Q_SLOT void on_total_slices_valueChanged(int value) {
				_session->change_total_slices(value);
			}
			Q_SLOT void on_shown_slices_valueChanged(int value) {
				_session->change_shown_slices(value);
			}
			Q_SLOT void on_quality_button_box_rejected() { _dlg_quality->hide(); }
			Q_SLOT void on_quality_button_box_accepted() {
				QSettings settings{this};
				settings.beginGroup(QStringLiteral("fix"));
				settings.setValue(QStringLiteral("scale-factor"), _ui_quality.select_scale->currentData());
				auto x=_ui_quality.shown_slices->value();
				auto y=_ui_quality.total_slices->value();
				settings.setValue(QStringLiteral("slice-params"), QPoint{x, y});
				settings.endGroup();
				_dlg_quality->hide();
			}


			Q_SLOT void on_tracing_connect_triggered() {
				_session->tracing_connect();
			}
			Q_SLOT void on_tracing_extend_triggered() {
				_session->tracing_extend();
			}
			Q_SLOT void on_tracing_branch_triggered() {
				_session->tracing_branch();
			}

			Q_SLOT void on_tracing_attach_triggered() {
				auto dlg=std::make_unique<SelectTypeDialog>(this);
				dlg->setWindowModality(Qt::WindowModal);
				connect(dlg.get(), &QDialog::finished, this, &Window::tracing_attach_dialog_finished);
				dlg->show();
				_dlg_type=std::move(dlg);
			}
			Q_SLOT void tracing_attach_dialog_finished(int result) {
				auto dlg=_dlg_type.release();
				dlg->deleteLater();
				if(result!=QDialog::Accepted)
					return;
				_session->tracing_attach(dlg->type());
			}

			Q_SLOT void on_tracing_end_triggered() {
				_session->tracing_end();
			}

			Q_SLOT void on_tracing_end_as_triggered() {
				auto dlg=std::make_unique<EndAsDialog>(this);
				dlg->setWindowModality(Qt::WindowModal);
				connect(dlg.get(), &QDialog::finished, this, &Window::end_as_dialog_finished);
				dlg->show();
				_dlg_end_as=std::move(dlg);
			}
			Q_SLOT void end_as_dialog_finished(int result) {
				auto dlg=_dlg_end_as.release();
				dlg->deleteLater();
				if(result!=QDialog::Accepted)
					return;
				_session->tracing_end_as(dlg->state());
			}

			Q_SLOT void on_tracing_delete_triggered() {
				_session->tracing_delete();
			}
			Q_SLOT void on_tracing_examine_triggered() {
				_session->tracing_examine();
			}

			Q_SLOT void on_tools_save_img_triggered() {
				auto dlg=std::make_unique<QFileDialog>(this, "Please choose a NRRD file to save.", "", "");
				dlg->setWindowModality(Qt::WindowModal);
				dlg->setAcceptMode(QFileDialog::AcceptSave);
				dlg->setDefaultSuffix("nrrd");
				dlg->setFileMode(QFileDialog::AnyFile);
				dlg->setNameFilter("NRRD file (*.nrrd)");
				connect(dlg.get(), &QDialog::finished, this, &Window::save_img_dialog_finished);
				dlg->show();
				_dlg_save=std::move(dlg);
			}
			Q_SLOT void save_img_dialog_finished(int result) {
				auto dlg=_dlg_save.release();
				dlg->deleteLater();
				if(result!=QDialog::Accepted)
					return;
				_session->save_img(dlg->selectedFiles()[0].toStdString());
			}

			Q_SLOT void on_help_manual_triggered() {
				gapr::app().display_help(*this, "fix");
			}
			Q_SLOT void on_help_about_triggered() {
				gapr::app().show_about_dialog(*this);
			}



			void critical_error(const QString& err, const QString& info, const QString& detail);
			void show_message(const QString& str) {
				_ui.statusbar->showMessage(str, 2000);
			}

			void show_retry_dlg(const QString& err, const QString& info, const QString& detail) {
				auto mbox=new QMessageBox{QMessageBox::Warning, QStringLiteral("Error"), err, QMessageBox::Close|QMessageBox::Retry, this};
				if(!info.isEmpty())
					mbox->setInformativeText(info);
				if(!detail.isEmpty())
					mbox->setDetailedText(detail);
				mbox->setWindowModality(Qt::WindowModal);
				connect(mbox, &QDialog::finished, this, &Window::show_retry_dlg_cb);
				mbox->open();
			}
			Q_SLOT void show_retry_dlg_cb(int result);

			void ask_password(const QString& str, const QString& err) {
				auto dlg=std::make_unique<gapr::PasswordDialog>(err.isEmpty()?QStringLiteral("Authentication Required"):QStringLiteral("Login Error"), str, err, this);
				dlg->setWindowModality(Qt::WindowModal);
				connect(dlg.get(), &QDialog::finished, this, &Window::ask_password_cb);
				dlg->show();
				_dlg_pw=std::move(dlg);
			}
			Q_SLOT void ask_password_cb(int result) {
				auto dlg=_dlg_pw.release();
				dlg->deleteLater();
				if(result!=QDialog::Accepted)
					_session->got_passwd({});
				_session->got_passwd(dlg->get_password().toStdString());
			}

			void display_login() {
				auto dlg=std::make_unique<gapr::login_dialog>(this);
				dlg->setWindowModality(Qt::WindowModal);
				QObject::connect(dlg.get(), &QDialog::finished, this, &Window::login_dialog_finished);
				dlg->show();
				_dlg_login=std::move(dlg);
			}
			Q_SLOT void login_dialog_finished(int result);


			void init_list() {
				_list_model=new NeuronList{this};
				_ui.list_view->setModel(_list_model);
				_ui.list_view->setEnabled(true);
				QObject::connect(_ui.list_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &Window::list_view_selectionChanged);
			}
			void list_select(gapr::node_id root) {
				std::size_t idx;
				unsigned int hit{0};
				std::size_t n=_list_model->rowCount(QModelIndex{});
				for(std::size_t i=0; i<n; i++) {
					if(root==_list_model->get_node(i)) {
						idx=i;
						++hit;
					}
				}
				if(hit==1) {
					auto ii=_list_model->index(idx, 0, {});
					_ui.list_view->selectionModel()->setCurrentIndex(ii, QItemSelectionModel::Clear|QItemSelectionModel::SelectCurrent|QItemSelectionModel::Rows);
				}
			}
			Q_SLOT void list_view_selectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
				std::size_t sel{SIZE_MAX};
				do {
					for(auto& index: selected.indexes()) {
						sel=index.row();
						break;
					}
					if(sel!=SIZE_MAX) {
						_list_sel=sel;
						_session->select_node(_list_model->get_node(sel));
						break;
					}
					sel=_list_sel;
					if(sel==SIZE_MAX)
						break;
					for(auto& index: deselected.indexes()) {
						if(sel==static_cast<std::size_t>(index.row())) {
							sel=SIZE_MAX;
							break;
						}
					}
					if(sel!=SIZE_MAX)
						break;
					_list_sel=SIZE_MAX;
					_session->select_node(gapr::node_id{});
				} while(false);
			}

			void enter_stage0();
			void enter_stage1();
			void enter_stage2();
			void enter_stage3();

			void closeEvent(QCloseEvent* event) override;
			void changeEvent(QEvent* event) override {
				switch(event->type()) {
					case QEvent::LanguageChange:
						_ui_channels.retranslateUi(_dlg_channels);
						_ui.retranslateUi(this);
						break;
					default:
						break;
				}
				QMainWindow::changeEvent(event);
			}
			
	};

}

#endif
