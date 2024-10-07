#include <charconv>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <random>
#include <regex>
#include <thread>

#include "gapr/cube-builder.hh"
#include "gapr/swc-helper.hh"
#include "gapr/bbox.hh"
#include "gapr/utility.hh"

#include "evaluator.hh"
#include "utils.hh"

#include <boost/asio/io_context.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>

//#include <c10/cuda/CUDACachingAllocator.h>

constexpr std::size_t static MAX_NEIGH=16;


namespace {

struct ImageSampler {
	static constexpr unsigned int Width{37};
	static constexpr unsigned int Height{37};
	static constexpr unsigned int Depth{17};
	static constexpr unsigned int Chan{1};
	static constexpr unsigned int SampleSize=Width*Height*Depth*Chan;

	std::thread _io_thread;
	boost::asio::io_context io_ctx{1};
	std::shared_ptr<gapr::cube_builder> _builder;

	gapr::cube _cur_cube;
	std::array<unsigned int, 3> _cur_offset;
	unsigned int _cur_chan{0};

	std::optional<std::promise<gapr::cube>> _cur_prom;

	std::vector<gapr::cube_info> cube_infos;

	//std::mutex _mtx;
	//std::conditional_variable _cv;
	explicit ImageSampler() {
		_io_thread=std::thread{[this]() { run_builder(); }};

	}
	void join() {
		io_ctx.stop();
		_io_thread.join();
	}
	void run_builder() {
		boost::asio::thread_pool pool{2};
		_builder=std::make_shared<gapr::cube_builder>(io_ctx.get_executor(), pool);
		wait_for_cubes(*_builder);
		io_ctx.run();
	}
	void wait_for_cubes(gapr::cube_builder& builder) {
		auto wg=boost::asio::require(io_ctx.get_executor(), boost::asio::execution::outstanding_work.tracked);
		builder.async_wait([&builder,this,wg=std::move(wg)](std::error_code ec, int progr) {
			std::cerr<<"builder info\n";
			if(ec) {
				std::cerr<<"builder info ec\n";
				std::cerr<<"error get cube\n";
				auto prom=std::move(_cur_prom);
				if(prom) {
					auto [cube, chan, offset, uri]=builder.get();
					prom->set_exception(std::make_exception_ptr(std::runtime_error{"error get cube"}));
				}
			}
			if(!ec && progr==1001) {
				std::cerr<<"builder info 1001\n";
				auto [cube, chan, offset, uri]=builder.get();
				_cur_cube=std::move(cube);
				_cur_chan=chan;
				_cur_offset=offset;
				_cur_bbox=calc_bbox(offset, _cur_cube.view<void>().sizes(), cube_infos[chan-1].xform);
				auto prom=std::move(_cur_prom);
				prom->set_value(_cur_cube);
			}
			wait_for_cubes(builder);
		});
	}

	gapr::bbox _cur_bbox;

