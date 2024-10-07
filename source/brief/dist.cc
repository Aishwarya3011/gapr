#include "dist.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <vector>
#include <memory>

#include "util.hh"

struct SerializedNeuron {
	struct Node {
		gapr::vec3<double> pos;
		double distToParent;
		double lengthSum;
		size_t par;
		std::vector<size_t> branches;
		bool isbp;
	};
	std::vector<Node> nodes;
	SerializedNeuron(): nodes{} {
	}
	SerializedNeuron(size_t n): nodes{n} {
	}
	Node& node(size_t i) {
		return nodes[i];
	}
	const Node& node(size_t i) const {
		return nodes[i];
	}
};

SerializedNeuron serialize(const SwcData& swc, double max_segm) {
	struct AuxData {
		unsigned int n_child{0};
		std::size_t new_idx{SIZE_MAX};
	};
	std::vector<AuxData> aux;
	aux.reserve(swc.nodes.size());
	unsigned int n_verts=0;
	for(unsigned int i=0; i<swc.nodes.size(); ++i) {
		auto& n=swc.nodes[i];
		aux.emplace_back();
		auto has_par=n.par_idx!=n.par_idx_null;
		if(has_par != (i>0))
			throw std::runtime_error{"need one neuron per SWC file"};
		if(has_par) {
			auto v=++aux[n.par_idx].n_child;
			if(v>=2)
				n_verts+=(v==2?2:1);
		}
	}
	SerializedNeuron nrn;
	nrn.nodes.reserve(swc.nodes.size()+n_verts+10);
	for(unsigned int i=0; i<swc.nodes.size(); ++i) {
		auto& n=swc.nodes[i];
		auto& a=aux[i];
		std::size_t parIdx=SIZE_MAX;
		if(i>0) {
			auto& aa=aux[n.par_idx];
			parIdx=aa.new_idx;
			auto& np=nrn.nodes[parIdx];
			if(np.isbp) {
				auto& nnn=nrn.nodes.emplace_back();
				nnn.pos=np.pos;
				nnn.distToParent=0.0;
				nnn.lengthSum=0.0;
				nnn.par=parIdx;
				nnn.isbp=false;
				parIdx=nrn.nodes.size()-1;
				np.branches.push_back(parIdx);
			}
			auto l=(n.pos-np.pos).mag();
			if(l>max_segm) {
				int m=lrint(l/max_segm+.5);
				for(int k=1; k<m; k++) {
					auto& nnn=nrn.nodes.emplace_back();
					nnn.pos=(np.pos*(m-k)+n.pos*k)/m;
					nnn.distToParent=0.0;
					nnn.lengthSum=0.0;
					nnn.par=parIdx;
					nnn.isbp=false;
					parIdx=nrn.nodes.size()-1;
				}
			}
		}
		auto& nn=nrn.nodes.emplace_back();
		nn.pos=n.pos;
		nn.distToParent=0.0;
		nn.lengthSum=0.0;
		nn.par=parIdx;
		nn.isbp=false;
		a.new_idx=nrn.nodes.size()-1;
		if(a.n_child>=2 || a.n_child==0) {
			nn.isbp=true;
			nn.branches.reserve(a.n_child);
		}
	}

	auto& nodes=nrn.nodes;
	for(size_t j=nodes.size(); j-->0;) {
		auto par=nodes[j].par;
		if(par!=SIZE_MAX) {
			nodes[j].distToParent=(nodes[j].pos-nodes[par].pos).mag();
		} else {
			nodes[j].distToParent=0;
		}

		double lengthSum=0;
		if(nodes[j].isbp) {
			for(auto b: nodes[j].branches) {
				lengthSum+=nodes[b].lengthSum+nodes[b].distToParent;
			}
		} else {
			lengthSum=nodes[j+1].lengthSum+nodes[j+1].distToParent;
		}
		nodes[j].lengthSum=lengthSum;
	}
	return nrn;
}

struct Grid {
	size_t n0, n1;

