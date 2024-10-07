/* file-cache.cc
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


//#include "gapr/mem-file-cache.hh"
#include "gapr/likely.hh"

//#include "gapr/utility.hh"
#include "gapr/streambuf.hh"

//#include <curl/curl.h>
//#include <uv.h>

//#include <regex>
#include <cstring>

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <iostream>


#if 0
	gapr::mem_file_cache::enabler enabler;
	boost::asio::io_context ctx{1};
	gapr::mem_file_cache h1{"abc", ctx.get_executor(), [](std::error_code ec, slot&& s, mem_file&& f) {
		gapr::print("h1: ", ec);
		if(ec)
			return;
		if(slot) {
			// use slot to fill in
		} else {
			if(file) {
				// use file
			} else {
				// file never available
			}
		}
	}};
	gapr::mem_file_cache h2{"abc", ctx.get_executor(), [](std::error_code ec, slot&& s, mem_file&& f) {
		gapr::print("h2: ", ec);
	}};
	gapr::mem_file_cache h3{"abc", ctx.get_executor(), [](std::error_code ec, slot&& s, mem_file&& f) {
		gapr::print("h3: ", ec);
	}};
	ctx.run();
#endif
#if 0
class mem_file_cache_service: public prot priv boost::asio::execution_context::service {
	public:
		explicit mem_file_cache_service(boost::asio::execution_context& owner): service{owner} { }
		~mem_file_cache_service() override { }
	private:
		void shutdown() override {
			/// Destroy all user-defined handler objects owned by the service.

		}
		void notify_fork(execution_context::fork_event event) override {
		}

};

///
//
//
//Let ctx be the execution context returned by the executor's context() member function. An executor becomes invalid when the first call to ctx.shutdown() returns. 
//The effect of calling on_work_started, on_work_finished, dispatch, post, or defer on an invalid executor is undefined. 

/*
int gapr::FileCache::_inst_refc{0};
gapr::FileCache* gapr::FileCache::_instance{nullptr};

void gapr::FileCache::create_instance() {
	assert(!_instance && _inst_refc==1);
	_io_ctx=new(&_io_ctx_) gapr::IoContext::Enabler{};
	_instance=new FileCache{};
}
void gapr::FileCache::destroy_instance() {
	assert(_instance && _inst_refc==0);
	delete _instance;
	_io_ctx->~Enabler();
}
*/
// 0 [] [url,0,errc]
// .5 [url,.5,errc,download lock,wq]
// 1 [url,1,errc,file,wq]
// -1 /pause/cancel
# if 0
template<typename Func>
class Cleaner {
	public:
		Cleaner(Func&& f): _f{std::forward<Func>(f)}, _r{false} { }
		~Cleaner() noexcept { if(!_r) _f(); }
		void release() noexcept { _r=true; }
	private:
		Func _f;
		bool _r;
};
template<typename Func>
Cleaner<Func> make_cleaner(Func&& f) {
	return Cleaner<Func>{std::forward<Func>(f)};
}

