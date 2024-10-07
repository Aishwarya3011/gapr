#include "neuron-list.hh"

#include "gapr/model.hh"

#include <charconv>

#include <QtGui/QtGui>

using NeuronList=gapr::fix::NeuronList;

struct NeuronList::NeuronInfo {
	std::string name;
	gapr::node_id id;
	int status;
	int mod;
	NeuronInfo(std::string&& n, gapr::node_id id, int status):
		name{std::move(n)}, id{id}, status{status}, mod{0} { }
};

NeuronList::NeuronList(QObject* parent):
	QAbstractItemModel{parent}, _infos{}
{
}
NeuronList::~NeuronList() {
}

QVariant NeuronList::headerData(int section, Qt::Orientation orientation, int role) const {
	switch(section) {
		case 0:
			switch(role) {
				case Qt::DisplayRole:
					return QStringLiteral("Name");
				default:
					break;
			}
			break;
		default:
			break;
	}
	return QVariant{};
}
QModelIndex NeuronList::index(int row, int column, const QModelIndex& parent) const {
	if(parent.isValid())
		return QModelIndex{};
	if(row>=0 && static_cast<std::size_t>(row)<_infos.size()) {
		//auto& info=_infos[row];
		if(column>=0 && column<1)
			return createIndex(row, column);
	}
	return QModelIndex{};
}
QModelIndex NeuronList::parent(const QModelIndex& index) const {
	return QModelIndex{};
}
int NeuronList::rowCount(const QModelIndex& parent) const {
	if(parent.isValid())
		return 0;
	return _infos.size();
}
int NeuronList::columnCount(const QModelIndex& parent) const {
	if(parent.isValid())
		return 0;
	return 1;
}
QVariant NeuronList::data(const QModelIndex& index, int role) const {
	if(!index.isValid())
		return QVariant{};
	auto row=index.row();
	if(row>=0 && static_cast<std::size_t>(row)<_infos.size()) {
		auto& info=_infos[row];
		switch(index.column()) {
			case 0:
				switch(role) {
					case Qt::DisplayRole:
						if(info.name.empty())
							return QStringLiteral("Unnamed");
						return QString::fromStdString(info.name);
					case Qt::DecorationRole:
						return info.status?QColor{0, 255, 0}:QColor{255, 0, 0};
						//QIcon{n.completed() ? ":/images/breeze/dialog-ok.svg" : ":/images/breeze/emblem-important.svg"};
					case Qt::EditRole:
						return QString::fromStdString(info.name);
					case Qt::FontRole:
						{
							QFont f{};
							if(info.name.empty())
								f.setItalic(true);
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
Qt::ItemFlags NeuronList::flags(const QModelIndex& index) const {
	if(!index.isValid())
		return Qt::NoItemFlags;
	return Qt::ItemIsSelectable|Qt::ItemIsEnabled|Qt::ItemNeverHasChildren|Qt::ItemIsUserCheckable|Qt::ItemIsEditable;
}
bool NeuronList::hasChildren(const QModelIndex& parent) const {
	if(parent.isValid())
		return false;
	return _infos.size()>0;
}

void NeuronList::update(gapr::edge_model::updater& updater) {
	for(auto n: updater.trees_del()) {
		for(std::size_t i=0; i<_infos.size(); i++) {
			if(_infos[i].id==n) {
				_infos[i].mod=-1;
				break;
			}
		}
	}
	for(std::size_t i=0; i<_infos.size(); i++) {
		if(_infos[i].mod==-1)
			continue;
		for(auto& [n, s]: updater.trees_chg()) {
			if(_infos[i].id==n) {
				_infos[i].name=std::move(s);
				_infos[i].mod=+1;
				break;
			}
		}
		auto& vert=updater.vertices().at(_infos[i].id);
		if(vert.complete!=_infos[i].status) {
			_infos[i].status=vert.complete;
			_infos[i].mod=+1;
		}
	}
	for(std::size_t i=0; i<_infos.size();) {
		auto mod=_infos[i].mod;
		if(!mod) {
			i++;
			continue;
		}
		std::size_t j;
		for(j=i+1; j<_infos.size(); j++) {
			if(_infos[j].mod!=mod)
				break;
		}
		if(mod==-1) {
			beginRemoveRows(QModelIndex{}, i, j-1);
			_infos.erase(_infos.begin()+i, _infos.begin()+j);
			endRemoveRows();
		} else if(mod==1) {
			for(std::size_t k=i; k<j; k++)
				_infos[k].mod=0;
			Q_EMIT dataChanged(createIndex(i, 0), createIndex(j-1, 0), {Qt::DisplayRole, Qt::EditRole, Qt::FontRole});
		}
	}
	auto n=updater.trees_add().size();
	if(n<=0)
		return;
	auto check_att=[&updater](const std::string& s) ->gapr::node_id {
		if(s.compare(0, 7, "attach@")!=0)
			return {};
		gapr::node_id r;
		auto [ptr, ec]=std::from_chars(&s[7], &s[s.size()], r.data);
		if(ec!=std::errc{} || ptr!=&s[s.size()])
			return {};
		auto it=updater.nodes().find(r);
		if(it==updater.nodes().end())
			return {};
		auto pos=it->second;
		if(pos.edge) {
			auto& edg=updater.edges().at(pos.edge);
			r=edg.root;
		} else if(pos.vertex) {
			auto& vert=updater.vertices().at(pos.vertex);
			r=vert.root;
		} else {
			return {};
		}
		return r;
	};
	std::unordered_map<gapr::node_id, gapr::node_id> later;
	for(auto& [n, s]: updater.trees_add()) {
		if(auto r=check_att(s); r!=gapr::node_id{})
			later.emplace(n, r);
	}
	beginInsertRows(QModelIndex{}, _infos.size(), _infos.size()+n-1-later.size());
	for(auto& [n, s]: updater.trees_add()) {
		if(later.find(n)!=later.end())
			continue;
		auto& vert=updater.vertices().at(n);
		_infos.emplace_back(std::string(s), n, vert.complete);
	}
	endInsertRows();
	for(auto [n, n2]: later) {
		unsigned int idx=_infos.size();
		for(unsigned int i=0; i<_infos.size(); ++i) {
			if(_infos[i].id==n2) {
				idx=i+1;
				break;
			}
		}
		auto& vert=updater.vertices().at(n);
		std::string nn{"   "};
		auto t=vert.attr.misc.t();
		switch(t) {
		case 1:
			nn+="Soma";
			break;
		case 2:
			nn+="Axon";
			break;
		case 3:
			nn+="Dendrite";
			break;
		case 4:
			nn+="Apical dendrite";
			break;
		default:
			nn+=std::to_string(t);
		}
		beginInsertRows(QModelIndex{}, idx, idx);
		_infos.emplace(_infos.begin()+idx, std::move(nn), n, vert.complete);
		endInsertRows();
	}
}

gapr::node_id NeuronList::get_node(std::size_t i) const noexcept {
	return _infos[i].id;
}
const std::string& NeuronList::get_name(std::size_t i) const noexcept {
	return _infos[i].name;
}

#if 0
bool ListModel::setData(const QModelIndex& index, const QVariant& value, int role) {
	/*
		if(role!=Qt::EditRole)
		return false;
		if(!index.isValid())
		return false;
		if(index.column()!=0)
		return false;
		tracer->renameNeuron(index.row(), value.toString());
		*/
	return false;
}
#endif

