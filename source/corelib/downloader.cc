#include "gapr/downloader.hh"

// winsock2.h want's to be before windows.h
#include <curl/curl.h>

#include "gapr/utility.hh"
#include "gapr/mem-file.hh"
#include "gapr/streambuf.hh"

#include <memory>
#include <set>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <condition_variable>
#include <system_error>

#include <boost/asio/post.hpp>

#include "curl-wrappers.hh"


gapr::curl_url gapr::curl_url::dup() const {
	auto p=::curl_url_dup(_u);
	if(!p)
		throw std::bad_alloc{};
	curl_url r{};
	r._u=p;
	return r;
}
void gapr::curl_url::set_field(CURLUPart pt, const char* v) {
	if(::curl_url_set(_u, pt, v, 0)!=CURLUE_OK)
		throw std::runtime_error{"curl_url_set err"};
}
gapr::curl_str gapr::curl_url::get_field(CURLUPart pt) const {
	char* v;
	if(auto e=::curl_url_get(_u, pt, &v, 0); e!=CURLUE_OK) {
		switch((pt<<16)|e) {
		case (CURLUPART_USER<<16)|CURLUE_NO_USER:
		case (CURLUPART_PASSWORD<<16)|CURLUE_NO_PASSWORD:
		case (CURLUPART_QUERY<<16)|CURLUE_NO_QUERY:
		case (CURLUPART_FRAGMENT<<16)|CURLUE_NO_FRAGMENT:
			return {};
		}
		throw std::runtime_error{"curl_url_get err"};
	}
	return curl_str{v};
}

/*!
 * download parallelism 2
 */

// in header
namespace gapr {
	class curl_errc {
		public:
			static constexpr const std::error_category& category() noexcept {
				return _the_cat;
			}
			static const curl_errc curle_failed_init;
		private:
			int _code;
			static const std::error_category& _the_cat;
			constexpr explicit curl_errc(int c) noexcept: _code{c} { }
			friend inline std::error_code make_error_code(curl_errc e) noexcept;
	};
	inline std::error_code make_error_code(curl_errc e) noexcept {
		return std::error_code{e._code, curl_errc::_the_cat};
	}
	class curlm_errc {
		public:
			static constexpr const std::error_category& category() noexcept {
				return _the_cat;
			}
			static const curlm_errc curle_failed_init;
		private:
			int _code;
			static const std::error_category& _the_cat;
			constexpr explicit curlm_errc(int c) noexcept: _code{c} { }
			friend inline std::error_code make_error_code(curlm_errc e) noexcept;
	};
	inline std::error_code make_error_code(curlm_errc e) noexcept {
		return std::error_code{e._code, curlm_errc::_the_cat};
	}
}
template<>
struct std::is_error_code_enum<gapr::curl_errc>: std::true_type { };
template<>
struct std::is_error_code_enum<gapr::curlm_errc>: std::true_type { };

// in cc
static inline std::error_code make_error_code(CURLcode e) noexcept {
	return std::error_code{static_cast<int>(e), gapr::curl_errc::category()};
}
static inline std::error_code make_error_code(CURLMcode e) noexcept {
	return std::error_code{static_cast<int>(e), gapr::curlm_errc::category()};
}
//http ftp
//https ftps sftp

struct CurlThread;
static std::unique_ptr<CurlThread> _curl_thread{nullptr};
static std::mutex _curl_thread_mtx{};
static unsigned int _curl_thread_refc{0};


struct DownloadItem;
extern "C" size_t writefn(char* ptr, size_t size, size_t nmemb, DownloadItem* f);

struct CacheFile {

	enum class State {
		Empty=0,
		Downloading,
		Ready,
		Error
	};

	CurlThread* ctp;
	int refc; //* ???
	int64_t ts; //*
	State state; //*

	std::vector<std::pair<std::weak_ptr<gapr::downloader_PRIV>, unsigned int>> downloaders;
	void notify_finish(std::error_code ec);

	gapr::mutable_mem_file data{false};
	gapr::mem_file data_c;

	gapr::curl_slist hints;

	CacheFile(const CacheFile&) =delete;
	CacheFile& operator=(const CacheFile&) =delete;

	CacheFile(CurlThread* _ctp):
		ctp{_ctp}, refc{0}, ts{0}, state{State::Empty}
	{
	}
	~CacheFile() {
	}
	size_t memUsage() const {
		return data_c?data_c.size():0;
	}

};

