#include "evaluator.hh"

#include "gapr/utility.hh"

#include <chrono>
#include <random>

#include <torch/torch.h>
#include <c10/cuda/CUDACachingAllocator.h>

#include "torch-helpers.hh"
#include "torch-networks.hh"
#include "gapr/detail/nrrd-output.hh"


static torch::Device dev{torch::kCUDA};

template<typename T>
static void save_pars(T& net, const std::filesystem::path& out_pars) {
	std::ofstream str{out_pars};
	if(!str)
		throw std::runtime_error{"failed to open output file"};
	net->to(torch::kCPU);
	torch::save(net, str);
	net->to(dev);
	if(str.close(), !str)
		throw std::runtime_error{"failed to close file"};
}

struct EvaluatorImpl: Evaluator {
	gapr::nn::Evaluator net;

	static constexpr const std::size_t InputSize=48*48*16;
	static constexpr const std::size_t OutputSize=3;
	static constexpr const std::size_t ResponseSize=1;

	explicit EvaluatorImpl(): Evaluator{}, net{} {
	}

	torch::Tensor get_loss(torch::Tensor pred, torch::Tensor y, torch::Tensor y_s) {
		return torch::nll_loss(pred, y);
	}

	unsigned int error_idx{0};
	void evaluate(const torch::Tensor& x, const torch::Tensor& y, const torch::Tensor& y_s) {
		auto pred=net->forward(x);
		auto loss1=get_loss(pred, y, y_s);
		std::cerr<<"  loss1: "<<loss1.item().toDouble()<<"\n";
		auto r=(pred.argmax(1)==y).sum().item().toDouble();
		std::cerr<<"eval: "<<r<<"/"<<x.size(0)<<"\n";
		if(true) {
			gapr::print(1, "begin dump errors");
			torch::Device cpu{torch::kCPU};
			auto a=pred.argmax(1).to(cpu);
			auto b=y.to(cpu);
			std::valarray<float> cube(16*48*48);
			for(unsigned int i=0; i<x.size(0); ++i) {
				if(a[i].item().toLong()!=b[i].item().toLong()) {
					char buf[1024];
					snprintf(buf, 1024, "/tmp/gapr-train-error-%03d.nrrd", error_idx);
					error_idx=(error_idx+1)%128;
					std::ofstream fs{buf};
					auto xx=x[i][0].to(cpu);
					for(unsigned int z=0; z<16; ++z) {
						for(unsigned int y=0; y<48; ++y) {
							for(unsigned int x=0; x<48; ++x) {
								cube[z*(48*48)+y*48+x]=xx[z][y][x].item().toDouble();
							}
						}
					}
					gapr::nrrd_output nrrd{fs};
					nrrd.header();
					nrrd.comment("training debug");
					nrrd.comment("pred ", a[i].item().toLong());
					nrrd.comment("truth ", b[i].item().toLong());
					nrrd.finish(&cube[0], 48, 48, 16);
					fs.close();
					if(!fs)
						throw std::runtime_error{"Failed to close file."};
				}
			}
			gapr::print(1, "end dump errors");
		}
	}

