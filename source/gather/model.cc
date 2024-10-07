#include "model.hh"

#include "stats.hh"

#include "gapr/utility.hh"
#include "gapr/streambuf.hh"
#include "gapr/archive.hh"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <unordered_set>

#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include <sys/stat.h>
//#include <unistd.h>
#include <malloc.h>

#include "../corelib/model-upgrade.hh"

static constexpr double HARD_COLL_DIST{1.0};
static constexpr double SOFT_COLL_DIST{5.0};
static constexpr unsigned int PROP_REFC{65536};

template<typename T>
static T failure(T res, const char* msg) {
	if(0)
		throw std::runtime_error{msg};
	gapr::print(msg);
	return res;
}

enum CollisionType {
	NO_COLL,
	SOFT_COLL,
	HARD_COLL,
	UNKNOWN_COLL
};

struct LineInt {
	using PointInt=gapr::node_attr::ipos_type;
	PointInt p0;
	PointInt p1;
	LineInt(gapr::node_attr n1, gapr::node_attr n2):
		p0{n1.ipos}, p1{n2.ipos} { }
};
static inline bool operator==(const LineInt& a, const LineInt& b) {
	return a.p0==b.p0 && a.p1==b.p1;
}

template<>
struct std::hash<LineInt> {
	std::size_t operator()(const LineInt& l) const noexcept {
		std::hash<std::decay_t<decltype(l.p0[0])>> h{};
		auto a=(h(l.p0[0])^h(l.p0[1])^h(l.p0[2]))>>8;
		auto b=h(l.p1[0])^h(l.p1[1])^h(l.p1[2]);
		return a^b;
	}
};

template<typename Hash> static inline void clear_hash(Hash& h) {
	if(h.size()>=8*1024) {
		h=Hash{};
		return;
	}
	if(h.size()>=64)
		return h.clear();
	h.erase(h.begin(), h.end());
}

static inline double dist(const std::array<double, 3>& p0, const std::array<double, 3>& p1) {
	double s{0.0};
	for(unsigned int i=0; i<3; i++) {
		auto d=p0[i]-p1[i];
		s+=d*d;
	}
	return std::sqrt(s);
}

struct spatial_info {
	struct Line {
		std::array<double, 3> p0;
		std::array<double, 3> p1;
		double len;
		bool near(const Line& r) const {
			// XXX coarse
			if(dist(p0, r.p0)>HARD_COLL_DIST+len+r.len)
				return false;
			if(dist(p0, r.p1)>HARD_COLL_DIST+len+r.len)
				return false;
			if(dist(p1, r.p0)>HARD_COLL_DIST+len+r.len)
				return false;
			if(dist(p1, r.p1)>HARD_COLL_DIST+len+r.len)
				return false;
			return true;
		}
	};
	uint64_t id;
	std::vector<Line> lines; // the first Line is actually a bbox.
	spatial_info() noexcept: id{9999999999999}, lines{} { }
	spatial_info(uint64_t id, std::unordered_set<LineInt>&& ls): id{id}, lines{} {
		if(ls.empty())
			return;
		lines.reserve(ls.size()+1);
		lines.emplace_back();
		std::array<double, 6> bbox{
			INFINITY, INFINITY, INFINITY,
			-INFINITY, -INFINITY, -INFINITY
		};
		for(auto& l: ls) {
			auto&& ll=lines.emplace_back();
			double d2sum{0.0};
			gapr::node_attr attr0{l.p0, {}};
			gapr::node_attr attr1{l.p1, {}};
			for(unsigned int i=0; i<3; i++) {
				auto a=ll.p0[i]=attr0.pos(i);
				if(a<bbox[i])
					bbox[i]=a;
				if(a>bbox[3+i])
					bbox[3+i]=a;
				auto b=ll.p1[i]=attr1.pos(i);
				if(b<bbox[i])
					bbox[i]=b;
				if(b>bbox[3+i])
					bbox[3+i]=b;
				b=b-a;
				d2sum+=b*b;
			}
			ll.len=std::sqrt(d2sum);
		}
		for(unsigned int i=0; i<3; i++) {
			lines.front().p0[i]=bbox[i]-SOFT_COLL_DIST;
			lines.front().p1[i]=bbox[3+i]+SOFT_COLL_DIST;
		}
	}
	spatial_info(const spatial_info&) =delete;
	spatial_info& operator=(const spatial_info&) =delete;
	spatial_info(spatial_info&& r) noexcept =default;
	spatial_info& operator=(spatial_info&& r) noexcept =default;
	//collision test: xyz position, with deltas not seen by client, start from latest

	CollisionType check(const spatial_info& r) const {
		if(lines.empty())
			return UNKNOWN_COLL;
		if(r.lines.empty())
			return UNKNOWN_COLL;
		for(unsigned int i=0; i<3; i++) {
			if(r.lines.front().p0[i]>lines.front().p1[i])
				return NO_COLL;
			if(lines.front().p0[i]>r.lines.front().p1[i])
				return NO_COLL;
		}
		for(std::size_t i=1; i<lines.size(); i++) {
			for(std::size_t j=1; j<r.lines.size(); j++) {
				if(lines[i].near(r.lines[j]))
					return HARD_COLL;
			}
		}
		return SOFT_COLL;
	}
};

struct gather_model::PRIV {

	enum VAL_STATE {
		PRE_ADD,
		PRE_DEL,
		PRE_CHG,
	};

	uint64_t _num_commits_nocheck;
	std::deque<spatial_info> _spat_infos;
	void spat_info_init(uint64_t n) { _num_commits_nocheck=n; }
	uint64_t spat_info_base() const { return _num_commits_nocheck; }
	void spat_info_append(uint64_t id) {
		// ZZZ
		constexpr unsigned int factor=8000;
		//constexpr unsigned int factor=1;
		if(_spat_infos.size()>4*factor) {
			fprintf(stderr, "spatinfo: resize %zu\n", _spat_infos.size());
			do {
				_spat_infos.pop_front();
				++_num_commits_nocheck;
			} while(_spat_infos.size()>1*factor);
		}
		//XXX remove _priv->lines dups
		auto n=id-_num_commits_nocheck;
		while(n>=_spat_infos.size())
			_spat_infos.push_back({});
		_spat_infos[n]=spatial_info{id, std::move(lines)};
	}
	const spatial_info& spat_info_at(uint64_t id) const {
		auto& info=_spat_infos[id-_num_commits_nocheck];
		assert(info.id==id);
		return info;
	}

	gapr::archive _repo;

		template<gapr::delta_type Typ> static bool do_prepare(gather_model& model, gapr::delta<Typ>&& delta);
		static void do_prepare1(gather_model& model, const gapr::commit_info& info, std::streambuf& fs);
		static bool do_prepare2(gather_model& model, gapr::delta_type type, std::streambuf& str, std::unique_lock<std::mutex>& lck);



