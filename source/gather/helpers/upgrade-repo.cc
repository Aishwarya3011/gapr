#include <cstdlib>
#include <string>
#include <fstream>
#include <cinttypes>

#include <unistd.h>
#include <sys/stat.h>

#include "gapr/archive.hh"
#include "gapr/exception.hh"
#include "gapr/model.hh"
#include "gapr/utility.hh"
#include "gapr/serializer.hh"
#include "gapr/commit.hh"

#include "../corelib/dup/save-load-helper.hh"

namespace gapr {

	namespace {

		struct commit_info0 {
			uint64_t id;
			std::string who;
			uint64_t when;
			node_id::data_type nid0;
			uint16_t type;
		};

		using delta0_reset_proofread=gapr::delta_reset_proofread_0_;
		struct delta0_proofread {
			std::vector<node_id::data_type> nodes;
		};

	}

	static char fix_xxx{'\xff'};
	template<> struct SerializerAdaptor<commit_info0, 0> {
		template<typename T> static auto& map(T& obj) { return obj.id; }
	};
	template<> struct SerializerAdaptor<commit_info0, 1> {
		template<typename T> static auto& map(T& obj) { return obj.who; }
	};
	template<> struct SerializerAdaptor<commit_info0, 2> {
		template<typename T> static auto& map(T& obj) { return obj.when; }
	};
	template<> struct SerializerAdaptor<commit_info0, 3> {
		template<typename T> static auto& map(T& obj) { return obj.nid0; }
	};
	template<> struct SerializerAdaptor<commit_info0, 4> {
		template<typename T> static auto& map(T& obj) { return obj.type; }
	};


	template<> struct SerializerAdaptor<delta0_proofread, 0> {
		template<typename T> static auto& map(T& obj) { return obj.nodes; }
	};
	template<> struct SerializerUsePredictor<delta0_proofread, 0> {
		constexpr static bool value=true;
	};
	template<> struct SerializerPredictor<delta0_proofread, 0> {
		template<typename T> static auto sub(T obj, T obj0) {
			return obj-obj0;
		}
		template<typename T, typename Td> static T add(Td d, T obj0) {
			return d+obj0;
		}
	};
	template<> struct SerializerAdaptor<delta0_proofread, 1> {
		template<typename T> static auto& map(T& obj) { return fix_xxx; }
	};

	template<> struct SerializerAdaptor<delta0_reset_proofread, 0> {
		template<typename T> static auto& map(T& obj) { return obj.nodes; }
	};
	template<> struct SerializerUsePredictor<delta0_reset_proofread, 0> {
		constexpr static bool value=true;
	};
	template<> struct SerializerPredictor<delta0_reset_proofread, 0> {
		template<typename T> static auto sub(T obj, T obj0) {
			using Td=std::make_signed_t<T>;
			return static_cast<Td>(obj-obj0);
		}
		template<typename T, typename Td> static T add(Td d, T obj0) {
			return d+obj0;
		}
	};
	template<> struct SerializerAdaptor<delta0_reset_proofread, 1> {
		template<typename T> static auto& map(T& obj) { return obj.props; }
	};

	namespace {

		template<typename T>
			auto do_upgrade_commit(T&& delta, commit_info0&& info, gapr::commit_info& info2) {
				using T2=std::decay_t<T>;
				T2 delta2=std::move(delta);
				return delta2;
			}

		gapr::delta_proofread_ do_upgrade_commit(delta0_proofread&& delta, commit_info0&& info, gapr::commit_info& info2) {
			gapr::delta_proofread_ delta2;
			delta2.nodes=std::move(delta.nodes);
			return delta2;
		}


                template <typename T, typename=decltype(gapr::load(std::declval<T&>(), std::declval<std::streambuf&>()))>
                bool load_impl_sel(T& delta, std::streambuf& fs, char)
                {
			////fprintf(stderr, "public\n");
                        return gapr::load(delta, fs);
                }
                template <typename T>
                bool load_impl_sel(T& delta, std::streambuf& fs, int)
                {
			////fprintf(stderr, "private\n");
                        return load_impl(delta, fs);
                }

