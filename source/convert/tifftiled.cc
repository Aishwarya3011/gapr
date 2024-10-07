#include "loadtiff.hh"

#include "gapr/utility.hh"

#include <cstring>
#include <mutex>
#include <tiffio.h>
#include <unordered_map>
#include <memory>
#include <queue>
#include <cassert>
#include <array>
#include <fstream>
#include <cinttypes>
#include <iomanip>

#ifdef __unix__
#include <unistd.h>
#include <fcntl.h>
#endif

#include <openssl/evp.h>

#include <zlib.h>

#include "../corelib/libc-wrappers.hh"

#include "config.hh"

namespace gapr {
class checksum {
public:
	bool init() {
		sum=::crc32(0, nullptr, 0);
		len=0;
		return true;
	}
	bool update(const void* buf, std::size_t len) {
		sum=::crc32(sum, static_cast<const unsigned char*>(buf), len);
		this->len+=len;
		return true;
	}
	bool combine(checksum r) {
		sum=::crc32_combine(sum, r.sum, r.len);
		this->len+=r.len;
		return true;
	}
	unsigned long value() const noexcept { return sum; }
private:
	unsigned long sum;
	std::size_t len;
};
}

struct tile_range {
	struct iterator {
		auto operator*() const noexcept { return ptr->get(); }
		bool operator!=(iterator r) const noexcept { return ptr!=r.ptr; }
		iterator operator++() noexcept { ptr=ptr->inc(); return *this; }
		tile_range* ptr;
	};
	constexpr tile_range(unsigned int width, unsigned int height, unsigned int tilew, unsigned int tileh):
		selected{nullptr}, width{width}, height{height}, tilew{tilew}, tileh{tileh}, cur_xy{0, 0}
	{ }
	iterator begin() noexcept {
		if(selected) {
			idx=0;
			return {this};
		}
		cur_xy={0, 0};
		return {this};
	}
	iterator end() const noexcept {
		return {nullptr};
	}

	void select(const std::array<unsigned int, 2>* sel) noexcept {
		selected=sel;
	}

private:
	const std::array<unsigned int, 2>* selected;
	unsigned int width, height;
	unsigned int tilew, tileh;
	union {
		unsigned int idx;
		std::array<unsigned int, 2> cur_xy;
	};

	std::array<unsigned int, 4> get_with(std::array<unsigned int, 2> xy) const noexcept {
		auto x1=std::min(tilew+xy[0], width);
		auto y1=std::min(tileh+xy[1], height);
		if(false) fprintf(stderr, "iter tile: %u %u %u %u\n", xy[0], xy[1], x1, y1);
		return {xy[0], xy[1], x1, y1};
	}
	std::array<unsigned int, 4> get() const noexcept {
		if(selected)
			return get_with(selected[idx]);
		return get_with(cur_xy);
	}
	tile_range* inc() noexcept {
		if(selected) {
			if(selected[++idx]==std::array{width, height})
				return nullptr;
			if(idx>0)
				assert(selected[idx][1]>=selected[idx-1][1]);
			return this;
		}
		cur_xy[0]+=tilew;
		if(cur_xy[0]>=width) {
			cur_xy[1]+=tileh;
			if(cur_xy[1]>=height)
				return nullptr;
			cur_xy[0]=0;
		}
		return this;
	}
};

template<typename T>
static void copy_tile(T* dst, unsigned int dst_x0, unsigned int dst_y0, unsigned int dst_tw, unsigned int dst_th,
		const T* src, unsigned int src_x0, unsigned int src_y0, unsigned int src_tw, unsigned int src_th,
		unsigned int x1, unsigned int y1, unsigned int spp) noexcept {
	unsigned int y_a=std::max(src_y0, dst_y0);
	unsigned int y_b=std::min(src_y0+src_th, y1);
	unsigned int x_a=std::max(src_x0, dst_x0);
	unsigned int x_b=std::min(src_x0+src_tw, x1);
	for(unsigned int s=0; s<spp; ++s) {
		auto optr=&dst[s*dst_th*dst_tw+x_a-dst_x0];
		auto ptr=&src[s*src_th*src_tw+x_a-src_x0];
		auto len=sizeof(T)*(x_b-x_a);
		for(auto y=y_a; y<y_b; ++y)
			std::memcpy(&optr[(y-dst_y0)*dst_tw], &ptr[(y-src_y0)*src_tw], len);
	}
}

