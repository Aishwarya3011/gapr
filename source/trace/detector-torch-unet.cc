#include "evaluator.hh"

#include "gapr/utility.hh"

#include <chrono>
#include <random>
#include <optional>

#include <torch/torch.h>
#include <c10/cuda/CUDACachingAllocator.h>

#include "torch-helpers.hh"
#include "torch-networks.hh"
#include "gapr/detail/nrrd-output.hh"


constexpr unsigned int pad_z=gapr::nn::image_prober_pad_z, pad_xy=gapr::nn::image_prober_pad_xy;

static torch::Device dev{torch::kCUDA};
struct DetectorImpl: Detector {
	gapr::nn::ImageProber net;

	explicit DetectorImpl(): Detector{}, net{} {
	}

	void probe(std::vector<float>& image, std::vector<float>& image2, const std::array<unsigned int, 3>& sizes) {
		auto input_img=torch::tensor(torch::ArrayRef<float>{image}).view({-1, 1, sizes[2], sizes[1], sizes[0]});

		gpu_lock lck{};
		net->to(dev);
		net->eval();
		torch::NoGradGuard no_grad{};

		input_img=input_img.to(dev);
		auto target=net(input_img);
		target=target.to(torch::kCPU);

		lck.unlock();

		image.resize(sizes[0]*sizes[1]*(sizes[2]*2+1)*input_img.size(0));
		image2.resize(sizes[0]*sizes[1]*(sizes[2]*2+1)*input_img.size(0));
		for(unsigned int k=0; k<input_img.size(0); ++k) {
			for(unsigned int z=0; z<(sizes[2]*2+1); ++z) {
				for(unsigned int y=0; y<sizes[1]; ++y) {
					std::copy_n(target[k][0][z][y].data_ptr<float>(), sizes[0], &image[((k*(sizes[2]*2+1)+z)*sizes[1]+y)*sizes[0]]);
					std::copy_n(target[k][1][z][y].data_ptr<float>(), sizes[0], &image2[((k*(sizes[2]*2+1)+z)*sizes[1]+y)*sizes[0]]);
				}
			}
		}
	}
	std::valarray<uint16_t> predict(gapr::cube_view<const void> cube_view) override {
		constexpr unsigned int batch_size=2;
		constexpr std::array<unsigned int, 3> sliding_cube_sizes{257, 257, 63};

		//ext_sizes[i]=(ext_sizes[i]/16+1)*16-1;
		std::vector<std::array<unsigned int, 3>> sliding_cubes;
		for(unsigned int z0=0; z0<cube_view.sizes(2); z0+=56) {
			for(unsigned int y0=0; y0<cube_view.sizes(1); y0+=224) {
				for(unsigned int x0=0; x0<cube_view.sizes(0); x0+=224) {
					auto& off=sliding_cubes.emplace_back();
					off={x0, y0, z0};

					for(unsigned int i=0; i<3; ++i) {
						assert(cube_view.sizes(i)>=sliding_cube_sizes[i]/2);
						if(off[i]+sliding_cube_sizes[i]/2>cube_view.sizes(i))
							off[i]=cube_view.sizes(i)-sliding_cube_sizes[i]/2;
					}
				}
			}
		}

		gpu_lock::begin_session();
		std::valarray<uint16_t> cube_img(uint16_t{0}, cube_view.sizes(0)*cube_view.sizes(1)*(cube_view.sizes(2)*2+1)*2);
		for(std::size_t k=0; k<sliding_cubes.size(); k+=batch_size) {
			std::vector<float> image;
			std::vector<float> image2;
			image.reserve(batch_size*sliding_cube_sizes[0]*sliding_cube_sizes[1]*(sliding_cube_sizes[2]*2+1));
			auto k2=std::min(k+batch_size, sliding_cubes.size());
			for(unsigned int j=k; j<k2; ++j) {
				auto cur_offset=sliding_cubes[j];
				for(unsigned int z0=0; z0<sliding_cube_sizes[2]; ++z0) {
					for(unsigned int y0=0; y0<sliding_cube_sizes[1]; ++y0) {
						auto z=z0+cur_offset[2];
						auto y=y0+cur_offset[1];
						if(z>=cube_view.sizes(2) || y>=cube_view.sizes(1)) {
							for(unsigned int x=0; x<sliding_cube_sizes[0]; ++x)
								image.push_back(-2);
							continue;
						}
						auto x0=cur_offset[0];
						auto x1=std::min(x0+sliding_cube_sizes[0], cube_view.sizes(0));
						cube_view.visit([y,z,&image,x0,x1](auto& view) {
							auto src=view.row(y, z);
							using T=std::remove_pointer_t<decltype(src)>;
							if constexpr(std::is_unsigned_v<T>) {
								double scale=std::numeric_limits<T>::max();
								for(unsigned int x=x0; x<x1; ++x)
									image.push_back((src[x]/scale)*2-1);
							}
						});
						for(unsigned int x=x1-x0; x<sliding_cube_sizes[0]; ++x)
							image.push_back(-2);
					}
				}
			}

			probe(image, image2, sliding_cube_sizes);

			for(unsigned int j=k; j<k2; ++j) {
				auto cur_offset=sliding_cubes[j];
				for(unsigned int z0=pad_z; z0+pad_z<sliding_cube_sizes[2]*2+1; ++z0) {
					for(unsigned int y0=pad_xy; y0+pad_xy<sliding_cube_sizes[1]; ++y0) {
						auto z=z0+cur_offset[2]*2;
						auto y=y0+cur_offset[1];
						if(z+pad_z>=cube_view.sizes(2)*2+1 || y+pad_xy>=cube_view.sizes(1))
							continue;
						auto src=&image[(((j-k)*(sliding_cube_sizes[2]*2+1)+z0)*sliding_cube_sizes[1]+y0)*sliding_cube_sizes[0]];
						auto dst=&cube_img[(z*cube_view.sizes(1)+y)*cube_view.sizes(0)];
						auto src2=&image2[(((j-k)*(sliding_cube_sizes[2]*2+1)+z0)*sliding_cube_sizes[1]+y0)*sliding_cube_sizes[0]];
						auto dst2=&cube_img[((cube_view.sizes(2)*2+1+z)*cube_view.sizes(1)+y)*cube_view.sizes(0)];
						for(unsigned int x0=pad_xy; x0+pad_xy<sliding_cube_sizes[0]; ++x0) {
							auto x=x0+cur_offset[0];
							if(x+pad_xy>=cube_view.sizes(0))
								break;
							assert(src[x0]<=1.0f);
							assert(src[x0]>=0.0f);
							uint16_t v1=src[x0]*float{std::numeric_limits<uint16_t>::max()};
							dst[x]=std::max(dst[x], v1);
							assert(src2[x0]>=0.0f);
							auto vv=src2[x0]/std::log(256.0f);
							if(vv>1.0f)
								vv=1.0f;
							dst2[x]=vv*float{std::numeric_limits<uint16_t>::max()};
						}
					}
				}
			}
		}

		gapr::print("probe done");
		if(false) {
			std::ostringstream oss;
			oss<<"/tmp/gapr-probe-"<<std::this_thread::get_id();
			auto pfx=oss.str();
			std::ofstream fs1{pfx+"-test-input.nrrd"};
			gapr::nrrd_output nrrd1{fs1, true};
			nrrd1.header();
			nrrd1.finish(cube_view);

			std::ofstream fs2{pfx+"-test.nrrd"};
			gapr::nrrd_output nrrd2{fs2, true};
			nrrd2.header();
			nrrd2.finish(&cube_img[0], cube_view.sizes(0), cube_view.sizes(1), cube_view.sizes(2)*2+1);

			std::ofstream fs3{pfx+"-test-r.nrrd"};
			gapr::nrrd_output nrrd3{fs3, true};
			nrrd3.header();
			nrrd3.finish(&cube_img[(cube_view.sizes(2)*2+1)*cube_view.sizes(1)*cube_view.sizes(0)], cube_view.sizes(0), cube_view.sizes(1), cube_view.sizes(2)*2+1);

			gapr::print("dump image");
		}

		return cube_img;
	}

