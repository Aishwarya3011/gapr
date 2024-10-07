#include "loadtiff.hh"

#include "gapr/str-glue.hh"
#include <stdexcept>
#include <tiffio.h>
#include <cstring>
#include <vector>

#include "gapr/utility.hh"


template<typename... Args>
static void throwError(Args&&... args) {
	gapr::str_glue str{std::forward<Args>(args)...};
	throw std::runtime_error{str.str()};
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

template<bool TILED>
inline std::pair<uint32_t, uint32_t> getTileDim(TIFF* tif, uint32_t);
template<>
inline std::pair<uint32_t, uint32_t> getTileDim<true>(TIFF* tif, uint32_t) {
	std::pair<uint32_t, uint32_t> res;
	if(!TIFFGetField(tif, TIFFTAG_TILEWIDTH, &res.first)
			|| !TIFFGetField(tif, TIFFTAG_TILELENGTH, &res.second))
		throwError("Failed to get tile width/length information.\n");
	return res;
}
template<>
inline std::pair<uint32_t, uint32_t> getTileDim<false>(TIFF* tif, uint32_t width) {
	std::pair<uint32_t, uint32_t> res;
	res.first=width;
	if(!TIFFGetField(tif, TIFFTAG_ROWSPERSTRIP, &res.second))
		throwError("Failed to get rows/strip information.");
	return res;
}

template<uint16_t PC>
inline uint64_t getTileBufSize(uint64_t tileSize, uint16_t spp);
template<>
inline uint64_t getTileBufSize<PLANARCONFIG_CONTIG>(uint64_t tileSize, uint16_t spp) {
	return tileSize;
}
template<>
inline uint64_t getTileBufSize<PLANARCONFIG_SEPARATE>(uint64_t tileSize, uint16_t spp) {
	return tileSize*spp;
}

template<bool TILED>
inline uint32_t getYCoord(uint32_t j, uint32_t h);
template<>
inline uint32_t getYCoord<true>(uint32_t j, uint32_t h) {
	return j; //h-1-j;
}
template<>
inline uint32_t getYCoord<false>(uint32_t j, uint32_t h) {
	return j;
}

template<bool TILED, uint16_t PC>
inline void readTile(TIFF* tif, uint8_t* bufTile, int64_t tileSize, uint16_t spp, uint32_t x, uint32_t y);
template<>
inline void readTile<true, PLANARCONFIG_CONTIG>(TIFF* tif, uint8_t* bufTile, int64_t tileSize, uint16_t spp, uint32_t x, uint32_t y) {
	auto tile=TIFFComputeTile(tif, x, y, 0, 0);
	if(-1==TIFFReadEncodedTile(tif, tile, bufTile, tileSize))
		throwError("Failed to read tile.\n");
}
template<>
inline void readTile<false, PLANARCONFIG_CONTIG>(TIFF* tif, uint8_t* bufTile, int64_t tileSize, uint16_t spp, uint32_t x, uint32_t y) {
	auto strip=TIFFComputeStrip(tif, y, 0);
	if(-1==TIFFReadEncodedStrip(tif, strip, bufTile, tileSize)) {
		std::memset(bufTile, 0, tileSize);
		gapr::print("Failed to read strip.\n");
	}
}
template<>
inline void readTile<true, PLANARCONFIG_SEPARATE>(TIFF* tif, uint8_t* bufTile, int64_t tileSize, uint16_t spp, uint32_t x, uint32_t y) {
	for(tsample_t s=0; s<spp; s++) {
		auto tile=TIFFComputeTile(tif, x, y, 0, s);
		if(-1==TIFFReadEncodedTile(tif, tile, bufTile+s*tileSize, tileSize))
			throwError("Failed to read tile.\n");
	}
}
template<>
inline void readTile<false, PLANARCONFIG_SEPARATE>(TIFF* tif, uint8_t* bufTile, int64_t tileSize, uint16_t spp, uint32_t x, uint32_t y) {
	for(tsample_t s=0; s<spp; s++) {
		auto strip=TIFFComputeStrip(tif, y, s);
		if(-1==TIFFReadEncodedStrip(tif, strip, bufTile+s*tileSize, tileSize)) {
			std::memset(bufTile+s*tileSize, 0, tileSize);
			gapr::print("Failed to read strip.\n");
		}
	}
}

template<uint16_t PC>
inline uint8_t* getBufStart(uint8_t* bufTile, uint64_t tileSize, tsample_t s);
template<>
inline uint8_t* getBufStart<PLANARCONFIG_CONTIG>(uint8_t* bufTile, uint64_t tileSize, tsample_t s) {
	return bufTile;
}
template<>
inline uint8_t* getBufStart<PLANARCONFIG_SEPARATE>(uint8_t* bufTile, uint64_t tileSize, tsample_t s) {
	return bufTile+s*tileSize;
}

template<uint16_t PC>
inline uint32_t getBufIndex(uint32_t idx, uint16_t spp, tsample_t s);
template<>
inline uint32_t getBufIndex<PLANARCONFIG_CONTIG>(uint32_t idx, uint16_t spp, tsample_t s) {
	return idx*spp+s;
}
template<>
inline uint32_t getBufIndex<PLANARCONFIG_SEPARATE>(uint32_t idx, uint16_t spp, tsample_t s) {
	return idx;
}

const int conv1bit[]={0, 255};
const int conv2bit[]={0, 85, 170, 255};
const int conv4bit[]={0, 17, 34, 51, 68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255};

template<uint16_t BPS, typename T>
inline T getValuePal(uint8_t* bufStart, uint32_t bufIdx);
template<>
inline uint8_t getValuePal<1, uint8_t>(uint8_t* bufStart, uint32_t bufIdx) {
	return (bufStart[bufIdx/8]>>(7-bufIdx%8))&0x01;
}
template<>
inline uint8_t getValuePal<2, uint8_t>(uint8_t* bufStart, uint32_t bufIdx) {
	return (bufStart[bufIdx/4]>>(3-bufIdx%4)*2)&0x03;
}
template<>
inline uint8_t getValuePal<4, uint8_t>(uint8_t* bufStart, uint32_t bufIdx) {
	return (bufStart[bufIdx/2]>>(1-bufIdx%2)*4)&0x0F;
}
template<>
inline uint8_t getValuePal<8, uint8_t>(uint8_t* bufStart, uint32_t bufIdx) {
	return bufStart[bufIdx];
}
template<>
inline uint16_t getValuePal<16, uint16_t>(uint8_t* bufStart, uint32_t bufIdx) {
	uint16_t* p=reinterpret_cast<uint16_t*>(bufStart+bufIdx*2);
	return *p;
}

template<uint16_t BPS, typename T>
inline T getValue(uint8_t* bufStart, uint32_t bufIdx);
template<>
inline uint8_t getValue<1, uint8_t>(uint8_t* bufStart, uint32_t bufIdx) {
	return conv1bit[(bufStart[bufIdx/8]>>(7-bufIdx%8))&0x01];
}
template<>
inline uint8_t getValue<2, uint8_t>(uint8_t* bufStart, uint32_t bufIdx) {
	return conv2bit[(bufStart[bufIdx/4]>>(3-bufIdx%4)*2)&0x03];
}
template<>
inline uint8_t getValue<4, uint8_t>(uint8_t* bufStart, uint32_t bufIdx) {
	return conv4bit[(bufStart[bufIdx/2]>>(1-bufIdx%2)*4)&0x0F];
}
template<>
inline uint8_t getValue<8, uint8_t>(uint8_t* bufStart, uint32_t bufIdx) {
	return bufStart[bufIdx];
}
template<>
inline uint16_t getValue<16, uint16_t>(uint8_t* bufStart, uint32_t bufIdx) {
	uint16_t* p=reinterpret_cast<uint16_t*>(bufStart+bufIdx*2);
	return *p;
}

template<typename T, typename T0, uint16_t BPS, uint16_t PC, bool TILED>
void tile_reader_pal(const TileReader& aux, TIFF* tif, std::vector<uint8_t>& bufTile, unsigned int x, unsigned int y, void* out_) {
#if 0
	readTile<TILED, PC>(tif, bufTile.data(), tileSize, spp, x, y);
	uint32_t w=tileW;
	if(w+x>width) {
		w=width-x;
	}
	uint32_t h=tileH;
	if(h+y>height) {
		h=height-y;
	}
	for(tsample_t s=0; s<spp; s++) {
		uint8_t *bufStart=getBufStart<PC>(bufTile.data(), tileSize, s);
		auto bufOut=bufSlice+x+y*width+s*3*width*height;
		for(uint32_t j=0; j<h; j++) {
			for(uint32_t i=0; i<w; i++) {
				auto bufIdx=getBufIndex<PC>(j*tileW+i, spp, s);
				auto vp=getValuePal<BPS, T0>(bufStart, bufIdx);
				for(tsample_t c=0; c<3; c++) {
					auto v=colors[c][vp];
					T ov;
					if(sizeof(ov)==sizeof(v)) {
						ov=v;
					} else {
						ov=xfunc[v];
					}
					bufOut[i+getYCoord<TILED>(j, h)*width+c*width*height]=ov;
				}
			}
		}
	}
#endif
}
template<typename T, typename T0, uint16_t BPS, uint16_t PC, bool TILED>
inline void convertTilePal(TileReader& aux, TIFF* tif) {
#if 0
	uint16_t* colors[3];
	if(!TIFFGetField(tif, TIFFTAG_COLORMAP, &colors[0], &colors[1], &colors[2]))
		throwError("Failed to read colormap.\n");
	auto tileSize=getTileSize<TILED>(tif);
	auto tileBufSize=getTileBufSize<PC>(tileSize, spp);
	if(bufTile.size()<tileBufSize)
		bufTile.resize(tileBufSize);
	uint64_t tilenx=((width+tileW-1)/tileW);
	uint64_t tilen=((height+tileH-1)/tileH)*tilenx;
	uint64_t tile_start=jobidx*tilen/jobn;
	uint64_t tile_end=(jobidx+1)*tilen/jobn;
	for(uint64_t tilei=tile_start; tilei<tile_end; tilei++) {
		auto y=tilei/tilenx*tileH;
		auto x=tilei%tilenx*tileW;
	}
#endif
}

	///////////
///////////////////
///////////////////
///////////////////
///////////////////
///////////////////////////////////////////////////////

template<typename T, typename T0, uint16_t BPS, uint16_t PC, bool TILED>
static void tile_reader(const TileReader& aux, TIFF* tif, std::vector<uint8_t>& bufTile, unsigned int x, unsigned int y, void* out_) {
	if(bufTile.size()<aux.tileBufSize)
		bufTile.resize(aux.tileBufSize);
	auto out=static_cast<T*>(out_);
	readTile<TILED, PC>(tif, bufTile.data(), aux.tileSize, aux.spp, x, y);
	uint32_t w=aux.tileW;
	if(w+x>aux.width) {
		w=aux.width-x;
	}
	uint32_t h=aux.tileH;
	if(h+y>aux.height) {
		h=aux.height-y;
	}
	for(tsample_t s=0; s<aux.spp; s++) {
		uint8_t *bufStart=getBufStart<PC>(bufTile.data(), aux.tileSize, s);
		auto bufOut=out+s*aux.tileW*aux.tileH;
		for(uint32_t j=0; j<h; j++) {
			if constexpr(sizeof(T0)!=sizeof(T)) {
				for(uint32_t i=0; i<w; i++) {
					auto bufIdx=getBufIndex<PC>(j*aux.tileW+i, aux.spp, s);
					auto v=getValue<BPS, T0>(bufStart, bufIdx);
					T ov;
					ov=0;
					//XXX
					//ov=xfunc[v];
					bufOut[i+getYCoord<TILED>(j, h)*aux.tileW]=ov;
				}
			} else if constexpr(BPS==8 && PC==PLANARCONFIG_SEPARATE) {
				static_assert(BPS==8);
				static_assert(std::is_same_v<T0, uint8_t>);
				std::memcpy(&bufOut[getYCoord<TILED>(j, h)*aux.tileW], &bufStart[j*aux.tileW], w*sizeof(T));
			} else if constexpr(BPS==16 && PC==PLANARCONFIG_SEPARATE) {
				static_assert(BPS==16);
				static_assert(std::is_same_v<T0, uint16_t>);
				std::memcpy(&bufOut[getYCoord<TILED>(j, h)*aux.tileW], &reinterpret_cast<T0*>(bufStart)[j*aux.tileW], sizeof(T)*w);
			} else {
				for(uint32_t i=0; i<w; i++) {
					auto bufIdx=getBufIndex<PC>(j*aux.tileW+i, aux.spp, s);
					auto v=getValue<BPS, T0>(bufStart, bufIdx);
					T ov;
					ov=v;
					// XXX
					bufOut[i+getYCoord<TILED>(j, h)*aux.tileW]=ov;
				}
			}
		}
	}
}

std::array<uint32_t, 2> get_tilesize(TIFF* tif) {
	auto [w,h]=TIFFIsTiled(tif)?getTileDim<true>(tif, 0):getTileDim<false>(tif, 0);
	return {w,h};
}

template<typename T, typename T0, uint16_t BPS, uint16_t PC, bool TILED>
inline void convertTile(TileReader& aux, TIFF* tif) {
	aux.tileSize=getTileSize<TILED>(tif);
	aux.tileBufSize=getTileBufSize<PC>(aux.tileSize, aux.spp);
	std::tie(aux.tileW, aux.tileH)=getTileDim<TILED>(tif, aux.width);
	aux.ptr=&tile_reader<T, T0, BPS, PC, TILED>;
	//////////////////////
	//////////////////////

	//uint64_t tilenx=((width+tileW-1)/tileW);
	//uint64_t tilen=((height+tileH-1)/tileH)*tilenx;
	//uint64_t tile_start=jobidx*tilen/jobn;
	//uint64_t tile_end=(jobidx+1)*tilen/jobn;
	//for(uint64_t tilei=tile_start; tilei<tile_end; tilei++) {
		//auto y=tilei/tilenx*tileH;
		//auto x=tilei%tilenx*tileW;
	//}
}
template<typename T, typename T0, uint16_t BPS, uint16_t PC>
void convertTile(TileReader& aux, TIFF* tif, bool tiled) {
	if(tiled) {
		return convertTile<T, T0, BPS, PC, true>(aux, tif);
	} else {
		return convertTile<T, T0, BPS, PC, false>(aux, tif);
	}
}
template<typename T, typename T0, uint16_t BPS, uint16_t PC>
void convertTilePal(TileReader& aux, TIFF* tif, bool tiled) {
	if(tiled) {
		return convertTilePal<T, T0, BPS, PC, true>(aux, tif);
	} else {
		return convertTilePal<T, T0, BPS, PC, false>(aux, tif);
	}
}
template<typename T, typename T0, uint16_t BPS>
void convertTile(TileReader& aux, TIFF* tif, uint16_t pc, bool tiled) {
	switch(pc) {
		case PLANARCONFIG_CONTIG:
			if(aux.spp!=1)
			return convertTile<T, T0, BPS, PLANARCONFIG_CONTIG>(aux, tif, tiled);
		case PLANARCONFIG_SEPARATE:
			return convertTile<T, T0, BPS, PLANARCONFIG_SEPARATE>(aux, tif, tiled);
		default:
			throwError("Planar configuration ", pc, " not supported.\n");
	}
}
template<typename T, typename T0, uint16_t BPS>
void convertTilePal(TileReader& aux, TIFF* tif, uint16_t pc, bool tiled) {
	switch(pc) {
		case PLANARCONFIG_CONTIG:
			return convertTilePal<T, T0, BPS, PLANARCONFIG_CONTIG>(aux, tif, tiled);
		case PLANARCONFIG_SEPARATE:
			return convertTilePal<T, T0, BPS, PLANARCONFIG_SEPARATE>(aux, tif, tiled);
		default:
			throwError("Planar configuration ", pc, " not supported.\n");
	}
}
template<typename T>
void convertTile(TileReader& aux, TIFF* tif, uint16_t bps, uint16_t pc, bool tiled);
template<>
void convertTile<uint8_t>(TileReader& aux, TIFF* tif, uint16_t bps, uint16_t pc, bool tiled) {
	switch(bps) {
		case 1:
			return convertTile<uint8_t, uint8_t, 1>(aux, tif, pc, tiled);
		case 2:
			return convertTile<uint8_t, uint8_t, 2>(aux, tif, pc, tiled);
		case 4:
			return convertTile<uint8_t, uint8_t, 4>(aux, tif, pc, tiled);
		case 8:
			return convertTile<uint8_t, uint8_t, 8>(aux, tif, pc, tiled);
		case 16:
			return convertTile<uint8_t, uint16_t, 16>(aux, tif, pc, tiled);
		default:
			throwError("Bits per sample ", bps, " not supported.\n");
	}
}
template<>
void convertTile<uint16_t>(TileReader& aux, TIFF* tif, uint16_t bps, uint16_t pc, bool tiled) {
	switch(bps) {
		case 16:
			return convertTile<uint16_t, uint16_t, 16>(aux, tif, pc, tiled);
		default:
			throwError("Bits per sample ", bps, " not supported.\n");
	}
}
template<typename T>
void convertTilePal(TileReader& aux, TIFF* tif, uint16_t bps, uint16_t pc, bool tiled) {
	switch(bps) {
		case 1:
			return convertTilePal<T, uint8_t, 1>(aux, tif, pc, tiled);
		case 2:
			return convertTilePal<T, uint8_t, 2>(aux, tif, pc, tiled);
		case 4:
			return convertTilePal<T, uint8_t, 4>(aux, tif, pc, tiled);
		case 8:
			return convertTilePal<T, uint8_t, 8>(aux, tif, pc, tiled);
		case 16:
			return convertTilePal<T, uint16_t, 16>(aux, tif, pc, tiled);
		default:
			throwError("Bits per sample ", bps, " not supported.\n");
	}
}
template<typename T>
void convertTile(TileReader& aux, TIFF* tif) {
	uint16_t pm;
	if(!TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &pm))
		throwError("Failed to read PHOTOMETRIC.\n");
	uint16_t pc;
	if(!TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &pc))
		throwError("Failed to read PLANARCONFIG.\n");
	bool tiled=TIFFIsTiled(tif);
	switch(pm) {
		case PHOTOMETRIC_MINISWHITE:
		case PHOTOMETRIC_MINISBLACK:
		case PHOTOMETRIC_SEPARATED:
		case PHOTOMETRIC_RGB:
			return convertTile<T>(aux, tif, aux.bps, pc, tiled);
		case PHOTOMETRIC_PALETTE:
			return convertTilePal<T>(aux, tif, aux.bps, pc, tiled);
		default:
			// Not supporting colormap
			throwError("Photometric type ", pm, " not supported.\n");
	}
}
template<typename T>
void TileReader::init(TIFF* tif) {
	convertTile<T>(*this, tif);
}
template void TileReader::init<uint8_t>(TIFF* tif);
template void TileReader::init<uint16_t>(TIFF* tif);