gapr::FileCacheProvider* gapr::FileCache::find_provider(const std::string& url) {
	for(auto& p: _providers) {
		if(std::strncmp(&p.first[0], &url[0], p.first.size())==0)
			return p.second;
	}
	return nullptr;
}
namespace ba=boost::asio;
gapr::FileCache::Lock gapr::FileCache::do_async_wait(const std::string& url, std::unique_ptr<FileCache_PRIV::CacheOp>&& op) {
	auto ins=_files.emplace(url, FileCache_PRIV::ItemPtr{});
	if(ins.second) {
		auto e=make_cleaner([&files=this->_files,it=ins.first]() ->void { files.erase(it); });
		ins.first->second=std::move(FileCache_PRIV::ItemPtr{new FileCache_PRIV::Item()});
		e.release();
	}
	auto item=ins.first->second.get();
	auto lockitem=op.get();
	item->wq.emplace_back(std::move(op));
	switch(item->state) {
		case FileCache_PRIV::EMPTY:
			if(auto prov=find_provider(url)) {
				prov->download(url, item);
				item->state=FileCache_PRIV::LOADING;
			} else {
				ba::post(_io_ctx, [item]() ->void {
					while(!item->wq.empty()) {
						auto op=std::move(item->wq.front());
						item->wq.pop_front();
						op->complete(std::error_code{5, std::generic_category()}, nullptr);
					}
				});
			}
			break;
		case FileCache_PRIV::LOADING:
			break;
		case FileCache_PRIV::READY:
			ba::post(_io_ctx, [item]() ->void {
				while(!item->wq.empty()) {
					auto op=std::move(item->wq.front());
					item->wq.pop_front();
					auto file=item->file;
					op->complete(std::error_code{}, std::unique_ptr<Streambuf>{Streambuf::memInput(std::move(file))});
				}
			});
			break;
	}
	return {lockitem};
}
gapr::FileCache::~FileCache() {
}
/////////////////////////
#if 0
namespace gapr { namespace WebCache_PRIV {
	enum class ReqState {
		Fresh,
		Queued,
		Active,
		Trashed,
	};
	struct FetchRequest {
		std::string url;
		WebCache::FetchCb cb;
		ReqState state;
		Downloading* downloading;
	};
	struct PrefetchRequest {
		std::string url;
		WebCache::PrefetchCb cb;
		ReqState state;
		Downloading* downloading;
	};
} }

using gapr::CacheFile;
using namespace gapr::WebCache_PRIV;
			WaitLock submit_op(const std::string& url, ...) {
					//
					//
				}

struct gapr::WebCache_PRIV::Downloading {
	std::string url;
	std::string site;

	std::vector<std::unique_ptr<char[]>> bufs;
	size_t pos;

	std::list<FetchRequest> fetch_reqs;
	std::list<PrefetchRequest> prefetch_reqs;

	std::list<PollContext> polls;

	bool paused;

	Downloading(CURLM* _curlm, const char* u, const char* s):
		curlm{_curlm}, curl{nullptr}, url{u}, site{s}, login_ver{0},
		bufs{}, pos{0},
		fetch_reqs{}, prefetch_reqs{}, polls{}, paused{true}
	{
			curl=curl_easy_init();
			if(!curl)
				gapr::report("curl_easy_init(): failed");
	}

	~Downloading() {
		try {
			destroy();
		} catch(...) {
		}
	}

	void destroy() {
		if(curl) {
			if(!paused) {
				auto r=curl_multi_remove_handle(curlm, curl);
				if(r!=0)
					gapr::report("curl_multi_remove_handle(): ", curl_multi_strerror(r));
				paused=true;
			}
			curl_easy_cleanup(curl);
			curl=nullptr;
		}
	}

	gapr::CacheFile finish_file() {
		return gapr::CacheFile{pos, BUFSIZ, &bufs[0]};
	}

	void set_options() {
		CURLcode r=curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		if(r!=0)
			gapr::report(curl_easy_strerror(r));

		r=curl_easy_setopt(curl, CURLOPT_PROTOCOLS, long(CURLPROTO_FILE|CURLPROTO_HTTP|CURLPROTO_HTTPS|CURLPROTO_FTP|CURLPROTO_FTPS|CURLPROTO_SCP|CURLPROTO_SFTP));
		if(r!=0)
			gapr::report(curl_easy_strerror(r));

		r=curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		if(r!=0)
			gapr::report(curl_easy_strerror(r));

		r=curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
		if(r!=0)
			gapr::report(curl_easy_strerror(r));

		/* XXX no connection reuse.
		 * Uncomment this if segfault happens in ``ssh_getworkingpath``.
		 * It's a bug in CURL.
		 r=curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
		 if(r!=0)
		 gapr::report(curl_easy_strerror(r));
		 */
	}

