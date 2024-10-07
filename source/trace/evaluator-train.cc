#include "evaluator.hh"

#include <random>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>
#include <iostream>
#include <cassert>

#include <gsl/gsl_bspline.h>

#include "utils.hh"

#include "gapr/detail/nrrd-output.hh"


using Vec=std::array<double, 3>;
struct Line {
	Vec dir;
	Vec shift;
	double r;
	double v;
};


static double len(const Vec& v) {
	double s=0.0;
	for(unsigned int i=0; i<3; i++) {
		auto vv=v[i];
		s+=vv*vv;
	}
	return std::sqrt(s);
}
static double dot(const Vec& v, const Vec& v2) {
	double s=0.0;
	for(unsigned int i=0; i<3; i++)
		s+=v[i]*v2[i];
	return s;
}
static Vec sub(const Vec& v, const Vec& v2) {
	Vec r;
	for(unsigned int i=0; i<3; i++)
		r[i]=v[i]-v2[i];
	return r;
}
[[maybe_unused]] static double dist(const Line& l, const Vec& pt) {
	auto d1=sub(pt, l.shift);
	auto dp=dot(d1, l.dir);
	if(dp<=0)
		return len(d1);
	for(unsigned int i=0; i<3; i++)
		d1[i]=d1[i]-l.dir[i]*dp;
	return len(d1);
}

struct Curve {
	struct bspline_workspace_deleter {
		void operator()(gsl_bspline_workspace* w) const {
			gsl_bspline_free(w);
		}
	};
	using bspline_workspace_ptr=std::unique_ptr<gsl_bspline_workspace, bspline_workspace_deleter>;
	static const constexpr std::size_t nbreak=5;

	bspline_workspace_ptr bw;
	std::array<double, nbreak+2> _rads;
	std::array<double, 5> _rad_breaks;
	std::array<double, 5> _rad_break_dts;
	std::size_t _rad_breaks_n;
	std::array<double, nbreak+2> _vals;
	std::array<double, 5> _val_breaks;
	std::array<double, 5> _val_break_dts;
	std::size_t _val_breaks_n;
	std::array<Vec, nbreak+2> _pts;

	mutable std::size_t istart, iend;
	mutable std::array<double, 4> Bk;
	mutable double _t;
	double t0{-1.0}, t1{1.0};

	Curve(): bw{gsl_bspline_alloc(4, nbreak)} {
		if(GSL_SUCCESS!=gsl_bspline_knots_uniform(t0, t1, bw.get()))
			throw std::runtime_error{"error init knots"};
	}

	void gen(std::mt19937& rng) {
		std::normal_distribution<double> ndist{};
		std::uniform_real_distribution<double> udist{};
		std::poisson_distribution<unsigned int> pdist{1.0};
		for(auto& r: _rads)
			r=std::tanh(ndist(rng))*0.3+0.5;
		for(auto& v: _vals)
			v=std::tanh(ndist(rng))*0.3+0.7;
		auto pt_out=[ndist](std::mt19937& rng) mutable ->Vec {
			do {
				Vec v;
				for(auto& x: v)
					x=ndist(rng);
				auto l=len(v);
				if(l<.2)
					continue;
				for(auto& x: v)
					x=x/l*(8+l);
				return v;
			} while(true);
		};
		auto pt_in=[ndist](std::mt19937& rng) mutable ->Vec {
			do {
				Vec v;
				for(auto& x: v)
					x=ndist(rng)*5;
				auto l=len(v);
				if(l>5)
					continue;
				return v;
			} while(true);
		};
		Vec ptl=pt_out(rng);
		Vec ptm=pt_in(rng);
		Vec ptr;
		do {
			ptr=pt_out(rng);
			auto v1=sub(ptl, ptm);
			auto v2=sub(ptr, ptm);
			if(dot(v1, v2)<.7*len(v1)*len(v2))
				break;
		} while(true);
		_pts[0]=ptl;
		_pts[1]=ptl;
		_pts[nbreak+1]=ptr;
		_pts[nbreak+0]=ptr;
		auto M=(nbreak+1)/2;
		_pts[M]=ptm;
		for(std::size_t i=2; i<M; i++) {
			for(std::size_t j=0; j<3; j++)
				_pts[i][j]=(_pts[1][j]*(M-i)+_pts[M][j]*(i-1))/(M-1)+ndist(rng)*2;
		}
		for(std::size_t i=M+1; i<nbreak; i++) {
			for(std::size_t j=0; j<3; j++)
				_pts[i][j]=(_pts[M][j]*(nbreak-i)+_pts[nbreak][j]*(i-M))/(nbreak-M)+ndist(rng)*2;
		}

		auto gen_breaks=[this,&rng,&udist,&pdist](std::array<double, 5>& brks, std::array<double, 5>& dts, std::size_t& n) {
			n=pdist(rng);
			if(n>brks.size())
				n=brks.size();
			for(std::size_t i=0; i<n; i++)
				brks[i]=udist(rng)*2-1;
			for(std::size_t i=0; i<n; i++) {
				double mdt{0.0};
				auto tt=brks[i];
				move_to(tt);
				auto pos0=pos();
				auto r0=std::min(get_real_r(), 0.5);
				for(double dt=0.0; dt<1.0; dt+=0.005) {
					bool out{false};
					if(tt+dt<t1) {
						move_to(tt+dt);
						if(len(sub(pos(), pos0))>r0)
							out=true;
					}
					if(tt-dt>t0) {
						move_to(tt-dt);
						if(len(sub(pos(), pos0))>r0)
							out=true;
					}
					if(out) {
						mdt=dt;
						break;
					}
				}
				dts[i]=mdt/2;
			}
		};
		gen_breaks(_val_breaks, _val_break_dts, _val_breaks_n);
		gen_breaks(_rad_breaks, _rad_break_dts, _rad_breaks_n);
	}

