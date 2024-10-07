#include "gapr/swc-helper.hh"
#include "gapr/model.hh"
#include "gapr/parser.hh"
#include "gapr/utility.hh"

#include <ostream>
#include <istream>
#include <sstream>
#include <cassert>
#include <charconv>

#include "config.hh"

static constexpr std::string_view ext_tag_prev_eq{"#" PACKAGE_NAME "!-1="};
static constexpr auto ext_tag_prev=ext_tag_prev_eq.substr(0, ext_tag_prev_eq.size()-1);
static constexpr auto ext_tag=ext_tag_prev.substr(0, ext_tag_prev.size()-2);

template<typename T=int>
static inline std::from_chars_result from_chars(const char* first, const char* last, double& value, T=T{}) {
	char* end;
	auto v=std::strtod(first, &end);
	if(end==first)
		return {first, std::errc::invalid_argument};
	value=v;
	return {end, std::errc{}};
}
template<typename T>
static inline std::from_chars_result parse_field(const char* begin, const char* end, T& val) {
	using namespace std;
	auto res=from_chars(begin, end, val);
	if(res.ec!=std::errc{})
		return res;
	if(res.ptr>=end || !std::isspace(*res.ptr))
		return {res.ptr, std::errc::invalid_argument};
	return res;
}
static std::from_chars_result parse_swc_node(std::string_view l, gapr::swc_node& n, std::size_t& p_pos) {
	std::from_chars_result res;
	auto end=l.data()+l.size();
	res=parse_field(l.data(), end, n.id);
	if(res.ec!=std::errc{})
		return res;
	res=parse_field(res.ptr+1, end, n.type);
	if(res.ec!=std::errc{})
		return res;
	res=parse_field(res.ptr+1, end, n.pos[0]);
	if(res.ec!=std::errc{})
		return res;
	res=parse_field(res.ptr+1, end, n.pos[1]);
	if(res.ec!=std::errc{})
		return res;
	res=parse_field(res.ptr+1, end, n.pos[2]);
	if(res.ec!=std::errc{})
		return res;
	res=parse_field(res.ptr+1, end, n.radius);
	if(res.ec!=std::errc{})
		return res;
	p_pos=res.ptr+1-l.data();
	res=std::from_chars(res.ptr+1, end, n.par_id);
	if(res.ec!=std::errc{})
		return res;
	if(res.ptr!=end)
		return {res.ptr, std::errc::invalid_argument};
	return res;
}