	std::unique_ptr<float[]> sample_seed(gapr::vec3<double>& pos, unsigned int chan) {
		std::promise<gapr::cube> prom;
		auto fut=prom.get_future();
		boost::asio::post(io_ctx, [this,pos,chan,prom=std::move(prom)]() mutable {
			if(_cur_cube && _cur_chan==chan) {
				auto abs_node_off=cube_infos[_cur_chan-1].xform.to_offset(pos);
				if(check_inside<Width, Height, Depth>(abs_node_off, _cur_cube.view<void>().sizes(), _cur_offset))
					return prom.set_value(_cur_cube);
				if(!cube_infos[chan-1].is_pattern())
					return prom.set_value(_cur_cube);
				if(!_builder->build(chan, cube_infos[chan-1], pos, true, &_cur_offset))
					return prom.set_value(_cur_cube);
			} else {
				if(cube_infos[chan-1].is_pattern()) {
					auto r=_builder->build(chan, cube_infos[chan-1], pos, true, nullptr);
					assert(r);
				} else {
					_builder->build(chan, cube_infos[chan-1]);
				}
			}
			_cur_prom=std::move(prom);
		});
		auto cube=fut.get();
		assert(cube);
		assert(_cur_chan==chan);
		return sample_cur(pos, true);
	}
	gapr::cube get_cube(const std::array<unsigned int, 3>& offset, unsigned int chan) {
		unsigned int retrycnt=0;
		while(true) {
			std::promise<gapr::cube> prom;
			auto fut=prom.get_future();
			boost::asio::post(io_ctx, [this,offset,chan,prom=std::move(prom)]() mutable {
				if(_cur_cube && _cur_chan==chan) {
					if(!cube_infos[chan-1].is_pattern())
						return prom.set_value(_cur_cube);
					if(offset==_cur_offset)
						return prom.set_value(_cur_cube);
					_builder->build(chan, cube_infos[chan-1], offset, true);
				} else {
					if(cube_infos[chan-1].is_pattern()) {
						_builder->build(chan, cube_infos[chan-1], offset, true);
					} else {
						_builder->build(chan, cube_infos[chan-1]);
					}
				}
				_cur_prom=std::move(prom);
			});
			try {
				auto cube=fut.get();
				assert(cube);
				assert(_cur_chan==chan);
				return cube;
			} catch(const std::runtime_error& e) {
				std::this_thread::sleep_for(std::chrono::seconds{100});
				std::cerr<<e.what()<<"\n";
				if(++retrycnt>10)
					throw;
			}
		}
	}
	std::unique_ptr<float[]> sample_cur(gapr::vec3<double>& pos, bool skip_chk) {
		std::array<unsigned int, 3> zero{0, 0, 0};
		return Sampler<Width, Height, Depth, 1>{}(cube_infos[_cur_chan-1].xform, _cur_cube.view<void>(), cube_infos[_cur_chan-1].is_pattern()?_cur_offset:zero, pos, skip_chk);
	}
	std::unique_ptr<float[]> sample(gapr::vec3<double>& pos, unsigned int chan) {
		if(chan!=_cur_chan)
			return {};
		if(!check_inside(_cur_bbox, pos))
			return {};
		return sample_cur(pos, false);
	}
};

/*! swc stuff */
using Pos=gapr::vec3<double>;
using Node=gapr::swc_node;
using Tree=std::vector<Node>;
struct ExtendedNode {
	Node base;
	unsigned int tree_id;
	unsigned int chan_id;
	std::unique_ptr<float[]> samp;
	std::array<double, 3> samp_shift;
	unsigned int cov{0};

	std::unique_ptr<std::array<float, 1024>> _vec, _vec2;
	auto& vec() {
		if(!_vec)
			_vec=std::make_unique<std::array<float, 1024>>();
		return *_vec;
	}
	auto& vec2() {
		if(!_vec2)
			_vec2=std::make_unique<std::array<float, 1024>>();
		return *_vec2;
	}
	unsigned int num_ds{0};
	std::array<std::pair<std::size_t, bool>, 2*MAX_NEIGH> neighbors;
	bool is_noise{false};

	explicit ExtendedNode() =default;
};
struct Dataset {
	unsigned int chan_id;
	std::size_t ni_start, ni_end;
	std::string swc_file;
	unsigned int noise;
	explicit Dataset() =default;
};

struct Helper {
	struct FileItem {
		std::ifstream fs;
		std::filesystem::path fn;
		int lineno{0};
	};
	std::vector<FileItem> files_stk;

	double fix_xres, fix_yres, fix_zres;

	std::shared_ptr<ImageSampler> sampler;
	std::shared_ptr<Detector> prober{nullptr};

	std::unordered_map<unsigned int, bool> _force_chan;

	std::vector<ExtendedNode> metatree;
	std::vector<Dataset> datasets;
	std::random_device rng{};

	explicit Helper() {
		sampler=std::make_shared<ImageSampler>();
		prober=Detector::create();
	}
	void finish() {
		sampler->join();
	}

