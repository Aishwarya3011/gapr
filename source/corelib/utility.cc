#include "gapr/utility.hh"

#include "gapr/detail/fixed-point.hh"
#include "gapr/detail/mini-float.hh"
#include "gapr/mt-safety.hh"
#include "gapr/fixes.hh"
#include "gapr/str-glue.hh"
#include "gapr/exception.hh"

#include <unordered_map>
#include <thread>
#include <mutex>
#include <fstream>
#include <chrono>
#include <cinttypes>
#include <array>
#include <charconv>
#include <filesystem>
#include <optional>
#include <clocale>

#include <limits.h>
#include <sys/stat.h>
#include <getopt.h>

#include "config.hh"

#ifdef _WIN64
#include <windows.h>
#include <userenv.h>
#else
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#endif

#ifdef ANDROID
#include <android/log.h>
#endif

#include <openssl/sha.h>

#include "libc-wrappers.hh"

namespace {
	enum class selfdir_state {
		null,
		installed,
		relocated,
		unknown
	};
	static std::pair<std::string, selfdir_state> get_selfdir_impl() {
		std::string _bindir;
#ifdef _WIN32
		char buffer[MAX_PATH+2];
		auto v=GetModuleFileName(0, buffer, MAX_PATH+1);
		buffer[MAX_PATH+1]=0;
		if(v==0) {
			_bindir="";
		} else {
			auto r=strlen(buffer)-1;
			while(buffer[r]!='\\' && r>0)
				r--;
			_bindir=std::string{buffer, r};
		}
#else
#ifdef __APPLE__
		//static QString appFileName;
		char path[1024];
		uint32_t size = sizeof(path);
		if(_NSGetExecutablePath(path, &size)==0) {
			for(size=0; path[size]!='\0'; size++);
			while(size>0 && path[size-1]!='/')
				size--;
			if(size>0)
				size--;
			_bindir=std::string{path, size};
		}
		else
			fprintf(stderr, "buffer too small; need size %u\n", size);
#else
		char link[PATH_MAX];
		auto r=readlink("/proc/self/exe", link, PATH_MAX);
		if(r==-1) {
			gapr::report("readlink(\"/proc/self/exe\"): ", strerror(errno));
		}
		if(r==PATH_MAX) {
			gapr::report("readlink(\"/proc/self/exe\"): path too long");
		}
		if(r==0) {
			gapr::report("readlink(\"/proc/self/exe\"): path 0 length");
		}
		r--;
		while(r>0 && link[r]!='/')
			r--;
		_bindir=std::string(link, r);
#endif
#endif
		gapr::print(1, "bindir: ", _bindir);
		if(_bindir==PACKAGE_BINDIR) {
			return {std::move(_bindir), selfdir_state::installed};
		} else {
			const char* s=PACKAGE_BINDIR;
			auto q=s+std::strlen(s);
			while(q>s && (q[-1]=='/'||q[-1]=='\\'))
				q--;
			auto p=q;
			while(p>s && (p[-1]!='/'&&p[-1]!='\\'))
				p--;
			std::size_t l=q-p;
			auto n=_bindir.size();
			if(n>=l && (n<=l || _bindir[n-l-1]=='/' || _bindir[n-l-1]=='\\') && !_bindir.compare(n-l, l, p, l)) {
				return {std::move(_bindir), selfdir_state::relocated};
			} else {
				return {std::move(_bindir), selfdir_state::unknown};
			}
		}
	}
	inline static std::pair<std::string_view, selfdir_state> get_selfdir() {
		static selfdir_state _state{selfdir_state::null};
		static std::string _dir;
		if(_state==selfdir_state::null)
			std::tie(_dir, _state)=get_selfdir_impl();
		return {_dir, _state};
	}
}

static std::string calc_relative_path(const char* a, const char* b) {
	size_t sepi=0;
	size_t i=0;
	while(a[i]==b[i]) {
		if(a[i]=='\0')
			break;
		if(a[i]=='/')
			sepi=i+1;
		i++;
	}
	if(a[i]==b[i])
		return "";
	std::string pth{"/../"};
	for(auto j=i; a[j]!='\0'; j++) {
		if(a[j]=='/')
			pth+="../";
	}
	pth+=&b[sepi];
	return pth;
}

