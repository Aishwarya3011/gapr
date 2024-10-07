#include "compute.hh"

#include "gapr/utility.hh"

#include <algorithm>
#include <list>

using Point=gapr::edge_model::point;

// XXX enable configurable timeout

double areaOfTriangle(const gapr::node_attr& p0, const gapr::node_attr& p1, const gapr::node_attr& p2, double maxdist) {
	auto dx=p2.pos(0)-p0.pos(0);
	auto dy=p2.pos(1)-p0.pos(1);
	auto dz=p2.pos(2)-p0.pos(2);
	if(sqrt(dx*dx+dy*dy+dz*dz)>maxdist)
		return INFINITY;
	auto ux=p0.pos(0)-p1.pos(0);
	auto uy=p0.pos(1)-p1.pos(1);
	auto uz=p0.pos(2)-p1.pos(2);
	auto vx=p2.pos(0)-p1.pos(0);
	auto vy=p2.pos(1)-p1.pos(1);
	auto vz=p2.pos(2)-p1.pos(2);
	auto cx=uy*vz-uz*vy;
	auto cy=uz*vx-ux*vz;
	auto cz=ux*vy-uy*vx;
	return sqrt(cx*cx+cy*cy+cz*cz)/2;
}

std::vector<Point> static reducePointsVisvalingam(const std::vector<Point>& pts, double maxdist, double maxarea) {
	// smooth
	std::list<std::pair<Point, double>> temppts;
	temppts.emplace_back(pts[0], INFINITY);
	for(size_t i=2; i<pts.size(); i++) {
		gapr::node_attr ptsp{pts[i-1]};
		gapr::node_attr ptspp{pts[i-2]};
		gapr::node_attr ptsc{pts[i]};
		//////////
		auto x=(ptsp.pos(0)*2+ptspp.pos(0)*1+ptsc.pos(0)*1)/4;
		auto y=(ptsp.pos(1)*2+ptspp.pos(1)*1+ptsc.pos(1)*1)/4;
		auto z=(ptsp.pos(2)*2+ptspp.pos(2)*1+ptsc.pos(2)*1)/4;
		auto r=(ptsp.misc.r()*2+ptspp.misc.r()*1+ptsc.misc.r()*1)/4;
		temppts.emplace_back(gapr::node_attr{x, y, z, r, 0}.data(), areaOfTriangle(ptspp, ptsp, ptsc, maxdist));
	}
	temppts.emplace_back(pts[pts.size()-1], INFINITY);
	while(temppts.size()>2) {
		double minarea=INFINITY;
		std::list<std::pair<Point, double>>::iterator miniter;
		auto i=temppts.begin();
		i++;
		auto j=i;
		j++;
		for(; j!=temppts.end(); i++, j++) {
			if(i->second<minarea) {
				minarea=i->second;
				miniter=i;
			}
		}
		if(minarea>maxarea)
			break;
		auto prev=miniter;
		prev--;
		auto next=miniter;
		next++;
		gapr::node_attr pts_curr{miniter->first};
		gapr::node_attr pts_prev{prev->first};
		gapr::node_attr pts_next{next->first};
		prev->first=gapr::node_attr{(pts_prev.pos(0)*2+pts_curr.pos(0))/3, (pts_prev.pos(1)*2+pts_curr.pos(1))/3, (pts_prev.pos(2)*2+pts_curr.pos(2))/3, (pts_prev.misc.r()*2+pts_curr.misc.r())/3, 0}.data();
		next->first=gapr::node_attr{(pts_next.pos(0)*2+pts_curr.pos(0))/3, (pts_next.pos(1)*2+pts_curr.pos(1))/3, (pts_next.pos(2)*2+pts_curr.pos(2))/3, (pts_next.misc.r()*2+pts_curr.misc.r())/3, 0}.data();
		temppts.erase(miniter);
		if(prev!=temppts.begin()) {
			auto pprev=prev;
			pprev--;
			prev->second=areaOfTriangle(gapr::node_attr{pprev->first}, pts_prev, pts_next, maxdist);
			if(pprev!=temppts.begin()) {
				auto ppprev=pprev;
				ppprev--;
				pprev->second=areaOfTriangle(gapr::node_attr{ppprev->first}, gapr::node_attr{pprev->first}, pts_prev, maxdist);
			}
		}
		auto nnext=next;
		nnext++;
		if(nnext!=temppts.end()) {
			next->second=areaOfTriangle(pts_prev, pts_next, gapr::node_attr{nnext->first}, maxdist);
			auto nnnext=nnext;
			nnnext++;
			if(nnnext!=temppts.end()) {
				nnext->second=areaOfTriangle(pts_next, gapr::node_attr{nnext->first}, gapr::node_attr{nnnext->first}, maxdist);
			}
		}
	}
	std::vector<Point> outpts{};
	for(auto& p: temppts) {
		outpts.push_back(p.first);
	}
	//for(size_t i=2; i<outpts.size(); i++)
		//fprintf(stderr, "lastarea: %lf\n", areaOfTriangle(outpts[i-2], outpts[i-1], outpts[i], maxdist));
	return outpts;
}

