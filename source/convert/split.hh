#include <filesystem>
#include <vector>
#include "savewebm.hh"

struct SplitArgs {
	std::vector<std::string> inputfiles;
	double xres=0;
	double yres=0;
	double zres=0;

	unsigned int xsize=0;
	unsigned int ysize=0;
	unsigned int zsize=0;
	unsigned int dsx=0;
	unsigned int dsy=0;
	unsigned int dsz=0;

	double ds_ratio{0.003};

	std::string plugin{};
	std::vector<std::string> plugin_args{};
};

struct ServerArgs {
	std::string user{};
	std::string passwd{};
	std::string host{};
	unsigned short port{0};
	std::string group{};
	std::string api_root{};
};

enum class job_state {
	init,
	loading,
	ready,
};
enum class job_prio {
	needed,
	suggested,
};
struct job_id {
	unsigned int x{0}, y{0}, z{0};
	unsigned short chan{0};
	unsigned short _type;

	constexpr job_id() noexcept: _type{0} { }

	bool parse(std::string_view path);
	std::string format() const;

	explicit operator bool() const noexcept { return _type; }
	bool isds() const noexcept { return _type==2 || _type==3; }
	bool operator==(const job_id& r) const noexcept {
		return x==r.x && y==r.y && z==r.z && chan==r.chan && _type==r._type;
	}
	struct hash {
		std::size_t operator()(const job_id& id) const noexcept {
			return (((((id.x*std::size_t{66666667})^id.y)*666667)^id.z)*17)^id.chan;
		}
	};
};

void prepare_conversion(const SplitArgs& args, const std::filesystem::path& workdir, unsigned int ncpu, std::size_t cachesize);
void resume_conversion(const std::filesystem::path& workdir, const std::optional<std::filesystem::path>& tiled_dir, unsigned int ncpu, std::size_t cachesize, ServerArgs&& srv, cube_enc_opts enc_opts);

void prepare_conversion_plugin(const SplitArgs& args, const std::filesystem::path& workdir, unsigned int ncpu, std::size_t cachesize);
void resume_conversion_plugin(const std::filesystem::path& workdir, unsigned int ncpu, std::size_t cachesize, ServerArgs&& srv);
