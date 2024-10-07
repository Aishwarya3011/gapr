#include "gapr/cube.hh"

#include "gapr/utility.hh"
#include "gapr/detail/finally.hh"

#include <cassert>
#include <algorithm>
#include <charconv>
#include <fstream>

#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filter/gzip.hpp>

#include "gapr/detail/nrrd-output.hh"

#include "config.hh"

//////////
using gapr::cube_PRIV;

cube_PRIV::Head* cube_PRIV::alloc(cube_type type, std::array<uint32_t, 3> sizes) {
	assert(type!=cube_type::unknown);

	std::size_t ystride=(voxel_size(type)*sizes[0]+7)/8*8;
	// XXX alignment???
	auto buf=new char[sizeof(cube_PRIV::Head)+ystride*sizes[1]*sizes[2]];
	return new(buf) cube_PRIV::Head{type, sizes, ystride};
}
void cube_PRIV::destroy(Head* p) noexcept {
	p->~Head();
	auto buf=reinterpret_cast<char*>(p);
	delete[] buf;
}

static void check_file_type_mesh(const std::string& path) noexcept {
	auto i=path.rfind('.');
	if(i!=path.npos) {
		auto p=&path[i];
		auto l=path.size()-i;
		auto pred=[](char a, char b) ->bool {
			if(a>='A' && a<='Z')
				a=a-'A'+'a';
			return a==b;
		};
		if(l==4 && std::equal(p, p+l, ".obj", pred))
			return;
	}
	gapr::report("Cannot handle file type: ", path);
}
static void check_file_type(const std::string& path) noexcept {
	auto i=path.rfind('.');
	if(i!=path.npos) {
		auto p=&path[i];
		auto l=path.size()-i;
		auto pred=[](char a, char b) ->bool {
			if(a>='A' && a<='Z')
				a=a-'A'+'a';
			return a==b;
		};
		if(l==5 && std::equal(p, p+l, ".nrrd", pred))
			return;
#ifdef WITH_VP9
		if(l==5 && std::equal(p, p+l, ".webm", pred))
			return;
#endif
#ifdef WITH_HEVC
		if(l==5 && std::equal(p, p+l, ".hevc", pred))
			return;
#endif
		if(l==5 && std::equal(p, p+l, ".tiff", pred))
			return;
		if(l==4 && std::equal(p, p+l, ".tif", pred))
			return;
		if(l==7 && std::equal(p, p+l, ".v3draw", pred))
			return;
		if(l==7 && std::equal(p, p+l, ".v3dpbd", pred))
			return;
	}
	gapr::report("Cannot handle file type: ", path);
}

template<typename T, size_t N> bool parseValues(const std::string& s, std::array<T, N>& v) {
	std::istringstream iss{s};
	for(size_t i=0; i<N; i++) {
		iss>>v[i];
		if(!iss)
			return false;
	}
	return true;
}

// XXX
template<typename Stream, typename String>
inline static Stream& fnt_getline(Stream& input, String& str) {
	auto& r=static_cast<Stream&>(std::getline(input, str));
	if(!str.empty() && str.back()=='\r')
		str.pop_back();
	return r;
}

struct LineInfo {
	int lineno;
	std::string key;
	std::string val;
	LineInfo(int lineno, std::string&& key, std::string&& val) noexcept:
		lineno{lineno}, key{std::move(key)}, val{std::move(val)} { }
};

static void parse(int lineno_name, std::vector<LineInfo>&& lines, gapr::cube_info& info) {
	bool size_set{false}, cubesize_set{false};
	while(!lines.empty()) {
		auto line=std::move(lines.back());
		lines.pop_back();
		if(line.key.compare("size")==0) {
			auto r=parseValues<uint32_t, 3>(line.val, info.sizes);
			if(!r)
				gapr::report("line ", line.lineno, ": failed to parse volume size");
			size_set=true;
		} else if(line.key.compare("cubesize")==0) {
			if(!info.is_pattern())
				gapr::report("line ", line.lineno, ": cubesize not applicable");
			auto r=parseValues<uint32_t, 3>(line.val, info.cube_sizes);
			if(!r)
				gapr::report("line ", line.lineno, ": failed to parse cube size");
			cubesize_set=true;
		} else if(line.key.compare("direction")==0) {
			auto r=parseValues<double, 9>(line.val, info.xform.direction);
			if(!r)
				gapr::report("line ", line.lineno, ": failed to parse direction");
		} else if(line.key.compare("origin")==0) {
			auto r=parseValues<double, 3>(line.val, info.xform.origin);
			if(!r)
				gapr::report("line ", line.lineno, ": failed to parse origin");
		} else if(line.key.compare("range")==0) {
			auto r=parseValues<double, 2>(line.val, info.range);
			if(!r)
				gapr::report("line ", line.lineno, ": failed to parse range");
		} else {
			gapr::report("line ", line.lineno, ": unknown key");
		}
	}
	if(!size_set)
		gapr::report("line: ", lineno_name, ": size not set");
	if(info.is_pattern() && !cubesize_set)
		gapr::report("line: ", lineno_name, ": cubesize not set");
}

