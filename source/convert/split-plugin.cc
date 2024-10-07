#include "split.hh"

#include <cctype>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <cstring>
#include <fstream>
#include <thread>
#include <deque>
#include <optional>

#include <boost/asio/thread_pool.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
//#include <boost/asio/post.hpp>

//#include <boost/beast/core.hpp>

#include "gapr/detail/nrrd-output.hh"
#include "gapr/str-glue.hh"
#include "gapr/utility.hh"
#include "gapr/mem-file.hh"

#include <sys/stat.h>
//#include <tiffio.h>
#include <unordered_set>

#include "../gather/stop-signal.hh"
//#include "loadtiff.hh"
#include "dshelper.hh"
#include "mem-str.hh"
#include "nethelper.hh"
#include "cache-helper.hh"
#include "savewebm.hh"

#ifdef _WIN64
#include <libloaderapi.h>
extern "C" {
#include "c-api.h"
}


template<typename T>
struct plugin_func {
	T func;
	const char* tag;
	//using Res=std::result_of_t<std::remove_pointer_t<T>>;
	plugin_func(HMODULE mod, const char* name): tag{name} {
		func=reinterpret_cast<T>(GetProcAddress(mod, name));
		assert(func!=NULL);
	}
	template<typename... Args>
	auto operator()(Args&&... args) const {
		//fprintf(stderr, "call: xxx %s\n", tag);
		return func(std::forward<Args>(args)...);
	}
};
struct plugin_helper {
	HMODULE mod;
	image_loader_context* ctx;
#define LoadFunc(name) plugin_func<decltype(&name)> func_##name{mod, #name};
	LoadFunc(image_loader_context_new);
	LoadFunc(image_loader_context_free);
	LoadFunc(image_loader_context_get_pixel_type);
	LoadFunc(image_loader_context_get_resolutions);
	LoadFunc(image_loader_context_get_sizes);
	LoadFunc(image_loader_context_get_block_sizes);
	LoadFunc(image_loader_context_load_voxels);
	LoadFunc(image_loader_context_get_resolutions_downsample);
	LoadFunc(image_loader_context_get_sizes_downsample);
	LoadFunc(image_loader_context_load_voxels_downsample);
#undef LoadFunc
	plugin_helper(const char* pth, const char* args): mod{LoadLibraryA(pth)} {
		assert(mod!=NULL);
		fprintf(stderr, "new ctx: %s\n", args);
		ctx=func_image_loader_context_new(args);
	}
	~plugin_helper() {
		fprintf(stderr, "free ctx\n");
		if(ctx)
			func_image_loader_context_free(ctx);
		fprintf(stderr, "free mod\n");
		if(mod)
			FreeLibrary(mod);
		fprintf(stderr, "free end\n");
	}
	operator image_loader_context*() const noexcept {
		return ctx;
	}
};


struct prepare_helper {
	unsigned int width=0;
	unsigned int height=0;
	unsigned int depth=0;

	unsigned short spp=1;
	unsigned short bps=0;

	double xres, yres, zres;

	unsigned int xsize=0;
	unsigned int ysize=0;
	unsigned int zsize=0;

	unsigned int dsw=0;
	unsigned int dsh=0;
	unsigned int dsd=0;
	double dsx, dsy, dsz;

	std::vector<std::string> args;

	void scan_files(const std::string& plugin, const std::vector<std::string>& args) {
		plugin_helper p{plugin.c_str(), args[0].c_str()};
		auto type=p.func_image_loader_context_get_pixel_type(p);
		switch(type) {
		default:
			throw std::runtime_error{"unsuported bps"};
			break;
		case IMAGE_LOADER_PIXEL_TYPE_U8:
			bps=8;
			break;
		case IMAGE_LOADER_PIXEL_TYPE_U16:
			bps=16;
			break;
		}

		double res[3];
		p.func_image_loader_context_get_resolutions(p, res);
		std::cerr<<"res: "<<res[0]<<"/"<<res[1]<<"/"<<res[2]<<"\n";
		unsigned int sizes[3];
		p.func_image_loader_context_get_sizes(p, sizes);
		std::cerr<<"sizes: "<<sizes[0]<<"/"<<sizes[1]<<"/"<<sizes[2]<<"\n";
		unsigned int cube_sizes[3];
		p.func_image_loader_context_get_block_sizes(p, cube_sizes);
		std::cerr<<"cube_sizes: "<<cube_sizes[0]<<"/"<<cube_sizes[1]<<"/"<<cube_sizes[2]<<"\n";
		double res_ds[3];
		p.func_image_loader_context_get_resolutions_downsample(p, res_ds);
		std::cerr<<"res_ds: "<<res_ds[0]<<"/"<<res_ds[1]<<"/"<<res_ds[2]<<"\n";
		unsigned int sizes_ds[3];
		p.func_image_loader_context_get_sizes_downsample(p, sizes_ds);
		std::cerr<<"sizes_ds: "<<sizes_ds[0]<<"/"<<sizes_ds[1]<<"/"<<sizes_ds[2]<<"\n";

		width=sizes[0];
		height=sizes[1];
		depth=sizes[2];
		xres=res[0];
		yres=res[1];
		zres=res[2];
		if(xsize<=0) {
			xsize=cube_sizes[0];
			ysize=cube_sizes[1];
			zsize=cube_sizes[2];
		}
		dsw=sizes_ds[0];
		dsh=sizes_ds[1];
		dsd=sizes_ds[2];
		dsx=res_ds[0]/res[0];
		dsy=res_ds[1]/res[1];
		dsz=res_ds[2]/res[2];

		for(auto& a: args)
			this->args.emplace_back(a);
		fprintf(stderr, "scan end\n");
	}