template<typename Impl> static std::string get_dir_impl() {
	static constexpr std::string_view dir{Impl{}.dir};
	auto [path, state]=get_selfdir();
	std::string res{};
	switch(state) {
		case selfdir_state::installed:
			res.append(dir.data(), dir.size());
			break;
		case selfdir_state::relocated:
			{
				auto p2=calc_relative_path(PACKAGE_BINDIR, dir.data());
				res.reserve(path.size()+p2.size()+1);
				res+=path;
				res+=p2;
			}
			break;
		default:
			break;
	}
	return res;
}
template<typename Impl> inline static std::string_view get_dir() {
	static bool _ready{false};
	static std::string _dir;
	if(!_ready) {
		_dir=get_dir_impl<Impl>();
		_ready=true;
		gapr::print(1, " dir: ", Impl{}.dir, ": ", _dir);
	}
	return _dir;
}

std::string_view gapr::selfdir() {
	auto [path, state]=get_selfdir();
	return path;
}
std::string_view gapr::bindir() {
	struct Impl { const std::string_view dir{PACKAGE_BINDIR}; };
	return get_dir<Impl>();
}
std::string_view gapr::datadir() {
	struct Impl { const std::string_view dir{PACKAGE_DATADIR}; };
	return get_dir<Impl>();
}
std::string_view gapr::libdir() {
	struct Impl { const std::string_view dir{PACKAGE_LIBDIR}; };
	return get_dir<Impl>();
}
std::string_view gapr::libexecdir() {
	struct Impl { const std::string_view dir{PACKAGE_LIBEXECDIR}; };
	return get_dir<Impl>();
}
std::string_view gapr::localedir() {
	struct Impl { const std::string_view dir{PACKAGE_LOCALEDIR}; };
	return get_dir<Impl>();
}
std::string_view gapr::sysconfdir() {
	struct Impl { const std::string_view dir{PACKAGE_SYSCONFDIR}; };
	return get_dir<Impl>();
}

static bool homedir_ready{false};
static std::filesystem::path homedir{};
const std::filesystem::path& gapr::get_homedir() {
	if(!homedir_ready) {
#ifdef _WIN32
		auto proc=::GetCurrentProcess();
		HANDLE token;
		if(::OpenProcessToken(proc, TOKEN_QUERY, &token)) {
			DWORD size=0;
			if(!::GetUserProfileDirectory(token, nullptr, &size) && size!=0) {
				auto buf=std::make_unique<char[]>(size);
				if(::GetUserProfileDirectory(token, &buf[0], &size)) {
					homedir=std::filesystem::path{&buf[0]};
					homedir_ready=true;
				}
			}
			::CloseHandle(token);
		}
		if(!homedir_ready) {
			auto up=getenv("USERPROFILE");
			if(up) {
				homedir=std::filesystem::path{up};
				homedir_ready=true;
			}
		}
#else
		homedir=getenv("HOME");
		homedir_ready=true;
#endif
		if(homedir_ready) {
			gapr::print("homedir: ", homedir);
		} else {
			homedir=std::filesystem::path{};
			homedir_ready=true;
		}
	}
	return homedir;
}

static std::optional<std::filesystem::path> cachedir{};
const std::filesystem::path& gapr::get_cachedir() {
	if(!cachedir) {
		auto xdgcache=::getenv("XDG_CACHE_HOME");
		auto d=xdgcache ? std::filesystem::path{xdgcache} : get_homedir()/".cache";
		d/=PACKAGE_NAME;
		if(!std::filesystem::exists(d)) {
			std::filesystem::create_directories(d);
			gapr::file_stream tagf{d/"CACHEDIR.TAG", "wb"};
		}
		cachedir.emplace(std::move(d));
	}
	return *cachedir;
}

std::string gapr::calculate_sha256(std::string_view data) {
	std::array<unsigned char, SHA256_DIGEST_LENGTH> md_buf;
	::SHA256(reinterpret_cast<const unsigned char*>(&data[0]), data.size(), &md_buf[0]);
	std::string res;
	res.resize(md_buf.size()*2, '\x00');
	dump_binary(&res[0], &md_buf[0], md_buf.size());
	return res;
}
std::filesystem::path gapr::get_cachepath(std::string_view str_tag) {
	auto tag_sha=gapr::calculate_sha256(str_tag);
	auto path=get_cachedir();
	path/=std::string_view{tag_sha}.substr(0, 2);
	if(!std::filesystem::exists(path)) {
		std::filesystem::create_directories(path);
	}
	path/=tag_sha;
	return path;
}

static std::unordered_map<std::thread::id, int> tid2color{};
static const char* colorstr[]={
	"[\e[31m",
	"[\e[32m",
	"[\e[33m",
	"[\e[34m",
	"[\e[35m",
	"[\e[36m",
};