struct CacheFileRef {
	CacheFile* file;
	unsigned int idx;
	CacheFileRef(): file{nullptr} { }
	CacheFileRef(CacheFile* f, unsigned int idx) noexcept: file{f}, idx{idx} { }// called when locked
	CacheFileRef(const CacheFileRef& l) =delete;
	CacheFileRef& operator=(const CacheFileRef& l) =delete;
	~CacheFileRef() {
		release();
	}
	CacheFileRef(CacheFileRef&& r): file{r.file}, idx{r.idx} {
		r.file=nullptr;
	}
	CacheFileRef& operator=(CacheFileRef&& r) {
		std::swap(file, r.file);
		std::swap(idx, r.idx);
		return *this;
	}
	bool isReady() const {
		return file->state==CacheFile::State::Ready;
	}
	gapr::mem_file get_file() const {
		return file->data_c;
	}
	void release();
	CacheFile* operator->() const { return file; }
};


static std::thread::id _active_tid;

struct DownloadItem {
	CURLM* curlm;
	std::string url;
	CURL* curl;
	int login_ver;
	char err[CURL_ERROR_SIZE];

	CacheFile* file;

	bool finished;

	DownloadItem(CURLM* _curlm, const std::string& u, CacheFile* c):
		curlm{_curlm}, url{u}, curl{nullptr}, login_ver{0},
		file{c}, finished{false}
	{
		assert(std::this_thread::get_id()==_active_tid);
		try {
			curl=curl_easy_init();
			if(!curl)
				gapr::report("Failed to call init CURL easy handle");
			setOptions();
			auto r=curl_multi_add_handle(curlm, curl);
			if(r!=0)
				throw std::system_error{make_error_code(r)};
		} catch(...) {
			if(curl) {
				curl_easy_cleanup(curl);
				curl=nullptr;
			}
			throw;
		}
	}

	void finishFile() {
		finished=true;
	}
	void setOptions() {
		assert(std::this_thread::get_id()==_active_tid);
		CURLcode r=curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		if(r!=0)
			throw std::system_error{make_error_code(r)};

#if CURL_AT_LEAST_VERSION(7,85,0)
		r=curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "FILE,HTTP,HTTPS");
		if(r!=0)
			throw std::system_error{make_error_code(r)};
#endif

		r=curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefn);
		if(r!=0)
			throw std::system_error{make_error_code(r)};

		r=curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
		if(r!=0)
			throw std::system_error{make_error_code(r)};

		/* XXX no connection reuse.
		 * Uncomment this if segfault happens in ``ssh_getworkingpath``.
		 * It's a bug in CURL.
		 r=curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
		 if(r!=0)
		 gapr::report(curl_easy_strerror(r));
		 */

		r=curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, long{10});
		if(r!=CURLE_OK)
			throw std::system_error{make_error_code(r)};
		r=curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, long{120});
		if(r!=CURLE_OK)
			throw std::system_error{make_error_code(r)};
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, err);

		// XXX
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, long{0});
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, long{0});

		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, file->hints.lower());
		curl_easy_setopt(curl, CURLOPT_VERBOSE, long{0});
	}
	void resetOptions() {
		assert(std::this_thread::get_id()==_active_tid);
		auto r=curl_multi_remove_handle(curlm, curl);
		if(r!=0)
			throw std::system_error{make_error_code(r)};
		curl_easy_reset(curl);
		setOptions();
		r=curl_multi_add_handle(curlm, curl);
		if(r!=0)
			throw std::system_error{make_error_code(r)};
	}
	bool destroy();
	~DownloadItem() {
		try {
			destroy();
		} catch(...) {
		}
	}
	void setLoginInfo(const std::string& usr, const std::string& pwd, int ver) {
		assert(std::this_thread::get_id()==_active_tid);
		CURLcode r=curl_easy_setopt(curl, CURLOPT_USERNAME, usr.c_str());
		if(r!=0)
			throw std::system_error{make_error_code(r)};
		r=curl_easy_setopt(curl, CURLOPT_PASSWORD, pwd.c_str());
		if(r!=0)
			throw std::system_error{make_error_code(r)};
		login_ver=ver;
	}
	size_t write(const char* ptr, size_t n) noexcept {
		auto& mfile=file->data;
		auto total=n;
		while(n>0) {
			try {
				auto b=mfile.map_tail();
				auto l=b.size();
				if(l>n)
					l=n;
				std::copy(ptr, ptr+l, b.data());
				ptr+=l;
				mfile.add_tail(l);
				n-=l;
			} catch(...) {
				return total-n;
			}
		}
		return total-n;
	}
	void requestLogin(const std::string& url, const std::string& oldusr, const std::string& oldpwd);
};

size_t writefn(char* ptr, size_t size, size_t nmemb, DownloadItem* f) {
	size*=nmemb;
	auto n=f->write(ptr, size);
	if(n<size)
		gapr::print("Failed to alloc more memory.");
	return n;
}

