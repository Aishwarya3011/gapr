#include <cstring>
#include <streambuf>

namespace gapr {

// XXX replace gapr::Streambuf_Output/Input
template<typename SubT>
class streambuf_impl: public std::streambuf {
protected:
#if 0
	template<typename... Args>
	streambuf_impl(Args&&... args):
		std::streambuf{}, _impl{std::forward<Args>(args)...}
	{ }
#endif
	explicit streambuf_impl(std::ios::openmode mode): std::streambuf{}, _mode{mode} { }
	~streambuf_impl() {
		if(_mode&std::ios::out) {
			if(pbase()<pptr()) {
				std::error_code ec{};
				//this this
				SubT::adapter_commit(subtype(), pptr()-pbase(), ec);
			}
		}
	}

#if 0
	void close() override {
		std::error_code ec{};
		if(_mode&std::ios::out)
			flush_buffer(ec);
		SubT::adapter_close(subtype(), ec);
		if(ec)
			throw std::system_error{ec};
	}
#endif

protected:
	std::streampos seekoff(std::streamoff off, std::ios_base::seekdir way, std::ios_base::openmode which) override {
		std::error_code ec{};
		which&=std::ios_base::out|std::ios::in;
		if(which==0)
			return -1;
		int whence=SEEK_SET;
		if(way==std::ios_base::cur) {
			if(which==std::ios::out) {
				if(off==0)
					return ftell(ec)+pptr()-pbase();
			} else if(which==std::ios::in) {
				if(off==0)
					return ftell(ec)+gptr()-egptr();
				if(pbase()>=pptr() && off+gptr()<=egptr() && off+gptr()>=eback()) {
					gbump(off);
					return ftell(ec)+gptr()-egptr();
				}
				off-=egptr()-gptr();
			} else {
				return -1;
			}
			whence=SEEK_CUR;
		} else if(way==std::ios_base::end) {
			whence=SEEK_END;
		}
		if(_mode&std::ios::out)
			flush_buffer(ec);
		fseek(off, whence, ec);
		if(_mode&std::ios::in)
			setg(nullptr, nullptr, nullptr);
		return ftell(ec);
	}

	std::streampos seekpos(std::streampos sp, std::ios_base::openmode which) override {
		std::error_code ec{};
		which&=std::ios_base::out|std::ios::in;
		if(which==0)
			return -1;
		if(_mode&std::ios_base::out)
			flush_buffer(ec);
		fseek(sp, SEEK_SET, ec);
		if(_mode&std::ios_base::in)
			setg(nullptr, nullptr, nullptr);
		return ftell(ec);
	}