const char* gapr::_get_thread_color() {
	auto id=std::this_thread::get_id();
	auto i=tid2color.find(id);
	if(i==tid2color.end()) {
		auto idx=tid2color.size();
		if(idx>=sizeof(colorstr)/sizeof(colorstr[0]))
			idx=sizeof(colorstr)/sizeof(colorstr[0])-1;
		tid2color[id]=idx;
		return colorstr[idx];
	} else {
		return colorstr[i->second];
	}
}
const char* gapr::_restore_color() {
	return "\e[0m]: ";
}

namespace std {
inline std::ostream& operator<<(std::ostream& fs, const std::chrono::microseconds& dur) {
	auto v=dur.count();
	/*
	fs.width(2);
	fs.fill('0');
	fs<<v/(std::chrono::microseconds::rep(1000000)*3600)<<':';
	v=v%(std::chrono::microseconds::rep(1000000)*3600);
	fs.width(2);
	fs.fill('0');
	fs<<v/(1000000*60)<<':';
	v=v%(1000000*60);
	fs.width(2);
	fs.fill('0');
	*/
	fs<<v/1000000<<'.';
	v=v%1000000;
	fs.width(6);
	fs.fill('0');
	fs<<v;
	return fs;
}
}
static std::chrono::microseconds dur0{};
static std::mutex _print_mtx;
void gapr::_do_print(const std::string& s) {
	static int debug_msg{-1};
	while(debug_msg<0) {
		std::lock_guard<std::mutex> lck{_print_mtx};
		auto p=getenv("GAPR_DEBUG");
		debug_msg=p?1:0;
	}
	if(!debug_msg)
		return;

	auto dur=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch());
	std::ostringstream oss;

	std::lock_guard<std::mutex> lck{_print_mtx};
	auto c0=gapr::_get_thread_color();
	auto c1=gapr::_restore_color();
	oss<<c0<<dur-(dur0==std::chrono::microseconds{}?dur:dur0)<<c1;
	dur0=dur;
#ifdef ANDROID
	__android_log_print(ANDROID_LOG_VERBOSE, PACKAGE_NAME, "%s%s\n", oss.str().c_str(), s.c_str());
#else
	std::cerr<<oss.str()<<s<<'\n';
#endif
}

void gapr::rename_with_backup(const char* oldpath, const char* newpath) {
	struct stat buf;
	auto r=stat(newpath, &buf);
	if(r!=-1) {
		auto lt=localtime(&buf.st_mtime);
#ifdef _MSC_VER
		constexpr auto PATH_MAX=MAX_PATH;
#endif
		char fn[PATH_MAX];
		auto n=snprintf(fn, PATH_MAX, "%s@%d-%02d-%02d@%02d%02d%02d",
				newpath, lt->tm_year+1900, lt->tm_mon+1, lt->tm_mday,
				lt->tm_hour, lt->tm_min, lt->tm_sec);
		if(n>=PATH_MAX)
			fn[PATH_MAX-1]='\0';
		if(rename(newpath, fn)!=0)
			gapr::report("rename(): ", strerror(errno));
	}
	if(oldpath) {
		if(rename(oldpath, newpath)!=0)
			gapr::report("rename(): ", strerror(errno));
	}
}
static const char* hexchars="0123456789abcdef";
void gapr::dump_binary(char* buf, const unsigned char* vals, int n) {
	for(int i=0; i<n; i++) {
		buf[2*i+0]=hexchars[(vals[i]>>4)&0x0F];
		buf[2*i+1]=hexchars[vals[i]&0x0F];
	}
}
bool gapr::parse_binary(unsigned char* vals, const char* buf, int n) {
	for(int i=0; i<n; i++) {
		char v[2];
		for(int j=0; j<2; j++) {
			char a=buf[2*i+j];
			if(a>='0' && a<='9') {
				v[j]=a-'0';
			} else if(a>='a' && a<='f') {
				v[j]=10+a-'a';
			} else if(a>='A' && a<='F') {
				v[j]=10+a-'A';
			} else {
				return false;
			}
		}
		vals[i]=(v[0]<<4)+v[1];
	}
	return true;
}
const char* gapr::check_name(const char* ptr, int len) {
	if(len<1)
		return "empty";
	int prevdot=-1;
	switch(len) {
		default:
			for(int i=1; i<len-1; i++) {
				auto c=static_cast<unsigned char>(ptr[i]);
				if(std::isalnum(c)) {
				} else if(c=='.') {
					if(prevdot+1==i)
						return "consecutive dots";
					prevdot=i;
				} else {
					return "char not in [.0-9A-Za-z]";
				}
			}
		case 2:
			if(!std::isalnum(static_cast<unsigned char>(ptr[len-1])))
				return "last char not in [0-9A-Za-z]";
		case 1:
			if(!std::isalpha(static_cast<unsigned char>(ptr[0])))
				return "first char not in [A-Za-z]";

			break;
	}
	return nullptr;
}

