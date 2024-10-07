/* lib/gui/application.cc
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


#include "gapr/gui/application.hh"

#include "about-dialog.hh"

#include "gapr/utility.hh"

#include <string>
#include <fstream>

#include <boost/asio/any_io_executor.hpp>

#include <QtWidgets/QtWidgets>

#include "config.hh"


// XXX todo: preference window, shortcut window, redirect online help

int gapr::Application::_inst_refc{0};
gapr::Application* gapr::Application::_instance{nullptr};

struct FakeArgs {
	int argc{0};
	std::vector<std::string> args{};
	std::vector<char*> argv{};
};
static std::unique_ptr<FakeArgs> fake_args{};

gapr::Application::Application(int& argc, char** argv):
	QApplication{argc, argv},
	_io_ctx{1},
	_ssl_ctx{ssl_context_type::tls},
	_threadp{std::thread::hardware_concurrency()},
	//_file_cache{_io_ctx},
	_translator{new QTranslator{this}}
{
	QString trans_path;
	{
		std::string p;
		auto path=gapr::datadir();
		if(!path.empty()) {
			p=std::string{path}+"/" PACKAGE_TARNAME "/translations";
		} else {
					std::string fmf{path};
					fmf+="/Makefile";
					std::ifstream ifs{fmf};
					std::string line;
					std::string tag{"CMAKE_BINARY_DIR = "};
					while(std::getline(ifs, line)) {
						if(line.size()>=tag.size() && std::equal(tag.begin(), tag.end(), &line[0], &line[tag.size()])) {
							p=line.substr(tag.size());
							p+="/source/gui-lib";
							break;
						}
					}
					// XXX eof or err
					if(p.empty())
						p=path;
		}
		trans_path=QString::fromStdString(p);
		gapr::print("1,: ", p);
	}
	if(_translator->load(QLocale{}, "", "", trans_path))
		QCoreApplication::installTranslator(_translator);

	QCoreApplication::setApplicationName(QStringLiteral(PACKAGE_NAME));
	QCoreApplication::setApplicationVersion(QStringLiteral(PACKAGE_VERSION));
	QCoreApplication::setOrganizationDomain(QStringLiteral(PACKAGE_DOMAIN));
	QCoreApplication::setOrganizationName(QStringLiteral(PACKAGE_ORG));
	QGuiApplication::setApplicationDisplayName(QStringLiteral(PACKAGE_NAME));

	QIcon::setThemeName("Adwaita");
	auto paths=QIcon::themeSearchPaths();
	auto datadir=gapr::datadir();
	paths<<QString::fromUtf8(datadir.data(), datadir.size())+"/icons";
	QIcon::setThemeSearchPaths(paths);
	auto icon=QIcon::fromTheme(QStringLiteral(APPLICATION_ID));
	if(icon.isNull())
		icon=QIcon::fromTheme("app-icon");
	if(icon.isNull()) {
		// XXX more sizes
		//icon.addFile(QStringLiteral(":/gui-lib/images/app-icon-128.png"), QSize{128, 128});
		//icon.addFile(QStringLiteral(":/gui-lib/images/app-icon-32.png"), QSize{32, 32});
	}
	QApplication::setWindowIcon(icon);
}
gapr::Application::~Application() {
	QCoreApplication::removeTranslator(_translator);
	delete _translator;
}

void gapr::Application::create_instance() {
	assert(_instance==nullptr);

	//Q_INIT_RESOURCE(gui);

	fake_args.reset(new FakeArgs{});
	fake_args->args.push_back(PACKAGE_TARNAME);
#ifdef x_WIN64
	fake_args->args.push_back("-style");
	fake_args->args.push_back("fusion");
#endif
	/*
		-style style
		-stylesheet stylesheet
		-widgetcount
		*/
	for(auto& s: fake_args->args)
		fake_args->argv.push_back(&s[0]);
	fake_args->argc=fake_args->argv.size();

	/*! required for Mac. */
	auto fmt=QSurfaceFormat::defaultFormat();
	fmt.setProfile(QSurfaceFormat::CoreProfile);
	fmt.setVersion(3, 3);
	//fmt.setOptions(fmt.options()|QSurfaceFormat::DebugContext);
	//fmt.setSwapInterval(0); // >60fps
	QSurfaceFormat::setDefaultFormat(fmt);

	/*! share gl data */
	QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts, true);

	/*! for high dpi icons */
#ifndef __APPLE__
	QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
#endif
	QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);

	_instance=new Application{fake_args->argc, &fake_args->argv[0]};
}

void gapr::Application::destroy_instance() {
	assert(_instance!=nullptr);
	delete _instance;
}

void gapr::Application::show_start_window(QWidget* src) {
#if 0
	for(auto win: QApplication::topLevelWidgets()) {
		if(auto launch_dlg=dynamic_cast<gapr::start_window*>(win)) {
			if(launch_dlg->fresh()) {
				launch_dlg->show();
				launch_dlg->raise();
				launch_dlg->activateWindow();
				return;
			}
		}
	}
	auto dlg=new gapr::start_window{};
	dlg->show();
#endif
}

void gapr::Application::show_start_window(Program* factory, int argc, char* argv[], QWidget* src) {
#if 0
	auto dlg=new gapr::start_window{*factory, argc, argv};
	dlg->show();
#endif
}

