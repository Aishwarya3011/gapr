#include <forward_list>
#include <vector>
#include <array>

template<typename Funcs, unsigned int Tiers, auto... mop>
class vertex_array_manager;

template<typename Funcs, unsigned int Tiers, typename T, typename Tm, Tm T::* mop1, auto... mops>
class vertex_array_manager<Funcs, Tiers, mop1, mops...>: private Funcs {
	public:
		constexpr explicit vertex_array_manager(): Funcs{} {
			auto& h=_infos.emplace_back(Info{0u, 0u, 0});
			h.next=0;
		}
		~vertex_array_manager() {
			if(!_vaos.empty())
				gapr::print("vao's not deleted");
		}

		unsigned int alloc(std::size_t count, unsigned int eid) {
			if(--count>=(1u<<Tiers))
				return alloc_big(eid);
			return alloc_small(lookup_tier(count), eid);
		}
		unsigned int alloc() {
			return alloc_big(0);
		}
		void recycle(unsigned int idx) {
			return do_recycle(idx);
		}
		void destroy() {
			while(!_vaos.empty()) {
				_vaos.front().destroy();
				_vaos.pop_front();
			}
		}

		GLuint vao(unsigned int idx) const noexcept {
			return _infos[idx].vao;
		}
		GLuint buffer(unsigned int idx) const noexcept {
			return _infos[idx].vbo;
		}
		std::pair<GLuint, GLint> vao_first(unsigned int idx) const noexcept {
			auto& h=_infos[idx];
			return {h.vao, h.first};
		}
		void buffer_data(unsigned int idx, const T* data, std::size_t count) {
			auto& h=_infos[idx];
			this->glBindBuffer(GL_ARRAY_BUFFER, h.vbo);
			if(h.tier>Tiers) {
				this->glBufferData(GL_ARRAY_BUFFER, sizeof(T)*count, data, GL_STATIC_DRAW);
			} else {
				this->glBufferSubData(GL_ARRAY_BUFFER, h.first*sizeof(T), count*sizeof(T), data);
			}
			gapr::gl::check_error(this->glGetError(), "buffer data");
		}
		std::pair<unsigned int, int> lookup(GLuint vao, GLint pos) {
			auto& v=_table.at(vao);
			auto tier=v.tier;
			if(tier>Tiers)
				return {v.uniq, pos};
			return {v[pos>>tier], pos&((1<<tier)-1)};
		}

	private:
		struct Info {
			GLuint vao;
			GLuint vbo;
			GLint first;
			union {
				unsigned int next;
				unsigned int tier;
			};
		};
		struct Head {
			unsigned int free{0};
			std::size_t used{0};
			std::size_t total{0};
			gapr::gl::VertexArray<Funcs>* ptr{nullptr};
		};
		std::vector<Info> _infos;
		std::array<Head, Tiers+1> _heads;
		std::forward_list<gapr::gl::VertexArray<Funcs>> _vaos;

		struct LookupTable {
			std::array<unsigned int, (1u<<Tiers)> tbl;
			explicit LookupTable(bool) {
				unsigned j=0;
				for(unsigned int i=0; i<tbl.size(); i++) {
					if((1u<<j)==i)
						++j;
					tbl[i]=j;
				}
			}
		};
		static unsigned int lookup_tier(std::size_t n) {
			static LookupTable tbl{true};
			return std::max(tbl.tbl[n], 2u);
		}
		struct TableItem: std::vector<unsigned int> {
			TableItem(unsigned int tier, std::size_t n): tier{tier} {
				resize(n);
			}
			TableItem(unsigned int tier): tier{tier} {
			}
			unsigned int uniq;
			unsigned int tier;
		};
		std::unordered_map<GLuint, TableItem> _table;
		bool _init{false};

		unsigned int alloc_small(unsigned int tier, unsigned int eid) {
			assert(tier<=Tiers);
			if(!_init) {
				Funcs::initialize();
				_init=true;
			}
			auto& h=_heads[tier];
			if(h.free) {
				auto r=h.free;
				auto& info=_infos[r];
				h.free=info.next;
				info.tier=tier;
				_table.at(info.vao)[info.first>>tier]=eid;
				return r;
			}
			constexpr std::size_t min_count{1024};
			constexpr std::size_t min_size{1024*1024};
			auto sz=(1u<<tier);
			if(h.used+sz>h.total) {
				auto& va=_vaos.emplace_front();
				std::size_t count{min_count};
				if((count<<tier)*sizeof(T)<min_size)
					count=(min_size>>tier)/sizeof(T);
				va.create(mop1, mops...);
				//va.create(mop1, mop2);
				this->glBindBuffer(GL_ARRAY_BUFFER, va.buffer());
				this->glBufferData(GL_ARRAY_BUFFER, (count<<tier)*sizeof(T), nullptr, GL_STATIC_DRAW);
				gapr::gl::check_error(this->glGetError(), "create vao");
				h.used=0;
				h.total=(count<<tier);
				h.ptr=&va;
				_table.emplace(GLuint{va}, TableItem{tier, count});
			}
			auto& info=_infos.emplace_back(Info{*h.ptr, h.ptr->buffer(), static_cast<GLint>(h.used)});
			info.tier=tier;
			h.used+=sz;
			_table.at(info.vao)[info.first>>tier]=eid;
			return _infos.size()-1;
		}