	void move_to(double t) const {
		auto Bk_view=gsl_vector_view_array(Bk.data(), Bk.size());
		if(GSL_SUCCESS!=gsl_bspline_eval_nonzero(t, &Bk_view.vector, &istart, &iend, bw.get()))
			throw std::runtime_error{"error eval bspline"};
		_t=t;
	}

	double t() const noexcept { return _t; }
	double get_real_r() const {
		double r=0.0;
		for(std::size_t i=0; istart+i<=iend; i++)
			r+=Bk[i]*_rads[istart+i];
		return r;
	}
	double rad() const noexcept {
		auto r=get_real_r();
		for(std::size_t i=0; i<_rad_breaks_n; i++) {
			if(std::abs(_t-_rad_breaks[i])<_rad_break_dts[i]) {
				r/=4;
				break;
			}
		}
		return r;
	}
	double val() const noexcept {
		double v=0.0;
		for(std::size_t i=0; istart+i<=iend; i++)
			v+=Bk[i]*_vals[istart+i];
		for(std::size_t i=0; i<_val_breaks_n; i++) {
			if(std::abs(_t-_val_breaks[i])<_val_break_dts[i]) {
				v/=10;
				break;
			}
		}
		return v;
	}
	Vec pos() const noexcept {
		Vec r{0.0, 0.0, 0.0};
		for(std::size_t i=0; istart+i<=iend; i++)
			for(std::size_t j=0; j<3; j++)
				r[j]+=Bk[i]*_pts[istart+i][j];
		return r;
	}
	void shift(const Vec& d) {
		for(auto& pt: _pts)
			for(unsigned int i=0; i<3; i++)
				pt[i]+=d[i];
	}
};