#if 0
template <typename T>
void Slice2Cube<T>::readInputFiles(Slices* slices, int jobi) {
	for(size_t si=0; si<slices->bufs.size(); si++) {
		auto zidx=slices->zidx+si;
		if(zidx>=inputpairs.size())
			throwError("Internal error. No file names.");
		auto& fn=*inputpairs[zidx].first;
		if(fn=="BLACK") {
			if(jobi==0) {
				memset(slices->bufs[si], 0, width*height*channels*sizeof(T));
			}
			continue;
		}
		if(fn=="WHITE") {
			if(jobi==0) {
				memset(slices->bufs[si], 0xFF, width*height*channels*sizeof(T));
			}
			continue;
		}
		std::vector<uint8_t> bufTile;
		Tiff tif(fn.c_str(), "r");
		if(!tif)
			throwError("Error opening TIFF file: '", fn, "'. Abort.\n");
		if(!TIFFSetDirectory(tif, inputpairs[zidx].second))
			throwError("Cannot set directory");
		convertTile<T>(tif, width, height, bufTile, slices->bufs[si], xfunc, jobi, slices->total);
	}
}
template void Slice2Cube<uint8_t>::readInputFiles(Slices* slices, int jobi);
template void Slice2Cube<uint16_t>::readInputFiles(Slices* slices, int jobi);
#endif

