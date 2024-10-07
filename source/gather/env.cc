#include "env.hh"

#include "gapr/utility.hh"
#include "gapr/parser.hh"
#include "gapr/mt-safety.hh"

#include <sstream>
#include <fstream>
#include <memory>
#include <algorithm>
#include <charconv>
#include <filesystem>
//#include <vector>
//#include <mutex>

#include <unistd.h>
#include <sys/stat.h>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include "config.hh"


#define PASSWD_SALT_SIZE 4
#define PASSWD_HASH_SIZE (gapr::pw_hash{}.size()-4)
#define PASSWD_TOTAL_SIZE (gapr::pw_hash{}.size())
#define PROJECT_SECRET_SIZE (gapr::pw_hash{}.size())

static constexpr std::string_view accounts_header{"# List of accounts:: name:tier:password-hash:gecos\n"};
static constexpr std::string_view projects_header{"# List of projects:: name:stage:secret:description\n"};
static constexpr std::string_view def_password{"alskdjfhg"};
constexpr static std::string_view http_srv_name{PACKAGE_NAME "/" PACKAGE_VERSION};


enum SaveFlags: unsigned int {
	SaveAccounts=1<<0,
	SaveProjects=1<<1,
	SaveAcls=1<<2,
	SaveFinish=1<<8,
};

struct gapr::gather_env::AclInfo {
	std::unordered_map<std::string, gapr::tier> name2tier;
	std::vector<std::pair<gapr::tier, gapr::tier>> tier2tier;
	bool empty() const noexcept {
		return name2tier.empty() && tier2tier.empty();
	}

	static std::unique_ptr<AclInfo> parse_acl_file(std::istream& str);
};

inline gapr::gather_env::Account::Account(std::string_view gecos):
	gecos{gecos}, order{-1} { }
inline gapr::gather_env::Project::Project(std::string_view desc):
	desc{desc}, acl_ver0{-1}, acl_ver1{-1}, order{-1} { }


bool gapr::account_reader::login(std::string_view password) const {
	assert(_ptr);
	auto& salt_hash=_ptr->salt_hash;
	/* set salt-and-hash to all-zero to reset password */
	if(password==def_password) {
		bool allzero=true;
		for(unsigned int i=0; i<PASSWD_TOTAL_SIZE; ++i) {
			if(salt_hash[i]!=0) {
				allzero=false;
				break;
			}
		}
		if(allzero)
			return true;
	}

	std::array<unsigned char, PASSWD_HASH_SIZE> hash;
	if(PKCS5_PBKDF2_HMAC(password.data(), password.size(), &salt_hash[0], PASSWD_SALT_SIZE, 2048, EVP_sha512(), hash.size(), &hash[0])!=1)
		gapr::report("Failed to calculate password hash.");
	if(std::memcmp(&hash[0], &salt_hash[PASSWD_SALT_SIZE], hash.size())==0)
		return true;
	return false;
}
gapr::account_reader::~account_reader() { }


gapr::account_writer::account_writer(gather_env& env, const std::string& name):
	_env{env}, _ptr{nullptr}, _lck{env._accounts_mtx}
{
	if(auto it=env._accounts.find(name); it!=env._accounts.end()) {
		_ptr=&it->second;
		{
			std::lock_guard lck{_env._save_mtx};
			_env._save_flags|=SaveAccounts;
		}
	}
}
gapr::account_writer::account_writer(gather_env& env, const std::string& name, const pw_hash& hash, std::string_view gecos):
	_env{env}, _ptr{nullptr}, _lck{env._accounts_mtx}
{
	auto [it, ins]=env._accounts.try_emplace(name, std::string{gecos});
	if(ins) {
		_ptr=&it->second;
		_ptr->tier=gapr::tier::locked;
		_ptr->salt_hash=hash;
		_ptr->order=env._accounts_misc.size()+env._accounts.size();
		++env._accounts_ver1;
		{
			std::lock_guard lck{_env._save_mtx};
			_env._save_flags|=SaveAccounts;
		}
	}
}
gapr::account_writer::~account_writer() {
	if(_ptr)
		_env._save_cv.notify_one();
}


void gapr::account_writer::tier(gapr::tier tier) {
	assert(_ptr);
#if 0
	if(_ptr->tier<tmin)
		gapr::report("Permission denied.");
#endif
	_ptr->tier=tier;
	++_env._accounts_ver1;
}

