/* cube-loader-nrrd.cc
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

#include "gapr/streambuf.hh"
#include "gapr/cube.hh"
#include "gapr/utility.hh"

#include <charconv>


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

static std::string& normalize(size_t start, std::string& str) {
	size_t i=0;
	bool nospace=true;
	for(size_t j=start; j<str.size(); j++) {
		if(isspace(str[j])) {
			if(!nospace)
				str[i++]=' ';
			nospace=true;
		} else {
			str[i++]=str[j];
			nospace=false;
		}
	}
	if(i>0 && str[i-1]==' ')
		i--;
	str.resize(i);
	return str;
}

static bool checkType(const std::string& str, gapr::cube_type& imgType) {
	if(str=="signed char" || str=="int8" || str=="int8_t") {
		imgType=gapr::cube_type::i8;
	} else if(str=="uchar" || str=="unsigned char" || str=="uint8" || str=="uint8_t") {
		imgType=gapr::cube_type::u8;
	} else if(str=="short" || str=="short int" || str=="signed short" || str=="signed short int" || str=="int16" || str=="int16_t") {
		imgType=gapr::cube_type::i16;
	} else if(str=="ushort" || str=="unsigned short" || str=="unsigned short int" || str=="uint16" || str=="uint16_t") {
		imgType=gapr::cube_type::u16;
	} else if(str=="int" || str=="signed int" || str=="int32" || str=="int32_t") {
		imgType=gapr::cube_type::i32;
	} else if(str=="uint" || str=="unsigned int" || str=="uint32" || str=="uint32_t") {
		imgType=gapr::cube_type::u32;
	} else if(str=="longlong" || str=="long long" || str=="long long int" || str=="signed long long" || str=="signed long long int" || str=="int64" || str=="int64_t") {
		imgType=gapr::cube_type::i64;
	} else if(str=="ulonglong" || str=="unsigned long long" || str=="unsigned long long int" || str=="uint64" || str=="uint64_t") {
		imgType=gapr::cube_type::u64;
	} else if(str=="float") {
		imgType=gapr::cube_type::f32;
	} else if(str=="double") {
		imgType=gapr::cube_type::f64;
	} else if(str=="block") {
		imgType=gapr::cube_type::unknown;
		return false;
	} else {
		imgType=gapr::cube_type::unknown;
		return false;
	}
	return true;
}

static bool checkEncoding(const std::string& str, bool& isGzip) {
	if(str=="gzip") {
		isGzip=true;
	} else if(str=="raw") {
		isGzip=false;
	} else {
		return false;
	}
	return true;
}

static bool checkDim(const std::string& str, std::size_t& dim) {
	if(str.empty())
		return false;
	auto [eptr, ec]=std::from_chars(&str[0], &str[0]+str.size(), dim, 10);
	if(ec!=std::errc{} || eptr!=&str[0]+str.size())
		return false;
	if(dim>0 && dim<=3) {
		return true;
	}
	return false;
}

static bool checkSizes(const std::string& str, std::size_t dim, std::array<int32_t, 3>& sizes) {
	const char* ptr=&str[0];
	const char* end=&str[str.size()];
	size_t i;
	for(i=0; i<dim && i<3 && ptr<end; i++) {
		auto [eptr, ec]=std::from_chars(ptr, end, sizes[i], 10);
		if(ec!=std::errc{} || eptr==ptr)
			return false;
		if(eptr<end) {
			if(*eptr!=' ')
				return false;
			ptr=eptr+1;
		} else {
			ptr=eptr;
		}
	}
	if(i==dim && ptr==end) {
		for(size_t d=dim; d<3; d++)
			sizes[d]=1;
		return true;
	}
	return false;
}

static bool checkEndian(const std::string& str, bool& isLE) {
	if(str=="big") {
		isLE=false;
	} else if(str=="little") {
		isLE=true;
	} else {
		return false;
	}
	return true;
}



namespace gapr {

	class NrrdLoader: public cube_loader {
		public:
			explicit NrrdLoader(gapr::Streambuf& file);
			~NrrdLoader() override { }
		private:
			void do_load(char* buf, int64_t ystride, int64_t zstride) override;

			std::size_t _offset;
			std::size_t _dim;
			bool _isGzip{false};
			bool _isLE{true};
	};

}

gapr::NrrdLoader::NrrdLoader(Streambuf& file): cube_loader{file} {
	std::istream fs{&file};
	std::string line;
	if(!std::getline(fs, line)) {
		if(!fs.eof())
			throw std::system_error{std::make_error_code(std::io_errc::stream)};
		throw std::system_error{std::make_error_code(std::errc::invalid_argument), "no header"};
	}
	if(line.compare(0, 4, "NRRD")!=0)
		throw std::system_error{std::make_error_code(std::errc::invalid_argument), "wrong header"};

	bool got_dim{false};
	bool got_sizes{false};
	bool got_type{false};
	cube_type type{};
	std::array<int32_t, 3> sizes;
	while(std::getline(fs, line)) {
		if(line.empty()) {
			if(!got_dim)
				throw std::system_error{std::make_error_code(std::errc::invalid_argument), "no dimension"};
			if(!got_sizes)
				throw std::system_error{std::make_error_code(std::errc::invalid_argument), "no sizes"};
			if(!got_type)
				throw std::system_error{std::make_error_code(std::errc::invalid_argument), "no type"};
			set_info(type, sizes);
			_offset=fs.tellg();
			return;
		}
		if(line[0]=='#')
			continue;

		if(line.compare(0, 5, "type:")==0) {
			if(!checkType(normalize(5, line), type))
				throw std::system_error{std::make_error_code(std::errc::invalid_argument), "wrong type"};
			got_type=true;
		} else if(line.compare(0, 9, "encoding:")==0) {
			if(!checkEncoding(normalize(9, line), _isGzip))
				throw std::system_error{std::make_error_code(std::errc::invalid_argument), "wrong encoding"};
		} else if(line.compare(0, 10, "dimension:")==0) {
			if(!checkDim(normalize(10, line), _dim))
				throw std::system_error{std::make_error_code(std::errc::invalid_argument), "wrong dimension"};
			got_dim=true;
		} else if(line.compare(0, 6, "sizes:")==0) {
			if(!checkSizes(normalize(6, line), _dim, sizes))
				throw std::system_error{std::make_error_code(std::errc::invalid_argument), "wrong sizes"};
			got_sizes=true;
		} else if(line.compare(0, 7, "endian:")==0) {
			if(!checkEndian(normalize(7, line), _isLE))
				throw std::system_error{std::make_error_code(std::errc::invalid_argument), "wrong endian"};
		}
	}
	if(!fs.eof())
		throw std::system_error{std::make_error_code(std::io_errc::stream)};
	throw std::system_error{std::make_error_code(std::errc::invalid_argument), "no data"};
}

void gapr::NrrdLoader::do_load(char* ptr, int64_t ystride, int64_t zstride) {
	if(file().pubseekpos(_offset, std::ios_base::in)==-1)
		throw std::system_error{std::make_error_code(std::io_errc::stream)};
	std::unique_ptr<Streambuf> filter_{};
	auto filter=&file();
	if(_isGzip) {
		filter_.reset(filter=Streambuf::inputFilter(file(), "gzip"));
		if(!filter)
			throw std::system_error{std::make_error_code(std::io_errc::stream)};
	}
	auto chunksize=sizes()[0]*voxel_size(type());
	for(int z=0; z<sizes()[2]; z++) {
		for(int y=0; y<sizes()[1]; y++) {
			if(filter->sgetn(ptr+y*ystride+z*zstride, chunksize)!=chunksize)
				throw std::system_error{std::make_error_code(std::io_errc::stream)};
		}
	}

	if(_isLE!=gapr::little_endian()) {
		switch(voxel_size(type())) {
			case 1:
				break;
			case 2:
				swapBytes<uint16_t>(ptr, ystride, zstride, sizes());
				break;
			case 4:
				swapBytes<uint32_t>(ptr, ystride, zstride, sizes());
				break;
			case 8:
				swapBytes<uint64_t>(ptr, ystride, zstride, sizes());
				break;
			default:
				gapr::report("Unknown size");
		}
	}

	if(_isGzip)
		filter->close();
}

namespace gapr {
	std::unique_ptr<cube_loader> make_cube_loader_nrrd(Streambuf& file) {
		return std::make_unique<NrrdLoader>(file);
	}
}

