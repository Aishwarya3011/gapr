/* gapr/ask-secret.hh
 *
 * Copyright (C) 2019 GOU Lingfeng
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
#ifndef _GAPR_INCLUDE_ASK_SECRET_HH_
#define _GAPR_INCLUDE_ASK_SECRET_HH_

#ifdef _WIN64
#else
#include <termios.h>
#endif

namespace gapr {

	class ask_secret {
		public:
			explicit ask_secret() noexcept: _hdl{std_hdl()}, _noecho{false} {
				try_turnoff_echo();
				//remove exposed chars,
				//not needed for Linux
			}
			~ask_secret() {
				if(_hdl!=invalid_hdl()) {
					if(_noecho)
						restore_echo(_hdl, &_st);
				}
			}
			ask_secret(const ask_secret&) =delete;
			ask_secret& operator=(const ask_secret&) =delete;
			ask_secret(ask_secret&& r) noexcept: _hdl{r._hdl}, _st{r._st} {
				r._hdl=invalid_hdl();
			}
			ask_secret& operator=(ask_secret&& r) =delete;

			std::string get() const {
				std::string s{};
				char c;
				do {
#ifdef _WIN64
					DWORD n;
					auto r=::ReadFile(_hdl, &c, 1, &n, nullptr);
					if(!r) {
						//XXX err GetLastError
						return s;
					}
#else
					auto n=::read(_hdl, &c, 1);
					if(n==-1) {
						// XXX //print return throw terminate
						return s;
					}
#endif
					if(n==0)
						return s;
					if(c=='\n')
						return s;
					if(c=='\r')
						continue;
					s.push_back(c);
				} while(true);
			}

		private:
#ifdef _WIN64
			using handle=HANDLE;
			using state=DWORD;
			static /*constexpr*/ handle invalid_hdl() noexcept { return reinterpret_cast<HANDLE>(INVALID_HANDLE_VALUE); }
			static handle std_hdl() noexcept { return ::GetStdHandle(STD_INPUT_HANDLE); }
			void try_turnoff_echo() noexcept {
				if(!::GetConsoleMode(_hdl, &_st))
					return; // XXX
				auto st2=_st&(~ENABLE_ECHO_INPUT);
				if(!::SetConsoleMode(_hdl, st2))
					return; // XXX
				_noecho=true;
			}
			static void restore_echo(handle hdl, state* st0) noexcept {
				if(!::SetConsoleMode(hdl, *st0))
					return; // XXX
				//print return throw terminate
			}
#else
			using handle=int;
			using state=struct termios;
			static constexpr handle invalid_hdl() noexcept { return -1; }
			static constexpr handle std_hdl() noexcept { return STDIN_FILENO; }
			void try_turnoff_echo() noexcept {
				if(::tcgetattr(_hdl, &_st)!=0)
					return; // XXX
				auto prev=_st.c_lflag;
				_st.c_lflag&=~(ECHO/*|ECHONL*/);
				if(::tcsetattr(_hdl, TCSAFLUSH/*|TCSASOFT*/, &_st)!=0)
					return; // XXX
				_st.c_lflag=prev;
				_noecho=true;
				std::cerr<<"sizeof(termios): "<<sizeof(state)<<'\n';
			}
			static void restore_echo(handle hdl, state* st0) noexcept {
				if(::tcsetattr(hdl, TCSAFLUSH/*|TCSASOFT*/, st0)!=0)
					return; // XXX
				//print return throw terminate
			}
#endif
			handle _hdl;
			state _st;
			bool _noecho;
	};

}

#endif
