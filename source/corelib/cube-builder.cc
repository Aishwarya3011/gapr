#include "gapr/cube-builder.hh"

#include "gapr/utility.hh"
#include "gapr/cube-loader.hh"
#include "gapr/streambuf.hh"
#include "gapr/downloader.hh"
//#include "tracer.h"
//#include "session.h"

#include <fstream>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <condition_variable>
#include <memory>
#include <deque>

#include <boost/asio/post.hpp>

//#include "stream.h"
//#include "annot.tree.h"
//#include "misc.h"


/*!
 * serial (not parallel) loading
 */

struct LoadThreadShared;

struct CubeData {

	enum class State {
		Empty=0,
		Reading,
		Ready,
		Error
	};

	LoadThreadShared* lts;
	std::vector<State> states;

	std::string id_uri;
	std::array<uint32_t, 3> id_offset; // XXX

	gapr::mutable_cube data;
	gapr::cube data_c;

	//int32_t x1() const { return id.x0+xsize*id.nx; }
	//int32_t y1() const { return id.y0+ysize*id.ny; }
	//int32_t z1() const { return id.z0+zsize*id.nz; }

	std::size_t nFinished;
	std::size_t nError;

	bool finished() const { return nFinished==states.size(); }
	std::size_t nTotal() { return states.size(); }

	CubeData(LoadThreadShared* _lts, std::string&& uri, std::array<uint32_t, 3> offset, std::size_t ntot):
		/*id(_id), */lts{_lts}, /*refc{0},*/ states(ntot, State::Empty),
		id_uri{std::move(uri)}, id_offset{offset},
		//xsize{0}, ysize{0}, zsize{0},
		//widthAdj{0}, heightAdj{0}, depthAdj{0},
		data{},
		nFinished{0}, nError{0}
	{
	}
	~CubeData() {
	}
};


struct Source {
	std::string name;
	std::array<int64_t, 3> size;
	std::array<int64_t, 3> cubesize;
	gapr::affine_xform xform;
};


struct LoadThreadShared {

	std::mutex mtx_loc;

	std::vector<std::weak_ptr<CubeData>> cubes;
	std::mutex mtx_cubes;
	std::condition_variable cv_cubes;



	LoadThreadShared():
		mtx_loc{},
		cubes{}, mtx_cubes{}, cv_cubes{}
	{
	}
	~LoadThreadShared() {
		//if(!cubes.empty())
			//gapr::print("Cube data memory leak!");
	}

	std::shared_ptr<CubeData> getCube(std::string&& uri, std::array<uint32_t, 3> offset, std::size_t ntot) {
		std::unique_lock<std::mutex> lck{mtx_cubes};
		for(auto& cube_w: cubes) {
			auto cube=cube_w.lock();
			if(!cube)
				continue;
			if(cube->id_uri==uri && cube->id_offset==offset && cube->nTotal()==ntot) {
				return cube;
			}
		}
		auto cube=std::make_shared<CubeData>(this, std::move(uri), offset, ntot);
		cubes.emplace_back(cube);
		return cube;
	}
	CubeData::State changeState(CubeData* c, int i, CubeData::State prev, CubeData::State s) {
		std::unique_lock<std::mutex> lck{mtx_cubes};
		if(c->states[i]==prev) {
			c->states[i]=s;
			if(prev==CubeData::State::Reading && (s==CubeData::State::Ready || s==CubeData::State::Error))
				cv_cubes.notify_all();
			return prev;
		} else {
			return c->states[i];
		}
	}
	bool alloc(CubeData& c, gapr::cube_type type, unsigned int w, unsigned int h, unsigned int d) {
		std::unique_lock<std::mutex> lck{mtx_cubes};
		if(c.data)
			return true;
		try {
			gapr::print("alloc: ", w, ':', h, ':', d);
			c.data=gapr::mutable_cube{type, {w, h, d}};
		} catch(...) {
			return false;
		}
		return true;
	}
	void waitForCube(CubeData* c, int i) {
		std::unique_lock<std::mutex> lck{mtx_cubes};
		while(c->states[i]==CubeData::State::Reading)
			cv_cubes.wait(lck);
	}
};


template<typename T, size_t N> bool parseValues(const std::string& s, std::array<T, N>& v);
template<bool T> std::string compose(const std::string& pos, const std::string& path);