	int sync() override {
		if(_mode&std::ios::out) {
			std::error_code ec{};
			flush_buffer(ec);
			SubT::adapter_fflush(subtype(), ec);
			if(ec)
				throw std::system_error{ec};
		}
		return 0;
	}
	std::streamsize showmanyc() override {
		//fprintf(stderr, "manyc: \n");
		if(!(_mode&std::ios::in))
			return -1;
		assert(egptr()==gptr());
		auto rem=SubT::adapter_remain(subtype());
		std::streamsize ret{0};
		if(rem.second) {
			ret+=rem.first;
			if(ret==0)
				ret=-1;
		} else {
			if(ret==0)
				ret=16*1024;
		}
		return ret;
	}
	std::streamsize xsgetn(char* s, std::streamsize n) override {
		//fprintf(stderr, "xsgetn: %u\n", (unsigned int)n);
		if(!(_mode&std::ios::in))
			return 0;
		std::size_t avail=egptr()-gptr();
		if(static_cast<std::size_t>(n)<bufsiz+avail)
			return std::streambuf::xsgetn(s, n);

		std::error_code ec{};
		std::streamsize total=n;
		if(avail) {
			traits_type::copy(s, gptr(), avail);
			setg(eback(), egptr(), egptr());
			s+=avail;
			n-=avail;
		}
		// just read blocks directly
		auto toread=n/bufsiz*bufsiz;
		auto r=SubT::adapter_fread(subtype(), s, toread, ec);
		if(ec)
			throw std::system_error{ec};
		s+=r;
		n-=r;
		if(r==toread && n>0) {
			n-=std::streambuf::xsgetn(s, n);
		}
		return total-n;
	}
	int underflow_(std::error_code& ec) {
		int ret=EOF;
		if(gptr()<egptr()) {
			ret=traits_type::to_int_type(*gptr());
		} else {
			const void* ptr;
			auto r=SubT::adapter_fmap(subtype(), &ptr, ec);
			//fprintf(stderr, "underflow 2 %d\n", ret);
			if(ec)
				throw std::system_error{ec};
			if(r!=0) {
				auto p=static_cast<char*>(const_cast<void*>(ptr));
				setg(p, p, p+r);
				ret=traits_type::to_int_type(*gptr());
			}
		}
		//fprintf(stderr, "underflow %d\n", ret);
		return ret;
	}
	int underflow() override {
		if(!(_mode&std::ios::in))
			return EOF;
		std::error_code ec{};
		return underflow_(ec);
	}
	int pbackfail(int c) override {
		if(!(_mode&std::ios::in))
			return EOF;
		std::error_code ec{};
		int ret=EOF;
		if(eback()<gptr()) {
			gbump(-1);
			if(c==EOF)
				ret=0;
			else
				ret=c;
		} else {
			fseek(gptr()-egptr()-1, SEEK_CUR, ec);
			setg(nullptr, nullptr, nullptr);
			auto r=underflow_(ec);
			if(r!=EOF) {
				if(c==EOF)
					ret=0;
				else
					ret=c;
			}
		}
		return ret;
	}

	std::streamsize xsputn(const char* s, std::streamsize n) override {
		if(!(_mode&std::ios::out))
			return 0;
		std::size_t slot=epptr()-pptr();
		if(static_cast<std::size_t>(n)<bufsiz+slot)
			return std::streambuf::xsputn(s, n);
		std::error_code ec{};
		std::streamsize total=n;

		traits_type::copy(pptr(), s, slot);
		pbump(slot);
		s+=slot;
		n-=slot;
		flush_buffer(ec);
		auto towrite=n/bufsiz*bufsiz;
		SubT::adapter_fwrite(subtype(), s, towrite, ec);
		if(ec)
			throw std::system_error{ec};
		s+=towrite;
		n-=towrite;
		prepare(ec);
		auto tocopy=epptr()-pptr();
		if(tocopy>n)
			tocopy=n;
		traits_type::copy(pptr(), s, tocopy);
		s+=tocopy;
		n-=tocopy;
		pbump(tocopy);
		return total-n;
	}

	int overflow(int c) override {
		if(!(_mode&std::ios::out))
			return EOF;
		std::error_code ec{};
		flush_buffer(ec);
		prepare(ec);
		if(c==EOF)
			return 0;
		*pptr()=traits_type::to_char_type(c);
		pbump(1);
		return c;
	}

private:
	SubT* subtype() noexcept {
		return static_cast<SubT*>(this);
	}
	void flush_buffer(std::error_code& ec) {
		if(pbase()<pptr()) {
			SubT::adapter_commit(subtype(), pptr()-pbase(), ec);
			setp(nullptr, nullptr);
			if(ec)
				throw std::system_error{ec};
		}
	}
	void fseek(off_t off, int whence, std::error_code& ec) {
		SubT::adapter_fseek(subtype(), off, whence, ec);
		if(ec)
			throw std::system_error{ec};
	}
	off_t ftell(std::error_code& ec) {
		auto ret=SubT::adapter_ftell(subtype(), ec);
		if(ec)
			throw std::system_error{ec};
		return ret;
	}
	void prepare(std::error_code& ec) {
		void* ptr;
		auto l=SubT::adapter_prepare(subtype(), &ptr, ec);
		if(ec)
			throw std::system_error{ec};
		auto p=static_cast<char*>(ptr);
		setp(p, p+l);
	}
private:

	//hnhnhn
	std::ios::openmode _mode;
	constexpr static std::size_t bufsiz=8*1024;
};

}

#include "gapr/detail/streambuf.hh"
#include "gapr/mem-file.hh"




class mem_file_buf: public gapr::streambuf_impl<mem_file_buf> {
#if 0
	class MemFileAdapt {
	public:
		void close(std::error_code& ec) {
			assert(_file);
			_file=gapr::mem_file{};
		}
		bool eof() { return _eof; }
	private:
	};
#endif
public:
	mem_file_buf(): streambuf_impl<mem_file_buf>{std::ios::openmode{}} { }
	explicit mem_file_buf(gapr::mem_file&& file):
		streambuf_impl<mem_file_buf>{std::ios::in}, _file{std::move(file)}, _off{0}, _eof{false} {
			assert(_file);
		}
	//*in *inout *out
	explicit mem_file_buf(gapr::mutable_mem_file&& file, std::ios::openmode mode):
		streambuf_impl<mem_file_buf>{mode}, _mfile{std::move(file)}, _off{0}, _eof{false} {
			assert(_mfile);
		}
	mem_file_buf(const mem_file_buf&) =delete;
	mem_file_buf& operator=(const mem_file_buf&) =delete;
#if 0
	mem_file_buf(mem_file_buf&& __rhs) : __streambuf_type(__rhs), _M_mode(__rhs._M_mode), _M_buf(__rhs._M_buf)
	{ }
	mem_file_buf& operator=(mem_file_buf&& __rhs) {
		mem_file_buf(std::move(__rhs)).swap(*this);
		return *this;
	}
	void swap(mem_file_buf& __rhs) {
		__streambuf_type::swap(__rhs);
		std::swap(_M_mode, __rhs._M_mode);
		std::swap(_M_buf, __rhs._M_buf);
	}
#endif

	gapr::mutable_mem_file mem_file() && noexcept {
		return std::move(_mfile);
	}

protected:
private:
	gapr::mem_file _file;
	gapr::mutable_mem_file _mfile;
	std::size_t _off;
	bool _eof;

	static std::pair<std::size_t, bool> adapter_remain(mem_file_buf* obj) {
		auto n=obj->_file?obj->_file.size():obj->_mfile.size();
		return {n-obj->_off, true};
	}
	static std::size_t adapter_fmap(mem_file_buf* obj, const void** pptr, std::error_code& ec) {
		if(obj->_eof)
			return 0;
		auto r=obj->_file?obj->_file.map(obj->_off):obj->_mfile.map(obj->_off);
		////fprintf(stderr, "fmap %zd %zd %p\n", obj->
		if(r.size()>0) {
			obj->_off+=r.size();
			*pptr=r.data();
		} else {
			obj->_eof=true;
		}
		return r.size();
	}
	static std::size_t adapter_fread(mem_file_buf* obj, void* ptr, std::size_t size, std::error_code& ec) {
		//fprintf(stderr, "fread: %zd\n", size);
		if(size<=0)
			return 0;
		std::size_t ret=0;
		do {
			const void* buf;
			auto n=adapter_fmap(obj, &buf, ec);
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
				obj->_off-=x;
				break;
			}
		} while(size>0);
		return ret;
	}
	static off_t adapter_ftell(mem_file_buf* obj, std::error_code& ec) {
		return obj->_off;
	}
	static void adapter_fseek(mem_file_buf* obj, off_t offset, int whence, std::error_code& ec) {
		auto fsize=obj->_file?obj->_file.size():obj->_mfile.size();
		switch(whence) {
		case SEEK_SET:
			obj->_off=offset;
			break;
		case SEEK_CUR:
			obj->_off=obj->_off+offset;
			break;
		case SEEK_END:
			obj->_off=fsize+offset;
			break;
		default:
			assert(0);
		}
		if(obj->_off<fsize) {
			obj->_eof=false;
		} else if(obj->_off==fsize) {
			obj->_eof=true;
		} else {
			ec=std::error_code{EINVAL, std::generic_category()};
		}
	}
	static std::size_t adapter_prepare(mem_file_buf* obj, void** pptr, std::error_code& ec) {
		auto buf=obj->_mfile.map_tail();
		*pptr=buf.data();
		return buf.size();
		//ec=std::error_code{EBADF, std::generic_category()};
		//return 0;
	}
	static void adapter_commit(mem_file_buf* obj, std::size_t size, std::error_code& ec) {
		obj->_mfile.add_tail(size);
		//ec=std::error_code{EBADF, std::generic_category()};
	}
	static void adapter_fwrite(mem_file_buf* obj, const void* ptr, std::size_t size, std::error_code& ec) {
		while(size>0) {
			auto buf=obj->_mfile.map_tail();
			auto n=buf.size();
			if(n>size)
				n=size;
			std::memcpy(buf.data(), ptr, n);
			obj->_mfile.add_tail(n);
			ptr=static_cast<const char*>(ptr)+n;
			size-=n;
		}
		//ec=std::error_code{EBADF, std::generic_category()};
	}
	static void adapter_fflush(mem_file_buf* obj, std::error_code& ec) {
		//ec=std::error_code{EBADF, std::generic_category()};
	}
	friend class streambuf_impl<mem_file_buf>;
};