void gapr::account_writer::passwd(gapr::tier tmin, const pw_hash& hash) {
	assert(_ptr);
	if(_ptr->tier<tmin)
		gapr::report("Permission denied.");
	_ptr->salt_hash=hash;
	++_env._accounts_ver1;
}


gapr::project_reader::~project_reader() { }
std::string gapr::project_reader::secret() const {
	assert(_ptr);
	std::string ret;
	ret.resize(PROJECT_SECRET_SIZE*2);
	dump_binary(&ret[0], &_ptr->secret[0], PROJECT_SECRET_SIZE);
	return ret;
}


gapr::project_writer::project_writer(gather_env& env, const std::string& name):
	_env{env}, _ptr{nullptr}, _lck{env._projects_mtx}
{
	if(auto it=env._projects.find(name); it!=env._projects.end()) {
		_ptr=&it->second;
		{
			std::lock_guard lck{_env._save_mtx};
			_env._save_flags|=SaveProjects;
		}
	}
}
gapr::project_writer::project_writer(gather_env& env, const std::string& name, const pw_hash& hash, std::string_view desc):
	_env{env}, _ptr{nullptr}, _lck{env._projects_mtx}
{
	auto [it, ins]=env._projects.try_emplace(name, desc);
	if(ins) {
		_ptr=&it->second;
		_ptr->stage=gapr::stage::initial;
		_ptr->secret=hash;
		_ptr->order=env._projects_misc.size()+env._projects.size();
		++env._projects_ver1;
		{
			std::lock_guard lck{_env._save_mtx};
			_env._save_flags|=SaveProjects;
		}
	}
}
gapr::project_writer::~project_writer() {
	if(_ptr)
		_env._save_cv.notify_one();
}

void gapr::project_writer::stage(gapr::stage stage) {
	assert(_ptr);
	_ptr->stage=stage;
	++_env._projects_ver1;
}

void gapr::project_writer::desc(std::string_view desc) {
	if(_ptr->desc.empty()) {
		if(!desc.empty()) {
			_ptr->desc=desc;
			++_env._projects_ver1;
		}
	} else {
		_ptr->desc=desc;
		++_env._projects_ver1;
	}
}


gapr::gather_env::gather_env():
	_pkey{"./private.key"},
	_cert{"./certificate"},
	_accounts_list{"./accounts"},
	_projects_list{"./projects"},
	_acl_d{"./ACL"},
	_repo_d{"./REPO"},
	_data_d{"./DATA"},
	_state_d{"./STATE"},
	_http_serv{http_srv_name}
{
	auto path=gapr::datadir();
	if(!path.empty()) {
		_docroot=std::string{path}+"/" PACKAGE_TARNAME "/docroot";
	} else {
		path=gapr::selfdir();
		std::string fmf{path};
		fmf+="/../source-dir";
		std::ifstream ifs{fmf};
		std::string line;
		// XXX eof or err
		if(std::getline(ifs, line) && !line.empty()) {
			_docroot=std::move(line);
			_docroot+="/gather/docroot";
		} else {
			_docroot=path;
		}
	}

	auto add_perms=[this](gapr::stage stg, std::initializer_list<std::pair<gapr::delta_type, gapr::tier>> list) {
		for(auto [t, tier]: list) {
			auto j=tier_map_key(stg, t);
			auto [it, ins]=_tier_map.try_emplace(j, tier);
			assert(ins);
		}
	};
	add_perms(gapr::stage::initial,
			{
				{gapr::delta_type::add_edge_, gapr::tier::admin},
				{gapr::delta_type::add_prop_, gapr::tier::admin},
				{gapr::delta_type::chg_prop_, gapr::tier::admin},
				{gapr::delta_type::add_patch_, gapr::tier::admin},
				{gapr::delta_type::del_patch_, gapr::tier::admin},
				{gapr::delta_type::proofread_, gapr::tier::admin},
				{gapr::delta_type::reset_proofread_, gapr::tier::admin},
			});
	add_perms(gapr::stage::open,
			{
				{gapr::delta_type::add_edge_, gapr::tier::proofreader},
				{gapr::delta_type::add_prop_, gapr::tier::proofreader},
				{gapr::delta_type::chg_prop_, gapr::tier::annotator},
				{gapr::delta_type::add_patch_, gapr::tier::admin},
				{gapr::delta_type::del_patch_, gapr::tier::proofreader},
				{gapr::delta_type::proofread_, gapr::tier::proofreader},
				{gapr::delta_type::reset_proofread_, gapr::tier::admin},
			});
	add_perms(gapr::stage::guarded,
			{
				{gapr::delta_type::add_edge_, gapr::tier::annotator},
				{gapr::delta_type::add_prop_, gapr::tier::proofreader},
				{gapr::delta_type::chg_prop_, gapr::tier::proofreader},
				{gapr::delta_type::add_patch_, gapr::tier::admin},
				{gapr::delta_type::del_patch_, gapr::tier::annotator},
				{gapr::delta_type::proofread_, gapr::tier::proofreader},
				{gapr::delta_type::reset_proofread_, gapr::tier::admin},
			});
	add_perms(gapr::stage::closed,
			{
				{gapr::delta_type::add_edge_, gapr::tier::annotator},
				{gapr::delta_type::add_prop_, gapr::tier::annotator},
				{gapr::delta_type::chg_prop_, gapr::tier::annotator},
				{gapr::delta_type::add_patch_, gapr::tier::admin},
				{gapr::delta_type::del_patch_, gapr::tier::annotator},
				{gapr::delta_type::proofread_, gapr::tier::annotator},
				{gapr::delta_type::reset_proofread_, gapr::tier::admin},
			});
	add_perms(gapr::stage::frozen,
			{
				{gapr::delta_type::add_edge_, gapr::tier::admin},
				{gapr::delta_type::add_prop_, gapr::tier::admin},
				{gapr::delta_type::chg_prop_, gapr::tier::admin},
				{gapr::delta_type::add_patch_, gapr::tier::admin},
				{gapr::delta_type::del_patch_, gapr::tier::admin},
				{gapr::delta_type::proofread_, gapr::tier::admin},
				{gapr::delta_type::reset_proofread_, gapr::tier::admin},
			});
}
gapr::gather_env::~gather_env() { }

