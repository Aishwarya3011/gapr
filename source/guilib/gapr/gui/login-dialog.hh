/* gui-lib/login-dialog.hh
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


#ifndef _GAPR_GUI_LOGIN_DIALOG_HH_
#define _GAPR_GUI_LOGIN_DIALOG_HH_

#include "gapr/config.hh"

#include <memory>

#include <QDialog>

class QListWidgetItem;

namespace gapr {

	class login_dialog final: public QDialog {
		Q_OBJECT
		public:
			GAPR_GUI_DECL explicit login_dialog(QWidget* parent=nullptr);
			GAPR_GUI_DECL ~login_dialog();

			GAPR_GUI_DECL void get(std::string& user, std::string& host, unsigned short& port, std::string& group, std::string& passwd);

		private:
			struct PRIV;
			std::unique_ptr<PRIV> _priv;

			Q_SLOT void on_history_currentItemChanged(QListWidgetItem* current, QListWidgetItem*);
			Q_SLOT void on_history_itemActivated(QListWidgetItem* item);
			Q_SLOT void on_button_box_accepted();
			Q_SLOT void on_button_box_rejected();
	};

}

#endif
