#ifndef _GAPR_GATHER_ENV_HH_
#define _GAPR_GATHER_ENV_HH_

#include "gapr/commit.hh"

#include <string>
#include <array>
#include <mutex>
#include <condition_variable>
#include <shared_mutex>
#include <unordered_map>
#include <memory>


namespace gapr {

	using pw_hash=std::array<unsigned char, 16>;

	class account_reader;
	class account_writer;
	class project_reader;
	class project_writer;

	class gather_env {
		public:
			explicit gather_env();
			~gather_env();
			gather_env(const gather_env&) =delete;
			gather_env& operator=(const gather_env&) =delete;

			const std::string& private_key() const noexcept { return _pkey; }
			const std::string& certificate() const noexcept { return _cert; }
			std::string project_repo(std::string_view project) const;
			std::string image_catalog(std::string_view project, bool check=true) const;
			std::string image_data(std::string_view project, std::string_view path, bool check=true) const;
			std::string session_state(std::string_view username, std::string_view project) const;

			const std::string& html_docroot() const noexcept { return _docroot; }
			const std::string& http_server() const noexcept { return _http_serv; }

			std::vector<std::string> projects() const;

			static std::string random_token(std::string_view secret);
			static std::size_t generate_etag(std::array<char, 128>& buf, std::string_view path);
			static void prepare_hash(pw_hash& hash, std::string_view password);
			static void prepare_secret(pw_hash& hash, std::string_view password);
			static void check_username(std::string_view name);
			static void check_project_name(std::string_view name);

			void prepare(std::string_view root);
			bool save_configs() const;
			void save_configs_end() const;

			gapr::tier commit_min_tier(gapr::delta_type type, gapr::stage stage) {
				auto k=tier_map_key(stage, type);
				if(auto it=_tier_map.find(k); it!=_tier_map.end())
					return it->second;
				return gapr::tier::root;
			}

		private:
			std::string _pkey;
			std::string _cert;
			std::string _accounts_list;
			std::string _projects_list;
			std::string _acl_d;
			std::string _repo_d;
			std::string _data_d;
			std::string _state_d;
			std::string _docroot;
			std::string _http_serv;

			struct Account {
				/* name will never be changed */
				gapr::tier tier;
				pw_hash salt_hash;
				std::string gecos;
				int order;
				Account(std::string_view gecos);
			};
			std::unordered_map<std::string, Account> _accounts;
			std::vector<std::pair<std::string, int>> _accounts_misc;
			mutable std::shared_mutex _accounts_mtx;
			mutable int64_t _accounts_ver0{-1};
			int64_t _accounts_ver1{-1};

			struct AclInfo;
			struct Project {
				/* name will never be changed */
				gapr::stage stage;
				pw_hash secret; /* secret will never be changed */
				std::string desc;
				std::unique_ptr<AclInfo> acl;
				mutable int64_t acl_ver0;
				int64_t acl_ver1;
				int order;
				Project(std::string_view desc);
			};
			std::unordered_map<std::string, Project> _projects;
			std::vector<std::pair<std::string, int>> _projects_misc;
			mutable std::shared_mutex _projects_mtx;
			mutable int64_t _projects_ver0{-1};
			int64_t _projects_ver1{-1};

			mutable std::condition_variable _save_cv;
			mutable std::mutex _save_mtx;
			mutable unsigned int _save_flags{0};

			void init_accounts();
			void init_projects();
			void load_accounts();
			void load_projects();

			void save_accounts_list() const;
			void save_projects_list() const;
			void save_acl_files() const;

			static gapr::tier get_acl(const AclInfo& acl, const std::string& who, gapr::tier tier);

			std::unordered_map<int64_t, gapr::tier> _tier_map;
			static int64_t tier_map_key(gapr::stage stg, gapr::delta_type type) {
				int64_t i=static_cast<unsigned int>(stg);
				i=(i<<(sizeof(gapr::delta_type)*8));
				int64_t j=i|static_cast<unsigned int>(type);
				return j;
			};

			friend class account_reader;
			friend class account_writer;
			friend class project_reader;
			friend class project_writer;
	};

	class account_reader {
		public:
			account_reader(const gather_env& env, const std::string& name):
				_env{env}, _ptr{nullptr}, _lck{env._accounts_mtx}
			{
				if(auto it=env._accounts.find(name); it!=env._accounts.end())
					_ptr=&it->second;
			}
			~account_reader();
			account_reader(const account_reader&) =delete;
			account_reader& operator=(const account_reader&) =delete;

			operator bool() const noexcept { return _ptr; }

			bool login(std::string_view password) const;
			gapr::tier tier() const noexcept {
				assert(_ptr);
				return _ptr->tier;
			}
			const std::string& gecos() const noexcept {
				assert(_ptr);
				return _ptr->gecos;
			}

		private:
			const gather_env& _env;
			const gather_env::Account* _ptr;
			std::shared_lock<std::shared_mutex> _lck;
	};

	class account_writer {
		public:
			account_writer(gather_env& env, const std::string& name);
			account_writer(gather_env& env, const std::string& name, const pw_hash& hash, std::string_view gecos);
			~account_writer();
			account_writer(const account_writer&) =delete;
			account_writer& operator=(const account_writer&) =delete;

			operator bool() const noexcept { return _ptr; }

			void passwd(gapr::tier tmin, const pw_hash& hash);
			void tier(gapr::tier tier);
			void gecos(std::string_view gecos);

		private:
			gather_env& _env;
			gather_env::Account* _ptr;
			std::unique_lock<std::shared_mutex> _lck;
	};

	class project_reader {
		public:
			project_reader(const gather_env& env, const std::string& name):
				_env{env}, _ptr{nullptr}, _lck{env._projects_mtx}
			{
				if(auto it=env._projects.find(name); it!=env._projects.end())
					_ptr=&it->second;
			}
			~project_reader();
			project_reader(const project_reader&) =delete;
			project_reader& operator=(const project_reader&) =delete;

			operator bool() const noexcept { return _ptr; }

			gapr::tier access(const std::string& who, gapr::tier tier) const {
				assert(_ptr);
				if(tier==gapr::tier::root)
					return gapr::tier::root;
				if(!_ptr->acl)
					return tier;
				auto t2=gather_env::get_acl(*_ptr->acl, who, tier);
				if(t2==gapr::tier::root)
					return tier;
				return t2;
			}
			gapr::stage stage() const noexcept {
				assert(_ptr);
				return _ptr->stage;
			}
			std::string secret() const;
			const std::string& desc() const noexcept {
				assert(_ptr);
				return _ptr->desc;
			}

		private:
			const gather_env& _env;
			const gather_env::Project* _ptr;
			std::shared_lock<std::shared_mutex> _lck;
	};

	class project_writer {
		public:
			project_writer(gather_env& env, const std::string& name);
			project_writer(gather_env& env, const std::string& name, const pw_hash& hash, std::string_view desc);
			~project_writer();
			project_writer(const project_writer&) =delete;
			project_writer& operator=(const project_writer&) =delete;

			operator bool() const noexcept { return _ptr; }

			void stage(gapr::stage stage);
			void desc(std::string_view desc);
			void access(const std::string& name, const std::string& who, bool val);
			void access_clear(const std::string& name);

		private:
			gather_env& _env;
			gather_env::Project* _ptr;
			std::unique_lock<std::shared_mutex> _lck;
	};

}

#endif