	struct GridNode {
		double score;
		double sumMatch;
		double sumNomatch;
		double dist;
		double scoreNoMatch0;
		double scoreNoMatch1;
		double mdist0;
		double mdist1;
		std::size_t bti;
		union {
			std::size_t btj;
			std::vector<std::pair<size_t, size_t>>* btx;
		};
		auto& bt() const noexcept {
			assert(bti==SIZE_MAX);
			return *btx;
		}
	};
	std::vector<GridNode> nodes;
	Grid(size_t _n0, size_t _n1): n0{_n0}, n1{_n1}, nodes{n0*n1} {
	}
	GridNode& node(size_t i, size_t j) {
		return nodes[i*n1+j];
	}
	std::vector<std::unique_ptr<std::vector<std::pair<size_t, size_t>>>> _btvecs;
	auto btvec() {
		auto ptr=std::make_unique<std::vector<std::pair<size_t, size_t>>>();
		auto r=ptr.get();
		_btvecs.push_back(std::move(ptr));
		return r;
	}
};

void calcDist1(const gapr::vec3<double>& a, const gapr::vec3<double>& b, const gapr::vec3<double>& c, const gapr::vec3<double>& d, double l0, double l1, double* d0, double* d1) {
	auto ab=b-a;
	auto ac=c-a;
	auto ab2=dot(ab, ab);
	auto abac=dot(ab, ac);
	auto t0=abac/l0/l0;
	if(std::isnan(t0))
		t0=0;
	if(t0<0) t0=0;
	if(t0>1) t0=1;
	auto d02=t0*t0*l0*l0+ab2-2*t0*abac;
	if(d02<0) d02=0;
	*d0=sqrt(d02);
	auto bd=d-b;
	auto abbd=dot(ab, bd);
	auto t1=-abbd/l1/l1;
	if(std::isnan(t1))
		t1=0;
	if(t1<0) t1=0;
	if(t1>1) t1=1;
	auto d12=t1*t1*l1*l1+ab2+2*t1*abbd;
	if(d12<0) d12=0;
	*d1=sqrt(d12);
}
struct NodePair {
	size_t i;
	size_t j;
};
struct NodePairScore {
	size_t i;
	size_t j;
	double s;
};
void calcDist(const SerializedNeuron& nrn0, const SerializedNeuron& nrn1, std::vector<std::pair<size_t, size_t>>& alignment, double* score, double* match, double* nomatch) {
	auto n0=nrn0.nodes.size();
	auto n1=nrn1.nodes.size();
	Grid grid{n0, n1};

	std::vector<NodePairScore> nodePairScores;
	std::vector<NodePair> nodePairs;
	std::vector<bool> nodeUsed0;
	std::vector<bool> nodeUsed1;

	for(size_t i=n0; i-->0;) {
		for(size_t j=n1; j-->0;) {
			auto& node=grid.node(i, j);
			node.dist=(nrn0.node(i).pos-nrn1.node(j).pos).mag();
			auto paridx0=nrn0.node(i).par;
			if(paridx0==SIZE_MAX)
				paridx0=i;
			auto paridx1=nrn1.node(j).par;
			if(paridx1==SIZE_MAX)
				paridx1=j;
			calcDist1(nrn0.node(i).pos, nrn1.node(j).pos, nrn0.node(paridx0).pos, nrn1.node(paridx1).pos, nrn0.node(i).distToParent, nrn1.node(j).distToParent, &node.mdist0, &node.mdist1);
			double scoreNoMatch0=0;
			if(nrn0.node(i).isbp) {
				for(auto b: nrn0.node(i).branches)
					//scoreNoMatch0+=nrn0.node(b).distToParent*(node.dist+grid.node(b, j).dist)/2+grid.node(b, j).scoreNoMatch0;
					scoreNoMatch0+=/*nrn0.node(b).distToParent*grid.node(b, j).mdist0+*/grid.node(b, j).scoreNoMatch0;
			} else {
				//scoreNoMatch0+=nrn0.node(i+1).distToParent*(node.dist+grid.node(i+1, j).dist)/2+grid.node(i+1, j).scoreNoMatch0;
				scoreNoMatch0+=nrn0.node(i+1).distToParent*grid.node(i+1, j).mdist0+grid.node(i+1, j).scoreNoMatch0;
			}
			node.scoreNoMatch0=scoreNoMatch0;
			double scoreNoMatch1=0;
			if(nrn1.node(j).isbp) {
				for(auto b: nrn1.node(j).branches)
					//scoreNoMatch1+=nrn1.node(b).distToParent*(node.dist+grid.node(i, b).dist)/2+grid.node(i, b).scoreNoMatch1;
					scoreNoMatch1+=/*nrn1.node(b).distToParent*grid.node(i, b).mdist1+*/grid.node(i, b).scoreNoMatch1;
			} else {
				//scoreNoMatch1+=nrn1.node(j+1).distToParent*(node.dist+grid.node(i, j+1).dist)/2+grid.node(i, j+1).scoreNoMatch1;
				scoreNoMatch1+=nrn1.node(j+1).distToParent*grid.node(i, j+1).mdist1+grid.node(i, j+1).scoreNoMatch1;
			}
			node.scoreNoMatch1=scoreNoMatch1;

			node.score=INFINITY;

			if(!nrn0.node(i).isbp) {
				auto len=nrn0.node(i+1).distToParent;
				//auto mval=grid.node(i+1, j).score+len*(node.dist+grid.node(i+1,j).dist)/2;
				auto mval=grid.node(i+1, j).score+len*grid.node(i+1,j).mdist0;
				if(mval<node.score) {
					node.score=mval;
					//node.bt.resize(1);
					//node.bt[0].first=i+1;
					//node.bt[0].second=j;
					node.bti=i+1;
					node.btj=j;
					node.sumMatch=grid.node(i+1, j).sumMatch+len;
					node.sumNomatch=grid.node(i+1, j).sumNomatch;
				}
			}
			if(!nrn1.node(j).isbp) {
				auto len=nrn1.node(j+1).distToParent;
				//auto mval=grid.node(i, j+1).score+len*(node.dist+grid.node(i,j+1).dist)/2;
				auto mval=grid.node(i, j+1).score+len*grid.node(i,j+1).mdist1;
				if(mval<node.score) {
					node.score=mval;
					//node.bt.resize(1);
					//node.bt[0].first=i;
					//node.bt[0].second=j+1;
					node.bti=i;
					node.btj=j+1;
					node.sumMatch=grid.node(i, j+1).sumMatch+len;
					node.sumNomatch=grid.node(i, j+1).sumNomatch;
				}
				if(nrn0.node(i).isbp) {
					double dscore=INFINITY;
					size_t i1=SIZE_MAX;
					for(auto c: nrn0.node(i).branches) {
						double scored=grid.node(c, j).score-grid.node(c, j).scoreNoMatch0-grid.node(i, j).scoreNoMatch1;
						if(scored<dscore) {
							dscore=scored;
							i1=c;
						}
					}
					auto score=node.scoreNoMatch0+node.scoreNoMatch1+dscore;
					if(score<node.score) {
						node.score=score;
						//node.bt.resize(1);
						//node.bt[0].first=i1;
						//node.bt[0].second=j;
						node.bti=i1;
						node.btj=j;
						node.sumMatch=grid.node(i1, j).sumMatch;
						node.sumNomatch=grid.node(i1, j).sumNomatch+nrn0.node(i).lengthSum-nrn0.node(i1).lengthSum;
					}
				}
			} else {
				if(nrn0.node(i).isbp) {
					nodePairScores.clear();
					for(size_t i1p=0; i1p<nrn0.node(i).branches.size(); i1p++) {
						for(size_t j1p=0; j1p<nrn1.node(j).branches.size(); j1p++) {
							auto i1=nrn0.node(i).branches[i1p];
							auto j1=nrn1.node(j).branches[j1p];
							auto scored=grid.node(i1, j1).score-grid.node(i1, j).scoreNoMatch0-grid.node(i, j1).scoreNoMatch1;
							nodePairScores.push_back({i1p, j1p, scored});
						}
					}
					std::sort(nodePairScores.begin(), nodePairScores.end(), [](const NodePairScore& a, const NodePairScore& b) { return a.s<b.s; });

					double dscore=0;
					nodePairs.clear();
					nodeUsed0.assign(nrn0.node(i).branches.size(), false);
					nodeUsed1.assign(nrn1.node(j).branches.size(), false);
					for(auto& nps: nodePairScores) {
						if(nps.s>0)
							break;
						if(nodeUsed0[nps.i])
							continue;
						if(nodeUsed1[nps.j])
							continue;
						dscore+=nps.s;
						nodePairs.push_back({nps.i, nps.j});
						nodeUsed0[nps.i]=true;
						nodeUsed1[nps.j]=true;
					}

					if(dscore<=0) {
						node.score=node.scoreNoMatch0+node.scoreNoMatch1+dscore;
						node.bti=SIZE_MAX;
						node.btx=grid.btvec();
						node.bt().resize(nodePairs.size());
						double sumMatch=0;
						double sumNomatch=nrn0.node(i).lengthSum+nrn1.node(j).lengthSum;
						for(size_t k=0; k<nodePairs.size(); k++) {
							auto i1=nrn0.node(i).branches[nodePairs[k].i];
							auto j1=nrn1.node(j).branches[nodePairs[k].j];
							node.bt()[k].first=i1;
							node.bt()[k].second=j1;
							sumMatch+=grid.node(i1, j1).sumMatch;
							sumNomatch+=grid.node(i1, j1).sumNomatch-nrn0.node(i1).lengthSum-nrn1.node(j1).lengthSum;
						}
						node.sumMatch=sumMatch;
						node.sumNomatch=sumNomatch;
					}
				} else {
					double dscore=INFINITY;
					size_t j1=SIZE_MAX;
					for(auto c: nrn1.node(j).branches) {
						auto scored=grid.node(i, c).score-grid.node(i, j).scoreNoMatch0-grid.node(i, c).scoreNoMatch1;
						if(scored<dscore) {
							dscore=scored;
							j1=c;
						}
					}
					auto score=node.scoreNoMatch0+node.scoreNoMatch1+dscore;
					if(dscore<0) {
						node.score=score;
						//node.bt.resize(1);
						//node.bt[0].first=i;
						//node.bt[0].second=j1;
						node.bti=i;
						node.btj=j1;
						node.sumMatch=grid.node(i, j1).sumMatch;
						node.sumNomatch=grid.node(i, j1).sumNomatch+nrn1.node(j).lengthSum-nrn1.node(j1).lengthSum;
					}
				}
			}
			if(node.scoreNoMatch0+node.scoreNoMatch1<node.score) {
				node.score=node.scoreNoMatch0+node.scoreNoMatch1;
				node.sumMatch=0;
				node.sumNomatch=nrn0.node(i).lengthSum+nrn1.node(j).lengthSum;
				node.bti=SIZE_MAX;
				node.btx=grid.btvec();
				node.bt().clear();
			}
		}
	}
	std::queue<std::pair<size_t, size_t>> que{};
	que.push(std::pair<size_t, size_t>{0, 0});
	while(!que.empty()) {
		auto cur=que.front();
		que.pop();
		//fprintf(stderr, "pop %zd %zd\n", cur.first, cur.second);
		alignment.push_back(cur);
		auto& node=grid.node(cur.first, cur.second);
		if(node.bti!=SIZE_MAX) {
			que.push({node.bti, node.btj});
		} else {
			for(auto& v: node.bt()) {
				que.push(v);
			}
		}
	}
	*score=grid.node(0, 0).score;
	*match=grid.node(0, 0).sumMatch;
	*nomatch=grid.node(0, 0).sumNomatch;
}