#if 0
std::string gapr::gather_env::project_acl(std::string_view project) const {
	std::ostringstream oss;
	oss<<_acl_d<<'/'<<project;
	return oss.str();
}
#endif
std::string gapr::gather_env::project_repo(std::string_view project) const {
	std::ostringstream oss;
	oss<<_repo_d<<'/'<<project<<".repo";
	return oss.str();
}
#if 0
std::string gapr::gather_env::project_state(std::string_view project) const {
	std::ostringstream oss;
	oss<<_repo_d<<'/'<<project<<".repo-state";
	return oss.str();
}
#endif
std::string gapr::gather_env::image_catalog(std::string_view project, bool check) const {
	std::ostringstream oss;
	oss<<_data_d<<'/'<<project<<".catalog";
	auto fn=oss.str();
	struct stat stbuf;
	if(!check || stat(fn.c_str(), &stbuf)!=-1)
		return fn;
	return {};
}
std::string gapr::gather_env::image_data(std::string_view project, std::string_view path, bool check) const {
	std::ostringstream oss;
	oss<<_data_d<<'/'<<project<<"/"<<path;
	auto fn=oss.str();
	struct stat stbuf;
	if(!check || stat(fn.c_str(), &stbuf)!=-1)
		return fn;
	return {};
}
std::string gapr::gather_env::session_state(std::string_view username, std::string_view project) const {
	std::ostringstream oss;
	oss<<_state_d<<'/'<<username<<'@'<<project<<".tmp";
	auto fn=oss.str();
	struct stat stbuf;
	if(stat(fn.c_str(), &stbuf)!=-1)
		return fn;
	return {};
}

