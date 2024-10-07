#include "f1s.hh"
#include "gapr/swc-helper.hh"

#include <cmath>
#include <cstddef>
#include <unordered_map>

// TODO detect subtle mismatches
constexpr static double max_tol=100;
constexpr static double max_dist=1.01;

namespace {

constexpr static unsigned int invalid_idx=0xffff'ffffu;

struct AuxData {
	unsigned int first_c{invalid_idx}, next_c{invalid_idx};
	double sum_c{0.0};
	double len_p{0.0};
};

template<bool T>
struct Index {
	static constexpr bool Tgt=T;
	unsigned int val;
	Index(unsigned int v): val{v} { }
	Index(std::size_t v): val{v==SIZE_MAX?0:static_cast<unsigned int>(v)} { }
	explicit operator bool() const noexcept { return val!=invalid_idx; }
	void reset() noexcept { val=invalid_idx; }
};
template<bool T>
bool operator==(Index<T> a, Index<T> b) noexcept {
	return a.val==b.val;
}
struct Grid {
	Index<false> i;
	Index<true> j;
	Grid(unsigned int i, unsigned int j): i{i}, j{j} { }
	Grid(Index<false> i, Index<true> j): i{i}, j{j} { }
};
bool operator==(Grid a, Grid b) noexcept {
	return a.i.val==b.i.val && a.j.val==b.j.val;
}

struct Score {
	std::array<double, 6> mm{0.0, };
	double dist{NAN};
	double d1;
	double d2;
	unsigned short stg{0};
	Score() { }
	Score(std::nullptr_t): stg{999} { }
};

struct Candidate {
	Index<false> i;
	Index<true> j;
	double v;
};

}

template<>
struct std::hash<Grid> {
	std::size_t operator()(Grid g) const noexcept {
		return (std::size_t{g.i.val}*19)^g.j.val;
	}
};

static std::shared_ptr<AuxData> gen_aux(const std::vector<gapr::swc_node>& nodes) {
	std::shared_ptr<AuxData> aux_{new AuxData[nodes.size()], std::default_delete<AuxData[]>{}};
	auto aux=aux_.get();
	for(std::size_t i=nodes.size(); i-->0; ) {
		if(auto pi=nodes[i].par_idx; pi!=gapr::swc_node::par_idx_null) {
			aux[i].next_c=aux[pi].first_c;
			aux[pi].first_c=i;
			auto dd=nodes[i].pos-nodes[pi].pos;
			auto d=aux[i].len_p=dd.mag();
			aux[pi].sum_c+=d+aux[i].sum_c;
		}
	}
	// XXX
	//
	return aux_;
}

namespace {

struct Aligner {
	const gapr::swc_node* ref;
	const AuxData* ref_aux;
	unsigned int ref_n;
	const gapr::swc_node* tgt;
	const AuxData* tgt_aux;
	unsigned int tgt_n;
	std::unordered_map<Grid, Score> scores2;
	std::vector<Grid> stack;