struct LoginInfo {
	std::string usr;
	std::string pwd;
	int ver;
};

static std::string check_hint(gapr::downloader_PRIV* dl, std::string_view url_nopath, std::string_view refpath, std::string_view h);

struct CurlThread {
	std::thread thread{};

	std::vector<LoginInfo> que_logins;
	std::unordered_map<std::string, LoginInfo> logins;

	std::mutex mtx_logins;
	std::recursive_mutex mtx_files;
	std::mutex mtx_input;
	std::mutex mtx_configs;
	std::condition_variable cv_input;
	std::condition_variable cv_output;
	std::condition_variable cv_logins;

	bool stopRequested;
	std::deque<std::string> queue_wait;
	std::deque<std::string> queue_nowait;

	std::unordered_map<std::string, CacheFile*> files;
	int64_t tot_size;
	int64_t max_size;

	size_t job_size_wait;
	size_t job_size_nowait;

	void setLoginInfo(DownloadItem* item, const std::string& url) {
		std::string url_root{gapr::curl_url{url}.cannolize().no_path().url()};
		std::unique_lock<std::mutex> lck{mtx_logins};
		auto i=logins.find(url_root);
		if(i==logins.end()) {
			auto usr=gapr::curl_url{url}.user().string();
			auto pwd=gapr::curl_url{url}.password().string();
			logins[url_root]=LoginInfo{usr, pwd, 0};
			item->setLoginInfo(usr, pwd, 0);
		} else {
			item->setLoginInfo(i->second.usr, i->second.pwd, i->second.ver);
		}
	}

	static int64_t get_ts() {
		auto dt=std::chrono::steady_clock::now()-std::chrono::steady_clock::time_point{};
		return std::chrono::duration_cast<std::chrono::microseconds>(dt).count();
	}

	CacheFileRef download(const std::string& url, std::shared_ptr<gapr::downloader_PRIV> dl, unsigned int dl_idx, const std::vector<std::string>* hints) {
		auto url_can=std::move(gapr::curl_url{url}.cannolize());
		std::string key{url_can.url()};
		auto curpath=url_can.path();
		std::string url_nopath{gapr::curl_url{url}.cannolize().no_path().url()};

		// XXX do_this_in curl_thread
		CacheFileRef ret{};
		{
			std::unique_lock lck{mtx_files};
			auto i=files.find(key);
			CacheFile* file;
			if(i!=files.end()) {
				file=i->second;
			} else {
				file=new CacheFile{this};
				files[key]=file;
				tot_size+=file->memUsage();
			}
			file->ts=get_ts();
			file->refc++;
			if(file->state==CacheFile::State::Ready)
				return CacheFileRef{file, 0};
			unsigned int di=file->downloaders.size()+1;
			file->downloaders.emplace_back(std::move(dl), dl_idx);
			ret=CacheFileRef{file, di};
			if(hints && file->state!=CacheFile::State::Downloading) {
				auto& hh=*hints;
				for(std::size_t i=dl_idx+1; i<hh.size(); ++i) {
					if(auto hint=check_hint(dl.get(), url_nopath, curpath, std::string_view{hh[i]}); !hint.empty())
						file->hints.append(hint.c_str());
				}
			}
		}

		{
			std::unique_lock<std::mutex> lck{mtx_input};
			queue_wait.push_back(url);
			cv_input.notify_one();
		}
		return ret;
	}
	void downloadIfIdle(const std::string& url) {
		std::string key{gapr::curl_url{url}.cannolize().url()};
		{
			std::unique_lock lck{mtx_files};
			auto i=files.find(key);
			if(i!=files.end()) {
				auto file=i->second;
				file->ts=get_ts();
				if(file->state==CacheFile::State::Ready)
					return;
			} else {
				auto file=new CacheFile{this};
				file->ts=get_ts();
				files[key]=file;
				tot_size+=file->memUsage();
			}
		}

		{
			std::unique_lock<std::mutex> lck{mtx_input};
			queue_nowait.push_back(url);
			cv_input.notify_one();
		}
	}

	void stop() {
		std::unique_lock<std::mutex> lck{mtx_input};
		stopRequested=true;
		cv_input.notify_one();
	}

	CurlThread():
		thread{std::thread{}},
		stopRequested{false},
		tot_size{0}, max_size{1024*1024*1024}, job_size_wait{2}, job_size_nowait{0}
	{
		auto r=curl_global_init(CURL_GLOBAL_DEFAULT);
		if(r!=0)
			throw std::system_error{make_error_code(r)};
		//CacheThreadOptions::instance()->attach(priv);
	}
	~CurlThread() {
		while(!files.empty()) {
			auto i=files.begin();
			delete i->second;
			files.erase(i);
		}
		curl_global_cleanup();
		//CacheThreadOptions::instance()->attach(nullptr);
	}

