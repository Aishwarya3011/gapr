#include "evaluator.hh"

#include <cstdio>

struct EvaluatorDumb: Evaluator {
	void train(BatchGenerator gen, const std::filesystem::path& fn_params) override {
		fprintf(stderr, "not implemented\n");
	}
	std::valarray<float> predict(const std::valarray<float>& input) override {
		fprintf(stderr, "not implemented\n");
		return {};
	}
	void load(std::istream& sbuf) override {
		fprintf(stderr, "not implemented\n");
	}
};

std::shared_ptr<Evaluator> Evaluator::create() {
	return std::make_shared<EvaluatorDumb>();
}

struct DetectorDumb: Detector {
	std::valarray<uint16_t> predict(gapr::cube_view<const void> input) override {
		fprintf(stderr, "not implemented\n");
		return {};
	}
	void load(std::istream& sbuf) override {
		fprintf(stderr, "not implemented\n");
	}
	void train(unsigned int seed, BatchGenerator gen, const std::filesystem::path& fn_params) override {
		fprintf(stderr, "not implemented\n");
	}
};

std::shared_ptr<Detector> Detector::create() {
	return std::make_shared<DetectorDumb>();
}