struct LoadPosition {
	std::size_t ch;
	bool isPat;
	std::string uri;
	std::array<uint32_t, 3> offset;
	std::array<uint32_t, 3> cube_sizes;
	unsigned int xn, yn, zn;
	bool operator==(const LoadPosition& p) const {
		if(uri!=p.uri) return false;
		if(offset!=p.offset) return false;
		if(xn!=p.xn) return false;
		if(yn!=p.yn) return false;
		if(zn!=p.zn) return false;
		return true;
	}
	unsigned int nFiles() const {
		return xn*yn*zn;
	}
};
namespace std {
	template<>
		struct hash<LoadPosition> {
			size_t operator()(const LoadPosition& pos) const {
				size_t h=0x12345678;
				h=h^std::hash<std::string>{}(pos.uri); h=(h>>(sizeof(h)*8/2))|(h<<(sizeof(h)*8/2));
				h=h^pos.offset[0]; h=(h>>(sizeof(h)*8/2))|(h<<(sizeof(h)*8/2));
				h=h^pos.offset[1]; h=(h>>(sizeof(h)*8/2))|(h<<(sizeof(h)*8/2));
				h=h^pos.offset[2]; h=(h>>(sizeof(h)*8/2))|(h<<(sizeof(h)*8/2));
				h=h^pos.xn; h=(h>>(sizeof(h)*8/2))|(h<<(sizeof(h)*8/2));
				h=h^pos.yn; h=(h>>(sizeof(h)*8/2))|(h<<(sizeof(h)*8/2));
				h=h^pos.zn; h=(h>>(sizeof(h)*8/2))|(h<<(sizeof(h)*8/2));
				return h;
			}
		};
}
struct CubePointer {
	int i;
	unsigned int xi, yi, zi;
};
struct LoadData {
	LoadPosition pos;
	std::shared_ptr<CubeData> cube; // *
	std::vector<std::string> files{};
	std::vector<CubePointer> ptrs{};
	gapr::downloader cache{}; // *
	int cache_st{0};

	std::pair<std::unique_ptr<std::streambuf>, int> waitForFile() {
		switch(cache_st) {
			case -2:
				return {nullptr, -1};
			case 0:
				{
					std::promise<int> prom;
					auto fut=prom.get_future();
					cache.async_wait([prom=std::move(prom)](std::error_code ec, int prg) mutable {
						try {
							if(ec)
								throw std::system_error{ec};
							prom.set_value(prg);
						} catch(...) {
							prom.set_exception(std::current_exception());
						}
					});
					try {
						auto prg=fut.get();
						if(prg==cache.FINISHED) {
							cache_st=-1;
							break;
						}
						cache_st=1;
					} catch(std::runtime_error& e) {
						gapr::print(1, "error wait: ", e.what());
						cache_st=-2;
						return {nullptr, -1};
					}
				}
				break;
		}
		auto res=cache.get();
		if(!res.first) {
			if(cache_st>-2)
				--cache_st;
			if(cache_st==-2)
				res.second=-1;
		}
		return res;
	}
};

static LoadThreadShared* loaderShared{nullptr};
static unsigned int loaderShared_refc{0};
static std::mutex loaderShared_mtx;

struct gapr::cube_builder_PRIV: gapr::cube_builder::DATA {
	std::thread thread;
	std::weak_ptr<gapr::cube_builder_PRIV> builder;
	boost::asio::any_io_executor builder_ex;
	boost::asio::thread_pool* pool;

	std::unordered_set<std::string> pathsVisited;
	std::vector<Source> sources;
	bool sources_ready;

	std::mutex mtx_in;
	std::condition_variable cv_in;
	bool stopRequested;
	std::unordered_map<int, LoadPosition> toload;

	std::mutex mtx_out;
	std::vector<gapr::cube_builder::Output> cubes_out;
	unsigned int get_offset{0};
	std::vector<gapr::cube_builder::Output> cubes_out2{};
	// offset... nx, ny, nz

	int nFilesToLoad;
	int nFilesLoaded;

	// XXX
	~cube_builder_PRIV() {
		sources.clear();
		unsigned int refc;
		{
			std::lock_guard lck{loaderShared_mtx};
			refc=--loaderShared_refc;
		}
		if(refc==0)
			delete loaderShared;
	}