template<typename T, typename Func> bool copy_pixels(TileReader& aux, TIFF* tif, Func func, uint32_t tilew, uint32_t tileh, PerTile per_tile_in={}, unsigned long* sum=nullptr, const std::array<unsigned int, 2>* range=nullptr) {
	unsigned int spp=aux.spp;
	aux.init<T>(tif);
	std::vector<uint8_t> tmpbuf;
	struct cache_ent {
		std::unique_ptr<T[]> dat;
		uint32_t yval;
	};
	auto cmp_ent=[](cache_ent* a, cache_ent* b) {
		return a->yval>b->yval;
	};
	std::unordered_map<std::size_t, cache_ent> inbufs;
	std::priority_queue recycle{cmp_ent, std::vector<cache_ent*>{}};
	auto get_tile=[&aux,&inbufs,&recycle,tif,&tmpbuf,per_tile_in](unsigned int x, unsigned int y, unsigned int oy0) ->const T* {
		auto key=std::size_t{y}*aux.width+x;
		auto [it, ins]=inbufs.emplace(key, cache_ent{});
		if(ins) {
			it->second.yval=y;
			if(!recycle.empty() && recycle.top()->yval+aux.tileH<=oy0) {
				it->second.dat=std::move(recycle.top()->dat);
				recycle.pop();
			} else {
				it->second.dat=std::make_unique<T[]>(aux.tileW*aux.tileH*aux.spp);
			}
			aux(tif, tmpbuf, x, y, &it->second.dat[0]);
			if(per_tile_in) {
				if(!per_tile_in(x, y, aux.tileW, aux.tileH, &it->second.dat[0]))
					return nullptr;
			}
			recycle.push(&it->second);
		}
		assert(it->second.yval==y);
		return &it->second.dat[0];
	};
	std::vector<T> obuf{};
	obuf.resize(tilew*tileh*spp);
	std::vector<gapr::checksum> sums;
	if(sum) {
		for(unsigned int y=0; y<tileh; ++y)
			for(unsigned int s=0; s<spp; ++s) {
				auto& sum=sums.emplace_back();
				sum.init();
			}
		auto& t=sums[0];
		t.update(&aux.width, sizeof(aux.width));
		t.update(&aux.height, sizeof(aux.height));
	}
	tile_range rng{aux.width, aux.height, tilew, tileh};
	rng.select(range);
	for(auto [ox0, oy0, ox1, oy1]: rng) {
		unsigned int y0=oy0/aux.tileH*aux.tileH;
		do {
			unsigned int x0=ox0/aux.tileW*aux.tileW;
			do {
				auto ptr=get_tile(x0, y0, oy0);
				if(!ptr)
					return false;
				copy_tile(&obuf[0], ox0, oy0, tilew, tileh, &ptr[0], x0, y0, aux.tileW, aux.tileH, ox1, oy1, spp);
				x0+=aux.tileW;
			} while(x0<aux.width && x0<ox0+tilew);
			y0+=aux.tileH;
		} while(y0<aux.height && y0<oy0+tileh);

		if(!func(ox0, oy0, tilew, tileh, &obuf[0]))
			return false;

		if(sum) {
			gapr::checksum t;
			bool lastx=ox0+tilew>=aux.width;
			if(lastx)
				t.init();
			for(unsigned int yy=0; yy+oy0<oy1; ++yy) {
				for(unsigned int s=0; s<spp; ++s) {
					auto& tt=sums[yy*spp+s];
					tt.update(&obuf[(s*tileh+yy)*tilew], (ox1-ox0)*sizeof(T));
					if(lastx) {
						t.combine(tt);
						tt.init();
					}
				}
			}
			if(lastx)
				sums[0]=t;
		}
	}
	if(sum)
		*sum=sums[0].value();
	return true;
}