struct CubeRef {
	gapr::cube_view<const void> view;

	double xres, yres, zres;

	std::array<double, 3> origin;
	std::array<double, 9> dir;
	std::array<double, 9> rdir;

	union {
		const uint8_t* dataU8;
		const uint16_t* dataU16;
		const char* data;
	};
	bool is16bit;

	std::array<double, 2> _xfunc;

	CubeRef(const gapr::cube& cube, const gapr::affine_xform& xform, const std::array<uint32_t, 3> offset, const std::array<double, 2>& xfunc):
		view{cube.view<const void>()},
		xres{xform.resolution[0]},
		yres{xform.resolution[1]},
		zres{xform.resolution[2]},
		dir{xform.direction}, rdir{xform.direction_inv},
		_xfunc{xfunc}
	{
		switch(view.type()) {
			case gapr::cube_type::u8:
				dataU8=static_cast<const uint8_t*>(view.row(0, 0));
				is16bit=false;
				break;
			case gapr::cube_type::u16:
				dataU16=static_cast<const uint16_t*>(view.row(0, 0));
				is16bit=true;
				break;
			default:
				data=nullptr;
				return;
		}

		for(int i=0; i<3; i++)
			origin[i]=xform.origin[i]+dir[i]*offset[0]+dir[i+3]*offset[1]+dir[i+6]*offset[2];
	}

	bool valid() const { return data; }

	double voxel(int64_t i) const { return is16bit?dataU16[i]/65535.0:dataU8[i]/255.0; }
	uint32_t voxel_uint(int64_t i) const { return is16bit?dataU16[i]:dataU8[i]; }
	void index2xyzf(int64_t i, double* px, double* py, double* pz) const {
		double x=i%view.width_adj()+.5;
		i/=view.width_adj();
		double y=i%view.sizes(1)+.5;
		i/=view.sizes(1);
		double z=i+.5;
		*px=x*dir[0]+y*dir[3]+z*dir[6]+origin[0];
		*py=x*dir[1]+y*dir[4]+z*dir[7]+origin[1];
		*pz=x*dir[2]+y*dir[5]+z*dir[8]+origin[2];
	}
	int64_t xyzf2index(double x, double y, double z) const {
		x-=origin[0];
		y-=origin[1];
		z-=origin[2];
		auto xf=x*rdir[0]+y*rdir[3]+z*rdir[6];
		if(xf<0)
			return -1;
		uint32_t xi=xf;
		if(xi>=view.sizes(0))
			return -1;
		auto yf=x*rdir[1]+y*rdir[4]+z*rdir[7];
		if(yf<0)
			return -1;
		uint32_t yi=yf;
		if(yi>=view.sizes(1))
			return -1;
		auto zf=x*rdir[2]+y*rdir[5]+z*rdir[8];
		if(zf<0)
			return -1;
		uint32_t zi=zf;
		if(zi>=view.sizes(2))
			return -1;
		return xi+view.width_adj()*(yi+view.sizes(1)*zi);
	}
	void xyzf2xyzi(double x, double y, double z, int32_t* px, int32_t* py, int32_t* pz) const {
		x-=origin[0];
		y-=origin[1];
		z-=origin[2];
		*px=x*rdir[0]+y*rdir[3]+z*rdir[6];
		*py=x*rdir[1]+y*rdir[4]+z*rdir[7];
		*pz=x*rdir[2]+y*rdir[5]+z*rdir[8];
	}
	void index2xyzi(int64_t i, int32_t* px, int32_t* py, int32_t* pz) const {
		*px=i%view.width_adj();
		i/=view.width_adj();
		*py=i%view.sizes(1);
		i/=view.sizes(1);
		*pz=i;
	}
	int64_t xyzi2index(uint32_t xi, uint32_t yi, uint32_t zi) const {
		return xi+view.width_adj()*(yi+view.sizes(1)*zi);
	}
	double xfunc(double v) const {
		auto& p=_xfunc;
		if(v<p[0])
			return 0.0;
		if(v>p[1])
			return 1.0;
		auto d=p[1]-p[0];
		if(d==0)
			return .5;
		return (v-p[0])/(p[1]-p[0]);
	}
};

