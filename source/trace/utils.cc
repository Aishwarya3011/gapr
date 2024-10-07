#include "utils.hh"

#include "gapr/serializer.hh"
#include "gapr/utility.hh"
#include "gapr/detail/zlib-stream.hh"

#include <valarray>


// XXX dup
template<typename T>
static bool save_impl(const T& d, std::ostream& str) {
	gapr::Serializer<T> ser{};
	char buf[4096];
	do {
		auto r=ser.save(d, buf, sizeof(buf)-20);
		//gapr::print("save: ", static_cast<bool>(ser), ' ', r);
		if(!str.write(buf, r))
			gapr::report("failed to write");
	} while(ser);
	return true;
}
template<typename T>
static bool load_impl(T& d, std::istream& str) {
	gapr::Deserializer<T> deser{};
	bool eof{false};
	char buf[4096];
	do {
		if(!str.read(buf, sizeof(buf))) {
			if(!str.eof())
				gapr::report("failed to read");
			eof=true;
			str.clear();
		}
		auto n=str.gcount();
		auto r=deser.load(d, buf, n);
		n=r-n;
		//gapr::print("deser: ", n, '/', r);
		if(n!=0) {
			//gapr::print("seek: ", n);
			if(!str.seekg(n, str.cur))
				//gapr::report("failed to seekg");
				throw std::logic_error{"adsf"};
		}
	} while(!eof && deser);
	//gapr::print("load: ", eof, static_cast<bool>(deser));
	return true;
	// XXX
	return !static_cast<bool>(deser);
}


struct ser_helper {
	std::vector<CompressedSample>& samples;
};
template<> struct gapr::SerializerAdaptor<CompressedSample, 0> {
	template<typename T> static auto& map(T& obj) { return obj.tag; }
};
template<> struct gapr::SerializerAdaptor<CompressedSample, 1> {
	template<typename T> static auto& map(T& obj) { return obj.content; }
};
template<> struct gapr::SerializerAdaptor<ser_helper, 0> {
	template<typename T> static auto& map(T& obj) { return obj.samples; }
};
bool save(const std::vector<CompressedSample>& samples, std::ostream& str) {
	ser_helper helper{const_cast<std::vector<CompressedSample>&>(samples)};
	return save_impl<ser_helper>(helper, str);
}
bool load(std::vector<CompressedSample>& samples, std::istream& str) {
	ser_helper helper{samples};
	return load_impl<ser_helper>(helper, str);
}

class vec_output {
	public:
		explicit vec_output(): _vec{} { }
		~vec_output() { }
		vec_output(const vec_output&) =delete;
		vec_output& operator=(const vec_output&) =delete;

		std::pair<char*, std::size_t> buffer() {
			if(_idx+128>_vec.size())
				_vec.resize(_vec.size()+512);
			return {_vec.data()+_idx, _vec.size()-_idx};
		}
		void commit(std::size_t n) {
			_idx+=n;
		}
		void flush() {
			_vec.resize(_idx);
		}

		std::size_t offset() const noexcept {
			return _idx;
		}

		std::vector<char>& vec() { return _vec; }

	private:
		std::vector<char> _vec;
		std::size_t _idx{0};
};
class vec_input {
	public:
		explicit vec_input(const std::vector<char>& f): _f{&f} { }
		~vec_input() { }
		vec_input(const vec_input&) =delete;
		vec_input& operator=(const vec_input&) =delete;

		std::pair<const char*, std::size_t> buffer() {
			if(!_f)
				return {nullptr, 0};
			return {_f->data()+_idx, _f->size()-_idx};
		}
		void consume(std::size_t n) {
			_idx+=n;
			if(_idx>=_f->size())
				_f=nullptr;
		}

		std::size_t offset() const noexcept {
			return _idx;
		}
	private:
		const std::vector<char>* _f;
		std::size_t _idx{0};
};
std::vector<char> compress_zlib(const void* p_, std::size_t n) {
	vec_output vec{};
	gapr::zlib_output zlib{vec};
	auto p=static_cast<const char*>(p_);
	while(n>0) {
		auto [buf, siz]=zlib.buffer();
		if(!buf)
			throw std::runtime_error{"failed to alloc buf"};
		if(siz>n)
			siz=n;
		std::memcpy(buf, p, siz);
		zlib.commit(siz);
		p+=siz;
		n-=siz;
	}
	zlib.flush();
	return std::move(vec.vec());
}

bool decompress_zlib(void* p_, std::size_t n, const std::vector<char>& v) {
	vec_input vec{v};
	gapr::zlib_input zlib{vec};
	auto p=static_cast<char*>(p_);
	std::size_t k=0;
	do {
		auto [buf, siz]=zlib.buffer();
		if(!buf)
			break;
		if(k+siz>n) {
			//fprintf(stderr, "unexpected size %zd %zd\n", k+siz, n);
			return false;
		}
		std::memcpy(p+k, buf, siz);
		k+=siz;
		zlib.consume(siz);
	} while(true);
	return k==n;
}

double get_scale_factor(const std::valarray<float>& in) {
	double var=0.0;
	for(unsigned int i=0; i<in.size(); i++) {
		auto v=in[i];
		assert(v>=0.0);
		var+=std::sqrt(v);
	}
	var=var/in.size();
	return 1.0/(var*var);
}