static bool tiff_to_tile(TIFF* tif, TIFF* otif, std::array<uint32_t, 2> tilesize={0,0}, PerTile per_tile_in={}) {
	auto get_and_set_if=[tif,otif](ttag_t tag, auto value) {
		if(!TIFFGetField(tif, tag, &value))
			return true;
		if(!TIFFSetField(otif, tag, value))
			return false;
		return true;
	};
	auto get_and_set=[tif,otif](ttag_t tag, auto& value) {
		if(!TIFFGetField(tif, tag, &value))
			return false;
		if(!TIFFSetField(otif, tag, value))
			return false;
		return true;
	};
	auto set=[otif](ttag_t tag, auto value) {
		if(!TIFFSetField(otif, tag, value))
			return false;
		return true;
	};
	uint32_t width, height;
	if(!get_and_set(TIFFTAG_IMAGEWIDTH, width))
		return false;
	if(!get_and_set(TIFFTAG_IMAGELENGTH, height))
		return false;
	uint16_t bps, spp;
	if(!get_and_set(TIFFTAG_BITSPERSAMPLE, bps))
		return false;
	if(!get_and_set(TIFFTAG_SAMPLESPERPIXEL, spp))
		return false;

	/*! COMPRESSION_LZW (vs. COMPRESSION_ADOBE_DEFLATE):
	 * significantly faster, slightly bigger file.
	 * no predictor: smaller output file.
	 */
	if(!get_and_set_if(TIFFTAG_COMPRESSION, uint16_t{}))
		return false;
	if(!get_and_set_if(TIFFTAG_PREDICTOR, uint16_t{}))
		return false;

	if(!get_and_set_if(TIFFTAG_PHOTOMETRIC, uint16_t{}))
		return false;
	if(!get_and_set_if(TIFFTAG_FILLORDER, uint16_t{}))
		return false;
	if(!get_and_set_if(TIFFTAG_ORIENTATION, uint16_t{}))
		return false;

	/*! smaller, square: smaller output file */
	auto [tilewidth, tileheight]=tilesize;
	bool strips{false};
	if(tilewidth==0) {
		if(tileheight==0) {
			tilewidth=tileheight=1024;
		} else {
			strips=true;
			tilewidth=width;
		}
	} else {
		if(tileheight==0)
			tileheight=tilewidth;
	}
	if(!strips) {
	if(!set(TIFFTAG_TILEWIDTH, tilewidth))
		return false;
	if(!set(TIFFTAG_TILELENGTH, tileheight))
		return false;
	} else {
		if(!set(TIFFTAG_ROWSPERSTRIP, tileheight))
			return false;
	}
	if(!set(TIFFTAG_PLANARCONFIG, PLANARCONFIG_SEPARATE))
		return false;
	if(!set(TIFFTAG_SOFTWARE, PACKAGE_NAME " " PACKAGE_VERSION))
		return false;

	if(bps==0 || bps%8!=0)
		return false;
	TileReader aux{width, height, spp, bps};
	auto func=[otif,tilewidth=tilewidth,tileheight=tileheight,spp,strips](unsigned int x, unsigned int y, unsigned int tilew, unsigned int tileh, auto* buf) {
		assert(tilew==tilewidth);
		assert(tileh==tileheight);
		auto len=sizeof(buf[0])*tilew*tileh;
		if(!strips) {
		assert(static_cast<std::size_t>(TIFFTileSize(otif))==len);
		for(unsigned int s=0; s<spp; ++s) {
			if(-1==TIFFWriteTile(otif, &buf[s*tilew*tileh], x, y, 0, s))
				return false;
		}
		} else {
			assert(static_cast<std::size_t>(TIFFStripSize(otif))==len);
			for(unsigned int s=0; s<spp; ++s) {
				if(-1==TIFFWriteEncodedStrip(otif, TIFFComputeStrip(otif, y, s), &buf[s*tilew*tileh], len))
					return false;
			}
		}
		return true;
	};
	bool r{false};
	unsigned long sum;
	if(bps<=8)
		r=copy_pixels<uint8_t>(aux, tif, func, tilewidth, tileheight, per_tile_in, &sum);
	else if(bps<=16)
		r=copy_pixels<uint16_t>(aux, tif, func, tilewidth, tileheight, per_tile_in, &sum);
	if(!r)
		return false;
	{
		std::ostringstream oss;
		oss<<aux.tileW<<':'<<aux.tileH;
		oss<<":0x"<<std::setbase(16)<<std::setw(8)<<std::setfill('0')<<sum;
		const char* prev_desc;
		if(::TIFFGetField(tif, TIFFTAG_IMAGEDESCRIPTION, &prev_desc))
			oss<<'\n'<<prev_desc;
		auto desc=oss.str();
		if(!set(TIFFTAG_IMAGEDESCRIPTION, desc.c_str()))
			return false;
	}

	if(!TIFFWriteDirectory(otif))
		return false;
	return true;
}