bool gapr::swc_input::read() {
	auto getline=[this]() ->bool {
		switch(_buf_st) {
			case _buf_avail:
				++_line_no;
				_buf_st=_buf_empty;
				return true;
			case _buf_bad:
				return false;
			case _buf_eof:
				return false;
		}
		++_line_no;
		if(std::getline(_base, _buf))
			return true;
		_buf_st=_base.eof()?_buf_eof:_buf_bad;
		return false;
	};
	auto parse_swc_line=[this]() ->std::pair<std::size_t, std::errc> {
		if(_buf[0]=='#') {
			if(_buf.size()>=ext_tag.size() && _buf.compare(1, ext_tag.size()-1, ext_tag.substr(1))==0) {
				int64_t id;
				auto res=gapr::make_parser(id).from_dec(&_buf[ext_tag.size()], _buf.size()-ext_tag.size());
				if(!res.second)
					return {ext_tag.size()+1, std::errc::invalid_argument};
				auto skip=res.first+ext_tag.size();
				if(skip>=_buf.size())
					return {skip, std::errc::invalid_argument};
				if(id==-1) {
					if(_prev_id==-1)
						return {ext_tag.size()+1, std::errc::result_out_of_range};
					id=_prev_id;
				} else {
					auto mapi=_ids.find(id);
					if(mapi==_ids.end())
						return {ext_tag.size()+1, std::errc::result_out_of_range};
				}
				if(_buf[skip]=='=') {
					++skip;
					if(skip+8!=_buf.size())
						return {skip, std::errc::invalid_argument};
					gapr::misc_attr attr;
					auto res=gapr::make_parser(attr.data).from_hex<8>(&_buf[skip]);
					if(!res)
						return {skip, std::errc::invalid_argument};
					_cur_node.id=id;
					_cur_attr=attr;
					_tag=tags::misc_attr;
					return {0, std::errc{}};
				} else if(_buf[skip]=='@') {
					++skip;
					unsigned int skipdot=0;
					if(skip<_buf.size() && _buf[skip]=='.')
						++skipdot;
					res=gapr::parse_name(&_buf[skip+skipdot], _buf.size()-skip-skipdot);
					if(!res.second)
						return {skip, std::errc::invalid_argument};
					auto j=skip+skipdot+res.first;
					if(j<_buf.size()) {
						if(_buf[j]!='=')
							return {j, std::errc::invalid_argument};
						/*
						if(j+1>=_buf.size())
							return {j, std::errc::invalid_argument};
							*/
						++j;
					}
					_cur_node.id=id;
					_annot_skip=skip;
					_annot_keyl=res.first+skipdot;
					_annot_skip2=j;
					_tag=tags::annot;
					return {0, std::errc{}};
				} else if(_buf[skip]=='/') {
					++skip;
					int64_t id2;
					res=gapr::make_parser(id2).from_dec(&_buf[skip], _buf.size()-skip);
					if(!res.second)
						return {skip, std::errc::invalid_argument};
					if(id2==-1)
						return {skip, std::errc::result_out_of_range};
					if(res.first+skip!=_buf.size())
						return {res.first+skip, std::errc::invalid_argument};
					auto mapi=_ids.find(id2);
					if(mapi==_ids.end())
						return {skip, std::errc::result_out_of_range};
					_cur_node.id=id;
					_cur_node.par_id=id2;
					_tag=tags::loop;
					return {0, std::errc{}};
				} else {
					return {skip, std::errc::invalid_argument};
				}
			} else {
				_buf.push_back('\n');
				_tag=tags::comment;
				return {0, std::errc{}};
			}
		} else {
			std::size_t p_pos;
			auto [ptr, ec]=parse_swc_node(_buf, _cur_node, p_pos);
			if(ec!=std::errc{})
				return {ptr-&_buf[0], ec};
			if(_cur_node.id==-1)
				return {0, std::errc::result_out_of_range};
			if(_cur_node.par_id!=-1) {
				auto mapi=_ids.find(_cur_node.par_id);
				if(mapi==_ids.end())
					return {p_pos, std::errc::result_out_of_range};
			}
			auto ins=_ids.emplace(_cur_node.id);
			if(!ins.second)
				return {0, std::errc::result_out_of_range};

			gapr::misc_attr attr{};
			if(std::getline(_base, _buf)) {
				if(_buf.size()>=ext_tag_prev_eq.size() && _buf.compare(0, ext_tag_prev_eq.size(), ext_tag_prev_eq)==0) {
					if(_buf.back()=='\r')
						_buf.pop_back();
					++_line_no;
					const std::size_t skip=ext_tag_prev_eq.size();
					if(skip+8!=_buf.size())
						return {skip, std::errc::invalid_argument};
					auto res=gapr::make_parser(attr.data).from_hex<8>(&_buf[skip]);
					if(!res)
						return {skip, std::errc::invalid_argument};
					_buf_st=_buf_empty;
				} else {
					_buf_st=_buf_avail;
				}
			} else {
				_buf_st=_base.eof()?_buf_eof:_buf_bad;
			}

			_prev_id=_cur_node.id;
			_cur_attr=attr;
			_tag=tags::node;
			return {0, std::errc{}};
		};
		return {0, std::errc::invalid_argument};
	};
	while(getline()) {
		assert(_buf_st==_buf_empty);
		if(_buf.empty()) {
			_buf.push_back('\n');
			_tag=tags::comment;
			return true;
		}
		if(_buf.back()=='\r')
			_buf.pop_back();
		if(_buf.empty()) {
			_buf.push_back('\n');
			_tag=tags::comment;
			return true;
		}
		auto [pos, err]=parse_swc_line();
		if(err!=std::errc{}) {
			gapr::print("err read: ", _line_no, ':', pos, " ", (int)err);
			break;
		}
		return true;
	}
	_tag=_tag_empty;
	return false;
}