void checkSlice(TIFF* tif, unsigned int* pwidth, unsigned int* pheight, unsigned short* pspp, unsigned short* pbps) {
	uint16_t spp, pm, pc, bps;
	if(!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, pwidth)
			|| !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, pheight))
		throwError("No width/length information in file: ");
	if(*pwidth==0 || *pheight==0)
		throwError("Zero width/length: ");
	if(!TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &spp))
		throwError("Failed to read SAMPLESPERPIXEL.");
	if(spp==0)
		throwError("Zero sample per pixel");
	if(!TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &pm))
		throwError("Failed to read PHOTOMETRIC.");
	switch(pm) {
		case PHOTOMETRIC_MINISWHITE:
		case PHOTOMETRIC_MINISBLACK:
		case PHOTOMETRIC_SEPARATED:
			break;
		case PHOTOMETRIC_RGB:
			if(spp!=3)
				throwError("Wrong spp for RGB image");
			break;
		case PHOTOMETRIC_PALETTE:
			if(spp!=1)
				throwError("Wrong spp for PALETTE image");
			break;
		default:
			throwError("Unsupported photometric");
	}
	if(!TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &pc))
		throwError("Failed to read PLANARCONFIG.");
	switch(pc) {
		case PLANARCONFIG_CONTIG:
		case PLANARCONFIG_SEPARATE:
			break;
		default:
			throwError("Unsupported planar config");
	}
	if(!TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bps))
		throwError("Failed to read BITSPERSAMPLE.");
	switch(bps) {
		case 1:
		case 2:
		case 4:
		case 8:
		case 16:
			break;
		default:
			throwError("Unsupported bits per sample");
	}
	*pspp=(pm==PHOTOMETRIC_PALETTE)?3*spp:spp;
	*pbps=(pm==PHOTOMETRIC_PALETTE)?16:bps;
}

