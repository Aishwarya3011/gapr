class gather_model;
class gather_stats {
	public:
		explicit gather_stats(/*const std::vector<std::string>&& grps*/): _mtx_out{}, _mtx_int{}, _stat_ts{0} {
#if 0
			for(auto& grp: grps)
				_infos.emplace(grp, {});
#endif
		}
		~gather_stats() { }

		struct PerUser {
			std::string name;
			double score_d;
			unsigned int score_d_pr;
			unsigned int score_d_rep;
			double score_m;
			unsigned int rank_d;
			unsigned int rank_m;
		};
		const std::vector<PerUser>& per_user(std::unique_lock<std::mutex>& lck) const {
			lck=std::unique_lock{_mtx_out};
			return _per_user;
		}
		struct PerGrp {
			std::string name;
			uint32_t num_nodes;
			uint32_t num_nodes_raw;
			uint32_t num_terms;
			uint32_t num_terms_raw;
			uint64_t num_commits;
			uint64_t num_commits_d;
			uint64_t num_commits_m;
		};
		const std::vector<PerGrp>& per_grp(std::unique_lock<std::mutex>& lck) const {
			lck=std::unique_lock{_mtx_out};
			return _per_grp;
		}

		uint64_t last_update() const {
			std::lock_guard lck{_mtx_out};
			return _stat_ts;
		}

		void update(const std::string& grp, const gather_model& mdl);
		void update_end();
	private:
		mutable std::mutex _mtx_out;
		std::mutex _mtx_int;
		uint64_t _stat_ts;
		struct CommitInfo: gapr::commit_info {
			double score{0.0};
			unsigned int score_pr{0};
			unsigned int score_rep{0};
		};
		struct Info {
			std::vector<CommitInfo> commits;
			PerGrp stats;
		};
		std::unordered_map<std::string, Info> _infos;
		std::vector<PerUser> _per_user;
		std::vector<PerGrp> _per_grp;
		friend class gather_model;
};