#if 0
unittest {
	std::ofstream os{"/tmp/asdf.swc"};
	gapr::swc_output swcout{os};
	std::ifstream fs{fn};
	gapr::swc_input swc{fs};
	while(swc.read()) {
		switch(swc.tag()) {
			case swc.tags::comment:
				swcout.comment(swc.comment().data());
				break;
			case swc.tags::node:
				{
					gapr::node_id id(swc.node().id);
					gapr::node_id par(swc.node().par_id==-1?0:swc.node().par_id);
					auto pos=swc.node().pos;
					gapr::node_attr attr{pos[0], pos[1], pos[2]};
					attr.misc=swc.misc_attr();
					swcout.node(id, par, attr);
				}
				break;
			case swc.tags::annot:
				swcout.annot(swc.id(), swc.annot().data());
				break;
			case swc.tags::misc_attr:
				swcout.misc_attr(swc.id(), swc.misc_attr());
				break;
			case swc.tags::loop:
				swcout.loop(swc.id(), swc.loop());
				break;
		}
	}
	if(!swc.eof())
		gapr::report("Failed to read swc file.");
	os.close();
	if(!os)
		throw;
}
#endif
#if 0

inline static bool write_swc_lines(std::ostream& ofs, gapr::SwcHelper_PRIV& swc) noexcept {
	ofs.setf(std::ios::fixed);
	ofs.precision(3);
	// XXX
	ofs<<"#\n# generated by GAPR\n#   <https://gapr.sourceforge.io/>\n#\n\n";
	if(!ofs)
		return false;
	auto n=swc.nodes.size();
	for(std::size_t i=0; i<n; i++) {
		const auto& node=swc.nodes[i];
		const auto& attr=node.second;
		ofs<<node.first<<' '<<attr.t()<<' ';
		for(std::size_t j=0; j<3; j++)
			ofs<<attr.pos(j)<<' ';
		ofs<<attr.r()<<' '<<swc.pars[i]<<'\n';
		if(!ofs)
			return false;
		if(attr.extended()) {
			ofs<<"#GAPR:!-1=";
			if(!ofs)
				return false;
			auto m=attr.misc();
			char buf[9];
			for(int i=7; i>=0; i--)
				buf[7-i]=("0123456789ABCDEF"[(m>>i*4)&0x0f]);
			buf[8]='\n';
			ofs.write(buf, 9);
			if(!ofs)
				return false;
		}
	}
	for(const auto& prop: swc.props) {
		ofs<<"#GAPR:!"<<prop.first<<'@'<<prop.second<<'\n';
		if(!ofs)
			return false;
	}
	return true;
}

gapr::SwcHelper::SwcHelper(const char* fn): SwcHelper{} {
	std::ifstream ifs{fn};
	if(!ifs)
		throw std::ios_base::failure{fn, std::io_errc::stream};
	nid_t prev_id{static_cast<nid_t>(-1)}; // XXX
	std::size_t row=0;
	std::string line;
	SwcHelper_PRIV tmp{};
	while(std::getline(ifs, line)) {
		while(line.size()>0 && std::isspace(line[line.size()-1]))
			line.pop_back();
		if(line.size()>0) {
			auto res=parse_swc_line(tmp, line, prev_id);
			if(res.second!=std::errc{}) {
				std::ostringstream oss;
				oss<<fn<<':'<<row+1<<':'<<res.first+1<<": ";
				throw std::system_error{std::make_error_code(res.second), oss.str()};
			}
		}
		row++;
	}
	if(!ifs.eof())
		throw std::ios_base::failure{fn, std::io_errc::stream};
	std::swap(_priv, tmp);
}

void gapr::SwcHelper::save(const char* fn) {
	assert(_priv.nodes.size()==_priv.pars.size());

	std::ofstream ofs{fn};
	if(!ofs)
		throw std::ios_base::failure{fn, std::io_errc::stream};
	if(!write_swc_lines(ofs, _priv))
		throw std::ios_base::failure{fn, std::io_errc::stream};
	ofs.close();
	if(!ofs) {
		fprintf(stderr, "err close\n");
		throw std::ios_base::failure{fn, std::io_errc::stream};
	}
}