	void write_state(const std::filesystem::path& workdir, const std::string& plugin) {
		fprintf(stderr, "write state\n");
		std::ofstream fs{workdir/"state"};
		if(!fs)
			throw std::runtime_error{"failed to open state file"};
		fprintf(stderr, "open succ\n");
		fs<<"plugin "<<plugin<<"\n";
		fs<<"sizes "<<width<<':'<<height<<':'<<depth<<"\n";
		fs<<"spp "<<spp<<"\n";
		fs<<"bps "<<bps<<"\n";
		fs<<"cubesizes "<<xsize<<':'<<ysize<<':'<<zsize<<"\n";
		fs<<"resolution "<<xres<<':'<<yres<<':'<<zres<<"\n";
		fs<<"downsample "<<dsx<<':'<<dsy<<':'<<dsz<<"\n";
		for(auto& f: args)
			fs<<"file "<<f<<'\n';
		if(fs.close(), !fs)
			throw std::runtime_error{"failed to save state file"};
		fprintf(stderr, "write state succ\n");
	}

	void print_info() {
		std::cerr<<"Input images: "<<bps<<" bit, "<<spp<<(spp>1?" channels, ":" channel, ")<<width<<"*"<<height<<"*"<<depth<<" voxels";
		std::cerr<<" ("<<width*xres<<"*"<<height*yres<<"*"<<depth*zres<<" µm³)\n";
		std::cerr<<"Output cubes: "<<bps<<" bit, "<<xsize<<"*"<<ysize<<"*"<<zsize<<" voxels";
		std::cerr<<" ("<<xsize*xres<<"*"<<ysize*yres<<"*"<<zsize*zres<<" µm³)\n";
		std::cerr<<"Downsampled volume [MIP/MEAN]: ";
		std::cerr<<dsw<<"*"<<dsh<<"*"<<dsd<<" voxels";
		std::cerr<<" (factors "<<dsx<<":"<<dsy<<":"<<dsz<<")\n";
		auto cubesize=xsize*ysize*zsize*bps/8;
		if(cubesize*3*3*3>1024*1024*1024)
			std::cerr<<"WARN: Output cubes might be too big! ("<<cubesize*3*3*3/1024.0/1024/1024<<" GiB for 3*3*3 cubes)\n";
		if(cubesize<4*1024*1024)
			std::cerr<<"WARN: Output cubes might be too small! ("<<cubesize/1024.0/1024<<" MiB per cube)\n";
		auto dssize=dsw*dsh*dsd*bps/8;
		if(dssize>512*1024*1024)
			std::cerr<<"WARN: Downsampled volume might be too big! ("<<dssize/1024.0/1024<<" MiB)\n";
	}

	void guess_cubesizes(double xres, double yres, double zres) {
		fprintf(stderr, "guess sizes\n");
		if(xres*yres*zres!=0) {
		fprintf(stderr, "xxxxxxx b\n");
			this->xres*=xres;
		fprintf(stderr, "xxxxxxx d\n");
			this->yres*=yres;
			this->zres*=zres;
		fprintf(stderr, "xxxxxxx c\n");
		}
		fprintf(stderr, "xxxxxxx a\n");
	}
};

void prepare_conversion_plugin(const SplitArgs& args, const std::filesystem::path& workdir, unsigned int ncpu, std::size_t) {
	std::filesystem::create_directories(workdir);

	prepare_helper helper{};

	helper.xsize=args.xsize;
	helper.ysize=args.ysize;
	helper.zsize=args.zsize;
	helper.scan_files(args.plugin, args.plugin_args);

	helper.guess_cubesizes(args.xres, args.yres, args.zres);

		fprintf(stderr, "xxxxxxx 0\n");
		std::cerr<<workdir<<"\n";
	helper.write_state(workdir, args.plugin);
		fprintf(stderr, "xxxxxxx 1\n");
	helper.print_info();
		fprintf(stderr, "xxxxxxx 2\n");
}


struct conversion_helper2 {
	std::ofstream _log;

