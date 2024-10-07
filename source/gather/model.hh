#include "gapr/commit.hh"

#include "gapr/mem-file.hh"
#include "gapr/node-allocator.hh"
#include "gapr/bbox.hh"

////#include <fstream>
#include <string>
#include <mutex>
#include <atomic>
#include <memory>
#include <unordered_map>


// XXX alternatives???


class gather_model {
	public:
		class modifier;
		class helper;

		explicit gather_model(std::string&& path);
		~gather_model();
		gather_model(const gather_model&) =delete;
		gather_model& operator=(const gather_model&) =delete;

		uint64_t num_commits() const noexcept {
			return _num_commits.load();
		}

		std::unique_ptr<std::streambuf> get_commit(uint32_t id) const;

		std::string get_stats(uint64_t from) const;
		std::string get_proofread_stats(gapr::commit_id from, gapr::commit_id to) const;
		void update_stats(void* prev, uint32_t& nn, uint32_t& nnr, uint32_t& nt, uint32_t& ntr, uint64_t& nc) const;

		// XXX
		explicit gather_model();
		void xxx_prepare(const gapr::commit_info& info, std::streambuf& buf);
		void xxx_apply();
		void peek_nodes_mod(std::vector<std::tuple<gapr::node_id, gapr::node_attr, int>>& out) const;
		void peek_links_mod(std::vector<std::tuple<gapr::node_id, gapr::node_id, int>>& out) const;
		void peek_props_mod(std::vector<std::tuple<gapr::node_id, std::string, std::string, int>>& out) const;
		const auto& peek_nodes() const noexcept { return _nodes; }
		const auto& peek_links() const noexcept { return _links; }
		const auto& peek_props() const noexcept { return _props; }
		void peek_bbox(gapr::bbox_int& bbox) const noexcept;

	private:
		const std::string _path;
		mutable std::mutex _mtx;

		using node_key=gapr::node_id;
		using node_hash=std::hash<node_key>;
		struct node_val {
			gapr::node_attr attr;
			unsigned int nref;
			// XXX nprop nlink
			// links: <nid, idx> -> nid2
		};
		using link_key=gapr::link_id;
		using link_hash=std::hash<link_key>;
		using link_val=gapr::misc_attr;
		using prop_key=gapr::prop_id;
		using prop_hash=std::hash<prop_key>;
		using prop_val=std::string;
		std::atomic<uint32_t> _num_commits;
		gapr::node_id _num_nodes;
		std::unordered_map<node_key, node_val, node_hash, std::equal_to<node_key>, gapr::node_allocator<std::pair<const node_key, node_val>>> _nodes;
		std::unordered_map<link_key, link_val, link_hash, std::equal_to<link_key>, gapr::node_allocator<std::pair<const link_key, link_val>>> _links;
		std::unordered_map<prop_key, prop_val, prop_hash> _props;
		std::vector<std::string> _logs;
		std::size_t _logs_prev;

		struct node_mod {
			int state;
			decltype(_nodes.end()) iter;
			gapr::node_attr val;
		};
		struct link_mod {
			int state;
			decltype(_links.end()) iter;
			gapr::misc_attr val;
		};
		struct prop_mod {
			int state;
			decltype(_props.end()) iter;
			std::string val;
		};
		struct nref_mod {
			decltype(_nodes.end()) iter;
			unsigned int nref;
		};
		gapr::node_id _nid_alloc;
		std::unordered_map<node_key, node_mod, node_hash, std::equal_to<node_key>, gapr::node_allocator<std::pair<const node_key, node_mod>>> _nodes_mod;
		std::unordered_map<link_key, link_mod, link_hash, std::equal_to<link_key>, gapr::node_allocator<std::pair<const link_key, link_mod>>> _links_mod;
		std::unordered_map<prop_key, prop_mod, prop_hash> _props_mod;
		std::unordered_map<node_key, nref_mod, node_hash, std::equal_to<node_key>, gapr::node_allocator<std::pair<const node_key, nref_mod>>> _nodes_nref;
		
#if 0
		// XXX compact data struct
		struct NodeArray;
		constexpr static std::size_t N_ARRAY=65536;
		std::size_t _num_arr;
		std::array<NodeArray*, N_ARRAY> _nodes;
#endif

		struct PRIV;
		std::unique_ptr<PRIV> _priv;
};

class gather_model::modifier {
	public:
		modifier(gather_model& model,
				gapr::delta_type type, gapr::mem_file&& payload) noexcept:
			_model{model}, _lck{_model._mtx, std::defer_lock},
			_type{type}, _payload{std::move(payload)} { }
		~modifier();
		modifier(const modifier&) =delete;
		modifier& operator=(const modifier&) =delete;

		uint64_t prepare();
		std::tuple<gapr::node_id::data_type, uint64_t, uint64_t> apply(std::string&& who, const gapr::commit_history& hist) noexcept;

	private:
		gather_model& _model;
		std::unique_lock<std::mutex> _lck;
		gapr::delta_type _type;
		gapr::mem_file _payload;
		friend struct PRIV;
};

class gather_model::helper {
	public:
		helper(gather_model& model) noexcept:
			_model{model}, _lck{_model._mtx, std::defer_lock} { }
		~helper();
		helper(const helper&) =delete;
		helper& operator=(const helper&) =delete;

		gapr::mem_file dump_state();

	private:
		gather_model& _model;
		std::unique_lock<std::mutex> _lck;
		//friend struct PRIV;
};