	template<bool T>
	void set_nodes(const gapr::swc_node* p, std::size_t n, const AuxData* a) noexcept {
		assert(n<=0xffff'ffff);
		(T?tgt:ref)=p;
		(T?tgt_n:ref_n)=n;
		(T?tgt_aux:ref_aux)=a;
	}

	template<bool T>
	const gapr::swc_node& get_node(Index<T> i) const noexcept {
		return i.Tgt?tgt[i.val]:ref[i.val];
	}
	template<bool T>
	Index<T> get_par(Index<T> i) const noexcept {
		return Index<i.Tgt>{(i.Tgt?tgt:ref)[i.val].par_idx};
	}
	template<bool T>
	Index<T> first_c(Index<T> i) const noexcept {
		return Index<i.Tgt>{(i.Tgt?tgt_aux:ref_aux)[i.val].first_c};
	}
	template<bool T>
	Index<T> next_c(Index<T> i) const noexcept {
		return Index<i.Tgt>{(i.Tgt?tgt_aux:ref_aux)[i.val].next_c};
	}
	template<bool T>
	double sum_c(Index<T> i) const noexcept {
		return (i.Tgt?tgt_aux:ref_aux)[i.val].sum_c;
	}
	template<bool T>
	double len_p(Index<T> i) const noexcept {
		return (i.Tgt?tgt_aux:ref_aux)[i.val].len_p;
	}
	double dist(Grid g, Score& s) const {
		if(!std::isnan(s.dist))
			return s.dist;
		auto& ni=get_node(g.i);
		auto& nj=get_node(g.j);
		auto dd=ni.pos-nj.pos;
		s.dist=dd.mag();
		if(s.dist<0.1) {
			s.d1=s.dist;
			s.d2=s.dist;
			return s.dist;
		}
		if(auto l=len_p(g.i); l>0.1) {
			Index<false> ip{ni.par_idx};
			assert(ip.val!=ni.par_idx_null);
			auto& nip=get_node(ip);
			auto diip=(nip.pos-ni.pos)/l;
			auto dij=nj.pos-ni.pos;
			auto ll=dot(dij, diip);
			if(ll<-0.01 || ll>l+0.01)
				s.d1=INFINITY;
			else
				s.d1=(dij-diip*ll).mag();
		}
		if(auto l=len_p(g.j); l>0.1) {
			Index<true> jp{nj.par_idx};
			assert(jp.val!=nj.par_idx_null);
			auto& njp=get_node(jp);
			auto diip=(njp.pos-nj.pos)/l;
			auto dij=ni.pos-nj.pos;
			auto ll=dot(dij, diip);
			if(ll<-0.01 || ll>l+0.01)
				s.d2=INFINITY;
			else
				s.d2=(dij-diip*ll).mag();
		}

		//fprintf(stderr, "dist: %ld %ld %lf %lf %lf\n", ni.id, nj.id, s.dist, s.d1, s.d2);
		return std::min(std::min(s.dist, s.d1), s.d2);
	}
	void sched(Grid g) {
		auto [it,ins]=scores2.emplace(g, Score{});
		if(!ins && it->second.stg>10)
			it->second.stg=0;
		stack.emplace_back(g);
		//fprintf(stderr, "sched: %ld %ld %s\n", get_node(g.i).id, get_node(g.j).id, ins?"*":"");
	}
	const Score& get_score(Grid g) const {
		auto& s=scores2.at(g);
		if(s.stg!=2) {
			fprintf(stderr, "!!!!! %u %ld %u %ld\n", g.i.val, get_node(g.i).id, g.j.val, get_node(g.j).id);
		}
		return s;
	}

	double calc() {
		sched(Grid{0, 0});
		std::vector<Candidate> cands;
		do {
			auto g=stack.back();
			auto& s=scores2.at(g);
			auto id1=get_node(g.i).id;
			auto id2=get_node(g.j).id;
			switch(s.stg) {
			case 1:
				break;
			case 0:
				if(auto d=dist(g, s); d<=max_tol) {
					//fprintf(stderr, "0000: %ld %ld\n", ref[g.i.val].id, ref[g.j.val].id);
					// dist: in scores
					s.stg=1;
					for(auto i=first_c(g.i); i; i=next_c(i)) {
						for(auto j=first_c(g.j); j; j=next_c(j)) {
							sched(Grid{i, j});
							//fprintf(stderr, "<<<<: %ld %ld\n", ref[i.val].id, ref[j.val].id);
						}
					}
					for(auto i=first_c(g.i); i; i=next_c(i))
						sched(Grid{i, g.j});
					for(auto j=first_c(g.j); j; j=next_c(j))
						sched(Grid{g.i, j});
					continue;
				}
				s.stg=2;
				for(unsigned int i=0; i<6; ++i)
					s.mm[i]=-INFINITY;
				[[fallthrough]];
			case 2:
				stack.pop_back();
				continue;
			default:
				assert(0);
				break;
			}
			assert(s.stg==1);
			s.stg=2;
			//fprintf(stderr, "calc: %ld %ld\n", get_node(g.i).id, get_node(g.j).id);

			s.mm[0]=0.0;
			cands.clear();
			unsigned int cands_maxi=0;
			double cands_max=0.0;
			//fprintf(stderr, "2222: %ld %ld %u\n", ref[g.i.val].id, ref[g.j.val].id, g.i.val);
			//fprintf(stderr, "cccc: %ld\n", first_c(Index<false>{0u}).val);
			for(auto i=first_c(g.i); i; i=next_c(i)) {
				for(auto j=first_c(g.j); j; j=next_c(j)) {
					//fprintf(stderr, "----: %ld %ld\n", ref[i.val].id, ref[j.val].id);
					auto v=get_score(Grid{i, j}).mm[5];
					if(v>0.0) {
						cands.push_back(Candidate{i, j, v});
						if(v>cands_max) {
							cands_max=v;
							cands_maxi=cands.size();
						}
					}
				}
			}
			while(cands_maxi) {
				//fprintf(stderr, "assdf %p %p %u\n", &s, ref_aux.get(), ref_aux[0].first_c);
				auto [ii, jj, vv]=cands[cands_maxi-1];
				//fprintf(stderr, "assdf1 %p %p %u\n", &s, ref_aux.get(), ref_aux[0].first_c);
				cands[cands_maxi-1]=cands.back();
				//fprintf(stderr, "assdf2 %p %p %u\n", &s, ref_aux.get(), ref_aux[0].first_c);
				cands.pop_back();
				//fprintf(stderr, "assdf3 %p %p %u\n", &s, ref_aux.get(), ref_aux[0].first_c);
				s.mm[0]+=vv;
				//fprintf(stderr, "assdf4 %p %p %u\n", &s, ref_aux.get(), ref_aux[0].first_c);
				cands_maxi=0;
				cands_max=0.0;
				for(std::size_t k=0; k<cands.size(); ++k) {
					auto& [i, j, v]=cands[k];
					if(!i)
						continue;
					if(i==ii) {
						i.reset();
						continue;
					}
					if(!j)
						continue;
					if(j==jj) {
						j.reset();
						continue;
					}
					if(v>cands_max) {
						cands_max=v;
						cands_maxi=k+1;
					}
				}
			}

			// XXX
			s.mm[1]=s.mm[0];
			for(auto j=first_c(g.j); j; j=next_c(j)) {
				auto v=get_score(Grid{g.i, j}).mm[3];
				if(v>s.mm[1])
					s.mm[1]=v;
			}
			s.mm[2]=s.mm[0];
			for(auto i=first_c(g.i); i; i=next_c(i)) {
				auto v=get_score(Grid{i, g.j}).mm[4];
				//if(id1==1170099 && id2==1170096)
					//fprintf(stderr, "---- %lf %lf %u %u\n", s.mm[0], v, i.val, g.j.val);
				if(v>s.mm[2])
					s.mm[2]=v;
			}

			if(auto gjp=get_par(g.j); gjp) {
				//fprintf(stderr, "%u %u\n", g.i.val, gjp.val);
				auto [it, ins]=scores2.emplace(Grid{g.i, gjp}, Score{nullptr});
				dist(Grid{g.i, gjp}, it->second);

				auto d1=scores2.at(Grid{g.i, gjp}).d1;
				s.mm[3]=s.mm[2]+(d1<max_dist?len_p(g.j):0.0);
				auto vv=s.mm[1]+(d1<max_dist?len_p(g.j):0.0);
				if(vv>s.mm[3])
					s.mm[3]=vv;
			}
			if(auto gip=get_par(g.i); gip) {
				auto [it, ins]=scores2.emplace(Grid{gip, g.j}, Score{nullptr});
				dist(Grid{gip, g.j}, it->second);

				auto d2=scores2.at(Grid{gip, g.j}).d2;
				s.mm[4]=s.mm[1]+(d2<max_dist?len_p(g.i):0.0);
				auto vv=s.mm[2]+(d2<max_dist?len_p(g.i):0.0);
				if(vv>s.mm[4])
					s.mm[4]=vv;
				//fprintf(stderr, "%lf %lf\n", d2, len_p(g.i));
			}
			// XXX
			s.mm[5]=std::max(s.mm[3]+len_p(g.i), s.mm[4]+len_p(g.j));

#if 0
			//if(id1<=1170100 && id1>=1170096 && id2<=1170100 && id2>=1170096) {
			if(id1==id2) {
			//if(==get_node(g.j).id) {
			auto t=sum_c(g.i)+sum_c(g.j);
			fprintf(stderr, "calc: %ld %ld (%zu)\n%lf %lf %lf %lf %lf %lf\n", get_node(g.i).id, get_node(g.j).id, scores2.size(),
					s.mm[0],
					s.mm[1],
					s.mm[2],
					s.mm[3],
					s.mm[4],
					s.mm[5]);
			fprintf(stderr, "len: %lf %lf %lf %s\n", t, len_p(g.i), len_p(g.j), std::abs(t-s.mm[0])>0.0001?"!!":"");
			}
#endif
			stack.pop_back();
		} while(!stack.empty());

		auto f1=get_score(Grid{0, 0}).mm[0]/(ref_aux[0].sum_c+tgt_aux[0].sum_c);
		return f1;
	}
};

}

static void unit_test() {
	struct SimpNode {
		int id;
		double x,y,z;
		int par;
	};
	auto to_nodes=[](const auto& v) ->std::vector<gapr::swc_node> {
		std::unordered_map<int, unsigned int> id2idx;
		std::vector<gapr::swc_node> r;
		for(std::size_t i=0; i<v.size(); ++i) {
			id2idx.emplace(v[i].id, r.size());
			auto& n=r.emplace_back();
			n.id=v[i].id;
			n.pos={v[i].x, v[i].y, v[i].z};
			if(v[i].par==-1) {
				n.par_idx=n.par_idx_null;
			} else {
				n.par_idx=id2idx.at(v[i].par);
			}
		}
		return r;
	};
	std::vector<SimpNode> v1{
		{1, 0, 0, 0, -1},
		{2, 0, 0, 3, 1},
		{3, 0, 0, 4, 2},
		{4, 0, 0, 6, 3},
		{5, 0, 0, 9, 4},
		{6, 0, 0, 10, 5},
		{7, 0, 0, 12, 6},
	};
	auto v2=v1;
	v2.back().z+=1;
	auto v3=v1;
	for(auto& n: v3)
		n.z+=1;
	auto v4=v3;
	v4.push_back({8, 0, 1, 8, 4});
	auto v5=v3;
	for(auto& n: v3) {
		if(n.id>4) {
			auto& nn=v5.emplace_back(n);
			nn.id+=10;
			if(nn.par>4)
				nn.par+=10;
		}
	}

	auto check=[to_nodes](const char* tag, const auto& a, const auto& b, double r) {
		auto aa=to_nodes(a);
		auto bb=to_nodes(b);
		auto aaa=gen_aux(aa);
		auto bbb=gen_aux(bb);
		auto check1=[&aa,&aaa,&bb,&bbb](auto alt, double r) {
			Aligner a;
			a.set_nodes<alt.value>(&aa[0], aa.size(), aaa.get());
			a.set_nodes<!alt.value>(&bb[0], bb.size(), bbb.get());
			auto f1=a.calc();
			auto t=a.ref_aux[0].sum_c+a.tgt_aux[0].sum_c;
			fprintf(stderr, "%lf+%lf=%lf\n",
					a.ref_aux[0].sum_c,
					a.tgt_aux[0].sum_c,
					t);
			fprintf(stderr, "%lf ? %lf\n", t-f1*t, r);
		};
		fprintf(stderr, "check: %s\n", tag);
		check1(std::true_type{}, r);
		check1(std::false_type{}, r);
	};
	check("v1 v2", v1, v2, 0.0);
	check("v1 v3", v1, v3, 0.0);
	check("v1 v4", v1, v4, 0.0);
	check("v3 v4", v3, v4, 0.0);
	check("v1 v5", v1, v5, 0.0);
	check("v3 v5", v3, v5, 0.0);
}

std::array<std::shared_ptr<void>, 2> f1score_impl(
		const std::vector<gapr::swc_node>& ref,
		const std::shared_ptr<void>& ref_aux_,
		const std::vector<gapr::swc_node>& tgt,
		const std::shared_ptr<void>& tgt_aux_) {
	//unit_test();
	auto ref_aux=std::static_pointer_cast<AuxData>(ref_aux_);
	if(!ref_aux)
		ref_aux=gen_aux(ref);
	auto tgt_aux=std::static_pointer_cast<AuxData>(tgt_aux_);
	if(!tgt_aux)
		tgt_aux=gen_aux(tgt);

	Aligner a;
	a.set_nodes<false>(&ref[0], ref.size(), ref_aux.get());
	a.set_nodes<true>(&tgt[0], tgt.size(), tgt_aux.get());
	auto f1=a.calc();

	fprintf(stdout, "%lf\n", f1);
	return {std::static_pointer_cast<void>(ref_aux),
		std::static_pointer_cast<void>(tgt_aux)};
}

