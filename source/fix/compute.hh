#ifndef _FIX_COMPUTE_HH_
#define _FIX_COMPUTE_HH_

#include "gapr/cube.hh"
#include "gapr/commit.hh"
#include "model-misc.hh"

namespace gapr::fix {

	struct ConnectAlg {
		struct {
			const edge_model* graph;
			Position cur_pos;
			Position tgt_pos;
			unsigned int method{0};
			std::atomic<bool>* cancel;

			gapr::cube cube;
			const gapr::affine_xform* xform;
			std::array<uint32_t, 3> offset;
			std::array<double, 2> xfunc;
		} args;

		/*! just return delta. serialize after user's acception */
		gapr::delta_add_edge_ compute();
		//...->***delta->|(setPath)->(chkPath)->*payload->commit->apply/change
	};

}

#endif