void gapr::gather_env::prepare_hash(pw_hash& hash, std::string_view password) {
	if(password.size()<3)
		gapr::report("Password too short. (>=3)");

	if(RAND_bytes(&hash[0], PASSWD_SALT_SIZE)!=1)
		gapr::report("Failed to generate random salt.");
	if(PKCS5_PBKDF2_HMAC(password.data(), password.size(), &hash[0], PASSWD_SALT_SIZE, 2048, EVP_sha512(), PASSWD_HASH_SIZE, &hash[PASSWD_SALT_SIZE])!=1)
		gapr::report("Failed to calculate password hash.");
}
void gapr::gather_env::prepare_secret(pw_hash& hash, std::string_view password) {
	if(password.size()<3)
		gapr::report("Password too short. (>=3)");
	{
		std::time_t t;
		std::tm tmbuf;
		time(&t);
		gapr::localtime_mt(&t, &tmbuf);
		auto y=(tmbuf.tm_year)%100;
		auto m=tmbuf.tm_mon;
		auto d=tmbuf.tm_mday;
		hash[0]=(y/10)*16+y%10;
		hash[1]=m+1;
		hash[2]=(d/10)*16+d%10;
		if(RAND_bytes(&hash[3], PASSWD_SALT_SIZE-3)!=1)
			gapr::report("Failed to generate random salt.");
	}
	if(PKCS5_PBKDF2_HMAC(password.data(), password.size(), &hash[0], PASSWD_SALT_SIZE, 2048, EVP_sha512(), PROJECT_SECRET_SIZE-PASSWD_SALT_SIZE, &hash[PASSWD_SALT_SIZE])!=1)
		gapr::report("Failed to calculate hash.");
}

void gapr::gather_env::check_username(std::string_view name) {
	//XXX check user format
	auto [idx, res]=gapr::parse_name(name.data(), name.size());
	if(!res)
		gapr::report("Username invalid: ", res.error());
	if(idx!=name.size())
		gapr::report("Username contains invalid characters.");
}
void gapr::gather_env::check_project_name(std::string_view name) {
	auto err=check_name(name.data(), name.size());
	if(err)
		gapr::report("Illegal project name: ", err);
}

// CUR
void gapr::gather_env::init_accounts() {
	gapr::print("init accounts list");
	std::error_code ec;
	if(std::filesystem::create_directory(_state_d, ec), ec)
		gapr::report("Failed to create dir: ", _state_d, ec.message());
	{
		_accounts_misc.clear();
		auto& l=_accounts_misc.emplace_back(accounts_header, 0);
		l.second=_accounts_misc.size();
	}
	_accounts.clear();
	auto [it, ins]=_accounts.try_emplace("root", /*"root", */"Super Administrator");
	if(!ins)
		gapr::report("Failed to add root");
	auto ptr=&it->second;
	ptr->tier=gapr::tier::root;
	for(auto& b: ptr->salt_hash)
		b=0;
	ptr->order=_accounts.size()+_accounts_misc.size();
	_accounts_ver0=0;
	_accounts_ver1=1;
}

void gapr::gather_env::init_projects() {
	gapr::print("init projects list");
	std::error_code ec;
	if(std::filesystem::create_directory(_repo_d, ec), ec)
		gapr::report("Failed to create dir: ", _repo_d, ec.message());
	if(std::filesystem::create_directory(_acl_d, ec), ec)
		gapr::report("Failed to create dir: ", _acl_d, ec.message());
	if(std::filesystem::create_directory(_data_d, ec), ec)
		gapr::report("Failed to create dir: ", _data_d, ec.message());

	{
		auto& l=_projects_misc.emplace_back(projects_header, 0);
		l.second=_projects_misc.size();
	}
	_projects.clear();
	_projects_ver0=0;
	_projects_ver1=1;
}

void gapr::gather_env::load_accounts() {
	std::ifstream ifs{_accounts_list};
	if(!ifs)
		gapr::report("Failed to open passwd file.");
	std::unordered_map<std::string, Account> tmpitems;
	std::string line;
	while(std::getline(ifs, line)) {
		if(line.empty()) {
			auto& l=_accounts_misc.emplace_back(line, 0);
			l.second=_accounts_misc.size()+tmpitems.size();
			continue;
		}
		if(line[0]=='#') {
			auto& l=_accounts_misc.emplace_back(line, 0);
			l.second=_accounts_misc.size()+tmpitems.size();
			continue;
		}
		size_t coli[3];
		for(unsigned int i=0; i<3; ++i) {
			coli[i]=line.find(':', i==0?0:coli[i-1]+1);
			if(coli[i]==std::string::npos)
				gapr::report("Too few fields: ", line);
		}
		/////////////
		if(coli[2]-coli[1]-1!=(PASSWD_TOTAL_SIZE)*2)
			gapr::report("Failed to parse salt-and-hash: ", line);

		auto name=line.substr(0, coli[0]);
		auto [it, ins]=tmpitems.try_emplace(name, /*name, */std::string_view{});
		if(!ins)
			gapr::report("Duplicated name: ", name);
		auto ent=&it->second;
		if(!parse_binary(&ent->salt_hash[0], &line[coli[1]+1], PASSWD_SALT_SIZE+PASSWD_HASH_SIZE))
			gapr::report("Failed to parse passwd: ", line);
		ent->gecos=line.substr(coli[2]+1, line.size()-coli[2]-1);
		unsigned int tier;
		auto [eptr, ec]=std::from_chars(&line[coli[0]+1], &line[coli[1]], tier, 10);
		if(ec!=std::errc{} || eptr!=&line[coli[1]])
			gapr::report("Failed to parse tier: ", line);
		ent->tier=gapr::tier{tier};
		ent->order=tmpitems.size()+_accounts_misc.size();
	}
	if(!ifs.eof())
		gapr::report("Failed to read file.");

	std::swap(tmpitems, _accounts);
	_accounts_ver0=_accounts_ver1=0;
}

