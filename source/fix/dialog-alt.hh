#ifndef _GAPR_FIX_DIALOGS_ALT_HH_
#define _GAPR_FIX_DIALOGS_ALT_HH_

#include <array>

#include "ui_dlg-error-state.h"

namespace gapr {

	class ErrorStateDialog: public QDialog { Q_OBJECT
		public:
			explicit ErrorStateDialog(QWidget* parent=nullptr): QDialog{parent} {
				_ui.setupUi(this);
			}
			~ErrorStateDialog() { }

			void disable_wontfix() {
				disable_item(3);
			}
			void disable_negative() {
				for(auto i: {0, 1, 4})
					disable_item(i);
			}
			std::string_view state() {
				return _states[_ui.list->currentRow()];
			}

		private:
			gapr::fix::Ui::ErrorStateDialog _ui;
			std::array<std::string_view, 6> _states{
				"", "invalid", "fixed", "wontfix", "redundant", "deferred"
			};

			void disable_item(int row) {
				auto i=_ui.list->item(row);
				auto f=i->flags();
				i->setFlags(f&(~Qt::ItemIsEnabled));
			}

			Q_SLOT void on_button_box_accepted() {
				return accept();
			}
			Q_SLOT void on_button_box_rejected() {
				return reject();
			}
			Q_SLOT void on_list_itemActivated(QListWidgetItem* item) {
				accept();
			}
	};

}

#endif
