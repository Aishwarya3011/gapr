#include "split.hh"

#include <cctype>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <initializer_list>
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
#include <iomanip>

#include <boost/asio/thread_pool.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
//#include <boost/asio/post.hpp>

//#include <boost/beast/core.hpp>

#include "gapr/detail/nrrd-output.hh"
#include "gapr/str-glue.hh"
#include "gapr/utility.hh"
#include "gapr/mem-file.hh"
#include "gapr/exception.hh"

#include <sys/stat.h>
#include <tiffio.h>
#include <unordered_set>

#include "../gather/stop-signal.hh"
#include "loadtiff.hh"
#include "dshelper.hh"
#include "mem-str.hh"
#include "nethelper.hh"
#include "cache-helper.hh"
#include "savewebm.hh"

#include "../corelib/libc-wrappers.hh"
#include "../corelib/windows-compat.hh"

#include "logger.h"

/*! features
 * / fast coarse downsample for navigation
 * / on-demand cube conversion
 * / cache active tiles
 * / pre-convert candidate cubes
 * / regularize (optimize) input files
 * / generate full downsample in the end
 * / distributed cube compression
 * / fully resumable (after preparation)
 */


struct prepare_helper {
	unsigned int width=0;
	unsigned int height=0;
	unsigned int depth=0;

	unsigned short spp=0;
	unsigned short bps=0;

	std::vector<std::pair<const std::string, unsigned int>> inputpairs;
	std::shared_ptr<dshelper> dsaux;

	void scan_files(const std::vector<std::string>& inputfiles, double xres, double yres, double zres, unsigned int ncpu, double ratio) {
		std::vector<std::thread> threads;

		TIFFSetWarningHandler(nullptr);
		bool first_slice=true;
		for(size_t i=0; i<inputfiles.size(); i++) {
			if(inputfiles[i]=="BLACK" || inputfiles[i]=="WHITE") {
				sched_downsample(inputfiles[i], 0, 65535);
				++depth;
				continue;
			}

			Tiff tif{inputfiles[i].c_str(), "r"};
			if(!tif)
				throw gapr::str_glue("Error opening TIFF file: '", inputfiles[i], "'. Abort.");
			do {
				unsigned int slice_width, slice_height;
				unsigned short slice_spp, slice_bps;
				auto dir=TIFFCurrentDirectory(tif);
				checkSlice(tif, &slice_width, &slice_height, &slice_spp, &slice_bps);
				if(first_slice) {
					width=slice_width;
					height=slice_height;
					spp=slice_spp;
					bps=slice_bps;
					first_slice=false;
					guess_downsample(xres, yres, zres);

					if(bps<=8) {
						for(unsigned int i=0; i<ncpu; ++i)
							threads.push_back(std::thread{&prepare_helper::downsample<uint8_t>, this, ratio});
						dsaux=dshelper::create_u8();
					} else if(bps<=16) {
						for(unsigned int i=0; i<ncpu; ++i)
							threads.push_back(std::thread{&prepare_helper::downsample<uint16_t>, this, ratio});
						dsaux=dshelper::create_u16();
					} else {
						throw std::runtime_error{"unsuported bps"};
					}
					dsaux->init(width, height, spp, bps, dsx, dsy, dsz);
				} else {
					if(width!=slice_width || height!=slice_height)
						//throw gapr::str_glue("Inconsistant width/height among slices.");
						gapr::print("Inconsistant width/height among slices:", i, ": ", inputfiles[i]);
					if(spp!=slice_spp)
						throw gapr::str_glue("Inconsistant number of spp among slices.");
					if(bps!=slice_bps)
						throw gapr::str_glue("Inconsistant number of bits per channel among slices.");
				}
				sched_downsample(inputfiles[i], dir, ncpu);
				++depth;
			} while(TIFFReadDirectory(tif));
		}
		gapr::print("before sched_fini");
		sched_finish();
		gapr::print("after sched_fini");
		dsd=(depth+dsz-1)/dsz;

		for(unsigned int i=0; i<threads.size(); ++i)
			threads[i].join();
		gapr::print("joined all");
	}

	bool _scan_fini{false};
	std::size_t _jobidx{0};
	std::mutex _mtx;
	std::condition_variable _cv_job;
	std::condition_variable _cv_spc;
	void sched_downsample(const std::string& file, unsigned int dir, unsigned int ncpu) {
		std::unique_lock lck{_mtx};
		while(_jobidx+2*ncpu<inputpairs.size())
			_cv_spc.wait(lck);
		inputpairs.emplace_back(file, dir);
		lck.unlock();
		_cv_job.notify_one();
	}
	void sched_finish() {
		{
			std::scoped_lock lck{_mtx};
			_scan_fini=true;
		}
		_cv_job.notify_all();
	}
	template<typename T>
	void downsample(double ratio) {
		std::mt19937 rng{std::random_device{}()};
		while(true) {
			std::unique_lock lck{_mtx};
			while(true) {
				if(_jobidx<inputpairs.size())
					break;
				if(_scan_fini) {
					gapr::print("handle: finish");
					return;
				}
				_cv_job.wait(lck);
			}
			auto curr_z=_jobidx++;
			auto [file, idx]=inputpairs[curr_z];
			lck.unlock();
			_cv_spc.notify_all();

			if(file=="BLACK" || file=="WHITE")
				continue;

			//int adv;
			//char mod[16];
			//sscanf(getenv("GAPR_TIFF_MODE"), "%s %d", mod, &adv);
			Tiff tif{file.c_str(), "rm"};
			gapr::print("handle: ", file);
			hint_sequential_read(tif);
			if(!TIFFSetDirectory(tif, idx))
				throw std::runtime_error{"failed to set directory"};
			TileReader aux{width, height, spp, bps};
			aux.init<T>(tif);

			std::vector<uint8_t> tmpbuf;
			std::vector<T> tile;
			tile.resize(aux.tileW*aux.tileH*spp);

			unsigned int y_start, y_end;
			{
				unsigned int hh=aux.tileH;
				while(hh<ratio*height) {
					if(aux.tileH+hh<=height) {
						hh+=aux.tileH;
						continue;
					}
					break;
				}
				y_start=rng()%((height-hh)/aux.tileH+1)*aux.tileH;
				y_end=std::min(y_start+hh, height);
			}
			// XXX select and convert
			for(unsigned int y=y_start; y<y_end; y+=aux.tileH) {
				for(unsigned int x=0; x<width; x+=aux.tileW) {
					aux(tif, tmpbuf, x, y, tile.data());
					dsaux->update(curr_z, x, y, aux.tileW, aux.tileH, tile.data());
				}
			}
			hint_discard_cache(tif);
			gapr::print("///handle: ", file);
		}
	}