void gapr::gather_env::load_projects() {
	std::ifstream ifs{_projects_list};
	if(!ifs)
		gapr::report("Failed to open group file");
	std::unordered_map<std::string, Project> tmpitems{};
	std::string line{};
	auto fn=_repo_d+'/';
	auto fn_acl=_acl_d+'/';
	while(std::getline(ifs, line)) {
		if(line.empty()) {
			auto& l=_projects_misc.emplace_back(line, 0);
			l.second=_projects_misc.size()+tmpitems.size();
			continue;
		}
		if(line[0]=='#') {
			auto& l=_projects_misc.emplace_back(line, 0);
			l.second=_projects_misc.size()+tmpitems.size();
			continue;
		}
		size_t coli[3];
		for(unsigned int i=0; i<3; ++i) {
			coli[i]=line.find(':', i==0?0:coli[i-1]+1);
			if(coli[i]==std::string::npos)
				gapr::report("Too few fields: ", line);
		}
		if(coli[2]-coli[1]-1!=PROJECT_SECRET_SIZE*2)
			gapr::report("Failed to parse secret a: ", line);

		auto name=line.substr(0, coli[0]);
		auto [it, ins]=tmpitems.try_emplace(name, std::string_view{});
		if(!ins)
			gapr::report("Duplicated name: ", name);
		auto item=&it->second;
		if(!parse_binary(&item->secret[0], &line[coli[1]+1], PROJECT_SECRET_SIZE))
			gapr::report("Failed to parse secret b: ", line);
		item->desc=line.substr(coli[2]+1, line.size()-coli[2]-1);
		unsigned int stage;
		auto [eptr, ec]=std::from_chars(&line[coli[0]+1], &line[coli[1]], stage, 10);
		if(ec!=std::errc{} || eptr!=&line[coli[1]])
			gapr::report("Failed to parse tier: ", line);
		item->stage=gapr::stage{stage};
		item->order=tmpitems.size()+_projects_misc.size();

		struct stat stbuf;
		fn_acl.resize(_acl_d.size()+1);
		fn_acl+=name;
		if(stat(fn_acl.c_str(), &stbuf)!=-1) {
			std::ifstream ifs1{fn_acl};
			if(!ifs1)
				gapr::report("Failed to open file");
			item->acl=AclInfo::parse_acl_file(ifs1);
		}


		// XXX
#if 0
		uint64_t id=0;
		while(true) {
			if(std::snprintf(buf, PATH_MAX, "%s/z%010" PRIX64, &line[0], id)>=PATH_MAX)
				gapr::report("Path too long.");
			std::unique_ptr<gapr::Buffer> sbuf1{archive.fopen(buf)};
			if(!sbuf1)
				break;
			gapr::print("id: ", id);
			// XXX read...;


			id++;
		}
#endif
	}
	if(!ifs.eof())
		gapr::report("Failed to read workspace list.");

	std::swap(_projects, tmpitems);
	_projects_ver0=_projects_ver1=0;
}