	void setPositionSmall(std::size_t ch, const gapr::cube_info& info) {
		{
			std::unique_lock<std::mutex> lck_in{mtx_in};
			auto it=toload.find(0);
			if(it==toload.end()) {
				toload[0]={ch, false, info.location(), {0, 0, 0}, info.sizes, 1, 1, 1};
				nFilesToLoad+=toload[0].nFiles();
			} else {
				nFilesToLoad-=it->second.nFiles();
				it->second={ch, false, info.location(), {0, 0, 0}, info.sizes, 1, 1, 1};
				nFilesToLoad+=it->second.nFiles();
			}
			cv_in.notify_one();
		}
	}
	bool get_bbox(std::array<uint32_t, 6>& bbox) {
		std::unique_lock<std::mutex> lck_in{mtx_in};
		auto it=toload.find(1);
		if(it==toload.end())
			return false;
		auto& pos=it->second;
		auto cnts=std::array<unsigned int, 3>{pos.xn, pos.yn, pos.zn};
		for(unsigned int i=0; i<3; i++) {
			bbox[i]=pos.offset[i];
			bbox[3+i]=pos.offset[i]+pos.cube_sizes[i]*cnts[i];
		}
		return true;
	}
	bool setPositionBig(std::size_t ch, const gapr::cube_info& info, const std::array<double, 3>& pos, bool big, const std::array<unsigned int, 3>* offset, bool fixed) {
		auto off=info.xform.to_offset_f(pos);
		std::array<unsigned int, 3> cnt;
		for(unsigned int i=0; i<3; ++i)
			cnt[i]=big?3u:2u;
		if(offset) {
			unsigned int hits=0;
			for(unsigned int i=0; i<3; ++i) {
				auto rr=(off[i]-(*offset)[i])/(cnt[i]*info.cube_sizes[i]);
				if(std::abs(2*rr-1)<0.618)
					++hits;
			}
			if(hits>=3)
				return false;
		}
		std::array<unsigned int, 3> offi;
		for(unsigned int i=0; i<3; ++i) {
			int oo=std::lround((2*off[i]/info.cube_sizes[i]-cnt[i])/2)*info.cube_sizes[i];
			if(fixed) {
				if(oo<=-static_cast<int>(cnt[i]*info.cube_sizes[i]))
					return false;
				if(oo<0) {
					cnt[i]-=static_cast<unsigned int>(-oo)/info.cube_sizes[i];
					oo=0;
				} else if(static_cast<unsigned int>(oo)>=info.sizes[i])
					return false;
				auto maxcnt=(info.sizes[i]-oo+info.cube_sizes[i]-1)/info.cube_sizes[i];
				if(cnt[i]>maxcnt)
					cnt[i]=maxcnt;
			} else {
				if(oo<0)
					oo=0;
				else if(static_cast<unsigned int>(oo)>=info.sizes[i])
					oo=info.sizes[i]/info.cube_sizes[i]*info.cube_sizes[i];
				auto maxcnt=(info.sizes[i]-oo+info.cube_sizes[i]-1)/info.cube_sizes[i];
				if(cnt[i]>maxcnt) {
					auto shift=std::min(cnt[i]-maxcnt, oo/info.cube_sizes[i]);
					oo-=shift*info.cube_sizes[i];
					cnt[i]-=(cnt[i]-maxcnt-shift);
				}
			}
			offi[i]=oo;
		}
		if(offset && offi==*offset)
			return false;

		gapr::print("load: ", offi[0], '+', cnt[0], '/', offi[1], '+', cnt[1], '/', offi[2], '+', cnt[2]);
		std::unique_lock<std::mutex> lck_in{mtx_in};
		auto it=toload.find(1);
		if(it==toload.end()) {
			toload[1]={ch, true, info.location(), offi, info.cube_sizes, cnt[0], cnt[1], cnt[2]};
			nFilesToLoad+=toload[1].nFiles();
		} else {
			nFilesToLoad-=it->second.nFiles();
			it->second={ch, true, info.location(), offi, info.cube_sizes, cnt[0], cnt[1], cnt[2]};
			nFilesToLoad+=it->second.nFiles();
		}
		cv_in.notify_one();
		return true;
	}