	bool multi_wait(CURLM* m) {
		assert(std::this_thread::get_id()==_active_tid);
		long t=-1;
		CURLMcode r=curl_multi_timeout(m, &t);
		if(r!=0)
			throw std::system_error{make_error_code(r)};
		struct timeval timeout{1, 0};
		if(t>=0) {
			timeout.tv_sec=t/1000;
			if(timeout.tv_sec>1)
				timeout.tv_sec=1;
			else
				timeout.tv_usec=(t%1000)*1000;
		}
		fd_set fdread;
		fd_set fdwrite;
		fd_set fdexcep;
		FD_ZERO(&fdread);
		FD_ZERO(&fdwrite);
		FD_ZERO(&fdexcep);
		int maxfd=-1;
		r=curl_multi_fdset(m, &fdread, &fdwrite, &fdexcep, &maxfd);
		if(r!=0)
			throw std::system_error{make_error_code(r)};
		if(maxfd==-1)
			return false;
		int rc=select(maxfd+1, &fdread, &fdwrite, &fdexcep, &timeout);
		if(rc==-1)
			gapr::report(strerror(errno));
		return rc;
	}
	size_t multi_perform(CURLM* m) {
		assert(std::this_thread::get_id()==_active_tid);
		int n;
		CURLMcode r=curl_multi_perform(m, &n);
		if(r!=0)
			throw std::system_error{make_error_code(r)};
		return n;
	}
	void handle_login_denied(ptrdiff_t pi, std::vector<std::unique_ptr<DownloadItem>>& items) {
		auto p=items.begin()+pi;
		auto item=(*p).get();
		std::string url{gapr::curl_url{item->url}.cannolize().no_path().url()};
		std::unique_lock<std::mutex> lck_logins{mtx_logins};
		auto i=logins.find(url);
		if(i==logins.end())
			gapr::report("Assertion failed");
		auto login_ver=i->second.ver;
		if(item->login_ver!=login_ver) {
			item->resetOptions();
			item->setLoginInfo(i->second.usr, i->second.pwd, login_ver);
			item->file->data.reset();
			gapr::print("Reset password: ", url);
		} else {
			item->requestLogin(url, i->second.usr, i->second.pwd);
			while(que_logins.empty())
				cv_logins.wait(lck_logins);
			auto& login=que_logins.front();
			if(login.ver==0) {
				auto url=item->url;
				gapr::print("Abort downloading: ", url);
				auto locked=item->destroy();
				items.erase(p);
				if(locked)
					cv_output.notify_all();
			} else {
				i->second.usr=login.usr;
				i->second.pwd=login.pwd;
				i->second.ver+=login.ver;
				item->resetOptions();
				item->setLoginInfo(i->second.usr, i->second.pwd, i->second.ver);
				item->file->data.reset();
				gapr::print("Set new password: ", url);
			}
			que_logins.clear();
		}
	}
	void multi_handle_finished(CURLM* curlm, std::vector<std::unique_ptr<DownloadItem>>& items) {
		assert(std::this_thread::get_id()==_active_tid);
		int msgs_left;
		while(auto msg=curl_multi_info_read(curlm, &msgs_left)) {
			if(msg->msg==CURLMSG_DONE) {
				auto p=items.end();
				for(auto i=items.begin(); i!=items.end(); i++) {
					if((*i)->curl==msg->easy_handle) {
						p=i;
						break;
					}
				}
				if(p==items.end())
					gapr::report("Unexpected missing item");
				gapr::print("msg.data.result: ", msg->data.result);
				switch(msg->data.result) {
					case CURLE_OK:
						{
							auto item=(*p).get();
							long httpCode=0;
							if(curl_easy_getinfo(item->curl, CURLINFO_RESPONSE_CODE, &httpCode)!=CURLE_OK)
								gapr::report("unknown response code");
							if(httpCode>=400 && httpCode<500) {
								if(httpCode!=401)
									gapr::report("unhandled http error: ", httpCode);
								//XXX CURLINFO_HTTPAUTH_AVAIL
								handle_login_denied(p-items.begin(), items);
							} else if(httpCode>=500 && httpCode<600) {
								auto url=item->url;
								gapr::print("Error downloading: ", url, ": http ", httpCode);
								auto locked=item->destroy();
								items.erase(p);
								if(locked>0)
									cv_output.notify_all();
							} else {
								// XXX CURLINFO_FILETIME_T
								item->finishFile();
								auto url=item->url;
								auto locked=item->destroy();
								items.erase(p);
								if(locked)
									cv_output.notify_all();
							}
						}
						break;
					case CURLE_LOGIN_DENIED:
						handle_login_denied(p-items.begin(), items);
						break;
						/*
							case TIMEOUT:
						// reset
						// redo
						break;
						*/
					default:
						{
							auto item=(*p).get();
							auto url=item->url;
							gapr::print("Error downloading: ", url, ": ", curl_easy_strerror(msg->data.result), " err: ", item->err);
							auto locked=item->destroy();
							items.erase(p);
							if(locked>0)
								cv_output.notify_all();
						}
				}
			}
		}
	}

