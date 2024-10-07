#include "torch-networks.hh"
#include "torch-helpers.hh"

#include <random>

#include <c10/cuda/CUDACachingAllocator.h>

gapr::nn::ResModuleImpl::ResModuleImpl(int in_chan, int out_chan, int xystride, int zstride, int groups, int factor) {
	int tmp_chan=out_chan/factor;
	torch::nn::Sequential ds{};
	if(in_chan!=out_chan || xystride!=1 || zstride!=1) {
		ds->push_back(torch::nn::Conv3d{torch::nn::Conv3dOptions{in_chan, out_chan, {1, 1, 1}}.stride({zstride, xystride, xystride}).bias(false)});
		ds->push_back(torch::nn::BatchNorm3d{out_chan});
	}
	torch::nn::ReLU relu{torch::nn::ReLUOptions{true}};

	torch::nn::Conv3d conv1{torch::nn::Conv3dOptions{in_chan, tmp_chan, {1, 1, 1}}.bias(false)};
	torch::nn::BatchNorm3d bn1{tmp_chan};

	torch::nn::Conv3d conv2{torch::nn::Conv3dOptions{tmp_chan, tmp_chan, {3, 3, 3}}.stride({zstride, xystride, xystride}).padding({1, 1, 1}).groups(groups).bias(false)};
	torch::nn::BatchNorm3d bn2{tmp_chan};

	torch::nn::Conv3d conv3{torch::nn::Conv3dOptions{tmp_chan, out_chan, {1, 1, 1}}.bias(false)};
	torch::nn::BatchNorm3d bn3{out_chan};

	register_module("list", torch::nn::ModuleList{
		ds, relu,
		conv1, bn1,
		conv2, bn2,
		conv3, bn3,
	});

	forward_impl=[=](torch::Tensor x) mutable ->torch::Tensor {
		auto x1=x;
		if(!ds->is_empty())
			x1=ds->forward(x1);
		//6.3G
		x=relu(bn1(conv1(x)));
		//12G
		x=relu(bn2(conv2(x)));
		x=relu(x1+bn3(conv3(x)));
		return x;
	};
	init=[=]() {
		torch::nn::init::constant_(bn3->weight, 0);
	};
}

gapr::nn::LinResModuleImpl::LinResModuleImpl(int in_chan, int out_chan) {
	torch::nn::Sequential ds{};
	if(in_chan!=out_chan) {
		ds->push_back(torch::nn::Linear{torch::nn::LinearOptions{in_chan, out_chan}.bias(false)});
		ds->push_back(torch::nn::BatchNorm1d{out_chan});
	}
	torch::nn::ReLU relu{torch::nn::ReLUOptions{true}};

	torch::nn::Linear lin1{in_chan, out_chan};
	torch::nn::BatchNorm1d bn1{out_chan};
	torch::nn::Linear lin2{out_chan, out_chan};
	torch::nn::BatchNorm1d bn2{out_chan};

	register_module("list", torch::nn::ModuleList{
		ds, relu,
		lin1, bn1,
		lin2, bn2,
	});

	forward_impl=[=](torch::Tensor x) mutable ->torch::Tensor {
		auto x1=x;
		if(!ds->is_empty())
			x1=ds->forward(x1);
		x=relu(bn1(lin1(x)));
		x=relu(x1+bn2(lin2(x)));
		return x;
	};
}

