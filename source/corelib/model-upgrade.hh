#include <algorithm>

static gapr::delta_reset_proofread_ upgrade_delta(gapr::delta_reset_proofread_0_&& delta) {
	gapr::delta_reset_proofread_ delta2;
	delta2.props=std::move(delta.props);
	std::sort(delta.nodes.begin(), delta.nodes.end(), [](auto a, auto b) {
		return a<b;
	});
	auto encode=[](const auto& in) {
		std::decay_t<decltype(in)> out;
		std::size_t i=0;
		std::decay_t<decltype(out[0])> v{0};
		while(in.size()>i) {
			if(in.size()<=i+1) {
				out.push_back(0);
				out.push_back(in[i]-v);
				v=in[i];
				++i;
				continue;
			}
			if(in[i]+1==in[i+1]) {
				std::size_t j=i+1;
				std::size_t n=std::min(in.size(), i+0x61);
				while(j+1<n && in[j]+1==in[j+1])
					++j;
				assert(j-i+1-2<=0x5f);
				out.push_back(0x20+(j-i+1-2));
				out.push_back(in[i]-v);
				v=in[j];
				i=j+1;
			} else {
				std::size_t j=i+1;
				std::size_t n=std::min(in.size(), 0x22+i);
				while(j+1<n) {
					if(in[j]==in[j+1]+1 || in[j]+1==in[j+1]) {
						--j;
						break;
					}
					++j;
				}
				if(j-i>0x1f)
					j=i+0x1f;
				out.push_back(j-i);
				out.push_back(in[i]-v);
				for(++i; i<=j; ++i)
					out.push_back(in[i]-in[i-1]);
				v=in[j];
				assert(i==j+1);
			}
		}
		return out;
	};
	delta2.nodes=encode(delta.nodes);
	return delta2;
}
template<typename T>
class AscendingSequenceDecoder {
	public:
		AscendingSequenceDecoder(const std::vector<T>& in): in{in} { }
		std::pair<T, bool> next() {
			if(bufi>=bufn) {
				if(i>=in.size())
					return {0, true};
				std::size_t k;
				bufi=0;
				switch(in[i]>>5) {
					case 1:
					case 2:
					case 3:
						assert(i+1<in.size());
						bufn=(in[i]-0x20)+2;
						v+=in[i+1];
						for(k=0; k<bufn; ++k)
							buf[k]=v+k;
						v+=k-1;
						i+=2;
						break;
					case 0:
						assert(i+1+(in[i]&0x1f)<in.size());
						bufn=(in[i]&0x1f)+1;
						v+=in[i+1];
						buf[0]=v;
						for(std::size_t k=1; k<bufn; ++k)
							buf[k]=(v+=in[i+1+k]);
						i+=1+bufn;
						break;
					default:
						assert(0);
				}
				assert(bufn<=128);
			}
			assert(bufi<bufn);
			auto vv=buf[bufi];
			++bufi;
			return {vv, false};
		}
	private:
		const std::vector<T>& in;
		std::size_t i=0;
		std::array<T, 128> buf;
		std::size_t bufi=0;
		std::size_t bufn=0;
		T v{0};
};