void gen_samples(std::valarray<float>& input, std::valarray<float>& output, std::valarray<float>& interm, std::size_t N, std::mt19937& rng) {
	const double near_thr=2.5;
	const double near_thr2=3.0;
	const double center_thr=1.0;
	const double center_thr2=1.5;
	const double upsamp_res=0.1;

	input.resize(N*1*16*48*48);
	interm.resize(N*4*12*20*20);
	output.resize(N*1);
	struct Pix {
		float val{0.0};
		float dist{INFINITY};
		float t{NAN};
		signed char ch{-1};
		bool is_fib{true};
		bool from_center1{false};
		bool from_center2{false};
	};
	struct DistMapItem {
		std::array<int, 3> dx;
		double dist;
	};

	std::vector<DistMapItem> dist_map;
	std::vector<std::array<Pix, 3>> upsamp(96*96*96);
	std::vector<std::array<double, 2>> crosses;
	for(int dx=-12; dx<=12; dx++)
		for(int dy=-12; dy<=12; dy++)
			for(int dz=-12; dz<=12; dz++) {
				double r=dx*dx+dy*dy+dz*dz;
				r=std::sqrt(r)*upsamp_res;
				dist_map.push_back(DistMapItem{{dx, dy, dz}, r});
			}
	std::sort(dist_map.begin(), dist_map.end(), [](auto& a, auto& b) {
		return a.dist<b.dist;
	});

	auto traverse_curve=[&dist_map,upsamp_res](const auto& curve_func, double t0, double t1, const auto& point_func) {
		for(double t=t0; t<=t1; t+=0.005) {
			Vec pos;
			double rad;
			rad=curve_func(t, pos);
			auto rad2=rad*rad;
			std::array<int, 3> pos0i;
			for(unsigned int i=0; i<3; i++)
				pos0i[i]=(pos[i]/upsamp_res+.5)+(96/2);
			for(auto& map: dist_map) {
				if(map.dist>rad+upsamp_res)
					break;
				std::array<int, 3> posi;
				bool valid{true};
				for(unsigned int i=0; i<3; i++) {
					posi[i]=pos0i[i]+map.dx[i];
					if(posi[i]<0)
						valid=false;
					if(posi[i]>=96)
						valid=false;
				}
				if(!valid)
					continue;
				double l2{0.0};
				for(unsigned int i=0; i<3; i++) {
					auto d=(posi[i]-(96/2)+.5)*upsamp_res-pos[i];
					l2+=d*d;
				}
				if(l2>rad2)
					continue;
				auto dist=std::sqrt(l2);
				point_func(t, posi, dist);
			}
		}
	};

	auto paint=[traverse_curve,&upsamp](const Curve& fib, std::size_t ch) {
		double val;
		traverse_curve([&fib,&val](double t, Vec& pos) ->double {
			fib.move_to(t);
			pos=fib.pos();
			val=fib.val();
			return fib.rad();
		}, fib.t0, fib.t1,
		[&upsamp,ch,&val](double t, std::array<int, 3> posi, double dist) {
			auto& pix=upsamp[posi[0]+posi[1]*96+posi[2]*(96*96)][ch];
			if(pix.dist>dist) {
				pix.val=val;
				pix.dist=dist;
				pix.t=t;
			}
		});
	};
	auto paint_seg=[traverse_curve,&upsamp](const Curve& fib, int ch) {
		traverse_curve([&fib](double t, Vec& pos) ->double {
			fib.move_to(t);
			pos=fib.pos();
			return 0.5;
		}, fib.t0, fib.t1,
		[&upsamp,ch](double t, std::array<int, 3> posi, double dist) {
			auto& pix=upsamp[posi[0]+posi[1]*96+posi[2]*(96*96)][2];
			if(pix.dist>dist) {
				pix.val=std::max(1-4*dist*dist, 0.0);
				pix.dist=dist;
				pix.t=t;
				pix.ch=ch;
			}
		});
	};

	auto gen_inside=[&rng](Curve& fib, double rad) {
		fib.gen(rng);
		fib.move_to(0);
		auto p0=fib.pos();
		auto l=len(p0);
		if(l>rad) {
			Vec p2;
			std::uniform_real_distribution udist{};
			do {
				for(unsigned int i=0; i<3; i++)
					p2[i]=(udist(rng)*2-1)*rad;
				if(len(p2)<rad)
					break;
			} while(true);
			Vec dp;
			for(unsigned int i=0; i<3; i++)
				dp[i]=p2[i]-p0[i];
			fib.shift(dp);
		}
	};
	auto gen_outside=[&rng](Curve& fib, double rad) {
		do {
			fib.gen(rng);
			fib.move_to(0);
			auto p0=fib.pos();
			auto l=len(p0);
			if(l<0.2)
				continue;
			if(l<=rad) {
				auto l2=rad+l;
				Vec dp;
				auto m=(1.0/l*l2-1);
				for(unsigned int i=0; i<3; i++)
					dp[i]=p0[i]*m;
				fib.shift(dp);
			}
			bool ok{true};
			for(double t=fib.t0; t<=fib.t1; t+=0.005) {
				fib.move_to(t);
				auto pos=fib.pos();
				l=len(pos);
				if(l<=rad) {
					ok=false;
					break;
				}
			}
			if(ok)
				break;
		} while(true);
	};
	auto gen_branch=[gen_inside,&rng](Curve& fib, const Curve& fib0, bool tshape) {
		const double cross_thr{1.0};
		const double cross_pos_thr{2.5};
		gen_inside(fib, tshape?0.1:cross_thr);
		Vec pos;
		std::uniform_real_distribution udist{};
		do {
			fib0.move_to((udist(rng)-.5));
			pos=fib0.pos();
			auto l=len(pos);
			if(l<cross_pos_thr)
				break;
		} while(true);
		fib.shift(pos);
	};
	auto downsamp_img=[&upsamp](float* ptr, std::mt19937& rng) {
		std::normal_distribution<double> ndist;
		for(unsigned int z=0; z<16; z++) {
			auto zz0=z*6;
			for(unsigned int y=0; y<48; y++) {
				auto yy0=y*2;
				for(unsigned int x=0; x<48; x++) {
					auto xx0=x*2;
					double v=0.0;
					for(unsigned int zz=6+zz0; zz-->zz0;) {
						for(unsigned int yy=2+yy0; yy-->yy0;) {
							for(unsigned int xx=2+xx0; xx-->xx0;) {
								auto& pixels=upsamp[xx+yy*96+zz*(96*96)];
								v+=std::max(pixels[0].val, pixels[1].val);
							}
						}
					}
					ptr[x+y*48+z*(48*48)]=v/(6*2*2)+std::tanh(std::abs(ndist(rng)))*0.5+.0;
				}
			}
		}
	};
	auto downsamp_seg=[&upsamp](auto s0, auto add, auto write) {
		for(unsigned int z=0; z<12; z++) {
			auto zz0=(z+2)*6;
			for(unsigned int y=0; y<20; y++) {
				auto yy0=(y+2)*4;
				for(unsigned int x=0; x<20; x++) {
					auto xx0=(x+2)*4;
					auto s=s0;
					for(unsigned int zz=zz0+6; zz-->zz0;)
						for(unsigned int yy=yy0+4; yy-->yy0;)
							for(unsigned int xx=xx0+4; xx-->xx0;) {
								auto& pix=upsamp[xx+yy*96+zz*(96*96)][2];
								add(s, pix);
							}
					for(auto& v: s)
						v/=(6*4*4);
					write(s, {x, y, z});
				}
			}
		}
	};

	auto mark_origin=[&crosses,&upsamp,center_thr](const Curve& fib1, int ch) {
		if(ch<0)
			return;
		bool has_cross=!crosses.empty();
		double mdist{INFINITY};
		double tmin{0.0};
		for(double t=fib1.t0; t<=fib1.t1; t+=0.005) {
			fib1.move_to(t);
			auto pos=fib1.pos();
			auto l=len(pos);
			if(l<mdist) {
				mdist=l;
				tmin=t;
			}
		}
		double t1=1.0, t0=-1.0;
		for(auto c: crosses) {
			auto t=c[ch];
			fib1.move_to(t);
			auto p=fib1.pos();
			if(len(p)<center_thr) {
				t1=t0;
				break;
			}
			if(t>tmin && t<t1)
				t1=t;
			if(t<tmin && t>t0)
				t0=t;
		}
		for(int z=0; z<96; z++) {
			for(int y=0; y<96; y++) {
				for(int x=0; x<96; x++) {
					auto& pix=upsamp[x+y*96+z*(96*96)][2];
					if(pix.ch>=0) {
						if(pix.ch==ch || has_cross)
							pix.from_center1=true;
						if(pix.ch==ch/* && pix.t<t1 && pix.t>t0*/)
							pix.from_center2=true;
					}
				}
			}
		}
	};
	auto mark_branches=[traverse_curve,&crosses,&upsamp](const Curve& fib1, const Curve& fib2) {
		for(auto [ta, tb]: crosses) {
			std::initializer_list<std::pair<const Curve&, double>> pairs{
				{fib1, ta}, {fib2, tb}
			};
			for(auto [fib, t]: pairs) {
				traverse_curve([&fib=fib](double t, Vec& pos) ->double {
					fib.move_to(t);
					pos=fib.pos();
					return 0.5;
				}, t, t,
				[&upsamp](double t, std::array<int, 3> posi, double dist) {
					auto& pix=upsamp[posi[0]+posi[1]*96+posi[2]*(96*96)][2];
					pix.is_fib=false;
				});
			}
		}
	};
	auto find_crosses=[&upsamp,&crosses]() {
		for(int x=1; x<96-1; x++) {
			for(int y=1; y<96-1; y++) {
				for(int z=1; z<96-1; z++) {
					auto& pixels=upsamp[x+y*96+z*(96*96)];
					auto d=pixels[0].dist+pixels[1].dist;
					if(std::isinf(d))
						continue;
					bool is_min{true};
					for(int dx=-1; dx<=1; dx++)
						for(int dy=-1; dy<=1; dy++)
							for(int dz=-1; dz<=1; dz++) {
								auto& pixels2=upsamp[x+dx+(y+dy)*96+(z+dz)*(96*96)];
								auto d2=pixels2[0].dist+pixels2[1].dist;
								if(d2<d)
									is_min=false;
							}
					if(is_min)
						crosses.emplace_back(std::array<double, 2>{pixels[0].t, pixels[1].t});
				}
			}
		}
	};
	auto need_swap=[](const Curve& fib1, const Curve& fib2) ->bool {
		auto dist_to_origin=[](const Curve& fib) ->double {
			double mdist{INFINITY};
			for(double t=fib.t0; t<=fib.t1; t+=0.005) {
				fib.move_to(t);
				auto pos=fib.pos();
				auto l=len(pos);
				if(l<mdist)
					mdist=l;
			}
			return mdist;
		};
		return dist_to_origin(fib1)>dist_to_origin(fib2);
	};

	for(std::size_t I=0; I<N; I++) {
		for(auto& s: upsamp)
			for(auto& p: s)
				p=Pix{};
		crosses.clear();

		Curve fib1, fib2;
		//XXX
		auto typ=rng()%10;
		if(typ>=5) {
			//567891
			typ=1;
		} else if(typ>=3) {
			//342
			typ=2;
		}
		//auto typ=rng()%1+1;
		int main_ch{-1};
		switch(typ) {
			case 0:
				gen_outside(fib1, center_thr2);
				gen_outside(fib2, center_thr2);
				break;
			case 1:
				gen_inside(fib1, center_thr);
				switch(rng()%3) {
					case 0:
						gen_inside(fib2, near_thr);
						break;
					case 1:
						gen_branch(fib2, fib1, false);
						break;
					case 2:
						gen_branch(fib2, fib1, true);
						fib2.t0=0.0;
						break;
				}
				if(need_swap(fib1, fib2))
					std::swap(fib1, fib2);
				main_ch=0;
				break;
			case 2:
				gen_inside(fib1, center_thr);
				gen_outside(fib2, near_thr2);
				main_ch=0;
				break;
		}
		output[I]=typ+1.0;
		paint(fib1, 0);
		paint(fib2, 1);
		downsamp_img(&input[I*1*16*48*48], rng);
		find_crosses();
		paint_seg(fib1, 0);
		paint_seg(fib2, 1);
		mark_branches(fib1, fib2);
		mark_origin(fib1, main_ch);
		downsamp_seg(std::array<double, 4>{0.0, 0.0, 0.0, 0.0},
				[](std::array<double, 4>& s, const Pix& pix)  {
					auto v=pix.val;
					s[0]+=v;
					if(pix.from_center1)
						s[2]+=v;
					if(pix.from_center2)
						s[3]+=v;
					if(!pix.is_fib) {
						s[1]+=v;
					}
				}, [&interm,I](std::array<double, 4>& s, std::array<unsigned int,3> x) {
					auto off=(I*4+0)*12*20*20+x[0]+x[1]*20+x[2]*(20*20);
					for(unsigned int k=0; k<4; k++)
						interm[off+k*12*20*20]=s[k];
				});
	}
}