bool tiff_to_tile(TIFF* tif, const std::filesystem::path& outfn) {
	Tiff otif{outfn, "w8m"};
	if(!tiff_to_tile(tif, otif))
		return false;
	if(!otif.flush())
		return false;
	return true;
}

void tiff_to_tile(const char* input_fn, const std::filesystem::path& output_fn, std::array<uint32_t, 2> tilesize) {
	Tiff tif{input_fn, "r"};
	if(!tiff_to_tile_safe(tif, output_fn, tilesize))
		throw std::runtime_error{"failed to convert"};
}


bool Tiff::flush() {
	if(!::TIFFFlush(_ptr))
		return false;
	auto fd=::TIFFFileno(_ptr);
	assert(fd);
	if(auto r=::fdatasync(fd); r!=0)
		return false;
	//XXX dir sync
	return true;
}

struct Tiff::MemBuf {
	std::vector<char> buf;
	std::size_t pos;
	bool ro;
	static constexpr bool dbg{false};
	static tsize_t read(thandle_t h, tdata_t p, tsize_t n) {
		if(dbg)
			fprintf(stderr, "tiff read %p %zd\n", p, n);
		auto& [buf, pos, ro]=*static_cast<MemBuf*>(h);
		std::size_t cnt=buf.size()-pos;
		if(cnt>static_cast<std::size_t>(n))
			cnt=n;
		std::memcpy(p, &buf[pos], cnt);
		pos+=cnt;
		return cnt;
	}
	static tsize_t write(thandle_t h, tdata_t p, tsize_t n) {
		if(dbg)
			fprintf(stderr, "tiff write %p %zd\n", p, n);
		auto& [buf, pos, ro]=*static_cast<MemBuf*>(h);
		assert(!ro);
		std::size_t s=pos+n;
		if(buf.size()<s)
			buf.resize(s, '\x00');
		std::memcpy(&buf[pos], p, n);
		pos+=n;
		return n;
	}
	static toff_t seek(thandle_t h, toff_t o, int whence) {
		auto& [buf, pos, ro]=*static_cast<MemBuf*>(h);
		if(dbg)
			fprintf(stderr, "tiff seek %" PRIu64 " %d %zd/%zd\n", o, whence, pos, buf.size());
		std::size_t next;
		int e=0;
		switch(whence) {
		case SEEK_SET:
			next=o;
			break;
		case SEEK_CUR:
			next=pos+o;
			break;
		case SEEK_END:
			next=buf.size()+o;
			break;
		default:
			e=EINVAL;
			assert(0);
		}
		if(next<0)
			e=EINVAL;
		if(e) {
			errno=e;
			return -1;
		}
		pos=next;
		return next;
	}
	static int close(thandle_t h) {
		if(dbg)
			fprintf(stderr, "tiff close\n");
		//auto mb=static_cast<MemBuf*>(h);
		return 0;
	}
	static toff_t size(thandle_t h) {
		if(dbg)
			fprintf(stderr, "tiff size\n");
		auto& [buf, pos, ro]=*static_cast<MemBuf*>(h);
		return buf.size();
	}
#if 0
	static int map(thandle_t, tdata_t*, toff_t*) {
	}
	static void unmap(thandle_t, tdata_t, toff_t) {
	}
#endif
};
Tiff::Tiff(const char* m): _ptr{nullptr}, _buf{std::make_shared<MemBuf>()} {
	unsigned int nw=0;
	for(auto p=m; *p; ++p) {
		if(*p=='w') ++nw;
	}
	assert(nw>0);
	_buf->pos=0;
	_buf->ro=false;
	_ptr=::TIFFClientOpen("", m, _buf.get(),
			&MemBuf::read, &MemBuf::write, &MemBuf::seek, &MemBuf::close, &MemBuf::size,
			nullptr, nullptr);
}
Tiff::Tiff(std::vector<char>&& buf, const char* m): _ptr{nullptr}, _buf{std::make_shared<MemBuf>()} {
	unsigned int nw=0;
	for(auto p=m; *p; ++p) {
		if(*p=='w') ++nw;
	}
	assert(nw==0);
	_buf->pos=0;
	_buf->ro=true;
	_buf->buf=std::move(buf);
	_ptr=::TIFFClientOpen("", m, _buf.get(),
			&MemBuf::read, &MemBuf::write, &MemBuf::seek, &MemBuf::close, &MemBuf::size,
			nullptr, nullptr);
}