	void tryMakeSpace() {
		int64_t maxSize;
		{
			std::unique_lock<std::mutex> lck{mtx_configs};
			maxSize=max_size;
		}
		std::unique_lock lck{mtx_files};
		if(tot_size>maxSize) {
			typedef decltype(files.begin()) files_iter;
			std::vector<files_iter> iters{};
			for(auto i=files.begin(); i!=files.end(); i++) {
				auto c=i->second->refc;
				if(c<0)
					gapr::report("negative refc???");
				if(c==0)
					iters.push_back(i);
			}
			std::sort(iters.begin(), iters.end(), [](const files_iter& a, const files_iter& b) { return a->second->ts>b->second->ts; });
			while(tot_size>maxSize && !iters.empty()) {
				auto i=iters.back();
				iters.pop_back();
				auto f=i->second;
				files.erase(i);
				tot_size-=f->memUsage();
				delete f;
			}
		}
	}

	void run() {
		_active_tid=std::this_thread::get_id();
		std::vector<std::unique_ptr<DownloadItem>> items;

		auto curlm_deleter=[](CURLM* m) {
			auto r=curl_multi_cleanup(m);
			if(r!=0)
				throw std::system_error{make_error_code(r)};
		};
		std::unique_ptr<CURLM, std::function<void(CURLM*)>> curlm{curl_multi_init(), curlm_deleter};
		if(!curlm.get())
			gapr::report("Failed to init CURL multi handle");

		std::unique_lock<std::mutex> lck_input{mtx_input};
		while(!stopRequested) {
			size_t nWait, nNoWait;
			{
				std::unique_lock<std::mutex> lck{mtx_configs};
				nWait=job_size_wait;
				nNoWait=job_size_nowait;
			}

			{
				std::vector<std::unique_ptr<DownloadItem>> itemsWait;
				std::vector<std::unique_ptr<DownloadItem>> itemsNoWait;
				std::unique_lock lck_files{mtx_files};
				while(!items.empty()) {
					auto& i=items.back();
					if(i->file->refc>1) {
						itemsWait.push_back(std::move(i));
					} else {
						assert(i->file->downloaders.empty());
						itemsNoWait.push_back(std::move(i));
					}
					items.pop_back();
				}
				std::swap(items, itemsWait);

				assert(lck_input.owns_lock());
				while(!queue_wait.empty() && items.size()<nWait) {
					auto url=queue_wait.front();
					queue_wait.pop_front();
					std::string key{gapr::curl_url{url}.cannolize().url()};
					auto i=files.find(key);
					if(i!=files.end()) {
						auto f=i->second;
						if(f->refc>0) {
							switch(f->state) {
								case CacheFile::State::Empty:
								case CacheFile::State::Error:
									{
										std::unique_ptr<DownloadItem> item{new DownloadItem(curlm.get(), key, f)};
										if(!item)
											gapr::report("Failed to create download item.");
										tot_size-=f->memUsage();
										f->state=CacheFile::State::Downloading;
										f->refc++;
										setLoginInfo(item.get(), url);
										items.push_back(std::move(item));
									}
									break;
								case CacheFile::State::Downloading:
									break;
								case CacheFile::State::Ready:
									if(f->refc>0)
										cv_output.notify_all();
									break;
							}
						}
					}
				}

				if(items.empty()) {
					while(!itemsNoWait.empty()) {
						items.push_back(std::move(itemsNoWait.back()));
						itemsNoWait.pop_back();
					}

					while(!queue_nowait.empty() && items.size()<nNoWait) {
						auto url=queue_nowait.back();
						queue_nowait.pop_back();
						std::string key{gapr::curl_url{url}.cannolize().url()};
						auto i=files.find(key);
						if(i!=files.end()) {
							auto f=i->second;
							switch(f->state) {
								case CacheFile::State::Empty:
								case CacheFile::State::Error:
									{
										std::unique_ptr<DownloadItem> item{new DownloadItem(curlm.get(), url, f)};
										if(!item)
											gapr::report("Failed to create download item.");
										tot_size-=f->memUsage();
										f->state=CacheFile::State::Downloading;
										f->refc++;
										setLoginInfo(item.get(), url);
										gapr::print("Start pre-downloading ", key);
										items.push_back(std::move(item));
									}
									break;
								case CacheFile::State::Downloading:
									break;
								case CacheFile::State::Ready:
									break;
							}
						}
					}
				}

				lck_files.unlock();
				while(!itemsNoWait.empty()) {
					auto i=std::move(itemsNoWait.back());
					itemsNoWait.pop_back();
					i->destroy();
				}
			}
			lck_input.unlock();

			tryMakeSpace();

			if(!items.empty()) {
				auto nleft=multi_perform(curlm.get());
				while(nleft==items.size()) {
					auto r=multi_wait(curlm.get());
					if(r) {
						nleft=multi_perform(curlm.get());
					} else {
						break;
					}
				}
				if(nleft<items.size()) {
					multi_handle_finished(curlm.get(), items);
				}
				lck_input.lock();
				continue;
			}

			lck_input.lock();
			while(queue_wait.empty() && queue_nowait.empty() && !stopRequested)
				cv_input.wait(lck_input);
		}
	}

