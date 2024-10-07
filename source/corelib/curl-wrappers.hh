#include <curl/curl.h>

namespace gapr {

class curl_slist {
public:
	constexpr curl_slist() noexcept: _p{nullptr} { }
	~curl_slist() {
		if(_p)
			::curl_slist_free_all(_p);
	}
	curl_slist(const curl_slist&) =delete;
	curl_slist& operator=(const curl_slist&) =delete;
	constexpr curl_slist(curl_slist&& r) noexcept: _p{r._p} {
		r._p=nullptr;
	}
	curl_slist& operator=(curl_slist&& r) noexcept {
		std::swap(_p, r._p);
		return *this;
	}

	explicit operator struct ::curl_slist*() const noexcept { return _p; }
	struct ::curl_slist* lower() const noexcept { return _p; }

	void append(const char* str) {
		auto tmp=::curl_slist_append(_p, str);
		if(!tmp)
			throw std::bad_alloc{};
		_p=tmp;
	}

private:
	struct ::curl_slist* _p;
};

class curl_str {
public:
	constexpr curl_str() noexcept: _p{nullptr}, _l{0} { }
	~curl_str() {
		if(_p)
			::curl_free(_p);
	}
	curl_str(const curl_str&) =delete;
	curl_str& operator=(const curl_str&) =delete;
	constexpr curl_str(curl_str&& r) noexcept: _p{r._p}, _l{r._l} {
		r._p=nullptr; r._l=0;
	}
	curl_str& operator=(curl_str&& r) noexcept {
		std::swap(_p, r._p);
		std::swap(_l, r._l);
		return *this;
	}

	explicit operator bool() const noexcept {
		return _p;
	}

	std::string string() const {
		if(_l!=SIZE_MAX)
			return std::string{_p, _l};
		std::string r{_p};
		_l=r.size();
		return r;
	}
	explicit operator std::string() const { return string(); }
	std::string_view string_view() const noexcept {
		if(_l!=SIZE_MAX)
			return std::string_view{_p, _l};
		std::string_view r{_p};
		_l=r.size();
		return r;
	}
	operator std::string_view() const noexcept { return string_view(); }

private:
	char* _p;
	mutable std::size_t _l;
	constexpr explicit curl_str(char* s) noexcept: _p{s}, _l{s?SIZE_MAX:0} { }
	friend class curl_url;
};

class curl_url {
public:
	constexpr curl_url() noexcept: _u{nullptr} { }
	explicit curl_url(std::nullptr_t): _u{::curl_url()} {
		if(!_u)
			throw std::bad_alloc{};
	}
	explicit curl_url(const char* u): curl_url{nullptr} { set_field(CURLUPART_URL, u); }
	explicit curl_url(std::string_view u): curl_url{nullptr} { set_field(CURLUPART_URL, u.data()); }
	~curl_url() {
		if(_u) ::curl_url_cleanup(_u);
	}
	curl_url(const curl_url&) =delete;
	curl_url& operator=(const curl_url&) =delete;
	curl_url(curl_url&& r) noexcept: _u{r._u} { r._u=nullptr; }
	curl_url& operator=(curl_url&& r) noexcept {
		std::swap(_u, r._u);
		return *this;
	}

	curl_url dup() const;

	void user(const char* v) { set_field(CURLUPART_USER, v); }
	curl_str user() const { return get_field(CURLUPART_USER); }
	void password(const char* v) { set_field(CURLUPART_PASSWORD, v); }
	curl_str password() const { return get_field(CURLUPART_PASSWORD); }
	void path(const char* v) { set_field(CURLUPART_PATH, v); }
	curl_str path() const { return get_field(CURLUPART_PATH); }
	void query(const char* v) { set_field(CURLUPART_QUERY, v); }
	curl_str query() const { return get_field(CURLUPART_QUERY); }
	void fragment(const char* v) { set_field(CURLUPART_FRAGMENT, v); }
	curl_str fragment() const { return get_field(CURLUPART_FRAGMENT); }
	void url(const char* v) { set_field(CURLUPART_URL, v); }
	curl_str url() const { return get_field(CURLUPART_URL); }
	explicit operator curl_str() const { return get_field(CURLUPART_URL); }

	curl_url& cannolize() {
		user(nullptr);
		password(nullptr);
		return *this;
	}
	curl_url& no_path() {
		path("/");
		query(nullptr);
		fragment(nullptr);
		return *this;
	}

private:
	CURLU* _u;
	void set_field(CURLUPart pt, const char* v);
	curl_str get_field(CURLUPart pt) const;
};

}