gapr::delta_add_edge_ gapr::fix::ConnectAlg::compute() {
	if(args.method%3==2) {
		gapr::edge_model::reader reader{*args.graph};
		auto left=reader.to_anchor(args.cur_pos).link;
		auto right=reader.to_anchor(args.tgt_pos).link;
		std::vector<Point> points{};
		auto a=gapr::node_attr{args.cur_pos.point}.pos();
		auto b=gapr::node_attr{args.tgt_pos.point}.pos();
		auto d=b-a;
		auto l=d.mag();
		unsigned int n=lround(l/1.6+.6);
		for(unsigned int k=0; k<=n; ++k) {
			auto pos=(a*(n-k)+b*k)/n;
			gapr::node_attr x{};
			x.misc.coverage(1);
			x.pos(pos);
			points.push_back(x.data());
		}
		return {left.data(), right.data(), std::move(points)};
	}
	CubeRef cube_ref{args.cube, *args.xform, args.offset, args.xfunc};
	if(!cube_ref.valid())
		return {};
	//const Graph* graph;
	//Position cur_pos;
	//Position tgt_pos;

	//gapr::cube cube;
	//const gapr::affine_xform* xform;
	//std::array<uint32_t, 3> offset;
	//std::array<double, 2> xfunc;

	struct AstarNode {
		int64_t came_from, came_from2;
		int64_t index;
		double g_score;
		double g_score_b;
		double h_score;
		int8_t state; // -+3, l/r open; -+2, l/r closed; -+1, l/r collide, 0 skip
	};
	double v0{0.007};
	bool success{false};
	std::vector<size_t> pq;
	std::vector<AstarNode> store;
	std::unordered_map<int64_t, size_t> idx2node;

	// improvements?
	auto heuristic_cost_estimate=[&cube_ref,&v0](int64_t b, const auto& c, double g, double g_b) -> double {
		gapr::vec3<double> bb, cc;
		cube_ref.index2xyzf(b, &bb[0], &bb[1], &bb[2]);
		double v1{INFINITY};
		for(auto [ci, xx]: c) {
			cube_ref.index2xyzf(ci, &cc[0], &cc[1], &cc[2]);
			cc-=bb;
			auto bc=cc.mag();
			if(bc<v1)
				v1=bc;
		}
		return v1/(g_b/g+v0);
	};
	auto dist_between=[&cube_ref,&v0](int64_t a, int64_t b, double d) -> double {
		auto aa=cube_ref.xfunc(cube_ref.voxel(a))*255;
		auto bb=cube_ref.xfunc(cube_ref.voxel(b))*255;
		return (1/(aa+v0)+1/(bb+v0))*d/2;
	};

	auto& p0=args.cur_pos;
	auto& p1=args.tgt_pos;
	//auto& gr=*args.graph;

	auto AstarNodeCmp=[&store](size_t a, size_t b) -> bool {
		return store[a].g_score+store[a].h_score>store[b].g_score+store[b].h_score;
	};

	std::vector<std::pair<int64_t, unsigned int>> starts, goals;
	gapr::node_attr pos0{p0.point};
	gapr::node_attr pos1{p1.point};
	int64_t start=cube_ref.xyzf2index(pos0.pos(0), pos0.pos(1), pos0.pos(2));
	int64_t goal=cube_ref.xyzf2index(pos1.pos(0), pos1.pos(1), pos1.pos(2));
	if(args.method%3==0) {
		gapr::edge_model::reader reader{*args.graph};
		auto add_starts=[&reader,&cube_ref](auto& starts, int64_t start, const auto& pos) {
			if(pos.edge) {
				auto& edg=reader.edges().at(pos.edge);
				unsigned int idx=pos.index/128;
				unsigned int idx0=idx>5?idx-5:0;
				unsigned int idx1=idx+5<edg.points.size()?idx+5:edg.points.size()-1;
				for(idx=idx0; idx<=idx1; ++idx) {
					gapr::node_attr p{edg.points[idx]};
					int64_t pp=cube_ref.xyzf2index(p.pos(0), p.pos(1), p.pos(2));
					starts.emplace_back(pp, 1+idx);
				}
			} else if(pos.vertex) {
				starts.emplace_back(start, 0);
			} else {
				starts.emplace_back(start, 0);
			}
		};
		add_starts(starts, start, p0);
		add_starts(goals, goal, p1);
	} else {
		starts.emplace_back(start, 0);
		goals.emplace_back(goal, 0);
	}
	for(auto [p,i]: starts) {
		for(auto [q,j]: goals) {
			if(p==q)
				return {};
		}
	}

	size_t nn;
	for(auto [start, idx]: starts) {
		store.emplace_back();
		nn=store.size()-1;
		store[nn].came_from=-1;
		store[nn].came_from-=idx;
		store[nn].came_from2=-1;
		store[nn].index=start;
		store[nn].g_score=0;
		store[nn].g_score_b=0;
		store[nn].h_score=0;
		store[nn].state=-3;
		idx2node[start]=nn;
		pq.push_back(nn); std::push_heap(pq.begin(), pq.end(), AstarNodeCmp);
	}
	for(auto [goal, idx]: goals) {
		store.emplace_back();
		nn=store.size()-1;
		store[nn].came_from=-1;
		store[nn].came_from-=idx;
		store[nn].came_from2=-1;
		store[nn].index=goal;
		store[nn].g_score=0;
		store[nn].g_score_b=0;
		store[nn].h_score=0;
		store[nn].state=3;
		idx2node[goal]=nn;
		pq.push_back(nn); std::push_heap(pq.begin(), pq.end(), AstarNodeCmp);
	}

	struct { int dx, dy, dz; double dist; } all_neighbors [] = {
		{-1, 0, 0, cube_ref.xres},
		{1, 0, 0, cube_ref.xres},
		{0, -1, 0, cube_ref.yres},
		{0, 1, 0, cube_ref.yres},
		{0, 0, -1, cube_ref.zres},
		{0, 0, 1, cube_ref.zres}
	};

	size_t currentp;
	unsigned int loop_cnt{0};
	while(!pq.empty()) {
		currentp=pq.front();
		std::pop_heap(pq.begin(), pq.end(), AstarNodeCmp); pq.pop_back();
		auto st=store[currentp].state;
		bool skip=false;
		switch(st) {
			case 0:
				skip=true;
				break;
			case -1:
			case 1:
				success=true;
				break;
			default:
				fprintf(stderr, "WRONG state1 %hhd!\n", st);
				return {};
			case -3:
				st++;
				break;
			case 3:
				st--;
				break;
		}
		if(skip)
			continue;
		if(success)
			break;

		loop_cnt++;
		if(loop_cnt%100==0) {
			if(args.cancel->load())
				break;
			if(loop_cnt%100000==0)
				gapr::print("compute: loop: ", loop_cnt);
			if(loop_cnt>500000)
				break;
		}

		store[currentp].state=st;
		auto current=store[currentp].index;

		int32_t x, y, z;
		cube_ref.index2xyzi(current, &x, &y, &z);
		for(auto& delta: all_neighbors) {
			uint32_t neighbor_x=x+delta.dx;
			uint32_t neighbor_y=y+delta.dy;
			uint32_t neighbor_z=z+delta.dz;
			if(neighbor_x<0 || neighbor_x>=cube_ref.view.sizes(0))
				continue;
			if(neighbor_y<0 || neighbor_y>=cube_ref.view.sizes(1))
				continue;
			if(neighbor_z<0 || neighbor_z>=cube_ref.view.sizes(2))
				continue;
			int64_t neighbor=cube_ref.xyzi2index(neighbor_x, neighbor_y, neighbor_z);
			auto neighbori=idx2node.find(neighbor);
			bool inserted=false;
			if(neighbori!=idx2node.end()) {
				auto neighborn=neighbori->second;
				bool cont=false;
				bool upd1=false;
				auto st2=store[neighborn].state;
				switch(st2) {
					default:
						fprintf(stderr, "WRONG state2 %hhd\n", st2);
						return {};
					case -2:
					case +2:
						cont=true;
						break;
					case -3:
					case 3:
						break;
					case -1:
					case 1:
						upd1=true;
						break;
				}
				if(cont)
					continue;
				double tentative_g_score=store[currentp].g_score+dist_between(current, neighbor, delta.dist);
				double tentative_g_score_b=store[currentp].g_score_b+delta.dist;
				if(upd1) {
					auto score_prev=st*st2<0?store[neighborn].h_score:store[neighborn].g_score;
					if(tentative_g_score<score_prev) {
						store[neighborn].state=0;
						store.emplace_back();
						nn=store.size()-1;
						store[nn].came_from=current;
						store[nn].came_from2=st*st2<0?store[neighborn].came_from:store[neighborn].came_from2;
						store[nn].g_score=tentative_g_score;
						store[nn].g_score_b=tentative_g_score_b;
						store[nn].h_score=st*st2<0?store[neighborn].g_score:store[neighborn].h_score;
						store[nn].state=st<0?-1:1;
						inserted=true;
					}
				} else {
					if(st*st2<0) {
						store[neighborn].state=0;
						store.emplace_back();
						nn=store.size()-1;
						store[nn].came_from=current;
						store[nn].came_from2=store[neighborn].came_from;
						store[nn].g_score=tentative_g_score;
						store[nn].g_score_b=tentative_g_score_b;
						store[nn].h_score=store[neighborn].g_score;
						store[nn].state=st<0?-1:1;
						inserted=true;
					} else if(tentative_g_score<store[neighborn].g_score) {
						store[neighborn].state=0;
						store.emplace_back();
						nn=store.size()-1;
						store[nn].came_from=current;
						store[nn].came_from2=store[neighborn].came_from2;
						store[nn].g_score=tentative_g_score;
						store[nn].g_score_b=tentative_g_score_b;
						store[nn].h_score=heuristic_cost_estimate(neighbor, st<0?goals:starts, tentative_g_score, tentative_g_score_b);
						store[nn].state=st2;
						inserted=true;
					}
				}
			} else {
				double tentative_g_score=store[currentp].g_score+dist_between(current, neighbor, delta.dist);
				double tentative_g_score_b=store[currentp].g_score_b+delta.dist;
				store.emplace_back();
				nn=store.size()-1;
				store[nn].came_from=current;
				store[nn].came_from2=-1;
				store[nn].g_score=tentative_g_score;
				store[nn].g_score_b=tentative_g_score_b;
				store[nn].h_score=heuristic_cost_estimate(neighbor, st<0?goals:starts, tentative_g_score, tentative_g_score_b);
				store[nn].state=st<0?-3:3;
				inserted=true;
			}
			if(inserted) {
				//store[nn].came_from=current;
				store[nn].index=neighbor;
				idx2node[neighbor]=nn;
				pq.push_back(nn); std::push_heap(pq.begin(), pq.end(), AstarNodeCmp);
			}
		}
	}

	if(!success)
		return {};

	std::vector<int64_t> idxes0;
	std::vector<int64_t> idxes1;
	gapr::print("idxex0: ", store[currentp].index);
	idxes0.push_back(store[currentp].index);
	{
		auto idx=store[currentp].came_from;
		while(idx>=0) {
			gapr::print("idxex0: ", idx);
			idxes0.push_back(idx);
			idx=store[idx2node[idx]].came_from;
		}
		gapr::print("idxex0: ", idx);
		idxes0.push_back(idx);
	}
	{
		auto idx=store[currentp].came_from2;
		while(idx>=0) {
		gapr::print("idxex1: ", idx);
			idxes1.push_back(idx);
			idx=store[idx2node[idx]].came_from;
		}
		gapr::print("idxex1: ", idx);
		idxes1.push_back(idx);
	}

	std::vector<Point> points{};
	if(store[currentp].state>0)
		std::swap(idxes0, idxes1);
	auto left_idx=idxes0.back(), right_idx=idxes1.back();
	idxes0.pop_back();
	idxes1.pop_back();
	points.reserve(idxes0.size()+idxes1.size());
	while(!idxes0.empty()) {
		auto v=idxes0.back();
		idxes0.pop_back();
		double mx, my, mz;
		cube_ref.index2xyzf(v, &mx, &my, &mz);
		points.emplace_back(gapr::node_attr{mx, my, mz}.data());
	}
	for(auto v: idxes1) {
		double mx, my, mz;
		cube_ref.index2xyzf(v, &mx, &my, &mz);
		points.emplace_back(gapr::node_attr{mx, my, mz}.data());
	}
	if(points.size()<2)
		return {};


	//estimateRadius(pth->points);
	points=reducePointsVisvalingam(points, 3.0, 0.5);
	//estimateRadius(pth->points);

	// XXX before deducing???
#if 0
	if(p0.edge) {
		points.front()={{0, 0, 0}, 0};
	}
	if(p1.edge) {
		points.back()={{0, 0, 0}, 0};
	}
#endif

	pq.clear();
	store.clear();
	idx2node.clear();

	gapr::edge_model::reader reader{*args.graph};
	auto get_link=[&reader](auto& p0, int64_t idx) {
		assert(idx<0);
		if(idx==-1)
			return reader.to_anchor(p0).link;
		assert(p0.edge);
		auto p00=p0;
		p00.index=(-2-idx)*128;
		return reader.to_anchor(p00).link;
	};
	auto left=get_link(p0, left_idx);
	auto right=get_link(p1, right_idx);
	for(auto& n: points) {
		gapr::node_attr attr{n};
		attr.misc.coverage(1);
		n=attr.data();
	}
	return {left.data(), right.data(), std::move(points)};
}



