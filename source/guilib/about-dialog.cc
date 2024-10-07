#include "about-dialog.hh"
#include "ui_dlg-about.h"

//#include <string>
//#include <memory>
//#include <unordered_map>

#include <QtGui/QtGui>

#include "config.hh"




struct gapr::AboutDialog::PRIV {
	Ui::about_dialog ui;
	
	bool _sysinfo_ok{false};
	void setup_sysinfo(QWidget* wid) {
		if(_sysinfo_ok)
			return;
		auto w=wid->windowHandle();
		auto c=QOpenGLContext::globalShareContext();
		c->makeCurrent(w);
		auto funcs=c->functions();
		QString glVen{reinterpret_cast<const char*>(funcs->glGetString(GL_VENDOR))};
		QString glVer{reinterpret_cast<const char*>(funcs->glGetString(GL_VERSION))};
		QString glRen{reinterpret_cast<const char*>(funcs->glGetString(GL_RENDERER))};
		c->doneCurrent();
		QString info;
		QTextStream str{&info};
		str<<"CPU: "<<QSysInfo::currentCpuArchitecture()<<'\n';
		str<<"Kernel: "<<QSysInfo::kernelType()<<' '<<QSysInfo::kernelVersion()<<'\n';
		str<<"Device: "<<QSysInfo::productType()<<' '<<QSysInfo::productVersion()<<'\n';
		str<<"Qt: "<<qVersion()<<" (" QT_VERSION_STR ")\n";
		str<<"OpenGL Vendor: "<<glVen<<'\n';
		str<<"OpenGL Version: "<<glVer<<'\n';
		str<<"OpenGL Renderer: "<<glRen<<'\n';
		ui.text_sysinfo->setText(info);
		_sysinfo_ok=true;
	}
};

gapr::AboutDialog::AboutDialog(QWidget* parent):
	QDialog{parent},
	_priv{std::make_unique<PRIV>()}
{
	_priv->ui.setupUi(this);

	auto icon=QApplication::windowIcon();
	_priv->ui.app_icon->setPixmap(icon.pixmap(QSize{128, 128}));

	std::vector<std::pair<QString, QString>> links_info={
		{AboutDialog::tr("Credits"), QLatin1String{"about:credits"}},
		{AboutDialog::tr("Licenses"), QLatin1String{"about:licenses"}},
		{AboutDialog::tr("System Info"), QLatin1String{"about:system"}},
		{AboutDialog::tr("Homepage"), QLatin1String{PACKAGE_URL}},
	};
	QStringList links{};
	for(auto& link: links_info) {
		QLatin1String tag;
		if(!link.second.startsWith(QLatin1String{"about:"}))
			tag=QLatin1String{" <img src=':/images/ext-link.svg' width='8' height='8'>"};
		links<<QStringLiteral("<a href='%2'><span>%1</span></a>%3").arg(link.first, link.second).arg(tag);
	}

	_priv->ui.app_name->setText(AboutDialog::tr(PACKAGE_NAME));
	_priv->ui.app_version->setText(QStringLiteral(PACKAGE_VERSION));
	_priv->ui.app_comment->setText(AboutDialog::tr(PACKAGE_DESCRIPTION));
	_priv->ui.app_copyright->setText(AboutDialog::tr("Copyright © %2 %1").arg(AboutDialog::tr("GOU Lingfeng"), QLatin1String{"2018"}));
	_priv->ui.app_license->setText(AboutDialog::tr("This program comes with absolutely no warranty.<br />See the <a href='%2'><span style='text-decoration: underline; color:#2a76c6;'>%1</span></a> for details.").arg(AboutDialog::tr("GNU General Public License, version 3 or later"), QLatin1String{"license:gpl30"/*"https://www.gnu.org/licenses/gpl-3.0.html"*/}));
	_priv->ui.links->setText(links.join(QStringLiteral("<br />")));

	//gtk_about_dialog_add_credit_section(GTK_ABOUT_DIALOG(dlg), _("Thanks to"), app_artists);
	//g_signal_connect(dlg, "response", G_CALLBACK(gtk_widget_destroy), nullptr);
	//"authors", app_authors,
	//"documenters", app_documenters,
	//"artists", app_artists,
	//"translator-credits", "GOU Lingfeng <goulf.3m@gmail.com> (zh_CN)",
}

gapr::AboutDialog::~AboutDialog() { }

void gapr::AboutDialog::reset_page() {
	_priv->ui.stackedWidget->setCurrentWidget(_priv->ui.about);
	// XXX reset license
}

void gapr::AboutDialog::on_links_linkActivated(const QString& link) {
	if(link.startsWith(QStringLiteral("about:"))) {
		auto x=link.midRef(6);
		if(x==QStringLiteral("credits")) {
			_priv->ui.stackedWidget->setCurrentWidget(_priv->ui.credits);
		} else if(x==QStringLiteral("licenses")) {
			_priv->ui.stackedWidget->setCurrentWidget(_priv->ui.licenses);
		} else if(x==QStringLiteral("system")) {
			_priv->setup_sysinfo(this);
			_priv->ui.stackedWidget->setCurrentWidget(_priv->ui.sysinfo);
		} else if(x==QStringLiteral("back")) {
			_priv->ui.stackedWidget->setCurrentWidget(_priv->ui.about);
		}
	} else if(link.startsWith(QStringLiteral("license:"))) {
		// XXX
	} else {
		QDesktopServices::openUrl(link);
	}
}