std::unordered_map<std::string, std::string> gapr::load_config(const std::filesystem::path& cfg_file) {
	std::ifstream fs{cfg_file};
	if(!fs)
		gapr::report("Failed to open: ", cfg_file);
	std::string line;
	std::string pfx{};
	std::unordered_map<std::string, std::string> cfg{};
	while(std::getline(fs, line)) {
		if(line.empty())
			continue;
		if(line[0]=='#')
			continue;
		if(line[0]=='[') {
			auto i=line.find(']');
			if(i==std::string::npos)
				gapr::report("Failed to parse: ", line);
			pfx=line.substr(1, i-1);
		} else {
			auto i=line.find('=');
			if(i==std::string::npos)
				gapr::report("Failed to parse: ", line);
			auto key=pfx;
			if(!pfx.empty()) key+='.';
			key+=line.substr(0, i);
			cfg[key]=line.substr(i+1);
		}
	}
	if(!fs.eof())
		gapr::report("Error while reading config file.");
	return cfg;
}
std::unordered_map<std::string, std::string> gapr::load_config(const char* cfg_file) {
	return load_config(std::filesystem::path{cfg_file});
}

#ifdef _MSC_VER
template<typename T> inline static bool S_ISREG(T m) {
	return (m&_S_IFMT)==_S_IFREG;
}
template<typename T> inline static bool S_ISDIR(T m) {
	return (m&_S_IFMT)==_S_IFDIR;
}
#endif
bool gapr::test_file(char code, const char* fn) {
	struct stat buf;
	if(stat(fn, &buf)==-1) {
		if(errno!=ENOENT)
			gapr::report("stat error");
		return false;
	}

	switch(code) {
		case 'e':
			return true;
		case 'f':
			return S_ISREG(buf.st_mode);
		case 'd':
			return S_ISDIR(buf.st_mode);
		default:
			gapr::report("unknown code");
	}
	return false;
}

namespace gapr_test {
	int chk_fixed_point() {
		using fp=gapr::fixed_point<int, 5, 5+8-1, 2>;
		using Case=std::pair<int, double>;
		auto tests={
			Case{0*32, 0.0/4},
				Case{1*32, 1.0/4},
				Case{126*32, 126.0/4},
				Case{127*32, +INFINITY},
				Case{128*32, NAN},
				Case{129*32, -INFINITY},
				Case{130*32, -126.0/4},
				Case{254*32, -2.0/4},
				Case{255*32, -1.0/4},
				Case{127*32, 128.0/4},
				Case{129*32, -127.0/4},
				Case{129*32, -128.0/4},
		};
		auto feq=[](double a, double b) -> bool {
			switch(std::fpclassify(a)) {
				case FP_INFINITE:
					if(std::fpclassify(b)!=FP_INFINITE)
						return false;
					if(std::signbit(a)!=std::signbit(b))
						return false;
					return true;
				case FP_NAN:
					if(std::fpclassify(b)!=FP_NAN)
						return false;
					return true;
				case FP_ZERO:
					if(std::fpclassify(b)!=FP_ZERO)
						return false;
					return true;
				default:
					break;
			}
			return a==b;
		};
		int r=0;
		for(auto [i, f]: tests) {
			auto f0=fp::get(i);
			if(!feq(f0, f))
				r++;
			auto i0=fp::enc(f);
			if(i0!=i)
				r++;
			gapr::print(i, ":", f0, ' ', i0, ":", f);
		}
		return r;
	}