gapr::nn::EvaluatorImpl::EvaluatorImpl() {
	torch::nn::Conv3d conv1{torch::nn::Conv3dOptions{4, 64, {1, 5, 5}}.stride({1, 2, 2}).bias(false)};
	torch::nn::BatchNorm3d bn1{64};
	torch::nn::ReLU relu{torch::nn::ReLUOptions{true}};

	torch::nn::Sequential layers0{};
	layers0->push_back(ResModule{64, 128, 1, 1, 16, 2});
	for(unsigned int i=1; i<4; ++i)
		layers0->push_back(ResModule{128, 128, 1, 1, 16, 2});
	ResModule ds0{128, 256, 2, 2, 16, 2};

	torch::nn::Sequential layers1{};
	for(unsigned int i=1; i<5; ++i)
		layers1->push_back(ResModule{256, 256, 1, 1, 32, 2});
	ResModule ds1{256, 512, 2, 2, 32, 2};

	torch::nn::Sequential layers2{};
	for(unsigned int i=1; i<7; ++i)
		layers2->push_back(ResModule{512, 512, 1, 1, 32, 2});
	ResModule ds2{512, 1024, 2, 2, 32, 2};

	torch::nn::AdaptiveAvgPool3d pool4{torch::nn::AdaptiveAvgPool3dOptions{{1, 1, 1}}};

	torch::nn::Linear lin5{1024, 1024};
	torch::nn::BatchNorm1d bn5{1024};
	LinResModule lres6{1024, 1024};
	LinResModule lres7{1024, 1024};
	LinResModule lres8{1024, 1024};
	torch::nn::Linear lin9{1024, 3};

	register_module("list", torch::nn::ModuleList{
		conv1, bn1, relu,
		layers0, ds0,
		layers1, ds1,
		layers2, ds2,
		pool4,
		lin5, bn5,
		lres6, lres7, lres8,
		lin9,
	});
	forward=[=](torch::Tensor x) mutable ->torch::Tensor {
		assert_sizes(x, -1, 1, 16, 48, 48);
		x=torch::slice(x, 3, 5, 42);
		x=torch::slice(x, 4, 5, 42);
		x=torch::cat({torch::slice(x, 2, 0, 1), x}, 2);
		auto scales=torch::tensor({1, 8, 64, 512}).view({1, -1, 1, 1,   1}).to(x.device());
		x=scales*(x-x.amin({1, 2, 3, 4}, true));
		x=x.clamp_max_(2).sub_(1);
		assert_sizes(x, -1, 4, 17, 37, 37);

		x=relu(bn1(conv1(x)));
		assert_sizes(x, -1, -1, 17, 17, 17);
		x=layers0->forward(x);
		x=ds0(x);
		assert_sizes(x, -1, -1, 9, 9, 9);
		x=layers1->forward(x);
		x=ds1(x);
		assert_sizes(x, -1, -1, 5, 5, 5);
		x=layers2->forward(x);
		x=ds2(x);
		assert_sizes(x, -1, -1, 3, 3, 3);
		x=pool4(x);
		x=torch::flatten(x, 1);
		x=relu(bn5(lin5(x)));
		x=lres6(x);
		x=lres7(x);
		x=lres8(x);
		x=torch::log_softmax(lin9(x), 1);
		return x;
	};
	for(auto m: modules(false)) {
		if(auto conv=m->as<torch::nn::Conv3d>()) {
			torch::nn::init::kaiming_normal_(conv->weight, 0.0, torch::kFanOut, torch::kReLU);
		} else if(auto bn=m->as<torch::nn::BatchNorm3d>()) {
			torch::nn::init::constant_(bn->weight, 1);
			torch::nn::init::constant_(bn->bias, 0);
		}
	}
	for(auto m: modules(false)) {
		if(auto res=m->as<ResModule>())
			res->init();
	}
}