gapr::Delta::Delta(SwcHelper&& swc_): Delta{} {
	assert(swc_._priv.nodes.size()==swc_._priv.pars.size());

	auto nodes=std::move(swc_._priv.nodes);
	auto pars=std::move(swc_._priv.pars);
	auto props=std::move(swc_._priv.props);
	const auto& idmap=swc_._priv.idmap;

	std::unordered_set<gapr::nid_t> named_neurons{};
	for(auto& prop: props) {
		auto it=idmap.find(prop.first);
		assert(it!=idmap.end());
		auto id=-static_cast<nid_t>(it->second+1);
		const auto& val=prop.second;
		if(val.compare(0, 6, "neuron", 6)==0 &&
				(val.size()==6 || (val.size()>7 && val[6]=='='))) {
			named_neurons.emplace(prop.first);
		}
		prop.first=id;
	}

	auto n=nodes.size();
	nid_t swclast{static_cast<nid_t>(-1)}; //XXX
	std::vector<nid_t> edges{};
	for(std::size_t i=0; i<n; i++) {
		auto swcpar=pars[i];
		auto& node=nodes[i];
		auto id=-static_cast<nid_t>(i+1);
		if(swcpar!=static_cast<nid_t>(-1)) { // XXX
			auto c=edges.empty()?1:(swcpar==swclast?2:0);
			switch(c) {
				case 0:
					edges.emplace_back(0);
				case 1:
					{
						auto it=idmap.find(swcpar);
						assert(it!=idmap.end());
						edges.emplace_back(-static_cast<nid_t>(it->second+1));
					}
				default:
					edges.emplace_back(id);
			}
		} else {
			if(node.second.t()==1) {
				auto it=named_neurons.find(node.first);
				if(it==named_neurons.end())
					props.emplace_back(id, "neuron");
			}
		}
		swclast=node.first;
		node.first=0;
	}

	set_nodes=std::move(nodes);
	add_edges=std::move(edges);
	set_props=std::move(props);
}


// Serializer
template<> struct gapr::SerializerUsePredictor<gapr::Delta, 0> {
	constexpr static bool value=true;
};
template<> struct gapr::SerializerAdaptor<gapr::Delta, 0> {
	template<typename T> static auto& map(T& obj) { return obj.remove_props; }
};
template<> struct gapr::SerializerUsePredictor<gapr::Delta, 1> {
	constexpr static bool value=true;
};
template<typename T> inline static auto sub_if_nz(T obj, T obj0) {
	return static_cast<std::make_signed_t<T>>(obj==0?0:obj-obj0);
}
template<typename T, typename Td> inline static T add_if_nz(Td d, T obj0) {
	return static_cast<T>(d==0?0:d+obj0);
}
template<> struct gapr::SerializerPredictor<gapr::Delta, 1> {
	template<typename T> static auto sub(T obj, T obj0) {
		return sub_if_nz(obj, obj0);
	}
	template<typename T, typename Td> static T add(Td d, T obj0) {
		return add_if_nz(d, obj0);
	}
};
template<> struct gapr::SerializerAdaptor<gapr::Delta, 1> {
	template<typename T> static auto& map(T& obj) { return obj.remove_edges; }
};
template<> struct gapr::SerializerUsePredictor<gapr::Delta, 2> {
	constexpr static bool value=true;
};
template<> struct gapr::SerializerAdaptor<gapr::Delta, 2> {
	template<typename T> static auto& map(T& obj) { return obj.remove_nodes; }
};
template<> struct gapr::SerializerUsePredictor<gapr::Delta, 3> {
	constexpr static bool value=true;
};
template<> struct gapr::SerializerAdaptor<gapr::Delta, 3> {
	template<typename T> static auto& map(T& obj) { return obj.set_nodes; }
};
template<> struct gapr::SerializerUsePredictor<gapr::Delta, 4> {
	constexpr static bool value=true;
};
template<> struct gapr::SerializerPredictor<gapr::Delta, 4> {
	template<typename T> static auto sub(T obj, T obj0) {
		return sub_if_nz(obj, obj0);
	}
	template<typename T, typename Td> static T add(Td d, T obj0) {
		return add_if_nz(d, obj0);
	}
};
template<> struct gapr::SerializerAdaptor<gapr::Delta, 4> {
	template<typename T> static auto& map(T& obj) { return obj.add_edges; }
};
template<> struct gapr::SerializerUsePredictor<gapr::Delta, 5> {
	constexpr static bool value=true;
};
template<> struct gapr::SerializerAdaptor<gapr::Delta, 5> {
	template<typename T> static auto& map(T& obj) { return obj.set_props; }
};

