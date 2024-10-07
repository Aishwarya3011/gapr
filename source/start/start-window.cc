/* lib/gui/start-window.cc
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

#include "start-window.hh"

#include "gapr/utility.hh"
#include "gapr/gui/application.hh"
#include "gapr/gui/utility.hh"

#include <QtWidgets/QtWidgets>

#include "ui_win-start.h"

#include "config.hh"

namespace {
	struct Program {
		QLatin1String id;
		bool enabled;
		const char* name;
		const char* info;
	};
	static /*constexpr*/ /*const*/ std::array<Program, 2> factories {
		Program{ QLatin1String{"proofread"}, true,
			QT_TRANSLATE_NOOP("start", "Proofread"),
			QT_TRANSLATE_NOOP("start", "Proofread tracing results.")
		}, Program{QLatin1String{"fix"}, true,
			QT_TRANSLATE_NOOP("start", "Fix"),
			QT_TRANSLATE_NOOP("start", "Edit tracing results.")
		},
	};
}

class FactoriesModel: public QAbstractItemModel {
	Q_OBJECT
	public:
		explicit FactoriesModel(QObject* parent=nullptr):
			QAbstractItemModel{parent} { }

		QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
			switch(section) {
				case 0:
					switch(role) {
						case Qt::DisplayRole:
							return QStringLiteral("Info");
						default:
							break;
					}
					break;
				default:
					break;
			}
			return QVariant{};
		}

		int rowCount(const QModelIndex& parent) const override {
			if(parent.isValid())
				return 0;
			return factories.size();
		}

		int columnCount(const QModelIndex& parent) const override {
			if(parent.isValid())
				return 0;
			return 1;
		}

		QModelIndex index(int row, int column, const QModelIndex& parent) const override {
			if(parent.isValid())
				return QModelIndex{};
			if(row>=0 && static_cast<std::size_t>(row)<factories.size()) {
				if(column>=0 && column<1)
					return createIndex(row, column);
			}
			return QModelIndex{};
		}

		QModelIndex parent(const QModelIndex& child) const override {
			return QModelIndex{};
		}

		QVariant data(const QModelIndex& index, int role) const override {
			if(!index.isValid())
				return QVariant{};
			auto row=index.row();
			if(row>=0 && static_cast<std::size_t>(row)<factories.size()) {
				auto& dat=factories[index.row()];
				switch(index.column()) {
					case 0:
						switch(role) {
							case Qt::DisplayRole:
								return QCoreApplication::translate("start", dat.info);
#if 0
							case Qt::DecorationRole:
								return QApplication::windowIcon();
#endif
							case Qt::FontRole:
								{
									QFont f{};
									f.setBold(true);
									return f;
								}
							default:
								break;
						}
					default:
						break;
				}
			}
			return QVariant{};
		}

		Qt::ItemFlags flags(const QModelIndex& index) const override {
			if(!index.isValid())
				return Qt::NoItemFlags;
			auto r=Qt::ItemIsSelectable|Qt::ItemNeverHasChildren|Qt::ItemIsUserCheckable;
			if(factories[index.row()].enabled)
				r|=Qt::ItemIsEnabled;
			return r;
		}
};

class ListDelegate: public QStyledItemDelegate {
	Q_OBJECT
	public:
		ListDelegate(QObject* parent): QStyledItemDelegate{parent} { }
		~ListDelegate() override { }
	private:

		void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
			auto style=QApplication::style();

			auto& dat=factories[index.row()];
			auto name=QCoreApplication::translate("start", dat.name);

			QPalette::ColorRole r=QPalette::Text;
			if(option.state&QStyle::State_Selected) {
				painter->fillRect(option.rect, option.palette.highlight());
				r=QPalette::HighlightedText;
			}
			auto f=option.font;
			f.setBold(true);
			f.setPointSizeF(1.1*f.pointSizeF());
			painter->setFont(f);
			auto rect0=option.rect;
			rect0.setBottom((rect0.top()+rect0.bottom())/2);
			style->drawItemText(painter, rect0, option.displayAlignment, option.palette, option.state&QStyle::State_Enabled, name, r);
			painter->setFont(option.font);
			auto rect=option.rect;
			rect.setTop((rect.top()+rect.bottom())/2);
			auto info=QCoreApplication::translate("start", dat.info);
			style->drawItemText(painter, rect, option.displayAlignment, option.palette, option.state&QStyle::State_Enabled, info, r);
		}
		QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
			auto s=QStyledItemDelegate::sizeHint(option, index);
			s.setHeight(2*s.height());
			s.setWidth(s.width()+32);
			return s;
		}
};




struct gapr::start_window::PRIV {
	gapr::Ui::start_window ui;
	QAbstractItemModel* model;

	explicit PRIV() {
	}
	~PRIV() {
	}
};

gapr::start_window::start_window():
	QMainWindow{nullptr},
	_priv{std::make_unique<PRIV>()}
{
	_priv->ui.setupUi(this);
	auto model=new FactoriesModel{this};
	_priv->ui.list_view->setModel(model);
	auto del=new ListDelegate{this};
	_priv->ui.list_view->setItemDelegate(del);
}

gapr::start_window::~start_window() {
}
void gapr::start_window::on_list_view_activated(const QModelIndex& index) {

	auto bindir=gapr::bindir();
	auto prog=QString::fromUtf8(bindir.data(), bindir.size());
	prog.append(QLatin1String{"/" PACKAGE_TARNAME "-"});
	prog.append(factories[index.row()].id);
	// XXX windows
	//std::regex_replace(std::back_inserter(result), s.begin(), s.end(), re, fmt, flags)
	gapr::print(1, "start: ", prog.toStdString());
	if(QProcess::startDetached(prog, {})) {
		deleteLater();
		return;
	}

	gapr::showError(QStringLiteral("failed to start"), this);
	factories[index.row()].enabled=false;
	auto model=_priv->ui.list_view->model();
	Q_EMIT model->dataChanged(index, index);
}

void gapr::start_window::on_file_run_triggered() {
	gapr::app().show_start_window(this);
}
void gapr::start_window::on_file_options_triggered() {
	gapr::app().show_options_dialog(*this);
}
void gapr::start_window::on_file_quit_triggered() {
	gapr::app().request_quit();
}
void gapr::start_window::on_help_manual_triggered() {
	gapr::app().display_help(*this);
}
void gapr::start_window::on_help_about_triggered() {
	gapr::app().show_about_dialog(*this);
}

#include "start-window.moc"
