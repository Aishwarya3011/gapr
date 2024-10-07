#ifndef _GAPR_FIX_DIALOGS_HH_
#define _GAPR_FIX_DIALOGS_HH_

#include "ui_dlg-node-state.h"
#include "ui_dlg-select-type.h"
#include "ui_dlg-create-neuron.h"
#include "ui_dlg-rename-neuron.h"

#include <cstring>

namespace gapr::fix {

	inline bool validate_neuron_name(std::string_view name) {
		for(auto c: name) {
			if(std::strchr("/<>:\"\\|?*\n\r", c)!=nullptr)
				return false;
		}
		return true;
	}

	class EndAsDialog: public QDialog { Q_OBJECT
		public:
			explicit EndAsDialog(QWidget* parent=nullptr): QDialog{parent} {
				_ui.setupUi(this);
			}
			~EndAsDialog() { }

			std::string state() { return _ui.state->text().toStdString(); }

		private:
			gapr::proofread::Ui::NodeStateDialog _ui;

			Q_SLOT void on_button_box_accepted() {
				return accept();
			}
			Q_SLOT void on_button_box_rejected() {
				return reject();
			}
			Q_SLOT void on_listWidget_currentItemChanged(QListWidgetItem* item, QListWidgetItem*) {
				_ui.state->setText(item->text());
			}
			Q_SLOT void on_listWidget_itemActivated(QListWidgetItem* item) {
				accept();
			}
	};

	class SelectTypeDialog: public QDialog { Q_OBJECT
		public:
			explicit SelectTypeDialog(QWidget* parent=nullptr): QDialog{parent} {
				_ui.setupUi(this);
			}
			~SelectTypeDialog() { }

			int type() { return _ui.type->value(); }

		private:
			gapr::fix::Ui::SelectTypeDialog _ui;

			Q_SLOT void on_button_box_accepted() {
				return accept();
			}
			Q_SLOT void on_button_box_rejected() {
				return reject();
			}
			Q_SLOT void on_listWidget_currentRowChanged(int currentRow) {
				_ui.type->setValue(currentRow);
			}
			Q_SLOT void on_listWidget_itemActivated(QListWidgetItem* item) {
				accept();
			}
	};

	class CreateNeuronDialog: public QDialog { Q_OBJECT
		public:
			explicit CreateNeuronDialog(QWidget* parent=nullptr): QDialog{parent} {
				_ui.setupUi(this);
			}
			~CreateNeuronDialog() { }

			std::string name() { return _ui.name->text().toStdString(); }

		private:
			gapr::fix::Ui::CreateNeuronDialog _ui;
			Q_SLOT void on_button_box_accepted() {
				if(!validate_neuron_name(name()))
					return;
				return accept();
			}
			Q_SLOT void on_button_box_rejected() {
				return reject();
			}
	};

	class RenameNeuronDialog: public QDialog { Q_OBJECT
		public:
			explicit RenameNeuronDialog(QWidget* parent=nullptr): QDialog{parent} {
				_ui.setupUi(this);
			}
			~RenameNeuronDialog() { }

			void name(std::string_view n) {
				_ui.name->setText(QString::fromUtf8(n.data(), n.size()));
			}
			std::string name() { return _ui.name->text().toStdString(); }

		private:
			gapr::fix::Ui::RenameNeuronDialog _ui;
			Q_SLOT void on_button_box_accepted() {
				if(!validate_neuron_name(name()))
					return;
				return accept();
			}
			Q_SLOT void on_button_box_rejected() {
				return reject();
			}
	};

}

#endif
