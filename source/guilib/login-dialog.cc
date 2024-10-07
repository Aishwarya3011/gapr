#include "gapr/gui/login-dialog.hh"

#include "gapr/parser.hh"
#include "gapr/utility.hh"

#include <cassert>
//#include <string>
//#include <memory>
//#include <unordered_map>

#include <QtGui/QtGui>

#include "ui_dlg-login.h"

class RepoValidator: public QValidator {
	Q_OBJECT
	public:
		explicit RepoValidator(QObject* parent=nullptr):
			QValidator{parent} { }
		~RepoValidator() { }
	private:
		QValidator::State validate(QString& input, int& pos) const override {
			auto j=input.size();
			while(j>0 && input[j-1].isSpace())
				j--;
			int left=0;
			while(left<j && input[left].isSpace())
				left++;

			if(left>=j)
				return QValidator::Intermediate;
			auto str=input.midRef(left, j-left);
			auto coli=str.indexOf(':', 0);
			if(coli==-1) {
				coli=str.size();
			} else {
				if(coli<=0) {
					pos=left;
					return QValidator::Intermediate;
				}
			}
			for(auto k=0; k<coli; k++)
				if(str[k].isSpace()) {
					return QValidator::Invalid;
				}

			if(str.size()<=coli+1)
				return QValidator::Intermediate;
			auto slashi=str.indexOf('/', coli+1);
			if(slashi==-1) {
				slashi=str.size();
			} else {
				if(slashi<=coli+1) {
					pos=left+coli+1;
					return QValidator::Intermediate;
				}
			}
			auto port_str=input.mid(coli+1+left, slashi-coli-1).toStdString();
			try {
				auto port=gapr::parse_port(&port_str[0], port_str.size());
				(void)port;
			} catch(const std::exception& e) {
				return QValidator::Invalid;
			}

			if(str.size()<=slashi+1)
				return QValidator::Intermediate;
			for(auto k=slashi+1; k<str.size(); k++)
				if(str[k].isSpace()) {
					return QValidator::Invalid;
				}

			input=str.toString();
			return QValidator::Acceptable;
		}
};

class UserValidator: public QValidator {
	Q_OBJECT
	public:
		explicit UserValidator(QObject* parent=nullptr):
			QValidator{parent} { }
		~UserValidator() { }
	private:
		QValidator::State validate(QString& input, int& pos) const override {
			auto j=input.size();
			while(j>0 && input[j-1].isSpace())
				j--;
			int left=0;
			while(left<j && input[left].isSpace())
				left++;

			if(left>=j)
				return QValidator::Intermediate;
			auto str=input.mid(left, j-left);
			auto user=str.toStdString();
			auto [p, ok]=gapr::parse_name(&user[0], user.size());
			if(!ok || p!=user.size()) {
				pos=left+p+1;
				if(pos>input.size())
					pos=input.size();
				return QValidator::Intermediate;
			}

			input=std::move(str);
			return QValidator::Acceptable;
		}
};

class PassValidator: public QValidator {
	Q_OBJECT
	public:
		explicit PassValidator(QObject* parent=nullptr):
			QValidator{parent} { }
		~PassValidator() { }
	private:
		QValidator::State validate(QString& input, int& pos) const override {
			if(input.isEmpty()) {
				//pos=0;
				return QValidator::Intermediate;
			}
			for(auto k=0; k<input.size(); k++)
				if(input[k]=='\n' || input[k]=='\r') {
					pos=k;
					return QValidator::Invalid;
				}

			return QValidator::Acceptable;
		}
};

static const QString hist_grp=QStringLiteral("history");
static const QString hist_key=QStringLiteral("repo");
static constexpr const int hist_max=16;

struct gapr::login_dialog::PRIV {
	Ui::login_dialog ui;
};

gapr::login_dialog::login_dialog(QWidget* parent):
	QDialog{parent},
	_priv{std::make_unique<PRIV>()}
{
	_priv->ui.setupUi(this);

	_priv->ui.repo->setValidator(new RepoValidator{this});
	_priv->ui.user->setValidator(new UserValidator{this});
	_priv->ui.pass->setValidator(new PassValidator{this});

	QSettings settings{};
	settings.beginGroup(hist_grp);
	auto cnt=settings.beginReadArray(hist_key);
	auto list=_priv->ui.history;
	while(cnt-->0) {
		settings.setArrayIndex(cnt);
		if(auto v=settings.value(hist_key); v.type()==QVariant::String)
			list->addItem(v.toString());
	}
	settings.endArray();
	settings.endGroup();

	auto icon=QApplication::windowIcon();
}

gapr::login_dialog::~login_dialog() { }

void gapr::login_dialog::on_history_currentItemChanged(QListWidgetItem* item, QListWidgetItem*) {
	auto repo=item->text();
	auto i=repo.indexOf('@');
	if(i==-1) {
		item->setHidden(true);
		return;
	}
	_priv->ui.user->setText(repo.left(i));
	_priv->ui.repo->setText(repo.mid(i+1));
	//_priv->ui.pass->clear();
}
void gapr::login_dialog::on_history_itemActivated(QListWidgetItem* item) {
	on_button_box_accepted();
}
void gapr::login_dialog::on_button_box_accepted() {
	if(!_priv->ui.repo->hasAcceptableInput()) {
		_priv->ui.repo->setFocus();
		return;
	}
	if(!_priv->ui.user->hasAcceptableInput()) {
		_priv->ui.user->setFocus();
		return;
	}
	if(!_priv->ui.pass->hasAcceptableInput()) {
		_priv->ui.pass->setFocus();
		return;
	}

	auto usr_repo=QStringLiteral("%1@%2").arg(_priv->ui.user->text()).arg(_priv->ui.repo->text());
	// XXX further check???

	QSettings settings{};
	settings.beginGroup(hist_grp);
	auto cnt=settings.beginReadArray(hist_key);
	QStringList repos{};
	repos<<usr_repo;
	for(auto idx=cnt; idx-->0;) {
		settings.setArrayIndex(idx);
		if(auto v=settings.value(hist_key); v.type()==QVariant::String) {
			auto repo=v.toString();
			if(repo==usr_repo) {
				if(idx+1!=cnt)
					continue;
				repos.clear();
				break;
			}
			repos<<repo;
			if(repos.size()>=hist_max)
				break;
		}
	}
	settings.endArray();
	if(!repos.empty()) {
		settings.beginWriteArray(hist_key);
		auto idx=repos.size();
		for(auto& r: repos) {
			settings.setArrayIndex(--idx);
			settings.setValue(hist_key, std::move(r));
		}
		settings.endArray();

		settings.sync();
		if(auto s=settings.status(); s!=QSettings::NoError)
			gapr::print(1, "failed to save settings: ", s);
	}
	settings.endGroup();
	
	accept();
}

void gapr::login_dialog::on_button_box_rejected() {
	reject();
}

void gapr::login_dialog::get(std::string& user, std::string& host, unsigned short& port, std::string& group, std::string& passwd) {
	try {
		std::string user_;
		parse_repo(_priv->ui.repo->text().toStdString(), user_, host, port, group);
	} catch(const std::exception& e) {
		assert(0);
	}
	user=_priv->ui.user->text().toStdString();
	passwd=_priv->ui.pass->text().toStdString();
}

#include "login-dialog.moc"