std::vector<char> Tiff::buffer() && noexcept {
	assert(_buf);
	auto buf=std::move(_buf->buf);
	return buf;
}

template<typename T> bool check_tiff_integrity(TileReader& aux, TIFF* tif) {
	aux.init<T>(tif);
	std::vector<uint8_t> tmpbuf;
	auto dat=std::make_unique<T[]>(std::size_t{aux.tileW}*aux.tileH*aux.spp);
	try {
		for(uint32_t y0=0; y0<aux.height; y0+=aux.tileH) {
			for(uint32_t x0=0; x0<aux.width; x0+=aux.tileW) {
				aux(tif, tmpbuf, x0, y0, &dat[0]);
			}
		}
	} catch(const std::runtime_error& e) {
		gapr::print("read error: ", e.what());
		return false;
	}
	return true;
}
static bool check_tiff_integrity(TIFF* tif) {
	uint32_t width, height;
	if(!::TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width))
		return false;
	if(!::TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height))
		return false;
	uint16_t bps, spp;
	if(!::TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bps))
		return false;
	if(!::TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &spp))
		return false;
	if(bps==0 || bps%8!=0)
		return false;

	TileReader aux{width, height, spp, bps};
	bool r{false};
	if(bps<=8)
		r=check_tiff_integrity<uint8_t>(aux, tif);
	else if(bps<=16)
		r=check_tiff_integrity<uint16_t>(aux, tif);
	if(!r)
		return false;
	return true;
}

namespace gapr {
class digest {
public:
	explicit digest(): _c{::EVP_MD_CTX_create()} { }
	~digest() { if(_c) ::EVP_MD_CTX_destroy(_c); }
	digest(const digest&) =delete;
	digest& operator=(const digest&) =delete;
	//mc //m=

	bool init(const EVP_MD* md) {
		//if(!::EVP_get_digestbyname(name))
		//return false;
		return ::EVP_DigestInit_ex(_c, md, nullptr);
	}
	bool update(const void* d, std::size_t cnt) {
		return EVP_DigestUpdate(_c, d, cnt);
	}
	using mdbuf=std::array<unsigned char, EVP_MAX_MD_SIZE>;
	unsigned int finish(mdbuf& buf) {
		unsigned int n;
		if(!::EVP_DigestFinal_ex(_c, &buf[0], &n))
			return 0;
		return n;
	}

private:
	EVP_MD_CTX* _c;
};
}

bool check_file_integrity(const std::filesystem::path& fn, const char* refbuf, std::size_t reflen) {
	gapr::digest::mdbuf sum1{'0'}, sum2{'1'};
	gapr::digest ctx;
	if(!ctx.init(::EVP_sha256()))
		return false;
	if(!ctx.update(&refbuf[0], reflen))
		return false;
	auto len1=ctx.finish(sum1);
	if(!len1)
		return false;

	char buf[BUFSIZ];
	gapr::file_stream file{fn.c_str(), "rb"};
	if(!file)
		return false;
	if(!ctx.init(EVP_sha256()))
		return false;
	do {
		auto n=::fread(&buf[0], 1, BUFSIZ, file);
		if(n>0) {
			if(!ctx.update(&buf[0], n))
				return false;
		}
		if(n<BUFSIZ) {
			if(::ferror(file))
				return false;
			break;
		}
	} while(true);
	assert(::feof(file));
#ifdef __linux__
	{
		::posix_fadvise(::fileno(file), 0, 0, POSIX_FADV_DONTNEED);
	}
#endif

	auto len2=ctx.finish(sum2);
	if(!len2 || len2!=len1)
		return false;
	if(std::memcmp(&sum1[0], &sum2[0], len2)!=0)
		return false;
	return true;
}

/*! LZW cpu usage is high
 * tiffcp is similarly slow, so not much room for improvements.
 */