	void pause() {
		if(!paused) {
			auto r=curl_multi_remove_handle(curlm, curl);
			if(r!=0)
				gapr::report("curl_multi_remove_handle(): ", curl_multi_strerror(r));
			curl_easy_reset(curl);
			paused=true;
		}
	}
	void resume(const char* usr, const char* pw, int ver) {
		if(paused) {
			set_options();
			if(usr && *usr!='\0') {
				if(auto r=curl_easy_setopt(curl, CURLOPT_USERNAME, usr))
					gapr::report("curl_easy_setopt(): ", curl_easy_strerror(r));
				if(auto r=curl_easy_setopt(curl, CURLOPT_PASSWORD, pw))
					gapr::report("curl_easy_setopt(): ", curl_easy_strerror(r));
				login_ver=ver;
			}
			if(auto r=curl_multi_add_handle(curlm, curl))
				gapr::report("curl_multi_add_handle(): ", curl_multi_strerror(r));
			paused=false;
		}
	}

	size_t write(const char* ptr, size_t n) noexcept {
		try {
			size_t cnt=(pos+n+BUFSIZ-1)/BUFSIZ;
			while(cnt>bufs.size())
				bufs.push_back(std::unique_ptr<char[]>{new char[BUFSIZ]});
		} catch(...) {
			return 0;
		}

		auto total=n;
		while(n>0) {
			auto rem=pos%BUFSIZ;
			auto p=bufs[pos/BUFSIZ].get()+rem;
			auto l=BUFSIZ-rem;
			if(l>n)
				l=n;
			std::char_traits<char>::copy(p, ptr, l);
			ptr+=l;
			n-=l;
			pos+=l;
		}
		return total-n;
	}

	static int socket_callback(CURL *easy, curl_socket_t s, int what, void *userp, void *socketp) noexcept;
	static int timer_callback(CURLM *multi, long timeout_ms, void *userp) noexcept;
	static void poll_cb(uv_poll_t* handle, int status, int events) noexcept;
	static void timer_cb(uv_timer_t* handle) noexcept;
	static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) noexcept {
		auto dl=static_cast<Downloading*>(userdata);
		size*=nmemb;
		auto n=dl->write(ptr, size);
		return n;
	}
};

gapr::WebCache* gapr::WebCache::_instance{nullptr};
int gapr::WebCache::_instance_refc{0};
void gapr::WebCache::Enabler::create_instance(uv_loop_t* loop) {
	if(auto r=curl_global_init(CURL_GLOBAL_DEFAULT))
		gapr::report("curl_global_init(): ", curl_easy_strerror(r));
	gapr::WebCache::_instance=new WebCache{loop};
}
void gapr::WebCache::Enabler::destroy_instance() {
	delete gapr::WebCache::_instance;
	curl_global_cleanup();
}
gapr::WebCache::WebCache(uv_loop_t* loop):
	_loop{loop}, _max_usage{0}, _max_fetches{0}, _max_prefetches{0},
	_password_cb{},
	_files{}, _downloads{}, _logins{},
	_fetches_fresh{}, _fetches_queued{},
	_prefetches_fresh{}, _prefetches_queued{},
	_timer{nullptr}, _curlm{nullptr},
	_mem_usage{0}, _nfetches{0}, _nprefetches{0}, _nactive{0}
{
	_timer=new uv_timer_t{};
	_timer->data=this;
	if(auto r=uv_timer_init(_loop, _timer))
		gapr::report("uv_timer_init(): ", uv_strerror(r));

	_curlm=curl_multi_init();
	if(!_curlm)
		gapr::report("curl_multi_init(): failed");
	if(auto r=curl_multi_setopt(_curlm, CURLMOPT_SOCKETFUNCTION, &Downloading::socket_callback))
		gapr::report("curl_multi_setopt(): ", curl_multi_strerror(r));
	if(auto r=curl_multi_setopt(_curlm, CURLMOPT_SOCKETDATA, this))
		gapr::report("curl_multi_setopt(): ", curl_multi_strerror(r));
	if(auto r=curl_multi_setopt(_curlm, CURLMOPT_TIMERFUNCTION, &Downloading::timer_callback))
		gapr::report("curl_multi_setopt(): ", curl_multi_strerror(r));
	if(auto r=curl_multi_setopt(_curlm, CURLMOPT_TIMERDATA, this))
		gapr::report("curl_multi_setopt(): ", curl_multi_strerror(r));
}