	unsigned int width=0;
	unsigned int height=0;
	unsigned int depth=0;
	unsigned short spp=0;
	unsigned short bps=0;

	unsigned int xsize=0;
	unsigned int ysize=0;
	unsigned int zsize=0;
	double dsx=0;
	double dsy=0;
	double dsz=0;
	unsigned int dsw, dsh, dsd;

	double xres, yres, zres;

	std::vector<std::pair<const std::string, unsigned int>> inputpairs;

	std::string token{};
	uint64_t downsample_ver{0};

	boost::asio::io_context _ctx;
	boost::asio::thread_pool _pool;
	boost::asio::steady_timer _timer;
	network_helper netaux;
	std::shared_ptr<cache_helper> cacheaux;
	stop_signal _stop_signal{};

	std::string user{};
	std::string passwd{};
	std::string group{};

	unsigned int ncpu;

	std::optional<plugin_helper> p;

	explicit conversion_helper2(unsigned int ncpu, ServerArgs&& srv):
		_ctx{}, _pool{ncpu}, _timer{_ctx},
		netaux{_ctx, std::move(srv.host), std::to_string(srv.port)},
		user{std::move(srv.user)},
		passwd{std::move(srv.passwd)},
		group{std::move(srv.group)},
		ncpu{ncpu}
	       	{
		}


	void write_catalog(std::ostream& str) {
		char tag[10];
		auto maxsig=_ds_max_val;
		for(unsigned int i=0; i<spp; ++i) {
			snprintf(tag, 10, "%02u", i);
			str<<"[CH"<<tag<<"_MIP]\n";
			str<<"location=ch"<<tag<<"-mip.webm\n";
			str<<"origin=0 0 0\n";
			str<<"direction="<<xres*dsx<<" 0 0 0 "<<yres*dsy<<" 0 0 0 "<<zres*dsz<<"\n";
			str<<"size="<<dsw<<" "<<dsh<<" "<<dsd<<"\n";
			str<<"range=0 "<<maxsig*1.5<<"\n";
			str<<"[CH"<<tag<<"_WEBM]\n";
			str<<"pattern=ch"<<tag<<"webm/z<08u$z>/y<08u$y>.x<08u$x>.webm\n";
			str<<"size="<<width<<" "<<height<<" "<<depth<<"\n";
			str<<"cubesize="<<xsize<<" "<<ysize<<" "<<zsize<<"\n";
			str<<"origin=0 0 0\n";
			str<<"direction="<<xres<<" 0 0 0 "<<yres<<" 0 0 0 "<<zres<<"\n";
			str<<"range=0 "<<maxsig*1.5<<"\n";
			if(!str)
				throw std::runtime_error{"Failed to write catalog."};
		}
	}

	void load_state(const std::filesystem::path& workdir) {
		std::cerr<<"loading state...";
		//gapr::print("cmd: ", workdir);
		std::ifstream prev_log{workdir/"state"};
		if(!prev_log)
			throw std::runtime_error{"failed to open read"};
		std::string line;
		std::string plugin;
		std::vector<std::string> plugin_args;
		while(std::getline(prev_log, line)) {
			unsigned int ii=0;
			while(ii<line.length() && !std::isspace(line[ii]))
				++ii;
			std::string_view cmd{&line[0], ii};
			if(ii<line.length()) {
				do ++ii; while(ii<line.length() && std::isspace(line[ii]));
			}
			std::string_view args{&line[ii], line.size()-ii};
		//gapr::print("cmd: ", cmd);

			if(cmd=="sizes") {
				if(gapr::parse_tuple(&args[0], &width, &height, &depth)!=3)
					throw std::runtime_error{"failed to parse sizes"};
			} else if(cmd=="spp") {
				if(gapr::parse_tuple(&args[0], &spp)!=1)
					throw std::runtime_error{"failed to parse spp"};
			} else if(cmd=="bps") {
				if(gapr::parse_tuple(&args[0], &bps)!=1)
					throw std::runtime_error{"failed to parse bps"};
			} else if(cmd=="cubesizes") {
				if(gapr::parse_tuple(&args[0], &xsize, &ysize, &zsize)!=3)
					throw std::runtime_error{"failed to parse cubesizes"};
			} else if(cmd=="downsample") {
				if(gapr::parse_tuple(&args[0], &dsx, &dsy, &dsz)!=3)
					throw std::runtime_error{"failed to parse downsample"};
		//gapr::print("dsx: ", dsx);
			} else if(cmd=="resolution") {
				if(gapr::parse_tuple(&args[0], &xres, &yres, &zres)!=3)
					throw std::runtime_error{"failed to parse resolution"};
			} else if(cmd=="file") {
				std::string path;
				if(gapr::parse_tuple(&args[0], &path)!=1)
					throw std::runtime_error{"failed to parse filename"};
				plugin_args.emplace_back(std::move(path));
			} else if(cmd=="token") {
				if(gapr::parse_tuple(&args[0], &token)!=1)
					throw std::runtime_error{"failed to parse token"};
			} else if(cmd=="downsample-ready") {
				if(gapr::parse_tuple(&args[0], &downsample_ver)!=1)
					throw std::runtime_error{"failed to parse downsample-ready"};
			} else if(cmd=="cube-needed") {
				job_id id;
				if(!id.parse(args))
					throw std::runtime_error{"failed to parse cube"};
				add_needed(id, true);
				add_suggested(id);
			} else if(cmd=="cube-ready") {
				job_id id;
				if(!id.parse(args))
					throw std::runtime_error{"failed to parse cube"};
				assume_ready(id);
			} else if(cmd=="plugin") {
				plugin=args;
			} else {
				throw std::runtime_error{"unknown line"};
			}
		}
		//gapr::print("dsx: ", dsx);
		p.emplace(plugin.c_str(), plugin_args[0].c_str());
		unsigned int sizes_ds[3];
		p->func_image_loader_context_get_sizes_downsample(*p, sizes_ds);
		dsw=sizes_ds[0];
		dsh=sizes_ds[1];
		dsd=sizes_ds[2];
		unsigned int cube_sizes[3];
		p->func_image_loader_context_get_block_sizes(*p, cube_sizes);
		tileW=cube_sizes[0];
		tileH=cube_sizes[1];
		tileD=cube_sizes[2];
		if(!prev_log.eof())
			throw std::runtime_error{"failed to read line"};

		load_downsample();

		_log.open(workdir/"state", std::ios::app);
		if(!_log)
			throw std::runtime_error{"failed to open write"};
		//_log.seekp(0, std::ios::end);
		std::cerr<<"  DONE\n";
	}

