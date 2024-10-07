#include <vector>
#include <istream>
#include <ostream>
#include <valarray>
#include <memory>

#include "gapr/affine-xform.hh"
#include "gapr/bbox.hh"
#include "gapr/cube.hh"

struct CompressedSample {
	std::vector<char> content;
	int tag;
};

bool save(const std::vector<CompressedSample>& samples, std::ostream& str);
bool load(std::vector<CompressedSample>& samples, std::istream& str);

std::vector<char> compress_zlib(const void* p, std::size_t n);
bool decompress_zlib(void* p_, std::size_t n, const std::vector<char>& v);

double get_scale_factor(const std::valarray<float>& in);

template<unsigned int Width, unsigned int Height, unsigned int Depth>
static bool check_inside(const std::array<unsigned int, 3>& node_offset, const std::array<unsigned int, 3>& cube_sizes, const std::array<unsigned int, 3>& cube_offset) {
	std::array<unsigned int, 3> cube_offset2{Width/2, Height/2, Depth/2};
	for(unsigned int i=0; i<3; ++i)
		cube_offset2[i]+=cube_offset[i];
	if(node_offset[2]<cube_offset2[2] ||
			node_offset[2]+Depth>cube_sizes[2]+cube_offset2[2])
		return false;
	if(node_offset[1]<cube_offset2[1] ||
			node_offset[1]+Height>cube_sizes[1]+cube_offset2[1])
		return false;
	if(node_offset[0]<cube_offset2[0] ||
			node_offset[0]+Width>cube_sizes[0]+cube_offset2[0])
		return false;
	return true;
}

inline gapr::bbox calc_bbox(std::array<unsigned int, 3> offset, std::array<unsigned int, 3> sizes, const gapr::affine_xform& xform) {
	gapr::bbox bbox{};
	for(unsigned int i=0; i<8; ++i) {
		auto o=offset;
		for(unsigned int k=0; k<3; ++k)
			if(i&(1<<k))
				o[k]+=sizes[k];
		auto p=xform.from_offset(o);
		bbox.add(gapr::vec3<double>{p[0], p[1], p[2]});
	}
	return bbox;
}
inline bool check_inside(const gapr::bbox& bbox, const std::array<double, 3>& pos) {
	return bbox.hit_test(gapr::vec3<double>(pos[0], pos[1], pos[2]));
}

template<unsigned int Width, unsigned int Height, unsigned int Depth, unsigned int... Scales> class Sampler {
	public:
		std::unique_ptr<float[]> operator()(const gapr::affine_xform& xform, const gapr::cube_view<const void>& cube, std::array<unsigned int, 3> offset, gapr::vec3<double>& pos, bool skip_chk) {
			auto abs_node_off=xform.to_offset(pos);
			if(!skip_chk) {
				if(!check_inside<Width, Height, Depth>(abs_node_off, cube.sizes(), offset))
					return {};
			}
			auto pos2=xform.from_offset(abs_node_off);
			for(unsigned int i=0; i<3; ++i)
				pos[i]-=pos2[i];
			auto node_off=abs_node_off;
			for(unsigned int i=0; i<3; ++i)
				node_off[i]-=offset[i];
			return const_cast<gapr::cube_view<const void>&>(cube).visit([this,node_off](auto& view) {
				return do_sample(node_off, view);
			});
		}
	private:
		std::unique_ptr<float[]> do_sample(std::array<unsigned int, 3> node_off, const gapr::cube_view<const void>& cube) {
			return {};
		}
		template<typename T> std::unique_ptr<float[]> do_sample(std::array<unsigned int, 3> node_off, const gapr::cube_view<const T>& cube) {
			static constexpr unsigned int SampleSize=Width*Height*Depth*sizeof...(Scales);
			auto res=std::make_unique<float[]>(SampleSize);
			constexpr auto HalfW=Width/2;
			constexpr auto HalfH=Height/2;
			constexpr auto HalfD=Depth/2;
			unsigned int ii=0;
			double scale=std::numeric_limits<T>::max();
			for(unsigned int factor: {Scales...}) {
				for(unsigned int dz=0; dz<Depth; ++dz) {
					bool skip_z=factor*dz+node_off[2]<factor*HalfD || node_off[2]+factor*dz>=cube.sizes(2)+factor*HalfD;
					for(unsigned int dy=0; dy<Height; ++dy) {
						bool skip_y=factor*dy+node_off[1]<factor*HalfH || node_off[1]+factor*dy>=cube.sizes(1)+factor*HalfH;
						auto p=cube.row(node_off[1]+factor*dy-factor*HalfH, node_off[2]+factor*dz-factor*HalfD);
						for(unsigned int dx=0; dx<Width; ++dx) {
							bool skip_x=factor*dx+node_off[0]<factor*HalfW || node_off[0]+factor*dx>=cube.sizes(0)+factor*HalfW;
							float v=0;
							if(!skip_z&&!skip_y&&!skip_x)
								v=p[node_off[0]+factor*dx-factor*HalfW];
							res[ii++]=v/scale;
						}
					}
				}
			}
			return res;
		}
};