static void timer_close_cb(uv_handle_t* handle) { delete handle; }
gapr::WebCache::~WebCache() {
	if(auto r=curl_multi_cleanup(_curlm))
		gapr::report("curl_multi_cleanup(): ", curl_multi_strerror(r));

	uv_close(reinterpret_cast<uv_handle_t*>(_timer), timer_close_cb);
	_timer=nullptr;

	// XXX
	//_downloads{}, _fetches_fresh{}, _fetches_queued{}, _prefetches_fresh{}, _prefetches_queued{},
}

static std::list<FetchRequest> dustbin_for_fetches{};
static std::list<PrefetchRequest> dustbin_for_prefetches{};
static std::list<PollContext> dustbin_for_polls{};
static void poll_close_cb(uv_handle_t* handle) {
	auto poll=static_cast<PollContext*>(handle->data);
	dustbin_for_polls.erase(poll->iter);
}
//////// //////////////////////////////////////////////////////////////

//////////////////////////
		////////
		//////////////
		//////////////////////////
	/////////////////// ///////////////////////////////
		//url.toEncoded().data());
	  //


gapr::WebCache::FetchLock gapr::WebCache::fetch(const char* url, const FetchCb& cb) {
	return FetchLock{this, _fetches_fresh.insert(_fetches_fresh.end(), FetchRequest{url, cb, ReqState::Fresh})};
}
gapr::WebCache::PrefetchLock gapr::WebCache::prefetch(const char* url, const PrefetchCb& cb) {
	return PrefetchLock{this, _prefetches_fresh.insert(_prefetches_fresh.end(), PrefetchRequest{url, cb, ReqState::Fresh})};
}
void gapr::WebCache::cancel(gapr::WebCache::FetchLock::Req req) {
	switch(req->state) {
		case ReqState::Fresh:
			_fetches_fresh.erase(req);
			break;
		case ReqState::Queued:
			_fetches_queued.erase(req);
			break;
		case ReqState::Active:
			{
				auto downloading=req->downloading;
				downloading->fetch_reqs.erase(req);
				if(downloading->fetch_reqs.empty()) {
					_nfetches--;
					if(!downloading->prefetch_reqs.empty())
						_nprefetches++;
				}
			}
			break;
		case ReqState::Trashed:
			dustbin_for_fetches.erase(req);
			break;
	}
}
void gapr::WebCache::cancel(gapr::WebCache::PrefetchLock::Req req) {
	switch(req->state) {
		case ReqState::Fresh:
			_prefetches_fresh.erase(req);
			break;
		case ReqState::Queued:
			_prefetches_queued.erase(req);
			break;
		case ReqState::Active:
			{
				auto downloading=req->downloading;
				downloading->prefetch_reqs.erase(req);
				if(downloading->fetch_reqs.empty()) {
					if(downloading->prefetch_reqs.empty()) {
						_nprefetches--;
					}
				}
			}
			break;
		case ReqState::Trashed:
			dustbin_for_prefetches.erase(req);
			break;
	}
}
void gapr::WebCache::handle_fresh() {
	for(auto j=_fetches_fresh.begin(); j!=_fetches_fresh.end(); ) {
		auto i=j;
		j++;
		auto f=_files.find(i->url);
		if(f!=_files.end()) {
			f->second.ts=time(nullptr);
			if(i->cb)
				i->cb(nullptr, f->second.file);
			i->state=ReqState::Trashed;
			dustbin_for_fetches.splice(dustbin_for_fetches.end(), _fetches_fresh, i);
		} else {
			auto d=_downloads.find(i->url);
			if(d!=_downloads.end()) {
				i->state=ReqState::Active;
				auto dd=d->second;
				dd->fetch_reqs.splice(dd->fetch_reqs.end(), _fetches_fresh, i);
			} else {
				i->state=ReqState::Queued;
				_fetches_queued.splice(_fetches_queued.end(), _fetches_fresh, i);
			}
		}
	}
	for(auto j=_prefetches_fresh.begin(); j!=_prefetches_fresh.end(); ) {
		auto i=j;
		j++;
		auto f=_files.find(i->url);
		if(f!=_files.end()) {
			f->second.ts=time(nullptr);
			if(i->cb)
				i->cb(nullptr);
			i->state=ReqState::Trashed;
			dustbin_for_prefetches.splice(dustbin_for_prefetches.end(), _prefetches_fresh, i);
		} else {
			auto d=_downloads.find(i->url);
			if(d!=_downloads.end()) {
				i->state=ReqState::Active;
				auto dd=d->second;
				dd->prefetch_reqs.splice(dd->prefetch_reqs.end(), _prefetches_fresh, i);
			} else {
				i->state=ReqState::Queued;
				_prefetches_queued.splice(_prefetches_queued.end(), _prefetches_fresh, i);
			}
		}
	}
}