template<> struct gapr::SerializerAdaptor<gapr::NodeAttr, 0> {
	template<typename T> static auto& map(T& obj) { return obj.data()[0]; }
};
template<> struct gapr::SerializerAdaptor<gapr::NodeAttr, 1> {
	template<typename T> static auto& map(T& obj) { return obj.data()[1]; }
};
template<> struct gapr::SerializerAdaptor<gapr::NodeAttr, 2> {
	template<typename T> static auto& map(T& obj) { return obj.data()[2]; }
};
template<> struct gapr::SerializerPredictor<gapr::NodeAttr, 3> {
	template<typename T> static auto sub(T obj, T obj0) {
		return static_cast<std::make_unsigned_t<T>>(obj^obj0);
	}
	template<typename T, typename Td> static T add(Td d, T obj0) {
		return static_cast<T>(d^obj0);
	}
};
template<> struct gapr::SerializerAdaptor<gapr::NodeAttr, 3> {
	template<typename T> static auto& map(T& obj) { return obj.data()[3]; }
};

struct gapr::DeltaSerializer_PRIV: gapr::Serializer<Delta> { };
gapr::DeltaSerializer::DeltaSerializer():
	_ptr{new DeltaSerializer_PRIV{}}, _ok{true}
{ }
std::size_t gapr::DeltaSerializer::save(const Delta& d, void* p, std::size_t l) noexcept {
	auto r=_ptr->save(d, p, l);
	_ok=static_cast<bool>(*_ptr);
	return r;
}
void gapr::DeltaSerializer::destroy(DeltaSerializer_PRIV* p) { delete p; }

struct gapr::DeltaDeserializer_PRIV: gapr::Deserializer<Delta> { };
gapr::DeltaDeserializer::DeltaDeserializer():
	_ptr{new DeltaDeserializer_PRIV{}}, _ok{true}
{ }
std::size_t gapr::DeltaDeserializer::load(Delta& d, const void* p, std::size_t l) noexcept {
	auto r=_ptr->load(d, p, l);
	_ok=static_cast<bool>(*_ptr);
	return r;
}
void gapr::DeltaDeserializer::destroy(DeltaDeserializer_PRIV* p) { delete p; }
#endif
#if 0
void Session::importSwc() {
	std::vector<Point> points;
	std::vector<int64_t> parents;
	try {
		for(auto& filename: dlg.selectedFiles()) {
			std::map<int64_t, int64_t> idmap;
			// XXX use ifstream and move to shared?
			QFile sf_{filename};
			if(!sf_.open(QIODevice::ReadOnly)) {
				throwError("Failed to open file");
			}
			QTextStream sf{&sf_};

			while(!sf.atEnd()) {
				auto line=sf.readLine();
				if(line.isEmpty()) continue;
				if(line[0]=='#') continue;
				QTextStream fs(&line);
				fs.setRealNumberNotation(QTextStream::SmartNotation);
				fs.setRealNumberPrecision(13);
				int64_t id, par;
				int16_t type;
				double x, y, z, r;
				fs>>id>>type>>x>>y>>z>>r>>par;
				if(fs.status()!=QTextStream::Ok)
					throwError(QString{"Failed to parse line %1"}+=line);

				idmap[id]=points.size();
				points.emplace_back(x, y, z, r, type);
				if(par==-1) {
					parents.push_back(-1);
				} else {
					parents.push_back(idmap[par]);
				}
			}
		}
	} catch(const std::exception& e) {
		showWarning("Failed to import SWC", e.what(), this);
		return;
	}
	if(points.size()<=0)
		return;
}
			//
			//
			//
			//
			//check id
				//check id
#endif