	void updateLogin(const std::string& usr, const std::string& pwd, bool cancelled) {
		int verinc=cancelled?0:1;
		std::unique_lock<std::mutex> lck{mtx_logins};
		que_logins.push_back(LoginInfo{usr, pwd, verinc});
		cv_logins.notify_one();
	}

	void start() {
		thread=std::thread{[this]() {
				gapr::print(1, "Cache thread started");
				try {
					run();
				} catch(const std::runtime_error& e) {
					//Q_EMIT threadError(std::string{"Unexpected error in cache thread: "}+=e.what());
					gapr::print("Cache thread error: ", e.what());
					return;
				}
				gapr::print(1, "Cache thread finished");
			}
		};
	}
	void join() {
		thread.join();
	}
};

void CacheFileRef::release() {
	if(file) {
		std::unique_lock lck{file->ctp->mtx_files};
		file->refc--;
		if(idx>0) {
			file->downloaders[idx-1].second=0;
			while(!file->downloaders.empty() && file->downloaders.back().second==0)
				file->downloaders.pop_back();
		}
		file=nullptr;
	}
}

bool DownloadItem::destroy() {
	assert(std::this_thread::get_id()==_active_tid);

	if(curl) {
		auto r=curl_multi_remove_handle(curlm, curl);
		if(r!=0)
			throw std::system_error{make_error_code(r)};
		curl_easy_cleanup(curl);
		curl=nullptr;
	}
	bool locked=false;
	if(file) {
		std::unique_lock lck{file->ctp->mtx_files};
		file->refc--;
		locked=file->refc>0;
		if(finished) {
			file->state=CacheFile::State::Ready;
			file->data_c=std::move(file->data);
		} else {
			file->state=CacheFile::State::Error;
			// XXX retry !resume or *restart???
			file->data.reset();
		}
		auto hints=std::move(file->hints);
		file->notify_finish(finished?std::error_code{}:std::make_error_code(std::io_errc::stream));
		// XXXXXX 
		file->ctp->tot_size+=file->memUsage();
		file=nullptr;
	}
	return locked;
}





//#include "cache.thread.options.cc"

////////////////////////////////////////////////////////

namespace {
	class curl_easy_category: public std::error_category {
		public:
			constexpr curl_easy_category() noexcept =default;
			~curl_easy_category() override =default;

			const char* name() const noexcept override {
				return "curl_easy";
			}
			std::string message(int i) const override {
				//XXX CURLOPT_ERRORBUFFER(3)
				auto m=::curl_easy_strerror(static_cast<CURLcode>(i));
				return m?m:"Unknown error";
			}

			std::error_condition default_error_condition(int i) const noexcept override {
				std::errc e;
				switch(i) {
					default:
						return std::error_condition{i, *this};
					case CURLE_OK:
						return std::error_condition{};
					case CURLE_UNSUPPORTED_PROTOCOL:
						e=std::errc::protocol_not_supported; break;
					case CURLE_OUT_OF_MEMORY:
						e=std::errc::not_enough_memory; break;
					case CURLE_AGAIN:
						e=std::errc::resource_unavailable_try_again; break;
					case CURLE_OPERATION_TIMEDOUT:
						e=std::errc::timed_out; break;
					case CURLE_NOT_BUILT_IN:
						e=std::errc::operation_not_supported; break;
					case CURLE_LOGIN_DENIED:
						e=std::errc::permission_denied; break;
				}
				return std::make_error_condition(e);
			}
	};
	class curl_multi_category: public std::error_category {
		public:
			constexpr curl_multi_category() noexcept =default;
			~curl_multi_category() override =default;

