#ifndef _TRACE_COMPUTE_HH_
#define _TRACE_COMPUTE_HH_

#include <unordered_set>

#include "gapr/cube.hh"
#include "gapr/edge-model.hh"

#include "evaluator.hh"

namespace gapr::trace {

	class ProbeAlg {
		public:
			explicit ProbeAlg(const gapr::affine_xform& xform);
			~ProbeAlg() { }

			struct Job {
				gapr::cube cube;
				std::atomic<bool>* cancel;
				void operator()(ProbeAlg& alg);
				std::vector<std::array<double, 3>> seeds;
			};

		private:
			const gapr::affine_xform _xform;
	};

	class ConnectAlg {
		public:
			ConnectAlg(const std::string& params, const gapr::affine_xform& xform, const edge_model& graph);
			~ConnectAlg() { }

			void evaluator(const std::string& params);

			struct Job {
				gapr::cube cube;
				std::array<unsigned int, 3> offset;
				std::atomic<bool>* cancel;
				void operator()(ConnectAlg& alg);
				gapr::delta_add_patch_ delta;
				gapr::delta_proofread_ delta2;
			};

		private:
			const gapr::affine_xform _xform;
			const edge_model& _graph;
			std::unordered_set<gapr::node_id> _dirty;
			std::shared_ptr<Evaluator> _evaluator;
			std::shared_ptr<Detector> _detector;
			void impl(Job& job);
	};

	class EvaluateAlg {
		public:
			EvaluateAlg(const std::string& params, const gapr::affine_xform& xform, const gapr::edge_model& graph);
			~EvaluateAlg() { }

			bool skip_node(gapr::node_id id) {
				return _dirty.find(id)!=_dirty.end();
			}

			struct Job {
				gapr::cube cube;
				std::array<unsigned int, 3> offset;
				std::atomic<bool>* cancel;
				void operator()(EvaluateAlg& alg);
				gapr::delta_proofread_ delta;
			};

		private:
			const gapr::affine_xform _xform;
			const edge_model& _graph;
			std::unordered_set<gapr::node_id> _dirty;
			std::shared_ptr<Evaluator> _evaluator;
	};

}

#endif