	auto get_pfx() const { return files_stk.back().fn.parent_path(); }
	void push_data(const char* datafile) {
		std::string cat_file{};
		std::vector<std::string> swc_files{};
		struct Pars {
			unsigned int use_r;
			unsigned int repeat;
			unsigned int force;
		};
		std::vector<Pars> pars_stk{};
		{
			assert(files_stk.empty());
			assert(pars_stk.empty());
			auto& item=files_stk.emplace_back();
			item.fn=datafile;
			item.fs=std::ifstream{item.fn};
			if(!item.fs)
				throw std::runtime_error{"failed to open file"};
			pars_stk.emplace_back(Pars{1,1,0});
		}
		std::string line;
		std::regex reg{"^(\\w+)(\\s+(.*))?$"};
		do {
			auto& [fs, fn_cur, lineno]=files_stk.back();
			if(!std::getline(fs, line)) {
				if(!fs.eof())
					throw std::runtime_error{"failed to getline()"};
				files_stk.pop_back();
				pars_stk.pop_back();
				continue;
			}
			++lineno;
			if(line.empty())
				continue;
			if(line[0]=='#')
				continue;
			std::smatch mat;
			if(!regex_match(line, mat, reg))
				throw std::runtime_error{"wrong line"};
			auto n=mat[1].str();
			if(mat[2].length()==0) {
				if(n=="add_sample") {
					if(swc_files.empty())
						throw std::runtime_error{"no swc file"};
					if(cat_file.empty())
						throw std::runtime_error{"no cat file"};

					for(unsigned int kkk=0; kkk<pars_stk.back().repeat; ++kkk) {
						auto chan=load_catalog(cat_file, pars_stk.back().force);

						for(auto& swc_file: swc_files) try {
							std::cerr<<"add sample "<<swc_file<<"\n";
							add_metatree(swc_file, chan, pars_stk.back().use_r);
						} catch(const std::runtime_error& e) {
							std::cerr<<"failed to load swc: "<<swc_file<<"\n";
						}
						load_catalog_end(chan);
					}
					swc_files.clear();
					cat_file.clear();
				} else if(n=="push") {
					pars_stk.push_back(pars_stk.back());
				} else if(n=="pop") {
					pars_stk.pop_back();
				} else {
					throw std::runtime_error{"unknown cmd"};
				}
			} else {
				if(n=="swc_file") {
					if(false && !swc_files.empty())
						throw std::runtime_error{"multiple swc file"};
					swc_files.push_back(get_pfx()/mat[3].str());
				} else if(n=="cat_file") {
					if(!cat_file.empty())
						throw std::runtime_error{"multiple cat file"};
					cat_file=get_pfx()/mat[3].str();
				} else if(n=="include") {
					auto pfx=get_pfx();
					auto& item=files_stk.emplace_back();
					item.fn=pfx/mat[3].str();
					item.fs=std::ifstream{item.fn};
					pars_stk.push_back(pars_stk.back());
				} else if(n=="use_r") {
					auto s=mat[3];
					auto [p, ec]=std::from_chars(&*s.first, &*s.second, pars_stk.back().use_r, 10);
					if(ec!=std::errc{} || p!=&*s.second)
						throw std::runtime_error{"error line"};
				} else if(n=="repeat") {
					auto s=mat[3];
					auto [p, ec]=std::from_chars(&*s.first, &*s.second, pars_stk.back().repeat, 10);
					if(ec!=std::errc{} || p!=&*s.second)
						throw std::runtime_error{"error line"};
				} else if(n=="force") {
					auto s=mat[3];
					auto [p, ec]=std::from_chars(&*s.first, &*s.second, pars_stk.back().force, 10);
					if(ec!=std::errc{} || p!=&*s.second)
						throw std::runtime_error{"error line"};
				} else {
					throw std::runtime_error{"unknown var"};
				}
			}
		} while(!files_stk.empty());
	}

	unsigned int load_catalog(const std::string& path, unsigned int force) {
		auto chan=load_catalog_impl(path);
		auto& xform=sampler->cube_infos[chan-1].xform;
		fix_xres=xform.direction[0];
		fix_yres=xform.direction[4];
		fix_zres=xform.direction[8];
		xform.direction[0]=1;
		xform.direction[4]=1;
		xform.direction[8]=1;
		xform.update_direction_inv();
		_force_chan[chan-1]=force;
		return chan;
	}
	void load_catalog_end(unsigned int chan) {
		prepare_sliding_cubes(chan);
	}
	void add_metatree(const std::string& swc_file, unsigned int chan, bool use_r);
	unsigned int load_catalog_impl(const std::string& path);