	void setPositionBigOffset(std::size_t ch, const gapr::cube_info& info, const std::array<unsigned int, 3>& offset, bool big) {
		unsigned int xn=big?3:2;
		unsigned int yn=big?3:2;
		unsigned int zn=big?3:2;
		unsigned int x0=offset[0];
		unsigned int y0=offset[1];
		unsigned int z0=offset[2];
		while(xn>0 && x0+info.cube_sizes[0]*xn-info.cube_sizes[0]>=info.sizes[0])
			--xn;
		while(yn>0 && y0+info.cube_sizes[1]*yn-info.cube_sizes[1]>=info.sizes[1])
			--yn;
		while(zn>0 && z0+info.cube_sizes[2]*zn-info.cube_sizes[2]>=info.sizes[2])
			--zn;

		gapr::print("load: ", x0, '/', y0, '/', z0);
		std::unique_lock<std::mutex> lck_in{mtx_in};
		auto it=toload.find(1);
		if(it==toload.end()) {
			toload[1]={ch, true, info.location(), {x0, y0, z0}, info.cube_sizes, xn, yn, zn};
			nFilesToLoad+=toload[1].nFiles();
		} else {
			nFilesToLoad-=it->second.nFiles();
			it->second={ch, true, info.location(), {x0, y0, z0}, info.cube_sizes, xn, yn, zn};
			nFilesToLoad+=it->second.nFiles();
		}
		cv_in.notify_one();
	}

