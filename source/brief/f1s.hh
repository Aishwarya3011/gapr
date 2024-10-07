#include "gapr/swc-helper.hh"

#include <vector>
#include <memory>

std::array<std::shared_ptr<void>, 2> f1score_impl(
		const std::vector<gapr::swc_node>& ref,
		const std::shared_ptr<void>& ref_aux,
		const std::vector<gapr::swc_node>& tgt,
		const std::shared_ptr<void>& tgt_aux);