static void save_nrrd(const float* ptr, const char* pfx, const char* sfx, std::size_t id, std::size_t w, std::size_t h, std::size_t d) {
	char ofn[1024];
	snprintf(ofn, 1024, "%s-%08zu-%s.nrrd", pfx, id, sfx);
	std::ofstream fs{ofn};
	gapr::nrrd_output nrrd{fs};
	nrrd.header();
	nrrd.finish(ptr, w, h, d);
}
static void save_input(const std::valarray<float> input, std::size_t N, const char* dir, const char* label, std::size_t w, std::size_t h, std::size_t d) {
	for(std::size_t i=0; i<N && i<400; i++) {
		auto ptr=&input[(w*h*d+1)*i];
		save_nrrd(ptr, ((std::string{dir}+'-')+label).c_str(), "", ptr[w*h*d], w, h, d);
	}
}

[[maybe_unused]] static void test_predict(Evaluator& ffn, const std::valarray<float>& input, const std::valarray<float>& output, std::size_t N, const char* fn) {
	auto pred=ffn.predict(input);
	std::ofstream fs{fn};
	double s0{0.0}, s1{0.0};
	for(std::size_t i=0; i<N; i++) {
		auto p0=pred[0+3*i];
		auto p1=pred[1+3*i];
		auto p2=pred[2+3*i];
		auto o0=output[0+1*i];
		fs<<i<<": "<<o0<<' '<<p0<<','<<p1<<','<<p2<<'\n';
		//p0-=o0;
		//s0+=p0*p0;
		//p1-=o1;
		//s1+=p1*p1;
	}
	auto t=time(nullptr);
	fs<<s0<<" "<<s1<<" "<<ctime(&t)<<"\n";
	fs.close();
}

