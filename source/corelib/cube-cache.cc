#include "gapr/cube-loader.hh"



#if 0

	
//////////////////
#if 0
bool isUrl(const QString& s) {
	QRegExp regex{"^[a-zA-Z]+://.*/.*$", Qt::CaseInsensitive, QRegExp::RegExp};
	return regex.exactMatch(s);
}





struct Location {
	int id;
	bool isPat;
	bool isUrl;
	QString location;
};

struct Source {
	QString name;
	Location* location; //*
	int annotTree; //*
	std::array<int64_t, 3> size;
	std::array<int64_t, 3> cubesize;
	CubeXform xform;
};


struct LoadThreadShared {

	LoadThreadShared():
		locations{}, mtx_loc{},
		cubes{}, mtx_cubes{}, cv_cubes{},
		annotTrees{}, mtx_annots{}, cv_annots{}
	{
	}
	~LoadThreadShared() {
		if(!cubes.empty())
			gapr::print("Cube data memory leak!");
		for(auto& p: locations)
			delete p;
		for(auto& p: annotTrees)
			delete p;
	}

	int getLocation(bool isPat, bool isUrl, const QString& loc) {
		if(loc.isEmpty())
			return -1;
		std::unique_lock<std::mutex> lck{mtx_loc};
		auto i=location_map.find({isPat, loc});
		if(i!=location_map.end())
			return i->second;
		int id=locations.size();
		locations.push_back(new Location{id, isPat, isUrl, loc});
		location_map[{isPat, loc}]=id;
		return id;
	}

	int getAnnot(const QString& loc) {
		if(loc.isEmpty())
			return -1;
		std::unique_lock<std::mutex> lck{mtx_annots};
		auto i=annotTreeMap.find(loc);
		if(i!=annotTreeMap.end())
			return i->second;
		int id=annotTrees.size();
		annotTrees.push_back(new AnnotTree{id, loc});
		annotTreeMap[loc]=id;
		return id;
	}

	CubeData getCube(const gapr::CubeId& id) {
		std::unique_lock<std::mutex> lck{mtx_cubes};
		for(auto cube: cubes) {
			if(cube->data.id==id) {
				return cube->data;
			}
		}
		auto cube=new CubeDataState{this, id};
		cubes.insert(cube);
		return cube->data;
	}
	/*
		if(c->refc<=0) {
			cubes.erase(c);
			delete c;
		}
		*/
	CubeDataState::State changeState(CubeDataState* c, int i, CubeDataState::State prev, CubeDataState::State s) {
		std::unique_lock<std::mutex> lck{mtx_cubes};
		if(c->states[i]==prev) {
			c->states[i]=s;
			if(prev==CubeDataState::State::Reading && (s==CubeDataState::State::Ready || s==CubeDataState::State::Error))
				cv_cubes.notify_all();
			return prev;
		} else {
			return c->states[i];
		}
	}
	bool alloc(CubeData* c, gapr::CubeType type, int64_t w, int64_t h, int64_t d) {
		std::unique_lock<std::mutex> lck{mtx_cubes};
		if(c->data)
			return true;
		try {
			c->sizes[0]=w*c->id.counts[0];
			c->sizes[1]=h*c->id.counts[1];
			c->sizes[2]=d*c->id.counts[2];
			c->type=type;
			c->data.reset(new char[c->sizeAdj<0>()*c->sizeAdj<1>()*c->sizeAdj<2>()*type]);
		} catch(...) {
			return false;
		}
		return true;
	}
	void waitForCube(CubeDataState* c, int i) {
		std::unique_lock<std::mutex> lck{mtx_cubes};
		while(c->states[i]==CubeDataState::State::Reading)
			cv_cubes.wait(lck);
	}
};

LoadThreadShared* LoadThread::createShared() {
	return new LoadThreadShared{};
}
void LoadThread::destroyShared(LoadThreadShared* lts) {
	delete lts;
}


template<typename T, size_t N> bool parseValues(const std::string& s, std::array<T, N>& v);
template<bool T> QString compose(const QString& pos, const QString& path);

struct LoadPosition {
	int idx;
	int32_t x0, y0, z0;
	int xn, yn, zn;
	bool operator==(const LoadPosition& p) const {
		if(idx!=p.idx) return false;
		if(x0!=p.x0) return false;
		if(y0!=p.y0) return false;
		if(z0!=p.z0) return false;
		if(xn!=p.xn) return false;
		if(yn!=p.yn) return false;
		if(zn!=p.zn) return false;
		return true;
	}
	int nFiles() const {
		return xn*yn*zn;
	}
};
namespace std {
	template<>
		struct hash<LoadPosition> {
			size_t operator()(const LoadPosition& pos) const {
				size_t h=0x12345678;
				h=h^pos.idx; h=(h>>(sizeof(h)*8/2))|(h<<(sizeof(h)*8/2));
				h=h^pos.x0; h=(h>>(sizeof(h)*8/2))|(h<<(sizeof(h)*8/2));
				h=h^pos.y0; h=(h>>(sizeof(h)*8/2))|(h<<(sizeof(h)*8/2));
				h=h^pos.z0; h=(h>>(sizeof(h)*8/2))|(h<<(sizeof(h)*8/2));
				h=h^pos.xn; h=(h>>(sizeof(h)*8/2))|(h<<(sizeof(h)*8/2));
				h=h^pos.yn; h=(h>>(sizeof(h)*8/2))|(h<<(sizeof(h)*8/2));
				h=h^pos.zn; h=(h>>(sizeof(h)*8/2))|(h<<(sizeof(h)*8/2));
				return h;
			}
		};
}
struct CubePointer {
	int i;
	int xi, yi, zi;
};
struct LoadData {
	LoadPosition pos;
	CubeDataState cube; // *
	std::vector<QString> files{};
	std::vector<CubePointer> ptrs{};
	std::vector<CacheFileRef> cache{}; // *
};