	void train(unsigned int seed, BatchGenerator gen, const std::filesystem::path& fn_params) override;

	void load(std::istream& str) override {
		torch::load(net, str);
	}
};

std::shared_ptr<Detector> Detector::create() {
	return std::make_shared<DetectorImpl>();
}

namespace {
	struct Summary {
		double sum1{0.0}, max1{-INFINITY};
		std::size_t cnt{0};
		std::chrono::steady_clock::time_point t0;
		Summary(): t0{std::chrono::steady_clock::now()} { }
		void add(double v) {
			sum1+=v;
			if(v>max1)
				max1=v;
			++cnt;
		}
		void clear() {
			*this=Summary{};
		}
		void print(std::ostream& oss) {
			auto t1=std::chrono::steady_clock::now();
			auto dt=std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
			oss<<", loss: "<<sum1/cnt<<' '<<max1<<" time: "<<dt<<"ms\n";
		}
	};
}

static auto prepare_torch_xy(const std::vector<float>& input_imgs_, const std::vector<float>& target_imgs_, unsigned int n_bat, unsigned long rng) {
	auto input_imgs=torch::tensor(torch::ArrayRef<float>{input_imgs_}).view({n_bat, 1, prober_d, prober_wh, prober_wh}).to(dev);
	auto target_imgs=torch::tensor(torch::ArrayRef<float>{target_imgs_}).view({n_bat, 2, prober_d2, prober_wh, prober_wh}).to(dev);
	std::vector<int64_t> flips{};
	for(auto dim: {2, 3, 4}) {
		if((rng>>dim)&1)
			flips.push_back(dim);
	}
	if(!flips.empty()) {
		input_imgs=torch::flip(input_imgs, flips);
		target_imgs=torch::flip(target_imgs, flips);
	}
	bool swapxy=(rng>>8)&1;
	if(swapxy) {
		input_imgs=torch::transpose(input_imgs, 3, 4);
		target_imgs=torch::transpose(target_imgs, 3, 4);
	}
	std::string flips_str;
	for(auto f: flips)
		flips_str+=" "+std::to_string(f);
	std::cerr<<flips_str<<" "<<swapxy<<"\n";
	input_imgs=input_imgs.clone(torch::MemoryFormat::Contiguous);
	target_imgs=target_imgs.clone(torch::MemoryFormat::Contiguous);
	return std::make_tuple(std::move(input_imgs), std::move(target_imgs));
}