void gapr::Application::show_about_dialog(QWidget& src) {
	for(auto win: QApplication::topLevelWidgets()) {
		if(auto about_dlg=dynamic_cast<gapr::AboutDialog*>(win)) {
			about_dlg->reset_page();
			about_dlg->show();
			about_dlg->raise();
			about_dlg->activateWindow();
			return;
		}
	}
	auto dlg=new gapr::AboutDialog{&src};
	dlg->show();
}

int gapr::Application::exec() {
	_instance->_gui_thread=std::this_thread::get_id();

	boost::asio::any_io_executor wg=boost::asio::require(_instance->_io_ctx.get_executor(), boost::asio::execution::outstanding_work.tracked);
	std::thread thr{[]() ->void {
		gapr::print("begin run");
		_instance->_io_ctx.run();
		gapr::print("end run");
	}};

	auto r=QApplication::exec();

	wg={};
	thr.join();

	return r;
}

bool gapr::Application::notify(QObject* receiver, QEvent* event) {
#if 0
	try {
#endif
		return QApplication::notify(receiver, event);
#if 0
	} catch(const std::exception& e) {
		QMessageBox mbox(QMessageBox::Critical, "Critical Error", QString{"Uncaught exception: "}+=e.what(), QMessageBox::Ok);
		mbox.setDefaultButton(QMessageBox::Ok);
		mbox.exec();
		return true;
	}
#endif
}

constexpr static auto _event_quit_request{static_cast<QEvent::Type>(QEvent::User+1)};

#if 0
	bool sessionRequestClose(Session* s) {
		if(!sessions.contains(s))
			return false;
		if(!closeSession(s))
			return false;
		if(sessions.empty()) {
			if(sessions_not_ready.empty())
				Splash::instance()->show();
		} else {
			//auto w=sessions.front();
			//raise w
		}
		return true;
	}
	bool closeSessionImp() {
		switch(state) {
			case SessionState::Invalid:
				return false;
			case SessionState::LoadingCatalog:
			case SessionState::Computing:
			case SessionState::LoadingCubes:
				showMessage("Close session", "Please cancel running operations first.", session);
				return false;
			case SessionState::Readonly:
			case SessionState::Ready:
				break;
		}

		if(ver!=verFile) {
			QMessageBox mbox(QMessageBox::Question, "Save Session?", "Do you want to save changes to the current session?", QMessageBox::Save|QMessageBox::Cancel|QMessageBox::Discard, session);
			mbox.setDefaultButton(QMessageBox::Save);
			auto res=mbox.exec();
			if(res==QMessageBox::Cancel) {
				return false;
			}
			if(res==QMessageBox::Save) {
				if(!saveSession()) {
					return false;
				}
			}
		}

		setState(SessionState::Invalid, "Ready to be destroyed.", false);

		return true;
	}

#endif

bool gapr::Application::event(QEvent* event) {
	auto type=event->type();
	if(type>=QEvent::User && type<=QEvent::MaxUser) {
		switch(type-QEvent::User) {
			case _event_post_cb-QEvent::User:
				static_cast<PostExecEvent*>(event)->cb_wrapper_call();
				return true;
			case _event_quit_request-QEvent::User:
				// XXX registered windows???
				gapr::print("quit request");
				while(true) {
					auto list=QApplication::topLevelWidgets();
					if(list.empty())
						break;
					bool hit{false};
					for(auto wid: QApplication::topLevelWidgets()) {
						gapr::print("try: ", wid);
						if(auto win=dynamic_cast<QMainWindow*>(wid)) {
							if(!win->close())
								return true;
							delete win;
							hit=true;
							break;
						}
					}
					if(!hit)
						break;
				}

#if 0
				if(CacheThread::instance()->isRunning()) {
					CacheThread::instance()->stop();
					if(!CacheThread::instance()->wait())
						CacheThread::instance()->terminate();
				}
#endif

				QCoreApplication::quit();
				return true;
			default:
				break;
		}
		//!!!int QEvent::registerEventType(int hint = -1)
	}
	return QApplication::event(event);
}

void gapr::Application::request_quit() {
	return postEvent(_instance, new QEvent{_event_quit_request});
}
void gapr::Application::show_options_dialog(QWidget& src) {
}
void gapr::Application::display_help(QWidget& src, const QString& part) {
#if 0
	url.setFragment(part);
	auto pthExec=QCoreApplication::applicationDirPath();
	QDir dirExec{pthExec};
	auto manualFile=dirExec.absoluteFilePath("../" PACKAGE_DATADIR "/doc/" PACKAGE_NAME_LONG_LC "/index.html");
	if(QFile{manualFile}.exists()) {
		if(!QDesktopServices::openUrl(QUrl{manualFile}))
			showWarning("Show manual", "Failed to open local copy of manual.", src);
	} else {
		QMessageBox mbox(QMessageBox::Question, "Open online manual", "Local copy of manual cannot be found. Open the online manual?", QMessageBox::Yes|QMessageBox::No, src);
		mbox.setDefaultButton(QMessageBox::Yes);
		auto res=mbox.exec();
		if(res==QMessageBox::Yes) {
			if(!QDesktopServices::openUrl(QUrl{PACKAGE_URL "manual/"}))
				showWarning("Show manual", "Failed to open online manual.", src);
		}
	}
#endif
}