static void merge(std::valarray<float>& out, const std::valarray<float>& in, std::mt19937& rng) {
	bool flipx=rng()%2;
	bool flipy=rng()%2;
	bool flipz=rng()%2;
	bool swapxy=rng()%2;
	auto scale1=get_scale_factor(out);
	auto scale2=get_scale_factor(in);

	for(unsigned int z=0; z<16; z++) {
		for(unsigned int y=0; y<48; y++) {
			for(unsigned int x=0; x<48; x++) {
				auto xx=flipx?(47-x):x;
				auto yy=flipy?(47-y):y;
				auto zz=flipz?(15-z):z;
				if(swapxy) {
					auto t=xx;
					xx=yy;
					yy=t;
				}
				auto& o=out[x+y*48+z*(48*48)];
				o=o*scale1+in[xx+yy*48+zz*(48*48)]*scale2;
			}
		}
	}
}
static void perturb(float* out, const std::valarray<float>& in, std::mt19937& rng) {
	std::normal_distribution<double> ndist;
	auto scale=std::exp(ndist(rng)*1);
	if(0) {
		double var=0.0;
		for(unsigned int i=0; i<16*48*48; i++) {
			auto v=in[i];
			var+=v*v;
		}
		var=var/(16*48*48);
		scale=1/std::sqrt(var);
	}
	if(1) {
		scale=get_scale_factor(in);
	}
	bool flipx=rng()%2;
	bool flipy=rng()%2;
	bool flipz=rng()%2;
	bool swapxy=rng()%2;

	for(unsigned int z=0; z<16; z++) {
		for(unsigned int y=0; y<48; y++) {
			for(unsigned int x=0; x<48; x++) {
				auto xx=flipx?(47-x):x;
				auto yy=flipy?(47-y):y;
				auto zz=flipz?(15-z):z;
				if(swapxy) {
					auto t=xx;
					xx=yy;
					yy=t;
				}
				out[x+y*48+z*(48*48)]=in[xx+yy*48+zz*(48*48)]*scale;
			}
		}
	}
}

