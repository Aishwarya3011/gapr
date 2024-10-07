/* detail/cb-wrapper.hh
 *
 * Copyright (C) 2018 GOU Lingfeng
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


//@@@@@
#ifndef _GAPR_INCLUDE_DETAIL_CB_WRAPPER_HH_
#define _GAPR_INCLUDE_DETAIL_CB_WRAPPER_HH_


#include <memory>
#include <type_traits>

namespace gapr {

	class cb_wrapper_PRIV {

		struct uniq;

		template<typename Bas, typename=void>
			struct base {
				static_assert(std::has_virtual_destructor<Bas>::value);
				struct typ: Bas {
					using Bas::Bas;
					~typ() override =default;
					virtual uniq* _cb_wrapper_vf() const noexcept =0;
					constexpr static std::size_t _cb_wrapper_cnt=0;
					using _cb_wrapper_typ=typ;
				};
			};
		template<typename Bas>
			struct base<Bas, std::enable_if_t<std::is_same<Bas, uniq>::value>> {
				struct typ {
					virtual ~typ() { }
					virtual uniq* _cb_wrapper_vf() const noexcept =0;
					constexpr static std::size_t _cb_wrapper_cnt=0;
					using _cb_wrapper_typ=typ;
				};
			};
		template<typename Bas>
			struct base<Bas, std::enable_if_t<std::is_same<cb_wrapper_PRIV::uniq*, decltype(std::declval<Bas&>()._cb_wrapper_vf())>::value>> {
				using typ=Bas;
			};

		template<typename Sig>
			struct sig_aux;
		template<typename Ret, typename... Args>
			struct sig_aux<Ret(Args...)> {
				using ret_typ=Ret;
				using ptr_typ=Ret(*)(void*, Args...);
			};

		template<typename Tp> class cb_reseter {
			public:
				explicit cb_reseter(void* ptr) noexcept:
					_ptr{*static_cast<Tp*>(ptr)} { }
				~cb_reseter() { _ptr._cb_wrapper_clear(); }
				cb_reseter(const cb_reseter&) =delete;
				cb_reseter& operator=(const cb_reseter&) =delete;

				Tp* operator->() const noexcept { return &_ptr; }
			private:
				Tp& _ptr;
		};

		template<typename Tp, typename Sig>
			struct ptr_aux;
		template<typename Tp, typename Ret, typename... Args>
			struct ptr_aux<Tp, Ret(Args...)> {
				static Ret fn(void* _p, Args... args) {
					cb_reseter<Tp> p{_p};
					return p->_cb_wrapper_cb(std::forward<Args>(args)...);
				}
			};

		template<typename Tp, std::size_t I, typename=void>
			struct type_aux {
				using typ=typename type_aux<typename Tp::_cb_wrapper_par, I>::typ;
			};
		template<typename Tp, std::size_t I>
			struct type_aux<Tp, I, std::enable_if_t<I+1==Tp::_cb_wrapper_cnt>> {
				using typ=Tp;
			};

		template<typename Sig, typename Bas>
			struct add: Bas {
					template<typename... Args>
						typename sig_aux<Sig>::ret_typ cb_wrapper_call(Args&&... args) {
							return _cb_wrapper_ptr(this, std::forward<Args>(args)...);
						}
					using Bas::Bas;
					//
					constexpr static std::size_t _cb_wrapper_cnt=Bas::_cb_wrapper_cnt+1;
					using _cb_wrapper_sig=Sig; // XXX redundant?
					using _cb_wrapper_typ=add;
					using _cb_wrapper_par=typename Bas::_cb_wrapper_typ;
					typename sig_aux<Sig>::ptr_typ _cb_wrapper_ptr;
#if 0
				//typedef typename sig_helper_<typename T::_cb_wrapper_sig>::ret_type ret_type;
				template<std::size_t I, typename... Args,
					typename Helper=type_aux<add_, I>>
						typename Helper::ret_type call(Args&&... args) {
							auto p=static_cast<typename Helper::type*>(this);
							return p->_cb_wrapper_ptr(p, std::forward<Args>(args)...);
						}
				//template<typename T, typename... Cbs>
					//friend struct extend;
#endif
			};

		template<typename Tp, typename... Cbs>
			struct extend_bas;
		template<typename Tp>
			struct extend_bas<Tp> {
				using typ=Tp;
			};
		template<typename Tp, typename Cb, typename... Cbs>
			struct extend_bas<Tp, Cb, Cbs...> {
				using bas_typ=typename extend_bas<Tp, Cbs...>::typ;
				using Typ=typename type_aux<Tp, Tp::_cb_wrapper_cnt-1-sizeof...(Cbs)>::typ;
				struct typ: bas_typ {
					union { Cb _cb_wrapper_cb; };
					void _cb_wrapper_clear() noexcept {
						_cb_wrapper_cb.~Cb();
						static_cast<Typ*>(this)->_cb_wrapper_ptr=nullptr;
					}
					template<typename Cb2, typename... Args>
						typ(Cb2&& cb, Args&&... args):
							bas_typ{std::forward<Args>(args)...},
						_cb_wrapper_cb{std::forward<Cb2>(cb)} {
							static_cast<Typ*>(this)->_cb_wrapper_ptr=&ptr_aux<typ, typename Typ::_cb_wrapper_sig>::fn;
						}
					~typ() {
						if(static_cast<Typ*>(this)->_cb_wrapper_ptr)
							_cb_wrapper_cb.~Cb();
					}
				};
			};
		template<typename Tp, typename... Cbs>
			struct extend_bas<Tp, std::nullptr_t, Cbs...> {
				using bas_typ=typename extend_bas<Tp, Cbs...>::typ;
				using Typ=typename type_aux<Tp, Tp::_cb_wrapper_cnt-1-sizeof...(Cbs)>::typ;
				struct typ: bas_typ {
					template<typename... Args>
						typ(std::nullptr_t, Args&&... args):
							bas_typ{std::forward<Args>(args)...} {
								static_cast<Typ*>(this)->_cb_wrapper_ptr=nullptr;
							}
				};
			};
		template<typename Tp, typename... Cbs>
			struct extend: extend_bas<Tp, Cbs...>::typ {
				static_assert(sizeof...(Cbs)==Tp::_cb_wrapper_cnt);
				using bas=typename extend_bas<Tp, Cbs...>::typ;
				using bas::bas;
	//**vf
				uniq* _cb_wrapper_vf() const noexcept override { return nullptr; }
			};

		template<typename, typename Tp, typename TpP, typename... Cbs>
			struct creater;
		template<typename Tp, typename... Cbs>
			struct creater<std::enable_if_t<sizeof...(Cbs)==Tp::_cb_wrapper_cnt>, Tp, std::unique_ptr<Tp>, Cbs...> {
				template<typename... Args>
					static std::unique_ptr<Tp> mak(Cbs&&... cbs, Args&&... args) {
						return std::make_unique<extend<Tp, std::decay_t<Cbs>...>>(std::forward<Cbs>(cbs)..., std::forward<Args>(args)...);
					}
			};
		template<typename Tp, typename... Cbs>
			struct creater<std::enable_if_t<sizeof...(Cbs)==Tp::_cb_wrapper_cnt>, Tp, std::shared_ptr<Tp>, Cbs...> {
				template<typename... Args>
					static std::shared_ptr<Tp> mak(Cbs&&... cbs, Args&&... args) {
						return std::make_shared<extend<Tp, Cbs...>>(std::move(cbs)..., std::forward<Args>(args)...);
					}
			};
		template<typename Tp, typename TpP, typename... Cbs>
			struct creater<std::enable_if_t<sizeof...(Cbs)<Tp::_cb_wrapper_cnt>, Tp, TpP, Cbs...> {
				template<typename Cb, typename... Args>
					static TpP mak(Cbs&&... cbs, Cb&& cb, Args&&... args) {
						return creater<void, Tp, TpP, Cbs..., Cb>::mak(std::forward<Cbs>(cbs)..., std::forward<Cb>(cb), std::forward<Args>(args)...);
					}
			};

		friend struct cb_wrapper;
	};


	struct cb_wrapper {

		template<typename Tp, typename... Args>
			static std::unique_ptr<Tp> make_unique(Args&&... args) {
				return cb_wrapper_PRIV::creater<void, Tp, std::unique_ptr<Tp>>::mak(std::forward<Args>(args)...);
			}

		template<typename Tp, typename... Args>
			static std::shared_ptr<Tp> make_shared(Args&&... args) {
				return cb_wrapper_PRIV::creater<void, Tp, std::shared_ptr<Tp>>::mak(std::forward<Args>(args)...);
			}

		template<typename Sig, typename Bas=cb_wrapper_PRIV::uniq>
			using add=cb_wrapper_PRIV::add<Sig, typename cb_wrapper_PRIV::base<Bas>::typ>;

	};


}

#endif