static inline std::to_chars_result to_chars(char* first, char* last, double value, bool sci) {
	char fmt[]="%.4f";
	if(sci)
		fmt[3]='g';
	auto r=std::snprintf(first, last-first, fmt, value);
	if(first+r>=last)
		return {first, std::errc::invalid_argument};
	return {first+r, std::errc{}};
}

template<typename T, typename... Args>
std::to_chars_result format_field(char* begin, char* end, T val, Args&&... args) {
	using namespace std;
	auto res=to_chars(begin, end, val, std::forward<Args>(args)...);
	if(res.ec!=std::errc{})
		return res;
	if(res.ptr+1>=end)
		return {res.ptr, std::errc::invalid_argument};
	*res.ptr=' ';
	return res;
};

void gapr::swc_output::comment(const char* str) {
	if(!(_base<<str))
		throw std::ios_base::failure{"failed to write comment"};
}

constexpr static char SWC_HDR[]{"##\n## generated by " PACKAGE_NAME
	"\n##   <" PACKAGE_URL ">\n##\n\n"};
void gapr::swc_output::header() {
	if(!(_base<<SWC_HDR))
		throw std::ios_base::failure{"failed to write header"};
}
static std::ostream& write_attr_misc(std::ostream& str, gapr::misc_attr attr) {
	char buf[10];
	buf[0]='=';
	auto m=attr.data;
	for(int i=7; i>=0; i--)
		buf[8-i]=("0123456789ABCDEF"[(m>>i*4)&0x0f]);
	buf[9]='\n';
	return str.write(buf, 10);
}
static inline std::to_chars_result format_swc_node(std::array<char, 128>& buf, const gapr::swc_node& n) {
	std::to_chars_result res;
	auto end=buf.data()+buf.size();
	res=format_field(buf.data(), end, n.id);
	if(res.ec!=std::errc{})
		return res;
	res=format_field(res.ptr+1, end, n.type);
	if(res.ec!=std::errc{})
		return res;
	res=format_field(res.ptr+1, end, n.pos[0], false);
	if(res.ec!=std::errc{})
		return res;
	res=format_field(res.ptr+1, end, n.pos[1], false);
	if(res.ec!=std::errc{})
		return res;
	res=format_field(res.ptr+1, end, n.pos[2], false);
	if(res.ec!=std::errc{})
		return res;
	res=format_field(res.ptr+1, end, n.radius, true);
	if(res.ec!=std::errc{})
		return res;
	res=std::to_chars(res.ptr+1, end, n.par_id);
	if(res.ec!=std::errc{})
		return res;
	if(res.ptr>=end)
		return {res.ptr, std::errc::invalid_argument};
	*res.ptr='\n';
	return {res.ptr+1, res.ec};
}

void gapr::swc_output::node(gapr::node_id id, gapr::node_id par, gapr::node_attr attr) {
	assert(id!=gapr::node_id{});

	std::array<char, 128> buf;
	auto res=format_swc_node(buf, swc_node{int64_t{id.data}, attr.misc.t(),
		{attr.pos(0), attr.pos(1), attr.pos(2)}, attr.misc.r(),
		{par==gapr::node_id{}?-1:int64_t{par.data}}});
	if(res.ec!=std::errc{})
		throw std::system_error{std::make_error_code(res.ec), "failed to format line"};
	if(!_base.write(buf.data(), res.ptr-buf.data()))
		throw std::ios_base::failure{"failed to write node"};
	if(attr.misc.extended()) {
		_base<<ext_tag_prev;
		if(!write_attr_misc(_base, attr.misc))
			throw std::ios_base::failure{"failed to write node attr"};
	}
}
void gapr::swc_output::node(const gapr::swc_node& n) {
	assert(n.id!=-1);

	std::array<char, 128> buf;
	auto res=format_swc_node(buf, n);
	if(res.ec!=std::errc{})
		throw std::system_error{std::make_error_code(res.ec), "failed to format line"};
	if(!_base.write(buf.data(), res.ptr-buf.data()))
		throw std::ios_base::failure{"failed to write node"};
}
void gapr::swc_output::annot(gapr::node_id id, const char* key, const char* val) {
	assert(id!=gapr::node_id{});
	_base<<ext_tag<<int64_t{id.data};
	_base<<'@'<<key;
	_base<<'='<<val;
	if(!(_base<<'\n'))
		throw std::ios_base::failure{"failed to write annot"};
}
void gapr::swc_output::annot(int64_t id, const char* keyval) {
	assert(id!=-1);
	_base<<ext_tag<<id;
	_base<<'@'<<keyval;
	if(!(_base<<'\n'))
		throw std::ios_base::failure{"failed to write annot"};
}
void gapr::swc_output::misc_attr(int64_t id, gapr::misc_attr attr) {
	assert(id!=-1);
	_base<<ext_tag<<id;
	if(!write_attr_misc(_base, attr))
		throw std::ios_base::failure{"failed to write node attr"};
}
void gapr::swc_output::loop(gapr::node_id id, gapr::node_id id2) {
	assert(id!=gapr::node_id{});
	assert(id2!=gapr::node_id{});
	_base<<ext_tag<<int64_t{id.data};
	_base<<'/'<<int64_t{id2.data};
	if(!(_base<<'\n'))
		throw std::ios_base::failure{"failed to write loop"};
}
void gapr::swc_output::loop(int64_t id, int64_t id2) {
	assert(id!=-1);
	assert(id2!=-1);
	_base<<ext_tag<<id;
	_base<<'/'<<id2;
	if(!(_base<<'\n'))
		throw std::ios_base::failure{"failed to write loop"};
}