	void train(BatchGenerator gen, const std::filesystem::path& fn_params) override {
		std::mt19937 rng{std::random_device{}()};

		{
			std::ostringstream oss;
			oss<<*net<<'\n';
			print_params(oss, *net);
			std::cerr<<oss.str();
		}

		net->to(dev);
		net->train();
		constexpr std::size_t dbg_interval{50};
		constexpr std::size_t total_iter{9'000};
		//auto optim=torch::optim::SGD(net->parameters(), torch::optim::SGDOptions{.01});
		//auto optim=torch::optim::Adam(net->parameters(), torch::optim::AdamOptions{.001});
		torch::optim::RMSprop optim{net->parameters(), torch::optim::RMSpropOptions{.001}};

		std::vector<float> samples;
		std::vector<long> labels;
		std::vector<float> label_probs;
		double loss_sum1{0.0};
		double loss_max1{-INFINITY};
		std::size_t loss_cnt{0};
		std::cerr<<"******begin\n";
		auto t0=std::chrono::steady_clock::now();
		std::uniform_real_distribution udist{};
		for(std::size_t iter=1; iter<=total_iter; iter++) {
			auto batch=gen();
			assert(batch.size()%(InputSize+ResponseSize)==0);
			if(batch.size()==0)
				break;

			samples.clear();
			labels.clear();
			label_probs.clear();
			std::size_t count=batch.size()/(InputSize+ResponseSize);
			for(std::size_t I=0; I<count; I++) {
				auto p=&batch[I*(InputSize+ResponseSize)];
				long q=std::lrint(p[InputSize])-1;
				labels.push_back(q);
				double noise[2]={
					udist(rng)*0.05,
					udist(rng)*0.05,
				};
				int noisei=0;
				for(int i=0; i<3; i++)
					label_probs.push_back(i==q?(0.9-noise[0]-noise[1]):(0.05+noise[noisei++]));
				for(unsigned int i=0; i<16*48*48; i++)
					samples.push_back(p[i]);
			}
			auto x=torch::tensor(at::ArrayRef<float>{samples}).view({(long)count, 1, 16, 48, 48}).to(dev);
			auto y=torch::tensor(at::ArrayRef<long>{labels}).view({(long)count}).to(dev);
			auto y_s=torch::tensor(at::ArrayRef<float>{label_probs}).view({(long)count, 3}).to(dev);

			if(iter%dbg_interval==0) {
				net->eval();
				torch::NoGradGuard no_grad{};
				evaluate(x, y, y_s);

				c10::cuda::CUDACachingAllocator::emptyCache();
				net->train();
			}

			optim.zero_grad();
			auto pred=net->forward(x);

			auto loss=get_loss(pred, y, y_s);
			loss.backward();
			optim.step();

			loss_sum1+=loss.template item<float>();
			if(auto v=loss.template item<float>(); v>loss_max1)
				loss_max1=v;
			loss_cnt+=1;
			if(loss_cnt>=dbg_interval) {
				auto t1=std::chrono::steady_clock::now();
				auto dt=std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
				t0=t1;
				std::cout<<"iter: "<<iter<<", loss: "<<loss_sum1/loss_cnt<<' '<<loss_max1<<" time: "<<dt<<"ms\n";
				loss_sum1=0.0;
				loss_max1=-INFINITY;
				loss_cnt=0;
				std::this_thread::sleep_for(std::chrono::seconds{1});
			}
		}
		save_pars(net, fn_params);
		std::cerr<<"******finished\n";
	}

	std::valarray<float> predict(const std::valarray<float>& input) override {
		gpu_lock::begin_session();
		assert(input.size()%InputSize==0);
		std::size_t count=input.size()/InputSize;
		std::valarray<float> outcome(count*OutputSize);

		std::vector<float> samples;
		std::size_t I=0;
		while(I<count) {
			samples.clear();
			auto prev_i=I;
			while(I<prev_i+64 && I<count) {
				auto p=&input[I*InputSize];
				for(unsigned int i=0; i<16*48*48; i++)
					samples.push_back(p[i]);
				I++;
			}

			gpu_lock lock{};
			net->to(dev);
			net->eval();
			torch::NoGradGuard no_grad{};
			auto x=torch::tensor(at::ArrayRef<float>{samples}).view({(long)(I-prev_i), 1, 16, 48, 48}).to(dev);
			auto pred=net->forward(x).exp().to(torch::kCPU);
			lock.unlock();
			auto ptr=pred.template accessor<float, 2>();
			for(std::size_t i=prev_i; i<I; i++) {
				for(unsigned int j=0; j<3; j++)
					outcome[j+OutputSize*(i)]=ptr[i-prev_i][j];
			}
		}
		return outcome;
	}
	void load(std::istream& str) override {
		torch::load(net, str);
	}
};

std::shared_ptr<Evaluator> Evaluator::create() {
	return std::make_shared<EvaluatorImpl>();
}

std::shared_ptr<Evaluator> Evaluator::create_stage1() {
	// XXX
	return std::make_shared<EvaluatorImpl>();
}

