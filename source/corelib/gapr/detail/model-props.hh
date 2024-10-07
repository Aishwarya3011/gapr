#include <gapr/config.hh>
#include <gapr/model.hh>

#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <cstring>

namespace gapr {

namespace {
	template<typename C, typename T> struct DelIf {
		C* c;
		T todel;
		DelIf(C& c, T t): c{&c}, todel{t} { }
		~DelIf() {
			if(c)
				c->erase(todel);
		}
		void nodel() noexcept { c=nullptr; }
	};
}

class node_props: private std::unordered_map<prop_id, std::string> {
public:
	explicit node_props(): std::unordered_map<prop_id, std::string>{} {
		for(auto& i: _per_key)
			i.first="";
		auto add_slot=[this](const char* key, unsigned int idx) {
			assert(idx<_per_key.size());
			_per_key[idx].first=key;
			_per_key[idx].second.emplace();
		};
		add_slot("", 0);
		add_slot("state", 5);
		add_slot("error", 7);
		add_slot("root", 9);
		add_slot("raise", 10);
		add_slot(".traced", 12);
	}

	using std::unordered_map<prop_id, std::string>::find;
	using std::unordered_map<prop_id, std::string>::end;
	using std::unordered_map<prop_id, std::string>::begin;
	using std::unordered_map<prop_id, std::string>::empty;
	using std::unordered_map<prop_id, std::string>::at;
	using std::unordered_map<prop_id, std::string>::size;

	template<typename K, typename V>
	auto try_emplace(K&& k, V&& v) {
		auto it=_per_node.emplace(k.node, k.key);
		DelIf<std::decay_t<decltype(_per_node)>, std::decay_t<decltype(it)>> dd{_per_node, it};
		auto slot=find_slot(k.key);
		DelIf dd2{*_per_key[0].second, _per_key[0].second->end()};
		if(slot) {
			auto [it2, ins2]=slot->emplace(k.node);
			assert(ins2);
			dd2.c=slot;
			dd2.todel=it2;
		} else {
			dd2.c=nullptr;
		}
		auto ret=std::unordered_map<prop_id, std::string>::try_emplace(std::forward<K>(k), std::forward<V>(v));
		if(ret.second) {
			dd.nodel();
			if(slot)
				dd2.nodel();
		}
		return ret;
	}

	template<typename It>
	auto erase(It it) {
		auto [it1, it2]=_per_node.equal_range(it->first.node);
		for(auto i=it1; i!=it2; ) {
			if(i->second==it->first.key)
				i=_per_node.erase(i);
			else
				++i;
		}
		auto slot=find_slot(it->first.key);
		if(slot)
			slot->erase(it->first.node);
		return std::unordered_map<prop_id, std::string>::erase(it);
	}
	auto erase(const gapr::prop_id& k) {
		auto it=find(k);
		return erase(it);
	}

	auto per_node(gapr::node_id n) const {
		return _per_node.equal_range(n);
	}
	auto& per_key(std::string_view key) const {
		auto slot=const_cast<node_props*>(this)->find_slot(key);
		assert(slot);
		return *slot;
	}

private:
	std::unordered_multimap<node_id, std::string> _per_node;
	std::array<std::pair<const char*, std::optional<std::unordered_set<node_id>>>, 16> _per_key;

	// by gperf
	GAPR_CORE_DECL const static unsigned char asso_values[256];
	static unsigned int hash(const char *str, std::size_t len) {
		return len + asso_values[(unsigned char)str[0]];
	}
	std::unordered_set<node_id>* find_slot(std::string_view key) {
		auto str=key.data();
		auto len=key.size();
		if(len>0) {
			unsigned int key = hash (str, len);
			if (key <= _per_key.size()) {
				auto& hit=_per_key[key];
				const char *s = hit.first;
				if (*str == *s && !std::strcmp (str + 1, s + 1))
					return &hit.second.value();
			}
		}
		return nullptr;
	}
};
}