void gapr::gather_env::save_accounts_list() const {
	int64_t prev_ver1{-1};
	std::vector<std::tuple<std::size_t, std::size_t, int>> lines;
	std::string lines_buf;
	{
		std::shared_lock rlock{_accounts_mtx};
		if(_accounts_ver0>=_accounts_ver1)
			return;
		prev_ver1=_accounts_ver1;
		lines.reserve(_accounts.size());
		std::ostringstream oss;
		std::size_t pos=0;
		char buf[(PASSWD_TOTAL_SIZE)*2];
		for(auto& [name, account]: _accounts) {
			dump_binary(buf, &account.salt_hash[0], PASSWD_TOTAL_SIZE);
			oss<<name<<':'<<static_cast<unsigned int>(account.tier)<<':';
			oss.write(buf, sizeof(buf));
			oss<<':'<<account.gecos;
			if(!oss)
				gapr::report("Failed to write line.");
			std::size_t pos2=oss.tellp();
			lines.emplace_back(pos, pos2, account.order);
			pos=pos2;
		}
		for(auto& [l, o]: _accounts_misc) {
			oss<<l;
			if(!oss)
				gapr::report("Failed to write line.");
			std::size_t pos2=oss.tellp();
			lines.emplace_back(pos, pos2, o);
			pos=pos2;
		}
		lines_buf=oss.str();
	}
	assert(prev_ver1>=0);

	gapr::print("Update passwd...");
	std::sort(lines.begin(), lines.end(), [](auto& a, auto& b) ->bool {
		return std::get<2>(a)<std::get<2>(b);
	});
	auto fnnew=_accounts_list+"@new";
	std::ofstream ofs{fnnew};
	if(!ofs)
		gapr::report("Failed to open passwd file.");
	if(!ofs)
		gapr::report("Failed to write line");
	for(auto& [i, j, k]: lines) {
		std::string_view l{&lines_buf[i], j-i};
		ofs<<l<<'\n';
		if(!ofs)
			gapr::report("Failed to write line.");
	}
	ofs.close();
	if(!ofs)
		gapr::report("Failed to close file.");
	rename_with_backup(&fnnew[0], &_accounts_list[0]);
	gapr::print("Update passwd... DONE");
	std::unique_lock wlock{_accounts_mtx};
	if(prev_ver1>_accounts_ver0)
		_accounts_ver0=prev_ver1;
}

void gapr::gather_env::save_projects_list() const {
	int64_t prev_ver1{-1};
	std::vector<std::tuple<std::size_t, std::size_t, int>> lines;
	std::string lines_buf;
	{
		std::shared_lock rlock{_projects_mtx};
		if(_projects_ver0>=_projects_ver1)
			return;
		prev_ver1=_projects_ver1;
		lines.reserve(_projects.size());
		std::ostringstream oss;
		std::size_t pos{0};
		char buf[PROJECT_SECRET_SIZE*2];
		for(auto& [name, project]: _projects) {
			dump_binary(buf, &project.secret[0], PROJECT_SECRET_SIZE);
			oss<<name<<':'<<static_cast<unsigned int>(project.stage)<<':';
			oss.write(buf, sizeof(buf));
			oss<<':'<<project.desc;
			std::size_t pos2=oss.tellp();
			lines.emplace_back(pos, pos2, project.order);
			pos=pos2;
		}
		for(auto& [l, o]: _projects_misc) {
			oss<<l;
			std::size_t pos2=oss.tellp();
			lines.emplace_back(pos, pos2, o);
			pos=pos2;
		}
		lines_buf=oss.str();
	}
	assert(prev_ver1>=0);

	gapr::print("Update group...");
	std::sort(lines.begin(), lines.end(), [](auto& a, auto& b) ->bool {
		return std::get<2>(a)<std::get<2>(b);
	});

	auto fnnew=_projects_list+"@new";
	std::ofstream ofs{fnnew};
	if(!ofs)
		gapr::report("Failed to open file: ", _projects_list);
	if(!ofs)
		gapr::report("Failed to write line");
	for(auto& [i, j, k]: lines) {
		std::string_view l{&lines_buf[i], j-i};
		ofs<<l<<'\n';
		if(!ofs)
			gapr::report("Failed to write line");
	}
	ofs.close();
	if(!ofs)
		gapr::report("Failed to close file");
	rename_with_backup(fnnew.c_str(), _projects_list.c_str());
	gapr::print("Update group... DONE");

	std::unique_lock wlock{_projects_mtx};
	if(prev_ver1>_projects_ver0)
		_projects_ver0=prev_ver1;
}