	std::unique_ptr<char[]> _ds_data;
	double _ds_max_val{1.0};
	void load_downsample() {
		_ds_data=std::make_unique<char[]>(dsw*dsh*dsd*bps/8);
		p->func_image_loader_context_load_voxels_downsample(*p, _ds_data.get(), dsw*bps/8, dsh*dsw*bps/8);
		if(bps==8) {
			get_ds_max(uint8_t{});
		} else if(bps==16) {
			get_ds_max(uint16_t{});
		} else {
		}
	}
	template<typename T>
	void get_ds_max(T) {
		auto ptr=reinterpret_cast<T*>(_ds_data.get());
		T mv{0};
		for(std::size_t i=0; i<dsw*dsh*dsd; ++i) {
			auto v=ptr[i];
			if(mv<v)
				mv=v;
		}
		_ds_max_val=mv*1.0/std::numeric_limits<T>::max();
	}

	template<typename Cb>
	void async_put(std::string_view path, gapr::mem_file&& file, Cb&& cb) {
		auto sess=std::make_shared<network_helper::session>(std::move(file), std::forward<Cb>(cb));
		sess->req_put.method(boost::beast::http::verb::put);
		sess->req_put.target(path);
		netaux.do_session(*sess, _ctx.get_executor());
	}
	template<typename Cb>
	void async_get(std::string_view path, Cb&& cb) {
		auto sess=std::make_shared<network_helper::session>(std::forward<Cb>(cb));
		sess->req_get.method(boost::beast::http::verb::get);
		sess->req_get.target(path);
		netaux.do_session(*sess, _ctx.get_executor());
	}

	void upload_catalog() {
		std::cerr<<"upload catalog ...";
		auto ex=boost::asio::require(_ctx.get_executor(), boost::asio::execution::outstanding_work.tracked);
		boost::asio::post(_pool, [this,ex]() {
			gapr::mem_file cat;
			{
				mem_file_out str{gapr::mutable_mem_file{true}};
				write_catalog(str);
				if(!str.flush())
					throw std::runtime_error{"failed to write catalog"};
				cat=std::move(str).mem_file();
			}
			boost::asio::post(ex, [this,cat=std::move(cat)]() mutable {
				async_put("/api/catalog/"+group+"/"+passwd, std::move(cat), [this](std::string&& res, const /*boost::beast::http::response_header<>*/auto& hdr) {
					if(hdr.result()!=boost::beast::http::status::ok)
						throw std::runtime_error{std::move(res)};
					_log<<"token "<<res<<"\n";
					if(!_log.flush())
						throw std::runtime_error{"failed to flush"};
					token=std::move(res);
					std::cerr<<"   DONE\n";
					upload_downsample();
				});
			});
		});
	}

