#include <torch/torch.h>

namespace gapr::nn {

	struct ResModuleImpl: torch::nn::Module {
		std::function<torch::Tensor(torch::Tensor)> forward_impl;
		std::function<void()> init;
		torch::Tensor forward(torch::Tensor x) {
			return forward_impl(x);
		}
		explicit ResModuleImpl(int in_chan, int out_chan, int xystride, int zstride, int groups=1, int factor=4);
	};
	TORCH_MODULE(ResModule);

	struct LinResModuleImpl: torch::nn::Module {
		std::function<torch::Tensor(torch::Tensor)> forward_impl;
		torch::Tensor forward(torch::Tensor x) {
			return forward_impl(x);
		}
		explicit LinResModuleImpl(int in_chan, int out_chan);
	};
	TORCH_MODULE(LinResModule);

	struct EvaluatorImpl: torch::nn::Module {
		std::function<torch::Tensor(torch::Tensor)> forward;
		explicit EvaluatorImpl();
	};
	TORCH_MODULE(Evaluator);

	struct ImageProberImpl: torch::nn::Module {
		std::function<torch::Tensor(torch::Tensor)> forward;
		torch::Tensor _tmp_ds1, _tmp_x63, _tmp_ups, _tmp_res2;
		torch::Tensor _tmp_w, _tmp_b, _tmp_con;
		explicit ImageProberImpl();
	};
	TORCH_MODULE(ImageProber);
	constexpr unsigned int image_prober_pad_z=3, image_prober_pad_xy=12;
	constexpr unsigned int image_prober_wh=129;
	constexpr unsigned int image_prober_d=47;
	constexpr unsigned int image_prober_d2=95;

}

