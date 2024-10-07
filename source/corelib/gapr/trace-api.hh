/* gapr/trace-api.hh
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

//@@@@
#ifndef _GAPR_INCLUDE_TRACE_API_HH_
#define _GAPR_INCLUDE_TRACE_API_HH_


#include "gapr/future.hh"
#include "gapr/connection.hh"
//#include <functional>
//#include "gapr/chunk.h"
#include "gapr/commit.hh"
#include "gapr/mem-file.hh"


namespace gapr {

	struct trace_api_PRIV;
	class GAPR_CORE_DECL trace_api {
		public:
			struct handshake_result {
				unsigned int version;
				std::string banner;
			};
			future<handshake_result> handshake(client_end& cli);

			struct login_result {
				// tier>=11 no user or wrong password
				// tier==10 locked user
				gapr::tier tier;
				std::string gecos;
			};
			future<login_result> login(client_end& cli, const std::string& usr, const std::string& pw);

			struct select_result {
				//tier2==gapr::tier::nobody, no permission
				gapr::tier tier2;
				gapr::stage stage;
				uint64_t commit_num;
				std::string secret;
				std::string description;
			};
			future<select_result> select(client_end& cli, const std::string& grp);

			struct get_catalog_result {
				gapr::mem_file file;
			};
			future<get_catalog_result> get_catalog(client_end& cli);

			struct get_state_result {
				gapr::mem_file file;
			};
			future<get_state_result> get_state(client_end& cli);

			struct get_commit_result {
				/*serialized and compressed*/
				gapr::mem_file file;
			};
			future<get_commit_result> get_commit(client_end& cli, uint64_t id);
			struct get_commits_result {
				gapr::mem_file file;
			};
			future<get_commits_result> get_commits(client_end& cli, gapr::mem_file&& hist, uint64_t upto);

			struct commit_result {
				gapr::nid_t nid0;
				uint64_t id;
				uint64_t update;
			};
			future<commit_result> commit(client_end& cli, gapr::delta_type type, gapr::mem_file&& payload, uint64_t base, uint64_t tip);
			
			struct get_model_result {
				gapr::mem_file file;
			};
			future<get_model_result> get_model(client_end& cli);

			future<int> upload_fragment(client_end& cli, gapr::mem_file&& payload, uint64_t base, uint64_t tip);

			unsigned int state() const noexcept { return _state; }

		private:
			//
			unsigned int _state;
	};

}
/////////////////////////////////////////

#if 0
		gapr::here_ptr<resolver> _resolver{};
	gapr::here_ptr<resolver> _resolver;
		resolver::results_type _addrs{};
	resolver::results_type _addrs{};
		resolver::endpoint_type _addr{ba::ip::address{}, 0};
	resolver::endpoint_type _addr{ba::ip::address{}, 0};
			typedef std::function<void(const char*, const struct sockaddr*)> resolve_cb;
			void resolve(const char* host, const char* port, const resolve_cb& cb);
			typedef std::function<void(const char*)> connect_cb;
			void connect(const struct sockaddr* addr, const connect_cb& cb);
#endif

/////////////////////////


	///////////////////////


#if 0
			typedef std::function<void(const char*, unsigned int, const char*)> login_cb;
			void login(const char* user, const char* pw, const login_cb& cb);

			typedef std::function<void(const char*)> select_cb;
			void select(const char* ws, const select_cb& cb);

			typedef std::function<void(const char*)> commit_cb;
			void commit(const gapr::Delta& delta, const commit_cb& cb);

			typedef callback<>::type save_cb;
			void save(const save_cb& cb);

/////////////////////////////// ////////////////////
		private:


			//connect_cb _connect_cb;
			//void connect_cb_(gapr::Connection* con);


			//uint32_t _save_req{0};
			//save_cb _save_cb;
			////////////////////
			////////////////


	};

}

#endif
#endif