#include "dist-test.cc"
void dist_impl_test() {
	double max_segm=INFINITY;
	//max_segm=.5;
	std::vector<SerializedNeuron> neurons;
	for(auto& nrn: test_neurons) {
		SwcData swc;
		int id=1;
		for(auto& n: nrn) {
			auto& nn=swc.nodes.emplace_back();
			nn.id=id++;
			swc.id2idx.emplace(nn.id, swc.nodes.size()-1);
			nn.pos={n.x, n.y, 0.0};
			nn.radius=1.0;
			nn.type=0;
			if(n.p==-1)
				nn.par_idx=nn.par_idx_null;
			else
				nn.par_idx=swc.id2idx.at(n.p);
		}
		neurons.emplace_back(serialize(swc, max_segm));
		if(1) {
			std::size_t idx=0;
			for(auto& nn: neurons.back().nodes) {
				fprintf(stderr, "%% %zd: %lf,%lf %s %lf %lf %zd [",
						idx++,
						nn.pos[0], nn.pos[1],
						nn.isbp?"*":"-",
						nn.distToParent,
						nn.lengthSum,
						nn.par);
				for(auto& c: nn.branches)
					fprintf(stderr, " %zd", c);
				fprintf(stderr, "\n");
			}
			fprintf(stderr, "\n");
		}
	}

	std::cout<<"\\documentclass{article}\n";
	std::cout<<"\\usepackage{tikz}\n";
	std::cout<<"\\usetikzlibrary{calc}\n";
	std::cout<<"\\begin{document}\n";
	std::cout.setf(std::ios::fixed);
	for(size_t kk=0; kk+1<neurons.size(); kk+=2) {
		double minx=INFINITY, miny=INFINITY;
		double maxx=-INFINITY, maxy=-INFINITY;
		std::vector<std::pair<size_t, size_t>> alignment{};
		double dist, matchl, nomatchl;
		calcDist(neurons[kk], neurons[kk+1], alignment, &dist, &matchl, &nomatchl);
		for(size_t k=0; k<2; k++) {
			auto& nrn=neurons[kk+k];
			for(auto& n: nrn.nodes) {
				auto x=n.pos[0];
				auto y=n.pos[1];
				if(x<minx) minx=x;
				if(x>maxx) maxx=x;
				if(y<miny) miny=y;
				if(y>maxy) maxy=y;
			}
		}

		minx=floor(minx)-1;
		maxx=ceil(maxx-.3);
		miny=floor(miny)-1;
		maxy=ceil(maxy-.3);

		std::cout<<"\\begin{tikzpicture}\n";
		std::cout<<"\\draw ("<<maxx+.5<<"cm,"<<maxy+.5<<"cm) rectangle (";
		std::cout<<minx+.5<<"cm,"<<miny+.5<<"cm);\n";
		std::cout<<"\\node at (0, 1cm) {"<<dist<<"};\n";
		std::cout<<"\\node at (0, .5cm) {"<<matchl<<"};\n";
		std::cout<<"\\node at (0, 0cm) {"<<nomatchl<<"};\n";
		std::cout<<"\\begin{scope}[line width=.4pt,dashed,color=black!20]\n";
		for(double x=minx+1; x<=maxx-1; x+=1) {
			std::cout<<"\\draw ("<<x+.5<<"cm,"<<maxy+.5<<"cm)--";
			std::cout<<"("<<x+.5<<"cm,"<<miny+.5<<"cm);\n";
		}
		for(double y=miny+1; y<=maxy-1; y+=1) {
			std::cout<<"\\draw ("<<minx+.5<<"cm,"<<y+.5<<"cm)--";
			std::cout<<"("<<maxx+.5<<"cm,"<<y+.5<<"cm);\n";
		}
		std::cout<<"\\end{scope}\n";

		for(size_t k=0; k<2; k++) {
			auto& nrn=neurons[kk+k];
			auto x=nrn.nodes[0].pos[0];
			auto y=nrn.nodes[0].pos[1];
			if(k==0) {
				std::cout<<"\\draw[yshift=-"<<k*.11<<"cm,color=green]";
			} else {
				std::cout<<"\\draw[yshift=-"<<k*.11<<"cm,color=red]";
			}
			std::cout<<" ("<<x<<"cm,"<<y<<"cm) circle(.15cm);\n";
		}

		for(size_t k=0; k<2; k++) {
			auto& nrn=neurons[kk+k];
			for(unsigned int i=1; i<nrn.nodes.size(); ++i) {
					auto x=nrn.nodes[i].pos[0];
					auto y=nrn.nodes[i].pos[1];
					if(k==0) {
						std::cout<<"\\draw [yshift=-"<<k*.11<<"cm,color=green]";
					} else {
						std::cout<<"\\draw [yshift=-"<<k*.11<<"cm,color=red]";
					}
					std::cout<<" ("<<x<<"cm,"<<y<<"cm) circle (.1cm);\n";
			}
		}

		std::cout<<"\\begin{scope}[line width=.02cm]\n";
		for(size_t k=0; k<2; k++) {
			auto& nrn=neurons[kk+k];
			for(unsigned int i=1; i<nrn.nodes.size(); ++i) {
					auto x=nrn.nodes[i].pos[0];
					auto y=nrn.nodes[i].pos[1];
					auto x0=nrn.nodes[nrn.nodes[i].par].pos[0];
					auto y0=nrn.nodes[nrn.nodes[i].par].pos[1];
					if(k==0) {
						std::cout<<"\\draw [yshift=-"<<k*.11<<"cm,color=green]";
					} else {
						std::cout<<"\\draw [yshift=-"<<k*.11<<"cm,color=red]";
					}
					std::cout<<"("<<x0<<"cm,"<<y0<<"cm)--";
					std::cout<<"("<<x<<"cm,"<<y<<"cm);\n";
			}
		}
		std::cout<<"\\end{scope}\n";
		std::cout<<"\\begin{scope}[line width=.01cm,color=blue]\n";
		for(auto& p: alignment) {
			std::cout<<"\\draw ("<<neurons[kk].node(p.first).pos[0]<<"cm,"<<neurons[kk].node(p.first).pos[1]<<"cm)--";
			std::cout<<"($("<<neurons[kk+1].node(p.second).pos[0]<<"cm,"<<neurons[kk+1].node(p.second).pos[1]<<"cm)+(0, -.11cm)$);\n";
		}
		std::cout<<"\\end{scope}\n";

		std::cout<<"\\end{tikzpicture}\n";
	}
	std::cout<<"\\end{document}\n";
}

