#include "evaluator.hh"

#include <dlib/dnn.h>

template<typename T>
static std::array<dlib::matrix<float,48,48>,16> to_samp(const T* p) {
	std::array<dlib::matrix<float,48,48>,16> samp;
	for(unsigned int k=0; k<16; k++) {
		for(unsigned int y=0; y<48; y++) {
			for(unsigned int x=0; x<48; x++) {
				samp[k](y, x)=p[x+y*48+k*(48*48)];
			}
		}
	}
	return samp;
}

template<typename Impl>
struct EvaluatorImpl: Evaluator {
	typename Impl::FFN net;
	explicit EvaluatorImpl(): Evaluator{}, net{} {
		Impl::build(net);
	}

	void train(const std::valarray<float>& input, const std::valarray<float>& outcome, std::size_t count) override {
		assert(input.size()==count*Impl::InputSize);
		assert(outcome.size()==count*Impl::ResponseSize);

		const double initial_learning_rate=0.1;
		const double weight_decay=0.0001;
		const double momentum=0.9;

		dlib::dnn_trainer<typename Impl::FFN> trainer(net, dlib::sgd(weight_decay, momentum));
		trainer.be_verbose();
		trainer.set_learning_rate(initial_learning_rate);
		trainer.set_synchronization_file("dlib-trainer-sync-file.dat", std::chrono::minutes(10));
		trainer.set_iterations_without_progress_threshold(1024);
		dlib::set_all_bn_running_stats_window_sizes(net, 256);
		if(trainer.get_learning_rate()<initial_learning_rate*.5)
			trainer.set_learning_rate(initial_learning_rate*.5);
		std::cerr<<net<<'\n';
		std::cerr<<trainer<<'\n';

		std::vector<std::array<dlib::matrix<float,48,48>,16>> samples;
		std::vector<unsigned long> labels;
		std::size_t I=0;
		std::size_t K=0;
		while(trainer.get_learning_rate()>=initial_learning_rate*1e-3) {
			samples.clear();
			labels.clear();

			while(samples.size()<32) {
				auto p=&input[I*Impl::InputSize];
				unsigned long q=outcome[I]-1;
				samples.emplace_back(to_samp(p));
				labels.push_back(q);
				I=(I+1)%count;
			}
			if((++K)%16==0) {
				trainer.test_one_step(samples, labels);
			}
			trainer.train_one_step(samples, labels);
		}
		trainer.get_net();
		net.clean();
	}
	std::valarray<float> predict(const std::valarray<float>& input, std::size_t count) const override {
		assert(input.size()==count*Impl::InputSize);
		std::valarray<float> outcome(count*Impl::OutputSize);
		std::vector<std::array<dlib::matrix<float,48,48>,16>> samples;
		std::size_t I=0;
		while(I<count) {
			samples.clear();
			auto prev_i=I;
			while(samples.size()<64 && I<count) {
				auto p=&input[I*Impl::InputSize];
				samples.emplace_back(to_samp(p));
				I++;
			}
			auto& ret=const_cast<typename Impl::FFN&>(net).subnet()(samples.begin(), samples.end());
			assert(ret.size()==Impl::OutputSize*samples.size());
			auto q=ret.host();
			for(std::size_t i=0; i<samples.size(); i++) {
				for(unsigned int j=0; j<3; j++)
					outcome[j+Impl::OutputSize*(i+prev_i)]=q[j+Impl::OutputSize*(i+prev_i)];
			}
		}
		return outcome;
	}
	void load(std::streambuf& sbuf) override {
		std::istream str{&sbuf};
		deserialize(net, str);
		if(!str)
			throw std::runtime_error{"failed to read size"};
	}
	void save(std::streambuf& sbuf) const override {
		std::ostream str{&sbuf};
		serialize(net, str);
		if(!str)
			throw std::runtime_error{"failed to write size"};
	}
};

struct ImplClassify {

	template <template <int,template<typename>class,int,typename> class block, int N, template<typename>class BN, typename SUBNET>
		using residual = dlib::add_prev1<block<N,BN,1,dlib::tag1<SUBNET>>>;

	template <template <int,template<typename>class,int,typename> class block, int N, template<typename>class BN, typename SUBNET>
		using residual_down = dlib::add_prev2<dlib::avg_pool<2,2,2,2,dlib::skip1<dlib::tag2<block<N,BN,2,dlib::tag1<SUBNET>>>>>>;

	template <int N, template <typename> class BN, int stride, typename SUBNET> 
		using block  = BN<dlib::con<N,3,3,1,1,dlib::relu<BN<dlib::con<N,3,3,stride,stride,SUBNET>>>>>;

	template <int N, typename SUBNET> using res       = dlib::relu<residual<block,N,dlib::bn_con,SUBNET>>;
	template <int N, typename SUBNET> using ares      = dlib::relu<residual<block,N,dlib::affine,SUBNET>>;
	template <int N, typename SUBNET> using res_down  = dlib::relu<residual_down<block,N,dlib::bn_con,SUBNET>>;
	template <int N, typename SUBNET> using ares_down = dlib::relu<residual_down<block,N,dlib::affine,SUBNET>>;

	template <typename SUBNET> using level1 = SUBNET;
	template <typename SUBNET> using level2 = SUBNET;
	template <typename SUBNET> using level3 = res<32,res<32,res<32,res_down<32,SUBNET>>>>;
	template <typename SUBNET> using level4 = res<16,res<16,SUBNET>>;

	template <typename SUBNET> using alevel1 = SUBNET;
	template <typename SUBNET> using alevel2 = SUBNET;
	template <typename SUBNET> using alevel3 = ares<32,ares<32,ares<32,ares_down<32,SUBNET>>>>;
	template <typename SUBNET> using alevel4 = ares<16,ares<16,SUBNET>>;

	using input_type=dlib::input<std::array<dlib::matrix<float,48,48>,16>>;

	using net_type = dlib::loss_multiclass_log<dlib::fc<3,dlib::avg_pool_everything<
		level1<
		level2<
		level3<
		level4<
		dlib::max_pool<3,3,2,2,dlib::relu<dlib::bn_con<dlib::con<16,6,6,1,1,
		input_type>>>>>>>>>>>;
	using anet_type = dlib::loss_multiclass_log<dlib::fc<3,dlib::avg_pool_everything<
		alevel1<
		alevel2<
		alevel3<
		alevel4<
		dlib::max_pool<3,3,2,2,dlib::relu<dlib::affine<dlib::con<16,6,6,1,1,
		input_type>>>>>>>>>>>;

	using FFN=net_type;
	static void build(FFN& ffn) {
	}
	static constexpr const std::size_t InputSize=48*48*16;
	static constexpr const std::size_t OutputSize=3;
	static constexpr const std::size_t ResponseSize=1;
};


std::shared_ptr<Evaluator> Evaluator::create() {
	return std::make_shared<EvaluatorImpl<ImplClassify>>();
}