	template<typename T>
	gapr::mem_file downsample(T) {
		return convert_webm(reinterpret_cast<T*>(_ds_data.get()), dsw, dsh, dsd, {});
	}
	void upload_downsample() {
		std::cerr<<"upload downsample ...";
		auto ex=boost::asio::require(_ctx.get_executor(), boost::asio::execution::outstanding_work.tracked);
		boost::asio::post(_pool, [this,ex]() {
			auto ver=1;

			gapr::mem_file file;
			if(bps==8)
				file=downsample(uint8_t{});
			else if(bps==16)
				file=downsample(uint16_t{});
			boost::asio::post(ex, [this,file=std::move(file),ver]() mutable {
				async_put("/api/data/"+group+"/ch00-mip.webm", std::move(file), [this,ver](std::string&& res, const boost::beast::http::response_header<>& hdr) {
					if(hdr.result()!=boost::beast::http::status::ok)
						throw std::runtime_error{std::move(res)};
					_log<<"downsample-ready "<<ver<<"\n";
					if(!_log.flush())
						throw std::runtime_error{"failed to flush"};
					downsample_ver=ver;
					std::cerr<<"   DONE\n";
					upload_cubes();
				});
			});
		});
	}

	struct job_st {
		job_state st;
		job_prio prio;

		std::unordered_set<unsigned int> todo;
		std::unique_ptr<uint16_t[]> data;

		std::atomic<uint64_t> time_read_us{0};
		std::chrono::steady_clock::time_point time_read_start{}, time_read_end;
		std::atomic<unsigned int> read_cache_hit{0};
		std::atomic<unsigned int> read_total{0};
		std::chrono::milliseconds time_conv;
	};

	std::unordered_map<job_id, job_st, job_id::hash> _job_states;
	std::deque<job_id> _job_que0;
	std::deque<job_id> _job_que1;
	std::deque<job_id> _job_que2;
	unsigned int _n_running{0};

	struct file_st {
		//std::optional<Tiff> tif;
		unsigned int zz;
		//std::optional<TileReader> aux;
		std::chrono::steady_clock::time_point ts;
		bool busy{false};
	};
	std::unordered_map<unsigned int, file_st> _files;

	file_st* open_file(unsigned int zz) {
		constexpr unsigned int max_files_a=480;
		constexpr unsigned int max_files_b=360;
		if(_files.size()>max_files_a) {
			auto it_end=_files.end();
			std::vector<decltype(it_end)> its;
			for(auto it=_files.begin(); it!=it_end; ++it)
				if(!it->second.busy)
					its.push_back(it);
			std::sort(its.begin(), its.end(), [](auto a, auto b) {
				return a->second.ts>b->second.ts;
			});
			while(!its.empty()) {
				auto it=its.back();
				its.pop_back();
				_files.erase(it);
				if(_files.size()<=max_files_b)
					break;
			}
		}
		auto [it, ins]=_files.try_emplace(zz, file_st{});
		assert(ins);
		auto file=&it->second;
		file->zz=zz;
		file->ts=std::chrono::steady_clock::now();
		file->busy=true;
		return file;
	}

	void handle_slice(job_id id, unsigned int zz, file_st* file, job_st* job, const char* que) {
		assert(_ctx.get_executor().running_in_this_thread());
		gapr::print("handle slice [", _n_running, "/", que, "]: ", id.x, "/", id.y, "/", id.z, "/", zz);
		auto ex=boost::asio::require(_ctx.get_executor(), boost::asio::execution::outstanding_work.tracked);
		++_n_running;

		if(job->time_read_start==std::chrono::steady_clock::time_point{})
			job->time_read_start=std::chrono::steady_clock::now();

		boost::asio::post(_pool, [this,ex,id,zz,file,job]() {
			if(bps<=8) {
				handle_slice_work<uint8_t>(id, zz, file, job);
			} else if(bps<=16) {
				handle_slice_work<uint16_t>(id, zz, file, job);
			} else {
				throw std::logic_error{"unsuported bps"};
			}
			boost::asio::post(ex, [this,id,zz,job,file]() {
				handle_slice_done(id, zz, job, file);
			});
		});
	}

