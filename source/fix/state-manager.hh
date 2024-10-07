/*!
 * use this,
 * if downstream depends on multiple state variables,
 * or if state variable change multiple times,
 * or if state variable is already available.
 * if a state variable is not expected, do it directly.
*/

namespace gapr {

	class state_manager {
		public:
			enum class primary { null=0 };
			enum class secondary { null=0 };

			primary add_primary() {
				_v1.emplace_back(V1{});
				return static_cast<primary>(_v1.size());
			}
			void change(primary c);
			void change(std::initializer_list<primary> changes) {
				for(auto c: changes) {
					change(c);
				}
			}
			unsigned int changes() const noexcept { return _changed_head==0?0:1; }
			void propagate();
			int get(secondary i) {
				auto ii=static_cast<unsigned int>(i);
				auto& dd=_v2[ii-1];
				if(!dd.valid) {
					dd.value=dd.func();
					dd.valid=true;
				}
				return dd.value;
			}
			template<std::size_t N, typename F>
				secondary add_secondary(std::initializer_list<primary> deps, const secondary (&deps2)[N], F&& f) {
					auto& d=_v2.emplace_back(V2{});
					d.func=std::bind(wrap<N>(deps2, std::move(f)), this);
					handle_deps(deps, deps2, d.deps);
					auto id=_v2.size();
					for(auto i: d.deps)
						_v1[i-1].downstream2.push_back(id);
					return static_cast<secondary>(id);
				}
			template<typename F>
				secondary add_secondary(std::initializer_list<primary> deps, F&& f) {
					auto& d=_v2.emplace_back(V2{});
					d.func=std::move(f);
					handle_deps(deps, d.deps);
					auto id=_v2.size();
					for(auto i: d.deps)
						_v1[i-1].downstream2.push_back(id);
					return static_cast<secondary>(id);
				}
			template<std::size_t N, typename F>
				void add_codes(std::initializer_list<primary> deps, const secondary (&deps2)[N], F&& f) {
					auto& d=_v3.emplace_back(V3{});
					d.func=std::bind(wrap<N>(deps2, std::move(f)), this);
					handle_deps(deps, deps2, d.deps);
					auto id=_v3.size();
					for(auto i: d.deps)
						_v1[i-1].downstream3.push_back(id);
					d.active=true;
					d.next=_active_head;
					_active_head=id;
				}
			template<typename F>
				void add_codes(std::initializer_list<primary> deps, F&& f) {
					auto& d=_v3.emplace_back(V3{});
					d.func=std::move(f);
					handle_deps(deps, d.deps);
					auto id=_v3.size();
					for(auto i: d.deps)
						_v1[i-1].downstream3.push_back(id);
					d.active=true;
					d.next=_active_head;
					_active_head=id;
				}
		private:
			template<std::size_t N>
				void handle_deps(std::initializer_list<primary> deps, const secondary (&deps2)[N], std::vector<unsigned int>& out) {
					std::unordered_set<unsigned int> flat_deps;
					for(auto c: deps)
						flat_deps.emplace(static_cast<unsigned int>(c));
					for(auto c: deps2) {
						auto ci=static_cast<unsigned int>(c);
						auto& dd=_v2[ci-1];
						for(auto i: dd.deps)
							flat_deps.emplace(i);
					}
					for(auto i: flat_deps)
						out.push_back(i);
					std::sort(out.begin(), out.end());
				}
			void handle_deps(std::initializer_list<primary> deps, std::vector<unsigned int>& out) {
				std::unordered_set<unsigned int> flat_deps;
				for(auto c: deps)
					flat_deps.emplace(static_cast<unsigned int>(c));
				for(auto i: flat_deps)
					out.push_back(i);
				std::sort(out.begin(), out.end());
			}

			template<std::size_t N, typename F>
				auto wrap(const secondary* p, F&& f) {
					if constexpr(N==0) {
						return [f=std::move(f)](state_manager* p, auto... args) {
							return f(args...);
						};
					} else {
						return [f=wrap<N-1>(p, std::move(f)),e=p[N-1]](state_manager* p, auto... args) {
							auto v=p->get(e);
							return f(p, v, args...);
						};
					}
				}

			struct V1 {
				bool changed{false};
				unsigned int next;
				std::vector<unsigned int> downstream2;
				std::vector<unsigned int> downstream3;
			};
			struct V2 {
				int value;
				bool valid{false};
				std::function<int()> func;
				std::vector<unsigned int> deps;
			};
			struct V3 {
				bool active{false};
				unsigned int next;
				std::function<void()> func;
				std::vector<unsigned int> deps;
			};
			std::vector<V1> _v1;
			std::vector<V2> _v2;
			std::vector<V3> _v3;
			unsigned int _changed_head{0};
			unsigned int _active_head{0};
	};

}

inline void gapr::state_manager::propagate() {
	//gapr::print(1, "--- begin propagate");
	auto h=_changed_head;
	if(h!=0) {
		_changed_head=0;
		do {
			auto& dd=_v1[h-1];
			dd.changed=false;
			h=dd.next;
		} while(h!=0);
	}
	h=_active_head;
	if(h!=0) {
		_active_head=0;
		do {
			auto& dd=_v3[h-1];
			dd.active=false;
			dd.func();
			h=dd.next;
		} while(h!=0);
	}
	//gapr::print(1, "--- finish propagate");
}
inline void gapr::state_manager::change(primary c) {
	auto ci=static_cast<unsigned int>(c);
	assert(ci!=0);
	auto& d=_v1[ci-1];
	if(!d.changed) {
		d.changed=true;
		d.next=_changed_head;
		_changed_head=ci;
		for(unsigned int i=d.downstream2.size(); i-->0;) {
			auto h=d.downstream2[i];
			auto& dd=_v2[h-1];
			dd.valid=false;
		}
		for(unsigned int i=d.downstream3.size(); i-->0;) {
			auto h=d.downstream3[i];
			auto& dd=_v3[h-1];
			if(!dd.active) {
				dd.active=true;
				dd.next=_active_head;
				_active_head=h;
			}
		}
	}
}