	void prepare_sliding_cubes(unsigned int chan_id);
	std::vector<std::array<std::valarray<float>, 2>> prepare_prober_data();
	void prepare_sliding_cube_sel() {
		for(std::size_t i=0; i<sliding_cubes.size(); ++i) {
			if(sliding_cubes[i].is_busy) {
				++sliding_cubes[i].cov;
				sliding_cubes[i].is_busy=false;
			}
		}
	}
	void print_training_stats();
	void train_probe(const std::filesystem::path& fn_params) {
		print_training_stats();

		if(std::filesystem::exists(fn_params)) {
			std::ifstream str{fn_params};
			prober->load(str);
		}

		unsigned int selected_coverage=9999;
		std::size_t selected_i=0;
		std::vector<std::array<std::valarray<float>, 2>> selected_subimages;
		auto gen=[&selected_i,&selected_coverage,&selected_subimages,this]() ->Detector::Batch {
			if(selected_i==0) {
				if(selected_coverage+1>=2) {
					selected_subimages.clear();
					selected_subimages=prepare_prober_data();
					selected_i=0;
					selected_coverage=0;
				} else {
					std::shuffle(selected_subimages.begin(), selected_subimages.end(), rng);
					selected_i=0;
					++selected_coverage;
					fprintf(stderr, "----- reuse sample: cnt=%zu\n", selected_subimages.size());
				}
			}
			std::vector<float> input_imgs_;
			std::vector<float> target_imgs_;

			unsigned int n_bat{0};
			do {
				auto& [cur_input, cur_target]=selected_subimages[selected_i++];
				if(selected_i==selected_subimages.size())
					selected_i=0;

				for(auto v: cur_input)
					input_imgs_.push_back(v);
				for(auto v: cur_target)
					target_imgs_.push_back(v==0?0.0f:1.0f);
				for(auto v: cur_target)
					target_imgs_.push_back((v==0||v==-1)?0.0f:v);
				++n_bat;
			} while(n_bat<4 && selected_i!=0);

			//[[maybe_unused]] auto n_bat2=n_bat>0?n_bat:1;
			return {std::move(input_imgs_), std::move(target_imgs_), n_bat};
		};
		auto res=gen();
		prober->train(rng(), gen, fn_params);
	}

	struct SlidingCube {
		unsigned int chan_id;
		std::array<unsigned int, 3> offset;

		std::array<unsigned int, 3> sizes;
		bool is_busy{false};
		unsigned int cov{0};
		std::vector<std::size_t> nodes;
	};
	std::vector<SlidingCube> sliding_cubes;
	std::pair<std::size_t, std::vector<std::size_t>> sliding_cube_sel(std::size_t min_node_cnt, std::size_t max_cube_size) {
		do {
			unsigned int min_cov{99999999};
			for(std::size_t i=0; i<sliding_cubes.size(); ++i) {
				if(!sliding_cubes[i].is_busy && min_cov>sliding_cubes[i].cov)
					min_cov=sliding_cubes[i].cov;
			}
			std::vector<std::size_t> cubes;
			for(std::size_t i=0; i<sliding_cubes.size(); ++i) {
				if(!sliding_cubes[i].is_busy && min_cov==sliding_cubes[i].cov)
					cubes.push_back(i);
			}
			auto seed=cubes[rng()%cubes.size()];
			auto& cube_spec=sliding_cubes[seed];
			std::vector<std::size_t> nodes;
			auto& cube_info=sampler->cube_infos[cube_spec.chan_id-1];
			gapr::bbox cur_bbox=calc_bbox(cube_spec.offset, cube_spec.sizes, cube_info.xform);
			for(std::size_t i=0; i<metatree.size(); ++i) {
				auto& nn=metatree[i];
				if(nn.chan_id!=cube_spec.chan_id)
					continue;
				if(!nn.is_noise && check_inside(cur_bbox, nn.base.pos)) {
					auto abs_node_off=cube_info.xform.to_offset(nn.base.pos);
					if(check_inside<0, 0, 0>(abs_node_off, cube_spec.sizes, cube_spec.offset))
						nodes.push_back(i);
				}
			}
			if(nodes.size()<min_node_cnt && !_force_chan.at(cube_spec.chan_id-1)) {
				cube_spec.cov+=9999;
				fprintf(stderr, "<");
				continue;
			}
			if(std::size_t{cube_spec.sizes[0]}*cube_spec.sizes[1]*cube_spec.sizes[2]>max_cube_size) {
				cube_spec.cov+=9999;
				fprintf(stderr, ">");
				continue;
			}
			fprintf(stderr, "----- sample: loc=%s\n----- offset=[%u,%u,%u], node_cnt=%zd/%zd, cubes_left=%zd, cur_cov=%u\n", cube_info.location().c_str(), cube_spec.offset[0], cube_spec.offset[1], cube_spec.offset[2], nodes.size(), cube_spec.nodes.size(), cubes.size(), cube_spec.cov);
			cube_spec.is_busy=true;
			return {seed, std::move(nodes)};
		} while(true);
	}
};

}

