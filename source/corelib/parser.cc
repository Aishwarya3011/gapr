#include "gapr/parser.hh"

#include "gapr/utility.hh"

#include <limits>
#include <charconv>

inline static unsigned int randomize(unsigned int& i) {
	unsigned int p=i;
	i=(i+29)%64;
	return p;
}
inline gapr::AsciiTables::AsciiTables() {
	static_assert('A'+25=='Z' && 'a'+25=='z' && '0'+9=='9', "");
	using namespace Parser_PRIV;
	for(auto& vals: _from_char) {
		vals[0]=out_of_range();
		vals[1]=out_of_range();
		vals[2]=out_of_range();
	}
	unsigned int i=0;
	_from_char[static_cast<unsigned char>('.')][2]=TYPE_DOT;
	for(char c='A'; c<='Z'; c++) {
		_b64_to_char[randomize(i)]=c;
		_from_char[static_cast<unsigned char>(c)][2]=TYPE_ALPHA;
	}
	for(char c='0'; c<='9'; c++) {
		_b64_to_char[randomize(i)]=c;
		_from_char[static_cast<unsigned char>(c)][2]=TYPE_NUM;
	}
	_b64_to_char[randomize(i)]='-';
	_b64_to_char[randomize(i)]='+';
	for(char c='a'; c<='z'; c++) {
		_b64_to_char[randomize(i)]=c;
		_from_char[static_cast<unsigned char>(c)][2]=TYPE_ALPHA;
	}

	for(i=0; i<64; i++)
		_from_char[_b64_to_char[i]][0]=i;

	for(i=0; i<10; i++)
		_from_char[static_cast<unsigned char>("0123456789"[i])][1]=i;
	for(i=0; i<6; i++) {
		_from_char[static_cast<unsigned char>("abcdef"[i])][1]=i+10;
		_from_char[static_cast<unsigned char>("ABCDEF"[i])][1]=i+10;
	}
}
gapr::AsciiTables gapr::AsciiTables::_tables{};

unsigned short gapr::parse_port(const char* s, std::size_t n) {
	unsigned short p;
	auto [eptr, ec]=std::from_chars(s, s+n, p, 10);
	if(ec!=std::errc{} || eptr!=s+n)
		gapr::report("invalid PORT");
	if(p==0)
		gapr::report("PORT out of range");
	return p;
}
void gapr::parse_repo(const std::string& _str, std::string& user, std::string& host, unsigned short& port, std::string& group) {
	// XXX use string_view???
	std::string str;
	{
		std::size_t i=0;
		while(i<_str.size() && std::isspace(_str[i]))
			i++;
		std::size_t j=_str.size();
		while(j>0 && std::isspace(_str[j-1]))
			j--;
		for(std::size_t k=i+1; k+1<j; k++)
			if(std::isspace(_str[k]))
				gapr::report("invalid char");
		str=_str.substr(i, j-i);
	}
	auto slashi=str.find('/');
	if(slashi==std::string::npos) {
		if(str.empty())
			gapr::report("empty REPO");
		group=str;
	} else {
		if(slashi+1>=str.length())
			gapr::report("empty REPO");
		group=str.substr(slashi+1);
		auto ati=str.find('@');
		if(ati==std::string::npos) {
			if(slashi<=0)
				gapr::report("empty HOST:PORT");
			ati=0;
		} else {
			if(ati<=0)
				gapr::report("empty USER");
			user=str.substr(0, ati);
			if(ati+1>=slashi)
				return;
			ati++;
		}
		auto coloni=str.rfind(':', slashi);
		if(coloni==std::string::npos || coloni<ati) {
			gapr::report("wrong HOST:PORT");
		} else {
			if(ati>=coloni)
				gapr::report("empty HOST");
			if(coloni+1>=slashi)
				gapr::report("empty PORT");
			host=str.substr(ati, coloni-ati);
			port=gapr::parse_port(&str[coloni+1], slashi-coloni-1);
		}
	}
}

gapr::Parser_PRIV::ErrorStrs::ErrorStrs(): _strs{
	"",
		"not hex",
		"not dec",
		"not b64",
		"empty",
		"start with non-letter",
		"consecutive dots",
		"end with dot",
} { }
const gapr::Parser_PRIV::ErrorStrs gapr::Parser_PRIV::ErrorStrs::_error_strs;
