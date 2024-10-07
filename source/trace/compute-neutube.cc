#define _USE_MATH_DEFINES

#include "compute.hh"

#include "gapr/utility.hh"

extern "C" {
#include "tz_local_neuroseg.h"
#include "tz_trace_utils.h"
#include "tz_locseg_chain_com.h"
#include "tz_stack_math.h"
#include "tz_stack_lib.h"
#include "tz_locseg_chain.h"
}

std::vector<std::array<double, 4>> find_seeds_neutube(Stack* stack, double z_scale);
void process_stack_neutube(Stack* stack);

using ChainPtr=std::shared_ptr<Locseg_Chain>;
static ChainPtr wrap(Locseg_Chain* chain) {
	return ChainPtr{chain, [](Locseg_Chain* chain) {
		if(chain)
			Kill_Locseg_Chain(chain);
	}};
}
using StackPtr=std::shared_ptr<Stack>;
static StackPtr wrap(Stack* stack) {
	return StackPtr{stack, [](Stack* stack) {
		if(stack)
			Kill_Stack(stack);
	}};
}
using TraceWorkspacePtr=std::shared_ptr<Trace_Workspace>;
static TraceWorkspacePtr wrap(Trace_Workspace* ws) {
	return TraceWorkspacePtr{ws, [](Trace_Workspace* ws) {
		if(ws) {
			Kill_Trace_Workspace(ws);
			if(ws->fit_workspace)
				Kill_Locseg_Fit_Workspace((Locseg_Fit_Workspace*)ws->fit_workspace);
		}
	}};
}
struct NeurosegDel {
	void operator()(Local_Neuroseg* seg) const noexcept {
		if(seg)
			Kill_Local_Neuroseg(seg);
	}
};
using NeurosegPtr=std::unique_ptr<Local_Neuroseg, NeurosegDel>;

struct CubeRef {
	std::valarray<uint16_t> img;
	gapr::affine_xform xform;

	union {
		const uint8_t* _dataU8;
		const uint16_t* _dataU16;
		const float* _dataF32;
		const unsigned char* data{nullptr};
	};

	bool preproc() const noexcept { return img.size()>0; }
	float get_pix(unsigned int x, unsigned int y, unsigned int z) const noexcept {
		auto v=img[(z*sizes[1]+y)*sizes[0]+x];
		return v/float{std::numeric_limits<uint16_t>::max()};
	};
	float get_pix2(unsigned int x, unsigned int y, unsigned int z) const noexcept {
		auto v=img[((sizes[2]+z)*sizes[1]+y)*sizes[0]+x];
		return v*std::log(256.0f)/float{std::numeric_limits<uint16_t>::max()};
	};
	void bind(const gapr::cube& cube, const gapr::affine_xform& xform, const std::array<unsigned int, 3>& offset) {
		auto view=cube.view<const void>();
		this->xform=xform;
		this->xform.origin=xform.from_offset(offset);
		switch(view.type()) {
		case gapr::cube_type::u8:
			_dataU8=static_cast<const uint8_t*>(view.row(0, 0));
			kind=GREY;
			break;
		case gapr::cube_type::u16:
			_dataU16=static_cast<const uint16_t*>(view.row(0, 0));
			kind=GREY16;
			break;
		default:
			_dataF32=nullptr;
			return;
		}
		for(unsigned int i=0; i<3; ++i)
			sizes[i]=view.sizes(i);
		width_adj=view.width_adj();
		z_scale=(xform.resolution[0]+xform.resolution[1])/xform.resolution[2]/2;
	}
	void bind(std::valarray<uint16_t>&& img, const std::array<unsigned int, 3>& sizes, const gapr::affine_xform& xform, const std::array<unsigned int, 3>& offset) {
		this->img=std::move(img);
		this->sizes=sizes;
		this->sizes[2]=sizes[2]*2+1;

		this->xform=xform;
		std::array<double, 3> off;
		for(unsigned int i=0; i<3; ++i)
			off[i]=offset[i];
		off[2]-=0.25;
		this->xform.origin=xform.from_offset_f(off);
		this->xform.resolution[2]/=2;
		for(unsigned int i=0; i<3; ++i) {
			this->xform.direction[i+6]/=2;
			this->xform.direction_inv[2+3*i]*=2;
		}
		_dataU16=&this->img[0];
		kind=GREY16;
		width_adj=sizes[0];
		z_scale=(xform.resolution[0]+xform.resolution[1])/xform.resolution[2];
	}

	int kind{0};
	std::array<unsigned int, 3> sizes{0, 0, 0};
	unsigned int width_adj{0};
	double z_scale{0.0};
};