	std::unique_ptr<LoadData> setupJobRemoteSmall(const LoadPosition& pos) {
		gapr::print("setupJobRemoteSmall");
		if(pos.xn!=1 || pos.yn!=1 || pos.zn!=1)
			gapr::report("Asertion failed!");
		std::unique_ptr<LoadData> data{new LoadData{}};
		data->pos=pos;
		data->cube=loaderShared->getCube(std::string{pos.uri}, pos.offset, pos.nFiles());
		std::unique_lock<std::mutex> lck{loaderShared->mtx_cubes};
		auto s=data->cube->states[0]; //none //reading //read //failed
		if(s==CubeData::State::Empty) {
			data->files.push_back(pos.uri);
			data->cache=gapr::downloader{builder_ex, std::string{pos.uri}};
			data->ptrs.push_back({0});
		}
		return data;
	}
	std::unique_ptr<LoadData> setupJobRemoteBig(const LoadPosition& pos) {
		gapr::print("setupJobRemoteBig");
		std::unique_ptr<LoadData> data{new LoadData{}};
		data->pos=pos;
		data->cube=loaderShared->getCube(std::string{pos.uri}, pos.offset, pos.nFiles());
		std::unique_lock<std::mutex> lck{loaderShared->mtx_cubes};
		int i=0;
		for(unsigned int zi=0; zi<pos.zn; zi++) {
			for(unsigned int yi=0; yi<pos.yn; yi++) {
				for(unsigned int xi=0; xi<pos.xn; xi++) {
					auto s=data->cube->states[i]; //none //reading //read //failed
					if(s==CubeData::State::Empty) {
						auto uri=pos.uri;
						auto loc=gapr::pattern_subst(pos.uri, {pos.offset[0]+xi*pos.cube_sizes[0], pos.offset[1]+yi*pos.cube_sizes[1], pos.offset[2]+zi*pos.cube_sizes[2]});
						gapr::print("loc: ", loc);
						data->files.push_back(loc);
						data->ptrs.push_back({i, xi, yi, zi});
					}
					i++;
				}
			}
		}
		data->cache=gapr::downloader{builder_ex, data->files};
		return data;
	}
	void loadImageSmall(gapr::cube_loader* imageReader, const std::string& file, CubeData* cube) {
		if(!imageReader)
			gapr::report("Cannot read file: ", file);
		gapr::cube_type type=imageReader->type();
		switch(type) {
			case gapr::cube_type::u8:
			case gapr::cube_type::u16:
			case gapr::cube_type::u32:
			case gapr::cube_type::i8:
			case gapr::cube_type::i16:
			case gapr::cube_type::i32:
			case gapr::cube_type::f32:
				break;
			default:
				gapr::report("Voxel type not supported");
		}
		auto [w, h, d]=imageReader->sizes();

		if(!loaderShared->alloc(*cube, type, w, h, d))
			gapr::report("Failed to alloc cube memory");
		auto cube_view=cube->data.view<char>();
		imageReader->load(cube_view.row(0, 0), cube_view.ystride(), cube_view.zstride());
	}
	void loadImageBig(gapr::cube_loader* imageReader, const std::string& file, CubeData* cube, int xi, int yi, int zi, int64_t cw, int64_t ch, int64_t cd, unsigned int xn, unsigned int yn, unsigned int zn) {
		if(!imageReader)
			gapr::report("Cannot read file: ", file);
		gapr::cube_type type=imageReader->type();
		switch(type) {
			case gapr::cube_type::u8:
			case gapr::cube_type::u16:
			case gapr::cube_type::u32:
			case gapr::cube_type::i8:
			case gapr::cube_type::i16:
			case gapr::cube_type::i32:
			case gapr::cube_type::f32:
				break;
			default:
				gapr::report("Voxel type not supported");
		}

		if(!loaderShared->alloc(*cube, type, cw*xn, ch*yn, cd*zn))
			gapr::report("Failed to alloc cube memory");
		auto bpv=voxel_size(type);
		auto cube_view=cube->data.view<char>();
		imageReader->load(cube_view.row(yi*ch, zi*cd)+xi*cw*bpv, cube_view.ystride(), cube_view.zstride());
	}
	bool loadCubeRemoteSmall(LoadData* curJob) {
		if(!curJob->cache)
			return false;
		auto [cacheFile, r]=curJob->waitForFile();
		if(!cacheFile) {
			if(r<0) {
				auto s=loaderShared->changeState(curJob->cube.get(), 0, CubeData::State::Empty, CubeData::State::Reading);
				if(s==CubeData::State::Empty) {
					loaderShared->changeState(curJob->cube.get(), 0, CubeData::State::Reading, CubeData::State::Error);
					curJob->cube->nFinished++;
					curJob->cube->nError++;
				} else if(s==CubeData::State::Reading) {
					loaderShared->waitForCube(curJob->cube.get(), 0);
				}
				return true;
			}
			return false;
		}
		auto s=loaderShared->changeState(curJob->cube.get(), 0, CubeData::State::Empty, CubeData::State::Reading);
		if(s==CubeData::State::Empty) {
			try {
				//XXX
				//if(!cacheFile.isReady())
					//gapr::report("Failed to download file");
				auto uri=curJob->files[r];
				auto sb=std::move(cacheFile);
				auto imageReader=gapr::make_cube_loader(uri, static_cast<gapr::Streambuf&>(*sb));
				loadImageSmall(imageReader.get(), curJob->files[0], curJob->cube.get());
				loaderShared->changeState(curJob->cube.get(), 0, CubeData::State::Reading, CubeData::State::Ready);
				curJob->cube->nFinished++;
			} catch(std::exception& e) {
				gapr::print("Failed to load image: ", curJob->files[r], e.what());
				loaderShared->changeState(curJob->cube.get(), 0, CubeData::State::Reading, CubeData::State::Error);
				curJob->cube->nFinished++;
				curJob->cube->nError++;
			}
			return true;
		} else if(s==CubeData::State::Reading) {
			loaderShared->waitForCube(curJob->cube.get(), 0);
			return true;
		} else {
			return true;
		}
	}
	bool loadCubeRemoteBig(LoadData* curJob) {
		if(!curJob->cache)
			return false;
		auto [cacheFile, r]=curJob->waitForFile();
		if(!cacheFile) {
			if(r<0) {
				for(std::size_t cpi=0; cpi<curJob->cube->states.size(); cpi++) {
					auto s=loaderShared->changeState(curJob->cube.get(), cpi, CubeData::State::Empty, CubeData::State::Reading);
					if(s==CubeData::State::Empty) {
						loaderShared->changeState(curJob->cube.get(), cpi, CubeData::State::Reading, CubeData::State::Error);
						curJob->cube->nFinished++;
						curJob->cube->nError++;
					} else if(s==CubeData::State::Reading) {
						loaderShared->waitForCube(curJob->cube.get(), cpi);
					}
				}
				return true;
			}
			return false;
		}
		auto cpi=curJob->ptrs[r].i;
		auto s=loaderShared->changeState(curJob->cube.get(), cpi, CubeData::State::Empty, CubeData::State::Reading);
		if(s==CubeData::State::Empty) {
			try {
				//XXX
				//if(!cacheFile.isReady())
					//gapr::report("Failed to download file");
				auto uri=curJob->files[r];
				auto sb=std::move(cacheFile);//std::move(f));
				auto imageReader=gapr::make_cube_loader(uri, static_cast<gapr::Streambuf&>(*sb));
				loadImageBig(imageReader.get(), curJob->files[0], curJob->cube.get(), curJob->ptrs[r].xi, curJob->ptrs[r].yi, curJob->ptrs[r].zi, curJob->pos.cube_sizes[0], curJob->pos.cube_sizes[1], curJob->pos.cube_sizes[2], curJob->pos.xn, curJob->pos.yn, curJob->pos.zn);
				loaderShared->changeState(curJob->cube.get(), cpi, CubeData::State::Reading, CubeData::State::Ready);
				curJob->cube->nFinished++;
			} catch(std::exception& e) {
				gapr::print("Failed to load image: ", curJob->files[r]);
				loaderShared->changeState(curJob->cube.get(), cpi, CubeData::State::Reading, CubeData::State::Error);
				curJob->cube->nFinished++;
				curJob->cube->nError++;
			}
			return true;
		} else if(s==CubeData::State::Reading) {
			loaderShared->waitForCube(curJob->cube.get(), cpi);
			return true;
		} else {
			return true;
		}
	}