	unsigned int tileW, tileH, tileD;
	template<typename T>
	void handle_slice_work(job_id id, unsigned int zz, file_st* file, job_st* job) {
		assert(_pool.get_executor().running_in_this_thread());
		assert(zz==file->zz);
		T* data{nullptr};
		if constexpr(std::is_same_v<T, uint16_t>) {
			data=job->data.get();
		}

		auto time_begin=std::chrono::steady_clock::now();

		std::vector<uint8_t> tmpbuf;
		auto tileW=this->tileW;
		auto tileH=this->tileH;
		auto tileD=this->tileD;
		unsigned int y=id.y/tileH*tileH;
		unsigned int read_total{0}, read_nohit{0};
		do {
			unsigned int x=id.x/tileW*tileW;
			do {
				++read_total;
				auto cache=cacheaux->get(zz, x, y);
				if(!cache) {
					++read_nohit;
					auto tile=std::make_unique<T[]>(tileW*tileH*tileD*spp);
					unsigned int cur_off[3]={x, y, zz};
					gapr::print("load data: ", x, '/', y, '/', zz);
					p->func_image_loader_context_load_voxels(*p, cur_off, reinterpret_cast<char*>(tile.get()), tileW*sizeof(T), tileH*tileW*sizeof(T));
					// XXX TILEDEPTH
					cache=cacheaux->put(zz, x, y, tileW, tileH, spp, std::move(tile));
					assert(cache);
				}
				assert(zz>=id.z);
				assert(zz<id.z+zsize);
				assert(zz<depth);
				auto zzz0=std::max(zz, id.z);
				auto zzz1=std::min(std::min(zz+tileD, id.z+zsize), depth);
				auto yy0=std::max(y, id.y);
				auto yy1=std::min(std::min(y+tileH, id.y+ysize), height);
				auto xx0=std::max(x, id.x);
				auto xx1=std::min(std::min(x+tileW, id.x+xsize), width);
				assert(yy0<yy1);
				assert(xx0<xx1);
				for(auto zzz=zzz0; zzz<zzz1; ++zzz) {
					auto ptr=cache->ptr(T{})+tileW*tileH*(zzz-zz)+tileW*tileH*tileD*id.chan;
					auto optr=data+(zzz-id.z)*xsize*ysize;
					for(auto yy=yy0; yy<yy1; ++yy)
						std::memcpy(&optr[(xx0-id.x)+(yy-id.y)*xsize], &ptr[(xx0-x)+(yy-y)*tileW], (xx1-xx0)*sizeof(T));
				}

				x+=tileW;
			} while(x<id.x+xsize && x<width);

			y+=tileH;
		} while(y<id.y+ysize && y<height);

		auto time_end=std::chrono::steady_clock::now();
		auto time_us=std::chrono::duration_cast<std::chrono::microseconds>(time_end-time_begin).count();
		job->time_read_us.fetch_add(time_us);
		job->read_total.fetch_add(read_total);
		job->read_cache_hit.fetch_add(read_total-read_nohit);
	}
	void handle_slice_done(job_id id, unsigned int zz, job_st* job, file_st* file) {
		assert(_ctx.get_executor().running_in_this_thread());
		--_n_running;
		//gapr::print("handle slice [", _n_running, "]: ", id.x, "/", id.y, "/", id.z, "/", zz);

		file->ts=std::chrono::steady_clock::now();
		file->busy=false;
		auto& st=_job_states.at(id);
		auto it=st.todo.find(zz);
		assert(it!=st.todo.end());
		st.todo.erase(it);
		assert(st.st==job_state::loading);
		if(st.todo.empty()) {
			st.st=job_state::ready;
			return handle_cube(id, job);
		}
		upload_cubes();
	}
	void handle_cube(job_id id, job_st* job) {
		assert(_ctx.get_executor().running_in_this_thread());
		job->time_read_end=std::chrono::steady_clock::now();
		gapr::print("compress cube [", _n_running, "]: ", id.x, "/", id.y, "/", id.z);
		auto ex=boost::asio::require(_ctx.get_executor(), boost::asio::execution::outstanding_work.tracked);
		++_n_running;
		boost::asio::post(_pool, [this,ex,id,job]() {
			auto t_begin=std::chrono::steady_clock::now();
			auto data=std::move(job->data);
			if(0) {
				std::filesystem::path file{"/tmp/convdata/"+id.format()};
				std::filesystem::create_directories(file.parent_path());
				std::ofstream ostr{file};
				gapr::nrrd_output nrrd{ostr, true};
				nrrd.header();
				nrrd.finish(data.get(), xsize, ysize, zsize);
			}
			gapr::mem_file ret{convert_webm(data.get(), xsize, ysize, zsize, {})};
			auto t_end=std::chrono::steady_clock::now();
			job->time_conv=std::chrono::duration_cast<std::chrono::milliseconds>(t_end-t_begin);
			boost::asio::post(ex, [this,id,job,ret=std::move(ret)]() mutable {
				handle_cube_done(id, job, std::move(ret));
			});
		});
	}
	void handle_cube_done(job_id id, job_st* job, gapr::mem_file&& file) {
		assert(_ctx.get_executor().running_in_this_thread());
		--_n_running;
		gapr::print("handle cube [", _n_running, "]: ", id.x, "/", id.y, "/", id.z);

		auto path=id.format();
		auto t_begin=std::chrono::steady_clock::now();
		auto f_size=file.size();
		async_put("/api/data/"+group+"/"+path, std::move(file), [this,job,f_size,t_begin,path](std::string&& res, const auto& hdr) {
			if(hdr.result()!=boost::beast::http::status::ok)
				throw std::runtime_error{std::move(res)};
			auto t_end=std::chrono::steady_clock::now();

			_log<<"cube-ready "<<path<<"\n";
			if(!_log.flush())
				throw std::runtime_error{"failed to flush"};
			auto time_send_ms=std::chrono::duration_cast<std::chrono::milliseconds>(t_end-t_begin).count();
			auto time_read_real=std::chrono::duration_cast<std::chrono::milliseconds>(job->time_read_end-job->time_read_start);
			gapr::print("cube finish: ", path, "\n    stats: ",
					job->time_read_us.load()/1000, "ms/", time_read_real.count(), "ms ",
					job->read_cache_hit.load(), "/", job->read_total.load(), " ",
					job->time_conv.count(), "ms ",
					f_size/1000, "K ",
					time_send_ms, "ms");
		});

		upload_cubes();
	}