// tests =======================
#include "gapr/utility.hh"

#if 0
template<typename T>
static T rand_int(T l0, T l1) {
	auto v=static_cast<T>(std::rand());
	if(sizeof(T)>sizeof(int))
		v=(v<<32)^static_cast<T>(std::rand());
	if(l1-l0+1)
		return v%(l1-l0+1)+l0;
	return v+l0;
}
static std::string rand_str(std::size_t l0, std::size_t l1) {
	std::size_t l=rand_int<std::size_t>(l0, l1);
	std::string s;
	s.reserve(l);
	for(std::size_t i=0; i<l; i++) {
		auto c=std::rand()%(26+26+10);
		if(c<10) {
			s.push_back('0'+c);
			continue;
		}
		c-=10;
		if(c<26) {
			s.push_back('a'+c);
			continue;
		}
		c-=26;
		s.push_back('A'+c);
	}
	return s;
}


static gapr::Delta test_ser_deser_gend(std::size_t c0, std::size_t c1, gapr::nid_t n0, gapr::nid_t n1, int32_t x0, int32_t x1) {
	gapr::Delta delta;
	auto nrp=rand_int<std::size_t>(c0, c1);
	for(std::size_t i=0; i<nrp; i++)
		delta.remove_props.emplace_back(rand_int<gapr::nid_t>(n0, n1), rand_str(0,7));
	auto nre=rand_int<std::size_t>(c0, c1);
	for(std::size_t i=0; i<nre; i++) {
		auto id=rand_int<gapr::nid_t>(n0, n1);
		while(delta.remove_edges.size()>0 && delta.remove_edges.back()==id)
			id=rand_int<gapr::nid_t>(n0, n1);
		delta.remove_edges.emplace_back(id);
	}
	auto nrn=rand_int<std::size_t>(c0, c1);
	for(std::size_t i=0; i<nrn; i++)
		delta.remove_nodes.emplace_back(rand_int<gapr::nid_t>(n0, n1));
	auto nae=rand_int<std::size_t>(c0, c1);
	for(std::size_t i=0; i<nae; i++) {
		auto id=rand_int<gapr::nid_t>(n0, n1);
		while(delta.add_edges.size()>0 && delta.add_edges.back()==id)
			id=rand_int<gapr::nid_t>(n0, n1);
		delta.add_edges.emplace_back(id);
	}
	auto nsn=rand_int<std::size_t>(c0, c1);
	for(std::size_t i=0; i<nsn; i++) {
		auto x=rand_int<int32_t>(x0, x1);
		auto y=rand_int<int32_t>(x0, x1);
		auto z=rand_int<int32_t>(x0, x1);
		auto r=rand_int<int32_t>(x0, x1);
		delta.set_nodes.emplace_back(rand_int<gapr::nid_t>(n0, n1), gapr::NodeAttr{x, y, z, r});
	}
	auto nsp=rand_int<std::size_t>(c0, c1);
	for(std::size_t i=0; i<nsp; i++)
		delta.set_props.emplace_back(rand_int<gapr::nid_t>(n0, n1), rand_str(5,13));
	return delta;
}