void gapr::WebCache::flush() {
	handle_fresh();
	handle_queue();
}

bool extract_url(const std::string& str, std::string* url, std::string* site, std::string* usr, std::string* pw) {
	static std::regex re{"^([^:]+):(//(([^:@]+)(:([.]*))?@)?([^@/:?#]*)(:[^:/?#]+)?)?(/[^?#]*)?(\\?[^#]*)?(#[.]*)?$"};
	std::smatch match;
	if(!regex_match(str, match, re))
		return false;
	(*site)=match[1];
	(*site)+=':';
	if(match.length(2)>0) {
		(*site)+="//";
		(*site)+=match[7];
		(*site)+=match[8];
		(*usr)=match[4];
		(*pw)=match[6];
	}
	(*url)=(*site);
	(*url)+=match[9];
	(*url)+=match[10];
	(*url)+=match[11];
	return true;
}
void gapr::WebCache::add_downloading(FetchReq iter) {
	std::string url, site, usr, pw;
	if(!extract_url(iter->url, &url, &site, &usr, &pw)) {
		gapr::print("Failed to parse url: ", iter->url);
		if(iter->cb)
			iter->cb("Failed to parse url", CacheFile{});
		dustbin_for_fetches.splice(dustbin_for_fetches.end(), _fetches_queued, iter);
		return;
	}

	std::unique_ptr<Downloading> dl{new Downloading{_curlm, url.c_str(), site.c_str()}};
	auto i=_logins.find(site);
	if(i!=_logins.end()) {
		if(i->second.verErr<i->second.ver) {
			dl->resume(i->second.usr.c_str(), i->second.pwd.c_str(), i->second.ver);
			_nactive++;
		}
	} else if(!usr.empty()) {
		_logins[site]=LoginInfo{usr, pw, 0, -1};
		dl->resume(usr.c_str(), pw.c_str(), 0);
		_nactive++;
	} else {
		dl->resume(nullptr, nullptr, 0);
		_nactive++;
	}
	iter->state=ReqState::Active;
	dl->fetch_reqs.splice(dl->fetch_reqs.end(), _fetches_queued, iter);
	_nfetches++;
	_downloads[dl->url]=dl.release();
}
void gapr::WebCache::add_downloading(PrefetchReq iter) {
	std::string url, site, usr, pw;
	if(!extract_url(iter->url, &url, &site, &usr, &pw)) {
		gapr::print("Failed to parse url: ", iter->url);
		if(iter->cb)
			iter->cb("Failed to parse url");
		dustbin_for_prefetches.splice(dustbin_for_prefetches.end(), _prefetches_queued, iter);
		return;
	}

	std::unique_ptr<Downloading> dl{new Downloading{_curlm, url.c_str(), site.c_str()}};
	auto i=_logins.find(site);
	if(i!=_logins.end()) {
		if(i->second.verErr<i->second.ver) {
			dl->resume(i->second.usr.c_str(), i->second.pwd.c_str(), i->second.ver);
			_nactive++;
		}
	} else if(!usr.empty()) {
		_logins[site]=LoginInfo{usr, pw, 0, -1};
		dl->resume(usr.c_str(), pw.c_str(), 0);
		_nactive++;
	} else {
		dl->resume(nullptr, nullptr, 0);
		_nactive++;
	}
	iter->state=ReqState::Active;
	dl->prefetch_reqs.splice(dl->prefetch_reqs.end(), _prefetches_queued, iter);
	_nfetches++;
	_downloads[dl->url]=dl.release();
}