gapr::nn::ImageProberImpl::ImageProberImpl() {
	torch::nn::Conv3d ds10{torch::nn::Conv3dOptions{4, 16, {1, 5, 5}}.stride({1, 2, 2}).bias(false)};
	torch::nn::BatchNorm3d bn10{16};
	gapr::nn::ResModule res11{16, 16, 1, 1, 1, 2};
	//gapr::nn::ResModule res12{16, 16, 1, 1, 1, 2};
	torch::nn::Conv3d ds20{torch::nn::Conv3dOptions{16, 64, {3, 3, 3}}.stride({2, 2, 2}).bias(false)};
	torch::nn::BatchNorm3d bn20{64};
	gapr::nn::ResModule res21{64, 64, 1, 1, 1, 2};
	gapr::nn::ResModule res22{64, 64, 1, 1, 1, 2};
	gapr::nn::ResModule res23{64, 64, 1, 1, 1, 2};
	torch::nn::Conv3d ds30{torch::nn::Conv3dOptions{64, 256, {3, 3, 3}}.stride({2, 2, 2}).bias(false)};
	torch::nn::BatchNorm3d bn30{256};
	gapr::nn::ResModule res31{256, 256, 1, 1, 2, 2};
	gapr::nn::ResModule res32{256, 256, 1, 1, 2, 2};
	gapr::nn::ResModule res33{256, 256, 1, 1, 2, 2};
	gapr::nn::ResModule res34{256, 256, 1, 1, 2, 2};
	gapr::nn::ResModule res35{256, 256, 1, 1, 2, 2};
	torch::nn::Conv3d ds40{torch::nn::Conv3dOptions{256, 1024, {3, 3, 3}}.stride({2, 2, 2}).bias(false).groups(8)};
	torch::nn::BatchNorm3d bn40{1024};
	gapr::nn::ResModule res41{1024, 1024, 1, 1, 64, 2};
	gapr::nn::ResModule res42{1024, 1024, 1, 1, 64, 2};
	gapr::nn::ResModule res43{1024, 1024, 1, 1, 64, 2};
	gapr::nn::ResModule res44{1024, 1024, 1, 1, 64, 2};
	gapr::nn::ResModule res45{1024, 1024, 1, 1, 64, 2};
	gapr::nn::ResModule res46{1024, 1024, 1, 1, 64, 2};
	gapr::nn::ResModule res47{1024, 1024, 1, 1, 64, 2};
	gapr::nn::ResModule res48{1024, 1024, 1, 1, 64, 2};

	torch::nn::ConvTranspose3d ups50{torch::nn::ConvTranspose3dOptions{1024, 256, {3, 3, 3}}.stride({2, 2, 2}).bias(false).groups(8)};
	torch::nn::BatchNorm3d bn50{256};
	gapr::nn::ResModule res51{256, 256, 1, 1, 2, 2};
	gapr::nn::ResModule res52{256, 256, 1, 1, 2, 2};
	gapr::nn::ResModule res53{256, 256, 1, 1, 2, 2};
	gapr::nn::ResModule res54{256, 256, 1, 1, 2, 2};
	gapr::nn::ResModule res55{256, 256, 1, 1, 2, 2};
	torch::nn::ConvTranspose3d ups60{torch::nn::ConvTranspose3dOptions{512, 64, {3, 3, 3}}.stride({2, 2, 2}).bias(false)};
	torch::nn::BatchNorm3d bn60{64};
	gapr::nn::ResModule res61{64, 64, 1, 1, 1, 2};
	gapr::nn::ResModule res62{64, 64, 1, 1, 1, 2};
	gapr::nn::ResModule res63{64, 64, 1, 1, 1, 2};
	torch::nn::ConvTranspose3d ups70{torch::nn::ConvTranspose3dOptions{128, 16, {3, 3, 3}}.stride({2, 2, 2}).bias(false)};
	torch::nn::BatchNorm3d bn70{16};
	gapr::nn::ResModule res71{16, 16, 1, 1, 1, 2};
	torch::nn::ConvTranspose3d ups80{torch::nn::ConvTranspose3dOptions{32, 4, {3, 5, 5}}.stride({2, 2, 2}).bias(false)};
	torch::nn::BatchNorm3d bn80{4};
	gapr::nn::ResModule res81{4, 4, 1, 1, 1, 2};
	torch::nn::Conv3d con9{torch::nn::Conv3dOptions{4, 2, {3, 3, 3}}.stride({1, 1, 1}).padding(1).bias(false)};
	torch::nn::ReLU relu{torch::nn::ReLUOptions{true}};

	register_module("list", torch::nn::ModuleList{
		ds10, bn10,
		res11,
		ds20, bn20,
		res21, res22, res23,
		ds30, bn30,
		res31, res32, res33, res34, res35,
		ds40, bn40,
		res41, res42, res43, res44, res45, res46, res47, res48,
		ups50, bn50,
		res51, res52, res53, res54, res55,
		ups60, bn60,
		res61, res62, res63,
		ups70, bn70,
		res71,
		ups80, bn80,
		res81,
		con9,
		relu,
	});

	auto print_snapshot=[](const char* id) {
		if(true)
			return;
		using namespace c10::cuda::CUDACachingAllocator;
		auto segs=snapshot();
		char fnbuf[1024];
		snprintf(fnbuf, 1024, "/tmp/gapr-debug-memory-%s.txt", id);
		FILE* f=fopen(fnbuf, "a");
		fprintf(f, "=======================================\n");
		int64_t total{0};
		for(auto& seg: segs) {
			fprintf(f, "- segment: dev=%ld size=%ld/%ld*%ld addr=%ld %c\n",
				seg.device, seg.total_size, seg.allocated_size,
				seg.active_size,
				seg.address, seg.is_large?'L':'s');
			for(auto& blk: seg.blocks) {
				fprintf(f, "  -- block: %ld %c %c\n", blk.size,
					blk.allocated?'A':'a',
					blk.active?'*':'!');
			}
			//5.9G total+=seg.allocated_size;
			total+=seg.total_size;
		}
		fprintf(f, "- total: %ld\n", total);
		fclose(f);
	};

	std::chrono::steady_clock::time_point _tp0, _tpp;
	auto timer_time=[_tp0,_tpp,this](const char* tag) mutable {
		if(false && !is_training()) {
			if(!tag) {
				_tp0=std::chrono::steady_clock::now();
				_tpp=_tp0;
				return;
			}
			//cudaStreamSynchronize(nullptr);

			auto tp=std::chrono::steady_clock::now();
			auto dt1=std::chrono::duration_cast<std::chrono::microseconds>(tp-_tp0).count()/1000.0;
			auto dt2=std::chrono::duration_cast<std::chrono::microseconds>(tp-_tpp).count()/1000.0;
			_tpp=tp;
			fprintf(stdout, "timer +%06.3lf %06.3lf %s\n", dt2, dt1, tag);
		}
	};

	forward=[=](torch::Tensor x) mutable ->torch::Tensor {
		timer_time(nullptr);
		_tmp_con=ds10->weight.clone();
		_tmp_w=bn10->weight.clone();
		_tmp_b=bn10->bias.clone();
		//if(is_training())
			//assert_sizes(x, -1, 1, image_prober_d, image_prober_wh, image_prober_wh);
#if 1
		auto scales=torch::tensor({1, 8, 64, 512}).view({1, -1, 1, 1, 1}).to(x.device());
		auto xmin=x;
		xmin=torch::masked_fill(xmin, torch::less(xmin, -1), 1);
		xmin=xmin.amin({1, 2, 3, 4}, true);
		x=scales*(x-xmin);
		x=x.clamp_(0, 2).sub_(1);
		_tmp_x63=x;
#endif
		timer_time("prepare");
		print_snapshot("xxx000");
		x=relu(bn10(ds10(x)));
		print_snapshot("xxx010");
		_tmp_ds1=x;
		//if(is_training())
			//assert_sizes(x, -1, -1, image_prober_d, 63, 63);
		timer_time("ds10");
		auto x63=x=res11(x);
		print_snapshot("xxx011");
		timer_time("res1");

		x=relu(bn20(ds20(x)));
		print_snapshot("xxx020");
		//if(is_training())
		//assert_sizes(x, -1, -1, 23, 31, 31);
		timer_time("ds20");
		auto x31=x=res23(res22(res21(x)));
		print_snapshot("xxx021");
		timer_time("res2");

		x=relu(bn30(ds30(x)));
		print_snapshot("xxx030");
		//if(is_training())
		//assert_sizes(x, -1, -1, 11, 15, 15);
		timer_time("ds30");
		auto x15=x=res35(res34(res33(res32(res31(x)))));
		print_snapshot("xxx031");
		timer_time("res3");

		x=relu(bn40(ds40(x)));
		print_snapshot("xxx040");
		//if(is_training())
		//assert_sizes(x, -1, -1, 5, 7, 7);
		timer_time("ds40");
		x=res48(res47(res46(res45(res44(res43(res42(res41(x))))))));
		print_snapshot("xxx041");
		timer_time("res4");

		x=relu(bn50(ups50(x)));
		//if(is_training())
		//assert_sizes(x, -1, -1, 11, 15, 15);
		print_snapshot("xxx051");
		timer_time("ups50");
		x=res55(res54(res53(res52(res51(x)))));
		print_snapshot("xxx052");
		timer_time("res5");

		x=torch::cat({x, x15}, 1);
		x=relu(bn60(ups60(x)));
		//if(is_training())
		//assert_sizes(x, -1, -1, 23, 31, 31);
		print_snapshot("xxx061");
		timer_time("ups60");
		x=res63(res62(res61(x)));
		print_snapshot("xxx062");
		timer_time("res6");

		x=torch::cat({x, x31}, 1);
		x=relu(bn70(ups70(x)));
		//if(is_training())
		//assert_sizes(x, -1, -1, image_prober_d, 63, 63);
		print_snapshot("xxx071");
		timer_time("ups70");
		x=res71(x);
		print_snapshot("xxx072");
		timer_time("res7");

		x=torch::cat({x, x63}, 1);
		x=relu(bn80(ups80(x)));
		//if(is_training())
		//assert_sizes(x, -1, -1, image_prober_d, image_prober_wh, image_prober_wh);
		_tmp_ups=x;
		print_snapshot("xxx081");
		timer_time("ups80");
		x=res81(x);
		print_snapshot("xxx082");
		timer_time("res8");
		_tmp_res2=x;

		x=con9(x);
		x=torch::cat({torch::sigmoid(torch::slice(x, 1, 0, 1)), torch::relu(torch::slice(x, 1, 1, 2))}, 1);
		print_snapshot("xxx091");
		timer_time("finish");
		return x;
	};
#if 1
	for(auto m: modules(false)) {
		if(auto conv=m->as<torch::nn::Conv3d>()) {
			torch::nn::init::kaiming_normal_(conv->weight, 0.0, torch::kFanOut, torch::kReLU);
		} else if(auto bn=m->as<torch::nn::BatchNorm3d>()) {
			torch::nn::init::constant_(bn->weight, 1);
			torch::nn::init::constant_(bn->bias, 0);
		}
	}
	for(auto m: modules(false)) {
		if(auto res=m->as<ResModule>())
			res->init();
	}
#endif
#if 1
	{
#if 0
		std::vector<float> kernels;
		kernels.reserve(16*5*5);
		for(unsigned int z=0; z<16; ++z) {
			auto a=M_PI/4*z;
			for(int x=-2; x<=2; ++x) {
				for(int y=-2; y<=2; ++y) {
					auto p=(std::cos(a)*x+std::sin(a)*y)*M_PI/3;
					double v=std::exp(-(x*x+y*y)/3)*std::sin(p);
					kernels.push_back(v);
				}
			}
		}
		torch::NoGradGuard nograd;
		ds10->weight.copy_(torch::tensor(torch::ArrayRef<float>(kernels)).view(ds10->weight.sizes()));
		for(unsigned int z=0; z<16; ++z)
			bn10->weight[z]=std::pow(100.0, z/8);
		torch::nn::init::zeros_(bn10->bias);
#endif
		torch::NoGradGuard nograd;
		auto& w=ds10->weight;
		assert_sizes(w, -1, -1, 1, 5, 5);
		for(int i=0; i<w.size(0); ++i) {
			for(int j=0; j<w.size(1); ++j) {
				for(int x=0; x<5; ++x) {
					for(int y=0; y<5; ++y) {
						if(std::abs(x-2)>=2 || std::abs(y-2)>=2)
							w[i][j][0][y][x]=0;
					}
				}
			}
		}
	}
#endif
}