std::vector<std::array<std::valarray<float>, 2>> Helper::prepare_prober_data() {
	prepare_sliding_cube_sel();
	std::vector<std::array<std::valarray<float>, 2>> images;
	images.clear();

	for(unsigned int k=0; k<4; ++k) {
		auto [cube_idx, nodes]=sliding_cube_sel(1536, 1500'000'000);
		auto& cube_spec=sliding_cubes[cube_idx];
		auto& cube_info=sampler->cube_infos[cube_spec.chan_id-1];

		auto cube=sampler->get_cube(cube_spec.offset, cube_spec.chan_id);
		assert(cube);
		auto input_cube=cube.view<void>();

		std::vector<float> target_cube(cube_spec.sizes[0]*cube_spec.sizes[1]*(cube_spec.sizes[2]*2+1), 0.0);
		auto paint_target=[&cube_info,&cube_spec,&target_cube](const gapr::vec3<double>& node_pos, int node_type, float rad) {
			auto abs_off=cube_info.xform.to_offset_f(node_pos);
			abs_off[2]=abs_off[2]*2+1;
			std::array<unsigned int, 3> off;
			for(unsigned int i=0; i<3; i++)
				off[i]=abs_off[i]-cube_spec.offset[i];
			off[2]-=cube_spec.offset[2];
			for(int dz=-0; dz<=0; ++dz) {
				if(off[2]+dz<0 || off[2]+dz>=(cube_spec.sizes[2]*2+1))
					continue;
				for(int dy=-0; dy<=0; ++dy) {
					if(off[1]+dy<0 || off[1]+dy>=cube_spec.sizes[1])
						continue;
					auto ptr=&target_cube[((off[2]+dz)*cube_spec.sizes[1]+off[1]+dy)*cube_spec.sizes[0]];
					for(int dx=-0; dx<=0; ++dx) {
						if(off[0]+dx<0 || off[0]+dx>=cube_spec.sizes[0])
							continue;
						auto v=rad==0?-1.0f:std::log(1+rad);
						auto& vv=ptr[dx+off[0]];
						if(vv==0) {
							vv=v;
						} else if(vv<v) {
							vv=v;
						}
					}
				}
			}
		};
		for(auto i: nodes) {
			auto& na=metatree[i].base;
			paint_target(na.pos, na.type, na.radius);
			if(na.par_idx!=na.par_idx_null) {
				auto& nb=metatree[na.par_idx].base;
				for(double t=0.04; t<1.0; t+=0.04) {
					auto pos=na.pos*t+nb.pos*(1-t);
					auto r=na.radius*t+nb.radius*(1-t);
					paint_target(pos, 0, r);
				}
			}
		}

		for(unsigned int z0=0; z0+prober_d<=cube_spec.sizes[2]; z0+=40) {
			for(unsigned int y0=0; y0+prober_wh<=cube_spec.sizes[1]; y0+=108) {
				for(unsigned int x0=0; x0+prober_wh<=cube_spec.sizes[0]; x0+=108) {
					auto& [input_img, target_img]=images.emplace_back();
					input_img.resize(prober_d*prober_wh*prober_wh);
					for(unsigned int dz=0; dz<prober_d; ++dz) {
						for(unsigned int dy=0; dy<prober_wh; ++dy) {
							auto ptr=&input_img[(dz*prober_wh+dy)*prober_wh];
							input_cube.visit([y=y0+dy,z=z0+dz,x0,ptr](auto& view) {
								auto src=view.row(y, z);
								using T=std::remove_pointer_t<decltype(src)>;
								if constexpr(std::is_unsigned_v<T>) {
									double scale=std::numeric_limits<T>::max();
									for(std::size_t i=0; i<prober_wh; ++i)
										ptr[i]=(src[i+x0]/scale)*2-1;
								}
							});
						}
					}
					target_img.resize(prober_d2*prober_wh*prober_wh);
					for(unsigned int dz=0; dz<prober_d2; ++dz) {
						for(unsigned int dy=0; dy<prober_wh; ++dy) {
							std::copy_n(&target_cube[((z0*2+dz)*cube_spec.sizes[1]+y0+dy)*cube_spec.sizes[0]+x0], prober_wh, &target_img[(dz*prober_wh+dy)*prober_wh]);
						}
					}
					double tgt_sum{0.0};
					for(auto v: target_img)
						tgt_sum+=(v!=0)?1.0:0.0;
					if(tgt_sum<64 && !_force_chan.at(cube_spec.chan_id-1))
						images.pop_back();
				}
			}
		}
	}

	assert(!images.empty());
	std::shuffle(images.begin(), images.end(), rng);
	fprintf(stderr, "----- sample: cnt=%zu\n", images.size());
	return images;
}