void gapr::gather_env::save_acl_files() const {
	struct Acl {
		std::string name;
		std::vector<std::string> acl;
		int64_t prev_ver1;
	};
	std::vector<Acl> acls{};

	{
		std::shared_lock rlock{_projects_mtx};
		for(auto& p: _projects) {
			auto ptr=&p.second;
			if(ptr->acl_ver0<ptr->acl_ver1) {
				acls.emplace_back(Acl{p.first, {}, ptr->acl_ver1});
#if XXX
				auto& acl1=acls.back().acl;
				for(auto& l: ptr->acl)
					acl1.push_back(l);
#endif
			}
		}
		if(acls.empty())
			return;
	}

	gapr::print("Update acl...");
	auto fn=_acl_d+'/';
	for(auto& p: acls) {
		fn.resize(_acl_d.size()+1);
		fn+=p.name;
		if(!p.acl.empty()) {
			std::sort(p.acl.begin(), p.acl.end());
			auto fnnew=fn+"@new";
			std::ofstream ofs{fnnew};
			if(!ofs)
				gapr::report("Failed to open file");
			for(auto& l: p.acl) {
				ofs<<l<<'\n';
				if(!ofs)
					gapr::report("Failed to write file");
			}
			ofs.close();
			if(!ofs)
				gapr::report("Failed to close file");
			rename_with_backup(fnnew.c_str(), fn.c_str());
		} else {
			rename_with_backup(nullptr, fn.c_str());
		}
	}
	gapr::print("Update acl... DONE");

	std::unique_lock wlock{_projects_mtx};
	for(auto& p: acls) {
		auto i=_projects.find(p.name);
		if(i==_projects.end())
			continue;
		auto& ver0=i->second.acl_ver0;
		if(p.prev_ver1>ver0)
			ver0=p.prev_ver1;
	}

	//XXX save commit
	// XXX shared_ptr to Workspace?
}

gapr::tier gapr::gather_env::get_acl(const AclInfo& acl, const std::string& who, gapr::tier tier) {
	auto it=acl.name2tier.find(who);
	if(it!=acl.name2tier.end())
		return it->second;
	for(auto [t1, t2]: acl.tier2tier) {
		if(tier<=t1)
			return t2;
	}
	return gapr::tier::root;
}

#ifdef _MSC_VER
template<typename T> inline static bool S_ISREG(T m) {
	return (m&_S_IFMT)==_S_IFREG;
}
template<typename T> inline static bool S_ISDIR(T m) {
	return (m&_S_IFMT)==_S_IFDIR;
}
#endif

void gapr::gather_env::prepare(std::string_view root) {
	std::error_code ec;
	if(std::filesystem::current_path({root.begin(), root.end()}, ec), ec)
		throw gapr::CliErrorMsg{"chdir(`", root, "'): ", ec.message()};

		// XXX Check dir
		// XXX load config
	// XXX move these to Passwd.ctor
	// and report with cli_err..., like in import-pr4m
	struct stat buf;
	if(stat(_accounts_list.c_str(), &buf)==-1) {
		if(errno!=ENOENT)
			gapr::report("passwd file error");
		init_accounts();
	} else {
		if(!S_ISREG(buf.st_mode))
			gapr::report("passwd file error");
		load_accounts();
	}
	if(stat(_projects_list.c_str(), &buf)==-1) {
		if(errno!=ENOENT)
			gapr::report("group file error");
		//if(stat("./repository", &buf)!=-1)
		//gapr::report("repository dir error");
		if(errno!=ENOENT)
			gapr::report("repository dir error");
		init_projects();
	} else {
		if(!S_ISREG(buf.st_mode))
			gapr::report("group file error");
		//if(stat("./repository", &buf)==-1)
		//gapr::report("repository dir error");
		//if(!S_ISDIR(buf.st_mode))
		//gapr::report("repository dir error");
		load_projects();
	}
}

std::vector<std::string> gapr::gather_env::projects() const {
	std::vector<std::string> res;
	std::shared_lock rlock{_projects_mtx};
	for(auto& [key, val]: _projects) {
		res.push_back(key);
	}
	return res;
}

static const char* base6x_chars="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+/-.";
std::string gapr::gather_env::random_token(std::string_view secret) {
	constexpr static std::size_t salt_size=8;
	constexpr static std::size_t token_size=36;
	unsigned char buf[token_size]={'\0'};
	if(RAND_bytes(&buf[0], salt_size)!=1)
		gapr::report("Failed to generate random salt.");

	if(PKCS5_PBKDF2_HMAC(secret.data(), secret.size(), &buf[0], salt_size, 2048, EVP_sha512(), token_size-salt_size, &buf[salt_size])!=1)
		gapr::report("Failed to calculate password hash.");

	std::string str;
	str.reserve(token_size*8/6);
	unsigned int nbits=0;
	unsigned int v{0};
	for(unsigned int i=0; i<sizeof(buf); i++) {
		unsigned int vv{buf[i]};
		v=(vv<<nbits)|v;
		nbits+=8;
		while(nbits>=6) {
			str.push_back(base6x_chars[v&0x3f]);
			v=v>>6;
			nbits-=6;
		}
	}
	if(nbits>0) {
		str.push_back(base6x_chars[v&0x3f]);
	}
	return str;
}