static std::mutex _lck_gpu;
static std::unordered_set<std::condition_variable*> _active_cvs;
static std::list<std::pair<std::thread::id, std::condition_variable>> _per_thr;
static std::thread::id _active_thr{};

void gpu_lock::begin_session() {
	auto myid=std::this_thread::get_id();
	std::lock_guard lck{_lck_gpu};
	auto prev=std::move(_per_thr);
	bool hit{false};
	for(auto it=prev.begin(); it!=prev.end(); ++it) {
		if(it->first==myid) {
			hit=true;
			_per_thr.splice(_per_thr.end(), prev, it);
			break;
		}
	}
	_per_thr.splice(_per_thr.begin(), std::move(prev));
	if(!hit)
		_per_thr.emplace_back().first=myid;
	assert(_per_thr.back().first==myid);
}

void gpu_lock::lock() {
	if(_id!=std::thread::id{}) {
		assert(_id==std::this_thread::get_id());
		return;
	}
	auto myid=std::this_thread::get_id();
	std::unique_lock lck{_lck_gpu};
	std::condition_variable* cv{nullptr};
	for(auto& p: _per_thr) {
		if(p.first==myid) {
			cv=&p.second;
			break;
		}
	}
	assert(cv);
	auto [it, ins]=_active_cvs.emplace(cv);
	assert(ins);
	while(_active_thr!=std::thread::id{})
		cv->wait(lck);
	it=_active_cvs.find(cv);
	assert(it!=_active_cvs.end());
	_active_cvs.erase(it);
	_active_thr=myid;
	lck.unlock();
	_id=myid;
	//c10::cuda::CUDACachingAllocator::emptyCache();
}
void gpu_lock::unlock() {
	assert(_id!=std::thread::id{});
	auto myid=std::this_thread::get_id();
	assert(_id==myid);
	std::condition_variable* cv{nullptr};
	_id=std::thread::id{};
	{
		std::lock_guard lck{_lck_gpu};
		assert(_active_thr==myid);
		_active_thr=std::thread::id{};
		for(auto& p: _per_thr) {
			auto it=_active_cvs.find(&p.second);
			if(it!=_active_cvs.end()) {
				assert(p.first!=_id);
				cv=&p.second;
				//_active_cvs.erase(it);
				break;
			}
		}
	}
	if(cv)
		cv->notify_all();
}

