#include <chrono>
#include <tuple>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <cassert>

struct cache_helper {
	using triple=std::tuple<unsigned int, unsigned int, unsigned int>;
	struct triple_hash {
		std::size_t operator()(const triple& tri) const noexcept {
			auto [z, x, y]=tri;
			return (((z*std::size_t{952381})^y)*1000667)^x;
		}
	};
	struct entry {
		std::unique_ptr<uint8_t[]> dat8;
		std::unique_ptr<uint16_t[]> dat16;
		std::size_t size;
		std::chrono::steady_clock::time_point ts;
		unsigned int ww, hh;
		const uint8_t* ptr(uint8_t) const noexcept {
			assert(dat8);
			return dat8.get();
		}
		const uint16_t* ptr(uint16_t) const noexcept {
			assert(dat16);
			return dat16.get();
		}
		bool check(unsigned int tilew, unsigned int tileh, unsigned int spp) const noexcept {
			if(ww!=tilew)
				return false;
			if(hh!=tileh)
				return false;
			if(dat8 && sizeof(uint8_t)*tilew*tileh*spp!=size)
				return false;
			if(dat16 && sizeof(uint16_t)*tilew*tileh*spp!=size)
				return false;
			return true;
		}
	};

	cache_helper(std::size_t ub, std::size_t ub2): _ub{ub}, _ub2{ub2} { }

	std::shared_ptr<entry> get(unsigned int z, unsigned int x, unsigned int y) const {
		triple tri{z, x, y};
		std::lock_guard lck{_mtx};
		auto it=_cache.find(tri);
		if(it!=_cache.end()) {
			it->second->ts=std::chrono::steady_clock::now();
			return it->second;
		}
		return {};
	}
	template<typename T>
	std::shared_ptr<entry> put(unsigned int z, unsigned int x, unsigned int y, unsigned int tilew, unsigned int tileh, unsigned int spp, std::unique_ptr<T[]>&& dat) {
		auto siz=sizeof(T)*tilew*tileh*spp;
		auto ent=std::make_shared<entry>();
		if constexpr(std::is_same_v<T, uint8_t>) {
			ent->dat8=std::move(dat);
		} else {
			ent->dat16=std::move(dat);
		}
		ent->ts=std::chrono::steady_clock::now();
		ent->size=siz;
		ent->ww=tilew;
		ent->hh=tileh;

		triple tri{z, x, y};
		std::lock_guard lck{_mtx};
		auto [it, ins]=_cache.emplace(tri, ent);
		assert(ins);
		_tot_siz+=siz;
		if(_tot_siz>_ub) {
			auto it_end=_cache.end();
			std::vector<decltype(it_end)> its;
			for(auto it=_cache.begin(); it!=it_end; ++it)
				its.push_back(it);
			std::sort(its.begin(), its.end(), [](auto a, auto b) {
				return a->second->ts>b->second->ts;
			});
			while(!its.empty()) {
				auto it=its.back();
				its.pop_back();
				_tot_siz-=it->second->size;
				_cache.erase(it);
				if(_tot_siz<=_ub2)
					break;
			}
		}
		return ent;
	}
	void clear(unsigned int zz) {
		std::lock_guard lck{_mtx};
		auto it_end=_cache.end();
		std::vector<decltype(it_end)> its;
		for(auto it=_cache.begin(); it!=it_end; ++it) {
			auto [z, x, y]=it->first;
			if(z==zz)
				its.push_back(it);
		}
		while(!its.empty()) {
			auto it=its.back();
			its.pop_back();
			_tot_siz-=it->second->size;
			_cache.erase(it);
		}
	}

	mutable std::mutex _mtx;
	std::unordered_map<triple, std::shared_ptr<entry>, triple_hash> _cache;
	std::size_t _tot_siz{0};
	std::size_t _ub, _ub2;
};