	void write_state(const std::filesystem::path& workdir, double xres, double yres, double zres) {
		dsaux->writecache(workdir/"downsample", true);
		dsaux->writecache(workdir/"downsample", false);

		std::ofstream fs{workdir/"state"};
		if(!fs)
			throw std::runtime_error{"failed to open state file"};
		fs<<"sizes "<<width<<':'<<height<<':'<<depth<<"\n";
		fs<<"spp "<<spp<<"\n";
		fs<<"bps "<<bps<<"\n";
		fs<<"cubesizes "<<xsize<<':'<<ysize<<':'<<zsize<<"\n";
		fs<<"resolution "<<xres<<':'<<yres<<':'<<zres<<"\n";
		fs<<"downsample "<<dsx<<':'<<dsy<<':'<<dsz<<"\n";
		for(auto& [f,i]: inputpairs)
			fs<<"file "<<i<<":"<<f<<'\n';
		if(fs.close(), !fs)
			throw std::runtime_error{"failed to save state file"};
	}

	void print_info(double xres, double yres, double zres) {
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

	unsigned int xsize=0;
	unsigned int ysize=0;
	unsigned int zsize=0;

	unsigned int dsx=0;
	unsigned int dsy=0;
	unsigned int dsz=0;

	unsigned int dsw=0;
	unsigned int dsh=0;
	unsigned int dsd=0;

	void guess_cubesizes(double xres, double yres, double zres) {
		// XXX
		if(xsize<=0) {
			uint64_t xn=1, yn=1, zn=1;
			xsize=((width+xn-1)/xn+7)/8*8;
			ysize=((height+yn-1)/yn+7)/8*8;
			zsize=((depth+zn-1)/zn+7)/8*8;
			while(xsize*ysize*zsize>16*1024*1024) {
				int maxis=0;
				double mlen=xsize*xres;
				if(ysize*yres>mlen) {
					mlen=ysize*yres;
					maxis=1;
				}
				if(zsize*zres>mlen) {
					mlen=zsize*zres;
					maxis=2;
				}
				if(maxis==0) {
					xn++;
				} else if(maxis==1) {
					yn++;
				} else {
					zn++;
				}
				xsize=((width+xn-1)/xn+7)/8*8;
				ysize=((height+yn-1)/yn+7)/8*8;
				zsize=((depth+zn-1)/zn+7)/8*8;
			}
			if(zn==1)
				zsize=depth;
		}
	}
	void guess_downsample(double xres, double yres, double zres) {
		if(dsx<=0) {
			dsx=1;
			dsy=1;
			dsz=1;
			dsw=(width+dsx-1)/dsx;
			dsh=(height+dsy-1)/dsy;
			while(dsw*dsh*(dsw+dsh)/2>320*1024*1024) {
				int maxis=0;
				auto mres=dsx*xres;
				if(dsy*yres<mres) {
					mres=dsy*yres;
					maxis=1;
				}
				if(dsz*zres<mres) {
					mres=dsz*zres;
					maxis=2;
				}
				if(maxis==0) {
					dsx++;
				} else if(maxis==1) {
					dsy++;
				} else {
					dsz++;
				}
				dsw=(width+dsx-1)/dsx;
				dsh=(height+dsy-1)/dsy;
			}
		} else {
			dsw=(width+dsx-1)/dsx;
			dsh=(height+dsy-1)/dsy;
		}
	}
	/////////
	//
#if 0
	cachesize{_cachesize},
	ncpu{_ncpu},
	tiffidx{0}, tiffd{nullptr},
	imagebufs{}, tempfs(channels), nrrdfs(channels), tempfn{},
	start_idx_ds{0},
	bufds{}, bufdssum{}, writers{},
	error{false}, finished{false},
	mtx{}, cv0{}, cv1{},
	readQue{}, writeQue{}, writeCubeQue{}, writeDsQue{},
	readQueSize{0}, writeQueSize{0}, writeCubeQueSize{0}, writeDsQueSize{0}
#endif

};

// thread cube
// thread tiff
// thread tiff/i
//
void prepare_conversion(const SplitArgs& args, const std::filesystem::path& workdir, unsigned int ncpu, std::size_t) {
	if(!args.plugin.empty())
		return prepare_conversion_plugin(args, workdir, ncpu, 0);
	std::filesystem::create_directories(workdir);

	prepare_helper helper{};
	helper.dsx=args.dsx;
	helper.dsy=args.dsy;
	helper.dsz=args.dsz;
	helper.scan_files(args.inputfiles, args.xres, args.yres, args.zres, ncpu, args.ds_ratio);

	helper.xsize=args.xsize;
	helper.ysize=args.ysize;
	helper.zsize=args.zsize;
	helper.guess_cubesizes(args.xres, args.yres, args.zres);

	helper.write_state(workdir, args.xres, args.yres, args.zres);
	helper.print_info(args.xres, args.yres, args.zres);
}

#if 0
#if 0
{
	writeCatalogSimple();
	auto time_end=std::chrono::system_clock::now();
	time_delta=std::chrono::duration_cast<std::chrono::milliseconds>(time_end-time_start);
	std::cerr<<"["<<time_delta<<"] Complete ("<<time_end<<").\n";
}
#endif
template <typename T>
void Slice2Cube<T>::writeCatalogSimple() {
	std::ofstream fs;
	std::ostringstream oss;
	oss<<outdir<<"/catalog";
	auto fn=oss.str();
	fs.open(fn);
	if(!fs)
		throwError("Failed to open file: ", fn, ".");
	do_write
	///////////////////////////////////
	fs.close();
	if(!fs)
		throwError("Failed to close file: ", fn, ".");
}
#endif

const unsigned int ncpuio{3};
struct conversion_helper {
	std::optional<std::filesystem::path> tiled_dir{};
	gapr::file_stream _log;
	std::shared_ptr<void> _log_unlock{};

	unsigned int width=0;
	unsigned int height=0;
	unsigned int depth=0;
	unsigned short spp=0;
	unsigned short bps=0;

	unsigned int xsize=0;
	unsigned int ysize=0;
	unsigned int zsize=0;
	unsigned int dsx=0;
	unsigned int dsy=0;
	unsigned int dsz=0;
	unsigned int dsw, dsh, dsd;

	double xres, yres, zres;

	std::vector<std::tuple<const std::string, unsigned int, std::filesystem::path>> inputpairs;
	std::shared_ptr<dshelper> dsaux;

	std::string token{};
	uint64_t downsample_ver{0};
	uint64_t downsample_ver_avg{0};