[[maybe_unused]] static torch::Tensor remove_borders(torch::Tensor x) {
	x=torch::slice(x, 2, 2, prober_d2-2);
	x=torch::slice(x, 3, 4, prober_wh-4);
	x=torch::slice(x, 4, 4, prober_wh-4);
	return x;
}

template<typename T>
void save_params(T& net, const std::filesystem::path& fn) {
	std::ofstream str{fn};
	if(!str)
		throw std::runtime_error{"failed to write file"};
	net->to(torch::kCPU);
	torch::save(net, str);
	net->to(dev);
	if(str.close(), !str)
		throw std::runtime_error{"failed to close file"};
}

void DetectorImpl::train(unsigned int seed, BatchGenerator gen, const std::filesystem::path& fn_params) {
	torch::manual_seed(seed);
	std::mt19937 rng{seed};

	auto net=this->net;
	net->to(dev);
	net->train();
	print_params(std::cerr, *net);

	//torch::optim::SGD optim{net->parameters(), torch::optim::SGDOptions{.01}};
	//torch::optim::Adam optim{net->parameters(), torch::optim::AdamOptions{.01}};
	torch::optim::RMSprop optim{net->parameters(), torch::optim::RMSpropOptions{.001}};
	torch::optim::StepLR scheduler{optim, 1, 0.3};

	Summary loss_sum;

	unsigned int max_epoch=8;
	for(unsigned int epoch=1; epoch<=max_epoch; ++epoch) {
		unsigned int max_iter=1024*(2*epoch);
		for(std::size_t iter=0; iter<max_iter; ) {
			auto [input_imgs_, target_imgs_, n_bat]=gen();
			auto [input_imgs, target_imgs]=prepare_torch_xy(input_imgs_, target_imgs_, n_bat, rng());

			if(iter%32==0)
				loss_sum.clear();
			torch::Tensor w_probs;
			if(input_imgs.size(0)<4)
				continue;
			++iter;

			auto get_loss=[this](bool print, auto pred_imgs_, auto target_imgs_, auto& w_probs) {
				std::optional<torch::NoGradGuard> no_grad;
				no_grad.emplace();
				auto tgt_probs=torch::slice(target_imgs_, 1, 0, 1);
				auto tgt_rad=torch::slice(target_imgs_, 1, 1, 2);
				{
					tgt_rad=torch::max_pool3d(tgt_rad, {3, 3, 3}, 1, {1, 1, 1});
				}
				auto tmp=torch::avg_pool3d(tgt_probs, {3, 5, 5}, 1, {1, 2, 2}, false, false);
				tmp=torch::avg_pool3d(tmp, {3, 3, 3}, 1, {1, 1, 1}, false, false);
				tmp=torch::avg_pool3d(tmp, {1, 3, 3}, 1, {0, 1, 1}, false, false);
				auto mask_o1=torch::less(tmp, 1.0/64);
				auto mask_o2=torch::less(tmp, 1.0/512);
				no_grad.reset();

				pred_imgs_=remove_borders(pred_imgs_);
				auto pred_probs=torch::slice(pred_imgs_, 1, 0, 1);
				auto pred_rad=torch::slice(pred_imgs_, 1, 1, 2);
				mask_o1=remove_borders(mask_o1);
				mask_o2=remove_borders(mask_o2);
				tgt_probs=remove_borders(tgt_probs);
				tgt_rad=remove_borders(tgt_rad);
				auto scale=tgt_probs.size(3)*tgt_probs.size(4);

				no_grad.emplace();
				w_probs=torch::zeros(tgt_probs.sizes()).to(dev);
				w_probs=torch::masked_fill(w_probs, torch::greater(tgt_probs, 0), 1);
				w_probs=torch::masked_fill(w_probs, mask_o1, .5);
				w_probs=torch::masked_fill(w_probs, mask_o2, .1);
				tgt_probs=torch::max_pool3d(tgt_probs, {1, 3, 3}, 1, {0, 1, 1});
				no_grad.reset();
				auto loss_probs=torch::binary_cross_entropy(pred_probs, tgt_probs, w_probs)*scale;

				// XXX
				//auto mask_r=torch::logical_or(torch::greater(tgt_rad, 0), mask_o);
				auto mask_r=torch::greater(tgt_rad, 0);
				pred_rad=torch::masked_select(pred_rad, mask_r);
				tgt_rad=torch::masked_select(tgt_rad, mask_r);
				auto loss_rad=torch::smooth_l1_loss(pred_rad, tgt_rad, torch::Reduction::Mean, 0.01).nan_to_num(0.0, 0.0, 0.0);

				if(print)
					fprintf(stderr, "..... eval: %f %f\n", loss_probs.template item<float>(), loss_rad.template item<float>());
				return loss_probs+loss_rad;
			};

			if(iter%32==0) {
				c10::cuda::CUDACachingAllocator::emptyCache();
				net->eval();
				torch::NoGradGuard no_grad{};

				auto pred_imgs=net->forward(input_imgs);
				auto loss=get_loss(true, pred_imgs, target_imgs, w_probs);
				if(true) {
					std::vector<std::pair<std::string, torch::Tensor>> to_dump{
						{"input", input_imgs[0][0]},
						{"weight", w_probs[0]},
						{"target.prob", target_imgs[0][0]},
						{"target.r", target_imgs[0][1]},
						{"output.prob", pred_imgs[0][0]},
						{"output.r", pred_imgs[0][1]},
						{"tmp4", net->_tmp_res2[0]},
						{"tmp3", net->_tmp_ups[0]},
						{"tmp2", net->_tmp_x63[0]},
						{"tmp1", net->_tmp_ds1[0]},
						{"w", net->_tmp_w},
						{"b", net->_tmp_b},
						{"con", net->_tmp_con},
					};
					for(auto [lbl, img]: to_dump) {
						auto [vec, sizes]=tensor_to_cube<float>(img);
						gapr::nrrd_output::save("/tmp/test."+lbl+".nrrd", &vec[0], sizes);
					}
				}

				net->train();
				c10::cuda::CUDACachingAllocator::emptyCache();
			}

			assert(input_imgs.size(0)>=4);
			optim.zero_grad();
			auto pred_imgs=net(input_imgs);
			auto loss=get_loss(false, pred_imgs, target_imgs, w_probs);
			loss.backward();
			optim.step();

			loss_sum.add(loss.template item<float>());
			if(iter%32==0) {
				std::cout<<"E: "<<epoch<<'/'<<max_epoch;
				loss_sum.print(std::cout<<" I: "<<iter<<'/'<<max_iter);
			}
			if(iter%(1024*2)==0 && iter<max_iter)
				save_params(net, fn_params);
		}
		scheduler.step();
		if(epoch<max_epoch)
			save_params(net, fn_params);
	}
	save_params(net, fn_params);
}