std::size_t gapr::gather_env::generate_etag(std::array<char, 128>& buf, std::string_view path) {
	std::error_code ec;
	auto last_write=std::filesystem::last_write_time({path.begin(), path.end()}, ec);
	if(ec)
		return 0;
	auto filets=std::chrono::duration_cast<std::chrono::milliseconds>(last_write.time_since_epoch()).count();
	buf[0]='"';
	auto [ptr, ec2]=std::to_chars(&buf[1], &buf[buf.size()-1], filets);
	if(ec2!=std::errc{})
		return 0;
	*ptr++='"';
	return ptr-&buf[0];
};

void gapr::gather_env::save_configs_end() const {
	{
		std::lock_guard lck{_save_mtx};
		_save_flags|=SaveFinish;
	}
	_save_cv.notify_one();
}
bool gapr::gather_env::save_configs() const {
	unsigned int flags;
	{
		std::unique_lock lck{_save_mtx};
		while(true) {
			flags=_save_flags;
			if(flags&SaveFinish) {
				_save_flags=SaveFinish;
				lck.unlock();
				gapr::print("save configs final");
				save_accounts_list();
				save_projects_list();
				save_acl_files();
				return false;
			}
			if(flags) {
				_save_flags=0;
				break;
			}
			_save_cv.wait(lck);
		}
	}

	if(flags&SaveAccounts)
		save_accounts_list();
	if(flags&SaveProjects)
		save_projects_list();
	if(flags&SaveAcls)
		save_acl_files();
	return true;
}

std::unique_ptr<gapr::gather_env::AclInfo> gapr::gather_env::AclInfo::parse_acl_file(std::istream& str) {
	AclInfo acl;
	std::string line;
	auto parse_t2=[](std::string_view l) ->gapr::tier {
		if(l.empty())
			return gapr::tier::root;
		if(l[0]!=':')
			throw std::runtime_error{"invalid sep"};
		l.remove_prefix(1);
		unsigned int t;
		auto [eptr, ec]=std::from_chars(l.data(), l.data()+l.size(), t, 10);
		if(ec!=std::errc{})
			throw std::system_error(std::make_error_code(ec));
		if(eptr!=l.data()+l.size())
			throw std::runtime_error{"extra chars"};
		return static_cast<gapr::tier>(t);
	};
	while(std::getline(str, line)) {
		if(line.empty())
			continue;
		if(line[0]=='#')
			continue;
		std::string_view l{line};
		if(l[0]=='*') {
			l.remove_prefix(1);
			auto t2=parse_t2(l);
			acl.tier2tier.emplace_back(gapr::tier::nobody, t2);
		} else if(l[0]=='@') {
			unsigned int t;
			auto [eptr, ec]=std::from_chars(&l[0]+1, &l[0]+l.size(), t, 10);
			if(ec!=std::errc{})
				throw std::system_error(std::make_error_code(ec));
			l.remove_prefix(eptr-&l[0]);
			auto t2=parse_t2(l);
			acl.tier2tier.emplace_back(static_cast<gapr::tier>(t), t2);
		} else {
			auto [idx, res]=gapr::parse_name(&l[0], l.size());
			if(!res)
				gapr::report("username invalid: ", res.error());
			std::string_view n{&l[0], idx};
			l.remove_prefix(idx);
			auto t2=parse_t2(l);
			auto [it, ins]=acl.name2tier.try_emplace(std::string{n}, t2);
			if(!ins) {
				gapr::print("overriding acl for ", n);
				it->second=t2;
			}
		}
	}
	if(!str.eof())
		gapr::report("Failed to read acl list.");
	if(acl.empty())
		return {};
	std::sort(acl.tier2tier.begin(), acl.tier2tier.end(), [](auto a, auto b) {
		return a.first<b.first;
	});
	return std::make_unique<AclInfo>(std::move(acl));
}