		unsigned int alloc_big(unsigned int eid) {
			if(!_init) {
				Funcs::initialize();
				_init=true;
			}
			auto& h=_infos[0];
			if(h.next) {
				auto r=h.next;
				auto& info=_infos[r];
				h.next=info.next;
				info.tier=Tiers+1;
				_table.at(info.vao).uniq=eid;
				return r;
			}
			auto& va=_vaos.emplace_front();//*static_cast<Funcs*>(this));
			va.create(mop1, mops...);
			//va.create(mop1, mop2);
			auto& info=_infos.emplace_back(Info{va, va.buffer(), 0});
			_table.emplace(info.vao, TableItem{Tiers+1});
			info.tier=Tiers+1;
			_table.at(info.vao).uniq=eid;
			return _infos.size()-1;
		}
		void do_recycle(unsigned int idx) {
			auto tier=_infos[idx].tier;
			if(tier>Tiers) {
				_infos[idx].next=_infos[0].next;
				_infos[0].next=idx;
			} else {
				_infos[idx].next=_heads[tier].free;
				_heads[tier].free=idx;
			}
		}
};

template<typename... Pars>
class draw_cache {
	public:
		draw_cache() { }

		template<typename F>
			void begin(const F& f) {
				while(!_map.empty()) {
					auto it=_map.begin();
					f(it->second.elems);
					_map.erase(it);
				}
			}
		void cache(const std::tuple<Pars...>& pars, GLint first, GLsizei count) {
			auto [it, ins]=_map.emplace(pars, Data{});
			it->second.draws.emplace_back(Item{first, count});
		}
		template<typename F>
			void finish(const F& f) {
				_ordered.clear();
				std::vector<GLsizei> tmp_count;
				std::vector<const GLvoid*> tmp_indices1;
				std::vector<const GLvoid*> tmp_indices2;
				std::vector<GLint> tmp_basevertex;
				for(auto it=_map.begin(); it!=_map.end(); ++it) {
					_ordered.push_back(it);

					auto& dat=it->second;
					std::sort(dat.draws.begin(), dat.draws.end(), [](auto a, auto b) {
						return a.first<b.first;
					});

					std::vector<GLuint> elems;
					//LINE_STRIP
					for(unsigned int i=0; i<dat.draws.size(); i++) {
						if(i>0)
							elems.push_back(GLuint(-1));
#if 1
						auto [first, count]=dat.draws[i];
						for(GLsizei j=0; j<count; j++) {
							elems.push_back(j+first);
#if 0
							if(j+9<count)
								j+=4;
#endif
						}
					}
#else
					//LINES
					for(unsigned int i=0; i<dat.count.size(); i++) {
						auto b=dat.basevertex[i];
						for(GLsizei j=1; j<dat.count[i]; j++) {
							elems.push_back(j+b-1);
							elems.push_back(j+b);
						}
					}
#endif
					dat.elems=f(elems.data(), elems.size()*sizeof(GLuint));
					dat.indices=nullptr;
					dat.count2=elems.size();
				}
				std::sort(_ordered.begin(), _ordered.end(), [](auto a, auto b) {
					return a->first<b->first;
				});
			}

		template<typename F1, typename F2> void replay(const F1& f1, const F2& f2) {
			std::tuple<Pars...> prev;
			auto update=[&prev,&f1](bool force, const std::tuple<Pars...>& pars, auto recurse, auto idx) {
				if constexpr(idx.value<sizeof...(Pars)) {
					if(force || std::get<idx.value>(prev)!=std::get<idx.value>(pars))
						f1(std::get<idx.value>(prev)=std::get<idx.value>(pars), idx);
					using Idx2=std::integral_constant<std::size_t, idx.value+1>;
					recurse(force, pars, recurse, Idx2{});
				}
			};
			unsigned int cnt=0;
			for(auto it: _ordered) {
				auto& [pars, dat]=*it;
				update(!cnt, pars, update, std::integral_constant<std::size_t, 0>{});
				f2(dat.elems, dat.count2, dat.indices);
				++cnt;
				if(cnt>_ordered.size())
					break;
			}
		}

	private:
		struct Item {
			GLint first;
			GLsizei count;
		};
		struct Data {
			std::vector<Item> draws;

			GLuint elems{0};
			const GLvoid* indices;
			GLsizei count2;
		};
		struct Hash {
			std::size_t operator()(const std::tuple<Pars...>& pars) const noexcept {
				return calc<0>(pars);
			};
			template<std::size_t I> std::size_t calc(const std::tuple<Pars...>& pars) const noexcept {
				if constexpr(I<sizeof...(Pars)) {
					using T=std::tuple_element_t<I, std::tuple<Pars...>>;
					return std::hash<T>{}(std::get<I>(pars))^calc<I+1>(pars);
				} else {
					return 0;
				}
			}
		};
		std::unordered_map<std::tuple<Pars...>, Data, Hash> _map;
		std::vector<decltype(_map.begin())> _ordered;
};