struct LoadThreadPriv {
	// XXX
	~LoadThreadPriv() {
		rootlocs.clear();
		sources.clear();
	}

	template<bool T> AnnotTree* parseAnnot(const std::map<std::string, std::string>& data, const QString& pos) {
		auto li=data.find("annotation");
		if(li!=data.end()) {
			QString loc=QString::fromStdString(li->second);
			if(isUrl(loc)) {
				return loaderShared->getAnnot(loc);
			} else {
				auto pos_new=compose<T>(pos, loc);
				return loaderShared->getAnnot(pos_new);
			}
		}
		return nullptr;
	}
	template<bool T> void addSource(const std::map<std::string, std::string>& data, const QString& pfx, const QString& pos, const std::array<double, 3>& p0, const std::array<double, 9>& dir) {
		//for(int i=0; i<9; i++)
		//std::cerr<<dir[i]<<"\n";
		CubeXform xform;
		auto p0i=data.find("origin");
		if(p0i==data.end()) {
			xform.origin=p0;
		} else {
			std::array<double, 3> p0_temp;
			auto r=parseValues<double, 3>(p0i->second, p0_temp);
			if(!r)
				gapr::report("Failed to parse origin.");
			for(int i=0; i<3; i++)
				xform.origin[i]=p0[i]+dir[0+i]*p0_temp[0]+dir[3+i]*p0_temp[1]+dir[6+i]*p0_temp[2];
		}
		auto diri=data.find("direction");
		if(diri==data.end()) {
			xform.direction=dir;
		} else {
			std::array<double, 9> dir_temp;
			auto r=parseValues<double, 9>(diri->second, dir_temp);
			if(!r)
				gapr::report("Failed to parse direction.");
			for(int i=0; i<3; i++)
				for(int j=0; j<3; j++)
					xform.direction[i+j*3]=dir[i+0*3]*dir_temp[0+j*3]+dir[i+1*3]*dir_temp[1+j*3]+dir[i+2*3]*dir_temp[2+j*3];
		}

		auto ri=data.find("link");
		if(ri!=data.end()) {
			QString link=QString::fromStdString(ri->second);
			if(link.compare(QLatin1String{"catalog"}, Qt::CaseInsensitive)==0)
				gapr::report("A link to the same catalog not allowed.");
			if(!link.endsWith(QLatin1String{"/catalog"}, Qt::CaseInsensitive))
				gapr::report("A link must refer to a catalog file.");
			if(isUrl(link)) {
				loadCatalog<true>(pfx+'/', link, xform.origin, xform.direction);
			} else {
				auto pos_new=compose<T>(pos, link);
				loadCatalog<T>(pfx+'/', pos_new, xform.origin, xform.direction);
			}
			return;
		}
		auto li=data.find("location");
		if(li!=data.end()) {
			QString loc=QString::fromStdString(li->second);
			if(!loc.endsWith(QLatin1String{".nrrd"}, Qt::CaseInsensitive)
#ifdef WITH_VP9
					&& !loc.endsWith(QLatin1String{".webm"}, Qt::CaseInsensitive)
#endif
					&& !loc.endsWith(QLatin1String{".tiff"}, Qt::CaseInsensitive)
					&& !loc.endsWith(QLatin1String{".tif"}, Qt::CaseInsensitive))
				gapr::report("Cannot handle file type: ", loc);

			auto annotTree=parseAnnot<T>(data, pos);

			Location* location{nullptr};
			if(isUrl(loc)) {
				location=loaderShared->getLocation(false, true, loc);
			} else {
				auto pos_new=compose<T>(pos, loc);
				location=loaderShared->getLocation(false, T, pos_new);
			}

			if(location)
				sources.push_back(Source{pfx, location, annotTree, {0, 0, 0}, {0, 0, 0}, xform});
			return;
		}
		auto pi=data.find("pattern");
		if(pi!=data.end()) {
			QString pat=QString::fromStdString(pi->second);
			if(!pat.endsWith(QLatin1String{".nrrd"}, Qt::CaseInsensitive)
#ifdef WITH_VP9
					&& !pat.endsWith(QLatin1String{".webm"}, Qt::CaseInsensitive)
#endif
					&& !pat.endsWith(QLatin1String{".tiff"}, Qt::CaseInsensitive)
					&& !pat.endsWith(QLatin1String{".tif"}, Qt::CaseInsensitive))
				gapr::report("Cannot handle file type: ", pat);
			std::array<int64_t, 3> s;
			std::array<int64_t, 3> cs;
			auto si=data.find("size");
			if(si==data.end())
				gapr::report("Volume size not defined.");
			auto r=parseValues<int64_t, 3>(si->second, s);
			if(!r)
				gapr::report("Failed to parse volume size.");
			auto ci=data.find("cubesize");
			if(ci==data.end())
				gapr::report("Cube size not defined.");
			r=parseValues<int64_t, 3>(ci->second, cs);
			if(!r)
				gapr::report("Failed to parse cube size.");

			auto annotTree=parseAnnot<T>(data, pos);

			Location* location{nullptr};
			if(isUrl(pat)) {
				location=loaderShared->getLocation(true, true, pat);
			} else {
				auto pos_new=compose<T>(pos, pat);
				location=loaderShared->getLocation(true, T, pos_new);
			}

			if(location)
				sources.push_back(Source{pfx, location, annotTree, s, cs, xform});
			return;
		}
		gapr::report("Unknown type of image source.");
	}
	template<bool T> void parseCatalog(std::istream& ifs, const QString& pfx, const QString& pos, const std::array<double, 3>& p0, const std::array<double, 9>& dir) {
		std::string line;
		QString section{};
		std::map<std::string, std::string> data{};
		while(std::getline(ifs, line)) {
			if(line.empty())
				continue;
			if(line[0]=='#')
				continue;
			if(line[0]=='[') {
				auto i=line.find(']');
				if(i==std::string::npos)
					gapr::report("Failed to parse line: ", line);
				if(!section.isNull()) {
					addSource<T>(data, pfx+section, pos, p0, dir);
					data.clear();
				}
				section=QLatin1String(&line[1], i-1);
			} else {
				auto i=line.find('=');
				if(i==std::string::npos)
					gapr::report("Failed to parse line: ", line);
				// XXX tolower
				data[line.substr(0, i)]=line.substr(i+1);
			}
		}
		if(!ifs.eof())
			gapr::report("Error while reading catalog file.");
		if(!section.isNull()) {
			addSource<T>(data, pfx+section, pos, p0, dir);
		}
	}
	template<bool U> void loadCatalog(const QString& pfx, const QString& pos, const std::array<double, 3>& p0, const std::array<double, 9>& dir) {
		auto i=pathsVisited.find(pos);
		if(i!=pathsVisited.end())
			return;
		pathsVisited.insert(pos);

		if(U) {
			auto f=CacheThread::instance()->download(pos);
			if(!f)
				gapr::report("Failed to start downloading catalog.");
			auto r=CacheThread::instance()->waitForFile(f);
			if(r!=0)
				gapr::report("Failed to download catalog. (r!=0)");
			if(!f.isReady()) {
				Q_EMIT thread->threadWarning("Failed to download catalog. (not ready)");
				return;
			}
			gapr::MemInput mems{f.buffers(), f.size()};
			parseCatalog<U>(mems, pfx, pos, p0, dir);
		} else {
			std::ifstream ifs{pos.toStdString()};
			if(!ifs) {
				Q_EMIT thread->threadWarning("Failed to open catalog.");
				return;
			}
			parseCatalog<U>(ifs, pfx, pos, p0, dir);
		}
	}