Tree load_swc(const char* fn) {
	Tree nodes{};
	std::unordered_map<int64_t, std::size_t> id2idx;

	std::ifstream fs{fn};
	gapr::swc_input swc{fs};
	while(swc.read()) {
		switch(swc.tag()) {
		case gapr::swc_input::tags::node:
			{
				auto n=swc.node();
				auto [it, ins]=id2idx.emplace(n.id, nodes.size());
				assert(ins);
				if(n.par_id==-1) {
					std::size_t jj=SIZE_MAX;
					unsigned int hits=0;
					for(std::size_t j=0; j<nodes.size(); ++j) {
						if(n.pos==nodes[j].pos && nodes[j].par_idx!=Node::par_idx_null) {
							jj=j;
							++hits;
						}
					}
					if(hits>=2)
						fprintf(stderr, "%s:%ld: duplicated hits\n", fn, n.id);
					if(hits==1) {
						it->second=jj;
						break;
					}
					n.par_idx=Node::par_idx_null;
				} else {
					it=id2idx.find(n.par_id);
					assert(it!=id2idx.end());
					n.par_idx=it->second;
				}
				nodes.emplace_back(n);
			}
			break;
		default:
			break;
		}
	}
	if(!swc.eof()) 
		throw std::runtime_error{"failed to read file"};
	return nodes;
}

void save_swc(const char* fn, const Tree& nodes) {
	std::ofstream fs{fn};
	gapr::swc_output swc{fs};

	Node n2;
	for(auto& n: nodes) {
		int pid{-1};
		if(n.par_idx!=Node::par_idx_null)
			pid=nodes[n.par_idx].id;
		n2=n;
		n2.par_id=pid;
		swc.node(n2);
	}

	fs.close();
	if(!fs)
		throw std::runtime_error{"failed to close file"};
}