	boost::asio::io_context _ctx;
	boost::asio::thread_pool _pool;
	boost::asio::thread_pool _pool_io;
	boost::asio::steady_timer _timer;
	network_helper netaux;
	std::shared_ptr<cache_helper> cacheaux;
	stop_signal _stop_signal{};

	std::string user{};
	std::string passwd{};
	std::string group{};
	std::string api_root{};

	unsigned int ncpu;
	cube_enc_opts enc_opts;

	std::string plugin{};

	explicit conversion_helper(const std::optional<std::filesystem::path>& tiled_dir, unsigned int ncpu, ServerArgs&& srv):
		tiled_dir{tiled_dir}, _ctx{}, _pool{ncpu}, _pool_io{ncpuio}, _timer{_ctx},
		netaux{_ctx, std::move(srv.host), std::to_string(srv.port)},
		user{std::move(srv.user)},
		passwd{std::move(srv.passwd)},
		group{std::move(srv.group)},
		api_root{std::move(srv.api_root)},
		ncpu{ncpu}
	       	{
		}


	void write_catalog(std::ostream& str) {
		char tag[10];
		auto maxsig=dsaux->max_value();
		for(unsigned int i=0; i<spp; ++i) {
			snprintf(tag, 10, "%02u", i);
			str<<"[CH"<<tag<<"_MIP]\n";
			str<<"location=ch"<<tag<<"-mip.webm\n";
			str<<"origin=0 0 0\n";
			str<<"direction="<<xres*dsx<<" 0 0 0 "<<yres*dsy<<" 0 0 0 "<<zres*dsz<<"\n";
			str<<"size="<<dsw<<" "<<dsh<<" "<<dsd<<"\n";
			str<<"range=0 "<<maxsig*1.5<<"\n";
			str<<"[CH"<<tag<<"_AVG]\n";
			str<<"location=ch"<<tag<<"-avg.webm\n";
			str<<"origin=0 0 0\n";
			str<<"direction="<<xres*dsx<<" 0 0 0 "<<yres*dsy<<" 0 0 0 "<<zres*dsz<<"\n";
			str<<"size="<<dsw<<" "<<dsh<<" "<<dsd<<"\n";
			str<<"range=0 "<<1.5<<"\n";
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
	std::filesystem::path workdir;

	bool is_valid(job_id id) {
		auto check=[](unsigned int v, unsigned int b, unsigned int s) {
			return v<s && (v%b==0);
		};
		do {
			if(!check(id.x, xsize, width))
				break;
			if(!check(id.y, ysize, height))
				break;
			if(!check(id.z, zsize, depth))
				break;
			if(id.chan!=0)
				break;
			return true;
		} while(false);
		fprintf(stderr, "%u %u %d\n", id.x, id.y, id.z);
		return false;
	}

	void load_state(const std::filesystem::path& workdir) {
		this->workdir=workdir;
		std::cerr<<"loading state...";

		_log={(workdir/"state").c_str(), "r+b"};
		if(!_log)
			throw std::runtime_error{"failed to open log file"};
		struct fd_unlocker {
			int fd;
			explicit fd_unlocker(FILE* f): fd{-1} {
				auto d=::fileno(f);
				if(d==-1 || -1==::lock_fd(d))
					return;
				fd=d;
			}
			~fd_unlocker() {
				if(fd!=-1)
					::unlock_fd(fd);
			}
			explicit operator bool() const noexcept { return fd!=-1; }
		};
		auto unlocker=std::make_shared<fd_unlocker>(_log);
		if(!*unlocker)
			throw gapr::reported_error{"failed to lock log file"};
		_log_unlock=std::move(unlocker);

		std::array<char, BUFSIZ> line;
		while(std::fgets(&line[0], line.size(), _log)) {
			auto lineend=std::strchr(&line[0], '\n');
			if(lineend==nullptr)
				throw std::runtime_error{"failed to read a valid line"};
			*lineend='\0';
			std::size_t linelen=lineend-&line[0];
			unsigned int ii=0;
			while(ii<linelen && !std::isspace(line[ii]))
				++ii;
			std::string_view cmd{&line[0], ii};
			if(ii<linelen) {
				do ++ii; while(ii<linelen && std::isspace(line[ii]));
			}
			std::string_view args{&line[ii], linelen-ii};

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
			} else if(cmd=="resolution") {
				if(gapr::parse_tuple(&args[0], &xres, &yres, &zres)!=3)
					throw std::runtime_error{"failed to parse resolution"};
			} else if(cmd=="file") {
				unsigned int idx;
				std::string path;
				if(gapr::parse_tuple(&args[0], &idx, &path)!=2)
					throw std::runtime_error{"failed to parse filename"};

				inputpairs.emplace_back(std::move(path), idx, "");
			} else if(cmd=="token") {
				if(gapr::parse_tuple(&args[0], &token)!=1)
					throw std::runtime_error{"failed to parse token"};
			} else if(cmd=="downsample-ready") {
				uint64_t ver;
				std::string_view tag;
				auto n=gapr::parse_tuple(&args[0], &ver, &tag);
				if(n==1) {
					downsample_ver=ver;
				} else if(n==2 && tag=="avg") {
					downsample_ver_avg=ver;
				} else if(n==2 && tag=="mip") {
					downsample_ver=ver;
				} else
					throw std::runtime_error{"failed to parse downsample-ready"};
			} else if(cmd=="cube-needed") {
				job_id id;
				if(!id.parse(args))
					throw std::runtime_error{"failed to parse cube"};
				assert(is_valid(id));
				add_needed(id, true);
				add_suggested(id);
			} else if(cmd=="cube-ready") {
				job_id id;
				if(!id.parse(args))
					throw std::runtime_error{"failed to parse cube"};
				assert(is_valid(id));
				assume_ready(id);
			} else if(cmd=="plugin") {
				plugin=args;
				std::cerr<<"  USING PLUGIN\n";
				return;
			} else {
				throw std::runtime_error{"unknown line"};
			}
		}
		if(std::ferror(_log))
			throw std::runtime_error{"failed to read line"};
		assert(std::feof(_log));

		auto read_last=std::ftell(_log);
		if(read_last==-1)
			throw std::runtime_error{"failed to get pos"};
		if(std::fseek(_log, 0, SEEK_END)==-1)
			throw std::runtime_error{"failed to seek"};
		assert(read_last==std::ftell(_log));

		dsw=(width+dsx-1)/dsx;
		dsh=(height+dsy-1)/dsy;
		dsd=(depth+dsz-1)/dsz;
		if(bps<=8) {
			dsaux=dshelper::create_u8();
		} else if(bps<=16) {
			dsaux=dshelper::create_u16();
		} else {
			throw std::runtime_error{"unsuported bps"};
		}
		dsaux->init(width, height, spp, bps, dsx, dsy, dsz);
		dsaux->loadcache(workdir/"downsample", depth);
		_last_saveds=std::chrono::steady_clock::now();

		// XXX
		ncpusweep=3;
		auto nnn=getenv("GAPR_SWEEP");
		if(nnn)
			ncpusweep=atoi(nnn);

		for(unsigned int zz=0; zz<this->depth; ++zz) {
			_queued_slices.push_back(zz);

			unsigned int need_sweep{0};
			/* do not discover file changes after this initialization
			 * thus other processes should not modify anything
			 * XXX maybe there is write lock for directories.
			 */
			auto& [path, idx, pathtiled]=inputpairs[zz];
			if(tiled_dir) {
				auto outpath=get_tiled_path(path, idx);
				if(std::filesystem::exists(outpath)) {
					pathtiled=std::move(outpath);
				} else {
					if(!std::filesystem::exists(path))
						throw std::runtime_error{"file missing"};
					need_sweep+=100;
				}
			}
			if(!dsaux->finished(zz))
				++need_sweep;
			if(need_sweep)
				_queued_slices_chk.emplace(zz, need_sweep);
		}
		std::shuffle(_queued_slices.begin(), _queued_slices.end(), std::random_device{});

		std::cerr<<"  DONE\n";
	}

	template<bool Sync=false, typename... Args>
	void write_log(const char* format, Args... args) {
		assert(_ctx.get_executor().running_in_this_thread());
		if(std::fprintf(_log, format, args...)<0)
			throw std::runtime_error{"failed to write log"};
		if(std::fflush(_log)!=0)
			throw std::runtime_error{"failed to flush"};
		if constexpr(Sync) {
			auto fd=::fileno(_log);
			if(auto r=::fdatasync(fd); r!=0)
				throw std::runtime_error{"failed to sync"};
		}
	}

	std::string api_path(std::string_view rel) {
		if(api_root.empty())
			return std::string{"/api/"}+=rel;
		//"/yanlab-upload/"
		std::string r{api_root};
		return r+=rel;
	}
	template<typename Cb>
	void async_put(std::string_view path, gapr::mem_file&& file, Cb&& cb) {
		auto sess=std::make_shared<network_helper::session>(std::move(file), std::forward<Cb>(cb));
		sess->req_put.method(boost::beast::http::verb::put);
		sess->req_put.target(api_path(path));
		netaux.do_session(*sess, _ctx.get_executor());
	}
	template<typename Cb>
	void async_get(std::string_view path, Cb&& cb) {
		auto sess=std::make_shared<network_helper::session>(std::forward<Cb>(cb));
		sess->req_get.method(boost::beast::http::verb::get);
		sess->req_get.target(api_path(path));
		netaux.do_session(*sess, _ctx.get_executor());
	}
	
	// void upload_catalog() {
	// 	std::cerr<<"upload catalog ...";
	// 	auto ex=boost::asio::require(_ctx.get_executor(), boost::asio::execution::outstanding_work.tracked);
	// 	boost::asio::post(_pool, [this,ex]() {
	// 		gapr::mem_file cat;
	// 		{
	// 			mem_file_out str{gapr::mutable_mem_file{true}};
	// 			write_catalog(str);
	// 			if(!str.flush())
	// 				throw std::runtime_error{"failed to write catalog"};
	// 			cat=std::move(str).mem_file();
	// 		}
	// 		boost::asio::post(ex, [this,cat=std::move(cat)]() mutable {
	// 			async_put("catalog/"+group+"/"+passwd, std::move(cat), [this](std::string&& res, const /*boost::beast::http::response_header<>*/auto& hdr) {
	// 				if(hdr.result()!=boost::beast::http::status::ok)
	// 					throw std::runtime_error{std::move(res)};
	// 				write_log<true>("token %s\n", res.c_str());
	// 				token=std::move(res);
	// 				std::cerr<<"   DONE\n";
	// 				upload_cubes();
	// 			});
	// 		});
	// 	});
	// }

	void upload_catalog() {
		std::cerr << "upload catalog ...";
		Logger::instance().logMessage(__FILE__, "Uploading catalog...");
		
		auto ex = boost::asio::require(_ctx.get_executor(), boost::asio::execution::outstanding_work.tracked);
		
		boost::asio::post(_pool, [this, ex]() {
			gapr::mem_file cat;
			{
				Logger::instance().logMessage(__FILE__, "Writing catalog to memory...");
				
				mem_file_out str{gapr::mutable_mem_file{true}};
				write_catalog(str);
				
				if (!str.flush()) {
					Logger::instance().logMessage(__FILE__, "Failed to flush catalog to memory.");
					throw std::runtime_error{"failed to write catalog"};
				}
				
				Logger::instance().logMessage(__FILE__, "Catalog written to memory.");
				cat = std::move(str).mem_file();
			}

			Logger::instance().logMessage(__FILE__, "Posting catalog to executor...");
			
			boost::asio::post(ex, [this, cat = std::move(cat)]() mutable {
				// Log the size of the catalog being uploaded
				Logger::instance().logMessage(__FILE__, "Catalog size: " + std::to_string(cat.size()));
				
				async_put("catalog/" + group + "/" + passwd, std::move(cat), 
						[this](std::string&& res, const auto& hdr) {
							if (hdr.result() != boost::beast::http::status::ok) {
								Logger::instance().logMessage(__FILE__, "Failed to upload catalog. Server responded with: " + res);
								throw std::runtime_error{std::move(res)};
							}
							
							Logger::instance().logMessage(__FILE__, "Catalog uploaded successfully. Server response: " + res);
							write_log<true>("token %s\n", res.c_str());
							token = std::move(res);
							
							std::cerr << "   DONE\n";
							upload_cubes();  
						});
			});
		});
	}

	void upload_downsample() {
		Logger::instance().logMessage(__FILE__, "Entered upload_downsample: ");
		assert(_ctx.get_executor().running_in_this_thread());
		std::cerr<<"upload downsample ...";
		auto ex=boost::asio::require(_ctx.get_executor(), boost::asio::execution::outstanding_work.tracked);
		++_n_running;
		++_n_running;
		auto mipver=downsample_ver;
		auto avgver=downsample_ver_avg;
		boost::asio::post(_pool, [this,ex,mipver,avgver]() {
			std::promise<std::vector<uint16_t>> avg;
			auto avgfut=avg.get_future();
			std::promise<gapr::mem_file> avgfile;
			auto avgfilefut=avgfile.get_future();
			boost::asio::post(_pool, [this,avgfut=std::move(avgfut), avgfile=std::move(avgfile)]() mutable {
				auto avg=avgfut.get();
				if(avg.empty()) {
					avgfile.set_value(gapr::mem_file{});
					return;
				}
				auto eopts=enc_opts;
				eopts.threads=2;
				gapr::mem_file ret{convert_webm(avg.data(), dsw, dsh, dsd, eopts)};
				if(false) {
					std::ofstream fs{"/tmp/testconv.nrrd"};
					gapr::nrrd_output nrrd{fs, true};
					nrrd.header();
					nrrd.finish(avg.data(), dsw, dsh, dsd);
				}
				avgfile.set_value(std::move(ret));
			});
			auto [ver, file]=dsaux->downsample(std::move(avg), enc_opts, mipver, avgver);
			boost::asio::post(ex, [this,file=std::move(file),ver=ver]() mutable {
				assert(_ctx.get_executor().running_in_this_thread());
				--_n_running;
				if(!file)
					return;
				async_put("data/"+group+"/ch00-mip.webm", std::move(file), [this,ver](std::string&& res, const boost::beast::http::response_header<>& hdr) {
					if(hdr.result()!=boost::beast::http::status::ok)
						throw std::runtime_error{std::move(res)};
					write_log("downsample-ready %lu:mip\n", ver);
					downsample_ver=ver;
					std::cerr<<"   DONE\n";
				});
				upload_cubes();
			});
			file=avgfilefut.get();
			boost::asio::post(ex, [this,file=std::move(file),ver=ver]() mutable {
				assert(_ctx.get_executor().running_in_this_thread());
				--_n_running;
				if(!file)
					return;
				async_put("data/"+group+"/ch00-avg.webm", std::move(file), [this,ver](std::string&& res, const boost::beast::http::response_header<>& hdr) {
					if(hdr.result()!=boost::beast::http::status::ok)
						throw std::runtime_error{std::move(res)};
					write_log("downsample-ready %lu:avg\n", ver);
					downsample_ver_avg=ver;
					std::cerr<<"   DONE\n";
				});
				upload_cubes();
			});
		});
		upload_cubes();
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
	unsigned int _n_running_io{0};

	struct file_st {
		std::optional<Tiff> tif;
		unsigned int zz;
		std::optional<TileReader> aux;
		std::chrono::steady_clock::time_point ts;
		std::chrono::steady_clock::time_point time_sweep_start, time_sweep_end;
		bool busy{false};
		bool busy_sweep{false};
		std::atomic<bool> tiled_avail{false};
		std::filesystem::path tiled_path{};
		std::filesystem::path remove_later{};
	};
	std::unordered_map<unsigned int, file_st> _files;

	file_st* open_file(unsigned int zz, bool sweeping=false) {
		constexpr unsigned int max_files_a=480*2;
		constexpr unsigned int max_files_b=360*2;
		if(_files.size()>max_files_a) {
			auto it_end=_files.end();
			std::vector<decltype(it_end)> its;
			for(auto it=_files.begin(); it!=it_end; ++it)
				if(!it->second.busy && !it->second.busy_sweep)
					its.push_back(it);
			std::sort(its.begin(), its.end(), [](auto a, auto b) {
				return a->second.ts>b->second.ts;
			});
			while(!its.empty()) {
				auto it=its.back();
				its.pop_back();
				if(it->second.tif)
					hint_discard_cache(*it->second.tif);
				_files.erase(it);
				if(_files.size()<=max_files_b)
					break;
			}
		}
		auto [it, ins]=_files.try_emplace(zz);
		assert(ins);
		auto file=&it->second;
		file->zz=zz;
		if(sweeping) {
			file->ts=std::chrono::steady_clock::time_point{};
			file->busy_sweep=true;
		} else {
			file->ts=std::chrono::steady_clock::now();
			file->busy=true;
		}
		return file;
	}

	void handle_slice(job_id id, unsigned int zz, file_st* file, job_st* job, const char* que) {
		assert(_ctx.get_executor().running_in_this_thread());
		//gapr::print("handle slice [", _n_running, "/", que, "]: ", id.x, "/", id.y, "/", id.z, "/", zz);
		auto ex=boost::asio::require(_ctx.get_executor(), boost::asio::execution::outstanding_work.tracked);
		++_n_running_io;
		queue_sweep(zz);

		if(job->time_read_start==std::chrono::steady_clock::time_point{})
			job->time_read_start=std::chrono::steady_clock::now();

		boost::asio::post(_pool_io, [this,ex,id,zz,file,job]() {
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

	std::filesystem::path get_tiled_path(const std::filesystem::path& path, unsigned int diri) const {
		assert(tiled_dir);
		auto outpath=tiled_dir->empty()?path.parent_path():*tiled_dir;
		outpath/=path.stem();
		outpath+="-tiled";
		if(diri>0)
			(outpath+='-')+=std::to_string(diri);
		outpath.replace_extension(path.extension());
		assert(path!=outpath);
		return outpath;
	}
	template<typename T>
	void handle_slice_work(job_id id, unsigned int zz, file_st* file, job_st* job) {
		assert(_pool_io.get_executor().running_in_this_thread());
		assert(zz==file->zz);
		auto [_path, diri, pathtiled]=inputpairs[zz];
		std::filesystem::path path=_path;
		T* data{nullptr};
		if constexpr(std::is_same_v<T, uint16_t>) {
			data=job->data.get();
		}

		// XXX
		if(path=="BLACK" || path=="WHITE")
			return;

		auto time_begin=std::chrono::steady_clock::now();
		if(!file->tif) {
			assert(file->busy);
			if(!pathtiled.empty()) {
				assert(tiled_dir);
				file->tif.emplace(pathtiled.c_str(), "r");
				assert(TIFFIsTiled(*file->tif));
			} else {
				file->tif.emplace(path.c_str(), "r");
				if(!TIFFSetDirectory(*file->tif, diri))
					throw std::runtime_error{"failed to set directory"};
			}
			file->aux.emplace(width, height, spp, bps);
			file->aux->init<T>(*file->tif);
		}

		std::vector<uint8_t> tmpbuf;
		auto tileW=file->aux->tileW;
		auto tileH=file->aux->tileH;
		unsigned int y=id.y/tileH*tileH;
		unsigned int read_total{0}, read_nohit{0};
		do {
			unsigned int x=id.x/tileW*tileW;
			do {
				++read_total;
				auto cache=cacheaux->get(zz, x, y);
				if(!cache) {
					++read_nohit;
					auto tile=std::make_unique<T[]>(tileW*tileH*spp);
					(*file->aux)(*file->tif, tmpbuf, x, y, tile.get());
					if(pathtiled.empty())
						dsaux->update(zz, x, y, tileW, tileH, tile.get());
					cache=cacheaux->put(zz, x, y, tileW, tileH, spp, std::move(tile));
					assert(cache);
				}
				assert(cache->check(tileW, tileH, spp));
				auto ptr=cache->ptr(T{})+tileW*tileH*id.chan;
				assert(zz>=id.z);
				assert(zz<id.z+zsize);
				assert(zz<depth);
				auto optr=data+(zz-id.z)*xsize*ysize;
				auto yy0=std::max(y, id.y);
				auto yy1=std::min(std::min(y+tileH, id.y+ysize), height);
				auto xx0=std::max(x, id.x);
				auto xx1=std::min(std::min(x+tileW, id.x+xsize), width);
				assert(yy0<yy1);
				assert(xx0<xx1);
				for(auto yy=yy0; yy<yy1; ++yy)
					std::memcpy(&optr[(xx0-id.x)+(yy-id.y)*xsize], &ptr[(xx0-x)+(yy-y)*tileW], (xx1-xx0)*sizeof(T));

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
		Logger::instance().logMessage(__FILE__, "Entered handle_slice_done: ");
		assert(_ctx.get_executor().running_in_this_thread());
		--_n_running_io;
		//gapr::print("handle slice [", _n_running, "]: ", id.x, "/", id.y, "/", id.z, "/", zz);

		file->ts=std::chrono::steady_clock::now();
		assert(file->busy==true);
		file->busy=false;
		if(file->tiled_avail.exchange(false)) {
			assert(!file->busy_sweep);
			assert(tiled_dir);
			file->tif.reset();
			cacheaux->clear(zz);
			auto todel=std::move(file->remove_later);
			std::error_code ec;
			if(!todel.empty())
				std::filesystem::remove(todel, ec);
			assert(!file->tiled_path.empty());
			std::get<2>(inputpairs[zz])=std::move(file->tiled_path);
		}
		auto& st=_job_states.at(id);
		auto it=st.todo.find(zz);
		assert(it!=st.todo.end());
		st.todo.erase(it);
		assert(st.st==job_state::loading);
		if(st.todo.empty()) {
			st.st=job_state::ready;
			handle_cube(id, job);
		}
		upload_cubes();
	}

	void handle_cube(job_id id, job_st* job) {
		Logger::instance().logMessage(__FILE__, "Entered handle_cube: ");
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
			gapr::mem_file ret{convert_webm(data.get(), xsize, ysize, zsize, enc_opts)};
			auto t_end=std::chrono::steady_clock::now();
			job->time_conv=std::chrono::duration_cast<std::chrono::milliseconds>(t_end-t_begin);
			boost::asio::post(ex, [this,id,job,ret=std::move(ret)]() mutable {
				handle_cube_done(id, job, std::move(ret));
			});
		});
	}
	
	void handle_cube_done(job_id id, job_st* job, gapr::mem_file&& file) {
		Logger::instance().logMessage(__FILE__, "Entered handle_cube_done: ");
		assert(_ctx.get_executor().running_in_this_thread());
		--_n_running;
		gapr::print("handle cube [", _n_running, "]: ", id.x, "/", id.y, "/", id.z);

		auto path=id.format();
		auto t_begin=std::chrono::steady_clock::now();
		auto f_size=file.size();
		async_put("data/"+group+"/"+path, std::move(file), [this,job,f_size,t_begin,path](std::string&& res, const auto& hdr) {
			if(hdr.result()!=boost::beast::http::status::ok){
				// Log the Content-Length header as an integer
				auto content_length = hdr["Content-Length"];
				int content_length_value = 0;
				if (!content_length.empty()) {
					content_length_value = std::stoi(std::string(content_length));
				}
				Logger::instance().logMessage(__FILE__, "Content-Length: " + std::to_string(content_length_value));

				// Log the Content-Type header as a string
				auto content_type = hdr["Content-Type"];
				Logger::instance().logMessage(__FILE__, "Content-Type: " + std::string(content_type));

				// Log server error message
				Logger::instance().logMessage(__FILE__, "Cube upload failed. Server response: " + res);
				throw std::runtime_error{"Failed to upload cube. Server response: " + res};
			}
			auto t_end=std::chrono::steady_clock::now();

			write_log("cube-ready %s\n", path.c_str());
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
				for(unsigned int z=i.z; z<i.z+zsize && z<depth; ++z)
					st.todo.emplace(z);
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

	std::deque<unsigned int> _queued_slices;
	std::unordered_map<unsigned int, unsigned int> _queued_slices_chk;
	unsigned int ncpusweep;
	void queue_sweep(unsigned int z) {
		assert(_ctx.get_executor().running_in_this_thread());
		auto it=_queued_slices_chk.find(z);
		if(it!=_queued_slices_chk.end() && it->second>=100)
			_queued_slices.push_front(z);
	}
	std::tuple<unsigned int, file_st*> find_slice_to_complete() {
		assert(_ctx.get_executor().running_in_this_thread());
		while(!_queued_slices.empty()) {
			auto z=_queued_slices.front();
			if(_queued_slices_chk.find(z)==_queued_slices_chk.end()) {
				_queued_slices.pop_front();
				continue;
			}
			assert(z<this->depth);
			auto it=_files.find(z);
			if(it==_files.end()) {
				_queued_slices.pop_front();
				return {z, nullptr};
			}
			if(!it->second.busy_sweep) {
				auto file=&it->second;
				file->busy_sweep=true;
				_queued_slices.pop_front();
				return {z, file};
			}
			_queued_slices.pop_front();
		}
		return {this->depth+10, nullptr};
	}

	unsigned int _n_sweeping{0};
	void handle_slice_sweep(unsigned int zz, file_st* file) {
		assert(_ctx.get_executor().running_in_this_thread());
		gapr::print("handle slice [", _n_sweeping, "/sweep]: ", zz);
		auto ex=boost::asio::require(_ctx.get_executor(), boost::asio::execution::outstanding_work.tracked);
		++_n_sweeping;
		++_n_running;

		file->time_sweep_start=std::chrono::steady_clock::now();
		boost::asio::post(_pool, [this,ex,zz,file]() {
			if(bps<=8) {
				handle_slice_sweep_work<uint8_t>(zz, file);
			} else if(bps<=16) {
				handle_slice_sweep_work<uint16_t>(zz, file);
			} else {
				throw std::logic_error{"unsuported bps"};
			}
			boost::asio::post(ex, [this,zz,file]() {
				handle_slice_sweep_done(zz, file);
			});
		});
	}
	std::mutex _mtx_read_tif;
	std::mutex _mtx_write_tif;
	template<typename T>
	void handle_slice_sweep_work(unsigned int zz, file_st* file) {
		assert(_pool.get_executor().running_in_this_thread());
		assert(zz==file->zz);
		assert(file->busy_sweep);

		auto [_path, diri, pathtiled]=inputpairs[zz];
		std::filesystem::path path=_path;
		// XXX
		if(path=="BLACK" || path=="WHITE")
			return;

		auto dsfunc=[dsaux=dsaux.get(),zz](unsigned int x, unsigned int y, unsigned int tileW, unsigned int tileH, const void* ptr) {
			dsaux->update(zz, x, y, tileW, tileH, ptr);
			return true;
		};

		if(!tiled_dir) {
			assert(pathtiled.empty());
			if(!dsaux->finished(zz)) {
				Tiff tif{path.c_str(), "rm"};
				if(!::TIFFSetDirectory(tif, diri))
					throw std::runtime_error{"failed to set directory"};

				auto [tilew, tileh]=get_tilesize(tif);
				if(tilew==0)
					tilew=width;
				auto missing=dsaux->missing(zz, tilew, tileh);
				if(missing.empty()) {
					fprintf(stderr, "!!!!!!!!!!!!!!!!!!! false alarm\n");
					return;
				}
				missing.emplace_back()={width, height};

				hint_sequential_read(tif);
				tiff_read_as_tiled(tif, tilew, tileh, dsfunc, nullptr, &missing[0]);
				hint_discard_cache(tif);
			}
			return;
		}

		auto seq_read=[this](const std::filesystem::path& path) {
			std::vector<char> filebuf;
			gapr::file_stream f{path.c_str(), "r"};
			if(-1==std::fseek(f, 0, SEEK_END))
				throw std::runtime_error{"error seek"};
			auto siz=std::ftell(f);
			if(siz==-1)
				throw std::runtime_error{"error tell"};
			if(-1==std::fseek(f, 0, SEEK_SET))
				throw std::runtime_error{"error seek"};
			filebuf.resize(siz+1);
			hint_sequential_read(f);
			std::unique_lock seq_read{_mtx_read_tif};
			auto nread=std::fread(&filebuf[0], 1, filebuf.size(), f);
			seq_read.unlock();
			if(nread+1!=filebuf.size())
				throw std::runtime_error{"error read"};
			if(std::ferror(f))
				throw std::runtime_error{"error read2"};
			assert(std::feof(f));
			hint_discard_cache(f);
			return filebuf;
		};
		do {
			if(!pathtiled.empty())
				break;

#define PRE_READ
#ifdef PRE_READ
			Tiff tif{seq_read(path), "rm"};
#else
			Tiff tif{path.c_str(), "rm"};
#endif
			if(!TIFFSetDirectory(tif, diri))
				throw std::runtime_error{"failed to set directory"};
#ifndef PRE_READ
			hint_sequential_read(tif);
#endif

			gapr::print("convert to tiles: ", path);
			auto outpath=get_tiled_path(path, diri);
			auto res=tiff_to_tile_safe(tif, outpath, {0,0}, dsfunc, &_mtx_write_tif);
			std::error_code ec;
			if(res) {
				if(tiled_dir->empty())
					file->remove_later=path;
				pathtiled=std::move(outpath);
				file->tiled_path=pathtiled;
			} else {
				fprintf(stderr, "! failed to convert to tile: %s\n", _path.c_str());
				std::filesystem::remove(outpath, ec);
				outpath.replace_extension(".tif-temp");
				std::filesystem::remove(outpath, ec);
			}
#ifndef PRE_READ
			hint_discard_cache(tif);
#endif
			return;
		} while(false);

		assert(tiled_dir);
		assert(!pathtiled.empty());
		if(!dsaux->finished(zz)) {
#ifdef PRE_READ
			Tiff tif{seq_read(pathtiled), "rm"};
#else
			Tiff tif{pathtiled.c_str(), "rm"};
#endif
			assert(TIFFIsTiled(tif));

			const char* prev_desc;
			if(!::TIFFGetField(tif, TIFFTAG_IMAGEDESCRIPTION, &prev_desc)) {
				fprintf(stderr, "!!!!!!!!!!!!!!!!!!! missing tileinfo 1\n");
				return;
			}
			unsigned int tilew{0}, tileh{0};
			unsigned long sum{0};
			std::string desc{prev_desc};
			if(auto i=desc.find('\n'); i!=std::string::npos)
				desc.resize(i);
			if(gapr::parse_tuple(&desc[0], &tilew, &tileh)) {
			} else if(gapr::parse_tuple(&desc[0], &tilew, &tileh, &sum)) {
				fprintf(stderr, "sum %08lx\n", sum);
			} else {
				fprintf(stderr, "!!!!!!!!!!!!!!!!!!! missing tileinfo 2\n");
				return;
			}
			auto missing=dsaux->missing(zz, tilew, tileh);
			if(missing.empty()) {
				fprintf(stderr, "!!!!!!!!!!!!!!!!!!! false alarm\n");
				return;
			}
			missing.emplace_back()={width, height};

#ifndef PRE_READ
			hint_sequential_read(tif);
#endif
			tiff_read_as_tiled(tif, tilew, tileh, dsfunc, nullptr, &missing[0]);
#ifndef PRE_READ
			hint_discard_cache(tif);
#endif
		}
	}
	void handle_slice_sweep_done(unsigned int zz, file_st* file) {
		Logger::instance().logMessage(__FILE__, "Entered handle_slice_sweep_done: ");
		assert(_ctx.get_executor().running_in_this_thread());
		--_n_sweeping;
		--_n_running;
		file->time_sweep_end=std::chrono::steady_clock::now();
		auto time_sweep_real=std::chrono::duration_cast<std::chrono::milliseconds>(file->time_sweep_end-file->time_sweep_start);
		gapr::print("handle slice [", _n_sweeping, "/sweep]: ", zz, " ", time_sweep_real.count(), "ms");
		if(!file->tiled_path.empty()) {
			assert(tiled_dir);
			if(file->busy) {
				file->tiled_avail.store(true);
			} else {
				file->tif.reset();
				cacheaux->clear(zz);
				auto todel=std::move(file->remove_later);
				std::error_code ec;
				if(!todel.empty())
					std::filesystem::remove(todel, ec);
				std::get<2>(inputpairs[zz])=std::move(file->tiled_path);
			}
		}
		assert(file->busy_sweep);
		file->busy_sweep=false;
		_queued_slices_chk.erase(zz);
		upload_cubes();
	}

	std::vector<job_id> _pending_jobs_sugg;
	void add_needed(job_id id, bool loading);
	void add_suggested(job_id id);

	std::chrono::steady_clock::time_point _last_fetch{};
	std::chrono::steady_clock::time_point _last_uploadds{std::chrono::steady_clock::time_point::min()};
	std::chrono::steady_clock::time_point _last_saveds{};
	bool _timer_busy{false};
	uint64_t _last_sync_needed{0};


	void upload_cubes() {
		assert(_ctx.get_executor().running_in_this_thread());
		if(_stop_signal.signal()>1)
			return;

		while(true) {
			if(_n_running>=ncpu || _n_running_io>=ncpuio)
				break;
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

			std::string url = "pending/" + group + "/" + std::to_string(_last_sync_needed);
			Logger::instance().logMessage(__FILE__, "Requesting pending jobs from URL: " + url);

			async_get("pending/"+group+"/"+std::to_string(_last_sync_needed), [this](gapr::mem_file&& file, const auto& hdr) {
				// if(hdr.result()!=boost::beast::http::status::ok){
				// 	//throw std::runtime_error{std::move(res)};
				// 	// throw std::runtime_error{"asdf"};
				// 	Logger::instance().logMessage(__FILE__, "Error: Server responded with an unexpected value while processing cubes.");
				// 	throw std::runtime_error{"Unexpected server response while processing cubes.(Replacement for asdf)"};
				// }
				if (hdr.result() != boost::beast::http::status::ok) {
					Logger::instance().logMessage(__FILE__, "Server responded with unexpected status.\nHTTP Status: " + std::to_string(hdr.result_int()));

					Logger::instance().logMessage(__FILE__, "Logging headers:");
					for (auto const& field : hdr) {
						Logger::instance().logMessage(__FILE__, std::string(field.name_string()) + ": " + std::string(field.value()));
					}

					std::string body(file.map(0));
					Logger::instance().logMessage(__FILE__, "Response Body: \n" + body);

					throw std::runtime_error{"Unexpected server response while processing cubes."};
				}
				
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
						continue;
						//throw std::runtime_error{"line err"};
					if(!is_valid(id))
						continue;
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
		} else if(ts>_last_uploadds+std::chrono::hours{24}) {
			_last_uploadds=ts;
			upload_downsample();
		} else if(ts>_last_saveds+std::chrono::hours{36}) {
			_last_saveds=ts;
			boost::asio::post(_pool_io, [this]() {
				dsaux->writecache(workdir/"downsample", false);
			});
			upload_cubes();
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

		do {
			if(_n_running>=ncpu || _n_sweeping>=ncpusweep)
				break;
			auto [zz, file]=find_slice_to_complete();
			if(zz>=this->depth)
				break;
			if(!file)
				file=open_file(zz, true);

			handle_slice_sweep(zz, file);
		} while(true);

		while(true) {
			if(_n_running*2>=ncpu || _n_running_io>=ncpuio)
				return;
			auto [id, zz, file, job]=find_job_slice(_job_que1);
			if(!id)
				break;
			if(!file)
				file=open_file(zz);

			handle_slice(id, zz, file, job, "que1");
		}
		while(true) {
			if(_n_running*2>=ncpu || _n_running_io>=ncpuio)
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
	}
};

void resume_conversion(const std::filesystem::path& workdir, const std::optional<std::filesystem::path>& tiled_dir, unsigned int ncpu, std::size_t cachesize, ServerArgs&& srv, cube_enc_opts enc_opts) {
	ServerArgs srv2{srv};
	conversion_helper helper{tiled_dir, ncpu, std::move(srv)};
	helper.enc_opts=enc_opts;
	helper.load_state(workdir);
	if(!helper.plugin.empty())
		return resume_conversion_plugin(workdir, ncpu, cachesize, std::move(srv2));
	if(0) {
		mem_file_out str{gapr::mutable_mem_file{true}};
		helper.write_catalog(str);
		if(!str.flush())
			throw std::runtime_error{"failed to write catalog"};
		auto file=std::move(str).mem_file();
		gapr::print("length: ", file.size());
		mem_file_in str2{gapr::mutable_mem_file{std::move(file)}};
		std::string line;
		while(std::getline(str2, line))
			std::cout<<line<<'\n';
		if(!str2.eof())
			throw std::runtime_error{"asdf"};
	}
	helper.cacheaux=std::make_shared<cache_helper>(cachesize, cachesize/3*2);

	boost::asio::post(helper._ctx, [&helper]() {
		if(helper.token.empty())
			helper.upload_catalog();
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
	helper.dsaux->writecache(workdir/"downsample", false);

	helper._pool_io.join();
	helper._pool.join();
	helper.dsaux->writecache(workdir/"downsample", false);
	helper._stop_signal.stop();
}

bool job_id::parse(std::string_view path) {
	std::string tag{};
	unsigned int bits=0;
	for(std::size_t i=0; i<path.size(); ++i) {
		auto c=path[i];
		if(std::isalpha(c)) {
			tag.push_back(c);
		} else if(std::isdigit(c)) {
			unsigned int val;
			auto [eptr, ec]=std::from_chars(&path[i], path.data()+path.size(), val);
			if(ec!=std::errc{})
				return false;
			i=eptr-&path[0]-1;
			if(tag=="ch") {
				chan=val;
				bits+=0x1000000;
			} else if(tag=="x") {
				x=val;
				bits+=0x100000;
			} else if(tag=="y") {
				y=val;
				bits+=0x10000;
			} else if(tag=="z") {
				z=val;
				bits+=0x1000;
			}
			tag.clear();
		} else {
			if(tag=="mip") {
				bits+=0x100;
			} else if(tag=="avg") {
				bits+=0x10;
			}
			tag.clear();
		}
	}
	switch(bits) {
		case 0x1111000:
			_type=1;
			break;
		case 0x1000100:
			_type=2;
			break;
		case 0x1000010:
			_type=3;
			break;
		default:
			return false;
	}
	return format()==path;
}
std::string job_id::format() const {
	assert(_type!=0);
	std::ostringstream str;
	if(_type==2) {
		str<<"ch"<<std::setw(2)<<std::setfill('0')<<chan<<"-mip.webm";
		return str.str();
	}
	if(_type==3) {
		str<<"ch"<<std::setw(2)<<std::setfill('0')<<chan<<"-avg.webm";
		return str.str();
	}
	str<<"ch"<<std::setw(2)<<std::setfill('0')<<chan<<"webm/z";
	str<<std::setw(8)<<std::setfill('0')<<z<<"/y";
	str<<std::setw(8)<<std::setfill('0')<<y<<".x";
	str<<std::setw(8)<<std::setfill('0')<<x<<".webm";
	return str.str();
}


void conversion_helper::add_needed(job_id id, bool loading) {
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
			write_log("cube-needed %s\n", id.format().c_str());
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
void conversion_helper::add_suggested(job_id id) {
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
			if(sig[3] && false)
				_job_que2.push_front(id2);
			else
				_job_que1.push_front(id2);
		}
	}
}