void gapr::WebCache::handle_queue() {
	if(_nfetches<_max_fetches && _fetches_queued.size()>0) {
		add_downloading(_fetches_queued.begin());
		for(auto j=_fetches_queued.begin(); j!=_fetches_queued.end(); ) {
			auto i=j; j++;
			auto d=_downloads.find(i->url);
			if(d!=_downloads.end()) {
				i->state=ReqState::Active;
				auto dd=d->second;
				dd->fetch_reqs.splice(dd->fetch_reqs.end(), _fetches_queued, i);
			} else if(_nfetches<_max_fetches) {
				add_downloading(i);
			}
		}
		for(auto j=_prefetches_queued.begin(); j!=_prefetches_queued.end(); ) {
			auto i=j; j++;
			auto d=_downloads.find(i->url);
			if(d!=_downloads.end()) {
				i->state=ReqState::Active;
				auto dd=d->second;
				dd->prefetch_reqs.splice(dd->prefetch_reqs.end(), _prefetches_queued, i);
			}
		}
	}
	if(_nfetches>0) {
		if(_nprefetches>0) {
			// cancel prefetches
		}
	} else {
		if(_nprefetches<_max_prefetches && _prefetches_queued.size()>0) {
			add_downloading(_prefetches_queued.begin());
			for(auto j=_prefetches_queued.begin(); j!=_prefetches_queued.end(); ) {
				auto i=j; j++;
				auto d=_downloads.find(i->url);
				if(d!=_downloads.end()) {
					i->state=ReqState::Active;
					auto dd=d->second;
					dd->prefetch_reqs.splice(dd->prefetch_reqs.end(), _prefetches_queued, i);
				} else if(_nprefetches<_max_prefetches) {
					add_downloading(i);
				}
			}
		}
	}

	// hi_que lo_que dl

	// ing hi
	// q hi
	// -----------
	// ing lo
	// q lo
	//
}
	////////////
	//cb
	//0 d
	//ncb
	//0 d
		//auto key=url.toString(QUrl::RemovePassword|QUrl::RemoveUserInfo|QUrl::NormalizePathSegments);
		//auto key=url.toString(QUrl::RemovePassword|QUrl::RemoveUserInfo|QUrl::NormalizePathSegments);