	void loadCubes () {
		std::deque<std::unique_ptr<LoadData>> toloadCur{};

		std::unique_lock<std::mutex> lck{mtx_in};
		while(!stopRequested) {
			std::unordered_set<LoadPosition> toloadNew{};
			for(auto& p: toload) {
				//if(p.second.idx>0)
					toloadNew.insert(p.second);
			}
			lck.unlock();
			{
				std::deque<std::unique_ptr<LoadData>> toloadTmp{};
				std::vector<std::unique_ptr<LoadData>> toloadDel{};
				std::swap(toloadTmp, toloadCur);
				int nLocal=0;
				int nRemote=0;
				for(auto& pos: toloadTmp) {
					auto i=toloadNew.find(pos->pos);
					if(i!=toloadNew.end()) {
						if(true) {
							nRemote++;
						} else {
							nLocal++;
						}
						toloadCur.push_back(std::move(pos));
						toloadNew.erase(i);
					} else {
						toloadDel.push_back(std::move(pos));
					}
				}
				for(auto& pos: toloadNew) {
					if(true) {
						if(nRemote<2) {
							std::unique_ptr<LoadData> data{};
							if(pos.isPat) {
								data=setupJobRemoteBig(pos);
							} else {
								data=setupJobRemoteSmall(pos);
							}
							if(data) {
								toloadCur.push_back(std::move(data));
								nRemote++;
							}
						}
					} else {
						if(nLocal<1) {
							std::unique_ptr<LoadData> data{};
							if(pos.isPat) {
								assert(0);
								//data=setupJobLocalBig(pos);
							} else {
								assert(0);
								//data=setupJobLocalSmall(pos);
							}
							if(data) {
								toloadCur.push_front(std::move(data));
								nLocal++;
							}
						}
					}
				}
			}

			if(!toloadCur.empty()) {
				auto curJob=toloadCur.front().get();
				//auto& source=sources[curJob->pos.idx];

				bool progr=false;
				if(true) {
					if(curJob->pos.isPat) {
						progr=loadCubeRemoteBig(curJob);
					} else {
						progr=loadCubeRemoteSmall(curJob);
					}
				} else {
					if(curJob->pos.isPat) {
						assert(0);
						//progr=loadCubeLocalBig(curJob);
					} else {
						assert(0);
						//progr=loadCubeLocalSmall(curJob);
					}
				}

				lck.lock();
				if(curJob->cube->finished()) {
					curJob->cube->data_c=std::move(curJob->cube->data);
					std::vector<int> chs{};
					auto filesLoadedTmp=0;
					std::vector<decltype(toload.begin())> toremove{};
					for(auto it=toload.begin(); it!=toload.end(); ++it) {
						auto ch=it->first;
						if(it->second==curJob->pos) {
							chs.push_back(ch);
							nFilesLoaded+=it->second.nFiles();
							toremove.push_back(it);
						}
						for(size_t i=1; i<toloadCur.size(); i++) {
							if(toloadCur[i].get()->pos==it->second) {
								filesLoadedTmp+=toloadCur[i]->cube->nFinished;
							}
						}
					}
					for(auto it: toremove)
						toload.erase(it);

					bool err=curJob->cube->nError;
					if(!chs.empty()) {
						auto id_ch=curJob->pos.ch;
						auto id_offset=curJob->pos.offset;
						auto id_uri=curJob->cube->id_uri;
						{
							std::unique_lock<std::mutex> lck_out{mtx_out};
							cubes_out.push_back({{}, (unsigned int)id_ch, id_offset, std::move(id_uri)});
							if(!err) {
								cubes_out.back().data=curJob->cube->data_c;
							}
							get_head=1;
						}
						lck.unlock();
						notify_finish(err?std::make_error_code(std::io_errc::stream):std::error_code{}, nFilesLoaded+filesLoadedTmp, nFilesToLoad);
						lck.lock();
						{
							if(toload.size()<=0) {
								nFilesToLoad-=nFilesLoaded;
								nFilesLoaded=0;
							}
						}
					}
					toloadCur.pop_front();
					if(chs.empty())
						notify_progress(nFilesLoaded+filesLoadedTmp, nFilesToLoad);
				} else if(progr) {
					auto filesLoadedTmp=0;
					for(auto it=toload.begin(); it!=toload.end(); ++it) {
						for(size_t i=0; i<toloadCur.size(); i++) {
							if(toloadCur[i].get()->pos==it->second) {
								filesLoadedTmp+=toloadCur[i].get()->cube->nFinished;
							}
						}
					}
					notify_progress(nFilesLoaded+filesLoadedTmp, nFilesToLoad);
				}
				continue;
			}
			lck.lock();
			while(!stopRequested && toload.empty())
				cv_in.wait(lck);
		}
	}
	cube_builder_PRIV():
		thread{},
		builder{},
		pathsVisited{}, sources{}, sources_ready{false},
		mtx_in{}, cv_in{}, stopRequested{false}, toload{},
		nFilesToLoad{0}, nFilesLoaded{0}
	{
		unsigned int refc;
		{
			std::lock_guard lck{loaderShared_mtx};
			refc=++loaderShared_refc;
			if(refc==1)
				loaderShared=new LoadThreadShared{};
		}
		thread=std::thread{[this]() {
			gapr::print("Load thread started");
			try {
				loadCubes();
			} catch(const std::exception& e) {
				//Q_EMIT threadError(QString{"Unexpected error in load thread: "}+=e.what());
				gapr::print("Load thread error, ", e.what());
			}
			gapr::print("Load thread finished");
		}};
	}
	void stop() {
		std::unique_lock<std::mutex> lck{mtx_in};
		stopRequested=true;
		cv_in.notify_one();
	}
	void cancel() {
		std::unique_lock<std::mutex> lck{mtx_in};
		for(auto& p: toload) {
			//if(p.second.idx>0) {
				nFilesToLoad-=p.second.nFiles();
			//}
		}
		toload.clear();
	}
	cube_builder::Output do_get() {
		{
			std::lock_guard lck{mtx_out};
			if(get_offset>=cubes_out2.size()) {
				if(cubes_out.empty()) {
					get_head=0;
					return {};
				}
				cubes_out.swap(cubes_out2);
				cubes_out.clear();
				get_offset=0;
			}
			return std::move(cubes_out2[get_offset++]);
		}
	}
	std::unique_ptr<cube_builder::WaitOp> _op;
	void do_wait(std::unique_ptr<cube_builder::WaitOp> op) {
		std::error_code ec;
		{
			std::lock_guard lck{mtx_out};
			if(pending) {
				pending=false;
				notified=true;
				ec={};
			} else {
				notified=false;
				_op=std::move(op);
			}
		}
		if(op)
			boost::asio::post(builder_ex, [op=std::move(op),ec,p=_progress]() {
				op->complete(ec, p);
			});
	}
	int _progress{-1};
	bool pending{false};
	void notify_finish(std::error_code ec, int a, int b) {
		size_t n=0;
		{
			std::unique_lock<std::mutex> lck_out{mtx_out};
			n+=cubes_out.size();
			n+=cubes_out2.size()-get_offset;
			gapr::print("N left: ", n);
			n=0;
		}
		{
			std::unique_lock<std::mutex> lck_in{mtx_in};
			n+=toload.size();
			//for(auto& p: priv->toload) {
			//if(p.second.idx>0)
			//n++;
			//}
		}
		gapr::print(1, "N left: ", n);
		if(n==0)
			_progress=1001;
		else
			_progress=a*1000/b;
		std::unique_ptr<cube_builder::WaitOp> op;
		{
			std::lock_guard lck{mtx_out};
			op=std::move(_op);
			if(!op)
				pending=true;
			notified=true;
		}
		if(op)
			boost::asio::post(builder_ex, [op=std::move(op),ec,p=_progress]() {
				op->complete(ec, p);
			});
	}
	void notify_progress(int a, int b) {
		_progress=a*1000/b;
		std::unique_ptr<cube_builder::WaitOp> op;
		{
			std::lock_guard lck{mtx_out};
			op=std::move(_op);
			if(!op)
				pending=true;
			notified=true;
		}
		if(op)
			boost::asio::post(builder_ex, [op=std::move(op),p=_progress]() {
				op->complete(std::error_code{}, p);
			});
	}
};