static void parse(int lineno_name, std::vector<LineInfo>&& lines, gapr::mesh_info& info) {
	bool color_set{false};
	while(!lines.empty()) {
		auto lineno=lines.back().lineno;
		auto key=std::move(lines.back().key);
		auto val=std::move(lines.back().val);
		lines.pop_back();
		if(key.compare("color")==0) {
			std::array<double, 4> col;
			auto r=parseValues<double, 4>(val, col);
			if(!r)
				gapr::report("line ", lineno, ": failed to parse color");
			for(int i=0; i<4; i++) {
				if(col[i]<0 || col[i]>1)
					gapr::report("line ", lineno, ": value not in range [0, 1]");
				info.color[i]=std::lround(col[i]*255);
			}
			color_set=true;
		} else if(key.compare("direction")==0) {
			auto r=parseValues<double, 9>(val, info.xform.direction);
			if(!r)
				gapr::report("line ", lineno, ": failed to parse direction");
		} else if(key.compare("origin")==0) {
			auto r=parseValues<double, 3>(val, info.xform.origin);
			if(!r)
				gapr::report("line ", lineno, ": failed to parse origin");
		} else {
			gapr::report("line ", lineno, ": unknown key");
		}
	}
	if(!color_set)
		gapr::report("line: ", lineno_name, ": color not set");
}


#if 0
			auto annotTree=parseAnnot<T>(data, pos);

			Location* location{nullptr};
			if(isUrl(loc)) {
				location=loaderShared->getLocation(false, true, loc);
			} else {
				auto pos_new=compose<T>(pos, loc);
				location=loaderShared->getLocation(false, T, pos_new);
			}

			if(location)
				sources.push_back(Source{pfx, location, annotTree, {0, 0, 0}, {0, 0, 0}, xform});
#endif
#if 0
			auto annotTree=parseAnnot<T>(data, pos);

			Location* location{nullptr};
			if(isUrl(pat)) {
				location=loaderShared->getLocation(true, true, pat);
			} else {
				auto pos_new=compose<T>(pos, pat);
				location=loaderShared->getLocation(true, T, pos_new);
			}

			if(location)
				sources.push_back(Source{pfx, location, annotTree, s, cs, xform});
#endif

