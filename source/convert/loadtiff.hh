#include <array>
#include <mutex>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <functional>

#include <tiffio.h>

#include "windows-compat.hh"

class Tiff {
public:
	constexpr Tiff() noexcept: _ptr{nullptr} { }
	Tiff(const std::filesystem::path& p, const char* m):
		_ptr{TIFFOpen(p.c_str(), m)} { }
	~Tiff() {
		if(_ptr)
			TIFFClose(_ptr);
	}
	Tiff(const Tiff&) =delete;
	Tiff& operator=(const Tiff&) =delete;
	Tiff(Tiff&& r) noexcept: _ptr{r._ptr}, _buf{std::move(r._buf)} {
		r._ptr=nullptr;
	}
	Tiff& operator=(Tiff&& r) noexcept {
		std::swap(_ptr, r._ptr);
		std::swap(_buf, r._buf);
		return *this;
	}

	operator TIFF*() const noexcept { return _ptr; }
	bool operator!() const noexcept { return !_ptr; }

	bool flush();

	/*! write to mem, then sync to file
	 * 82% time usage with direct write, by comparison
	 */
	explicit Tiff(const char* m);

	/*! read from mem
	 */
	explicit Tiff(std::vector<char>&& buf, const char* m);

	std::vector<char> buffer() && noexcept;

private:
	TIFF* _ptr;
	struct MemBuf;
	std::shared_ptr<MemBuf> _buf;
};
std::array<uint32_t, 2> get_tilesize(TIFF* tif);

struct TileReader {
	unsigned width, height;
	uint16_t spp;
	uint16_t bps;

	uint64_t tileSize{0};
	uint64_t tileBufSize{0};
	uint32_t tileW{0}, tileH{0};

	using Func=void(const TileReader&, TIFF*, std::vector<uint8_t>&, unsigned int, unsigned int, void*);
	Func* ptr{nullptr};
	void operator()(TIFF* tif, std::vector<uint8_t>& tmpbuf, unsigned int x, unsigned int y, void* out) const {
		return (*ptr)(*this, tif, tmpbuf, x, y, out);
	}

	constexpr TileReader(unsigned int width, unsigned int height,
			uint16_t spp, uint16_t bps) noexcept:
		width{width}, height{height}, spp{spp}, bps{bps} { }

	template<typename T>
	void init(TIFF* tif);
};
extern template void TileReader::init<uint16_t>(TIFF* tif);

void checkSlice(TIFF* tif, unsigned int* pwidth, unsigned int* pheight, unsigned short* pspp, unsigned short* pbps);

bool tiff_to_tile(TIFF* tif, const std::filesystem::path& outfn, std::array<uint32_t, 2> tilesize);
void tiff_to_tile(const char* input_fn, const std::filesystem::path& output_fn, std::array<uint32_t, 2> tilesize);
using PerTile=std::function<bool(unsigned int,unsigned int,unsigned int,unsigned int,const void*)>;
bool tiff_to_tile_safe(TIFF* tif, const std::filesystem::path& outfn, std::array<uint32_t, 2> tilesize, PerTile per_tile_in={}, std::mutex* seq_write=nullptr);
bool tiff_read_as_tiled(TIFF* tif, unsigned int tilew, unsigned int tileh, PerTile per_tile, std::mutex* seq_read=nullptr, const std::array<unsigned int, 2>* range=nullptr);
void tiff_to_hash(const char* input_fn);