#if 0
QVector<QString> LoadThread::sources() const {
	if(!priv->sources_ready)
		return {};
	QVector<QString> srcs;
	for(auto& s: priv->sources)
		srcs.push_back(s.name);
	return srcs;
}
bool LoadThread::getBoundingBox(std::array<double, 6>* pbbox) const {
	if(!pbbox)
		return false;
	auto& bbox=*pbbox;

	bool expanded{false};
	for(auto& s: priv->sources) {
		for(int dir=0; dir<3; dir++) {
			double rr=s.xform.origin[dir];
			double ll=rr;
			for(int k=0; k<3; k++) {
				if(s.xform.direction[dir+k*3]>=0) {
					rr+=s.xform.direction[dir+k*3]*s.size[k];
				} else {
					ll+=s.xform.direction[dir+k*3]*s.size[k];
				}
			}
			if(ll<bbox[dir*2+0]) {
				bbox[dir*2+0]=ll;
				expanded=true;
			}
			if(rr>bbox[dir*2+1]) {
				bbox[dir*2+1]=rr;
				expanded=true;
			}
		}
	}
	return expanded;
}
bool LoadThread::getMinVoxelSize(double* vsize) const {
	if(!vsize)
		return false;

	bool expanded{false};
	for(auto& s: priv->sources) {
		for(int dir=0; dir<3; dir++) {
			auto r=s.xform.resolution[dir];
			if(r<*vsize) {
				*vsize=r;
				expanded=true;
			}
		}
	}
	return expanded;
}
#endif