static void fix_swc(std::vector<gapr::swc_node>& tree, double fix_xres, double fix_yres, double fix_zres) {
	auto to_type=[](int type) ->int {
		if(type==-2) {
			return 0;
		} else if(type==-1) {
			return 1;
		} else {
			return 2;
		}
	};
	std::unordered_map<std::size_t, int> special_nodes;
	for(std::size_t i=0; i<tree.size(); ++i) {
		auto& n=tree[i];
		if(n.type/10*10==1000'000)
			special_nodes.emplace(i, n.type);
		n.type=0;
	}
	auto xyres=(fix_xres+fix_yres)/2;
	for(auto& n: tree) {
		n.pos[0]/=fix_xres;
		n.pos[1]/=fix_yres;
		n.pos[2]/=fix_zres;
		n.radius/=xyres;

		if(n.par_idx!=n.par_idx_null) {
			--tree[n.par_idx].type;
			--n.type;
		}
	}
	for(std::size_t i=tree.size(); i-->0; ) {
		auto& n=tree[i];
		if(n.type>=0)
			continue;
		if(n.par_idx!=n.par_idx_null) {
			n.type=to_type(n.type);
			continue;
		}
		if(i==0) {
			n.type=3;
			continue;
		}
		int d=n.type;
		std::vector<std::size_t> hits;
		for(std::size_t j=0; j<i; ++j) {
			if(tree[j].pos==n.pos) {
				hits.push_back(j);
				d+=tree[j].type;
			}
		}
		d=to_type(d);
		for(auto j: hits)
			tree[j].type=d;
		n.type=d;
	}
	for(auto [i, t]: special_nodes) {
		switch(t) {
		case 1000'000:
			tree[i].par_idx=SIZE_MAX;
			tree[i].type=3;
			break;
		default:
			break;
		}
	}
}

static std::size_t fix_metatree(std::vector<ExtendedNode>& metatree, std::size_t ni_start, std::size_t ni_end) {
	for(std::size_t i=ni_start; i<ni_end; ++i) {
		if(auto pidx=metatree[i].base.par_idx; pidx!=gapr::swc_node::par_idx_null) {
			auto& pn=metatree[pidx];
			pn.neighbors[pn.num_ds++]={i, true};
			if(pn.num_ds+5>=MAX_NEIGH) {
				std::cerr<<"too many connections: "<<pn.base.id<<"\n";
				return pidx;
			}
		}
	}
	return SIZE_MAX;
}

void Helper::add_metatree(const std::string& swc_file, unsigned int chan, bool use_r) {
	auto& data=datasets.emplace_back();
	auto tree=load_swc(swc_file.c_str());
	fix_swc(tree, fix_xres, fix_yres, fix_zres);
	data.chan_id=chan;
	data.noise=std::hash<std::string_view>{}(swc_file);
	data.swc_file=swc_file;
	auto a=data.ni_start=metatree.size();
	for(auto& n: tree) {
		auto& nn=metatree.emplace_back();
		nn.base=n;
		if(!use_r)
			nn.base.radius=0;
		if(n.par_idx!=n.par_idx_null)
			nn.base.par_idx=n.par_idx+a;
		nn.tree_id=datasets.size();
		nn.chan_id=data.chan_id;
	}
	data.ni_end=metatree.size();
	if(SIZE_MAX!=::fix_metatree(metatree, data.ni_start, data.ni_end)) {
		std::cerr<<swc_file<<"\n";
		throw std::runtime_error{"err"};
	}
}

unsigned int Helper::load_catalog_impl(const std::string& path) {
	std::vector<gapr::mesh_info> mesh_infos;
	std::ifstream ifs{path};
	auto aa=sampler->cube_infos.size();
	auto url=gapr::to_url_if_path(path);
	gapr::parse_catalog(ifs, sampler->cube_infos, mesh_infos, url.empty()?path:url);
	unsigned int first_c{0}, first_g{0};
	for(unsigned int c=aa; c<sampler->cube_infos.size(); ++c) {
		sampler->cube_infos[c].xform.update_direction_inv();
		if(sampler->cube_infos[c].is_pattern()) {
			if(!first_c)
				first_c=c+1;
		} else {
			if(!first_g)
				first_g=c+1;
		}
	}
	if(first_c)
		return first_c;
	else if(first_g)
		return first_g;
	else {
		fprintf(stderr, "%s\n", path.c_str());
		throw std::runtime_error{"no image channel"};
	}
}
void Helper::prepare_sliding_cubes(unsigned int chan_id) {
	auto& cube_info=sampler->cube_infos[chan_id-1];
	unsigned long nodes_hits=0;
	if(!cube_info.is_pattern()) {
		auto& cube=sliding_cubes.emplace_back();
		cube.chan_id=chan_id;
		cube.offset={0, 0, 0};
		cube.sizes=cube_info.sizes;
		for(auto i=metatree.size(); i-->0; ) {
			auto& nn=metatree[i];
			if(nn.chan_id!=chan_id)
				break;
			if(nn.is_noise)
				continue;
			auto abs_node_off=cube_info.xform.to_offset(nn.base.pos);
			if(check_inside<0, 0, 0>(abs_node_off, cube.sizes, cube.offset)) {
				cube.nodes.push_back(i);
				++nodes_hits;
			}
		}
		std::cerr<<"hits "<<nodes_hits<<"\n";
		return;
	}

	auto get_key=[&cube_info](unsigned int x, unsigned int y, unsigned int z) {
		return (std::size_t{z}*cube_info.sizes[1]+y)*cube_info.sizes[0]+x;
	};
	std::unordered_map<std::size_t, SlidingCube> tmp_cubes;
	for(unsigned int x0=0; x0<cube_info.sizes[0]; x0+=2*cube_info.cube_sizes[0]) {
		for(unsigned int y0=0; y0<cube_info.sizes[1]; y0+=2*cube_info.cube_sizes[1]) {
			for(unsigned int z0=0; z0<cube_info.sizes[2]; z0+=2*cube_info.cube_sizes[2]) {
				auto [it, ins]=tmp_cubes.emplace(get_key(x0, y0, z0), SlidingCube{});
				assert(ins);
				auto& cube=it->second;
				cube.chan_id=chan_id;
				cube.offset={x0, y0, z0};
				for(unsigned int i=0; i<3; ++i) {
					auto s=3*cube_info.cube_sizes[i];
					if(s>cube_info.sizes[i]-cube.offset[i])
						s=cube_info.sizes[i]-cube.offset[i];
					cube.sizes[i]=s;
				}
			}
		}
	}

	for(auto i=metatree.size(); i-->0; ) {
		auto& nn=metatree[i];
		if(nn.chan_id!=chan_id)
			break;
		if(nn.is_noise)
			continue;
		auto abs_node_off=cube_info.xform.to_offset(nn.base.pos);
		auto cube_off=abs_node_off;
		for(unsigned int j=0; j<3; ++j)
			cube_off[j]=cube_off[j]/cube_info.cube_sizes[j]*cube_info.cube_sizes[j];
		for(int dz=-2; dz<=2; ++dz) {
			for(int dy=-2; dy<=2; ++dy) {
				for(int dx=-2; dx<=2; ++dx) {
					auto xx=cube_off[0]+dx*cube_info.cube_sizes[0];
					auto yy=cube_off[1]+dy*cube_info.cube_sizes[1];
					auto zz=cube_off[2]+dz*cube_info.cube_sizes[2];
					if(auto it=tmp_cubes.find(get_key(xx, yy, zz)); it!=tmp_cubes.end()) {
						if(check_inside<0, 0, 0>(abs_node_off, it->second.sizes, it->second.offset)) {
							it->second.nodes.push_back(i);
							++nodes_hits;
						}
					}
				}
			}
		}
	}
	for(auto& [k, v]: tmp_cubes) {
		if(!v.nodes.empty() || _force_chan.at(v.chan_id-1))
			sliding_cubes.emplace_back(std::move(v));
	}
	std::cerr<<"hits "<<nodes_hits<<"\n";
}
void Helper::print_training_stats() {
	std::unordered_set<int> types;
	double r0=INFINITY, r1=-INFINITY;
	unsigned int r0i=0, r1i=0;
	double d0=INFINITY, d1=-INFINITY;
	std::size_t d0i=0, d1i=0;
	for(std::size_t ii=0; ii<metatree.size(); ++ii) {
		auto& nn=metatree[ii];
		types.insert(nn.base.type);
		auto r=nn.base.radius;
		if(r0>r) {
			r0=r;
			r0i=nn.tree_id;
		}
		if(r1<r) {
			r1=r;
			r1i=nn.tree_id;
		}
		if(auto pidx=nn.base.par_idx; pidx!=nn.base.par_idx_null) {
			auto& nnn=metatree[pidx];
			auto d=(nnn.base.pos-nn.base.pos).mag2();
			if(d0>d) {
				d0=d;
				d0i=ii;
			}
			if(d1<d) {
				d1=d;
				d1i=ii;
			}
		}
	}
	for(auto t: types)
		std::fprintf(stderr, "type: %d\n", t);
	std::fprintf(stderr, "r: %lf, %lf\n", r0, r1);
	std::fprintf(stderr, "%s\n%s\n", datasets[r0i-1].swc_file.c_str(), datasets[r1i-1].swc_file.c_str());
	std::fprintf(stderr, "d: %lf, %lf\n", std::sqrt(d0), std::sqrt(d1));
	std::fprintf(stderr, "%ld %s\n%ld %s\n", metatree[d0i].base.id, datasets[metatree[d0i].tree_id-1].swc_file.c_str(), metatree[d1i].base.id, datasets[metatree[d1i].tree_id-1].swc_file.c_str());
	for(std::size_t dsi=0; dsi<datasets.size(); ++dsi) {
		auto a=datasets[dsi].ni_start;
		auto b=datasets[dsi].ni_end;
		double sum_l=0.0;
		std::size_t num_c=0;
		for(std::size_t j=a; j<b; ++j) {
			auto& n=metatree[j];
			if(n.base.par_idx!=n.base.par_idx_null) {
				auto& nn=metatree[n.base.par_idx];
				auto dp=nn.base.pos-n.base.pos;
				sum_l+=dp.mag();
				++num_c;
			}
		}
		if(0)
			std::fprintf(stderr, "dist %lf : %s\n", sum_l/num_c, datasets[dsi].swc_file.c_str());
	}
}


int train_prober(const std::filesystem::path& out_pars, int ndata, char* data_files[]) {
	Helper helper{};

	try {
		for(int i=0; i<ndata; ++i) {
			helper.push_data(data_files[i]);
		}
	} catch(const std::system_error& e) {
		auto& [fs, fn_cur, lineno]=helper.files_stk.back();
		std::cerr<<fn_cur<<':'<<lineno<<": "<<e.what()<<"\n";
		return -1;
	}

	helper.train_probe(out_pars);
	helper.finish();
	return 0;

}

