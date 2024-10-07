/* cube-loader-tiff.cc
 *
 * Copyright (C) 2018 GOU Lingfeng
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


#include "gapr/cube-loader.hh"

#include "gapr/cube.hh"
#include "gapr/streambuf.hh"
#include "gapr/utility.hh"

#include <vector>

#include <tiffio.h>

using gapr::Streambuf;


extern "C" tsize_t tiff_write_proc(thandle_t fh, tdata_t ptr, tsize_t n) {
	return 0;
}
extern "C" tsize_t tiff_read_proc(thandle_t fh, tdata_t ptr, tsize_t n) {
	auto buf=static_cast<Streambuf*>(fh);
	return buf->sgetn(static_cast<char*>(ptr), n);
}
extern "C" toff_t tiff_seek_proc(thandle_t fh, toff_t off, int whence) {
	auto fs=static_cast<Streambuf*>(fh);
	switch(whence) {
		case SEEK_SET:
			fs->pubseekoff(off, std::ios_base::beg, std::ios_base::in);
			break;
		case SEEK_CUR:
			fs->pubseekoff(off, std::ios_base::cur, std::ios_base::in);
			break;
		case SEEK_END:
			fs->pubseekoff(off, std::ios_base::end, std::ios_base::in);
			break;
	}
	auto ret=fs->pubseekoff(0, std::ios_base::cur, std::ios_base::in);
	return ret;
}
extern "C" int tiff_close_proc(thandle_t fh) {
	//auto fs=static_cast<InputStream*>(fh);
	return 1;
}
extern "C" toff_t tiff_size_proc(thandle_t fh) {
	auto fs=static_cast<Streambuf*>(fh);
	auto pos=fs->pubseekoff(0, std::ios_base::cur, std::ios_base::in);
	fs->pubseekoff(0, std::ios_base::end, std::ios_base::in);
	auto ret=fs->pubseekoff(0, std::ios_base::cur, std::ios_base::in);
	fs->pubseekoff(pos, std::ios_base::beg, std::ios_base::in);
	return ret;
}

template<bool TILED>
inline uint64_t getTileSize(TIFF* tif);
template<>
inline uint64_t getTileSize<true>(TIFF* tif) {
	return TIFFTileSize(tif);
}
template<>
inline uint64_t getTileSize<false>(TIFF* tif) {
	return TIFFStripSize(tif);
}

template<uint16 PC>
inline uint64_t getTileBufSize(uint64_t tileSize, uint16 spp);
template<>
inline uint64_t getTileBufSize<PLANARCONFIG_CONTIG>(uint64_t tileSize, uint16 spp) {
	return tileSize;
}
template<>
inline uint64_t getTileBufSize<PLANARCONFIG_SEPARATE>(uint64_t tileSize, uint16 spp) {
	return tileSize*spp;
}

template<bool TILED>
inline uint32 getYCoord(uint32 j, uint32 h);
template<>
inline uint32 getYCoord<true>(uint32 j, uint32 h) {
	return h-1-j;
}
template<>
inline uint32 getYCoord<false>(uint32 j, uint32 h) {
	return j;
}

template<bool TILED, uint16 PC>
inline void readTile(TIFF* tif, uint8_t* bufTile, int64_t tileSize, uint16 spp, uint32 x, uint32 y);
template<>
inline void readTile<true, PLANARCONFIG_CONTIG>(TIFF* tif, uint8_t* bufTile, int64_t tileSize, uint16 spp, uint32 x, uint32 y) {
	auto tile=TIFFComputeTile(tif, x, y, 0, 0);
	if(-1==TIFFReadEncodedTile(tif, tile, bufTile, tileSize))
		throw std::system_error{std::make_error_code(std::io_errc::stream), "TIFFReadEncodedTile"};
}
template<>
inline void readTile<false, PLANARCONFIG_CONTIG>(TIFF* tif, uint8_t* bufTile, int64_t tileSize, uint16 spp, uint32 x, uint32 y) {
	auto strip=TIFFComputeStrip(tif, y, 0);
	if(-1==TIFFReadEncodedStrip(tif, strip, bufTile, tileSize))
		gapr::report("Failed to read strip.\n");
}
template<>
inline void readTile<true, PLANARCONFIG_SEPARATE>(TIFF* tif, uint8_t* bufTile, int64_t tileSize, uint16 spp, uint32 x, uint32 y) {
	for(tsample_t s=0; s<spp; s++) {
		auto tile=TIFFComputeTile(tif, x, y, 0, s);
		if(-1==TIFFReadEncodedTile(tif, tile, bufTile+s*tileSize, tileSize))
			gapr::report("Failed to read tile.\n");
	}
}
template<>
inline void readTile<false, PLANARCONFIG_SEPARATE>(TIFF* tif, uint8_t* bufTile, int64_t tileSize, uint16 spp, uint32 x, uint32 y) {
	for(tsample_t s=0; s<spp; s++) {
		auto strip=TIFFComputeStrip(tif, y, s);
		if(-1==TIFFReadEncodedStrip(tif, strip, bufTile+s*tileSize, tileSize))
			gapr::report("Failed to read strip.\n");
	}
}

template<uint16 PC>
inline uint8_t* getBufStart(uint8_t* bufTile, uint64_t tileSize, tsample_t s);
template<>
inline uint8_t* getBufStart<PLANARCONFIG_CONTIG>(uint8_t* bufTile, uint64_t tileSize, tsample_t s) {
	return bufTile;
}
template<>
inline uint8_t* getBufStart<PLANARCONFIG_SEPARATE>(uint8_t* bufTile, uint64_t tileSize, tsample_t s) {
	return bufTile+s*tileSize;
}

template<uint16 PC>
inline uint32 getBufIndex(uint32 idx, uint16 spp, tsample_t s);
template<>
inline uint32 getBufIndex<PLANARCONFIG_CONTIG>(uint32 idx, uint16 spp, tsample_t s) {
	return idx*spp+s;
}
template<>
inline uint32 getBufIndex<PLANARCONFIG_SEPARATE>(uint32 idx, uint16 spp, tsample_t s) {
	return idx;
}

const int conv1bit[]={0, 255};
const int conv2bit[]={0, 85, 170, 255};
const int conv4bit[]={0, 17, 34, 51, 68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255};

template<uint16 BPS, typename T>
inline T getValuePal(uint8_t* bufStart, uint32 bufIdx);
template<>
inline uint8_t getValuePal<1, uint8_t>(uint8_t* bufStart, uint32 bufIdx) {
	return (bufStart[bufIdx/8]>>(7-bufIdx%8))&0x01;
}
template<>
inline uint8_t getValuePal<2, uint8_t>(uint8_t* bufStart, uint32 bufIdx) {
	return (bufStart[bufIdx/4]>>(3-bufIdx%4)*2)&0x03;
}
template<>
inline uint8_t getValuePal<4, uint8_t>(uint8_t* bufStart, uint32 bufIdx) {
	return (bufStart[bufIdx/2]>>(1-bufIdx%2)*4)&0x0F;
}
template<>
inline uint8_t getValuePal<8, uint8_t>(uint8_t* bufStart, uint32 bufIdx) {
	return bufStart[bufIdx];
}
template<>
inline uint16_t getValuePal<16, uint16_t>(uint8_t* bufStart, uint32 bufIdx) {
	uint16* p=reinterpret_cast<uint16*>(bufStart+bufIdx*2);
	return *p;
}

template<uint16 BPS, typename T>
inline T getValue(uint8_t* bufStart, uint32 bufIdx);
template<>
inline uint8_t getValue<1, uint8_t>(uint8_t* bufStart, uint32 bufIdx) {
	return conv1bit[(bufStart[bufIdx/8]>>(7-bufIdx%8))&0x01];
}
template<>
inline uint8_t getValue<2, uint8_t>(uint8_t* bufStart, uint32 bufIdx) {
	return conv2bit[(bufStart[bufIdx/4]>>(3-bufIdx%4)*2)&0x03];
}
template<>
inline uint8_t getValue<4, uint8_t>(uint8_t* bufStart, uint32 bufIdx) {
	return conv4bit[(bufStart[bufIdx/2]>>(1-bufIdx%2)*4)&0x0F];
}
template<>
inline uint8_t getValue<8, uint8_t>(uint8_t* bufStart, uint32 bufIdx) {
	return bufStart[bufIdx];
}
template<>
inline uint16_t getValue<16, uint16_t>(uint8_t* bufStart, uint32 bufIdx) {
	uint16_t* p=reinterpret_cast<uint16_t*>(bufStart+bufIdx*2);
	return *p;
}

template<typename T, typename T0, uint16 BPS, uint16 PC, bool TILED>
inline void convertTilePal(TIFF* tif, uint64_t width, uint64_t height, std::vector<uint8_t>& bufTile, char* bufSlice, uint32 tileW, uint32 tileH, uint16 spp, int64_t ystride) {
	uint16* colors[3];
	if(!TIFFGetField(tif, TIFFTAG_COLORMAP, &colors[0], &colors[1], &colors[2]))
		gapr::report("Failed to read colormap.\n");
	auto tileSize=getTileSize<TILED>(tif);
	auto tileBufSize=getTileBufSize<PC>(tileSize, spp);
	if(bufTile.size()<tileBufSize)
		bufTile.resize(tileBufSize);
	uint64_t tilenx=((width+tileW-1)/tileW);
	uint64_t tilen=((height+tileH-1)/tileH)*tilenx;
	for(uint64_t tilei=0; tilei<tilen; tilei++) {
		auto y=tilei/tilenx*tileH;
		auto x=tilei%tilenx*tileW;
		readTile<TILED, PC>(tif, bufTile.data(), tileSize, 1, x, y);
		uint32 w=tileW;
		if(w+x>width) {
			w=width-x;
		}
		uint32 h=tileH;
		if(h+y>height) {
			h=height-y;
		}
		for(tsample_t s=0; s<1; s++) {
			uint8_t *bufStart=getBufStart<PC>(bufTile.data(), tileSize, s);
			for(uint32 j=0; j<h; j++) {
				auto bufOut=reinterpret_cast<T*>(bufSlice+x*sizeof(T)+(y+getYCoord<TILED>(j, h))*ystride);
				for(uint32 i=0; i<w; i++) {
					auto bufIdx=getBufIndex<PC>(j*tileW+i, spp, s);
					auto vp=getValuePal<BPS, T0>(bufStart, bufIdx);
					for(tsample_t c=0; c<1; c++) {
						auto v=colors[c][vp];
						T ov;
						ov=v;
						bufOut[i]=ov;
					}
				}
			}
		}
	}
}
template<typename T, typename T0, uint16 BPS, uint16 PC, bool TILED>
inline void convertTile(TIFF* tif, uint64_t width, uint64_t height, std::vector<uint8_t>& bufTile, char* bufSlice, uint32 tileW, uint32 tileH, uint16 spp, int64_t ystride) {
	auto tileSize=getTileSize<TILED>(tif);
	auto tileBufSize=getTileBufSize<PC>(tileSize, spp);
	if(bufTile.size()<tileBufSize)
		bufTile.resize(tileBufSize);

	uint64_t tilenx=((width+tileW-1)/tileW);
	uint64_t tilen=((height+tileH-1)/tileH)*tilenx;
	for(uint64_t tilei=0; tilei<tilen; tilei++) {
		auto y=tilei/tilenx*tileH;
		auto x=tilei%tilenx*tileW;
		readTile<TILED, PC>(tif, bufTile.data(), tileSize, 1, x, y);
		uint32 w=tileW;
		if(w+x>width) {
			w=width-x;
		}
		uint32 h=tileH;
		if(h+y>height) {
			h=height-y;
		}
		for(tsample_t s=0; s<1; s++) {
			uint8_t *bufStart=getBufStart<PC>(bufTile.data(), tileSize, s);
			for(uint32 j=0; j<h; j++) {
				auto bufOut=reinterpret_cast<T*>(bufSlice+x*sizeof(T)+(y+getYCoord<TILED>(j, h))*ystride);
				for(uint32 i=0; i<w; i++) {
					auto bufIdx=getBufIndex<PC>(j*tileW+i, spp, s);
					auto v=getValue<BPS, T0>(bufStart, bufIdx);
					T ov;
					ov=v;
					bufOut[i]=ov;
				}
			}
		}
	}
}
template<typename T, typename T0, uint16 BPS, uint16 PC>
void convertTile(TIFF* tif, uint64_t width, uint64_t height, std::vector<uint8_t>& bufTile, char* buf, bool tiled, uint32 tileW, uint32 tileH, uint16 spp, int64_t ystride) {
	if(tiled) {
		return convertTile<T, T0, BPS, PC, true>(tif, width, height, bufTile, buf, tileW, tileH, spp, ystride);
	} else {
		return convertTile<T, T0, BPS, PC, false>(tif, width, height, bufTile, buf, tileW, tileH, spp, ystride);
	}
}
template<typename T, typename T0, uint16 BPS, uint16 PC>
void convertTilePal(TIFF* tif, uint64_t width, uint64_t height, std::vector<uint8_t>& bufTile, char* buf, bool tiled, uint32 tileW, uint32 tileH, uint16 spp, int64_t ystride) {
	if(tiled) {
		return convertTilePal<T, T0, BPS, PC, true>(tif, width, height, bufTile, buf, tileW, tileH, spp, ystride);
	} else {
		return convertTilePal<T, T0, BPS, PC, false>(tif, width, height, bufTile, buf, tileW, tileH, spp, ystride);
	}
}
template<typename T, typename T0, uint16 BPS>
void convertTile(TIFF* tif, uint64_t width, uint64_t height, std::vector<uint8_t>& bufTile, char* buf, uint16 pc, bool tiled, uint32 tileW, uint32 tileH, uint16 spp, int64_t ystride) {
	switch(pc) {
		case PLANARCONFIG_CONTIG:
			return convertTile<T, T0, BPS, PLANARCONFIG_CONTIG>(tif, width, height, bufTile, buf, tiled, tileW, tileH, spp, ystride);
		case PLANARCONFIG_SEPARATE:
			return convertTile<T, T0, BPS, PLANARCONFIG_SEPARATE>(tif, width, height, bufTile, buf, tiled, tileW, tileH, spp, ystride);
		default:
			gapr::report("Planar configuration ", pc, " not supported.\n");
	}
}
template<typename T, typename T0, uint16 BPS>
void convertTilePal(TIFF* tif, uint64_t width, uint64_t height, std::vector<uint8_t>& bufTile, char* buf, uint16 pc, bool tiled, uint32 tileW, uint32 tileH, uint16 spp, int64_t ystride) {
	switch(pc) {
		case PLANARCONFIG_CONTIG:
			return convertTilePal<T, T0, BPS, PLANARCONFIG_CONTIG>(tif, width, height, bufTile, buf, tiled, tileW, tileH, spp, ystride);
		case PLANARCONFIG_SEPARATE:
			return convertTilePal<T, T0, BPS, PLANARCONFIG_SEPARATE>(tif, width, height, bufTile, buf, tiled, tileW, tileH, spp, ystride);
		default:
			gapr::report("Planar configuration ", pc, " not supported.\n");
	}
}
template<typename T>
void convertTile(TIFF* tif, uint64_t width, uint64_t height, std::vector<uint8_t>& bufTile, char* buf, uint16 bps, uint16 pc, bool tiled, uint32 tileW, uint32 tileH, uint16 spp, int64_t ystride);
template<>
void convertTile<uint8_t>(TIFF* tif, uint64_t width, uint64_t height, std::vector<uint8_t>& bufTile, char* buf, uint16 bps, uint16 pc, bool tiled, uint32 tileW, uint32 tileH, uint16 spp, int64_t ystride) {
	switch(bps) {
		case 1:
			return convertTile<uint8_t, uint8_t, 1>(tif, width, height, bufTile, buf, pc, tiled, tileW, tileH, spp, ystride);
		case 2:
			return convertTile<uint8_t, uint8_t, 2>(tif, width, height, bufTile, buf, pc, tiled, tileW, tileH, spp, ystride);
		case 4:
			return convertTile<uint8_t, uint8_t, 4>(tif, width, height, bufTile, buf, pc, tiled, tileW, tileH, spp, ystride);
		case 8:
			return convertTile<uint8_t, uint8_t, 8>(tif, width, height, bufTile, buf, pc, tiled, tileW, tileH, spp, ystride);
		case 16:
			return convertTile<uint8_t, uint16_t, 16>(tif, width, height, bufTile, buf, pc, tiled, tileW, tileH, spp, ystride);
		default:
			gapr::report("Bits per sample ", bps, " not supported.\n");
	}
}
template<>
void convertTile<uint16_t>(TIFF* tif, uint64_t width, uint64_t height, std::vector<uint8_t>& bufTile, char* buf, uint16 bps, uint16 pc, bool tiled, uint32 tileW, uint32 tileH, uint16 spp, int64_t ystride) {
	switch(bps) {
		case 16:
			return convertTile<uint16_t, uint16_t, 16>(tif, width, height, bufTile, buf, pc, tiled, tileW, tileH, spp, ystride);
		default:
			gapr::report("Bits per sample ", bps, " not supported.\n");
	}
}
template<typename T>
void convertTilePal(TIFF* tif, uint64_t width, uint64_t height, std::vector<uint8_t>& bufTile, char* buf, uint16 bps, uint16 pc, bool tiled, uint32 tileW, uint32 tileH, uint16 spp, int64_t ystride) {
	switch(bps) {
		case 1:
			return convertTilePal<T, uint8_t, 1>(tif, width, height, bufTile, buf, pc, tiled, tileW, tileH, spp, ystride);
		case 2:
			return convertTilePal<T, uint8_t, 2>(tif, width, height, bufTile, buf, pc, tiled, tileW, tileH, spp, ystride);
		case 4:
			return convertTilePal<T, uint8_t, 4>(tif, width, height, bufTile, buf, pc, tiled, tileW, tileH, spp, ystride);
		case 8:
			return convertTilePal<T, uint8_t, 8>(tif, width, height, bufTile, buf, pc, tiled, tileW, tileH, spp, ystride);
		case 16:
			return convertTilePal<T, uint16_t, 16>(tif, width, height, bufTile, buf, pc, tiled, tileW, tileH, spp, ystride);
		default:
			gapr::report("Bits per sample ", bps, " not supported.\n");
	}
}
template<typename T>
void convertTile(TIFF* tif, uint64_t width, uint64_t height, std::vector<uint8_t>& bufTile, char* buf, int64_t ystride) {
	uint16 pm;
	if(!TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &pm))
		gapr::report("Failed to read PHOTOMETRIC.\n");
	uint16 bps;
	if(!TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bps))
		gapr::report("Failed to read BITSPERSAMPLE.\n");
	uint16 pc;
	if(!TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &pc))
		gapr::report("Failed to read PLANARCONFIG.\n");
	bool tiled=TIFFIsTiled(tif);

	uint32 tileW, tileH;
	if(tiled) {
		if(!TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tileW)
				|| !TIFFGetField(tif, TIFFTAG_TILELENGTH, &tileH))
			gapr::report("Failed to get tile width/length information.\n");
	} else { // A strip is like a tile.
		tileW=width;
		if(!TIFFGetField(tif, TIFFTAG_ROWSPERSTRIP, &tileH))
			gapr::report("Failed to get rows/strip information.");
	}
	uint16 spp;
	if(!TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &spp))
		gapr::report("Failed to read SAMPLESPERPIXEL.\n");

	switch(pm) {
		case PHOTOMETRIC_MINISWHITE:
		case PHOTOMETRIC_MINISBLACK:
		case PHOTOMETRIC_SEPARATED:
		case PHOTOMETRIC_RGB:
			return convertTile<T>(tif, width, height, bufTile, buf, bps, pc, tiled, tileW, tileH, spp, ystride);
		case PHOTOMETRIC_PALETTE:
			return convertTilePal<T>(tif, width, height, bufTile, buf, bps, pc, tiled, tileW, tileH, spp, ystride);
		default:
			// Not supporting colormap
			gapr::report("Photometric type ", pm, " not supported.\n");
	}
}


template<typename T> static inline void readTiffData(TIFF* tif, const std::array<int32_t, 3>& sizes, char* ptr, int64_t ystride, int64_t zstride) {
	std::vector<uint8_t> bufTile;
	if(!TIFFSetDirectory(tif, 0))
		throw std::system_error{std::make_error_code(std::errc::invalid_argument), "TIFFSetDirectory"};
	int32_t z=0;
	do {
		convertTile<T>(tif, sizes[0], sizes[1], bufTile, ptr+z*zstride, ystride);
		z++;
	} while(TIFFReadDirectory(tif));
}



struct TiffHelper {
	TIFF* _f;
	explicit TiffHelper(TIFF* f) noexcept: _f{f} { }
	~TiffHelper() noexcept { if(_f) ::TIFFClose(_f); }
	TiffHelper(const TiffHelper&) =delete;
	TiffHelper& operator=(const TiffHelper&) =delete;
	TIFF* get() noexcept { return _f; }
	TIFF* release() noexcept { auto f=_f; _f=nullptr; return f; }
};

namespace gapr {

	class TiffLoader: public cube_loader {
		public:
			explicit TiffLoader(gapr::Streambuf& file);
			~TiffLoader() override {
				TiffHelper tiff{_tif};
			}

		private:
			void do_load(char* buf, int64_t ystride, int64_t zstride) override;

			TIFF* _tif{nullptr};
	};

}

gapr::TiffLoader::TiffLoader(gapr::Streambuf& file): cube_loader{file} {
	TiffHelper tiff_{TIFFClientOpen("x.tiff", "r", &file, tiff_read_proc, tiff_write_proc, tiff_seek_proc, tiff_close_proc, tiff_size_proc, nullptr, nullptr)};
	if(!tiff_.get())
		throw std::system_error{std::make_error_code(std::errc::invalid_argument), "TIFFClientOpen"};

	auto tif=tiff_.get();
	if(!TIFFSetDirectory(tif, 0))
		throw std::system_error{std::make_error_code(std::errc::invalid_argument), "TIFFSetDirectory"};

	uint16 spp, pm, pc, bps;
	uint32 width, height;
	if(!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width)
			|| !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height))
		gapr::report("No width/length information in file: ");
	if(width==0 || height==0)
		gapr::report("Zero width/length: ");
	if(!TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &spp))
		gapr::report("Failed to read SAMPLESPERPIXEL.");
	if(spp==0)
		gapr::report("Zero sample per pixel");
	if(!TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &pm))
		gapr::report("Failed to read PHOTOMETRIC.");
	switch(pm) {
		case PHOTOMETRIC_MINISWHITE:
		case PHOTOMETRIC_MINISBLACK:
		case PHOTOMETRIC_SEPARATED:
			break;
		case PHOTOMETRIC_RGB:
			if(spp!=3)
				gapr::report("Wrong spp for RGB image");
			break;
		case PHOTOMETRIC_PALETTE:
			if(spp!=1)
				gapr::report("Wrong spp for PALETTE image");
			break;
		default:
			gapr::report("Unsupported photometric");
	}
	if(!TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &pc))
		gapr::report("Failed to read PLANARCONFIG.");
	switch(pc) {
		case PLANARCONFIG_CONTIG:
		case PLANARCONFIG_SEPARATE:
			break;
		default:
			gapr::report("Unsupported planar config");
	}
	cube_type type;
	if(!TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bps))
		gapr::report("Failed to read BITSPERSAMPLE.");
	switch(bps) {
		case 1:
		case 2:
		case 4:
		case 8:
			type=cube_type::u8;
			break;
		case 16:
			type=cube_type::u16;
			break;
		default:
			gapr::report("Unsupported bits per sample");
	}
	if(pm==PHOTOMETRIC_PALETTE)
		type=cube_type::u16;

	uint32 depth=0;
	do
		depth++;
	while(TIFFReadDirectory(tif));
	_tif=tiff_.release();
	set_info(type, {static_cast<int>(width), static_cast<int>(height), static_cast<int>(depth)});
}

void gapr::TiffLoader::do_load(char* ptr, int64_t ystride, int64_t zstride) {
	assert(_tif);
	switch(type()) {
		case cube_type::u8:
			return readTiffData<uint8_t>(_tif, sizes(), ptr, ystride, zstride);
		case cube_type::u16:
			return readTiffData<uint16_t>(_tif, sizes(), ptr, ystride, zstride);
		default:
			throw std::system_error{std::make_error_code(std::errc::invalid_argument), "unsupported type"};
	}
}

namespace gapr {
	std::unique_ptr<cube_loader> make_cube_loader_tiff(Streambuf& file) {
		return std::make_unique<TiffLoader>(file);
	}
}