void gapr::parse_catalog(std::istream& ifs, std::vector<cube_info>& cube_infos, std::vector<mesh_info>& mesh_infos, std::string_view url) {

	{
		auto i=url.size();
		while(i>0 && url[i-1]!='/')
			--i;
		url={url.data(), i};
	}
	int lineno{0};
	std::string line;
	int lineno_name{-1};
	std::string name;
	std::vector<LineInfo> lines;
	enum {
		Unknown,
		Mesh,
		Cube
	} st=Unknown;
	auto ncube=cube_infos.size();
	auto nmesh=mesh_infos.size();
	auto cleaner=gapr::make_finally([&cube_infos,&mesh_infos,ncube,nmesh]() noexcept {
		while(cube_infos.size()>ncube)
			cube_infos.pop_back();
		while(mesh_infos.size()>nmesh)
			mesh_infos.pop_back();
	});
	while(fnt_getline(ifs, line)) {
		lineno++;
		if(line.empty())
			continue;
		if(line[0]=='#')
			continue;
		if(line[0]=='[') {
			auto i=line.find(']');
			if(i==line.npos)
				gapr::report("line ", lineno, ": no closing ']': ", line);
			if(i<=1)
				gapr::report("line ", lineno, ": empty name: ", line);
			if(lineno_name>=0) {
				switch(st) {
					case Mesh:
						parse(lineno_name, std::move(lines), mesh_infos.back());
						break;
					case Cube:
						parse(lineno_name, std::move(lines), cube_infos.back());
						break;
					default:
						gapr::report("line ", lineno_name, ": incomplete section");
				}
				lines.clear();
				st=Unknown;
			}
			lineno_name=lineno;
			name=std::string{&line[1], i-1};
		} else {
			if(lineno_name<0)
				gapr::report("line ", lineno, ": not in a section");

			auto i=line.find('=');
			if(i==line.npos)
				gapr::report("line ", lineno, ": missing '=': ", line);
			if(i<=0)
				gapr::report("line ", lineno, ": empty key: ", line);
			for(std::size_t j=0; j<i; j++) {
				auto c=line[j];
				if(c>='A' && c<='Z')
					line[j]=c-'A'+'a';
			}
			auto val=line.substr(i+1);
			auto fix_relative=[url](std::string&& path) ->std::string {
				if(path.find("://")!=path.npos)
					return std::move(path);
				std::string res;
				res.reserve(url.size()+path.size());
				res+=url;
				res+=path;
				return res;
			};
			if(line.compare(0, i, "pattern")==0) {
				if(st!=Unknown)
					gapr::report("line ", lineno, ": contradicting key");
				check_file_type(val);
				cube_infos.emplace_back(std::move(name), fix_relative(std::move(val)), true);
				st=Cube;
			} else if(line.compare(0, i, "location")==0) {
				if(st!=Unknown)
					gapr::report("line ", lineno, ": contradicting key");
				check_file_type(val);
				cube_infos.emplace_back(std::move(name), fix_relative(std::move(val)), false);
				st=Cube;
			} else if(line.compare(0, i, "mesh")==0) {
				if(st!=Unknown)
					gapr::report("line ", lineno, ": contradicting key");
				check_file_type_mesh(val);
				mesh_infos.emplace_back(std::move(name), fix_relative(std::move(val)));
				st=Mesh;
			//} else if(line.compare(0, i, "origin")==0) {
			//} else if(line.compare(0, i, "direction")==0) {
			} else {
				lines.emplace_back(lineno, line.substr(0, i), std::move(val));
			}
		}
	}
	if(!ifs.eof())
		gapr::report("error while reading catalog file.");
				
	if(lineno_name>=0) {
		switch(st) {
			case Mesh:
				parse(lineno_name, std::move(lines), mesh_infos.back());
				break;
			case Cube:
				parse(lineno_name, std::move(lines), cube_infos.back());
				break;
			default:
				gapr::report("line ", lineno_name, ": incomplete section");
		}
	}
	cleaner.abort();

	if(0) {
		for(auto& info: cube_infos) {
			gapr::print("name: ", info.name());
			gapr::print("loc: ", info.location());
			gapr::print("dir: ");
			for(int i=0; i<9; i++) {
				gapr::print("dir: ", info.xform.direction[i]);
			}
		}
	}
}

bool gapr::affine_xform::update_direction_inv() const noexcept {
	auto& inv_mat=direction_inv;
	auto& dir=direction;
	auto& rdir=inv_mat;
	auto det0=dir[1+1*3]*dir[2+2*3]-dir[2+1*3]*dir[1+2*3];
	auto det1=dir[1+2*3]*dir[2+0*3]-dir[1+0*3]*dir[2+2*3];
	auto det2=dir[1+0*3]*dir[2+1*3]-dir[1+1*3]*dir[2+0*3];
	auto det=dir[0+0*3]*det0+dir[0+1*3]*det1+dir[0+2*3]*det2;
	// XXX not correct
	if(std::fabs(det)<1e-12)
		return false;
	auto inv=1/det;
	if(std::fabs(inv)<1e-12)
		return false;
	rdir[0+0*3]=det0*inv;
	rdir[0+1*3]=(dir[0+2*3]*dir[2+1*3]-dir[0+1*3]*dir[2+2*3])*inv;
	rdir[0+2*3]=(dir[0+1*3]*dir[1+2*3]-dir[0+2*3]*dir[1+1*3])*inv;
	rdir[1+0*3]=det1*inv;
	rdir[1+1*3]=(dir[0+0*3]*dir[2+2*3]-dir[0+2*3]*dir[2+0*3])*inv;
	rdir[1+2*3]=(dir[1+0*3]*dir[0+2*3]-dir[0+0*3]*dir[1+2*3])*inv;
	rdir[2+0*3]=det2*inv;
	rdir[2+1*3]=(dir[2+0*3]*dir[0+1*3]-dir[0+0*3]*dir[2+1*3])*inv;
	rdir[2+2*3]=(dir[0+0*3]*dir[1+1*3]-dir[1+0*3]*dir[0+1*3])*inv;
	return true;
}
bool calcInverse(const std::array<double, 9>& dir, std::array<double, 9>& rdir) {
	auto det0=dir[1+1*3]*dir[2+2*3]-dir[2+1*3]*dir[1+2*3];
	auto det1=dir[1+2*3]*dir[2+0*3]-dir[1+0*3]*dir[2+2*3];
	auto det2=dir[1+0*3]*dir[2+1*3]-dir[1+1*3]*dir[2+0*3];
	auto det=dir[0+0*3]*det0+dir[0+1*3]*det1+dir[0+2*3]*det2;
	if(fabs(det)<1e-12)
		return false;
	auto inv=1/det;
	if(fabs(inv)<1e-12)
		return false;
	rdir[0+0*3]=det0*inv;
	rdir[0+1*3]=(dir[0+2*3]*dir[2+1*3]-dir[0+1*3]*dir[2+2*3])*inv;
	rdir[0+2*3]=(dir[0+1*3]*dir[1+2*3]-dir[0+2*3]*dir[1+1*3])*inv;
	rdir[1+0*3]=det1*inv;
	rdir[1+1*3]=(dir[0+0*3]*dir[2+2*3]-dir[0+2*3]*dir[2+0*3])*inv;
	rdir[1+2*3]=(dir[1+0*3]*dir[0+2*3]-dir[0+0*3]*dir[1+2*3])*inv;
	rdir[2+0*3]=det2*inv;
	rdir[2+1*3]=(dir[2+0*3]*dir[0+1*3]-dir[0+0*3]*dir[2+1*3])*inv;
	rdir[2+2*3]=(dir[0+0*3]*dir[1+1*3]-dir[1+0*3]*dir[0+1*3])*inv;
	return true;
}