static void load_pars(Evaluator& ffn, const std::filesystem::path& out_pars) {
	std::ifstream str{out_pars};
	if(!str)
		return;
	ffn.load(str);
}

static void train_stage1(Evaluator& ffn, std::mt19937& rng, const std::filesystem::path& out_pars) {
	std::mutex mtx;
	std::condition_variable cv;
	bool gen_end{false};
	std::deque<std::valarray<float>> batch_que;
	auto gen_batches=[&mtx,&cv,&gen_end,&batch_que](unsigned int thr_id, auto rng_seed) {
		std::mt19937 rng{rng_seed};
		//std::size_t M=64;
		std::size_t M=32;
		std::size_t iter=0;
		do {
			std::valarray<float> batch((48*48*16+20*20*12*4+1)*M);
			std::valarray<float> input;
			std::valarray<float> output;
			std::valarray<float> interm;
			gen_samples(input, output, interm, M, rng);
			auto scale=get_scale_factor(input);
			for(std::size_t k=0; k<M; k++) {
				auto ptr=&batch[(48*48*16+20*20*12*4+1)*k];
				for(unsigned int i=0; i<16*48*48; i++)
					ptr[i]=input[i+(48*48*16)*k]*scale;
				ptr+=48*48*16;
				for(unsigned int i=0; i<20*20*12*4; i++)
					ptr[i]=interm[i+(20*20*12*4)*k];
				ptr+=20*20*12*4;
				*ptr=output[k];
			}
			if((iter++)%1024==0 && thr_id==0) {
				std::cerr<<"gen_batches: "<<iter-1<<'\n';
				//save_input(batch, M, "/tmp/convinput", "samp", 48, 48, 16);
			}
			std::unique_lock lck{mtx};
			while(true) {
				if(gen_end)
					return;
				if(batch_que.size()<1024)
					break;
				cv.wait(lck);
			}
			batch_que.push_back(std::move(batch));
			lck.unlock();
			cv.notify_all();
		} while(true);
	};
	auto get_batch=[&mtx,&cv,&batch_que]() mutable {
		std::unique_lock lck{mtx};
		while(batch_que.empty())
			cv.wait(lck);
		auto b=std::move(batch_que.front());
		batch_que.pop_front();
		lck.unlock();
		cv.notify_all();
		return b;
	};

	std::vector<std::thread> gen_threads;
	for(unsigned int i=0; i<std::thread::hardware_concurrency(); i++)
		gen_threads.push_back(std::thread{gen_batches, i, rng()});

	ffn.train(get_batch, out_pars);

	{
		std::lock_guard lck{mtx};
		gen_end=true;
	}
	cv.notify_all();

	for(auto& thr: gen_threads)
		thr.join();
}