#if 0
template <typename T>
void Slice2Cube<T>::closeTiffFiles() {
	if(tiffd) {
		TIFFClose(tiffd);
		tiffd=nullptr;
	}
}
template void Slice2Cube<uint8_t>::closeTiffFiles();
template void Slice2Cube<uint16_t>::closeTiffFiles();

template <typename T>
void Slice2Cube<T>::getNextImage() {
	if(!tiffd) {
		if(tiffidx>=inputfiles.size())
			return;
		if(inputfiles[tiffidx]=="BLACK" || inputfiles[tiffidx]=="WHITE") {
			inputpairs.push_back({&inputfiles[tiffidx++], 0});
			return;
		}
		tiffd=TIFFOpen(inputfiles[tiffidx].c_str(), "r");
		if(!tiffd)
			throwError("Failed to open.");
		inputpairs.push_back({&inputfiles[tiffidx], TIFFCurrentDirectory(tiffd)});
		return;
	} else {
		if(TIFFReadDirectory(tiffd)) {
			inputpairs.push_back({&inputfiles[tiffidx], TIFFCurrentDirectory(tiffd)});
			return;
		}
		TIFFClose(tiffd);
		tiffd=nullptr;
		tiffidx++;
		return getNextImage();
	}
}
template void Slice2Cube<uint8_t>::getNextImage();
template void Slice2Cube<uint16_t>::getNextImage();
#endif