bool tiff_to_tile_safe(TIFF* tif, const std::filesystem::path& outfn, std::array<uint32_t, 2> tilesize, PerTile per_tile_in, std::mutex* seq_write) {
	auto mark_error=[&outfn](const char* errext) {
		auto fn=outfn;
		fn.replace_extension(errext);
		std::ofstream f{fn};
		f<<errext;
		f.close();
	};

	gapr::print("tiff write membuf");
	// write in buffer
	std::vector<char> buf;
	{
		Tiff otif{"w8m"};
		if(!tiff_to_tile(tif, otif, tilesize, per_tile_in))
			return false;
		if(!::TIFFFlush(otif))
			return false;
		buf=std::move(otif).buffer();
	}

	// check buffer integrity
	if(false) {
		gapr::print("tiff check membuf");
		Tiff chktif{std::move(buf), "rm"};
		if(!check_tiff_integrity(chktif)) {
			mark_error(".tif-errorbuff");
			return false;
		}
		buf=std::move(chktif).buffer();
	}

	gapr::print("tiff write file");
	auto tmpfn=outfn;
	tmpfn.replace_extension(".tif-temp");
	// write to file
	std::unique_lock<std::mutex> lck{};
	if(seq_write)
		lck=std::unique_lock{*seq_write};
	gapr::file_stream f{tmpfn.c_str(), "wb"};
	if(!f)
		return false;
	auto n=std::fwrite(&buf[0], 1, buf.size(), f);
	if(n!=buf.size())
		return false;
	if(::fflush(f)!=0)
		return false;
	auto fd=::fileno(f);
	if(auto r=::fdatasync(fd); r!=0)
		return false;

	if(seq_write)
		lck.unlock();

#ifdef __linux__
	{
		::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
	}
#endif
	if(!f.close())
		return false;

	gapr::print("tiff rename file");
	std::error_code ec{};
	std::filesystem::rename(tmpfn, outfn, ec);
	if(ec)
		return false;
	{
		gapr::file_stream dir{outfn.parent_path().c_str(), "r"};
		if(::fdatasync(::fileno(dir))!=0)
			return false;
	}

	// check file integrity
	if(false) {
		gapr::print("tiff check file");
		if(!check_file_integrity(outfn, &buf[0], buf.size())) {
			mark_error(".tif-errorfile");
			return false;
		}
	}
	gapr::print("tiff done file");

	return true;
}

static bool tiff_to_hash(TIFF* tif, unsigned long& sum) {
	uint32_t width, height;
	if(!::TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width))
		return false;
	if(!::TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height))
		return false;
	uint16_t bps, spp;
	if(!::TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bps))
		return false;
	if(!::TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &spp))
		return false;
	if(bps==0 || bps%8!=0)
		return false;

	gapr::checksum ctx;
	if(!ctx.init())
		return false;
	if(!ctx.update(&width, sizeof(width)))
		return false;
	if(!ctx.update(&height, sizeof(height)))
		return false;

	auto func=[&ctx,spp](unsigned int x, unsigned int y, unsigned int tilew, unsigned int tileh, const auto* buf) {
		for(unsigned int s=0; s<spp; ++s) {
			if(!ctx.update(&buf[s*tilew*tileh], sizeof(buf[0])*tilew*tileh))
				return false;
		}
		return true;
	};
	TileReader aux{width, height, spp, bps};
	bool r{false};
	if(bps<=8)
		r=copy_pixels<uint8_t>(aux, tif, func, width, 1);
	else if(bps<=16)
		r=copy_pixels<uint16_t>(aux, tif, func, width, 1);
	if(!r)
		return false;
	sum=ctx.value();
	return true;
}
void tiff_to_hash(const char* input_fn) {
	Tiff tif{input_fn, "r"};
	unsigned long res{0};
	if(!tiff_to_hash(tif, res)) {
		fprintf(stderr, "error: %s\n", input_fn);
	}
	fprintf(stdout, "0x%08lx: %s\n", res, input_fn);
}

bool tiff_read_as_tiled(TIFF* tif, unsigned int tilew, unsigned int tileh, PerTile per_tile, std::mutex* seq_read, const std::array<unsigned int, 2>* range) {
	uint32_t width, height;
	if(!::TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width))
		return false;
	if(!::TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height))
		return false;
	uint16_t bps, spp;
	if(!::TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bps))
		return false;
	if(!::TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &spp))
		return false;
	if(bps==0 || bps%8!=0)
		return false;

	TileReader aux{width, height, spp, bps};
	bool r{false};
	if(bps<=8)
		r=copy_pixels<uint8_t>(aux, tif, per_tile, tilew, tileh, nullptr, nullptr, range);
	else if(bps<=16)
		r=copy_pixels<uint16_t>(aux, tif, per_tile, tilew, tileh, nullptr, nullptr, range);
	if(!r)
		return false;

	return true;
}