			const char* name() const noexcept override {
				return "curl_multi";
			}
			std::string message(int i) const override {
				auto m=::curl_multi_strerror(static_cast<CURLMcode>(i));
				return m?m:"Unknown error";
			}

			std::error_condition default_error_condition(int i) const noexcept override {
				std::errc e;
				switch(i) {
					default:
						return std::error_condition{i, *this};
					case CURLM_OK:
						return std::error_condition{};
					case CURLM_OUT_OF_MEMORY:
						e=std::errc::not_enough_memory; break;
				}
				return std::make_error_condition(e);
			}
	};
	template<typename> struct curl_category_instances {
		static const curl_easy_category easy;
		static const curl_multi_category multi;
	};
	template<typename T>
		const curl_easy_category curl_category_instances<T>::easy{};
	template<typename T>
		const curl_multi_category curl_category_instances<T>::multi{};
}

const std::error_category& gapr::curl_errc::_the_cat{curl_category_instances<void>::easy};
const gapr::curl_errc gapr::curl_errc::curle_failed_init{CURLE_FAILED_INIT};

const std::error_category& gapr::curlm_errc::_the_cat{curl_category_instances<void>::multi};

struct DownloadService: boost::asio::execution_context::service {
	using key_type=DownloadService;
	explicit DownloadService(boost::asio::execution_context& ctx): service{ctx}
	{
		unsigned int r;
		{
			std::lock_guard lck{_curl_thread_mtx};
			r=_curl_thread_refc;
			if(r==0)
				_curl_thread=std::make_unique<CurlThread>();
			++_curl_thread_refc;
		}
		if(r==0)
			_curl_thread->start();
	}
	void shutdown() noexcept override {
		//TODO A service's shutdown member function shall destroy all copies of user-defined function objects that are held by the service.
		{
			std::lock_guard lck{_curl_thread_mtx};
			if(--_curl_thread_refc!=0)
				return;
		}
		auto thr=std::move(_curl_thread);
		thr->stop();
		thr->join();
	}
	~DownloadService() { }
};

struct gapr::downloader_PRIV: gapr::downloader::DATA {

	struct Item {
		CacheFileRef file;
		unsigned int idx;
		int progr{0};
		Item(CacheFileRef&& f, unsigned int i): file{std::move(f)}, idx{i} { }
	};
	std::vector<Item> items;
	std::unique_ptr<downloader::WaitOp> cur_op;
	std::mutex mtx;
	int progress{-1};
	bool pending{false};
	unsigned int finished{0};
	std::unordered_set<std::string_view> _hints_done;