#if 0
struct AlgConnAStar : public AlgConn {
	void estimateRadius(std::vector<Point>& pth) {
		double res=xres<0>();
		double maxr=65536/256;
		if(res>yres<0>()) res=yres<0>();
		if(res>zres<0>()) res=zres<0>();
		res/=2;
		for(size_t i=0; i<pth.size(); i++) {
			std::vector<double> rvals{};
			int64_t idx_center=xyzf2index<0>(pth[i].x(), pth[i].y(), pth[i].z());
			double val_center=xfunc<0>(cube<0>(idx_center))*255;
			for(int j=0; j<500; j++) {
				int l=rand();
				double dx=l&0xFF, dy=(l>>8)&0xFF, dz=(l>>8)&0xFF;
				double len=sqrt(dx*dx+dy*dy+dz*dz);
				dx/=len;
				dy/=len;
				dz/=len;
				int mdiff=512;
				double rval=0;
				int val_prev=val_center;
				for(double len=res; len<=maxr; len+=res) {
					int64_t idx=xyzf2index<0>(pth[i].x()+dx*len, pth[i].y()+dy*len, pth[i].z()+dz*len);
					if(idx<0)
						break;
					double val=xfunc<0>(cube<0>(idx))*255;
					auto diff=val-val_prev;
					if(diff<mdiff) {
						mdiff=diff;
						rval=len;
					}
					val_prev=val;
					if(i==0) {
						//fprintf(stderr, "%lf, ", diff);
					}
					if(val<-mdiff)
						break;
				}
					if(i==0) {
						//fprintf(stderr, "%lf\n", rval);
					}
				rvals.push_back(rval);
			}
			std::sort(rvals.begin(), rvals.end());
			double sum=0;
			for(int j=100; j<200; j++) {
				sum+=rvals[j];
			}
			pth[i].r(sum/100);
					//fprintf(stderr, "asdf %lf\n", pth[i].r());
		}
	}
#endif