void dist_impl(unsigned int n_fns, char const* const fns[], double max_segm) {
	std::vector<SerializedNeuron> neurons;
	for(unsigned int i=0; i<n_fns; ++i) {
		SwcData swc;
		swc.read(fns[i]);
		neurons.emplace_back(serialize(swc, max_segm));
	}

	std::vector<size_t> ordered_indexes(n_fns);
	for(size_t i=0; i<n_fns; i++)
		ordered_indexes[i]=i;
	std::sort(ordered_indexes.begin(), ordered_indexes.end(), [&neurons](size_t a, size_t b) -> bool { return neurons[a].nodes.size()>neurons[b].nodes.size(); });

	std::cout<<"I\tJ\tScore\tMatch\tNoMatch\n";
#pragma omp parallel for schedule(static, 1)
	for(size_t ii=0; ii<n_fns; ii++) {
		auto i=ordered_indexes[ii];
#pragma omp critical
		std::cerr<<"i = "<<i<<": "<<neurons[i].nodes.size()<<"\n";
		for(size_t jj=ii; jj<n_fns; jj++) {
			auto j=ordered_indexes[jj];
			std::vector<std::pair<size_t, size_t>> alignment{};
			double dist, match, nomatch;
			calcDist(neurons[i], neurons[j], alignment, &dist, &match, &nomatch);
#pragma omp critical
			{
				std::cout<<i<<"\t"<<j<<"\t"<<dist<<"\t"<<match<<"\t"<<nomatch<<"\n";
			}
		}
	}
}

