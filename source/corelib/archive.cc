#include "gapr/archive.hh"

#include <mutex>

#include <lmdb.h>

#include "gapr/utility.hh"
#include "gapr/detail/streambuf.hh"

using archive=gapr::archive;

namespace {
	struct mdb_env_deleter {
		void operator()(MDB_env* env) const {
			//* XXX Only a single thread may call this function???
			::mdb_env_close(env);
		}
	};
	struct mdb_txn_deleter {
		void operator()(MDB_txn* txn) const {
			mdb_txn_abort(txn);
		}
	};
	struct mdb_cursor_deleter {
		void operator()(MDB_cursor* cursor) const {
			mdb_cursor_close(cursor);
		}
	};
}

inline static unsigned int update_offset(char* ptr, std::size_t siz, std::size_t o) noexcept {
	assert(8<siz);
	ptr[0]='\xff';
	unsigned int i=1;
	while(o>=0x80) {
		ptr[i++]=((o&0x7f)|0x80);
		o>>=7;
	}
	ptr[i++]=o;
	return i;
}

struct archive::PRIV {
	std::unique_ptr<MDB_env, mdb_env_deleter> _env;
	MDB_dbi _dbi;
	std::mutex _mtx;

	explicit PRIV(const char* path, bool rdonly) {
		MDB_env* env;
		if(auto r=mdb_env_create(&env); r!=0)
			gapr::report("error env_create: ", r);
		std::unique_ptr<MDB_env, mdb_env_deleter> env_{env};

		if(auto r=mdb_env_set_mapsize(env, 2'097'152'000L); r!=0)
			gapr::report("error env_set_mapsize: ", r);
		//XXX mdb_env_set_maxreaders()
		//XXX mdb_env_set_maxdbs()
		if(auto r=mdb_env_open(env, path, MDB_NOSUBDIR|MDB_NOTLS|(rdonly?MDB_RDONLY:0), 0640); r!=0)
			gapr::report("error env_open: ", r);

		MDB_txn* txn;
		if(auto r=mdb_txn_begin(env, nullptr, MDB_RDONLY, &txn); r!=0)
			gapr::report("error txn_begin: ", r);
		std::unique_ptr<MDB_txn, mdb_txn_deleter> txn_{txn};
		MDB_dbi dbi;
		{
			std::lock_guard lck{_mtx};
			if(auto r=mdb_dbi_open(txn, nullptr, 0, &dbi); r!=0)
				gapr::report("error dbi_open: ", r);
			txn_.release();
			if(auto r=mdb_txn_commit(txn); r!=0)
				gapr::report("error dbi_open commit: ", r);
		}
		_dbi=dbi;
		_env=std::move(env_);
	}
	~PRIV() {
		//XXX void mdb_dbi_close(MDB_env *env, MDB_dbi dbi);
	}

	void begin_buffered(unsigned int level) {
		unsigned int flags;
		switch(level) {
			case 0:
				flags=0;
				break;
			case 1:
				flags=MDB_NOMETASYNC;
				break;
			default:
				flags=MDB_NOMETASYNC|MDB_NOSYNC;
		}
		if(flags)
			if(auto r=mdb_env_set_flags(_env.get(), flags, 1); r!=0)
				gapr::report("failed to env_set_flags: ", r);
		flags=(~flags)&(MDB_NOMETASYNC|MDB_NOSYNC);
		if(flags)
			if(auto r=mdb_env_set_flags(_env.get(), flags, 0); r!=0)
				gapr::report("failed to env_set_flags: ", r);
	}
	bool end_buffered() {
		unsigned int flags=MDB_NOMETASYNC|MDB_NOSYNC;
		if(auto r=mdb_env_set_flags(_env.get(), flags, 0); r!=0)
			gapr::report("failed to env_set_flags: ", r);
		if(auto r=mdb_env_sync(_env.get(), 1); r!=0) {
			gapr::print("failed to env_sync: ", r);
			return false;
		}
		return true;
	}

	struct WriterImpl;
	struct ReaderImpl;
	class ArchiveIn;
};

// XXX delete file???
// int  mdb_cursor_del(MDB_cursor *cursor, unsigned int flags);

archive::archive(const char* path, bool rdonly):
	_p{std::make_shared<PRIV>(path, rdonly)}
{ }
void archive::begin_buffered(unsigned int level) const {
	return _p->begin_buffered(level);
}
bool archive::end_buffered() {
	return _p->end_buffered();
}

struct archive::PRIV::WriterImpl: archive::writer_DATA {
	std::shared_ptr<PRIV> _ref;
	std::unique_ptr<MDB_txn, mdb_txn_deleter> _txn;
	MDB_cursor* _cursor;
	std::array<char, 512> _key;
	std::size_t _key_len;

	WriterImpl(const std::shared_ptr<PRIV>& apriv, std::string_view key):
		writer_DATA{1, 0}, _ref{apriv}
	{
		MDB_txn* txn;
		if(auto r=mdb_txn_begin(apriv->_env.get(), nullptr, 0, &txn); r!=0)
			gapr::report("error txn_begin: ", r);
		std::unique_ptr<MDB_txn, mdb_txn_deleter> txn_{txn};
		if(auto r=mdb_cursor_open(txn, apriv->_dbi, &_cursor); r!=0)
			gapr::report("error cursor_open: ", r);
		_key_len=key.size();
		if(_key_len+9>_key.size())
			_key_len=_key.size()-9;
		std::memcpy(&_key[0], key.data(), _key_len);
		_txn=std::move(txn_);
	}
	~WriterImpl() { }
	void do_commit(std::size_t siz) {
		auto offset_len=update_offset(&_key[_key_len], _key.size()-_key_len, _o);
		MDB_val key{_key_len+offset_len, _key.data()};
		MDB_val data{siz, &_buf[0]};
		if(auto r=mdb_cursor_put(_cursor, &key, &data, MDB_NOOVERWRITE); r!=0)
			gapr::report("error cursor_put: ", r);
	}
	void do_commit2() {
		_buf[0]='\xff';
		do_commit(CHUNK_SIZE);
		_o+=CHUNK_SIZE-1;
		_i-=CHUNK_SIZE-1;
		if(_i>1)
			std::memcpy(&_buf[1], &_buf[CHUNK_SIZE], _i-1);
	}
	bool do_flush() {
		_buf[0]='\x00';
		do_commit(_i);
		_o+=_i-1;
		_i=1;
		if(auto r=mdb_txn_commit(_txn.release()); r!=0)
			return false;
		return true;
	}
};

archive::writer::writer(const std::shared_ptr<PRIV>& apriv, std::string_view key):
	_p{std::make_unique<PRIV::WriterImpl>(apriv, key)}
{ }
archive::writer::~writer() {
	std::unique_ptr<PRIV::WriterImpl> p{static_cast<PRIV::WriterImpl*>(_p.release())};
}
void archive::writer::do_commit() {
	return static_cast<PRIV::WriterImpl*>(_p.get())->do_commit2();
}
bool archive::writer::flush() {
	return static_cast<PRIV::WriterImpl*>(_p.get())->do_flush();
}

struct archive::PRIV::ReaderImpl: reader_DATA {
	std::shared_ptr<PRIV> _ref;
	std::unique_ptr<MDB_txn, mdb_txn_deleter> _txn;
	std::unique_ptr<MDB_cursor, mdb_cursor_deleter> _cursor;
	std::array<char, 512> _key;
	std::size_t _key_len;

	const char* _data{nullptr};
	std::size_t _data_len;
	std::size_t _data_off;
	std::size_t _cur_off{0};
	std::array<char, 128*2> _gap;
	std::size_t _gap_len;

	ReaderImpl(const std::shared_ptr<PRIV>& apriv, std::string_view key):
		reader_DATA{nullptr, 0, 1}, _ref{apriv}
	{
		MDB_txn* txn;
		if(auto r=mdb_txn_begin(apriv->_env.get(), nullptr, MDB_RDONLY, &txn); r!=0)
			gapr::report("error txn_begin: ", r);
		std::unique_ptr<MDB_txn, mdb_txn_deleter> txn_{txn};
		MDB_cursor* cursor;
		if(auto r=mdb_cursor_open(txn, apriv->_dbi, &cursor); r!=0)
			gapr::report("error cursor_open: ", r);
		std::unique_ptr<MDB_cursor, mdb_cursor_deleter> cursor_{cursor};
		_key_len=key.size();
		if(_key_len+9>_key.size())
			_key_len=_key.size()-9;
		std::memcpy(&_key[0], key.data(), _key_len);
		_txn=std::move(txn_);
		_cursor=std::move(cursor_);
	}
	~ReaderImpl() { }

	template<typename F> auto get_buffer(std::size_t off, F handle_err) {
		assert(!_data || _data[0]);
		auto offset_len=update_offset(&_key[_key_len], _key.size()-_key_len, off);
		MDB_val key{_key_len+offset_len, _key.data()};
		MDB_val data;
		if(auto r=mdb_cursor_get(_cursor.get(), &key, &data, MDB_SET_KEY); r!=0)
			return handle_err(r);
		_data=static_cast<const char*>(data.mv_data);
		_data_len=data.mv_size;
		_data_off=off;
		assert(_data[0]=='\x00' || _data[0]=='\xff');
		assert(_data_len>(off==0?0:1));
		return handle_err('0');
	}
	void get_buffer2(std::size_t off) {
		get_buffer(off, [](auto r) ->void {
			if constexpr(sizeof(r)==sizeof(int)) {
				gapr::report("error cursor_get: ", r);
			}
		});
	}
	bool check_exist() {
		assert(!_data);
		assert(_cur_off==0);
		auto r=get_buffer(0, [](auto r) ->bool {
			if constexpr(sizeof(r)==sizeof(int)) {
				if(r==MDB_NOTFOUND)
					return false;
				gapr::report("error cursor_get: ", r);
			}
			return true;
		});
		if(!r)
			return false;
		_idx=0;
		_siz=_data_len-1;
		if(_siz==0) {
			_buf=nullptr;
		} else {
			_buf=_data+1;
		}
		return true;
	}
	std::pair<const char*, std::size_t> do_buffer2() {
		assert(_data);
		assert(_siz-_idx<128);
		assert(_cur_off<=_data_off);
		if(_cur_off==_data_off) {
			assert(_siz==_data_len-1);
			if(!_data[0]) {
				_buf=_data+1;
				if(_idx==_siz)
					return {nullptr, 0};
				return {_buf+_idx, _siz-_idx};
			}
			_cur_off=_data_off+_idx;
			if(_siz-_idx>0)
				std::memcpy(_gap.data(), &_data[_idx+1], _siz-_idx);
			get_buffer2(_data_off+_data_len-1);
			if(_cur_off==_data_off) {
				_buf=_data+1;
				_idx=0;
				_siz=_data_len-1;
				return {_buf, _siz};
			}
			unsigned int tocopy=127;
			if(tocopy>_data_len-1)
				tocopy=_data_len-1;
			std::memcpy(_gap.data()+(_data_off-_cur_off), _data+1, tocopy);
			_gap_len=_data_off-_cur_off+tocopy;
			_buf=_gap.data();
			_idx=0;
			_siz=_gap_len;
			return {_buf, _siz};
		} else {
			assert(_gap_len==_siz);
			if(_cur_off+_idx<_data_off) {
				if(_siz==_idx)
					return {nullptr, 0};
				_buf=_gap.data();
				return {_buf+_idx, _siz-_idx};
			}
			_idx=_cur_off+_idx-_data_off;
			_cur_off=_data_off;
			_buf=_data+1;
			_siz=_data_len-1;
			return {_buf+_idx, _siz-_idx};
		}
	}
	std::pair<const char*, std::size_t> do_buffer3() {
		if(_data && _idx+1+128<=_data_len)
			return {_data+1+_idx, _data_len-1-_idx};
		return do_buffer2();
	}
};

archive::reader::reader(const std::shared_ptr<PRIV>& apriv, std::string_view key): _p{}
{
	auto p=std::make_unique<PRIV::ReaderImpl>(apriv, key);
	if(p->check_exist())
		_p=std::move(p);
}
archive::reader::~reader() {
	std::unique_ptr<PRIV::ReaderImpl> p{static_cast<PRIV::ReaderImpl*>(_p.release())};
}
std::pair<const char*, std::size_t> archive::reader::do_buffer() {
	return static_cast<PRIV::ReaderImpl*>(_p.get())->do_buffer2();
}

#if 0
// XXX reopen a reader
void reopen() cno {
	mdb_txn_reset(_txn);
	if(auto r=mdb_txn_renew(_txn); r!=0)
		gapr::report("error txn_renew: ", r);
	if(auto r=mdb_cursor_renew(_txn, _cursor); r!=0)
		gapr::report("error cursor_renew: ", r);

	//#mdb_txn_renew
	//#mdb_cursor_renew
}
#endif

class archive::PRIV::ArchiveIn {
	public:
		ArchiveIn(std::unique_ptr<archive::PRIV::ReaderImpl>&& file): _file{std::move(file)}, _eof{false} { }
		~ArchiveIn() { }
		void close(std::error_code& ec) {
			assert(_file);
			_file={};
		}
		bool eof() { return _eof; }
		std::size_t fread(void* ptr, std::size_t size, std::error_code& ec) {
			if(size<=0)
				return 0;
			std::size_t ret=0;
			do {
				const void* buf;
				auto n=fmap(&buf, ec);
				if(ec)
					return ret;
				if(n<=0)
					break;
				if(n<=size) {
					std::memcpy(ptr, buf, n);
					ptr=static_cast<char*>(ptr)+n;
					size-=n;
					ret+=n;
				} else {
					std::memcpy(ptr, buf, size);
					ret+=size;
					auto x=n-size;
					_file->_idx-=x;
					break;
				}
			} while(size>0);
			return ret;
		}
		std::size_t fmap(const void** pptr, std::error_code& ec) {
		try {
			if(_eof) {
				return 0;
			}
			auto r=_file->do_buffer3();
			if(!r.first) {
				_eof=true;
				return 0;
			}
			_file->_idx+=r.second;
			*pptr=r.first;
			return r.second;
		} catch(const std::exception& e) {
			fprintf(stderr, "exception: %s\n", e.what());
			throw;
		}
		}
		std::size_t prepare(void** pptr, std::error_code& ec) {
			ec=std::error_code{EBADF, std::generic_category()};
			return 0;
		}
		void fwrite(const void* ptr, std::size_t size, std::error_code& ec) {
			ec=std::error_code{EBADF, std::generic_category()};
		}
		void commit(std::size_t size, std::error_code& ec) {
			ec=std::error_code{EBADF, std::generic_category()};
		}
		void fflush(std::error_code& ec) {
			ec=std::error_code{EBADF, std::generic_category()};
		}
		void fseek(off_t offset, int whence, std::error_code& ec) {
			switch(whence) {
				case SEEK_SET:
					break;
				case SEEK_CUR:
					// XXX
					break;
				case SEEK_END:
					break;
				default:
					assert(0);
			}
			ec=std::error_code{EINVAL, std::generic_category()};
		}
		off_t ftell(std::error_code& ec) {
			return _file->_idx+_file->_cur_off;
		}
		std::pair<std::size_t, bool> remain() {
			return {_file->_siz-_file->_idx, _eof};
		}
	private:
		std::unique_ptr<archive::PRIV::ReaderImpl> _file;
		bool _eof;
};

template class gapr::Streambuf_Input<archive::PRIV::ArchiveIn>;

std::unique_ptr<std::streambuf> archive::reader_streambuf(std::string_view key) const {
	auto p=std::make_unique<PRIV::ReaderImpl>(_p, key);
	if(!p->check_exist())
		return {};
	return std::make_unique<gapr::Streambuf_Input<archive::PRIV::ArchiveIn>>(std::move(p));
}