	void do_wait(std::unique_ptr<downloader::WaitOp> op) {
		std::error_code ec;
		int progr;
		{
			std::lock_guard lck{mtx};
			if(this->pending) {
				ec=std::move(this->ec);
				this->pending=false;
				this->notified=true;
			} else {
				assert(!this->cur_op);
				this->cur_op=std::move(op);
				this->notified=false;
			}
			progr=progress*1000/(items.size()*1001);
			if(finished==items.size())
				progr=1001;
		}
		if(op) {
			boost::asio::post(ex, [op=std::move(op),ec,progr]() {
				op->complete(ec, progr);
			});
		}
	}
	std::pair<std::unique_ptr<std::streambuf>, unsigned int> do_get() {
		unsigned int idx;
		{
			std::lock_guard lck{mtx};
			idx=get_head-1;
			get_head=items[idx].idx;
		}
		auto ref=std::move(items[idx].file);
		std::unique_ptr<std::streambuf> file{};
		if(auto mf=ref.get_file(); mf)
			file=gapr::make_streambuf(std::move(mf));
		return {std::move(file), idx};
	}
	void post_finish(std::error_code ec, unsigned int idx) {
		std::unique_ptr<downloader::WaitOp> op;
		int progr;
		{
			std::lock_guard lck{mtx};
			op=std::move(this->cur_op);
			if(!op) {
				this->ec=ec;
				this->pending=true;
			} else {
				this->pending=false;
			}
			auto prev_progr=items[idx].progr;
			items[idx].progr=1001;
			{
				auto p=&get_head;
				while(*p)
					p=&items[*p-1].idx;
				items[idx].idx=*p;
				*p=idx+1;
			}
			gapr::print("do_post: ", idx, ' ', static_cast<bool>(items[idx].file.get_file()));
			progress+=1001-prev_progr;
			finished++;
			notified=true;
			progr=progress*1000/(items.size()*1001);
			assert(finished<=items.size());
			if(finished==items.size())
				progr=1001;
		}
		if(op) {
			boost::asio::post(ex, [op=std::move(op),ec,progr]() {
				op->complete(ec, progr);
			});
		}
	}
	void check_existing() {
		std::lock_guard lck{mtx};
		for(unsigned int idx=0; idx<items.size(); idx++) {
			if(items[idx].file.isReady()) {
				this->pending=true;
				auto prev_progr=items[idx].progr;
				items[idx].progr=1001;
				{
					auto p=&get_head;
					while(*p)
					  p=&items[*p-1].idx;
					items[idx].idx=*p;
					*p=idx+1;
				}
				gapr::print("do_check: ", idx, ' ', static_cast<bool>(items[idx].file.get_file()));
				progress+=1001-prev_progr;
				finished++;
				assert(finished<=items.size());
				notified=true;
			}
		}
	}
};
static std::string check_hint(gapr::downloader_PRIV* dl, std::string_view url_nopath, std::string_view refpath, std::string_view h) {
	auto add_rel_path=[](std::string& str, std::string_view p1, std::string_view p2) {
		assert(p1[0]=='/');
		assert(p2[0]=='/');
		unsigned int i=0, j=0;
		while(i<p1.size() && i<p2.size()) {
			if(p1[i]!=p2[i])
				break;
			if(p1[i]=='/')
				j=i;
			++i;
		}
		if(j==0) {
			str+=p2;
			return;
		}
		for(i=j+1; i<p1.size(); ++i)
			if(p1[i]=='/')
				str+="../";
		assert(p2[j]=='/');
		str+=p2.substr(j+1);
	};
	auto [it, ins]=dl->_hints_done.emplace(h);
	if(!ins)
		return {};
	gapr::curl_url u{&h[0]};
	auto path=u.path();
	auto query=u.query();
	auto fragment=u.fragment();
	u.cannolize().no_path();
	if(u.url()!=url_nopath) {
		dl->_hints_done.erase(it);
		return {};
	}
	std::string hint{"GaprWillGet: "};
	add_rel_path(hint, refpath, path);
	if(query) {
		(hint+='?')+=query;
	}
	if(fragment) {
		(hint+='#')+=fragment;
	}
	return hint;
}

	void CacheFile::notify_finish(std::error_code ec) {
		for(std::size_t i=downloaders.size(); i-->0;) {
			auto& p=downloaders[i];
			bool keep{false};
			if(auto pp=p.first.lock()) {
				pp->post_finish(ec, p.second);
				keep=true;
			}
			if(!keep) {
				p.first={};
				p.second=0;
			}
		}
	}

gapr::downloader::downloader(const executor_type& ex, std::string&& url):
	_priv{std::make_shared<downloader_PRIV>()}
{
	boost::asio::use_service<DownloadService>(boost::asio::query(ex, boost::asio::execution::context));
	_priv->ex=ex;

#if 0
	if(!url.isValid())
		gapr::report("URL is not valid.");
#endif
	auto ref=_curl_thread->download(url, _priv, 0, nullptr);
	_priv->items.emplace_back(/*std::move(url), */std::move(ref), 0);
	_priv->check_existing();
}
gapr::downloader::downloader(const executor_type& ex, const std::vector<std::string>& urls): _priv{std::make_shared<downloader_PRIV>()} {
	boost::asio::use_service<DownloadService>(boost::asio::query(ex, boost::asio::execution::context));
	_priv->ex=ex;
	_priv->items.reserve(urls.size());
	for(unsigned int i=0; i<urls.size(); i++) {
#if 0
		if(!url.isValid())
			gapr::report("URL is not valid.");
#endif
		auto ref=_curl_thread->download(urls[i], _priv, i, &urls);
		_priv->items.emplace_back(/*std::move(url), */std::move(ref), 0);
	}
	_priv->check_existing();
}
gapr::downloader::~downloader() {
	//updateLogin({}, {}, false);
}
void gapr::downloader::update_login(std::string&& username, std::string&& password) {
	_curl_thread->updateLogin(username, password, false);
}
void gapr::downloader::when_idle(std::string&& url) {
#if 0
	if(!url.isValid())
		gapr::report("URL is not valid.");
#endif
	return _curl_thread->downloadIfIdle(url);
}

void DownloadItem::requestLogin(const std::string& url, const std::string& oldusr, const std::string& oldpwd) {
	// TODO return error_code and expect password
	//Q_EMIT thread->requestLogin(url, i->second.usr, i->second.pwd);
	gapr::print(1, "emit request login");
}
std::pair<std::unique_ptr<std::streambuf>, unsigned int> gapr::downloader::do_get() const {
	return _priv->do_get();
}
void gapr::downloader::do_wait(downloader_PRIV* p, std::unique_ptr<WaitOp> op) {
	return p->do_wait(std::move(op));
}