class mem_file_in: public std::istream {
public:
	explicit mem_file_in(gapr::mem_file&& file, std::ios::openmode which=std::ios::in):
		//__which | ios_base::in)
		std::istream{&_buf}, _buf{std::move(file)} {
		}
	explicit mem_file_in(gapr::mutable_mem_file&& file, std::ios::openmode which=std::ios::in):
		std::istream{&_buf}, _buf{std::move(file)} {
		}
	mem_file_in(const mem_file_in&) =delete;
	mem_file_in& operator=(const mem_file_in&) =delete;

	mem_file_buf* rdbuf() const noexcept {
		return const_cast<mem_file_buf*>(&_buf);
	}

	gapr::mutable_mem_file mem_file() && noexcept {
		return std::move(_buf).mem_file();
	}

#if 0
	ispanstream(ispanstream&& __rhs) : std::istream(std::move(__rhs)), _M_sb(std::move(__rhs._M_sb))
	{
		std::istream::set_rdbuf(std::addressof(_M_sb));
	}
	ispanstream& operator=(ispanstream&& __rhs) = default;
	void swap(ispanstream& __rhs) {
		std::istream::swap(__rhs);
		_M_sb.swap(__rhs._M_sb);
	}
#endif

private:
	mem_file_buf _buf;
};

class mem_file_out: public std::ostream {
public:
	explicit mem_file_out(gapr::mutable_mem_file&& file, std::ios::openmode which=std::ios::out):
		//_M_sb(__s, __which | ios_base::in)
		std::ostream{&_buf}, _buf{std::move(file), which} {
		}
	mem_file_out(const mem_file_out&) =delete;
	mem_file_out& operator=(const mem_file_out&) =delete;
#if 0
	mem_file_out(mem_file_out&& __rhs)
		: __ostream_type(std::move(__rhs)), _M_sb(std::move(__rhs._M_sb))
	{
		__ostream_type::set_rdbuf(std::addressof(_M_sb));
	}
	mem_file_out& operator=(mem_file_out&& __rhs) = default;
	void swap(mem_file_out& __rhs) {
		__ostream_type::swap(__rhs);
		_M_sb.swap(__rhs._M_sb);
	}
#endif
	mem_file_buf* rdbuf() const noexcept {
		return const_cast<mem_file_buf*>(&_buf);
	}

	gapr::mutable_mem_file mem_file() && noexcept {
		return std::move(_buf).mem_file();
	}
private:
	mem_file_buf _buf;
};