std::string gapr::pattern_subst(const std::string& pat, std::array<uint32_t, 3> offsets) {
	const constexpr static std::size_t FMT_LEN=16;
	const constexpr static std::size_t PAT_LEN=32;

	enum {
		Text,
		Format0,
		FormatI,
		Select,
		MaybeDelta,
		Close
	} st=Text;
	uint32_t val;
	uint32_t delta;
	bool delta_sgn;
	char fmt_str[FMT_LEN];
	std::size_t fmt_str_i;

	std::string r;
	r.reserve(pat.size()+4*PAT_LEN);
	for(std::size_t i=0; i<pat.size(); i++) {
		auto c=pat[i];
		switch(st) {
			case Text:
				if(c=='<') {
					st=Format0;
					continue;
				}
				r.push_back(c);
				continue;
			case Format0:
				if(c!='>') {
					if(c!='%') {
						fmt_str[0]='%';
						fmt_str[1]=c;
						fmt_str_i=2;
					} else {
						fmt_str[0]='%';
						fmt_str_i=1;
					}
					st=FormatI;
					continue;
				}
				r.push_back('<');
				st=Text;
				continue;
			case FormatI:
				if(c=='>')
					gapr::report("no substitute");
				if(c=='%')
					gapr::report("no more '%'");
				if(c=='$') {
					auto tc=fmt_str[fmt_str_i-1];
					if(tc!='o' && tc!='u' && tc!='x' && tc!='X')
						gapr::report("wrong conversion specifier");
					fmt_str[fmt_str_i]='\0';
					st=Select;
					continue;
				}
				if(fmt_str_i+1>=FMT_LEN)
					gapr::report("pattern too long");
				fmt_str[fmt_str_i++]=c;
				continue;
			case Select:
				if(c=='x') {
					val=offsets[0];
				} else if(c=='y') {
					val=offsets[1];
				} else if(c=='z') {
					val=offsets[2];
				} else {
					gapr::report("invalid selector");
				}
				st=MaybeDelta;
				continue;
			case MaybeDelta:
				if(c=='>') {
					delta_sgn=false;
					delta=0;
					break;
				}
				if(c=='+') {
					delta_sgn=false;
				} else if(c=='-') {
					delta_sgn=true;
				} else {
					gapr::report("invalid char");
				}
				{
					auto [eptr, ec]=std::from_chars(&pat[i+1], &pat[pat.size()], delta, 10);
					std::size_t skip=eptr-&pat[i+1];
					if(ec!=std::errc{} || skip<=0)
						gapr::report("empty delta: ", &pat[i+1]);
					i+=skip;
				}
				st=Close;
				continue;
			case Close:
				if(c=='>')
					break;
				gapr::report("expected '>'");
		}
		char pat_str[PAT_LEN];
		auto n=std::snprintf(pat_str, PAT_LEN, fmt_str, delta_sgn?val-delta:val+delta);
		if(n<0)
			gapr::report("errno: ", errno);
		if(static_cast<std::size_t>(n)>=PAT_LEN)
			gapr::report("substitute text too long");
		std::copy(&pat_str[0], &pat_str[n], std::back_inserter(r));
		st=Text;
	}
	if(st!=Text)
		gapr::report("no matching '>'");
	return r;
}