// Helpers
template<typename T, size_t N> bool parseValues(const std::string& s, std::array<T, N>& v) {
	std::istringstream iss{s};
	for(size_t i=0; i<N; i++) {
		iss>>v[i];
		if(!iss)
			return false;
	}
	return true;
}
#if 0
template<> std::string compose<true>(const std::string& pos, const std::string& path) {
	if(QDir::isAbsolutePath(path)) {
		auto slash1=pos.indexOf('/');
		if(slash1<0)
			return {};
		auto slash2=pos.indexOf('/', slash1+1);
		if(slash2<0)
			return {};
		auto slash3=pos.indexOf('/', slash2+1);
		if(slash3<0)
			return {};
		return pos.left(slash3)+path;
	}
	return pos.left(pos.length()-7)+path;
}
template<> QString compose<false>(const QString& pos, const QString& path) {
	if(QDir::isAbsolutePath(path))
		return path;
	return pos.left(pos.length()-7)+path;
}
#endif

gapr::cube_builder::cube_builder(const executor_type& ex, boost::asio::thread_pool& pool): _priv{std::make_shared<gapr::cube_builder_PRIV>()} {
	_priv->builder_ex=ex;
	_priv->pool=&pool;
}
gapr::cube_builder::~cube_builder() {
	_priv->stop();
	_priv->thread.join();
}
bool gapr::cube_builder::build(unsigned int chan, const gapr::cube_info& info,
		const std::array<double, 3>& pos, bool bigger,
		const std::array<unsigned int, 3>* offset, bool fixed) {
	return _priv->setPositionBig(chan, info, pos, bigger, offset, fixed);
}
void gapr::cube_builder::build(unsigned int chan, const gapr::cube_info& info,
		const std::array<unsigned, 3>& offset, bool bigger) {
	return _priv->setPositionBigOffset(chan, info, offset, bigger);
}
void gapr::cube_builder::build(unsigned int chan, const gapr::cube_info& info) {
	return _priv->setPositionSmall(chan, info);
}

void gapr::cube_builder::do_wait(cube_builder_PRIV* p, std::unique_ptr<WaitOp> op) {
	return p->do_wait(std::move(op));
}
gapr::cube_builder::Output gapr::cube_builder::do_get() const {
	return _priv->do_get();
}
void gapr::cube_builder::update_login(std::string&& username, std::string&& password) {
}
void gapr::cube_builder::cancel() {
	return _priv->cancel();
}
bool gapr::cube_builder::get_bbox(std::array<uint32_t, 6>& bbox) {
	return _priv->get_bbox(bbox);
}