	std::tuple<job_id, unsigned int, file_st*, job_st*> find_job_slice(std::deque<job_id>& que) {
		job_id id;
		unsigned int zz{0};
		file_st* file{nullptr};
		job_st* job{nullptr};
		while(!que.empty() && !que.front())
			que.pop_front();
		for(auto& i: que) {
			if(!i)
				continue;
			auto& st=_job_states.at(i);
			if(st.st==job_state::ready) {
				assert(st.todo.empty());
				st.todo={};
				i={};
				continue;
			}
			if(st.st==job_state::init) {
				unsigned int z=i.z/tileD*tileD;
				do {
					st.todo.emplace(z);
					z+=tileD;
				} while(z<i.z+zsize && z<depth);
				for(auto z: st.todo)
					gapr::print("todo: ", z, "<", i.z+zsize, ":", depth);
				st.st=job_state::loading;
				st.data=std::make_unique<uint16_t[]>(xsize*ysize*zsize);
			}
			assert(st.st==job_state::loading);
			for(auto z: st.todo) {
				auto it=_files.find(z);
				if(it==_files.end()) {
					id=i;
					zz=z;
					job=&st;
					break;
				}
				if(!it->second.busy) {
					id=i;
					zz=z;
					job=&st;
					file=&it->second;
					file->busy=true;
					break;
				}
			}
			if(id)
				break;
		}
		return {id, zz, file, job};
	}

	std::vector<job_id> _pending_jobs_sugg;
	void add_needed(job_id id, bool loading);
	void add_suggested(job_id id);

	std::chrono::steady_clock::time_point _last_fetch{};
	bool _timer_busy{false};
	uint64_t _last_sync_needed{0};


	void upload_cubes() {
		assert(_ctx.get_executor().running_in_this_thread());
		if(_stop_signal.signal()>1)
			return;

		while(true) {
			if(_n_running>=ncpu)
				return;
			auto [id, zz, file, job]=find_job_slice(_job_que0);
			if(!id)
				break;
			if(!file)
				file=open_file(zz);

			handle_slice(id, zz, file, job, "que0");
		}

		if(_stop_signal.signal()>0)
			return;

		auto ts=std::chrono::steady_clock::now();
		if(std::chrono::duration_cast<std::chrono::milliseconds>(ts-_last_fetch).count()>2000) {
			_last_fetch=ts;
			gapr::print("get pending");
			async_get("/api/pending/"+group+"/"+std::to_string(_last_sync_needed), [this](gapr::mem_file&& file, const auto& hdr) {
				if(hdr.result()!=boost::beast::http::status::ok)
					//throw std::runtime_error{std::move(res)};
					throw std::runtime_error{"asdf"};
				mem_file_in str{std::move(file)};
				std::string line;
				while(std::getline(str, line)) {
					//chds *in*in
					std::string_view path;
					uint64_t seq;
					if(gapr::parse_tuple(&line[0], &seq, &path)!=2)
						throw std::runtime_error{"line err2"};
					job_id id;
					if(!id.parse(path))
						throw std::runtime_error{"line err"};
					if(id.isds()) {
						_last_sync_needed=seq;
						continue;
					}
					add_needed(id, false);
					_pending_jobs_sugg.push_back(id);
					_last_sync_needed=seq;
				}
				if(!str.eof())
					throw std::runtime_error{"err read line"};
				while(!_pending_jobs_sugg.empty()) {
					add_suggested(_pending_jobs_sugg.back());
					_pending_jobs_sugg.pop_back();
				}
				upload_cubes();
			});
		} else if(!_timer_busy) {
			_timer.expires_after(std::chrono::milliseconds{1000});
			_timer_busy=true;
			//boost::asio::post(_pool, [this]() {
				//dsaux->writecache(workdir/"downsample", false);
			//});
			_timer.async_wait([this](boost::system::error_code ec) {
				_timer_busy=false;
				upload_cubes();
			});
		}

		while(true) {
			if(_n_running*2>=ncpu)
				return;
			auto [id, zz, file, job]=find_job_slice(_job_que1);
			if(!id)
				break;
			if(!file)
				file=open_file(zz);

			handle_slice(id, zz, file, job, "que1");
		}
		while(true) {
			if(_n_running*2>=ncpu)
				return;
			auto [id, zz, file, job]=find_job_slice(_job_que2);
			if(!id)
				break;
			if(!file)
				file=open_file(zz);

			handle_slice(id, zz, file, job, "que2");
		}
	}
	void assume_ready(job_id id) {
		auto [it, ins]=_job_states.try_emplace(id);
		if(ins) {
			it->second.st=job_state::ready;
			it->second.prio=job_prio::suggested;
		} else {
			it->second.st=job_state::ready;
		}
		gapr::print((int)it->second.st, ": ", id.format());
	}
};