                template<typename T> std::string upgrade_commit(std::streambuf& fs, commit_info0&& info) {
			T delta;
			if(!load_impl_sel<>(delta, fs, 'x'))
				gapr::report("failed to load delta");
			gapr::commit_info info2;
			info2.id=info.id;
			info2.who=std::move(info.who);
			info2.when=info.when;
			info2.nid0=info.nid0;
			info2.type=info.type;
			auto delta2=do_upgrade_commit(std::move(delta), std::move(info), info2);
			std::ostringstream oss1, oss2;
			if(!info2.save(*oss1.rdbuf()))
				gapr::report("failed to save info");
			if(!save(delta2, *oss2.rdbuf()))
				gapr::report("failed to save delta");
			oss1<<oss2.str();
			return oss1.str();
		}
		std::string upgrade_commit0(uint64_t id, std::streambuf& fs) {
			commit_info0 info;
			if(!load_impl(info, fs))
				gapr::report("commit file no commit info");
			if(info.id!=id)
				gapr::report("commit file wrong id");
			////fprintf(stderr, "type: %u\n", info.type);
			switch(static_cast<gapr::delta_type>(info.type)) {
				default:
					throw std::logic_error{"invalid delta type2"};
				case gapr::delta_type::invalid:
					throw std::logic_error{"invalid delta type"};
				case gapr::delta_type::add_edge_:
					return upgrade_commit<gapr::delta_add_edge_>(fs, std::move(info));
				case gapr::delta_type::add_prop_:
					return upgrade_commit<gapr::delta_add_prop_>(fs, std::move(info));
				case gapr::delta_type::chg_prop_:
					return upgrade_commit<gapr::delta_chg_prop_>(fs, std::move(info));
				case gapr::delta_type::add_patch_:
					return upgrade_commit<gapr::delta_add_patch_>(fs, std::move(info));
				case gapr::delta_type::del_patch_:
					return upgrade_commit<gapr::delta_del_patch_>(fs, std::move(info));
				case gapr::delta_type::proofread_:
					return upgrade_commit<gapr::delta_proofread_>(fs, std::move(info));
				case gapr::delta_type::reset_proofread_0_:
					return upgrade_commit<delta0_reset_proofread>(fs, std::move(info));
				case gapr::delta_type::reset_proofread_:
					return upgrade_commit<gapr::delta_reset_proofread_>(fs, std::move(info));
			}
		}

	}

}

class Archive {
	public:
		explicit Archive(const char* path): arch{path} {
			arch.begin_buffered(2);
		}
		bool add(std::string_view key, const std::string& buf) {
			auto w=arch.get_writer(key);
			std::size_t i=0;
			while(i<buf.size()) {
				auto [ptr, siz]=w.buffer();
				auto len=buf.size()-i;
				if(len>siz)
					len=siz;
				std::memcpy(ptr, &buf[i], len);
				w.commit(len);
				i+=len;
			}
			return w.flush();
		}
		bool close() {
			return arch.end_buffered();
		}
	private:
		gapr::archive arch;
};

#ifdef _MSC_VER
template<typename T> inline static bool S_ISREG(T m) {
	return (m&_S_IFMT)==_S_IFREG;
}
#endif

int upgrade_ver0(unsigned int ncommit, const char* db_in, const char* db_out)
{
        gapr::archive archin { db_in, true };
        Archive arch { db_out };

        std::array<char, 32> keybuf;
        auto id2key = [&keybuf](unsigned int id) {
                return gapr::to_string_lex(keybuf, id);
        };

        unsigned int id = 0;
        while (ncommit == 0 || id < ncommit) {
                auto key = id2key(id);
                auto fs = archin.reader_streambuf(key);
                if (!fs)
                        break;
                auto buf = gapr::upgrade_commit0(id, *fs);
                if (!arch.add(key, std::move(buf))) {
                        std::cerr << "failed to add commit\n";
                        return -1;
                }
                id++;
        }
        if (!arch.close()) {
                std::cerr << "failed to flush output\n";
                return -1;
        }
        return 0;
}

int main(int argc, char* argv[])
{
        int opt;
        unsigned int ncommit = 0;
        try {
                char* eptr;
                while ((opt = ::getopt(argc, argv, ":n:h")) != -1)
                        switch (opt) {
                        case 'n':
                                ncommit = ::strtoul(optarg, &eptr, 10);
                                if (errno != 0 || *eptr != '\0')
                                        throw gapr::reported_error("Wrong value for option `-n'");
                                break;
                        case 'h':
                                std::cerr << argv[0] << " [-n NUM] DB_IN DB_OUT\n";
                                return 0;
                        }
                if (optind + 2 != argc)
                        throw gapr::reported_error("Wrong number of arguments");
        } catch (const gapr::reported_error& e) {
                std::cerr << argv[0] << ": failed to parse options: " << e.what() << "\n";
                return -1;
        }

        return upgrade_ver0(ncommit, argv[optind], argv[optind + 1]);
}
