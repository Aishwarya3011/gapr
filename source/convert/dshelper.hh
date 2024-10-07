#include <memory>
#include <filesystem>
#include <future>

#include "gapr/mem-file.hh"
#include "savewebm.hh"

class dshelper {
public:
	static std::shared_ptr<dshelper> create_u8();
	static std::shared_ptr<dshelper> create_u16();

	void init(unsigned int width, unsigned int height, unsigned short spp, unsigned short bps, unsigned int dsx, unsigned int dsy, unsigned int dsz) {
		this->width=width;
		this->height=height;
		this->spp=spp;
		this->bps=bps;
		this->dsx=dsx;
		this->dsy=dsy;
		this->dsz=dsz;
		dsw=(width+dsx-1)/dsx;
		dsh=(height+dsy-1)/dsy;
		this->_initialized=true;
	}

	virtual void update(unsigned int z, unsigned int x, unsigned int y, unsigned int tileW, unsigned int tileH, const void* data) =0;
	virtual void writecache(const std::filesystem::path& path, bool first) const =0;
	virtual void loadcache(const std::filesystem::path& path, unsigned int depth) =0;
	virtual double max_value() const =0;
	virtual std::pair<uint64_t, gapr::mem_file> downsample(std::promise<std::vector<uint16_t>> avg, cube_enc_opts enc_opts, uint64_t mipver, uint64_t avgver, unsigned int chan=0) const =0;
	virtual bool finished(unsigned int z) const =0;
	virtual std::vector<std::array<unsigned int, 2>> missing(unsigned int z, unsigned int tilew, unsigned int tileh) =0;

protected:
	dshelper() noexcept { }
	virtual ~dshelper() { }

	bool _initialized{false};

	unsigned int width, height;
	unsigned short spp;
	unsigned short bps;

	unsigned int dsx, dsy, dsz;
	unsigned int dsw, dsh;

	std::atomic<uint64_t> _version{0};
};