int train_evaluator(const std::filesystem::path& out_pars, int nsamp, char* samp_files[]) {
	std::mt19937 rng{std::random_device{}()};

	if(false) {
		std::valarray<float> input;
		std::valarray<float> output;
		std::valarray<float> interm;
		std::size_t N=16;
		gen_samples(input, output, interm, N, rng);
		for(std::size_t i=0; i<N; i++) {
			save_nrrd(&input[i*1*16*48*48], "/tmp/convinput/samp", "input", i*100+output[i], 48, 48, 16);
			for(std::size_t j=0; j<4; j++)
				save_nrrd(&interm[(i*4+j)*12*20*20], "/tmp/convinput/samp", "interm", i*100+j, 20, 20, 12);
		}
		return 0;
	}

	if(false) {
		auto ffn=Evaluator::create_stage1();
		load_pars(*ffn, out_pars);
		train_stage1(*ffn, rng, out_pars);
		return 0;
	}

	std::vector<CompressedSample> samples;
	for(int i=0; i<nsamp; i++) {
		std::vector<CompressedSample> vals_pos, vals_neg, vals_0;
		std::ifstream fs{samp_files[i]};
		std::vector<CompressedSample> tmp_samps;
		if(!load(tmp_samps, fs))
			throw std::runtime_error{"failed to load samples"};
		for(auto& s: tmp_samps) {
			if(s.tag>0) {
				vals_pos.push_back(std::move(s));
			} else if(s.tag<0) {
				vals_neg.push_back(std::move(s));
			} else {
				vals_0.push_back(std::move(s));
			}
		}

		std::size_t n{100'000'000};
		for(auto pvals: {&vals_pos, &vals_neg, &vals_0}) {
			if(n>pvals->size())
				n=pvals->size();
			std::shuffle(pvals->begin(), pvals->end(), rng);
		}
		if(n<2'000)
			n=2'000;
		n=n*5+1000;
		for(auto pvals: {&vals_pos, &vals_neg, &vals_0}) {
			while(pvals->size()>n)
				pvals->pop_back();
			for(auto& v: *pvals)
				samples.push_back(std::move(v));
		}
		std::cerr<<"load samp: "<<samples.size()<<'\n';
	}
	std::shuffle(samples.begin(), samples.end(), rng);

	auto ffn=Evaluator::create();
	load_pars(*ffn, out_pars);

	if(1) {
		std::mutex mtx;
		std::condition_variable cv;
		bool gen_end{false};
		std::deque<std::valarray<float>> batch_que;
		std::size_t I=rng()%samples.size();
		auto gen_batches=[&I,&samples,&mtx,&cv,&gen_end,&batch_que](unsigned int thr_id, auto rng_seed) {
			std::mt19937 rng{rng_seed};
			std::valarray<float> temp(48*48*16), temp2(48*48*16);
			//std::size_t M=64;
			std::size_t M=64;
			std::size_t iter=0;
			do {
				std::valarray<float> batch((48*48*16+1)*M);
				for(std::size_t k=0; k<M; k++) {
					auto ptr=&batch[(48*48*16+1)*k];
					unsigned int fake_ratio=800*0*std::exp(iter/-2000.0)+100*0;
					if(rng()%1000<fake_ratio) {
						std::valarray<float> input;
						std::valarray<float> output;
						std::valarray<float> interm;
						gen_samples(input, output, interm, 1, rng);
						perturb(ptr, input, rng);
						ptr[48*48*16]=output[0];
					} else {
						std::size_t II;
						{
							std::lock_guard lck{mtx};
							II=I;
							I=(I+1)%samples.size();
						}
						const auto& [samp, tag]=samples[II];
						if(!decompress_zlib(&temp[0], temp.size()*sizeof(float), samp))
							throw std::runtime_error{"failed to compress and decompress"};
						float o;
						if(tag>0) {
							o=3;
						} else if(tag<0) {
							o=1;
						} else {
							o=2;
						}
						if(true && tag>0 && rng()%15<11) {
							std::size_t III=(II+rng()%10'000+10'000)%samples.size();
							const auto& [samp2, tag2]=samples[III];
							if(!decompress_zlib(&temp2[0], temp2.size()*sizeof(float), samp2))
								throw std::runtime_error{"failed to compress and decompress 2"};
							merge(temp, temp2, rng);
							if(tag2>0) {
								o=2;
							} else if(tag2<0) {
								o=3;
							} else {
								o=2;
							}
						}
						perturb(ptr, temp, rng);
						ptr[48*48*16]=o;
					}
					if(false) {
						auto& cat=ptr[48*48*16];
						if(std::abs(cat-2.0)<.1)
							cat=3.0;
					}
					if(false) {
						auto& cat=ptr[48*48*16];
						cat=rng()%3+1;
						for(int i=0; i<48*48*4; i++)
							ptr[i]=cat;
					}
					if(std::abs(ptr[48*48*16]-1.0)<.1 && rng()%2==0)
						k--;
				}

				if((iter++)%1024==0 && thr_id==0) {
					std::cerr<<"gen_batches: "<<iter-1<<'\n';
					save_input(batch, M, "/tmp/convinput", "samp", 48, 48, 16);
				}

				std::unique_lock lck{mtx};
				while(true) {
					if(gen_end)
						return;
					if(batch_que.size()<1024)
						break;
					cv.wait(lck);
				}
				batch_que.push_back(std::move(batch));
				lck.unlock();
				cv.notify_all();
			} while(true);
		};
		auto get_batch=[&mtx,&cv,&batch_que]() mutable {
			std::unique_lock lck{mtx};
			while(batch_que.empty())
				cv.wait(lck);
			auto b=std::move(batch_que.front());
			batch_que.pop_front();
			lck.unlock();
			cv.notify_all();
			return b;
		};

		std::vector<std::thread> gen_threads;
		for(unsigned int i=0; i<std::thread::hardware_concurrency(); i++)
			gen_threads.push_back(std::thread{gen_batches, i, rng()});

		ffn->train(get_batch, out_pars);
		{
			std::lock_guard lck{mtx};
			gen_end=true;
		}
		cv.notify_all();

		for(auto& thr: gen_threads)
			thr.join();
		//test_predict(*ffn, input, output, N, "/tmp/test-pred2.txt");
	}

	return 0;
}

