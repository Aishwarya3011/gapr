#include "map.hh"
#include "gapr/swc-helper.hh"

#include <list>

namespace {
struct Point {
	gapr::vec3<double> p;
	std::size_t i;
	double area;
	std::list<Point> misc;
	Point(const gapr::vec3<double>& p, std::size_t i): p{p}, i{i}, area{INFINITY} { }
};
struct AuxData {
	union {
		std::size_t pidx;
		std::size_t newidx;
	};
	unsigned int deg{0};
	bool del{false};
	AuxData(): pidx{SIZE_MAX} { }
};
}

double areaOfTriangle(const Point& p0, const Point& p1, const Point& p2, double maxd, double maxh) {
	auto d=p2.p-p0.p;
	auto d2=d.mag2();
	if(d2>maxd*maxd)
		return INFINITY;
	auto calc_a2=[&p0,&p2,&d](auto& m) ->double {
		auto p01=p0.p-m.p;
		auto p21=p2.p-m.p;
		if(dot(p01, d)>0)
			return INFINITY;
		if(dot(p21, d)<0)
			return INFINITY;
		return cross(p01, p21).mag2();
	};
	auto a2=calc_a2(p1);
	auto a2max=maxh*maxh*d2;
	if(a2>a2max)
		return INFINITY;
	for(auto& m: p1.misc) {
		assert(m.misc.empty());
		if(calc_a2(m)>a2max)
			return INFINITY;
	}
	return sqrt(a2)/2;
}

std::vector<Point> reducePointsVisvalingam(std::vector<Point>&& pts, double maxd, double maxh) {
	std::list<Point> temppts;
	temppts.emplace_back(pts[0]);
	for(size_t i=2; i<pts.size(); ++i) {
		auto& p=temppts.emplace_back(pts[i-1]);
		p.area=areaOfTriangle(pts[i-2], pts[i-1], pts[i], maxd, maxh);
	}
	temppts.emplace_back(pts[pts.size()-1]);

	while(temppts.size()>2) {
		double minarea=INFINITY;
		std::list<Point>::iterator miniter;
		auto i=temppts.begin();
		i++;
		auto j=i;
		j++;
		for(; j!=temppts.end(); i++, j++) {
			if(i->area<minarea) {
				minarea=i->area;
				miniter=i;
			}
		}
		if(std::isinf(minarea))
			break;
		auto prev=miniter;
		prev--;
		auto next=miniter;
		next++;
		auto misc=std::move(miniter->misc);
		misc.splice(misc.cend(), temppts, miniter);
		if(prev!=temppts.begin()) {
			for(auto& m: misc)
				prev->misc.emplace_back(m);
			auto pprev=prev;
			pprev--;
			prev->area=areaOfTriangle(*pprev, *prev, *next, maxd, maxh);
		}
		auto nnext=next;
		nnext++;
		if(nnext!=temppts.end()) {
			next->misc.splice(next->misc.cend(), std::move(misc));
			next->area=areaOfTriangle(*prev, *next, *nnext, maxd, maxh);
		}
	}
	pts.clear();
	for(auto& p: temppts)
		pts.push_back(p);
	return std::move(pts);
}

std::vector<gapr::swc_node> decimate_impl(std::vector<gapr::swc_node>&& jnodes, double maxd, double maxh) {
	std::vector<AuxData> aux;
	std::vector<Point> cur_edg;
	aux.resize(jnodes.size(), AuxData{});
	for(std::size_t i=0; i<jnodes.size(); ++i) {
		auto pi=jnodes[i].par_idx;
		if(pi==gapr::swc_node::par_idx_null) {
			aux[i].deg+=1'000'000;
			continue;
		}
		++aux[pi].deg;
		++aux[i].deg;
	}
	for(std::size_t i=jnodes.size(); i-->0; ) {
		auto deg=aux[i].deg;
		std::size_t ii;
		switch(deg) {
		case 0:
			assert(0);
			break;
		case 2:
			break;
		default:
			if(deg>=1'000'000)
				break;
			cur_edg.clear();
			cur_edg.emplace_back(jnodes[i].pos, i);
			ii=i;
			do {
				ii=jnodes[ii].par_idx;
				cur_edg.emplace_back(jnodes[ii].pos, ii);
			} while(aux[ii].deg==2);
			cur_edg=reducePointsVisvalingam(std::move(cur_edg), maxd, maxh);
			for(std::size_t k=0; k+1<cur_edg.size(); ++k) {
				auto ik=cur_edg[k].i;
				auto ikp=cur_edg[k+1].i;
				aux[ik].pidx=ikp;
				ik=jnodes[ik].par_idx;
				while(ik!=ikp) {
					aux[ik].del=true;
					ik=jnodes[ik].par_idx;
				}
			}
			break;
		}
	}
	std::vector<gapr::swc_node> nodes;
	for(std::size_t i=0; i<jnodes.size(); ++i) {
		if(aux[i].del)
			continue;
		auto& n=nodes.emplace_back(jnodes[i]);
		if(auto pi=aux[i].pidx; pi!=SIZE_MAX) {
			assert(n.par_idx!=n.par_idx_null);
			n.par_idx=aux[pi].newidx;
		}
		aux[i].newidx=nodes.size()-1;
	}
	return nodes;
}

// XXX do not remove nodes at sharp turn
