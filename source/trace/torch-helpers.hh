#include <torch/torch.h>

#define assert_sizes(x, ...) \
	do assert_sizes_impl(x, {__VA_ARGS__}, __FILE__, __LINE__, #__VA_ARGS__); while(false)
inline static void assert_sizes_impl(const torch::Tensor& x, torch::IntArrayRef s, const char* file, int line, const char* dst) {
	auto res=[](auto ss, auto s) {
		if(ss.size()!=s.size())
			return false;
		for(std::size_t i=0; i<s.size(); ++i) {
			if(s[i]<0) {
				if(ss[i]%(-s[i])!=0)
					return false;
			} else {
				if(ss[i]!=s[i])
					return false;
			}
		}
		return true;
	}(x.sizes(), s);
	if(!res) {
		std::cerr<<file<<":"<<line<<": assert_sizes("<<x.sizes()<<", "<<dst<<") failed\n";
		std::abort();
	}
}

template<typename T> inline static void print_params(std::ostream& oss, T& net) {
	int64_t sss{0};
	for(auto& p: net.named_parameters()) {
		auto v=p.value().sizes();
		oss<<"par "<<p.key()<<":";
		int64_t s{1};
		for(std::size_t i=0; i<v.size(); i++) {
			auto ss=v[i];
			oss<<' '<<ss;
			s*=ss;
		}
		oss<<" = "<<s<<'\n';
		sss+=s;
	}
	oss<<"total = "<<sss<<'\n';
}

template<typename T> std::pair<std::vector<T>, std::array<unsigned int, 3>>
tensor_to_cube(torch::Tensor img) {
	unsigned int ww=1, hh=1;
	if(img.dim()>0)
		ww=img.size(img.dim()-1);
	if(img.dim()>1)
		hh=img.size(img.dim()-2);
	img=img.cpu().view({-1, hh, ww});
	unsigned int dd=img.size(0);
	std::vector<T> vals;
	vals.reserve(dd*hh*ww);
	for(unsigned int z=0; z<dd; ++z) {
		for(unsigned int y=0; y<hh; ++y) {
			auto ptr=img[z][y].data_ptr<T>();
			for(unsigned int x=0; x<ww; ++x)
				vals.push_back(ptr[x]);
		}
	}
	return {std::move(vals), {ww, hh, dd}};
}

class gpu_lock {
	public:
		explicit gpu_lock() { lock(); }
		~gpu_lock() { if(_id!=std::thread::id{}) unlock(); }

		void lock();
		void unlock();

		static void begin_session();

	private:
		std::thread::id _id;
};