	int chk_mini_float() {
		{
			auto wb=[](int64_t v) {
				for(int i=63; i>=0; i--) {
					if(v&(int64_t{1}<<i)) {
						std::cerr<<'1';
					} else {
						std::cerr<<'0';
					}
				}
				std::cerr<<'\n';
			};
#define TEST_FLOAT_TP gapr::MiniFloat<int64_t, 3, 34, 23>
#define TEST_FLOAT(P) \
			do { \
				int64_t v=P; \
				int64_t v2=~P; \
				std::cerr<<#P<<": "<<TEST_FLOAT_TP::get(v)<<'\n'; \
				wb(v); \
				TEST_FLOAT_TP::set(v2, TEST_FLOAT_TP::get(v)); \
				wb(v2); \
			} while(0)
			TEST_FLOAT(0b0'00000000'00000000000000000000001'001);
			TEST_FLOAT(0b0'00000000'11111111111111111111111'001);
			TEST_FLOAT(0b0'00000001'00000000000000000000000'001);
			TEST_FLOAT(0b0'11111110'11111111111111111111111'001);
			TEST_FLOAT(0b0'01111110'11111111111111111111111'001);
			TEST_FLOAT(0b0'01111111'00000000000000000000000'001);
			TEST_FLOAT(0b0'01111111'00000000000000000000001'001);
			TEST_FLOAT(0b1'10000000'00000000000000000000000'101);
			TEST_FLOAT(0b0'00000000'00000000000000000000000'101);
			TEST_FLOAT(0b1'00000000'00000000000000000000000'101);
			TEST_FLOAT(0b0'11111111'00000000000000000000000'101);
			TEST_FLOAT(0b1'11111111'00000000000000000000000'101);
			TEST_FLOAT(0b0'10000000'10010010000111111011011'101);
			TEST_FLOAT(0b0'01111101'01010101010101010101011'101);
			TEST_FLOAT(0b0'11111111'10000000000000000000001'101);
			TEST_FLOAT(0b0'11111111'00000000000000000000001'101);

#undef TEST_FLOAT_TP
#define TEST_FLOAT_TP gapr::MiniFloat<int64_t, 8, 23, 10>

			TEST_FLOAT(0b0'00000'0000000001'00000000);
			TEST_FLOAT(0b0'00000'1111111111'00000000);
			TEST_FLOAT(0b0'00001'0000000000'00000000);
			TEST_FLOAT(0b0'11110'1111111111'00000000);
			TEST_FLOAT(0b0'01110'1111111111'00000000);
			TEST_FLOAT(0b0'01111'0000000000'00000000);
			TEST_FLOAT(0b0'01111'0000000001'00000000);
			TEST_FLOAT(0b1'10000'0000000000'00000000);
			TEST_FLOAT(0b0'00000'0000000000'00000000);
			TEST_FLOAT(0b1'00000'0000000000'00000000);
			TEST_FLOAT(0b0'11111'0000000000'00000000);
			TEST_FLOAT(0b1'11111'0000000000'00000000);
			TEST_FLOAT(0b0'01101'0101010101'00000000);
			auto f=fopen("/tmp/gapr-fp.txt", "wb");

			constexpr int nb=11;
			constexpr int ns=6;
			constexpr int bias=8;
			////
			/////
			for(int64_t i=0; i<int64_t{1}<<(nb+4); i+=(1<<4)) {
				auto r=gapr::mini_float_nn<int64_t, 4, nb-1+4, ns, bias>::get(i);
				int64_t ii{0};
				gapr::mini_float_nn<int64_t, 4, nb-1+4, ns, bias>::set(ii, r);
				auto r2=gapr::mini_float_nn<int64_t, 4, nb-1+4, ns, bias>::get(ii);
				fprintf(f, "a %04" PRIx64 ": %lf\nb %04" PRIx64 ": %lf\n", i, r, ii, r2);
			}
			for(int64_t i=0; i<int64_t{1}<<(nb+4); i+=(1<<4)) {
				auto r=gapr::fixed_point<int64_t, 4, nb-1+4, 9>::get(i);
				int64_t ii{0};
				gapr::fixed_point<int64_t, 4, nb-1+4, 9>::set(ii, r);
				auto r2=gapr::fixed_point<int64_t, 4, nb-1+4, 9>::get(ii);
				fprintf(f, "a %04" PRIx64 ": %lf\nb %04" PRIx64 ": %lf\n", i, r, ii, r2);
			}
			//
			//
			fclose(f);


		}
		return 0;
	}
}

template <typename C>
inline std::ostream& operator<<(std::ostream& fs, const std::chrono::time_point<C>& tp) {
	auto tt=std::chrono::system_clock::to_time_t(tp);
	char buf[64];
	auto r=strftime(buf, 64, "%Y-%m-%d %H:%M:%S %z", localtime(&tt));
	fs.write(buf, r);
	return fs;
}

unsigned int nproc() noexcept {
#ifdef _WIN64
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	return sysinfo.dwNumberOfProcessors;
#else
#ifdef __APPLE__
	int name[2];
	int numCPU;
	size_t len=sizeof(numCPU);
	name[0]=CTL_HW;
	name[1]=HW_NCPU;
	sysctl(name, 2, &numCPU, &len, nullptr, 0);
	return numCPU;
#else
	return sysconf(_SC_NPROCESSORS_ONLN);
#endif
#endif
}

namespace gapr {
	std::mutex MT_SAFETY_LOCKS::tmbuf{};
}


void gapr::fix_console() {

#ifdef _WIN64
	if(std::getenv("MSYSTEM")==nullptr && AttachConsole(ATTACH_PARENT_PROCESS)) {
		std::freopen("CONOUT$", "w", stdout);
		std::freopen("CONOUT$", "w", stderr);
		std::freopen("CONIN$", "r", stdin);
		std::setvbuf(stderr, NULL, _IONBF, 0);
	}
#endif

}

void gapr::fix_relocate() {

#ifdef __APPLE__
	auto [dir, st]=get_selfdir();
	switch(st) {
		default:
			break;
		case selfdir_state::relocated:
			{
				std::string dir{libdir()};
				dir+="/gdk-pixbuf-loaders.cache";
				::setenv("GDK_PIXBUF_MODULE_FILE", dir.c_str(), 0);
				dir=libdir();
				dir+="/gtk-3.0/3.0.0/immodules.cache";
				::setenv("GTK_IM_MODULE_FILE", dir.c_str(), 0);
				dir=libdir();
				dir+="/gio-modules";
				::setenv("GIO_MODULE_DIR", dir.c_str(), 0);
				dir=datadir();
				::setenv("XDG_DATA_DIRS", dir.c_str(), 0);
				dir+="/locale";
				::setenv("LIBINTL_RELOCATE", dir.c_str(), 0);
				dir=sysconfdir();
				::setenv("XDG_CONFIG_DIRS", dir.c_str(), 0);
				//XXX GSETTINGS_SCHEMA_DIR
			} break;
	}
#endif

}

std::string_view gapr::to_string_lex(std::array<char,32>& buf, unsigned long v) {
	auto [ptr, ec]=std::to_chars(buf.data()+1, buf.data()+buf.size()-1, v);
	if(ec!=std::errc{})
		throw std::system_error{std::make_error_code(ec)};
	*ptr='\x00';
	std::size_t len=ptr-buf.data();
	buf[0]='A'+(len-2);
	return {buf.data(), len};
}

std::string gapr::to_url_if_path(std::string_view path) {
	std::filesystem::path p{path};
	std::error_code ec;
	if(auto r=exists(p, ec); ec || !r)
		return {};
	if(!p.is_absolute()) {
		p=absolute(p, ec);
		if(ec)
			throw std::system_error{ec};
	}
	// XXX win32 .native() -> wchar_t
	return "file://"+p.string();
	std::setlocale(LC_ALL, "");
}

static void fix_flatpak_sigint() {
	gapr::file_stream tty{"/dev/tty", "rb"};
	if(!tty)
		return;
	auto fd=::fileno(tty);
	if(fd==-1)
		return;
#if !defined(__WIN32__)
	auto pid=::getpid();
	if(::tcsetpgrp(fd, pid)==-1)
		fprintf(stderr, "failed to tcsetpgrp\n");
	if(::setpgid(pid, pid)==-1)
		fprintf(stderr, "failed to setpgid\n");
#endif
}

gapr::cli_helper::cli_helper() {
	std::setlocale(LC_ALL, "");
#ifdef _WIN32
	/*! regex bug in mingw: strxfrm error return not handled */
	std::setlocale(LC_COLLATE, "C");
#endif

	if(::getenv("FLATPAK_ID")!=nullptr)
		fix_flatpak_sigint();
}
gapr::cli_helper::~cli_helper() {
}

void gapr::cli_helper::report_unknown_opt(int argc, char* argv[]) {
	if(optopt!=0) {
		gapr::str_glue err{"unrecognized option `-", (char)optopt, '\''};
		throw gapr::reported_error{err.str()};
	}
	gapr::str_glue err{"unrecognized option `", argv[optind-1], '\''};
	throw gapr::reported_error{err.str()};
}

void gapr::cli_helper::report_missing_arg(int argc, char* argv[]) {
	gapr::str_glue err{"option `", argv[optind-1], "' requires an argument"};
	throw gapr::reported_error{err.str()};
}

void gapr::cli_helper::report_unmatched_opt(int argc, char* argv[]) {
	assert(0);
	throw gapr::reported_error{"unknown option"};
}