typedef std::array<double, 3> Vec;
#if 0
Vec asVec(const Point& p) {
	Vec a;
	a[0]=p.x(); a[1]=p.y(); a[2]=p.z();
	return a;
}
#endif
Vec operator+(const Vec& a, const Vec& b) {
	Vec c;
	for(size_t i=0; i<b.size(); i++)
		c[i]=a[i]+b[i];
	return c;
}
Vec operator-(const Vec& a, const Vec& b) {
	Vec c;
	for(size_t i=0; i<b.size(); i++)
		c[i]=a[i]-b[i];
	return c;
}
Vec operator*(double a, const Vec& b) {
	Vec c;
	for(size_t i=0; i<b.size(); i++)
		c[i]=b[i]*a;
	return c;
}
Vec operator/(const Vec& b, double a) {
	Vec c;
	for(size_t i=0; i<b.size(); i++)
		c[i]=b[i]/a;
	return c;
}
double dot(const Vec& a, const Vec& b) {
	return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
double len(const Vec& a) {
	return sqrt(dot(a, a));
}


void gapr::trace::ProbeAlg::Job::operator()(gapr::trace::ProbeAlg& alg) {
	auto& res=seeds;
	CubeRef cuberef;
	cuberef.bind(cube, alg._xform, {0, 0, 0});
	Stack stack;
	stack.kind=cuberef.kind;
	stack.width=cuberef.width_adj;
	stack.height=cuberef.sizes[1];
	stack.depth=cuberef.sizes[2];
	stack.text=nullptr;
	stack.array=const_cast<uint8*>(reinterpret_cast<const uint8*>(cuberef.data));
	auto zmax=Proj_Stack_Zmax(&stack);
	auto stkimg=stack;
	stkimg.depth=1;
	stkimg.array=zmax->array;
	std::vector<double> vals;
	vals.reserve(cuberef.sizes[1]*cuberef.sizes[0]);
	for(unsigned int y=0; y<cuberef.sizes[1]; y++) {
		for(unsigned int x=0; x<cuberef.sizes[0]; x++) {
			auto v=Stack_Pixel(&stkimg, x, y, 0, 0);
			if(v!=0)
				vals.push_back(v);
		}
	}
	std::sort(vals.begin(), vals.end());
	auto vmin0=vals[0];
	auto vmin=vals[vals.size()*9/10];
	auto vmax=vals[vals.size()-1];
	gapr::print("min: max: ", vmin0, ':', vmin, ':', vmax);
	vmin=(vmin-vmin0)+vmin;
	if(vmin>=vmax)
		vmin=vmax/2;
	for(unsigned int y=3; y<cuberef.sizes[1]-3; y++) {
		for(unsigned int x=3; x<cuberef.sizes[0]-3; x++) {
			auto v=Stack_Pixel(&stkimg, x, y, 0, 0);
			if(v<vmin)
				continue;
			bool is_locmax{true};
			for(int dx=-3; dx<=3; dx++) {
				for(int dy=-3; dy<=3; dy++) {
					auto v1=Stack_Pixel(&stkimg, x+dx, y+dy, 0, 0);
					if(v1>v)
						is_locmax=false;
				}
			}
			if(!is_locmax)
				continue;
			for(unsigned int z=0; z<cuberef.sizes[2]; z++) {
				if(Stack_Pixel(&stack, x, y, z, 0)==v) {
					double xf=x+.5;
					double yf=y+.5;
					double zf=z+.5;
					auto [xx, yy, zz]=alg._xform.from_offset_f({xf, yf, zf});
					res.emplace_back(std::array<double, 3>{xx, yy, zz});
				}
			}
		}
	}
}

struct NeutubeHelper {

	struct Seed {
		std::array<double, 3> pos;
		gapr::node_id nid;
		float val;
		bool operator<(const Seed& r) const noexcept { return val<r.val; }
		bool operator>(const Seed& r) const noexcept { return val>r.val; }
	};
	struct Hits {
		unsigned int r{0};
		std::size_t id;
		std::size_t idx;
		std::size_t idx0, idx1;
		double dist;

		struct Item {
			std::size_t i;
			std::size_t j;
			double d;
		};
		std::vector<Item> items;

		bool contains(std::size_t i, std::size_t j) {
			if(r<1)
				return false;
			if(r>1)
				return false;
			if(id!=i)
				return false;
			if(idx0>j)
				return false;
			if(idx1<=j)
				return false;
			return true;
		}
		void add(std::size_t i, std::size_t j, double d) {
			items.emplace_back(Item{i, j, d});
			if(r<1) {
				id=i;
				idx=j;
				idx0=j;
				idx1=j+1;
				dist=d;
				r=1;
				return;
			}
			if(r>1)
				return;
			if(id!=i) {
				r=2;
				return;
			}
			if(j>idx1 || j+1<idx0) {
				r=2;
				return;
			}
			if(j+1==idx0) {
				idx0=j;
			} else if(j==idx1) {
				idx1=j+1;
			}
			if(d<dist) {
				dist=d;
				idx=j;
			}
		}
	};
	struct Link {
		//node assoc.
		gapr::node_id nid;
		unsigned int nid_state{0};
		//chain conn.
		unsigned int adj_id;
		unsigned int adj_idx;
		unsigned int adj_state{0};

		gapr::node_id tmpid;

		Hits hits1, hits2;

		void add(gapr::node_id id, unsigned int l) {
			if(l>nid_state) {
				nid_state=l;
				nid=id;
			}
		}
		void link(std::size_t id, std::size_t idx) {
			if(adj_state<1) {
				adj_id=id;
				adj_idx=idx;
				adj_state=1;
				return;
			}
			if(adj_state>1)
				return;
			assert(0);
		}
		bool linked() const noexcept { return adj_state>0; }
	};

	struct Chain {
		ChainPtr chain;
		Seed seed;

		gapr::edge_model::edge_id eid;
		std::size_t start_i;

		std::size_t len;

		int seed_state;
		std::size_t seed_idx;

		std::vector<Link> links;
		enum TermState {
			MULT,
			MULTR,
			END,
			LINK,
			LOOP,
			ENDR
		} left_state, right_state;
		Hits left_hits1, left_hits2;
		Hits right_hits1, right_hits2;
	};

	CubeRef _cuberef;
	const gapr::edge_model& _model;
	gapr::node_attr _bbox_p0, _bbox_p1;
	double _z_scale;

	Stack _stack;
	std::shared_ptr<Stack> _stack_ptr;
	TraceWorkspacePtr _trace_workspace;
	Locseg_Fit_Workspace* _fit_workspace;
	Stack* _stack_lbl;

	std::vector<Chain> _existing_chains;
	std::vector<Chain> _fresh_chains;
	std::vector<Seed> _lost_seeds;

	NeutubeHelper(const gapr::edge_model& graph, CubeRef&& cube_ref): _cuberef{std::move(cube_ref)}, _model{graph} {
		_z_scale=_cuberef.z_scale;
		_z_scale=1;
		prepare_bbox();
	}

	bool in_bbox(gapr::node_attr::ipos_type p) {
		for(unsigned int k=0; k<3; k++) {
			if(p[k]<_bbox_p0.ipos[k])
				return false;
			if(p[k]>_bbox_p1.ipos[k])
				return false;
		}
		return true;
	}
	std::array<double, 3> to_local_pos(const std::array<double, 3>& pos) {
		std::array<double, 3> p;
		auto x=pos[0]-_cuberef.xform.origin[0];
		auto y=pos[1]-_cuberef.xform.origin[1];
		auto z=pos[2]-_cuberef.xform.origin[2];
		auto& rdir=_cuberef.xform.direction_inv;
		for(unsigned int i=0; i<3; i++)
			p[i]=x*rdir[i]+y*rdir[3+i]+z*rdir[6+i]-.5;
		p[2]/=_z_scale;
		return p;
	}
	gapr::node_attr to_node_attr(const double* pos) {
		gapr::node_attr p;
		auto x=pos[0]+.5;
		auto y=pos[1]+.5;
		auto z=pos[2]*_z_scale+.5;
		auto& dir=_cuberef.xform.direction;
		auto& origin=_cuberef.xform.origin;
		for(unsigned int i=0; i<3; i++)
			p.pos(i, x*dir[i]+y*dir[3+i]+z*dir[6+i]+origin[i]);
		return p;
	}
	gapr::node_attr to_node_attr(const Geo3d_Circle& c) {
		auto p=to_node_attr(c.center);
		p.misc.r(c.radius*(_cuberef.xform.resolution[0]+_cuberef.xform.resolution[1])/2);
		return p;
	}

	void prepare_bbox() {
		std::array<double, 6> bbox{INFINITY, INFINITY, INFINITY,
			-INFINITY, -INFINITY, -INFINITY};
		for(unsigned int idx=0; idx<8; idx++) {
			unsigned x=0, y=0, z=0;
			if(idx&1)
				x=_cuberef.sizes[0];
			if(idx&2)
				y=_cuberef.sizes[1];
			if(idx&4)
				z=_cuberef.sizes[2];
			auto& dir=_cuberef.xform.direction;
			auto& origin=_cuberef.xform.origin;
			for(unsigned int i=0; i<3; i++) {
				auto v=x*dir[i]+y*dir[3+i]+z*dir[6+i]+origin[i];
				if(v<bbox[i])
					bbox[i]=v;
				if(v>bbox[3+i])
					bbox[3+i]=v;
			}
		}
		for(unsigned int i=0; i<3; i++) {
			_bbox_p0.pos(i, bbox[i]);
			_bbox_p1.pos(i, bbox[3+i]);
		}
	}

	struct EdgeCache {
		bool is_seed;
		std::vector<gapr::node_id> nodes;
		std::vector<gapr::node_attr::data_type> points;
	};
	struct VertCache {
		bool is_seed;
		gapr::node_attr attr;
		std::vector<std::pair<gapr::edge_model::edge_id, bool>> edges;
	};
	std::unordered_map<gapr::edge_model::edge_id, EdgeCache> _edges_cache;
	std::unordered_map<gapr::edge_model::vertex_id, VertCache> _verts_cache;
	VertCache& cache_vert(gapr::edge_model::reader& model, gapr::edge_model::vertex_id vid, const gapr::edge_model::vertex& vert, bool handle_adj) {
		auto [it, ins]=_verts_cache.emplace(vid, VertCache{});
		if(ins) {
			it->second.is_seed=false;
			it->second.attr=vert.attr;
			for(auto e: vert.edges)
				it->second.edges.push_back(e);
		}
		if(handle_adj) {
			for(auto [eid, dir]: vert.edges) {
				auto& edg=model.edges().at(eid);
				cache_edge(model, eid, edg, false);
			}
		}
		return it->second;
	}
	EdgeCache& cache_edge(gapr::edge_model::reader& model, gapr::edge_model::edge_id eid, const gapr::edge_model::edge& edg, bool handle_adj) {
		auto [it, ins]=_edges_cache.emplace(eid, EdgeCache{});
		if(ins) {
			it->second.is_seed=false;
			for(auto e: edg.nodes)
				it->second.nodes.push_back(e);
			for(auto e: edg.points)
				it->second.points.push_back(e);
		}
		if(handle_adj) {
			for(auto vid: {edg.left, edg.right}) {
				auto& vert=model.vertices().at(vid);
				cache_vert(model, vid, vert, false);
			}
		}
		return it->second;
	}
	void cache_model() {
		gapr::edge_model::reader model{_model};
		for(auto& [vid, vert]: model.vertices()) {
			if(vert.edges.size()>=2)
				continue;
			if(model.props().find(gapr::prop_id{vid, "state"})!=model.props().end())
				continue;
			if(model.props().find(gapr::prop_id{vid, ".traced"})!=model.props().end())
				continue;
			auto& p=vert.attr;
			if(!in_bbox(p.ipos))
				continue;
			auto& vert2=cache_vert(model, vid, vert, true);
			if(vert.edges.size()==0) {
				vert2.is_seed=true;
				continue;
			}
			vert2.is_seed=true;
		}
		for(auto& [eid, edg]: model.edges()) {
			ChainPtr chain{nullptr};
			unsigned int hits=0;
			auto N=edg.nodes.size();
			for(std::size_t i=0; i<N; i++) {
				auto& p=edg.points[i];
				if(!in_bbox(p.first)) {
				} else {
					++hits;
				}
			}
			if(hits) {
				auto& edg2=cache_edge(model, eid, edg, true);
				edg2.is_seed=true;
			}
		}
	}
	void find_seeds_graph(std::vector<Seed>& seeds) {
		for(auto& [vid, vert]: _verts_cache) {
			if(!vert.is_seed)
				continue;
			auto& p=vert.attr;
			seeds.push_back(Seed{to_local_pos({p.pos(0), p.pos(1), p.pos(2)}), vid});
		}
	}

	void find_seeds_preproc(std::vector<Seed>& seeds) {
		constexpr int pad_xy=0, pad_z=0;
		constexpr std::size_t max_point_cnt{200'000};
		auto prev_cnt=seeds.size();

		for(unsigned int z=pad_z; z+pad_z<_cuberef.sizes[2]; ++z) {
			for(unsigned int y=pad_xy; y+pad_xy<_cuberef.sizes[1]; ++y) {
				for(unsigned int x=pad_xy; x+pad_xy<_cuberef.sizes[0]; ++x) {
					auto v=_cuberef.get_pix(x, y, z);
					if(v<.9)
						continue;
					if(seeds.size()-prev_cnt>=max_point_cnt && v<seeds[prev_cnt].val)
						continue;
					bool local_max{true};
					do {
						auto vl=std::max(_cuberef.get_pix(x-2, y, z), _cuberef.get_pix(x-1, y, z));
						auto vr=std::max(_cuberef.get_pix(x+2, y, z), _cuberef.get_pix(x+1, y, z));
						if(v>=vl && v>=vr)
							break;
						auto vt=std::max(_cuberef.get_pix(x, y-2, z), _cuberef.get_pix(x, y-1, z));
						auto vb=std::max(_cuberef.get_pix(x, y+2, z), _cuberef.get_pix(x, y+1, z));
						if(v>=vt && v>=vb)
							break;
						local_max=false;
					} while(false);
					if(local_max) {
						seeds.push_back(Seed{{x*1.0, y*1.0, z/_z_scale}, gapr::node_id{}, v});
						std::push_heap(seeds.begin()+prev_cnt, seeds.end(), std::greater<>{});
					}
				}
				while(seeds.size()-prev_cnt>=max_point_cnt) {
					std::pop_heap(seeds.begin()+prev_cnt, seeds.end(), std::greater<>{});
					seeds.pop_back();
				}
			}
		}
		std::sort_heap(seeds.begin()+prev_cnt, seeds.end(), std::greater<>{});
	}
	void find_seeds_image(std::vector<Seed>& seeds, double min_val) {
		if(_cuberef.preproc())
			return find_seeds_preproc(seeds);
		{
			auto seeds_in=find_seeds_neutube(&_stack, _z_scale);
			for(auto& [x,y,z,r]: seeds_in) {
				fprintf(stderr, "seed %lf %lf %lf %lf %lf %zu\n", x, y, z, r, _z_scale, seeds_in.size());
				seeds.push_back(Seed{{x*1.0, y*1.0, z/_z_scale}, gapr::node_id{}});
			}
			return;
		}
		//return;
		auto zmax=Proj_Stack_Zmax(&_stack);
		auto stkimg=_stack;
		stkimg.depth=1;
		stkimg.array=zmax->array;
		struct Pt {
			unsigned int x, y;
			double val;
		};
		std::vector<Pt> vals;
		vals.reserve(_cuberef.sizes[1]*_cuberef.sizes[0]);
		for(unsigned int y=0; y<_cuberef.sizes[1]; y++) {
			for(unsigned int x=0; x<_cuberef.sizes[0]; x++) {
				auto v=Stack_Pixel(&stkimg, x, y, 0, 0);
				if(v!=0) {
					v=v-1;
					vals.emplace_back(Pt{x, y, v});
					Set_Stack_Pixel(&stkimg, x, y, 0, 0, v);
				}
			}
		}
		std::sort(vals.begin(), vals.end(), [](const Pt& a, const Pt& b) {
			return a.val>b.val;
		});
		auto vmin0=vals[vals.size()-1].val;
		auto vmin=vals[vals.size()*1/10].val;
		auto vmax=vals[0].val;
		gapr::print("min: max: ", vmin0, ':', vmin, ':', vmax);
		vmin=1*(vmin-vmin0)+vmin;
		for(auto& ptval: vals) {
			auto [x, y, v]=ptval;
			if(v<vmin)
				break;
				
			bool is_locmax{true};
			bool all_eq{true};
			for(int dx=-3; dx<=3; dx++) {
				for(int dy=-3; dy<=3; dy++) {
					auto v1=Stack_Pixel(&stkimg, x+dx, y+dy, 0, 0);
					if(v1>v)
						is_locmax=false;
					if(v1<v)
						all_eq=false;
				}
			}
			if(!is_locmax || all_eq)
				continue;
			v+=1;
			Set_Stack_Pixel(&stkimg, x, y, 0, 0, v);
			for(unsigned int z=0; z<_cuberef.sizes[2]; z++) {
				int nless{0};
				for(int dz=-3; dz<=3; dz++) {
					auto v1=Stack_Pixel(&_stack, x, y, z+dz, 0);
					if(v1<v)
						nless++;
				}
				if(Stack_Pixel(&_stack, x, y, z, 0)==v && nless>0) {
					if(v>=min_val)
						seeds.push_back(Seed{{x*1.0, y*1.0, z/_z_scale}, gapr::node_id{}});
				}
			}
		}
	}

	void add_existing_chain(ChainPtr c, gapr::edge_model::edge_id eid, std::size_t i) {
		Chain chain;
		chain.chain=c;
		chain.eid=eid;
		chain.start_i=i;
		_existing_chains.push_back(chain);
	}
	void add_fresh_chain(ChainPtr c, const Seed& seed) {
		auto all_masked=check_all_masked(c);
		Chain chain;
		chain.chain=c;
		chain.seed=seed;
		chain.eid=0;
		chain.len=Locseg_Chain_Length(c.get());
		gapr::print(1, "seed: ", seed.nid.data, " ", seed.pos[0],'/', seed.pos[1],'/', seed.pos[2]);
		link_seed(chain);
		update_mask(c);
		if(all_masked)
			return;
		if(Locseg_Chain_Length(c.get())<=3)
			return;
		_fresh_chains.push_back(chain);
	}

	void process_edges() {
		for(auto& [eid, edg]: _edges_cache) {
			if(!edg.is_seed)
				continue;
			ChainPtr chain{nullptr};
			std::size_t start_i;
			auto N=edg.nodes.size();
			for(std::size_t i=0; i<N; i++) {
				auto& p=edg.points[i];
				if(!in_bbox(p.first)) {
					if(chain) {
						add_existing_chain(chain, eid, start_i);
						chain=nullptr;
					}
					continue;
				}
				gapr::node_attr pp{p};
				auto pos=to_local_pos({pp.pos(0), pp.pos(1), pp.pos(2)});
				NeurosegPtr seg;
				if(chain) {
					auto iter=Locseg_Node_Dlist_Tail(chain->list);
					seg=optimize_fixed_pos(pos, iter->data->locseg);
				} else {
					seg=optimize_fixed_pos(pos);
				}
				if(!chain) {
					chain=wrap(New_Locseg_Chain());
					start_i=i;
				}
				Local_Neuroseg_Label_G(seg.get(), _stack_lbl, 0, 1, _z_scale);

				auto tr=New_Trace_Record();
				tr->mask=ZERO_BIT_MASK;
				Trace_Record_Set_Fix_Point(tr, 0.0);
				Trace_Record_Set_Direction(tr, DL_BOTHDIR);
				Locseg_Chain_Add_Node(chain.get(), Make_Locseg_Node(seg.release(), tr), DL_TAIL);
			}
			if(chain)
				add_existing_chain(chain, eid, start_i);
		}
	}

	void update_mask(ChainPtr chain) {
		auto n=Locseg_Chain_Length(chain.get());
		Locseg_Chain_Label_G(chain.get(), _stack_lbl, _z_scale, 0, n, 1.5, 0.0, 0, 1);
	}

	template<unsigned int npix>
	bool check_boundary1(const std::array<double, 3>& pos) {
		if(pos[0]<npix || pos[1]<npix || pos[2]<npix ||
				pos[0]+npix>=_cuberef.sizes[0] ||
				pos[1]+npix>=_cuberef.sizes[1] ||
				pos[2]+npix>=_cuberef.sizes[2]/_z_scale) {
			return true;
		}
		return false;
	}

	bool check_boundary(const std::array<double, 3>& pos) {
		return check_boundary1<8>(pos);
	}
	bool check_boundary(Local_Neuroseg* locseg) {
		std::array<double, 3> pos;
		Local_Neuroseg_Center(locseg, &pos[0]);
		return check_boundary(pos);
	}
	bool check_mask(const std::array<double, 3>& pos) {
		int x=pos[0]+0.5;
		int y=pos[1]+0.5;
		int z=pos[2]*_z_scale+0.5;
		if(check_boundary(pos))
			return true;

		if(*STACK_PIXEL_8(_stack_lbl, x, y, z, 0) > 0) {
			return true;
		}
		return false;
	}
	bool check_mask(Local_Neuroseg* locseg) {
		std::array<double, 3> pos;
		Local_Neuroseg_Center(locseg, &pos[0]);
		return check_mask(pos);
	}
	bool check_val(Local_Neuroseg* locseg) {
		if(_cuberef.preproc())
			return false;
		std::array<double, 3> pos;
		Local_Neuroseg_Center(locseg, &pos[0]);
		int x=pos[0]+0.5;
		int y=pos[1]+0.5;
		int z=pos[2]*_z_scale+0.5;

		fprintf(stderr, "check value: %lf\n", Stack_Pixel(&_stack, x, y, z, 0));
		if(Stack_Pixel(&_stack, x, y, z, 0) < 25)
			return true;
		return false;
	}

	bool check_score(Local_Neuroseg* locseg) {
		constexpr double seed_min_score=0.35;
		Stack_Fit_Score fs;
		fs.n=1;
		fs.options[0]=_trace_workspace->tscore_option;
		auto s=Local_Neuroseg_Score(locseg, &_stack, _z_scale, &fs);
		gapr::print("score: ", s);
		if(s<seed_min_score)
			return true;
		return false;
	}

	void get_workspace() {
		auto tspace=wrap(Locseg_Chain_Default_Trace_Workspace(nullptr, &_stack));
		if(!tspace->fit_workspace) {
			tspace->fit_workspace=New_Locseg_Fit_Workspace();
		}
		tspace->refit=true;
		tspace->tune_end=true;
		tspace->add_hit=true;

		tspace->resolution[0]=_cuberef.xform.resolution[0];
		tspace->resolution[1]=_cuberef.xform.resolution[1];
		tspace->resolution[2]=_cuberef.xform.resolution[2];

		tspace->trace_range[0]=0;
		tspace->trace_range[3]=_cuberef.sizes[0]-1;
		tspace->trace_range[1]=0;
		tspace->trace_range[4]=_cuberef.sizes[1]-1;
		tspace->trace_range[2]=0;
		tspace->trace_range[5]=_cuberef.sizes[2]-1;

		_stack_lbl=tspace->trace_mask=Make_Stack(GREY8, _stack.width, _stack.height, _stack.depth);
		Zero_Stack(_stack_lbl);

		tspace->min_score=0.3;
		tspace->tscore_option=STACK_FIT_CORRCOEF;

		_trace_workspace=tspace;
		_fit_workspace=(Locseg_Fit_Workspace*)tspace->fit_workspace;
	}

	void prepare_stack() {
		_stack.kind=_cuberef.kind;
		_stack.width=_cuberef.width_adj;
		_stack.height=_cuberef.sizes[1];
		_stack.depth=_cuberef.sizes[2];
		char dumb[]={'\x00'};
		_stack.text=dumb;
		_stack.array=const_cast<uint8*>(reinterpret_cast<const uint8*>(_cuberef.data));
		_stack_ptr=wrap(Copy_Stack(&_stack));
		_stack=*_stack_ptr;

#if 0
		auto fiter=Gaussian_Filter_3d(10 20 30 40, double sigma_y, double sigma_z);
		Filter_3d* Mexihat_Filter_3d(double sigma);
		Stack* Filter_Stack(const Stack *stack, const Filter_3d *filter);
#endif



	}

	void fix_stack() {
		if(_cuberef.preproc())
			return;
#if 0
		int xf=8;
		int yf=8;
		int zf=8*_z_scale;
		auto stk_bg1=wrap(Downsample_Stack_Mean(&_stack, xf, yf, zf, nullptr));
		auto stk_bg2=wrap(Stack_Running_Min(stk_bg1.get(), 2, nullptr));
		Stack_Running_Min(stk_bg2.get(), 2, stk_bg1.get());
		Stack_Running_Min(stk_bg1.get(), 0, stk_bg2.get());
		Stack_Running_Min(stk_bg2.get(), 0, stk_bg1.get());
		Stack_Running_Min(stk_bg1.get(), 0, stk_bg2.get());
		Stack_Running_Min(stk_bg2.get(), 0, stk_bg1.get());
		Stack_Running_Min(stk_bg1.get(), 0, stk_bg2.get());
		Stack_Running_Min(stk_bg2.get(), 1, stk_bg1.get());
		Stack_Running_Min(stk_bg1.get(), 1, stk_bg2.get());
		Stack_Running_Min(stk_bg2.get(), 1, stk_bg1.get());
		Stack_Running_Min(stk_bg1.get(), 1, stk_bg2.get());
		Stack_Running_Min(stk_bg2.get(), 1, stk_bg1.get());
		auto stk_bg=wrap(Resize_Stack(stk_bg1.get(), _stack.width, _stack.height, _stack.depth));

		Stack_Sub(&_stack, stk_bg.get(), &_stack);
#else
		process_stack_neutube(&_stack);
#endif
	}
	NeurosegPtr optimize(const std::array<double, 3>& pos) {
		NeurosegPtr locseg{New_Local_Neuroseg()};
		double r=3.0, c=0.0, h=11.0, theta=M_PI/4, psi=0*M_PI/2, curv=0, alpha=0, scale=1;
		Set_Neuroseg(&(locseg->seg), r, c, h, theta, psi, curv, alpha, scale);
		Set_Neuroseg_Position(locseg.get(), &pos[0], NEUROSEG_CENTER);
		Local_Neuroseg_Optimize_W(locseg.get(), &_stack, _z_scale, 1, _fit_workspace);
		r=locseg->seg.r1;
		c=locseg->seg.c;
		h=locseg->seg.h;
		theta=locseg->seg.theta;
		psi=locseg->seg.psi;
		curv=locseg->seg.curvature;
		alpha=locseg->seg.alpha;
		scale=locseg->seg.scale;
		return locseg;
	}
	NeurosegPtr optimize_fixed_pos(const std::array<double, 3>& pos, const Local_Neuroseg* ref) {
		NeurosegPtr locseg{Copy_Local_Neuroseg(ref)};
		Set_Neuroseg_Position(locseg.get(), &pos[0], NEUROSEG_CENTER);
		Fit_Local_Neuroseg_W(locseg.get(), &_stack, _z_scale, _fit_workspace);
		return locseg;
	}
	NeurosegPtr optimize_fixed_pos(const std::array<double, 3>& pos) {
		NeurosegPtr locseg{New_Local_Neuroseg()};
		double r=3.0, c=0.0, h=11.0, theta=M_PI/4, psi=0*M_PI/2, curv=0, alpha=0, scale=1;
		Set_Neuroseg(&(locseg->seg), r, c, h, theta, psi, curv, alpha, scale);
		Set_Neuroseg_Position(locseg.get(), &pos[0], NEUROSEG_CENTER);
		{
			Stack_Fit_Score fs;
			fs.n = 1;
			fs.options[0] = STACK_FIT_CORRCOEF;
			Local_Neuroseg_Orientation_Search_C(locseg.get(), &_stack, _z_scale, &fs); 
			Local_Neuroseg_R_Scale_Search(locseg.get(), &_stack, _z_scale, 1.0, 10.0, 1.0,
					0.5, 5.0, 0.5, NULL);
			Fit_Local_Neuroseg_W(locseg.get(), &_stack, _z_scale, _fit_workspace);
		}
		r=locseg->seg.r1;
		c=locseg->seg.c;
		h=locseg->seg.h;
		theta=locseg->seg.theta;
		psi=locseg->seg.psi;
		curv=locseg->seg.curvature;
		alpha=locseg->seg.alpha;
		scale=locseg->seg.scale;
		return locseg;
	}

	ChainPtr trace(NeurosegPtr locseg) {
		auto tr=New_Trace_Record();
		tr->mask=ZERO_BIT_MASK;
		Trace_Record_Set_Fix_Point(tr, 0.0);
		Trace_Record_Set_Direction(tr, DL_BOTHDIR);
		auto p=Make_Locseg_Node(locseg.release(), tr);
		auto locseg_chain=wrap(Make_Locseg_Chain(p));
		_trace_workspace->trace_status[0]=TRACE_NORMAL;
		_trace_workspace->trace_status[1]=TRACE_NORMAL;
		Trace_Locseg(&_stack, _z_scale, locseg_chain.get(), _trace_workspace.get());
		Locseg_Chain_Remove_Overlap_Ends(locseg_chain.get());
		Locseg_Chain_Remove_Turn_Ends(locseg_chain.get(), 1.0);
		printf("status0 %d\n", _trace_workspace->trace_status[0]);
		printf("status1 %d\n", _trace_workspace->trace_status[1]);
		return locseg_chain;
	}

	gapr::delta_add_patch_ debug_ds_seeds();
	gapr::delta_add_patch_ debug_cube_seeds();
	gapr::delta_add_patch_ compute();
	void print_chain(ChainPtr chain) {
		gapr::print("chain: ", chain);
		auto iter=Locseg_Node_Dlist_Head(chain->list);
		int i=0;
		std::array<double, 3> pos;
		while(iter) {
			auto seg=iter->data->locseg;
			Local_Neuroseg_Center(seg, &pos[0]);
			auto dir=Trace_Record_Direction(iter->data->tr);
			auto offset=Trace_Record_Fix_Point(iter->data->tr);

			gapr::print("chain[", i, "]: ", pos[0], ':', pos[1], ':', pos[2], ":DIR", dir, ":off", offset);
			i++;
			iter=iter->next;
		}
	}
	void print_circles(Geo3d_Circle* circles, int n) {
		for(int i=0; i<n; i++) {
			gapr::print("circ[", i, "]: ", circles[i].center[0], ':', circles[i].center[1], ':', circles[i].center[2], ':', circles[i].radius);
		}
	}
	bool check_all_masked(ChainPtr chain) {
		auto iter=Locseg_Node_Dlist_Head(chain->list);
		while(iter) {
			auto seg=iter->data->locseg;
			if(!check_boundary(seg) && !check_mask(seg))
				return false;
			iter=iter->next;
		}
		return true;
	}
	void clip_plain_chain(ChainPtr chain, const Seed& seed) {
		std::array<double, 3> pos;
		double dist{INFINITY};
		Locseg_Node_Dlist* iter_seed{nullptr};
		auto iter=Locseg_Node_Dlist_Head(chain->list);
		while(iter) {
			auto seg=iter->data->locseg;
			Local_Neuroseg_Center(seg, &pos[0]);
			auto diff=pos-seed.pos;
			auto d=len(diff);
			if(d<dist) {
				dist=d;
				iter_seed=iter;
			}
			iter=iter->next;
		}
		{
			auto it=iter_seed->prev;
			while(it) {
				auto seg=it->data->locseg;
				if(check_mask(seg)) {
					while(it->prev)
						Locseg_Chain_Remove_End(chain.get(), DL_HEAD);
					break;
				}
				it=it->prev;
			}
		}
		{
			auto it=iter_seed->next;
			while(it) {
				auto seg=it->data->locseg;
				if(check_mask(seg)) {
					while(it->next)
						Locseg_Chain_Remove_End(chain.get(), DL_TAIL);
					break;
				}
				it=it->next;
			}
		}
		{
			while(auto it=Locseg_Node_Dlist_Head(chain->list)) {
				auto seg=it->data->locseg;
				if(!check_val(seg))
					break;
				Locseg_Chain_Remove_End(chain.get(), DL_HEAD);
			}
		}
		{
			while(auto it=Locseg_Node_Dlist_Tail(chain->list)) {
				auto seg=it->data->locseg;
				if(!check_val(seg))
					break;
				Locseg_Chain_Remove_End(chain.get(), DL_TAIL);
			}
		}
	}
	void link_seed(Chain& _chain) {
		auto chain=_chain.chain;
		auto& seed=_chain.seed;
		print_chain(chain);

		std::array<double, 3> pos;
		{
			std::ostringstream oss;
			oss<<'\n';
			auto iter=Locseg_Node_Dlist_Head(chain->list);
			while(iter) {
				auto seg=iter->data->locseg;
				if(Local_Neuroseg_Hit_Test(seg, seed.pos[0], seed.pos[1], seed.pos[2])) {
					oss<<" S ";
				} else {
					oss<<"!S ";
				}
				if(check_mask(seg)) {
					oss<<" T ";
				} else {
					oss<<"!T ";
				}
				Local_Neuroseg_Center(seg, &pos[0]);
				auto diff=pos-seed.pos;
				auto d=len(diff);
				oss<<d<<'\n';
				iter=iter->next;
			}
			gapr::print(oss.str());
		}
		if(!seed.nid)
			return clip_plain_chain(chain, seed);

		double dist{INFINITY};
		Locseg_Node_Dlist* iter_seed{nullptr};
		{
			auto iter=Locseg_Node_Dlist_Head(chain->list);
			while(iter) {
				auto seg=iter->data->locseg;
				if(Local_Neuroseg_Hit_Test(seg, seed.pos[0], seed.pos[1], seed.pos[2])) {
					Local_Neuroseg_Center(seg, &pos[0]);
					auto diff=pos-seed.pos;
					auto d=len(diff);
					if(d<dist) {
						dist=d;
						iter_seed=iter;
					}
				}
				iter=iter->next;
			}
		}
		if(!iter_seed) {
			gapr::print("degrade");
			seed.nid=gapr::node_id{};
			return clip_plain_chain(chain, seed);
		}
		auto& vert=_verts_cache.at(seed.nid);
		if(vert.edges.size()==1) {
			auto [eid, dir]=vert.edges[0];
			auto& edg=_edges_cache.at(eid);
			auto pos2_=gapr::node_attr{dir?edg.points[edg.points.size()-2]:edg.points[1]};
			auto pos2=to_local_pos({pos2_.pos(0), pos2_.pos(1), pos2_.pos(2)});
			{
				double distp=0, distn=0;
				if(auto itp=iter_seed->prev; itp) {
					Local_Neuroseg_Center(itp->data->locseg, &pos[0]);
					pos=pos-pos2;
					distp=len(pos);
				}
				if(auto itn=iter_seed->next; itn) {
					Local_Neuroseg_Center(itn->data->locseg, &pos[0]);
					pos=pos-pos2;
					distn=len(pos);
				}
				if(std::abs(distp-distn)<1) {
					gapr::print("degrade: ", distp, ',', distn);
					seed.nid=gapr::node_id{};
					return clip_plain_chain(chain, seed);
				}
				if(distp<distn) {
					while(iter_seed->prev) {
						Locseg_Chain_Remove_End(chain.get(), DL_HEAD);
					}
					_chain.seed_state=-1;
				} else {
					while(iter_seed->next) {
						Locseg_Chain_Remove_End(chain.get(), DL_TAIL);
					}
					_chain.seed_state=1;
				}
			}
		} else {
			_chain.seed_state=0;
		}
		auto it0=iter_seed;
		do {
			auto seg=it0->data->locseg;
			if(!check_mask(seg))
				break;
			if(!Local_Neuroseg_Hit_Test(seg, seed.pos[0], seed.pos[1], seed.pos[2])) {
				it0=nullptr;
				break;
			}
			it0=it0->prev;
		} while(it0);
		if(it0) {
			auto it=it0->prev;
			while(it) {
				gapr::print("prev ", it);
				auto seg=it->data->locseg;
				if(check_mask(seg)) {
					gapr::print("prev taboo ", it);
					while(it->prev)
						Locseg_Chain_Remove_End(chain.get(), DL_HEAD);
					break;
				}
				it=it->prev;
			}
		} else {
			while(iter_seed->prev)
				Locseg_Chain_Remove_End(chain.get(), DL_HEAD);
		}
		auto it1=iter_seed;
		do {
			auto seg=it1->data->locseg;
			if(!check_mask(seg))
				break;
			if(!Local_Neuroseg_Hit_Test(seg, seed.pos[0], seed.pos[1], seed.pos[2])) {
				it1=nullptr;
				break;
			}
			it1=it1->next;
		} while(it1);
		if(it1) {
			auto it=it1->next;
			while(it) {
				auto seg=it->data->locseg;
				if(check_mask(seg)) {
					while(it->next)
						Locseg_Chain_Remove_End(chain.get(), DL_TAIL);
					break;
				}
				it=it->next;
			}
		} else {
			while(iter_seed->next)
				Locseg_Chain_Remove_End(chain.get(), DL_TAIL);
		}
		std::size_t seed_idx=0;
		auto iter=Locseg_Node_Dlist_Head(chain->list);
		while(iter!=iter_seed) {
			iter=iter->next;
			seed_idx++;
		}
		_chain.seed_idx=seed_idx;
	}

	void collect_lost_seeds(const std::vector<Seed>& seeds) {
		_lost_seeds.clear();
		for(auto& seed: seeds) {
			if(!seed.nid)
				continue;
			bool hit{false};
			for(auto& chain: _fresh_chains) {
				if(chain.seed.nid==seed.nid)
					hit=true;
			}
			if(!hit)
				_lost_seeds.push_back(seed);
		}
	}
	void build_delta2(gapr::delta_add_patch_& delta);

	Hits find_hits(const std::array<double, 3>& pos, bool skip_echains) {
		std::size_t j=0;
		if(skip_echains)
			j=_existing_chains.size();
		Hits hits{};
		for( ; j<_existing_chains.size()+_fresh_chains.size(); j++) {
			auto chain2=get_chain(j).chain;
			std::size_t idx2=0;
			auto iter=Locseg_Node_Dlist_Head(chain2->list);
			while(iter) {
				auto seg=iter->data->locseg;
				if(Local_Neuroseg_Hit_Test(seg, pos[0], pos[1], pos[2]))
					hits.add(j, idx2, dist(pos, seg));
				iter=iter->next;
				idx2++;
			}
		}
		return hits;
	}
	Chain& get_chain(std::size_t id) {
		if(id<_existing_chains.size())
			return _existing_chains[id];
		return _fresh_chains[id-_existing_chains.size()];
	}
	static double dist(const std::array<double, 3>& pos1, Local_Neuroseg* seg2) {
		std::array<double, 3> pos2;
		Local_Neuroseg_Center(seg2, &pos2[0]);
		auto diff=pos1-pos2;
		return len(diff);
	}
	static double dist(Local_Neuroseg* seg1, Local_Neuroseg* seg2) {
		std::array<double, 3> pos1, pos2;
		Local_Neuroseg_Center(seg1, &pos1[0]);
		Local_Neuroseg_Center(seg2, &pos2[0]);
		auto diff=pos1-pos2;
		return len(diff);
	}
	Hits find_hits(std::size_t id, std::size_t idx, bool skip_echains) {
		std::size_t j=0;
		if(skip_echains)
			j=_existing_chains.size();
		Local_Neuroseg* tgt_seg;
		{
			auto& chain=get_chain(id);
			tgt_seg=Locseg_Chain_Peek_Seg_At(chain.chain.get(), idx);
		}

		Hits hits{};
		for( ; j<_existing_chains.size()+_fresh_chains.size(); j++) {
			if(j==id)
				continue;
			auto chain2=get_chain(j).chain;
			std::size_t idx2=0;
			auto iter=Locseg_Node_Dlist_Head(chain2->list);
			while(iter) {
				auto seg=iter->data->locseg;
				if(Local_Neuroseg_Hit_Test2(tgt_seg, seg))
					hits.add(j, idx2, dist(tgt_seg, seg));
				iter=iter->next;
				idx2++;
			}
		}
		return hits;
	}

	std::string to_debug(std::size_t id) {
		auto& chain=_fresh_chains[id];
		auto len=Locseg_Chain_Length(chain.chain.get());
		std::ostringstream oss;
		oss<<'[';

		auto iter=Locseg_Node_Dlist_Head(chain.chain->list);
		for(int i=0; i<len; i++) {
			oss<<iter->data->tr->fs.scores[_trace_workspace->tscore_option]<<',';
			iter=iter->next;
		}
		oss<<"], [";
		iter=Locseg_Node_Dlist_Head(chain.chain->list);
		for(int i=0; i<len; i++) {
			std::array<double, 3> pos;
			Local_Neuroseg_Center(iter->data->locseg, &pos[0]);
			int x=pos[0]+0.5;
			int y=pos[1]+0.5;
			int z=pos[2]*_z_scale+0.5;
			auto v=Stack_Pixel(&_stack, x, y, z, 0);
			oss<<v<<',';
			iter=iter->next;
		}
		oss<<']';
		return oss.str();
	}
	std::vector<Geo3d_Circle> to_circles(std::size_t id) {
		auto& chain=_fresh_chains[id];
		auto len=Locseg_Chain_Length(chain.chain.get());
		std::vector<Geo3d_Circle> circles;
		circles.reserve(len);
		auto iter=Locseg_Node_Dlist_Head(chain.chain->list);
		std::ostringstream oss;
		for(int i=0; i<len; i++) {
			double t=0.5;
			if(i==0) {
				if(!check_boundary(iter->data->locseg) && chain.left_state==Chain::END)
					t=0.0;
			}
			if(i==len-1) {
				if(!check_boundary(iter->data->locseg) && chain.right_state==Chain::END)
					t=1.0;
			}
			Geo3d_Circle c;
			Local_Neuroseg_To_Circle_T(iter->data->locseg, t, NEUROSEG_CIRCLE_RX, &c);
			circles.push_back(c);
			iter=iter->next;
		}
		if(_cuberef.preproc()) {
			for(auto& c: circles) {
				unsigned int x=c.center[0]+0.5;
				unsigned int y=c.center[1]+0.5;
				unsigned int z=c.center[2]*_z_scale+0.5;
				auto r=_cuberef.get_pix2(x, y, z);
				c.radius=std::exp(r)-1;
			}
		}
		return circles;
	}
	bool check_val(Chain& chain) {
		return check_val(chain.chain.get());
	}
	bool check_val(Locseg_Chain* chain) {
		if(_cuberef.preproc())
			return true;
		auto len=Locseg_Chain_Length(chain);
		std::vector<double> vals;
		vals.reserve(len);
		auto iter=Locseg_Node_Dlist_Head(chain->list);
		for(int i=0; i<len; i++) {
			std::array<double, 3> pos;
			Local_Neuroseg_Center(iter->data->locseg, &pos[0]);
			int x=pos[0]+0.5;
			int y=pos[1]+0.5;
			int z=pos[2]*_z_scale+0.5;
			auto v=Stack_Pixel(&_stack, x, y, z, 0);
			vals.push_back(v);
			iter=iter->next;
		}
		std::sort(vals.begin(), vals.end());
		if(vals[len*8/10]>=70)
			return true;
		return false;
	}
};

gapr::delta_add_patch_ NeutubeHelper::debug_ds_seeds() {
	gapr::delta_add_patch_ delta;
	for(unsigned int i=0; i<2; i++) {
		std::array<double, 3> pos;
		pos[0]=_cuberef.sizes[0]*(1+i)/3.0;
		pos[1]=_cuberef.sizes[1]*(1+i)/3.0;
		pos[2]=_cuberef.sizes[2]/_z_scale*(1+i)/3.0;
		auto pt=to_node_attr(&pos[0]);
		delta.nodes.emplace_back(pt.data(), i);
		delta.props.emplace_back(i+1, "state=end");
	}
	return delta;
}
gapr::delta_add_patch_ NeutubeHelper::debug_cube_seeds() {
	std::vector<Seed> seeds;
	prepare_stack();
	get_workspace();

	find_seeds_graph(seeds);
	gapr::print("seed num: ", seeds.size());
	find_seeds_image(seeds, 70);
	gapr::print("seed num: ", seeds.size());

	gapr::delta_add_patch_ delta;
	unsigned int i=0;
	for(auto& seed: seeds) {
		auto pos=seed.pos;
		auto pt=to_node_attr(&pos[0]);
		delta.nodes.emplace_back(pt.data(), 0);
		delta.props.emplace_back(i+1, "state=end");
		i++;
	}
	return delta;
}

void gapr::trace::ConnectAlg::impl(Job& job) {
	CubeRef cube_ref;
	if(_detector) {
		auto cube_view=job.cube.view<const void>();
		auto res=_detector->predict(cube_view);
		cube_ref.bind(std::move(res), cube_view.sizes(), _xform, job.offset);
	} else {
		cube_ref.bind(job.cube, _xform, job.offset);
	}
	NeutubeHelper impl{_graph, std::move(cube_ref)};
	switch(1) {
		default:
			job.delta=impl.compute();
			break;
		case 2:
			job.delta=impl.debug_ds_seeds();
			return;
		case 3:
			job.delta=impl.debug_cube_seeds();
			return;
	}
}

gapr::delta_add_patch_ NeutubeHelper::compute() {
	std::vector<Seed> seeds;

	prepare_stack();
	get_workspace();

	cache_model();
	find_seeds_graph(seeds);
	gapr::print("seed num: ", seeds.size());
	fix_stack();
	find_seeds_image(seeds, 70);
	gapr::print("seed num: ", seeds.size());

	process_edges();

	std::vector<ChainPtr> chains;
	std::vector<std::pair<ChainPtr, Seed>> seeds_retry;
	// 1st, only long chains
	for(auto& seed: seeds) {
		NeurosegPtr localseg;
		if(!seed.nid) {
			if(check_mask(seed.pos))
				continue;
			if(check_boundary(seed.pos))
				continue;
			localseg=optimize(seed.pos);
			if(check_boundary(localseg.get()))
				continue;
			if(check_mask(localseg.get()))
				continue;
			if(check_score(localseg.get()))
				continue;
			Local_Neuroseg_Center(localseg.get(), &seed.pos[0]);
		} else {
			localseg=optimize_fixed_pos(seed.pos);
			check_score(localseg.get());
		}

		auto chain=trace(std::move(localseg));
		if(Locseg_Chain_Length(chain.get())<10) {
			seeds_retry.push_back({chain, seed});
			continue;
		}
		if(!seed.nid) {
			if(!check_val(chain.get()))
				continue;
		} else {
			if(!check_val(chain.get()))
				continue;
		}

		add_fresh_chain(chain, seed);
	}

	// 2nd, new short chains
	for(auto& [chain, seed]: seeds_retry) {
		NeurosegPtr localseg;
		if(!seed.nid) {
			localseg=optimize(seed.pos);
			if(check_boundary(localseg.get()))
				continue;
			if(check_mask(localseg.get()))
				continue;
			if(check_score(localseg.get()))
				continue;
			if(!check_val(chain.get()))
				continue;
		} else {
			localseg=optimize_fixed_pos(seed.pos);
			check_score(localseg.get());
			if(!check_val(chain.get()))
				continue;
		}
		if(Locseg_Chain_Length(chain.get())<5)
			continue;
		add_fresh_chain(chain, seed);
	}

	gapr::delta_add_patch_ delta;
	collect_lost_seeds(seeds);
	build_delta2(delta);
	return delta;
}

void NeutubeHelper::build_delta2(gapr::delta_add_patch_& delta) {

	//Seed nid assoc.
	for(auto& _chain: _fresh_chains) {
		auto chain=_chain.chain;
		auto l=Locseg_Chain_Length(chain.get());
		_chain.len=l;
		_chain.links.resize(l);
		if(_chain.seed.nid) {
			unsigned int link_pos{0};
			switch(_chain.seed_state) {
				case -1:
					link_pos=0;
					break;
				case 0:
					link_pos=_chain.seed_idx;
					break;
				case 1:
					link_pos=l-1;
					break;
			}
			_chain.links[link_pos].add(_chain.seed.nid, 3);
		}
	}

	for(auto& seed: _lost_seeds) {
		if(seed.nid) {
			auto& vert=_verts_cache.at(seed.nid);
			auto degree=vert.edges.size();
			gapr::print("lost seed: ", seed.nid.data, ':', degree);
			if(degree>=2)
				continue;
			if(degree==0) {
				auto hits=find_hits(seed.pos, true);
				gapr::print("seed 0: ", hits.r);
				if(hits.r!=1)
					continue;
				auto& chain=_fresh_chains[hits.id-_existing_chains.size()];
				chain.links[hits.idx].hits1=hits;
				auto hits2=find_hits(hits.id, hits.idx, false);
				chain.links[hits.idx].hits2=hits2;
				gapr::print("seed 0: ", hits2.r);
				if(hits2.r!=0)
					continue;
				chain.links[hits.idx].add(seed.nid, 2);
			} else if(degree==1) {
				auto hits=find_hits(seed.pos, true);
				if(hits.r!=1)
					continue;
				auto& chain=_fresh_chains[hits.id-_existing_chains.size()];
				chain.links[hits.idx].hits1=hits;
				auto hits2=find_hits(hits.id, hits.idx, false);
				chain.links[hits.idx].hits2=hits2;
				if(hits2.r!=1)
					continue;
				if(hits2.id>=_existing_chains.size())
					continue;
				auto& echain=_existing_chains[hits2.id];
				auto& edg=_edges_cache.at(echain.eid);
				bool h{false};
				for(std::size_t i=hits2.idx0; i<hits2.idx1; i++) {
					if(edg.nodes[i+echain.start_i]==seed.nid)
						h=true;
				}
				if(!h)
					continue;
				chain.links[hits.idx].add(seed.nid, 2);
			}
		}
	}

	std::vector<int> names;
	names.reserve(_fresh_chains.size());
	for(std::size_t i=0; i<_fresh_chains.size(); i++)
		names.push_back(i+1);

	auto handle_term=[this,&names](std::size_t id, std::size_t segidx) -> Chain::TermState {
		auto hits=find_hits(id+_existing_chains.size(), segidx, false);
		if(segidx==0) {
			_fresh_chains[id].left_hits1=hits;
		} else {
			_fresh_chains[id].right_hits1=hits;
		}
		if(hits.r==0)
			return Chain::END;
		if(hits.r>1)
			return Chain::MULT;
		auto hits2=find_hits(hits.id, hits.idx, false);
		if(segidx==0) {
			_fresh_chains[id].left_hits2=hits2;
		} else {
			_fresh_chains[id].right_hits2=hits2;
		}
		if(hits2.r==0) {
			if(hits.id>=_existing_chains.size()) {
				auto id2=hits.id-_existing_chains.size();
				if(_fresh_chains[id2].links[hits.idx].linked())
					return Chain::ENDR;
			}
		}
		hits2.add(id+_existing_chains.size(), segidx, hits.dist);
		assert(hits2.r!=0);
		if(hits2.r>1)
			return Chain::MULTR;
		if(!hits2.contains(id+_existing_chains.size(), segidx)) {
			gapr::print("wrong: ", hits2.id, ':', hits2.idx0, ':', hits2.idx1);
			gapr::print("wrong: ", id+_existing_chains.size(), ':', segidx);
			//assert(0);
		}
#if 0
		if(hits.contains(hits.id)) {
			//XXX hide id???
		} else {
			//
		}
#endif
		if(hits.id<_existing_chains.size()) {
			auto& echain=_existing_chains[hits.id];
			auto& edg=_edges_cache.at(echain.eid);
			auto nid=edg.nodes[hits.idx+echain.start_i];
			auto& chain=_fresh_chains[id];
			chain.links[segidx].add(nid, 1);
		} else {
			auto id2=hits.id-_existing_chains.size();
			auto name1=names[id];
			auto name2=names[id2];
			if(name1==name2)
				return Chain::LOOP;
			for(auto& n: names) {
				if(n==name2)
					n=name1;
			}
			auto& chain=_fresh_chains[id];
			auto& chain2=_fresh_chains[id2];
			//gapr::print("link1: ", id, '/', segidx, ": ", id2, '/', hits.idx);
			chain.links[segidx].link(id2, hits.idx);
			//gapr::print("link2: ", id2, '/', hits.idx, ": ", id, '/', segidx);
			chain2.links[hits.idx].link(id, segidx);
		}
		return Chain::LINK;
	};

	for(std::size_t i=0; i<_fresh_chains.size(); i++) {
		auto& chain=_fresh_chains[i];
		if(chain.links[0].adj_state<1)
			chain.left_state=handle_term(i, 0);
		else
			chain.left_state=Chain::LINK;
		if(chain.links[chain.len-1].adj_state<1)
			chain.right_state=handle_term(i, chain.len-1);
		else
			chain.right_state=Chain::LINK;
	}

	//fix links
	for(std::size_t i=0; i<_fresh_chains.size(); i++) {
		auto& chain=_fresh_chains[i];
		for(std::size_t j=0; j<chain.links.size(); j++) {
			unsigned int v=0;
			if(j>0) {
				if(chain.links[j-1].nid_state>v)
					v=chain.links[j-1].nid_state;
			}
			if(j+1<chain.links.size()) {
				if(chain.links[j+1].nid_state>v)
					v=chain.links[j+1].nid_state;
			}
			if(v>chain.links[j].nid_state)
				chain.links[j].nid_state=0;
		}
	}

	std::vector<std::size_t> todo{};
	todo.reserve(2*_fresh_chains.size());
	for(std::size_t i=_fresh_chains.size(); i-->0;)
		todo.push_back(i);

	while(!todo.empty()) {
		auto i=todo.back();
		todo.pop_back();
		if(!names[i])
			continue;
		names[i]=0;
		auto& chain=_fresh_chains[i];
		std::size_t len=Locseg_Chain_Length(chain.chain.get());
		if(len<1)
			continue;
		bool orphan{true};
		{
			if(chain.left_state==Chain::LINK)//!=Chain::END)
				orphan=false;
			if(chain.right_state==Chain::LINK)//!=Chain::END)
				orphan=false;
			for(auto& l: chain.links) {
				if(l.nid_state>0)
					orphan=false;
				if(l.adj_state>0)
					orphan=false;
			}
			if(orphan && len<32) {
				continue;
			}
			if(orphan && Locseg_Chain_Geolen(chain.chain.get())<32/_cuberef.xform.resolution[2]/_z_scale)
				continue;
			if(orphan && !check_val(chain))
				continue;
		}
		print_chain(chain.chain);
		auto circles=to_circles(i);
		print_circles(&circles[0], circles.size());

		gapr::node_id root_par{};
		std::size_t root_idx;
		for(std::size_t j=0; j<chain.links.size(); j++) {
			auto& link=chain.links[j];
			if(link.adj_state==1) {
				if(!names[link.adj_id]) {
					assert(!root_par);
					root_idx=j;
					auto& link2=_fresh_chains[link.adj_id].links[link.adj_idx];
					root_par=link2.tmpid;
					assert(root_par);
				}
			}
		}
		if(!root_par)
			root_idx=0;
		auto par=root_par;
		for(std::size_t j=root_idx+1; j-->0;) {
			auto& link=chain.links[j];
			auto pt=to_node_attr(circles[j]);
			delta.nodes.emplace_back(pt.data(), par.data);
			link.tmpid=par=gapr::node_id(delta.nodes.size());

			if(link.nid_state>0)
				delta.links.emplace_back(par.data, gapr::link_id{link.nid, {}}.data());
			if(link.adj_state==1)
				todo.push_back(link.adj_id);
		}
		par=chain.links[root_idx].tmpid;
		for(std::size_t j=root_idx+1; j<chain.links.size(); j++) {
			auto& link=chain.links[j];
			auto pt=to_node_attr(circles[j]);
			delta.nodes.emplace_back(pt.data(), par.data);
			link.tmpid=par=gapr::node_id(delta.nodes.size());

			if(link.nid_state>0)
				delta.links.emplace_back(par.data, gapr::link_id{link.nid, {}}.data());
			if(link.adj_state==1)
				todo.push_back(link.adj_id);
		}

		static int debug_iter=0;
		auto dump_hits=[](std::ostream& oss, const Hits& hits) ->bool {
			if(hits.r<=0)
				return false;
			oss<<"hits="<<hits.r;
			for(auto& i: hits.items) {
				oss<<','<<i.i<<':'<<i.j<<':'<<i.d;
			}
			oss<<"; ";
			return true;
		};
		for(unsigned int j=0; j<chain.links.size(); j++) {
			auto& link=chain.links[j];
			std::ostringstream oss;
			oss<<"debug.link."<<debug_iter<<'=';
			bool has_data{false};
			if(link.nid_state>0) {
				oss<<"nid_state: "<<link.nid_state<<','<<link.nid.data<<"; ";
				has_data=true;
			}
			if(link.adj_state>0) {
				oss<<"adj_state: "<<link.adj_state<<','<<link.adj_id<<':'<<link.adj_idx<<"; ";
				has_data=true;
			}
			if(dump_hits(oss, link.hits1))
				has_data=true;
			if(dump_hits(oss, link.hits2))
				has_data=true;
			if(has_data && false)
				delta.props.emplace_back(link.tmpid.data, oss.str());
		}
		for(auto [phits1, phits2, st, idx]: {std::tuple{&chain.left_hits1, &chain.left_hits2, chain.left_state, std::size_t{0}}, std::tuple{&chain.right_hits1, &chain.right_hits2, chain.right_state, chain.links.size()-1}}) {
			std::ostringstream oss;
			oss<<"debug.term."<<debug_iter<<'='<<st<<"; hits1: ";
			dump_hits(oss, *phits1);
			oss<<" hits2: ";
			dump_hits(oss, *phits2);
			if(false)
				delta.props.emplace_back(chain.links[idx].tmpid.data, oss.str());
		}
		++debug_iter;

		do {
			bool traced{true};
			if(chain.left_state==Chain::LINK) {
				if(chain.links[0].nid)
					break;
				traced=false;
			}
			auto iter=Locseg_Node_Dlist_Head(chain.chain->list);
			if(check_boundary(iter->data->locseg) && chain.left_state==Chain::END)
				traced=false;
			std::ostringstream oss;
			oss<<(traced?".traced":".tracedbg");
			oss<<"=patch"<<i<<"["<<circles.size()<<"]["<<chain.left_state<<"]";
			if(!traced && 1)
				break;
			delta.props.emplace_back(chain.links.front().tmpid.data, oss.str());
		} while(false);
		if(0) {
			auto dbg=to_debug(i);
			delta.props.emplace_back(chain.links[1].tmpid.data, ".xxxdebug="+dbg);
		}
		do {
			bool traced{true};
			if(chain.right_state==Chain::LINK) {
				if(chain.links[chain.links.size()-1].nid)
					break;
				traced=false;
			}
			auto iter=Locseg_Node_Dlist_Tail(chain.chain->list);
			if(check_boundary(iter->data->locseg) && chain.right_state==Chain::END)
				traced=false;
			std::ostringstream oss;
			oss<<(traced?".traced":".tracedbg");
			oss<<"=patch"<<i<<"["<<circles.size()<<"]["<<chain.right_state<<"]";
			if(!traced && 1)
				break;
			delta.props.emplace_back(chain.links.back().tmpid.data, oss.str());
		} while(false);
	}
	for(auto& seed: _lost_seeds) {
		if(!seed.nid)
			continue;
		if(check_boundary(seed.pos))
			continue;
		bool hit{false};
		for(auto l: delta.links) {
			if(l.second[0]==seed.nid.data)
				hit=true;
			if(l.second[1]==seed.nid.data)
				hit=true;
		}
		if(!hit) {
			delta.nodes.emplace_back(gapr::node_attr{}.data(), 0);
			auto tmpid=gapr::node_id(delta.nodes.size());
			std::ostringstream oss;
			oss<<".traced=notfound";
			delta.props.emplace_back(tmpid.data, oss.str());
			delta.links.emplace_back(tmpid.data, gapr::link_id{seed.nid, {}}.data());
		}
	}
}