	static void do_start(gather_model& model, std::size_t n_nodes, std::size_t n_links, std::size_t n_props) {
		auto rehash=[](auto& h, std::size_t n_add, std::size_t n_min) {
			auto n_new=h.size()+n_add+7;
			if(n_new<n_min)
				n_new=n_min;
			if(n_new>h.max_load_factor()*h.bucket_count()) {
				h.rehash(std::ceil(n_new*1.618/h.max_load_factor()));
				gapr::print("rehashed: ", h.size(), '/', h.bucket_count());
			}
		};
		// 12.52% (799,701,808B) 0x45579A: gather_model::PRIV::do_start(gather_model&, unsigned long, unsigned long, unsigned long) (model.cc:217)
		rehash(model._nodes, n_nodes, 10'000);
		// OOO 12.52% (799,535,784B) 0x455B07: gather_model::PRIV::do_start(gather_model&, unsigned long, unsigned long, unsigned long) (model.cc:218)
		rehash(model._links, n_links, 10'000);
		rehash(model._props, n_props, 1'000);
		model._nid_alloc=model._num_nodes+1;
	}
	using prop_key=gather_model::prop_key;
	using link_key=gather_model::link_key;
	using node_key=gather_model::node_key;
	using prop_mod=gather_model::prop_mod;
	using link_mod=gather_model::link_mod;
	using node_mod=gather_model::node_mod;
	using nref_mod=gather_model::nref_mod;
	using node_val=gather_model::node_val;

	static bool is_valid(const prop_key& key) {
		//assert(valid_names.has(key.second());
		//XXX check valid names???
		return key.node && !key.key.empty();
	}
	static bool is_valid(link_key key) {
		return key.nodes[0]>key.nodes[1] && key.nodes[1];
	}
	static bool is_valid(node_key key) {
		return key && true;
	}
	static void do_unref_(gather_model& model, node_key key, unsigned int v) {
		auto it=model._nodes.find(key);
		assert(it!=model._nodes.end());
		assert(it->second.nref>=v);
		auto [it2, ins2]=model._nodes_nref.emplace(key, nref_mod{it, it->second.nref-v});
		if(!ins2) {
			assert(it2->second.nref>=v);
			it2->second.nref-=v;
		}
	}
	static bool do_rm_prop(gather_model& model, prop_key&& key) {
		assert(is_valid(key));
		auto [it2, ins2]=model._props_mod.emplace(std::move(key), prop_mod{PRE_DEL, model._props.end(), {}});
		if(!ins2)
			return false;
		auto it=model._props.find(it2->first);
		if(it==model._props.end())
			return false;
		it2->second.iter=it;

		do_unref_(model, it2->first.node, PROP_REFC);
		return true;
	}
	static bool do_rm_link(gather_model& model, link_key key) {
		assert(is_valid(key));
		auto [it2, ins2]=model._links_mod.emplace(key, link_mod{PRE_DEL, model._links.end(), gapr::misc_attr{}});
		if(!ins2)
			return false;
		auto it=model._links.find(it2->first);
		if(it==model._links.end())
			return false;
		it2->second.iter=it;

		do_unref_(model, it2->first.nodes[0], 1);
		do_unref_(model, it2->first.nodes[1], 1);
		return true;
	}
	static bool do_rm_node(gather_model& model, node_key key) {
		assert(is_valid(key));
		auto [it2, ins2]=model._nodes_mod.emplace(key, node_mod{PRE_DEL, model._nodes.end(), gapr::node_attr{}});
		if(!ins2)
			return false;
		auto it=model._nodes.find(it2->first);
		if(it==model._nodes.end())
			return false;
		it2->second.iter=it;

		auto it3=model._nodes_nref.find(it2->first);
		if(it3==model._nodes_nref.end()) {
			auto it4=model._nodes.find(it2->first);
			assert(it4!=model._nodes.end());
			if(it4->second.nref!=0)
				return false;
		} else {
			if(it3->second.nref!=0)
				return false;
			it3->second.iter=model._nodes.end();
		}
		return true;
	}
	//XXX use chg mask to do partial modification.
	static bool do_chg_node(gather_model& model, node_key key, gapr::node_attr chg) {
		assert(is_valid(key));
		auto [it2, ins2]=model._nodes_mod.emplace(key, node_mod{PRE_CHG, model._nodes.end(), chg});
		if(!ins2)
			return false;
		auto it=model._nodes.find(it2->first);
		if(it==model._nodes.end())
			return false;
		it2->second.iter=it;
		return true;
	}
	static bool do_chg_link(gather_model& model, link_key key, gapr::misc_attr chg) {
		assert(is_valid(key));
		auto [it2, ins2]=model._links_mod.emplace(key, link_mod{PRE_CHG, model._links.end(), chg});
		if(!ins2)
			return false;
		auto it=model._links.find(it2->first);
		if(it==model._links.end())
			return false;
		it2->second.iter=it;

		auto it_node0=model._nodes.find(key.nodes[0]);
		assert(it_node0!=model._nodes.end());
		(void)it_node0;
		auto it_node1=model._nodes.find(key.nodes[1]);
		assert(it_node1!=model._nodes.end());
		(void)it_node1;
		return true;
	}
	static bool do_chg_prop(gather_model& model, prop_key&& key, std::string&& chg) {
		assert(is_valid(key));
		auto [it2, ins2]=model._props_mod.emplace(std::move(key), prop_mod{PRE_CHG, model._props.end(), std::move(chg)});
		if(!ins2)
			return false;
		auto it=model._props.find(it2->first);
		if(it==model._props.end())
			return false;
		it2->second.iter=it;

		auto it_node=model._nodes.find(it2->first.node);
		assert(it_node!=model._nodes.end());
		(void)it_node;
		return true;
	}

	static node_key do_add_node(gather_model& model, gapr::node_attr attr) {
		auto nid=model._nid_alloc++;
		//gapr::print("add node: ", nid);
		assert(is_valid(nid));
		auto [it2, ins2]=model._nodes_mod.emplace(nid, node_mod{PRE_ADD, model._nodes.end(), gapr::node_attr{}});
		if(!ins2)
			return {};
		// OOO 31.51% (2,012,348,416B) 0x45A7DD: gather_model::PRIV::do_add_node(gather_model&, gapr::node_attr) (model.cc:355)
		auto [it, ins]=model._nodes.emplace(it2->first, node_val{attr, 0});
		if(!ins)
			return {};
		it2->second.iter=it;

		return nid;
	}
	static void do_ref_(gather_model& model, node_key key, unsigned int v) {
		auto it=model._nodes.find(key);
		assert(it!=model._nodes.end());
		auto [it2, ins2]=model._nodes_nref.emplace(key, nref_mod{it, it->second.nref+v});
		if(!ins2)
			it2->second.nref+=v;
	}
	static bool do_add_link(gather_model& model, link_key key, gapr::misc_attr attr) {
		if(!is_valid(key)) {
			gapr::print("add link: ", key.nodes[0].data, ' ', key.nodes[1].data);
			std::swap(key.nodes[0], key.nodes[1]);
			//assert(0);
			//return true;
			//return false;
			//XXX
		}
		auto [it2, ins2]=model._links_mod.emplace(key, link_mod{PRE_ADD, model._links.end(), gapr::misc_attr{}});
		if(!ins2)
			return false;
		// OOO 23.55% (1,503,559,680B) 0x45A062: gather_model::PRIV::do_add_link(gather_model&, gapr::link_id, gapr::misc_attr) (model.cc:381)
		auto [it, ins]=model._links.emplace(it2->first, attr);
		if(!ins)
			return false;
		it2->second.iter=it;

		do_ref_(model, it2->first.nodes[0], 1);
		do_ref_(model, it2->first.nodes[1], 1);
		return true;
	}
	static bool do_add_prop(gather_model& model, prop_key&& key, std::string&& attr) {
		assert(is_valid(key));
		//XXX end() is not ok, or ok???
		auto [it2, ins2]=model._props_mod.emplace(std::move(key), prop_mod{PRE_ADD, model._props.end(), std::string{}});
		if(!ins2)
			return failure(false, "dup prop mod");
		auto [it, ins]=model._props.emplace(it2->first, std::move(attr));
		if(!ins)
			return failure(false, "dup prop");
		it2->second.iter=it;

		do_ref_(model, it2->first.node, PROP_REFC);
		return true;
	}
	static bool do_add_log(gather_model& model, std::string&& log) {
		model._logs.push_back(std::move(log));
		return true;
	}

	static void do_discard(gather_model& model) noexcept {
		auto erase=[](auto& h_mod, auto& h) noexcept {
			for(auto& [key, mod]: h_mod) {
				switch(mod.state) {
					case PRE_ADD:
						if(mod.iter!=h.end())
							h.erase(mod.iter);
						break;
					case PRE_CHG: case PRE_DEL:
						break;
					default:
						assert(0);
				}
			}
			clear_hash(h_mod);
		};
		erase(model._nodes_mod, model._nodes);
		erase(model._links_mod, model._links);
		erase(model._props_mod, model._props);
		clear_hash(model._nodes_nref);
		clear_hash(model._priv->lines);

		while(model._logs_prev<model._logs.size())
			model._logs.pop_back();
	}
	static void do_apply(gather_model& model) noexcept {
		auto apply=[](auto& h_mod, auto& h, auto chg) noexcept {
			for(auto& [key, mod]: h_mod) {
				switch(mod.state) {
					case PRE_ADD:
						break;
					case PRE_CHG:
						chg(mod);
						break;
					case PRE_DEL:
						h.erase(mod.iter);
						break;
					default:
						assert(0);
				}
			}
			clear_hash(h_mod);
		};
		apply(model._nodes_mod, model._nodes, [](node_mod& mod) noexcept {
			auto& attr=mod.iter->second.attr;
			//XXX delta or replace???
			//attr=attr+mod.val;
			attr=mod.val;
		});
		apply(model._links_mod, model._links, [](link_mod& mod) noexcept {
			auto& attr=mod.iter->second;
			attr=attr+mod.val;
		});
		apply(model._props_mod, model._props, [](prop_mod& mod) noexcept {
			mod.iter->second=std::move(mod.val);
		});

		for(auto& [key, mod]: model._nodes_nref) {
			if(mod.iter!=model._nodes.end())
				mod.iter->second.nref=mod.nref;
		}
		clear_hash(model._nodes_nref);
		clear_hash(model._priv->lines);
		model._num_nodes=model._nid_alloc-1;
	}
	static void do_save(gather_model::modifier& modif, std::string&& who);

#if 0
constexpr static std::size_t MAX_NID=(std::size_t{1}<<(sizeof(gapr::nid_t)*8))-1;
struct NodeElem {
	union {
		gapr::node_attr attr;
	} node;
	union {
		std::array<std::pair<gapr::nid_t, gapr::misc_attr>, 3> attrs_arr;
		std::vector<std::pair<gapr::nid_t, gapr::misc_attr> attrs_vec;
	} link;
	//fw_list<std::string> ...;
};
struct NodeArray: std::array<NodeElem, (MAX_NID+1)/N_ARRAY> {

}
#endif

static void save_swc(gather_model& model, std::ostream& str) {

}

std::unordered_set<LineInt> lines;
PRIV() { }

static void do_add_line(gather_model& model, node_key key1) {
	auto& n=model._nodes.at(key1);
	model._priv->lines.emplace(n.attr, n.attr);
}
static void do_add_line(gather_model& model, node_key key1, node_key key2) {
	auto& n1=model._nodes.at(key1);
	auto& n2=model._nodes.at(key2);
	model._priv->lines.emplace(n1.attr, n2.attr);
}

};

#ifdef _MSC_VER
template<typename T> inline static bool S_ISREG(T m) {
	return (m&_S_IFMT)==_S_IFREG;
}
template<typename T> inline static bool S_ISDIR(T m) {
	return (m&_S_IFMT)==_S_IFDIR;
}
#endif

gather_model::gather_model(std::string&& path):
	_path{std::move(path)}, _mtx{}, _num_nodes{0}, _nodes{},
	_priv{std::make_unique<PRIV>()}
{
	_nodes.max_load_factor(1/1.2);
	_links.max_load_factor(1/1.2);
	_props.max_load_factor(1/1.2);
	_nodes_mod.max_load_factor(1/1.2);
	_links_mod.max_load_factor(1/1.2);
	_props_mod.max_load_factor(1/1.2);
	_nodes_nref.max_load_factor(1/1.2);

	struct stat statbuf;
	if(stat(_path.c_str(), &statbuf)==-1) {
		if(errno!=ENOENT)
			gapr::report("stat() err: ", strerror(errno));

		_priv->_repo=gapr::archive{_path.c_str()};

		// XXX init in :...???
		_num_commits.store(0);
		_priv->spat_info_init(0);
		return;
	}
	if(!S_ISREG(statbuf.st_mode))
		gapr::report("repo dir error");
	// XXX perms???

	auto t0=std::chrono::steady_clock::now();
	auto repo=_priv->_repo=gapr::archive{_path.c_str()};
	uint64_t id=0;
	while(true) {
		std::array<char,32> fn_buf;
		auto sbuf=repo.reader_streambuf(gapr::to_string_lex(fn_buf, id));
		if(!sbuf)
			break;

		if(!sbuf)
			gapr::report("failed to open file");
		gapr::commit_info info;
		if(!info.load(*sbuf))
			gapr::report("commit file no commit info");
		if(info.id!=id)
			gapr::report("commit file wrong id");

		PRIV::do_prepare1(*this, info, *sbuf);

		PRIV::do_apply(*this);
		id++;
	}
	auto t1=std::chrono::steady_clock::now();
	gapr::print(_path, ": load time (n=", id, "): ", std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count(), "us");

	_num_commits.store(id);
	_priv->spat_info_init(id);
	//_num_nodes=...;
	//_num_arr=...;
}
gather_model::gather_model():
	_mtx{}, _num_nodes{0}, _nodes{},
	_priv{std::make_unique<PRIV>()}
{
	_nodes.max_load_factor(1/1.2);
	_links.max_load_factor(1/1.2);
	_props.max_load_factor(1/1.2);
	_nodes_mod.max_load_factor(1/1.2);
	_links_mod.max_load_factor(1/1.2);
	_props_mod.max_load_factor(1/1.2);
	_nodes_nref.max_load_factor(1/1.2);

	_num_commits.store(0);
	_priv->spat_info_init(-1);
}
gather_model::~gather_model() {
	// XXX
}

gather_model::modifier::~modifier() {
	if(_lck)
		PRIV::do_discard(_model);
}

uint64_t gather_model::modifier::prepare() {
	gapr::print("b4 prepare");
	auto strbuf=gapr::make_streambuf(gapr::mem_file{_payload});
	auto ret=PRIV::do_prepare2(_model, _type, *strbuf, _lck);
	if(!ret)
		return _model._num_commits.load();

	auto ncommits=_model._num_commits.load();
	_model._priv->spat_info_append(ncommits);

	_model._logs_prev=_model._logs.size();

	return 0;
}

std::tuple<gapr::node_id::data_type, uint64_t, uint64_t> gather_model::modifier::apply(std::string&& who, const gapr::commit_history& hist) noexcept {
	gapr::print("b4 apply");
	assert(!who.empty());
	assert(_lck);
	gapr::print("body_count: ", hist.body_count());
	if(hist.body_count()<_model._priv->spat_info_base())
		return {0, 1, _model._priv->spat_info_base()};

	uint64_t soft_coll{0};
	auto& tail=hist.tail();
	auto it=tail.begin();
	auto commit_id=_model._num_commits.load();
	unsigned int chk_cnt{0};
	for(std::size_t id=commit_id; id-->hist.body_count();) {
		if(it!=tail.end()) {
			assert(id>=*it);
			if(id==*it) {
				++it;
				continue;
			}
		}
		auto chk=_model._priv->spat_info_at(commit_id).check(_model._priv->spat_info_at(id));
		chk_cnt++;
		if(0)
			gapr::print("check against: ", id, ": ", chk);
		switch(chk) {
			case SOFT_COLL:
				if(id+1>soft_coll)
					soft_coll=id+1;
				break;
			case UNKNOWN_COLL:
			case HARD_COLL:
				gapr::print(1, "hard coll. check count: ", chk_cnt);
				gapr::print(id+1<=soft_coll?soft_coll:id+1, 1, 0);
				return {0, 1, id+1<=soft_coll?soft_coll:id+1};
			default:
				break;
		}
	}
	gapr::print(1, "check count: ", chk_cnt);

	try {
		PRIV::do_save(*this, std::move(who));
	} catch(...) {
		return {0, 1, soft_coll};
	}

	auto nid_alloc=_model._num_nodes+1;
	PRIV::do_apply(_model);
	// before unlock required.
	_model._num_commits.fetch_add(1);
	_lck.unlock();
	assert(nid_alloc);
	gapr::print("end apply", soft_coll, ':', commit_id, ':', nid_alloc);
	return {nid_alloc.data, commit_id, soft_coll};
}

void gather_model::PRIV::do_save(gather_model::modifier& modif, std::string&& who) {
	auto ts=gapr::to_timestamp(std::chrono::system_clock::now());
	auto& model=modif._model;
	auto commit_id=model._num_commits.load();
	auto nid0=model._num_nodes+1;
	gapr::commit_info info{commit_id, std::move(who), ts, nid0.data, static_cast<std::underlying_type_t<gapr::delta_type>>(modif._type)};

	std::array<char,32> fnbuf;
	auto repo=model._priv->_repo;

	auto ofs=repo.get_writer(gapr::to_string_lex(fnbuf, commit_id));
	if(!ofs)
		gapr::report("failed to open file");
	auto write=[&ofs](const char* ptr, std::size_t len) ->bool {
		while(len>0) {
			auto [buf, siz]=ofs.buffer();
			if(!buf)
				return false;
			if(siz>len)
				siz=len;
			std::memcpy(buf, ptr, siz);
			ofs.commit(siz);
			ptr+=siz;
			len-=siz;
		}
		return true;
	};
	{
		std::ostringstream oss;
		if(!info.save(*oss.rdbuf()))
			gapr::report("failed to save info");
		auto s=oss.str();
		if(!write(s.data(), s.size()))
			gapr::report("failed to save info 2");
	}
	gapr::print("saved info");

	std::size_t i=0;
	do {
		auto m=modif._payload.map(i);
		if(m.size()<=0)
			break;
		if(!write(m.data(), m.size()))
			gapr::report("failed to write delta");
		i+=m.size();
	} while(true);
	if(!ofs.flush())
		gapr::report("failed to close file");
}

template<>
bool gather_model::PRIV::do_prepare(gather_model& model, gapr::delta_add_edge_&& delta) {
	auto& nodes=delta.nodes;
	if(nodes.size()<2)
		return false;
	gapr::link_id left{delta.left};
	if(left.cannolize()!=left)
		return false;
	gapr::link_id right{delta.right};
	if(right.cannolize()!=right)
		return false;
	do_start(model, nodes.size(), nodes.size()+3, 0);
	gapr::node_id nid_cur;
	gapr::node_attr node_left{nodes.front()};
	if(left) {
		if(!left.on_node()) {
			if(node_left.misc.cannolize()!=node_left.misc)
				return false;
			//XXX link
			//do_rm_link(...);
			//do_add_node(...);
			//do_add_link();
			//do_add_link();
			//_priv->lines.add_3_lines;
			return false;
		} else {
			//node
			if(!node_left.at_origin())
				return false;
			if(node_left.misc.data!=0)
				return false;
			nid_cur=left.nodes[0];
			if(model._nodes.find(nid_cur)==model._nodes.end())
				return false;
		}
	} else {
		//none
		if(node_left.misc.cannolize()!=node_left.misc)
			return false;
		nid_cur=do_add_node(model, node_left);
		if(!nid_cur)
			return false;
	}
	for(std::size_t i=1; i+1<nodes.size(); i++) {
		gapr::node_attr node{nodes[i]};
		gapr::misc_attr misc{node.misc};
		if(misc.cannolize()!=misc)
			return false;
		auto nid=do_add_node(model, node);
		if(!nid)
			return false;
		if(!do_add_link(model, {gapr::node_id{nid}, gapr::node_id{nid_cur}}, misc))
			return false;
		do_add_line(model, nid, nid_cur);
		nid_cur=nid;
	}
	gapr::node_id nid_last;
	gapr::node_attr node_last{nodes.back()};
	if(right) {
		if(!right.on_node()) {
			if(node_last.misc.cannolize()!=node_last.misc)
				return false;
			//XXX
			//rm_link
			//add_node
			//add_links
			//add_last_link
			//_priv->lines.add_3_lines;
			return false;
		} else {
			//node
			if(!node_last.at_origin())
				return false;
			if(gapr::misc_attr{}!=node_last.misc)
				return false;
			nid_last=right.nodes[0];
			if(model._nodes.find(nid_last)==model._nodes.end())
				return false;
			if(nid_cur>nid_last) {
				nid_last=nid_cur;
				nid_cur=right.nodes[0];
			}
		}
	} else {
		//none
		if(node_last.misc.cannolize()!=node_last.misc)
			return false;
		nid_last=do_add_node(model, node_last);
		if(!nid_last)
			return false;
	}
	if(!do_add_link(model, {gapr::node_id{nid_last}, gapr::node_id{nid_cur}}, node_last.misc))
		return false;
	do_add_line(model, nid_last, nid_cur);
	return true;
}

template<>
bool gather_model::PRIV::do_prepare(gather_model& model, gapr::delta_add_prop_&& delta) {
	gapr::link_id link{delta.link};
	if(link.cannolize()!=link)
		return false;
	// XXX check delta.prop
	do_start(model, 1, 2, 1);
	gapr::node_id nid_cur;
	gapr::node_attr node{delta.node};
	if(link) {
		if(!link.on_node()) {
			if(node.misc.cannolize()!=node.misc)
				return false;
			//XXX link
			//do_rm_link(...);
			//do_add_node(...);
			//do_add_link();
			//do_add_link();
			//_priv->lines.add_3_liens;
			return false;
		} else {
			//node
			if(!node.at_origin())
				return false;
			if(node.misc.data!=0)
				return false;
			nid_cur=link.nodes[0];
			if(model._nodes.find(nid_cur)==model._nodes.end())
				return false;
			PRIV::do_add_line(model, nid_cur);
		}
	} else {
		//none
		if(node.misc.cannolize()!=node.misc)
			return false;
		nid_cur=do_add_node(model, node);
		if(!nid_cur)
			return false;
		PRIV::do_add_line(model, nid_cur);
	}

	auto i=delta.prop.find('=');
	std::string val{};
	if(i!=std::string::npos) {
		val.assign(delta.prop, i+1);
		delta.prop.erase(i);
	}

	gapr::prop_id id{gapr::node_id{nid_cur}, std::move(delta.prop)};
	if(!do_add_prop(model, std::move(id), std::move(val)))
		return false;

	return true;
}

template<>
bool gather_model::PRIV::do_prepare(gather_model& model, gapr::delta_chg_prop_&& delta) {
	if(delta.node==0)
		return false;
	// XXX check delta.prop

	do_start(model, 0, 0, 0);

	gapr::node_id nid_cur{delta.node};
	if(model._nodes.find(nid_cur)==model._nodes.end())
		return false;
	PRIV::do_add_line(model, nid_cur);

	auto i=delta.prop.find('=');
	std::string val{};
	if(i!=std::string::npos) {
		val.assign(delta.prop, i+1);
		delta.prop.erase(i);
	}

	gapr::prop_id id{gapr::node_id{nid_cur}, std::move(delta.prop)};
	if(!do_chg_prop(model, std::move(id), std::move(val)))
		return false;

	return true;
}

static int compare_tag(const std::string& a, const std::string& b) noexcept {
	for(std::size_t i=0; ; i++) {
		int aa=i<a.size()?a[i]:'=';
		int bb=i<b.size()?b[i]:'=';
		auto r=aa-bb;
		if(r!=0)
			return r;
		if(aa=='=')
			return 0;
	}
}
	bool is_key_val_valid(const std::string& str) {
		if(str.empty())
			return false;
		if(str[0]=='=')
			return false;
		//XXX check if key is valid name
		return true;
	}

template<>
bool gather_model::PRIV::do_prepare(gather_model& model, gapr::delta_add_patch_&& delta) {
		auto N=delta.nodes.size();
		if(N<1)
			return failure(false, "no nodes");
		do_start(model, N, 2*delta.links.size()+N-1, delta.props.size());
		std::size_t j=0;
		for(std::size_t i=0; i<delta.nodes.size(); i++) {
			auto par=delta.nodes[i].second;
			if(par>i)
				return failure(false, "wrong par order");
			gapr::link_id link{};
			if(j<delta.links.size()) {
				auto& linkj=delta.links[j];
				if(linkj.first<i+1)
					return failure(false, "wrong link order");
				if(linkj.first==i+1) {
					link=gapr::link_id{linkj.second};
					if(!link)
						return failure(false, "no link");
					if(link.cannolize().data()!=linkj.second)
						return failure(false, "invalid link");
					j++;
				}
			}
			gapr::node_id nid_cur;
			gapr::node_attr node{delta.nodes[i].first};
			bool has_node{true};
			if(link) {
				if(!link.on_node()) {
					//XXX link
					//do_rm_link(...);
					//do_add_node(...);
					//do_add_link();
					//do_add_link();
					//_priv->lines.add_3_lines;
					return failure(false, "no impl");
				} else {
					//node
					if(!node.at_origin())
						return failure(false, "unused pos");
					nid_cur=link.nodes[0];
					if(model._nodes.find(nid_cur)==model._nodes.end())
						return failure(false, "invalid node");
					has_node=false;
				}
			} else {
				//none
				nid_cur=do_add_node(model, node);
				if(!nid_cur)
					return failure(false, "failed to add node");
			}
			if((has_node?node.misc.cannolize():gapr::misc_attr{})!=node.misc)
				return failure(false, "unused attr");
			if(par) {
				gapr::node_id id1{delta.nodes[par-1].second};
				auto id0=nid_cur;
				if(id0<id1)
					std::swap(id0, id1);
				if(!do_add_link(model, {gapr::node_id{id0}, gapr::node_id{id1}}, node.misc))
					gapr::print("add link2: ", id0, ' ', id1);
				do_add_line(model, id0, id1);
			}
			delta.nodes[i].second=nid_cur.data;
		}
		if(j<delta.links.size())
			return failure(false, "unused links");

		std::size_t i;
		for(i=0; i<delta.props.size(); ++i) {
			auto id=delta.props[i].first;
			if(i>0) {
				auto& prev_prop=delta.props[i-1];
				if(id<prev_prop.first)
					return failure(false, "wrong prop order");
				if(id==prev_prop.first) {
					if(compare_tag(delta.props[i].second, prev_prop.second)<=0)
						return failure(false, "wrong prop order 2");
				}
			} else if(id==0)
				return failure(false, "wrong id");
			if(id==gapr::node_id::max().data)
				break;
			if(id>N)
				return failure(false, "wrong id");
			if(!is_key_val_valid(delta.props[i].second))
				return failure(false, "invalid prop");

			auto& prop=delta.props[i].second;
			std::string val{};
			if(auto i=prop.find('='); i!=std::string::npos) {
				val.assign(prop, i+1);
				prop.erase(i);
			}
			auto nid=delta.nodes[id-1].second;
			gapr::prop_id prop_id{gapr::node_id{nid}, std::string{prop}};
			if(!do_add_prop(model, std::move(prop_id), std::move(val)))
				return failure(false, "failed to add prop");
		}
		for(; i<delta.props.size(); ++i) {
			auto id=delta.props[i].first;
			if(id!=gapr::node_id::max().data)
				return failure(false, "invalid placeholder id");
			if(!is_key_val_valid(delta.props[i].second))
				return failure(false, "invalid log");
			if(!do_add_log(model, std::move(delta.props[i].second)))
				return failure(false, "failed to add log");
		};
		return true;
}

template<>
bool gather_model::PRIV::do_prepare(gather_model& model, gapr::delta_del_patch_&& delta) {
	do_start(model, 0, 0, 0);
	for(std::size_t i=0; i<delta.props.size(); i++) {
		gapr::node_id id{delta.props[i].first};
		if(i>0) {
			auto& prev_prop=delta.props[i-1];
			gapr::node_id prev_id{prev_prop.first};
			if(id<prev_id)
				return false;
			if(id==prev_id) {
				if(compare_tag(delta.props[i].second, prev_prop.second)<=0)
					return false;
			}
		} else if(!id)
			return false;
		if(!is_key_val_valid(delta.props[i].second))
			return false;
		auto& prop=delta.props[i].second;
		auto nid=id;
		gapr::prop_id prop_id{gapr::node_id{nid}, std::move(prop)};
		if(!do_rm_prop(model, std::move(prop_id)))
			return false;
		do_add_line(model, nid);
	}
	if(delta.nodes.empty())
		return true;

	std::size_t i=0;
	enum {
		Start,
		Open,
		Inside,
	} state=Start;
	gapr::node_id id_prev;
	bool dup_open, dup_close;
	do {
		gapr::node_id id{delta.nodes[i]};
		switch(state) {
			case Start:
				if(!id)
					return false;
				state=Open;
				id_prev=id;
				dup_open=false;
				dup_close=false;
				break;
			case Open:
				if(!id) {
					if(dup_open)
						return false;
					if(!do_rm_node(model, id_prev))
						return false;
					do_add_line(model, id_prev);
					state=Start;
				} else if(id==id_prev) {
					if(dup_open)
						return false;
					dup_open=true;
				} else {
					auto id1=id, id2=id_prev;
					if(id1<id2)
						std::swap(id1, id2);
					if(!do_rm_link(model, {gapr::node_id{id1}, gapr::node_id{id2}}))
						return false;
					do_add_line(model, id, id_prev);
					if(dup_open) {
						if(!do_rm_node(model, id_prev))
							return false;
					}
					state=Inside;
					id_prev=id;
				}
				break;
			case Inside:
				if(!id) {
					state=Start;
				} else if(id==id_prev) {
					if(!do_rm_node(model, id_prev))
						return false;
					do_add_line(model, id_prev);
					state=Start;
					dup_close=true;
				} else {
					auto id1=id, id2=id_prev;
					if(id1<id2)
						std::swap(id1, id2);
					if(!do_rm_link(model, {gapr::node_id{id1}, gapr::node_id{id2}}))
						return false;
					do_add_line(model, id, id_prev);
					if(!do_rm_node(model, id_prev))
						return false;
					id_prev=id;
				}
				break;
		}
		i++;
	} while(i<delta.nodes.size());
	switch(state) {
		case Start:
			if(!dup_close)
				return false;
			break;
		case Open:
			if(dup_open)
				return false;
			if(!do_rm_node(model, id_prev))
				return false;
			do_add_line(model, id_prev);
			break;
		case Inside:
			break;
	}
	return true;
}

template<>
bool gather_model::PRIV::do_prepare(gather_model& model, gapr::delta_proofread_&& delta) {
	do_start(model, 0, 0, 0);

	for(std::size_t i=0; i<delta.nodes.size(); i++) {
		gapr::node_id id{delta.nodes[i]};
		if(!id)
			return false;
		// XXX dup check
		auto it=model._nodes.find(id);
		if(it==model._nodes.end())
			return false;
		auto chg=it->second.attr;
		if(chg.misc.coverage())
			return false;
		chg.misc.coverage(true);
		if(!do_chg_node(model, id, chg))
			return false;
		//XXX add line or point?
		do_add_line(model, id);
	}
	return true;
}

template<>
bool gather_model::PRIV::do_prepare(gather_model& model, gapr::delta_reset_proofread_&& delta) {
	do_start(model, 0, 0, 0);
	for(std::size_t i=0; i<delta.props.size(); i++) {
		gapr::node_id id{delta.props[i].first};
		if(i>0) {
			auto& prev_prop=delta.props[i-1];
			gapr::node_id prev_id{prev_prop.first};
			if(id<prev_id)
				return failure(false, "wrong order");
			if(id==prev_id) {
				if(compare_tag(delta.props[i].second, prev_prop.second)<=0)
					return failure(false, "wrong order 2");
			}
		} else if(!id)
			return failure(false, "zero id");
		if(!is_key_val_valid(delta.props[i].second))
			return failure(false, "invalid keyval");
		auto& prop=delta.props[i].second;
		auto nid=id;
		//XXX bugs like this elsewhere
		gapr::prop_id prop_id{gapr::node_id{nid}, std::string{prop}};
		if(!do_rm_prop(model, std::move(prop_id)))
			return failure(false, "failed to del prop");
		do_add_line(model, nid);
	}
	AscendingSequenceDecoder dec{delta.nodes};
	while(true) {
		auto [id_, end]=dec.next();
		gapr::node_id id{id_};
		if(end)
			break;
		if(!id)
			return failure(false, "zero id");
		// XXX dup check
		auto it=model._nodes.find(id);
		if(it==model._nodes.end())
			return failure(false, "no such node");
		auto chg=it->second.attr;
		if(!chg.misc.coverage())
			return failure(false, "noop");
		chg.misc.coverage(false);
		if(!do_chg_node(model, id, chg))
			return failure(false, "failed to change node");
		//XXX add line or point?
		do_add_line(model, id);
	}
	return true;
}
template<>
bool gather_model::PRIV::do_prepare(gather_model& model, gapr::delta_reset_proofread_0_&& delta) {
	return do_prepare(model, upgrade_delta(std::move(delta)));
}

gapr::mem_file gather_model::helper::dump_state() {
	gapr::model_state st;
	const auto& model=_model;
	std::unordered_multimap<gapr::node_id, gapr::node_id> adjs;
	std::unordered_set<gapr::node_id> verts;

	gapr::print(1, "begin dump");
	_lck.lock();
	st.size=model._num_commits;
	for(auto& [key, val]: model._props) {
		auto node=key.node;
		if(model._nodes.find(node)==model._nodes.end()) {
			gapr::print("dead prop: ", node, ':', key.key);
			continue;
		}
		//  check key valid
		std::string s;
		s.reserve(key.key.size()+1+val.size());
		s+=key.key;
		s+='=';
		s+=val;
		st.props.emplace_back(node.data, std::move(s));
	}
	for(auto& log: model._logs)
		st.logs.emplace_back(log);
	// OOO 02.25% (143,688,632B) 0x447C1B: gather_model::helper::dump_state() (model.cc:1254)
	adjs.reserve(2*model._links.size());
	for(auto [l_, lattr]: model._links) {
		auto l=l_;
		if(l.nodes[0]==l.nodes[1]) {
			gapr::print("skip loop: ", l);
			continue;
		}
		if(l.nodes[0]<l.nodes[1]) {
			gapr::print("wrong link order: ", l);
			std::swap(l.nodes[0], l.nodes[1]);
			if(model._links.find(l)!=model._links.end()) {
				gapr::print("skip dup: ", l);
				continue;
			}
		}
		if(model._nodes.find(l.nodes[0])==model._nodes.end()) {
			gapr::print("dead link: ", l);
			continue;
		}
		if(model._nodes.find(l.nodes[1])==model._nodes.end()) {
			gapr::print("dead link: ", l);
			continue;
		}
		//check attr
		adjs.emplace(l.nodes[0], l.nodes[1]);
		adjs.emplace(l.nodes[1], l.nodes[0]);
	}
	for(auto& [nid, attr]: model._nodes) {
		if(!nid) {
			gapr::print("invalid node: ", nid);
			continue;
		}
		//check attr

		if(adjs.find(gapr::node_id{nid})==adjs.end()) {
			gapr::model_state::edge edg{{gapr::node_id{nid}}, {attr.attr.data()}};
			st.edges.emplace_back(std::move(edg));
		}
	}
	{
		gapr::node_id prev{};
		unsigned int cnt{0};
		for(auto [a, b]: adjs) {
			if(a==prev) {
				cnt++;
				continue;
			}
			if(prev) {
				if(cnt!=2)
					verts.emplace(prev);
			}
			prev=a;
			cnt=1;
		}
		if(prev) {
			if(cnt!=2)
				verts.emplace(prev);
		}
	}

	std::vector<gapr::node_id> path;
	auto add_edge=[&path,&model,&st](std::size_t i0, std::size_t i1) {
		gapr::model_state::edge edg{};
		edg.nodes.reserve(i1-i0);
		edg.points.reserve(i1-i0);
		for(std::size_t i=i0; i<i1; i++) {
			auto id=path[i];
			edg.nodes.push_back(id);
			auto a=model._nodes.at(id).attr;
			a.misc=a.misc.cannolize();
			if(i>i0) {
				gapr::link_id l{id, path[i-1]};
				auto it=model._links.find(l);
				if(it==model._links.end()) {
					std::swap(l.nodes[0], l.nodes[1]);
					it=model._links.find(l);
					assert(it!=model._links.end());
				}
				a.misc|=gapr::misc_attr{};
			}
			edg.points.push_back(a.data());
		}
		st.edges.emplace_back(std::move(edg));
	};
	auto extend_path=[&path,&adjs,&verts](gapr::node_id start, gapr::node_id b) {
		auto a=start;
		do {
			path.push_back(b);
			//gapr::print("path: ", b.data);
			auto [it1, it2]=adjs.equal_range(b);
			auto it_del=it2;
			auto it_del2=it2;
			unsigned int eq_hits=0;
			unsigned int ne_hits=0;
			for(auto it=it1; it!=it2; ++it) {
				if(it->second==a) {
					eq_hits++;
					it_del=it;
				} else {
					ne_hits++;
					it_del2=it;
				}
			}
			if(eq_hits!=1) {
				gapr::print("eq_hits: ", eq_hits, ',', a.data);
				for(auto it=it1; it!=it2; ++it)
					gapr::print("eq_hits: ", it->first.data, ':', it->second.data);
				assert(eq_hits==1);
			}
			adjs.erase(it_del);
			if(ne_hits!=1 || verts.find(b)!=verts.end())
				break;
			auto c=it_del2->second;
			if(c==start)
				break;
			adjs.erase(it_del2);
			a=b;
			b=c;
		} while(true);
		//gapr::print("path: ", "end");
	};
	while(!adjs.empty()) {
		auto it=adjs.begin();
		auto [a, b]=*it;
		//gapr::print("seed: ", a.data, ',', b.data);
		assert(a!=b);
		extend_path(a, b);
		std::reverse(path.begin(), path.end());
		extend_path(b, a);
		if(path.front()==path.back()) {
			assert(path.size()>=4);
			auto i=path.size()/2;
			add_edge(0, i);
			add_edge(i-1, path.size());
		} else {
			// OOO 02.16% (138,088,432B) 0x448B1B: gather_model::helper::dump_state() (model.cc:1389)
			add_edge(0, path.size());
		}
		path.clear();
	}
	_lck.unlock();

	std::sort(st.props.begin(), st.props.end());
	{
#if 0
		st.size=7;
		st.edges.clear();
		st.props.clear();
		for(unsigned i=1; i<5; i++) {
			gapr::model_state::edge edg;
			for(unsigned int j=0; j<i; j++) {
				edg.nodes.push_back(gapr::node_id{j});
				gapr::node_attr a{};
				a.misc.data=i;
				edg.points.push_back(a.data());
			}
			st.edges.push_back(std::move(edg));
			st.props.emplace_back(i, "a=b");
		}
		gapr::print("st sz: ", st.edges.size());
		int i=0;
		for(auto& edg: st.edges) {
			gapr::print("edge sizes: ", edg.nodes.size(), ':', edg.points.size());
			if(i++>20)
				break;
		}
#endif
	}

	gapr::mutable_mem_file file{true};
	std::ostringstream oss;
	// OOO 02.10% (134,217,729B) 0x4486BC: gather_model::helper::dump_state() (model.cc:1424)
	if(!st.save(*oss.rdbuf()))
		gapr::report("failed to save delta");
	/////////////////////////////////////////////////
	auto str=oss.str();
	if(0) {
		std::ofstream fs{model._path+"-state"};
		if(!st.save(*fs.rdbuf()))
			gapr::report("failed to save");
		fs.close();
	}
	std::size_t i=0;
	while(i<str.size()) {
		// OOO 08.62% (550,207,488B) 0x448702: gather_model::helper::dump_state() (model.cc:1436)
		auto buf=file.map_tail();
		auto n=str.size()-i;
		if(n>buf.size())
			n=buf.size();
		std::copy(&str[i], &str[i+n], buf.data());
		i+=n;
		file.add_tail(n);
	}
	gapr::print(1, "end dump");

	gapr::print(1, "before trim");
	//malloc_stats();
	//malloc_trim(0);
	gapr::print(1, "after trim");
	//malloc_stats();

	return file;
}
gather_model::helper::~helper() { }

template<gapr::delta_type Typ>
bool dump_special(std::ostream& str, const gapr::delta<Typ>& delta, const gapr::commit_info& info) {
	if constexpr(Typ==gapr::delta_type::add_edge_) {
		if(info.who==".........") {
			str<<'\n';
			auto k=info.nid0;
			gapr::node_id prev;
			if(gapr::link_id ll{delta.left}; ll) {
				prev=ll.nodes[0];
			} else {
				str<<"addnode: "<<k<<'\n';
				prev.data=k++;
			}
			for(unsigned int i=1; i+1<delta.nodes.size(); i++) {
				str<<"addlink: "<<prev.data<<'-'<<k<<'\n';
				str<<"addnode: "<<k<<'\n';
				prev.data=k++;
			}
			if(gapr::link_id rr{delta.right}; rr) {
				str<<"addlink: "<<prev.data<<'-'<<rr.nodes[0].data<<'\n';
			} else {
				str<<"addlink: "<<prev.data<<'-'<<k<<'\n';
				str<<"addnode: "<<k<<'\n';
			}
			return true;
		}
	}
	return false;
}
std::string gather_model::get_stats(uint64_t from) const {
	std::ostringstream oss_{};
	boost::iostreams::filtering_streambuf<boost::iostreams::output> oss_buf{};
	oss_buf.push(boost::iostreams::gzip_compressor{4});
	oss_buf.push(oss_);
	std::ostream oss{&oss_buf};
	auto to=_num_commits.load();
	oss<<"Stats: ["<<from<<", "<<to<<")\n";
	auto repo=_priv->_repo;
	for(auto id=from; id<to; id++) {
		std::array<char,32> fn_buf;
		auto sbuf=repo.reader_streambuf(gapr::to_string_lex(fn_buf, id));
		if(!sbuf)
			gapr::report("failed to open file");

		gapr::commit_info info;
		if(!info.load(*sbuf))
			gapr::report("commit file no commit info");
		oss<<"Commit: "<<info<<": ";
		gapr::delta_variant::visit<void>(gapr::delta_type{info.type},
				[sbuf=std::move(sbuf),&info,&oss](auto typ) {
					gapr::delta<typ> delta;
					if(!gapr::load(delta, *sbuf))
						gapr::report("failed to load delta");
					if(dump_special(oss, delta, info))
						return;
					dump(delta, oss, 1, gapr::node_id{info.nid0});
				});
		oss.flush();
	}
	oss_buf.reset();
	return oss_.str();
}

std::string gather_model::get_proofread_stats(gapr::commit_id from, gapr::commit_id to_) const {
	auto to=_num_commits.load();
	if(to_.data>0) {
		if(to>to_.data)
			to=to_.data;
	}
	auto repo=_priv->_repo;
	std::unordered_map<gapr::node_id, std::string> err2typ;
	std::unordered_map<gapr::node_id, std::string> err2rep;
	std::unordered_map<std::string, unsigned int> usr2pr;
	std::vector<std::pair<gapr::node_id, std::string>> duprep;
	for(auto id=from.data; id<to; id++) {
		std::array<char,32> fn_buf;
		auto sbuf=repo.reader_streambuf(gapr::to_string_lex(fn_buf, id));
		if(!sbuf)
			gapr::report("failed to open file");

		gapr::commit_info info;
		if(!info.load(*sbuf))
			gapr::report("commit file no commit info");
		gapr::delta_variant::visit<void>(gapr::delta_type{info.type},
				[sbuf=std::move(sbuf),&info,&usr2pr,&err2rep,&err2typ,&duprep](auto typ) {
					gapr::delta<typ> delta;
					if(!gapr::load(delta, *sbuf))
						gapr::report("failed to load delta");
					if constexpr(typ==gapr::delta_type::proofread_) {
						auto [it, ins]=usr2pr.emplace(info.who, 0);
						it->second+=delta.nodes.size();
					} else if constexpr(typ==gapr::delta_type::add_prop_) {
						auto n=delta.prop.find('=');
						if(n==delta.prop.npos) {
							if(delta.prop!="error")
								return;
						} else {
							if(delta.prop.compare(0, n, "error")!=0)
								return;
						}
						gapr::link_id link{delta.link};
						if(!link.nodes[0])
							link.nodes[0]=gapr::node_id{info.nid0};
						auto [it, ins]=err2rep.emplace(link.nodes[0], "");
						if(!ins)
							duprep.emplace_back(it->first, it->second);
						it->second=info.who;
					} else if constexpr(typ==gapr::delta_type::chg_prop_) {
						std::string res;
						auto n=delta.prop.find('=');
						if(n==delta.prop.npos) {
							if(delta.prop!="error")
								return;
							res="=";
						} else {
							if(delta.prop.compare(0, n, "error")!=0)
								return;
							res=delta.prop.substr(n);
						}
						//die "no rep!\n" if(not defined($err2rep{$eid}));
						auto [it, ins]=err2typ.emplace(delta.node, "");
						if(!ins) {
							//$err2typ{$eid}.=" $etyp($usr)";
						}
						it->second.reserve(res.size()+info.who.size()+2);
						it->second=res;
						it->second+='(';
						it->second+=info.who;
						it->second+=')';
					}
				});
	}

	std::ostringstream oss;
	oss<<"{\"from\":"<<from.data<<",\"to\":"<<to;
	if(!duprep.empty()) {
		oss<<",\"dupreps\"=[";
		for(std::size_t i=0; i<duprep.size(); ++i) {
			if(i>0)
				oss<<',';
			oss<<"{\"n\":"<<duprep[i].first.data;
			oss<<",\"u\":\""<<duprep[i].second<<"\"}";
		}
		oss<<"]";
	}
	std::vector<std::string> names;
	for(auto& [k, v]: usr2pr)
		names.push_back(k);
	std::sort(names.begin(), names.end());
	oss<<",\"stats\":[";
	unsigned int iii=0;
	for(auto& usr: names) {
		unsigned int tot{0};
		std::unordered_map<std::string, unsigned int> stats;
		for(auto& [k, v]: err2rep) {
			if(v==usr) {
				std::string typ;
				if(auto it=err2typ.find(k); it!=err2typ.end()) {
					typ=it->second;
				} else {
					typ="unresolved";
				}
				auto [it, ins]=stats.emplace(typ, 0);
				it->second+=1;
				tot+=1;
			}
		}
		std::vector<decltype(&*stats.begin())> ptrs;
		for(auto& it: stats)
			ptrs.push_back(&it);
		std::sort(ptrs.begin(), ptrs.end(), [](auto* a, auto* b) {
			return a->second>b->second;
		});
		if(iii++>0)
			oss<<',';
		oss<<"{\"u\":\""<<usr<<"\",\"nn\":"<<usr2pr.at(usr);
		oss<<",\"ne\":"<<tot<<",\"d\":[";
		for(unsigned int i=0; i<ptrs.size(); ++i) {
			if(i>0)
				oss<<',';
			oss<<"{\"n\":"<<ptrs[i]->second<<",\"t\":\"";
			oss<<ptrs[i]->first;
			oss<<"\"}";
		}
		oss<<"]}";
	}
	oss<<"]}";
	return oss.str();
}

#include "gapr/detail/delta-stats.hh"

void gather_model::update_stats(void* prev_, uint32_t& nn, uint32_t& nnr, uint32_t& nt, uint32_t& ntr, uint64_t& nc) const {
	auto& prev=*static_cast<std::vector<gather_stats::CommitInfo>*>(prev_);
	uint64_t from=0;
	if(!prev.empty())
		from=prev.back().id+1;
	auto to=_num_commits.load();
	auto repo=_priv->_repo;
	for(auto id=from; id<to; id++) {
		std::array<char,32> fn_buf;
		auto sbuf=repo.reader_streambuf(gapr::to_string_lex(fn_buf, id));
		if(!sbuf)
			gapr::report("failed to open file");

		gather_stats::CommitInfo info;
		if(!info.load(*sbuf))
			gapr::report("commit file no commit info");
		gapr::delta_variant::visit<void>(gapr::delta_type{info.type},
				[sbuf=std::move(sbuf),&info](auto typ) {
					gapr::delta<typ> delta;
					if(!gapr::load(delta, *sbuf))
						gapr::report("failed to load delta");
					DeltaStats stats;
					stats.add(delta);
					info.score=stats.score();
					info.score_pr+=stats.score_pr();
					info.score_rep+=stats.score_rep();
					if(0)
						std::cout<<stats;
				});

		prev.emplace_back(std::move(info));
	}

	std::lock_guard<std::mutex> lck{_mtx};
	nc=_num_commits;
	nt=0;
	ntr=0;
	for(auto& [key, val]: _props) {
		if(key.key=="error") {
			//num err
			//num resolved
		}
		if(key.key=="root") {
			auto& nd=_nodes.at(key.node);
			if(nd.nref%PROP_REFC>=2) {
				nt++;
				if(_props.find({key.node, "state"})==_props.end())
					ntr++;
			}
		}
	}
	nn=_nodes.size();
	nnr=0;
	for(auto& [nid, val]: _nodes) {
		if(!val.attr.misc.coverage())
			nnr++;
		if(val.nref%PROP_REFC<2) {
			nt++;
			if(_props.find({nid, "state"})==_props.end())
				ntr++;
		}
	}
}
void gather_stats::update(const std::string& grp, const gather_model& mdl) {
	std::lock_guard lck{_mtx_int};
	auto [it, ins]=_infos.emplace(grp, Info{});
	if(ins)
		it->second.stats.name=grp;
	auto& st=it->second.stats;
	mdl.update_stats(&it->second.commits, st.num_nodes, st.num_nodes_raw, st.num_terms, st.num_terms_raw, st.num_commits);
}


void gather_stats::update_end() {
	std::unordered_map<std::string, PerUser> users;
	std::unordered_map<std::string, PerGrp> groups;
	std::vector<PerGrp*> idxes_grp;
	{
		auto t=std::time(nullptr);
		auto tm0=*std::localtime(&t);
		tm0.tm_hour=0;
		tm0.tm_min=0;
		tm0.tm_sec=0;
		auto day0=gapr::to_timestamp(std::chrono::system_clock::from_time_t(std::mktime(&tm0)));
		tm0.tm_mday=1;
		auto mon0=gapr::to_timestamp(std::chrono::system_clock::from_time_t(std::mktime(&tm0)));
		auto ts=gapr::to_timestamp(std::chrono::system_clock::from_time_t(t));
		std::lock_guard lck{_mtx_int};
		_stat_ts=ts;
		for(auto& [grp, info]: _infos) {
			auto& pg=info.stats;
			auto& hist=info.commits;
			pg.num_commits_d=0;
			pg.num_commits_m=0;
			for(std::size_t i=hist.size(); i-->0;) {
				auto& c=hist[i];
				if(c.when<mon0)
					break;
				auto [it, ins]=users.emplace(c.who, PerUser{});
				auto dayscore=c.when<day0?0.0:c.score;
				auto dayscore_pr=c.when<day0?0:c.score_pr;
				auto dayscore_rep=c.when<day0?0:c.score_rep;
				if(ins) {
					it->second.name=c.who;
					it->second.score_d=dayscore;
					it->second.score_d_pr=dayscore_pr;
					it->second.score_d_rep=dayscore_rep;
					it->second.score_m=c.score;
				} else {
					it->second.score_d+=dayscore;
					it->second.score_d_pr+=dayscore_pr;
					it->second.score_d_rep+=dayscore_rep;
					it->second.score_m+=c.score;
				}
				pg.num_commits_m++;
				pg.num_commits_d+=(c.when<day0?0:1);
			}
			groups.emplace(grp, pg);
		}
	}
	std::vector<PerUser*> idxes;
	for(auto& [u, v]: users)
		idxes.push_back(&v);
	for(auto& [g, v]: groups)
		idxes_grp.push_back(&v);
	std::sort(idxes.begin(), idxes.end(), [](PerUser* a, PerUser* b) {
		return a->score_m>b->score_m;
	});
	for(std::size_t i=0; i<idxes.size(); i++) {
		if(i!=0 && idxes[i]->score_m==idxes[i-1]->score_m)
			idxes[i]->rank_m=idxes[i-1]->rank_m;
		else
			idxes[i]->rank_m=i+1;
	}
	std::sort(idxes.begin(), idxes.end(), [](PerUser* a, PerUser* b) {
		if(a->score_d>b->score_d)
			return true;
		if(a->score_d<b->score_d)
			return false;
		return a->score_m>b->score_m;
	});
	for(std::size_t i=0; i<idxes.size(); i++) {
		if(idxes[i]->score_d==0) {
			idxes[i]->rank_d=0;
			continue;
		}
		if(i!=0 && idxes[i]->score_d==idxes[i-1]->score_d)
			idxes[i]->rank_d=idxes[i-1]->rank_d;
		else
			idxes[i]->rank_d=i+1;
	}
	std::sort(idxes_grp.begin(), idxes_grp.end(), [](PerGrp* a, PerGrp* b) {
		if(a->num_commits_d>b->num_commits_d)
			return true;
		if(a->num_commits_d<b->num_commits_d)
			return false;
		if(a->num_commits_m>b->num_commits_m)
			return true;
		if(a->num_commits_m<b->num_commits_m)
			return false;
		return a->name>b->name;
	});

	std::lock_guard lck{_mtx_out};
	_per_user.clear();
	_per_grp.clear();
	for(auto p: idxes)
		_per_user.emplace_back(std::move(*p));
	for(auto p: idxes_grp)
		_per_grp.emplace_back(std::move(*p));
}


/*! after templ. inst. */
void gather_model::PRIV::do_prepare1(gather_model& model, const gapr::commit_info& info, std::streambuf& fs) {
	gapr::node_id nid0{info.nid0};
	if(nid0!=model._num_nodes+1) {
		if(nid0<model._num_nodes+1)
			gapr::report("commit file wrong nid0: ", info.nid0, ':', model._num_nodes+1);
		gapr::print(1, "commit file wrong nid0: ", info.nid0, "<-", model._num_nodes+1);
		model._num_nodes=nid0-1;
	}
	gapr::delta_variant::visit<void>(gapr::delta_type{info.type},
			[&model,&fs](auto typ) {
				gapr::delta<typ> delta;
				if(!gapr::load(delta, fs))
					gapr::report("failed to load delta");
				if(!do_prepare(model, std::move(delta)))
					gapr::report("failed to prepare");
			});
}
bool gather_model::PRIV::do_prepare2(gather_model& model, gapr::delta_type type, std::streambuf& str, std::unique_lock<std::mutex>& lck) {
	return gapr::delta_variant::visit<bool>(type,
			[&model,&lck,&str](auto typ) {
				gapr::delta<typ> delta;
				if(!gapr::load(delta, str))
					gapr::report("failed to load delta");
				lck.lock();
				auto r=PRIV::do_prepare(model, std::move(delta));
				// XXX if client has all commits, and r==false, then bug
				return r;
			});
}

std::unique_ptr<std::streambuf> gather_model::get_commit(uint32_t id) const {
	auto& repo=_priv->_repo;
	std::array<char,32> fn_buf;
	return repo.reader_streambuf(gapr::to_string_lex(fn_buf, id));
}

void gather_model::xxx_prepare(const gapr::commit_info& info, std::streambuf& buf) {
	return PRIV::do_prepare1(*this, info, buf);
}
void gather_model::xxx_apply() {
	return PRIV::do_apply(*this);
}
void gather_model::peek_nodes_mod(std::vector<std::tuple<gapr::node_id, gapr::node_attr, int>>& out) const {
	for(auto& [key, mod]: _nodes_mod) {
		switch(mod.state) {
			case PRIV::PRE_ADD:
				assert(mod.iter!=_nodes.end());
				out.emplace_back(key, mod.iter->second.attr, 1);
				break;
			case PRIV::PRE_DEL:
				out.emplace_back(key, gapr::node_attr{}, -1);
				break;
			case PRIV::PRE_CHG:
				out.emplace_back(key, mod.val, 0);
				break;
		}
	}
}
void gather_model::peek_links_mod(std::vector<std::tuple<gapr::node_id, gapr::node_id, int>>& out) const {
	for(auto& [key, mod]: _links_mod) {
		switch(mod.state) {
			case PRIV::PRE_ADD:
				assert(mod.iter!=_links.end());
				out.emplace_back(key.nodes[0], key.nodes[1], 1);
				break;
			case PRIV::PRE_DEL:
				out.emplace_back(key.nodes[0], key.nodes[1], -1);
				break;
			case PRIV::PRE_CHG:
				assert(0);
				break;
		}
	}
}
void gather_model::peek_props_mod(std::vector<std::tuple<gapr::node_id, std::string, std::string, int>>& out) const {
	for(auto& [key, mod]: _props_mod) {
		switch(mod.state) {
			case PRIV::PRE_ADD:
				assert(mod.iter!=_props.end());
				out.emplace_back(key.node, key.key, mod.iter->second, 1);
				break;
			case PRIV::PRE_DEL:
				out.emplace_back(key.node, key.key, std::string{}, -1);
				break;
			case PRIV::PRE_CHG:
				out.emplace_back(key.node, key.key, mod.val, 0);
				break;
		}
	}
}
void gather_model::peek_bbox(gapr::bbox_int& bbox) const noexcept {
	auto& lines=_priv->lines;
	for(auto& l: lines) {
		bbox.add(l.p0);
		bbox.add(l.p1);
	}
}