	bool setPositionSmall(int ch, int idx, const Cube& cur) {
		if(idx==0)
			return false;
		gapr::print("setpossmall");
		bool load_new_cube=true;
		if(cur.data.data) {
			auto& source=sources[idx];
			if(cur.data.id.channel==source.location->id) {
				if(cur.xform==source.xform)
					load_new_cube=false;
			}
		}
		if(load_new_cube) {
			std::unique_lock<std::mutex> lck_in{mtx_in};
			auto it=toload.find(ch);
			if(it==toload.end()) {
				toload[ch]={idx, 0, 0, 0, 1, 1, 1};
				nFilesToLoad+=toload[ch].nFiles();
			} else {
				nFilesToLoad-=it->second.nFiles();
				it->second={idx, 0, 0, 0, 1, 1, 1};
				nFilesToLoad+=it->second.nFiles();
			}
			cv_in.notify_one();
		}
		return load_new_cube;
	}
	bool setPositionBig(int ch, int idx, double x, double y, double z, double sight, const Cube& cur) {
		if(idx==0)
			return false;
		gapr::print("setposbig");
		bool load_new_cube=true;
		bool cube_reusable=false;
		auto& source=sources[idx];
		gapr::print(x, '/', y, '/', z);
		gapr::print(info.xform.direction[0], '/', info.xform.direction[1], '/', info.xform.direction[2]);
		gapr::print(info.xform.direction_inv[0], '/', info.xform.direction_inv[1], '/', info.xform.direction_inv[2]);
		double dx=x-info.xform.origin[0];
		double dy=y-info.xform.origin[1];
		double dz=z-info.xform.origin[2];
		double xp=info.xform.direction_inv[0]*dx+info.xform.direction_inv[3]*dy+info.xform.direction_inv[6]*dz;
		double yp=info.xform.direction_inv[1]*dx+info.xform.direction_inv[4]*dy+info.xform.direction_inv[7]*dz;
		double zp=info.xform.direction_inv[2]*dx+info.xform.direction_inv[5]*dy+info.xform.direction_inv[8]*dz;
		gapr::print(xp, '/', yp, '/', zp);
		bool same_channel=false;
		if(cur.data.data) {
			if(cur.data.id.channel==info.location->id) {
				if(cur.xform==info.xform) {
					cube_reusable=true;
					if(xp-sight/info.xform.resolution[0]-cur.data.id.origin[0]<0) cube_reusable=false;
					if(xp+sight/info.xform.resolution[0]-cur.data.limit<0>()>0) cube_reusable=false;
					if(yp-sight/info.xform.resolution[1]-cur.data.id.origin[1]<0) cube_reusable=false;
					if(yp+sight/info.xform.resolution[1]-cur.data.limit<1>()>0) cube_reusable=false;
					if(zp-sight/info.xform.resolution[2]-cur.data.id.origin[2]<0) cube_reusable=false;
					if(zp+sight/info.xform.resolution[2]-cur.data.limit<2>()>0) cube_reusable=false;
					gapr::print(cube_reusable);
					same_channel=true;
				}
			}
		}
		if(load_new_cube) {
			double rr=sight*1.61803398875;
			int xn=rr/info.xform.resolution[0]*2/info.cubesize[0]+2;
			int yn=rr/info.xform.resolution[1]*2/info.cubesize[1]+2;
			int zn=rr/info.xform.resolution[2]*2/info.cubesize[2]+2;
			if(xn>(info.size[0]+info.cubesize[0]-1)/info.cubesize[0])
				xn=(info.size[0]+info.cubesize[0]-1)/info.cubesize[0];
			if(yn>(info.size[1]+info.cubesize[1]-1)/info.cubesize[1])
				yn=(info.size[1]+info.cubesize[1]-1)/info.cubesize[1];
			if(zn>(info.size[2]+info.cubesize[2]-1)/info.cubesize[2])
				zn=(info.size[2]+info.cubesize[2]-1)/info.cubesize[2];
			int32_t x0=xp;
			x0=((x0*2+info.cubesize[0])/info.cubesize[0]-xn)/2*info.cubesize[0];
			int32_t y0=yp;
			y0=((y0*2+info.cubesize[1])/info.cubesize[1]-yn)/2*info.cubesize[1];
			int32_t z0=zp;
			z0=((z0*2+info.cubesize[2])/info.cubesize[2]-zn)/2*info.cubesize[2];
			if(x0<0) x0=0;
			if(x0+info.cubesize[0]*xn-info.cubesize[0]>=info.size[0]) x0=(info.size[0]+info.cubesize[0]-1)/info.cubesize[0]*info.cubesize[0]-xn*info.cubesize[0];
			if(y0<0) y0=0;
			if(y0+info.cubesize[1]*yn-info.cubesize[1]>=info.size[1]) y0=(info.size[1]+info.cubesize[1]-1)/info.cubesize[1]*info.cubesize[1]-yn*info.cubesize[1];
			if(z0<0) z0=0;
			if(z0+info.cubesize[2]*zn-info.cubesize[2]>=info.size[2]) z0=(info.size[2]+info.cubesize[2]-1)/info.cubesize[2]*info.cubesize[2]-zn*info.cubesize[2];
			if(same_channel) {
				if(xn==cur.data.id.counts[0]
						&& yn==cur.data.id.counts[1]
						&& zn==cur.data.id.counts[2]) {
					if(cube_reusable) {
						load_new_cube=false;
					}
					if(x0==cur.data.id.origin[0]
							&& y0==cur.data.id.origin[1]
							&& z0==cur.data.id.origin[2]) {
						load_new_cube=false;
					}
				}
			}
			if(load_new_cube) {
				std::unique_lock<std::mutex> lck_in{mtx_in};
				auto it=toload.find(ch);
				if(it==toload.end()) {
					toload[ch]={idx, x0, y0, z0, xn, yn, zn};
					nFilesToLoad+=toload[ch].nFiles();
				} else {
					nFilesToLoad-=it->second.nFiles();
					it->second={idx, x0, y0, z0, xn, yn, zn};
					nFilesToLoad+=it->second.nFiles();
				}
				cv_in.notify_one();
			}
		}
		return load_new_cube;
	}
	bool setPosition(int ch, int idx, double x, double y, double z, double sight, const Cube& cur) {
		//if(ch<0 || ch>=9)
		//gapr::report("Channel index out of range!");
		if(idx<0 || idx>=sources.size())
			gapr::report("Mapped channel index out of range!");
		auto& source=sources[idx];
		if(info.location->isPat)
			return setPositionBig(ch, idx, x, y, z, sight, cur);
		return setPositionSmall(ch, idx, cur);
	}

