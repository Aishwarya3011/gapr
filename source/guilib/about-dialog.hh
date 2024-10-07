/* lib/gui/about-dialog.hh
 *
 * Copyright (C) 2017 GOU Lingfeng
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


#ifndef _GAPR_PRIVATE_GUI_ABOUT_DIALOG_HH_
#define _GAPR_PRIVATE_GUI_ABOUT_DIALOG_HH_

#include <memory>

#include <QDialog>

namespace gapr {

	class AboutDialog final: public QDialog {
		Q_OBJECT
		public:
			AboutDialog(QWidget* parent=nullptr);
			~AboutDialog();

			void reset_page();

		private:
			Q_SLOT void on_links_linkActivated(const QString& link);
			Q_SLOT void on_links_2_linkActivated(const QString& link) {
				return on_links_linkActivated(link);
			}
			Q_SLOT void on_links_3_linkActivated(const QString& link) {
				return on_links_linkActivated(link);
			}
			Q_SLOT void on_links_4_linkActivated(const QString& link) {
				return on_links_linkActivated(link);
			}

			struct PRIV;
			std::unique_ptr<PRIV> _priv;

	};

}

#endif
