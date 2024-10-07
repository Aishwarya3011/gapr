#ifndef _GAPR_PROGRAM_FIX_NEURON_LIST_
#define _GAPR_PROGRAM_FIX_NEURON_LIST_

//#include "graph.h"
#include "gapr/edge-model.hh"

#include <QAbstractItemModel>

#if 0
namespace gapr {
	struct node_id;
};
#endif

namespace gapr::fix {

	class NeuronList final: public QAbstractItemModel {
		Q_OBJECT
		public:
			explicit NeuronList(QObject* parent=nullptr);
			~NeuronList();

			QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
			QModelIndex index(int row, int column, const QModelIndex& parent) const override;
			QModelIndex parent(const QModelIndex& index) const override;
			int rowCount(const QModelIndex& parent) const override;
			int columnCount(const QModelIndex& parent) const override;
			QVariant data(const QModelIndex& index, int role) const override;
			Qt::ItemFlags flags(const QModelIndex& index) const override;
			bool hasChildren(const QModelIndex& parent) const override;
#if 0
			bool setData(const QModelIndex& index, const QVariant& value, int role) override;
#endif

			void update(gapr::edge_model::updater& updater);
			gapr::node_id get_node(std::size_t i) const noexcept;
			const std::string& get_name(std::size_t i) const noexcept;

		private:
			struct NeuronInfo;
			std::vector<NeuronInfo> _infos;
	};

}

#endif
