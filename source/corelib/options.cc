#include "gapr/options.hh"

#include "gapr/utility.hh"

//#include "misc.h"

//#include <QTextStream>
//#include <QFile>
//#include <QDir>
//#include <fstream>

//bool changed;
//QMap<std::string, std::string> data;
gapr::Options::Options():
	changed{false}, data{}
{
}
gapr::Options::~Options() {
}

void gapr::Options::reset() {
	changed=false;
	data.clear();
}

bool gapr::Options::isChanged() const {
	return changed;
}
void gapr::Options::putLine(const std::string& line) {
	if(line.size()<=0)
		return;
	if(line[0]=='#')
		return;
	auto j=line.find('=');
	if(j==std::string::npos) {
		gapr::report("Wrong configuration line: ", line);
		return;
	}
	data[line.substr(0, j)]=line.substr(j+1);
}
std::vector<std::string> gapr::Options::getLines() const {
	std::vector<std::string> list;
	for(auto i=data.cbegin(); i!=data.cend(); i++) {
		std::string s=i->first+'='+i->second;
		list.push_back(s);
	}
	return list;
}

/*
bool gapr::Options::getXform(const std::string& key, std::array<double, 9>* x, std::array<double, 3>* p0) const {
	if(!getDouble(key+".0.0", &(*x)[0]))
		return false;
	if(!getDouble(key+".0.1", &(*x)[3]))
		return false;
	if(!getDouble(key+".0.2", &(*x)[6]))
		return false;
	if(!getDouble(key+".1.0", &(*x)[1]))
		return false;
	if(!getDouble(key+".1.1", &(*x)[4]))
		return false;
	if(!getDouble(key+".1.2", &(*x)[7]))
		return false;
	if(!getDouble(key+".2.0", &(*x)[2]))
		return false;
	if(!getDouble(key+".2.1", &(*x)[5]))
		return false;
	if(!getDouble(key+".2.2", &(*x)[8]))
		return false;
	if(!getDouble(key+".0.3", &(*p0)[0]))
		return false;
	if(!getDouble(key+".1.3", &(*p0)[1]))
		return false;
	if(!getDouble(key+".2.3", &(*p0)[2]))
		return false;
	return true;
}
void Options::setXform(const std::string& key, const std::array<double, 9>& x, const std::array<double, 3>& p0) {
	setDouble(key+".0.0", x[0]);
	setDouble(key+".0.1", x[3]);
	setDouble(key+".0.2", x[6]);
	setDouble(key+".1.0", x[1]);
	setDouble(key+".1.1", x[4]);
	setDouble(key+".1.2", x[7]);
	setDouble(key+".2.0", x[2]);
	setDouble(key+".2.1", x[5]);
	setDouble(key+".2.2", x[8]);
	setDouble(key+".0.3", p0[0]);
	setDouble(key+".1.3", p0[1]);
	setDouble(key+".2.3", p0[2]);
}
bool Options::getXyz(const std::string& key, double* x, double* y, double* z) const {
	if(!getDouble(key+".x", x))
		return false;
	if(!getDouble(key+".y", y))
		return false;
	if(!getDouble(key+".z", z))
		return false;
	return true;
}
bool Options::getXfunc(const std::string& key, double* x, double* y) const {
	if(!getDouble(key+".x", x))
		return false;
	if(!getDouble(key+".y", y))
		return false;
	return true;
}
void Options::setXfunc(const std::string& key, double x, double y) {
	setDouble(key+".x", x);
	setDouble(key+".y", y);
}
void Options::setXyz(const std::string& key, double x, double y, double z) {
	setDouble(key+".x", x);
	setDouble(key+".y", y);
	setDouble(key+".z", z);
}

bool Options::getXyzInt(const std::string& key, int* x, int* y, int* z) const {
	if(!getInt(key+".x", x))
		return false;
	if(!getInt(key+".y", y))
		return false;
	if(!getInt(key+".z", z))
		return false;
	return true;
}
void Options::setXyzInt(const std::string& key, int x, int y, int z) {
	setInt(key+".x", x);
	setInt(key+".y", y);
	setInt(key+".z", z);
}

bool Options::getInt(const std::string& key, int* v) const {
	int r=0;
	bool ok=false;
	auto i=data.find(key);
	if(i!=data.end())
		r=i->toInt(&ok);
	if(ok)
		*v=r;
	return ok;
}
void Options::setInt(const std::string& key, int v) {
	auto num=std::string::number(v);
	setString(key, num);
}
bool Options::getDouble(const std::string& key, double* v) const {
	double r=0;
	bool ok=false;
	auto i=data.find(key);
	if(i!=data.end())
		r=i->toDouble(&ok);
	if(ok)
		*v=r;
	return ok;
}
void Options::setDouble(const std::string& key, double v) {
	auto num=std::string::number(v, 'g', 13);
	setString(key, num);
}
const std::string Options::getString(const std::string& key) const {
	auto i=data.find(key);
	if(i!=data.end()) {
		return *i;
	} else {
		return std::string{};
	}
}
void Options::setString(const std::string& key, const std::string& v) {
	data[key]=v;
	changed=true;
}
const std::vector<std::string> Options::getStringList(const std::string& key) const {
	std::vector<std::string> lst;
	int i=0;
	auto templ=key+".%1";
	while(true) {
		auto keyi=templ.arg(i);
		auto iter=data.find(keyi);
		if(iter==data.end()) {
			break;
		}
		lst<<*iter;
		i++;
	}
	return lst;
}
void Options::setStringList(const std::string& key, const std::vector<std::string>& v) {
	auto templ=key+".%1";
	for(int i=0; i<v.size(); i++) {
		auto keyi=templ.arg(i);
		data[keyi]=v[i];
	}
	auto keyend=templ.arg(v.size());
	auto iter=data.find(keyend);
	if(iter!=data.end())
		data.erase(iter);
	changed=true;
}
const QList<int> Options::getIntList(const std::string& key) const {
	QList<int> lst;
	int i=0;
	auto templ=key+".%1";
	while(true) {
		auto keyi=templ.arg(i);
		int x;
		if(!getInt(keyi, &x)) {
			break;
		}
		lst<<x;
		i++;
	}
	return lst;
}
void Options::setIntList(const std::string& key, const QList<int>& v) {
	auto templ=key+".%1";
	for(int i=0; i<v.size(); i++) {
		auto keyi=templ.arg(i);
		setInt(keyi, v[i]);
	}
	auto keyend=templ.arg(v.size());
	auto iter=data.find(keyend);
	if(iter!=data.end())
		data.erase(iter);
	changed=true;
}
*/
void gapr::Options::removeKey(const std::string& key) {
	auto iter=data.find(key);
	if(iter!=data.end()) {
		data.erase(iter);
		changed=true;
	}
}

