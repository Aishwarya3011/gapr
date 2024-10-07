/* cube-loader-v3d.cc
 *
 * Copyright (C) 2021 GOU Lingfeng
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

#include "gapr/streambuf.hh"
#include "gapr/cube.hh"
#include "gapr/utility.hh"


// dup
template<typename T> inline void swapBytes(char* pptr, int64_t ystride, int64_t zstride, const std::array<int32_t, 3>& sizes) {
	for(int z=0; z<sizes[2]; z++) {
		for(int y=0; y<sizes[1]; y++) {
			T* ptr=reinterpret_cast<T*>(pptr+y*ystride+z*zstride);
			for(int x=0; x<sizes[0]; x++) {
				ptr[x]=gapr::swap_bytes<T>(ptr[x]);
			}
		}
	}
}

namespace gapr {

	class V3dLoader: public cube_loader {
		public:
			explicit V3dLoader(gapr::Streambuf& file, bool compressed);
			~V3dLoader() override { }
		private:
			void do_load(char* buf, int64_t ystride, int64_t zstride) override;
			void do_load_comp(char* buf, int64_t ystride, int64_t zstride);
			void do_load_raw(char* buf, int64_t ystride, int64_t zstride);

			template<unsigned int bpp> unsigned int extract(int r);

			//std::size_t _offset;
			//std::size_t _dim;
			bool _iscomp;

			unsigned int _cur_v=std::numeric_limits<unsigned int>::max();
			std::string _outbuf{}, _inbuf{};
	};

}

gapr::V3dLoader::V3dLoader(Streambuf& file, bool compressed): cube_loader{file}, _iscomp{compressed} {
	std::array<char, 0x2b> hdr;
	if(file.sgetn(hdr.data(), hdr.size())!=hdr.size())
		throw std::runtime_error{"failed to read file header"};
	std::string_view tags[]={
		"raw_image_stack_by_hpengL",
		"v3d_volume_pkbitdf_encodL",
	};
	if(tags[compressed?1:0]!=std::string_view{hdr.data(), 0x19})
		throw std::runtime_error{"wrong file header"};
	auto get_v=[p=&hdr[0]](unsigned int i, unsigned int n) ->unsigned int {
		unsigned int r=0;
		auto pp=reinterpret_cast<const uint8_t*>(p);
		bool le=gapr::little_endian();
		for(unsigned int k=0; k<n; ++k) {
			unsigned int v=pp[i+k];
			r|=(v<<(le?k*8:(n-1-k)*8));
		}
		return r;
	};
	auto bytes_pp=get_v(0x19, 2);
	int w=get_v(0x1b, 4);
	int h=get_v(0x1f, 4);
	int d=get_v(0x23, 4);
	cube_type type{};
	switch(bytes_pp) {
		case 1:
			type=gapr::cube_type::u8;
			break;
		case 2:
			type=gapr::cube_type::u16;
			break;
		default:
			throw std::runtime_error{"wrong type"};
	}
	set_info(type, {w, h, d});
}

void gapr::V3dLoader::do_load_raw(char* ptr, int64_t ystride, int64_t zstride) {
	auto chunksize=sizes()[0]*voxel_size(type());
	for(int z=0; z<sizes()[2]; z++) {
		for(int y=0; y<sizes()[1]; y++) {
			if(file().sgetn(ptr+y*ystride+z*zstride, chunksize)!=chunksize)
				throw std::runtime_error{"failed to read stride"};
		}
	}
	if(auto r=file().sbumpc(); r!=std::streambuf::traits_type::eof())
		throw std::runtime_error{"more to read"};
}

template<unsigned int bpp> unsigned int gapr::V3dLoader::extract(int r) {
	unsigned int n=r;
	auto copy=[this](unsigned int nn) ->unsigned int {
		_outbuf.resize(nn*bpp);
		if(file().sgetn(&_outbuf[0], _outbuf.size())!=_outbuf.size())
			return 0;
		auto p=reinterpret_cast<uint8_t*>(&_outbuf[nn*bpp-bpp]);
		_cur_v=0;
		for(unsigned int k=0; k<bpp; ++k)
			_cur_v+=(static_cast<unsigned int>(p[k])<<(8*k));
		return nn*bpp;
	};
	auto dup=[this](std::size_t nn) ->unsigned int {
		_inbuf.resize(bpp);
		if(file().sgetn(&_inbuf[0], _inbuf.size())!=_inbuf.size())
			return 0;
		_outbuf.resize(bpp*nn);
		for(unsigned int i=0; i<nn; ++i) {
			for(unsigned int k=0; k<2; ++k)
				_outbuf[i*bpp+k]=_inbuf[k];
		}
		_cur_v=0;
		auto p=reinterpret_cast<uint8_t*>(&_inbuf[0]);
		for(unsigned int k=0; k<bpp; ++k)
			_cur_v+=(static_cast<unsigned char>(p[k])<<(8*k));
		return bpp*nn;
	};
	auto diff=[this](std::size_t nn) ->unsigned int {
		if(_cur_v==std::numeric_limits<unsigned int>::max())
			return 0;
		_inbuf.resize((nn*(bpp+1)+7)/8);
		if(file().sgetn(&_inbuf[0], _inbuf.size())!=_inbuf.size())
			return 0;
		_outbuf.resize(nn*bpp);

		auto p=reinterpret_cast<uint8_t*>(&_inbuf[0]);
		unsigned int bits=(p++)[0];
		unsigned int nbits=8;
		unsigned int v=_cur_v;
		unsigned int j=0;
		assert(j<nn);

		do {
			assert(nbits>=(bpp+1));
			unsigned int t;
			if(bpp==2) {
				t=(bits>>(nbits-(bpp+1)));
			} else {
				t=bits;
				bits=bits>>(bpp+1);
			}
			nbits-=(bpp+1);
			t&=((1<<(bpp+1))-1);
			assert(t<(1<<(bpp+1)));

			if(t>(1<<((bpp+1)-1))) {
				v=v-(t&((1<<bpp)-1));
			} else {
				v=v+t;
			}

			for(unsigned int k=0; k<bpp; ++k)
				_outbuf[j*bpp+k]=(v>>(k*8));
			if(++j>=nn)
				break;
			if(nbits<bpp+1) {
				if(bpp==2) {
					bits=(bits<<8)+(p++)[0];
				} else {
					bits+=(static_cast<unsigned int>((p++)[0])<<nbits);
				}
				nbits+=8;
			}
		} while(true);
		_cur_v=v;
		return nn*bpp;
	};
	if(bpp==2) {
		switch(n>>5) {
			case 0:
				return copy(n+1);
			case 1:
			case 2:
				return diff(n-((1<<5)+1-bpp));
			case 7:
				return dup((n&0x1f)+bpp);
			default:
				break;
		}
	} else {
		switch(n>>5) {
			case 0:
				return copy(n+1);
			case 1:
				if(n==0x20)
					return copy(n+1);
			case 2:
			case 3:
				return diff(n-((1<<5)+1-bpp));
			case 4:
			case 5:
			case 6:
			case 7:
				return dup((n&0x7f)+bpp);
			default:
				break;
		}
	}
	return 0;
}
void gapr::V3dLoader::do_load_comp(char* ptr, int64_t ystride, int64_t zstride) {
	auto chunksize=sizes()[0]*voxel_size(type());
	std::size_t out_s{0};
	std::size_t outbuf_i{0};
	for(int z=0; z<sizes()[2]; z++) {
		for(int y=0; y<sizes()[1]; y++) {
			auto dst=ptr+y*ystride+z*zstride;
			unsigned int nn=0;
			do {
				auto bufl=_outbuf.size()-outbuf_i;
				if(bufl<=0) {
					std::size_t pos=file().pubseekoff(0, std::ios::cur);
					auto r=file().sbumpc();
					if(r==std::streambuf::traits_type::eof()) {
						fprintf(stderr, "premature eof\n");
						return;
					}
					unsigned int ii{0};
					switch(voxel_size(type())) {
						case 1:
							ii=extract<1>(r);
							break;
						case 2:
							ii=extract<2>(r);
							break;
						default:
							assert(0);
					}
					if(ii==0) {
						fprintf(stderr, "failed: %06zx:%06zx %04x\n", pos, out_s+0x2b, _cur_v);
						throw std::logic_error{"failed to extract"};
					}
					assert(ii==_outbuf.size());
					out_s+=ii;
					outbuf_i=0;
					bufl=_outbuf.size();
				}
				if(bufl>chunksize-nn)
					bufl=chunksize-nn;
				std::copy(&_outbuf[outbuf_i], &_outbuf[outbuf_i+bufl], &dst[nn]);
				outbuf_i+=bufl;
				nn+=bufl;
			} while(nn<chunksize);
		}
	}
	if(auto r=file().sbumpc(); r!=std::streambuf::traits_type::eof())
		throw std::runtime_error{"more to read"};
	std::size_t raw_s=chunksize;
	raw_s*=sizes()[2];
	raw_s*=sizes()[1];
	if(out_s!=raw_s) {
		fprintf(stderr, "total: %zd!=%zd\n", out_s, raw_s);
		throw std::runtime_error{"raw size not correct"};
	}
}
void gapr::V3dLoader::do_load(char* ptr, int64_t ystride, int64_t zstride) {
	if(file().pubseekpos(43, std::ios_base::in)==-1)
		throw std::runtime_error{"failed to seek"};
	if(_iscomp)
		return do_load_comp(ptr, ystride, zstride);
	else
		return do_load_raw(ptr, ystride, zstride);

	if(!gapr::little_endian()) {
		switch(voxel_size(type())) {
			case 1:
				break;
			case 2:
				swapBytes<uint16_t>(ptr, ystride, zstride, sizes());
				break;
			default:
				gapr::report("Unknown size");
		}
	}
}

namespace gapr {
	std::unique_ptr<cube_loader> make_cube_loader_v3d(Streambuf& file, bool compressed) {
		return std::make_unique<V3dLoader>(file, compressed);
	}
}