int gapr::WebCache_PRIV::Downloading::socket_callback(CURL *easy, curl_socket_t s, int what, void *userp, void *socketp) noexcept {
	Downloading* dl;
	auto r=curl_easy_getinfo(easy, CURLINFO_PRIVATE, &dl);
	if(r)
		gapr::report("curl_easy_getinfo(): ", curl_easy_strerror(r));
	auto wc=static_cast<WebCache*>(userp);
	auto poll=static_cast<PollContext*>(socketp);
	int events;
	switch(what) {
		case CURL_POLL_NONE:
			events=0;
			break;
		case CURL_POLL_IN:
			events=UV_READABLE;
			break;
		case CURL_POLL_OUT:
			events=UV_WRITABLE;
			break;
		case CURL_POLL_INOUT:
			events=UV_WRITABLE|UV_READABLE;
			break;
		case CURL_POLL_REMOVE:
			if(poll) {
				if(auto r=uv_poll_stop(&poll->poll))
					gapr::report("uv_poll_stop(): ", uv_strerror(r));

				dustbin_for_polls.splice(dustbin_for_polls.end(), dl->polls, poll->iter);
				uv_close(reinterpret_cast<uv_handle_t*>(&poll->poll), poll_close_cb);
				if(auto r=curl_multi_assign(wc->_curlm, s, nullptr))
					gapr::report("curl_multi_assign(): ", curl_multi_strerror(r));
			}
			return 0;
		default:
			gapr::report("Not possible");
			return 0;
	}

	if(!poll) {
		auto it=dl->polls.insert(dl->polls.end(), PollContext{});
		poll=&(*it);
		poll->socket=s;
		poll->wc=wc;
		poll->iter=it;
		if(auto r=uv_poll_init_socket(wc->_loop, &poll->poll, s))
			gapr::report("uv_poll_init_socket(): ", uv_strerror(r));
		poll->poll.data=poll;
		if(auto r=curl_multi_assign(wc->_curlm, s, poll))
			gapr::report("curl_multi_assign(): ", curl_multi_strerror(r));
	}
	if(auto r=uv_poll_start(&poll->poll, events, poll_cb))
		gapr::report("uv_poll_start(): ", uv_strerror(r));
	return 0;
}
int gapr::WebCache_PRIV::Downloading::timer_callback(CURLM *multi, long timeout_ms, void *userp) noexcept {
	auto wc=static_cast<WebCache*>(userp);
	if(timeout_ms<0) {
		if(auto r=uv_timer_stop(wc->_timer))
			gapr::report("uv_timer_stop(): ", uv_strerror(r));
	} else {
		if(timeout_ms==0)
			timeout_ms=1;
		auto r=uv_timer_start(wc->_timer, timer_cb, timeout_ms, 0);
		if(r)
			gapr::report("uv_timer_start(): ", uv_strerror(r));
	}
	return 0; // XXX
}
void gapr::WebCache_PRIV::Downloading::poll_cb(uv_poll_t* handle, int status, int events) noexcept {
	auto ctx=static_cast<PollContext*>(handle->data);
	int running_handles;
	int bits=0;
	if(status<0) {
		bits=CURL_CSELECT_ERR;
	} else {
		if(events&UV_READABLE)
			bits|=CURL_CSELECT_IN;
		if(events&UV_WRITABLE)
			bits|=CURL_CSELECT_OUT;
	}
	auto r=curl_multi_socket_action(ctx->wc->_curlm, ctx->socket, bits, &running_handles);
	if(r)
		gapr::report("curl_multi_socket_action(): ", curl_multi_strerror(r));
	if(running_handles<ctx->wc->_nactive)
		ctx->wc->handle_curlinfo();
}
void gapr::WebCache_PRIV::Downloading::timer_cb(uv_timer_t* handle) noexcept {
	auto wc=static_cast<WebCache*>(handle->data);
	int running_handles;
	auto r=curl_multi_socket_action(wc->_curlm, CURL_SOCKET_TIMEOUT, 0, &running_handles);
	if(r)
		gapr::report("curl_multi_socket_action(): ", curl_multi_strerror(r));
	if(running_handles<wc->_nactive)
		wc->handle_curlinfo();
}
void gapr::WebCache::handle_finished(Downloading* _dl, const char* err) {
	std::unique_ptr<Downloading> dl{_dl};
	auto iter=_downloads.find(dl->url);
	if(iter==_downloads.end()) {
		gapr::report("corrupted");
	} else if(iter->second!=_dl) {
		gapr::report("corrupted");
	} else {
		_downloads.erase(iter);
	}

	CacheFile file;
	if(!err) {
		file=dl->finish_file();
		_files[dl->url]=Downloaded{time(nullptr), file};
	}

	if(!dl->fetch_reqs.empty()) {
		for(auto j=dl->fetch_reqs.begin(); j!=dl->fetch_reqs.end(); ) {
			auto i=j; j++;
			if(i->cb)
				i->cb(err, file);
			dustbin_for_fetches.splice(dustbin_for_fetches.begin(), dl->fetch_reqs, i);
		}
	}
	if(!dl->prefetch_reqs.empty()) {
		for(auto j=dl->prefetch_reqs.begin(); j!=dl->prefetch_reqs.end(); ) {
			auto i=j; j++;
			if(i->cb)
				i->cb(err);
			dustbin_for_prefetches.splice(dustbin_for_prefetches.begin(), dl->prefetch_reqs, i);
		}
	}

	dl->destroy();
}
void gapr::WebCache::handle_login_denied(Downloading* _dl) {
	auto i=_logins.find(_dl->site);
	/////////
	///////////////
	if(i==_logins.end()) {
		_logins[_dl->site]={{}, {}, 0, 0};
		if(_password_cb) {
			_dl->pause();
			_password_cb(_dl->site.c_str(), nullptr, nullptr);
		} else {
			handle_finished(_dl, "No login info");
		}
	} else {
		if(i->second.verErr<i->second.ver) {
			if(_dl->login_ver<i->second.ver) {
				_dl->pause();
				_dl->resume(i->second.usr.c_str(), i->second.pwd.c_str(), i->second.ver);
				_dl->pos=0;
			} else {
				i->second.verErr=_dl->login_ver;
				if(_password_cb) {
					_dl->pause();
					_password_cb(_dl->site.c_str(), i->second.usr.c_str(), i->second.pwd.c_str());
				} else {
					handle_finished(_dl, "No login info");
				}
			}
		} else {
			_dl->pause();
		}
	}
}
void gapr::WebCache::set_password(const char* site, const char* usr, const char* pw) {
	bool resume=usr && *usr!='\0';
	std::string key{site};
	auto i=_logins.find(key);
	int ver;
	if(i==_logins.end()) {
		_logins[key]=LoginInfo{usr, pw, 0, -1};
		ver=0;
	} else {
		if(resume) {
			i->second.usr=usr;
			i->second.pwd=pw;
		}
		i->second.ver++;
		ver=i->second.ver;
	}
	for(auto& p: _downloads) {
		auto dl=p.second;
		if(dl->paused && dl->site==key) {
			if(resume) {
				dl->resume(usr, pw, ver);
				dl->pos=0;
			} else {
				handle_finished(dl, "Canceled");
			}
		}
	}
}
void gapr::WebCache::handle_curlinfo() {
	int msgs_left;
	while(auto msg=curl_multi_info_read(_curlm, &msgs_left)) {
		if(msg->msg==CURLMSG_DONE) {
			Downloading* dl;
			if(auto r=curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &dl))
				gapr::report("curl_easy_getinfo(): ", curl_easy_strerror(r));
			gapr::print("msg.data.result: ", msg->data.result);
			//if(p==items.end())
				//gapr::report("Unexpected missing item");
			switch(msg->data.result) {
				case CURLE_OK:
					{
						long httpCode=0;
						if(curl_easy_getinfo(dl->curl, CURLINFO_RESPONSE_CODE, &httpCode)==CURLE_OK && httpCode==401) {
							handle_login_denied(dl);
						} else {
							handle_finished(dl, nullptr);
						}
					}
					break;
				case CURLE_LOGIN_DENIED:
					handle_login_denied(dl);
					break;
#if 0
				case TIMEOUT:
					// reset
					// redo
					break;
#endif
				default:
					handle_finished(dl, curl_easy_strerror(msg->data.result));
					//////////////////////////
	//

			////////
			}
		}
	}


}


	////////////


#endif

#if 0
			tryMakeSpace();
	size_t memUsage() const {
		return data.size()*BUFSIZ;
	}


struct CacheThreadPriv {
	void tryMakeSpace() {
		int64_t maxSize;
		{
			std::unique_lock<std::mutex> lck{mtx_configs};
			maxSize=max_size;
		}
		std::unique_lock<std::mutex> lck{mtx_files};
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



};

#endif
#endif
#endif