namespace gapr_test { int chk_pattern_subst() {
	std::string s{"<#X$x-011>/<04u$y-9>/<#o$z-0x9>.xxx<>><><>>>"};
	auto t=gapr::pattern_subst(s, {10, 11, 12});
	gapr::print(s, " -> ", t);
	if(t!="0X1/0002/03.xxx<><<>>")
		return -1;
	return 0;
} }

static std::array<double, 9> def_direction{
	1.0, 0.0, 0.0,
	0.0, 1.0, 0.0,
	0.0, 0.0, 1.0,
};
void gapr::nrrd_output::finish_impl(const void* ptr, gapr::cube_type type, std::array<unsigned int, 3> sizes, std::size_t ystride, std::size_t zstride, const std::array<double, 3>* origin, const std::array<double, 9>* direction) {
	comment("Created by " PACKAGE_NAME " " PACKAGE_VERSION);
	if(!_base)
		throw std::runtime_error{"Failed to write header."};
	//_base.precision(13);
	//_base.setf(std::ios::scientific);

	switch(type) {
		case gapr::cube_type::u8: _base<<"type: uint8\n"; break;
		case gapr::cube_type::i8: _base<<"type: int8\n"; break;
		case gapr::cube_type::u16: _base<<"type: uint16\n"; break;
		case gapr::cube_type::i16: _base<<"type: int16\n"; break;
		case gapr::cube_type::u32: _base<<"type: uint32\n"; break;
		case gapr::cube_type::i32: _base<<"type: int32\n"; break;
		case gapr::cube_type::u64: _base<<"type: uint64\n"; break;
		case gapr::cube_type::i64: _base<<"type: int64\n"; break;
		case gapr::cube_type::f32: _base<<"type: float\n"; break;
		case gapr::cube_type::f64: _base<<"type: double\n"; break;
		case gapr::cube_type::unknown:
											throw std::logic_error{"Unknown cube type."};
		default:
											throw std::runtime_error{"cube type not supported"};
	}

	if(_gzip)
		_base<<"encoding: gzip\n";
	else
		_base<<"encoding: raw\n";
	_base<<"dimension: 3\n";
	_base<<"sizes: "<<sizes[0]<<" "<<sizes[1]<<" "<<sizes[2]<<"\n";
	_base<<"space: right-anterior-superior\nspace directions:";
	for(unsigned int i=0; i<3; ++i) {
		const auto& dir=direction?*direction:def_direction;
		_base<<" ("<<dir[i*3+0]<<','<<dir[i*3+1]<<','<<dir[i*3+2]<<')';
	}
	_base<<'\n';
	if(gapr::little_endian()) {
		_base<<"endian: little\n";
	} else {
		_base<<"endian: big\n";
	}
	_base<<"\n";
	if(!_base)
		throw std::runtime_error{"Failed to write header."};

	boost::iostreams::filtering_streambuf<boost::iostreams::output> gzip_buf{};
	// deflateInit2(&strm, 5, Z_DEFLATED, 15+16, 9, Z_FILTERED);
	if(_gzip)
		gzip_buf.push(boost::iostreams::gzip_compressor{4});
	gzip_buf.push(_base);
	std::ostream gzip{&gzip_buf};

	auto bpv=voxel_size(type);
	for(unsigned int z=0; z<sizes[2]; z++) {
		for(unsigned int y=0; y<sizes[1]; y++) {
			auto row=static_cast<const char*>(ptr)+y*ystride+z*zstride;
			gzip.write(row, sizes[0]*bpv);
		}
	}

	gzip_buf.reset();
	if(!_base)
		throw std::runtime_error{"Failed to write body."};
}

void gapr::nrrd_output::save_impl(const std::string& file, const void* ptr, gapr::cube_type type, std::array<unsigned int, 3> sizes) {
	std::ofstream fs{file};
	if(!fs)
		throw std::runtime_error{"failed to open file for write"};
	gapr::nrrd_output nrrd{fs};
	nrrd.header();
	nrrd.finish(ptr, type, sizes[0], sizes[1], sizes[2]);
}
