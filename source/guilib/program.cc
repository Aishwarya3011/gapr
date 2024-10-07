/* lib/gui/program.cc
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


#include "gapr/gui/program.hh"

#include "gapr/gui/application.hh"
#include "gapr/exception.hh"

//#include <iostream>
//#include <memory>

#include <QMainWindow>
#include <QMessageBox>


int gapr::GuiProgram::run() {
	gapr::Application::Enabler app_;
	auto& app=gapr::app();

#if 0
	QMessageBox* mbox{nullptr};
	try {
#endif
		//XXX how to delete???
		auto win=window();
		win->show();

#if 0
	} catch(const gapr::reported_error& e) {
		//not necessary
		mbox=new QMessageBox{QMessageBox::Critical, QStringLiteral("Critical Error"), QStringLiteral("Failed to create window."), QMessageBox::Close, nullptr};
		if(e.what())
			mbox->setInformativeText(e.what());
		if(e.code())
			mbox->setDetailedText(QString::fromStdString(e.code().message()));
	} catch(const std::exception& e) {
		// not necessary
		mbox=new QMessageBox{QMessageBox::Critical, QStringLiteral("Internal Error"), QStringLiteral("Failed to create window."), QMessageBox::Close, nullptr};
		mbox->setDetailedText(e.what());
	}
	if(mbox) {
		mbox->setWindowModality(Qt::ApplicationModal);
		QObject::connect(mbox, &QDialog::finished, mbox, &QObject::deleteLater);
		mbox->open();
	}
#endif
	return app.exec();
}


int gapr::gui_launch() {
	gapr::Application::Enabler app_;

	auto& app=gapr::app();
	app.show_start_window();
	return app.exec();
}

int gapr::gui_launch(gapr::Program* factory, int argc, char* argv[]) {
	gapr::Application::Enabler app_;

	auto& app=gapr::app();
	app.show_start_window(factory, argc, argv);
	return app.exec();
}