	std::unique_ptr<LoadData> setupJobLocalSmall(const LoadPosition& pos) {
		if(pos.xn!=1 || pos.yn!=1 || pos.zn!=1)
			gapr::report("Asertion failed!");
		std::unique_ptr<LoadData> data{new LoadData{}};
		data->pos=pos;
		data->cube=loaderShared->getCube(CubeId{sources[pos.idx].location->id, pos.x0, pos.y0, pos.z0, pos.xn, pos.yn, pos.zn});
		std::unique_lock<std::mutex> lck{loaderShared->mtx_cubes};
		auto s=data->cube->states[0]; //none //reading //read //failed
		if(s==CubeData::State::Empty) {
			data->files.push_back(sources[pos.idx].location->location);
			data->ptrs.push_back({0});
		}
		return data;
	}
	std::unique_ptr<LoadData> setupJobLocalBig(const LoadPosition& pos) {
		std::unique_ptr<LoadData> data{new LoadData{}};
		data->pos=pos;
		data->cube=loaderShared->getCube(CubeId{sources[pos.idx].location, sources[pos.idx].annotTree, pos.x0, pos.y0, pos.z0, pos.xn, pos.yn, pos.zn});
		std::unique_lock<std::mutex> lck{loaderShared->mtx_cubes};
		int i=0;
		for(int zi=0; zi<pos.zn; zi++) {
			for(int yi=0; yi<pos.yn; yi++) {
				for(int xi=0; xi<pos.xn; xi++) {
					auto s=data->cube->states[i]; //none //reading //read //failed
					if(s==CubeData::State::Empty) {
						auto loc=getCubeFileName(sources[pos.idx].location->location, pos.x0+xi*sources[pos.idx].cubesize[0], pos.y0+yi*sources[pos.idx].cubesize[1], pos.z0+zi*sources[pos.idx].cubesize[2]);
						data->files.push_back(loc);
						data->ptrs.push_back({i, xi, yi, zi});
					}
					i++;
				}
			}
		}

		return data;
	}
	std::unique_ptr<LoadData> setupJobRemoteSmall(const LoadPosition& pos) {
		if(pos.xn!=1 || pos.yn!=1 || pos.zn!=1)
			gapr::report("Asertion failed!");
		std::unique_ptr<LoadData> data{new LoadData{}};
		data->pos=pos;
		data->cube=loaderShared->getCube(CubeId{sources[pos.idx].location, sources[pos.idx].annotTree, pos.x0, pos.y0, pos.z0, pos.xn, pos.yn, pos.zn});
		std::unique_lock<std::mutex> lck{loaderShared->mtx_cubes};
		auto s=data->cube->states[0]; //none //reading //read //failed
		if(s==CubeData::State::Empty) {
			data->files.push_back(sources[pos.idx].location->location);
			data->cache.push_back(CacheThread::instance()->download(sources[pos.idx].location->location));
			data->ptrs.push_back({0});
		}
		return data;
	}
	std::unique_ptr<LoadData> setupJobRemoteBig(const LoadPosition& pos) {
		std::unique_ptr<LoadData> data{new LoadData{}};
		data->pos=pos;
		data->cube=loaderShared->getCube(CubeId{sources[pos.idx].location, sources[pos.idx].annotTree, pos.x0, pos.y0, pos.z0, pos.xn, pos.yn, pos.zn});
		std::unique_lock<std::mutex> lck{loaderShared->mtx_cubes};
		int i=0;
		for(int zi=0; zi<pos.zn; zi++) {
			for(int yi=0; yi<pos.yn; yi++) {
				for(int xi=0; xi<pos.xn; xi++) {
					auto s=data->cube->states[i]; //none //reading //read //failed
					if(s==CubeData::State::Empty) {
						auto loc=getCubeFileName(sources[pos.idx].location->location, pos.x0+xi*sources[pos.idx].cubesize[0], pos.y0+yi*sources[pos.idx].cubesize[1], pos.z0+zi*sources[pos.idx].cubesize[2]);
						data->files.push_back(loc);
						data->cache.push_back(CacheThread::instance()->download(loc));
						data->ptrs.push_back({i, xi, yi, zi});
					}
					i++;
				}
			}
		}

		return data;
	}
	void loadImageSmall(ImageReader* imageReader, const QString& file, CubeData* cube) {
		if(!imageReader)
			gapr::report("Cannot read file: ", file);
		gapr::CubeType type;
		if(!imageReader->getType(&type))
			gapr::report("Cannot get image type: ", file);
		switch(type) {
			case gapr::CubeType::U8:
			case CubeType::U16:
			case CubeType::U32:
			case CubeType::I8:
			case CubeType::I16:
			case CubeType::I32:
			case CubeType::F32:
				break;
			default:
				gapr::report("Voxel type not supported");
		}
		auto at=cube->id.annotTree;
		int64_t w, h, d;
		if(!imageReader->getSizes(&w, &h, &d))
			gapr::report("Cannot get image sizes: ", file);
		if(at) {
			CubeType otype;
			auto atsize=at->size();
			if(atsize<=255) {
				otype=CubeType::U8;
			} else if(atsize<=65535) {
				otype=CubeType::U16;
			} else {
				otype=CubeType::U32;
			}

			if(!loaderShared->alloc(cube, otype, w, h, d))
				gapr::report("Failed to alloc cube memory");

			auto bpv=bytes_per_voxel(type);
			auto dat=new char[bpv*w*h*d];
			if(!imageReader->readData(dat, 0, 0, 0, bpv*w, bpv*w*h))
				gapr::report("Failed to read image data: ", file);

			switch(type) {
				case CubeType::U8:
					at->mapData(reinterpret_cast<uint8_t*>(dat), cube->data, w, h, d, 0, 0, 0, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				case CubeType::I8:
					at->mapData(reinterpret_cast<int8_t*>(dat), cube->data, w, h, d, 0, 0, 0, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				case CubeType::U16:
					at->mapData(reinterpret_cast<uint16_t*>(dat), cube->data, w, h, d, 0, 0, 0, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				case CubeType::I16:
					at->mapData(reinterpret_cast<int16_t*>(dat), cube->data, w, h, d, 0, 0, 0, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				case CubeType::U32:
					at->mapData(reinterpret_cast<uint32_t*>(dat), cube->data, w, h, d, 0, 0, 0, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				case CubeType::I32:
					at->mapData(reinterpret_cast<int32_t*>(dat), cube->data, w, h, d, 0, 0, 0, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				case CubeType::U64:
					at->mapData(reinterpret_cast<uint64_t*>(dat), cube->data, w, h, d, 0, 0, 0, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				case CubeType::I64:
					at->mapData(reinterpret_cast<int64_t*>(dat), cube->data, w, h, d, 0, 0, 0, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				default:
					gapr::report("Voxel type not supported");
			}
		} else {
			if(!loaderShared->alloc(cube, type, w, h, d))
				gapr::report("Failed to alloc cube memory");
			auto bpv=bytes_per_voxel(type);
			if(!imageReader->readData(cube->data, 0, 0, 0, bpv*cube->sizeAdj<0>(), bpv*cube->sizeAdj<0>()*cube->sizeAdj<1>()))
				gapr::report("Failed to read image data: ", file);
		}
	}
	void loadImageBig(ImageReader* imageReader, const QString& file, CubeData* cube, int xi, int yi, int zi, int64_t cw, int64_t ch, int64_t cd) {
		if(!imageReader)
			gapr::report("Cannot read file: ", file);
		CubeType type;
		if(!imageReader->getType(&type))
			gapr::report("Cannot get image type: ", file);
		switch(type) {
			case CubeType::U8:
			case CubeType::U16:
			case CubeType::U32:
			case CubeType::I8:
			case CubeType::I16:
			case CubeType::I32:
			case CubeType::F32:
				break;
			default:
				gapr::report("Voxel type not supported");
		}
		auto at=cube->id.annotTree;
		if(at) {
			CubeType otype;
			auto atsize=at->size();
			if(atsize<=255) {
				otype=CubeType::U8;
			} else if(atsize<=65535) {
				otype=CubeType::U16;
			} else {
				otype=CubeType::U32;
			}

			if(!loaderShared->alloc(cube, otype, cw, ch, cd))
				gapr::report("Failed to alloc cube memory");

			auto bpv=bytes_per_voxel(type);
			auto dat=new char[bpv*cw*ch*cd];
			if(!imageReader->readData(dat, 0, 0, 0, bpv*cw, bpv*cw*ch))
				gapr::report("Failed to read image data: ", file);

			switch(type) {
				case CubeType::U8:
					at->mapData(reinterpret_cast<uint8_t*>(dat), cube->data, cw, ch, cd, xi*cw, yi*ch, zi*cd, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				case CubeType::I8:
					at->mapData(reinterpret_cast<int8_t*>(dat), cube->data, cw, ch, cd, xi*cw, yi*ch, zi*cd, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				case CubeType::U16:
					at->mapData(reinterpret_cast<uint16_t*>(dat), cube->data, cw, ch, cd, xi*cw, yi*ch, zi*cd, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				case CubeType::I16:
					at->mapData(reinterpret_cast<int16_t*>(dat), cube->data, cw, ch, cd, xi*cw, yi*ch, zi*cd, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				case CubeType::U32:
					at->mapData(reinterpret_cast<uint32_t*>(dat), cube->data, cw, ch, cd, xi*cw, yi*ch, zi*cd, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				case CubeType::I32:
					at->mapData(reinterpret_cast<int32_t*>(dat), cube->data, cw, ch, cd, xi*cw, yi*ch, zi*cd, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				case CubeType::U64:
					at->mapData(reinterpret_cast<uint64_t*>(dat), cube->data, cw, ch, cd, xi*cw, yi*ch, zi*cd, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				case CubeType::I64:
					at->mapData(reinterpret_cast<int64_t*>(dat), cube->data, cw, ch, cd, xi*cw, yi*ch, zi*cd, cube->sizeAdj<0>(), cube->sizeAdj<0>()*cube->sizeAdj<1>());
					break;
				default:
					gapr::report("Voxel type not supported");
			}
		} else {
			if(!loaderShared->alloc(cube, type, cw, ch, cd))
				gapr::report("Failed to alloc cube memory");
			auto bpv=bytes_per_voxel(type);
			if(!imageReader->readData(cube->data, xi*cw, yi*ch, zi*cd, bpv*cube->sizeAdj<0>(), bpv*cube->sizeAdj<0>()*cube->sizeAdj<1>()))
				gapr::report("Failed to read image data: ", file);
		}
	}
	bool loadCubeLocalSmall(LoadData* curJob) {
		if(curJob->files.empty())
			return false;
		size_t r=SIZE_MAX;
		if(!curJob->files[0].isEmpty())
			r=0;
		if(r>=curJob->files.size())
			return false;
		auto file=std::move(curJob->files[r]);
		auto s=loaderShared->changeState(curJob->cube._cube, 0, CubeData::State::Empty, CubeData::State::Reading);
		if(s==CubeData::State::Empty) {
			try {
				std::unique_ptr<ImageReader> imageReader{ImageReader::create(file.toStdString())};
				loadImageSmall(imageReader.get(), file, curJob->cube._cube);
				loaderShared->changeState(curJob->cube._cube, 0, CubeData::State::Reading, CubeData::State::Ready);
				curJob->cube->nFinished++;
			} catch(std::exception& e) {
				gapr::print("Failed to load image: ", curJob->files[r]);
				loaderShared->changeState(curJob->cube._cube, 0, CubeData::State::Reading, CubeData::State::Error);
				curJob->cube->nFinished++;
				curJob->cube->nError++;
			}
			return true;
		} else if(s==CubeData::State::Reading) {
			loaderShared->waitForCube(curJob->cube._cube, 0);
			return true;
		} else {
			return true;
		}
	}
	bool loadCubeLocalBig(LoadData* curJob) {
		size_t r=SIZE_MAX;
		for(size_t i=0; i<curJob->files.size(); i++) {
			if(!curJob->files[i].isEmpty()) {
				r=i;
				break;
			}
		}
		if(r>=curJob->files.size())
			return false;
		auto file=std::move(curJob->files[r]);
		auto cpi=curJob->ptrs[r].i;
		auto s=loaderShared->changeState(curJob->cube._cube, cpi, CubeData::State::Empty, CubeData::State::Reading);
		if(s==CubeData::State::Empty) {
			try {
				std::unique_ptr<ImageReader> imageReader{ImageReader::create(file.toStdString())};
				loadImageBig(imageReader.get(), file, curJob->cube._cube, curJob->ptrs[r].xi, curJob->ptrs[r].yi, curJob->ptrs[r].zi, sources[curJob->pos.idx].cubesize[0], sources[curJob->pos.idx].cubesize[1], sources[curJob->pos.idx].cubesize[2]);
				loaderShared->changeState(curJob->cube._cube, 0, CubeData::State::Reading, CubeData::State::Ready);
				curJob->cube->nFinished++;
			} catch(std::exception& e) {
				gapr::print("Failed to load image: ", curJob->files[r]);
				loaderShared->changeState(curJob->cube._cube, 0, CubeData::State::Reading, CubeData::State::Error);
				curJob->cube->nFinished++;
				curJob->cube->nError++;
			}
			return true;
		} else if(s==CubeData::State::Reading) {
			loaderShared->waitForCube(curJob->cube._cube, 0);
			return true;
		} else {
			return true;
		}
		return false;
	}
	bool loadCubeRemoteSmall(LoadData* curJob) {
		if(curJob->cache.empty())
			return false;
		auto r=CacheThread::instance()->waitForFile(curJob->cache[0]);
		if(r>=1)
			return false;
		auto cacheFile=std::move(curJob->cache[r]);
		auto s=loaderShared->changeState(curJob->cube._cube, 0, CubeData::State::Empty, CubeData::State::Reading);
		if(s==CubeData::State::Empty) {
			try {
				if(!cacheFile.isReady())
					gapr::report("Failed to download file");
				std::unique_ptr<ImageReader> imageReader{ImageReader::create(curJob->files[r].toStdString(), cacheFile.buffers(), cacheFile.size())};
				loadImageSmall(imageReader.get(), curJob->files[0], curJob->cube._cube);
				loaderShared->changeState(curJob->cube._cube, 0, CubeData::State::Reading, CubeData::State::Ready);
				curJob->cube->nFinished++;
			} catch(std::exception& e) {
				gapr::print("Failed to load image: ", curJob->files[r]);
				loaderShared->changeState(curJob->cube._cube, 0, CubeData::State::Reading, CubeData::State::Error);
				curJob->cube->nFinished++;
				curJob->cube->nError++;
			}
			return true;
		} else if(s==CubeData::State::Reading) {
			loaderShared->waitForCube(curJob->cube._cube, 0);
			return true;
		} else {
			return true;
		}
	}
	bool loadCubeRemoteBig(LoadData* curJob) {
		if(curJob->cache.empty())
			return false;
		auto r=CacheThread::instance()->waitForFiles(curJob->cache);
		if(r>=curJob->cache.size())
			return false;
		auto cacheFile=std::move(curJob->cache[r]);
		auto cpi=curJob->ptrs[r].i;
		auto s=loaderShared->changeState(curJob->cube._cube, cpi, CubeData::State::Empty, CubeData::State::Reading);
		if(s==CubeData::State::Empty) {
			try {
				if(!cacheFile.isReady())
					gapr::report("Failed to download file");
				std::unique_ptr<ImageReader> imageReader{ImageReader::create(curJob->files[r].toStdString(), cacheFile.buffers(), cacheFile.size())};
				loadImageBig(imageReader.get(), curJob->files[0], curJob->cube._cube, curJob->ptrs[r].xi, curJob->ptrs[r].yi, curJob->ptrs[r].zi, sources[curJob->pos.idx].cubesize[0], sources[curJob->pos.idx].cubesize[1], sources[curJob->pos.idx].cubesize[2]);
				loaderShared->changeState(curJob->cube._cube, 0, CubeData::State::Reading, CubeData::State::Ready);
				curJob->cube->nFinished++;
			} catch(std::exception& e) {
				gapr::print("Failed to load image: ", curJob->files[r]);
				loaderShared->changeState(curJob->cube._cube, 0, CubeData::State::Reading, CubeData::State::Error);
				curJob->cube->nFinished++;
				curJob->cube->nError++;
			}
			return true;
		} else if(s==CubeData::State::Reading) {
			loaderShared->waitForCube(curJob->cube._cube, 0);
			return true;
		} else {
			return true;
		}
		return false;
	}

	void loadCubes () {
		std::deque<std::unique_ptr<LoadData>> toloadCur{};

		std::unique_lock<std::mutex> lck{mtx_in};
		while(!stopRequested) {
			std::unordered_set<LoadPosition> toloadNew{};
			for(auto& p: toload) {
				if(p.second.idx>0)
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
						if(sources[pos->pos.idx].location->isUrl) {
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
					if(sources[pos.idx].location->isUrl) {
						if(nRemote<2) {
							std::unique_ptr<LoadData> data{};
							if(sources[pos.idx].location->isPat) {
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
							if(sources[pos.idx].location->isPat) {
								data=setupJobLocalBig(pos);
							} else {
								data=setupJobLocalSmall(pos);
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
				auto& source=sources[curJob->pos.idx];

				bool progr=false;
				if(source.location->isUrl) {
					if(source.location->isPat) {
						progr=loadCubeRemoteBig(curJob);
					} else {
						progr=loadCubeRemoteSmall(curJob);
					}
				} else {
					if(source.location->isPat) {
						progr=loadCubeLocalBig(curJob);
					} else {
						progr=loadCubeLocalSmall(curJob);
					}
				}

				lck.lock();
				if(curJob->cube->finished()) {
					QVector<int> chs{};
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
						std::unique_lock<std::mutex> lck_out{mtx_out};
						cubes_out.push_back({curJob->pos.idx, Cube{}});
						if(!err) {
							cubes_out.back().second.data=std::move(curJob->cube);
							cubes_out.back().second.xform=sources[curJob->pos.idx].xform;
						}
						Q_EMIT thread->cubeFinished();
						// msgbox(errror)
					}
					toloadCur.pop_front();
					Q_EMIT thread->updateProgress(nFilesLoaded+filesLoadedTmp, nFilesToLoad);
				} else if(progr) {
					auto filesLoadedTmp=0;
					for(auto it=toload.begin(); it!=toload.end(); ++it) {
						for(size_t i=0; i<toloadCur.size(); i++) {
							if(toloadCur[i].get()->pos==it->second) {
								filesLoadedTmp+=toloadCur[i].get()->cube->nFinished;
							}
						}
					}
					Q_EMIT thread->updateProgress(nFilesLoaded+filesLoadedTmp, nFilesToLoad);
				}
				continue;
			}
			lck.lock();
			cv_in.wait(lck);
		}
	}
	void setupSources() {
		sources.push_back(Source{QString{}, nullptr, nullptr, {0, 0, 0}, {0, 0, 0}, CubeXform{}});

		for(auto& loc: rootlocs) {
			if(loc.location.compare(QLatin1String{"catalog"}, Qt::CaseInsensitive)==0
					|| loc.location.endsWith(QLatin1String{"/catalog"}, Qt::CaseInsensitive)) {
			} else {
				if(loc.location.endsWith(QLatin1String{".nrrd"}, Qt::CaseInsensitive)
#ifdef WITH_VP9
						|| loc.location.endsWith(QLatin1String{".webm"}, Qt::CaseInsensitive)
#endif
						|| loc.location.endsWith(QLatin1String{".tif"}, Qt::CaseInsensitive)
						|| loc.location.endsWith(QLatin1String{".tiff"}, Qt::CaseInsensitive)) {
					Location* location{nullptr};
					QString fn{};
					if(isUrl(loc.location)) {
						location=loaderShared->getLocation(false, true, loc.location);
						fn=QUrl{loc.location}.fileName();
					} else {
						location=loaderShared->getLocation(false, false, loc.location);
						fn=QFileInfo{loc.location}.fileName();
					}
					if(location)
						sources.push_back(Source{fn, location, nullptr, {0, 0, 0}, {0, 0, 0}, {loc.origin, loc.direction}});
				} else {
					gapr::report("Unknown input volume type.");
				}
			}
		}
		for(auto& s: sources) {
			for(int i=0; i<3; i++)
				s.xform.resolution[i]=sqrt(s.xform.direction[0+i*3]*s.xform.direction[0+i*3]+s.xform.direction[1+i*3]*s.xform.direction[1+i*3]+s.xform.direction[2+i*3]*s.xform.direction[2+i*3]);
			if(!calcInverse(s.xform.direction, s.xform.direction_inv)) {
				for(int i=0; i<9; i++)
					std::cerr<<s.xform.direction[i]<<"\n";
				gapr::report("Failed to calculate the inverse of direction matrix.");
			}
			if(s.annotTree) {
				try {
					if(!s.annotTree->ensureLoaded())
						s.annotTree=nullptr;
				} catch(const std::exception& e) {
					Q_EMIT thread->threadWarning(QString{"Failed to parse annotation: "}+e.what());
					s.annotTree=nullptr;
				}
			}
		}

		sources_ready=true;
		Q_EMIT thread->sourcesReady();
	}

	LoadThreadPriv(LoadThread* _loader):
		thread{_loader},
		loaderShared{Tracer::instance()->loaderShared()},
		rootlocs{}, pathsVisited{}, sources{}, sources_ready{false},
		mtx_in{}, cv_in{}, stopRequested{false}, toload{},
		nFilesToLoad{0}, nFilesLoaded{0}
	{
	}
	LoadThreadPriv(LoadThread* _loader, const QString& input):
		LoadThreadPriv{s, _loader}
	{
		if(input.location.isEmpty())
			return;
		if(input.location.compare(QLatin1String{"none"}, Qt::CaseInsensitive)==0)
			return;
		rootlocs.push_back(input);
	}
	void stop() {
		std::unique_lock<std::mutex> lck{mtx_in};
		stopRequested=true;
		cv_in.notify_one();
	}
	void cancel() {
		std::unique_lock<std::mutex> lck{mtx_in};
		for(auto& p: toload) {
			if(p.second.idx>0) {
				nFilesToLoad-=p.second.nFiles();
			}
		}
		toload.clear();
	}
};

// LoadThread
void LoadThread::run() {
	gapr::print("Load thread started");
	try {
		priv->setupSources();
		priv->loadCubes();
	} catch(const std::exception& e) {
		Q_EMIT threadError(QString{"Unexpected error in load thread: "}+=e.what());
		gapr::print("Load thread error");
	}
	gapr::print("Load thread finished");
}
LoadThread::LoadThread(QObject* s):
	QThread{s}, priv{new LoadThreadPriv{s, this}}
{
}
LoadThread::LoadThread(QObject* s, const QString& input):
	QThread{s}, priv{new LoadThreadPriv{s, this, input}}
{
}

QVector<QString> LoadThread::sources() const {
	if(!priv->sources_ready)
		return {};
	QVector<QString> srcs;
	for(auto& s: priv->sources)
		srcs.push_back(s.name);
	return srcs;
}
#endif
#endif