static int test_ser_deser(std::size_t nr, std::size_t c0, std::size_t c1, gapr::nid_t n0, gapr::nid_t n1, int32_t x0, int32_t x1, std::size_t b0, std::size_t b1) {
	std::size_t ninput=0;
	std::size_t noutput=0;
	int ndiff=0;
	std::vector<char> buf{};
	std::vector<gapr::Delta> deltas;
	for(std::size_t i=0; i<nr; i++)
		deltas.push_back(test_ser_deser_gend(c0, c1, n0, n1, x0, x1));
	gapr::print("gen done");
	for(std::size_t i=0; i<nr; i++) {
		auto& delta=deltas[i];
		auto size=delta.remove_props.size()*sizeof(delta.remove_props[0])
			+delta.remove_edges.size()*sizeof(delta.remove_edges[0])
			+delta.remove_nodes.size()*sizeof(delta.remove_nodes[0])
			+delta.add_edges.size()*sizeof(delta.add_edges[0])
			+delta.set_nodes.size()*sizeof(delta.set_nodes[0])
			+delta.set_props.size()*sizeof(delta.set_props[0]);
		ninput+=size;
		buf.reserve(size);
		std::size_t save_size, load_size;
		{
			std::size_t i=0;
			gapr::DeltaSerializer ser{};
			do {
				auto n=rand_int<std::size_t>(b0, b1);
				if(n+i>buf.size())
					buf.resize(n+i);
				auto r=ser.save(delta, &buf[i], n);
				i+=r;
			} while(ser);
			save_size=i;
		}
		noutput+=save_size;
		gapr::Delta delta2;
		{
			buf.resize(buf.size()+1000);
			std::size_t i=0;
			gapr::DeltaDeserializer ser{};
			do {
				auto n=rand_int<std::size_t>(b0, b1);
				if(n+i>buf.size())
					buf.resize(n+i);
				auto r=ser.load(delta2, &buf[i], n);
				i+=r;
			} while(ser);
			load_size=i;
		}
		ndiff+=(save_size==load_size?0:1);
		ndiff+=(delta.remove_props==delta2.remove_props?0:1);
		ndiff+=(delta.remove_edges==delta2.remove_edges?0:1);
		ndiff+=(delta.remove_nodes==delta2.remove_nodes?0:1);
		ndiff+=(delta.add_edges==delta2.add_edges?0:1);
		ndiff+=(delta.set_nodes==delta2.set_nodes?0:1);
		ndiff+=(delta.set_props==delta2.set_props?0:1);
	}
	gapr::print("nin: ", ninput, ", nout: ", noutput, ", ndiff: ", ndiff);
	return ndiff;
}
#endif

namespace gapr_test { int chk_serializer() {
	int r=0;
#if 0
	r|=test_ser_deser(10000, 0, 5, -5, 5, -5, 5, 0, 3);
	r|=test_ser_deser(1000, 500, 1000, -65535, 65535, -65535, 65535, 0, 7);
	r|=test_ser_deser(1000, 500, 1000, -65535, 65535, -65535, 65535, 512, 1024);
	r|=test_ser_deser(1000, 500, 1000, -65535, 65535, -65535, 65535, 1024, 4096);
	r|=test_ser_deser(1000000, 9, 17, std::numeric_limits<gapr::nid_t>::min(), std::numeric_limits<gapr::nid_t>::min()+4095, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min()+4095, 1024, 2048);
	r|=test_ser_deser(1000000, 9, 17, std::numeric_limits<gapr::nid_t>::max()-65535, std::numeric_limits<gapr::nid_t>::max(), std::numeric_limits<int32_t>::max()-65535, std::numeric_limits<int32_t>::max(), 1024, 2048);
	r|=test_ser_deser(1000000, 9, 17, std::numeric_limits<gapr::nid_t>::min(), std::numeric_limits<gapr::nid_t>::max(), std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(), 1024, 2048);
#endif
	return r;
} }