void resume_conversion_plugin(const std::filesystem::path& workdir, unsigned int ncpu, std::size_t cachesize, ServerArgs&& srv) {
	conversion_helper2 helper{ncpu, std::move(srv)};
	helper.load_state(workdir);
	helper.cacheaux=std::make_shared<cache_helper>(cachesize, cachesize/3*2);

	boost::asio::post(helper._ctx, [&helper]() {
		if(helper.token.empty())
			helper.upload_catalog();
		else if(helper.downsample_ver<1)
			helper.upload_downsample();
		else
			helper.upload_cubes();
	});

	helper._stop_signal.on_stop([&helper](unsigned int n) {
		boost::asio::post(helper._ctx, [&helper,n]() {
			if(n>1)
				helper._ctx.stop();
		});
	});
	helper._stop_signal.start();

	helper._ctx.run();

	helper._pool.join();
	helper._stop_signal.stop();
}

void conversion_helper2::add_needed(job_id id, bool loading) {
	bool add_que0{false};
	auto [it, ins]=_job_states.try_emplace(id);
	if(ins) {
		it->second.st=job_state::init;
		add_que0=true;
	} else {
		if(it->second.prio!=job_prio::needed) {
			add_que0=true;
		}
	}

	if(add_que0) {
		it->second.prio=job_prio::needed;
		if(!loading) {
			_log<<"cube-needed "<<id.format()<<"\n";
			if(!_log.flush())
				throw std::runtime_error{"failed to flush"};
		}
		if(it->second.st!=job_state::ready) {
			//gapr::print("add que0: ", id.x, "/", id.y, "/", id.z);
			_job_que0.push_back(id);
		}
	}
}
static constexpr std::initializer_list<std::array<int, 4>> neighbors_reverse{
	// backward
	{2, 2, 2, 1},
	{1, 2, 2, 1},
	{0, 2, 2, 1},
	{2, 1, 2, 1},
	{1, 1, 2, 1},
	{0, 1, 2, 1},
	{2, 0, 2, 1},
	{1, 0, 2, 1},
	{0, 0, 2, 1},
	{2, 2, 1, 1},
	{1, 2, 1, 1},
	{0, 2, 1, 1},
	{2, 1, 1, 1},
	{2, 0, 1, 1},
	{2, 2, 0, 1},
	{1, 2, 0, 1},
	{0, 2, 0, 1},
	{2, 1, 0, 1},
	{2, 0, 0, 1},
	// forward
	{1, 1, 1, 0},
	{0, 1, 1, 0},
	{1, 0, 1, 0},
	{0, 0, 1, 0},
	{1, 1, 0, 0},
	{0, 1, 0, 0},
	{1, 0, 0, 0},
	// self {0, 0, 0, 0},
};
void conversion_helper2::add_suggested(job_id id) {
	for(auto& sig: neighbors_reverse) {
		auto id2=id;
		if(sig[0]==2) {
			if(id2.x<xsize)
				continue;
			id2.x-=xsize;
		} else if(sig[0]==1) {
			if(id2.x+xsize>=width)
				continue;
			id2.x+=xsize;
		}
		if(sig[1]==2) {
			if(id2.y<ysize)
				continue;
			id2.y-=ysize;
		} else if(sig[1]==1) {
			if(id2.y+ysize>=height)
				continue;
			id2.y+=ysize;
		}
		if(sig[2]==2) {
			if(id2.z<zsize)
				continue;
			id2.z-=zsize;
		} else if(sig[2]==1) {
			if(id2.z+zsize>=depth)
				continue;
			id2.z+=zsize;
		}

		auto [it, ins]=_job_states.try_emplace(id2);
		if(ins) {
			it->second.st=job_state::init;
			it->second.prio=job_prio::suggested;
			//gapr::print("add que1: ", id2.x, "/", id2.y, "/", id2.z);
			if(sig[3])
				_job_que2.push_front(id2);
			else
				_job_que1.push_front(id2);
		}
	}
}

#else
void prepare_conversion_plugin(const SplitArgs& args, const std::filesystem::path& workdir, unsigned int ncpu, std::size_t) {
}
void resume_conversion_plugin(const std::filesystem::path& workdir, unsigned int ncpu, std::size_t cachesize, ServerArgs&& srv) {
}
#endif
