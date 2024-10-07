#include "gapr/cube-loader.hh"

#include "gapr/cube.hh"
#include "gapr/streambuf.hh"
#include "gapr/utility.hh"

#ifdef WITH_VP9
#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>
#endif
#ifdef WITH_AV1
#include <aom/aomdx.h>
#include <aom/aom_decoder.h>
#endif
#include "mkvparser.hpp"
#include <string.h>
#ifdef HAVE_OPENMP
#include <omp.h>
#else
inline int omp_get_num_threads() { return 1; }
inline int omp_get_thread_num() { return 0; }
#endif
#include <memory>
#include <vector>

template<typename CTX>
struct codec_adapter;

#ifdef WITH_VP9
template<>
struct codec_adapter<vpx_codec_ctx_t> {
	using codec_ctx_t=vpx_codec_ctx_t;
	using codec_dec_cfg_t=vpx_codec_dec_cfg_t;
	using codec_iter_t=vpx_codec_iter_t;
	static constexpr vpx_codec_err_t codec_ok=VPX_CODEC_OK;
	static const char* codec_err_to_string(vpx_codec_err_t err) {
		return vpx_codec_err_to_string(err);
	}
	static constexpr vpx_img_fmt_t img_fmt(uint8_t) { return VPX_IMG_FMT_I420; }
	static constexpr vpx_img_fmt_t img_fmt(uint16_t) { return VPX_IMG_FMT_I42016; }
	static int get_img_stride(vpx_image_t* img) {
		return img->stride[VPX_PLANE_Y];
	}
	static unsigned char* get_img(vpx_image_t* img, size_t y) {
		return img->planes[VPX_PLANE_Y]+y*get_img_stride(img);
	}
	static vpx_codec_err_t codec_dec_init(vpx_codec_ctx_t* ctx, vpx_codec_dec_cfg_t* cfg) {
		return vpx_codec_dec_init(ctx, vpx_codec_vp9_dx(), cfg, 0/*VPX_CODEC_USE_HIGHBITDEPTH*/);
	}
	static vpx_codec_err_t codec_destroy(vpx_codec_ctx_t* ctx) {
		return vpx_codec_destroy(ctx);
	}
	static vpx_codec_err_t codec_decode(vpx_codec_ctx_t* ctx, const uint8_t* buf, unsigned int bufsz) {
		// XXX
		return vpx_codec_decode(ctx, buf, bufsz, nullptr, 50);
	}
	static vpx_image_t* codec_get_frame(vpx_codec_ctx_t* ctx, vpx_codec_iter_t* iter) {
		return vpx_codec_get_frame(ctx, iter);
	}
};
#endif
#ifdef WITH_AV1
template<>
struct codec_adapter<aom_codec_ctx_t> {
	using codec_ctx_t=aom_codec_ctx_t;
	using codec_dec_cfg_t=aom_codec_dec_cfg_t;
	using codec_iter_t=aom_codec_iter_t;
	static constexpr aom_codec_err_t codec_ok=AOM_CODEC_OK;
	static const char* codec_err_to_string(aom_codec_err_t err) {
		return aom_codec_err_to_string(err);
	}
	static constexpr aom_img_fmt_t img_fmt(uint8_t) { return AOM_IMG_FMT_I420; }
	static constexpr aom_img_fmt_t img_fmt(uint16_t) { return AOM_IMG_FMT_I42016; }
	static int get_img_stride(aom_image_t* img) {
		return img->stride[AOM_PLANE_Y];
	}
	static unsigned char* get_img(aom_image_t* img, size_t y) {
		return img->planes[AOM_PLANE_Y]+y*get_img_stride(img);
	}
	static aom_codec_err_t codec_dec_init(aom_codec_ctx_t* ctx, aom_codec_dec_cfg_t* cfg) {
		return aom_codec_dec_init(ctx, aom_codec_av1_dx(), cfg, 0);
	}
	static aom_codec_err_t codec_destroy(aom_codec_ctx_t* ctx) {
		return aom_codec_destroy(ctx);
	}
	static aom_codec_err_t codec_decode(aom_codec_ctx_t* ctx, const uint8_t* buf, unsigned int bufsz) {
		// XXX
		return aom_codec_decode(ctx, buf, bufsz, 0);
	}
	static aom_image_t* codec_get_frame(aom_codec_ctx_t* ctx, aom_codec_iter_t* iter) {
		return aom_codec_get_frame(ctx, iter);
	}
};
#endif

static uint16_t dec16(uint16_t x) {
	unsigned int i=x>>9;
	if(i<2)
		return x;
	// add 0.5 to compensate truncated bits
	x=((x&0x1ff)<<1)|0x401;
	return x<<(i-2);
}

class MyMkvReader: public mkvparser::IMkvReader {
	gapr::Streambuf& strm;
	long long size;

	public:
	MyMkvReader(gapr::Streambuf& strm): strm{strm}, size{-2} { }
	~MyMkvReader() { }

	MyMkvReader(const MyMkvReader&) =delete;
	MyMkvReader& operator=(const MyMkvReader&) =delete;

	int Read(long long pos, long len, unsigned char* buf) override {
		if(len==0)
			return 0;
		auto pr=strm.pubseekpos(pos);
		if(pr==-1)
		    return -1;
		auto sr=strm.sgetn(reinterpret_cast<char*>(buf), len);
	      if (sr != len)
				return -1;
		return 0;
	}
	int Length(long long* total, long long* avail) override {
		if(size==-2) {
			size=strm.pubseekoff(0, std::ios_base::end);
		}
		if(size<0)
			return -1;
		if(total)
			*total=size;
		if(avail)
			*avail=size;
		return 0;
	}
};

class WebmLoader: public gapr::cube_loader {
	public:
		explicit WebmLoader(gapr::Streambuf& file): gapr::cube_loader{file},
					reader{file} {
			supported=probe();
#if 0
						file.pubseekpos(0);
						FILE* f=fopen("/tmp/xxx.webm", "wb");
						while(true) {
							char buf[1024];
							auto r=file.sgetn(buf, 1024);
							fwrite(buf, 1, r, f);
							if(r<1024)
								break;
						}
						fclose(f);
#endif
		}
		~WebmLoader() override { }
	private:
		MyMkvReader reader;
	std::vector<unsigned char> firstFrame;
	std::vector<std::vector<uint8_t>> buffs{};
		bool supported;
		bool is_vp9{false};
		bool is_av1{false};
		bool enc16{false};

		bool probe() {
			mkvparser::EBMLHeader header;
			long long off=0;
			if(header.Parse(&reader, off)<0)
				return false;
			mkvparser::Segment *segment;
			if(mkvparser::Segment::CreateInstance(&reader, off, segment))
				return false;
			std::unique_ptr<mkvparser::Segment> _segment{segment};
			if(segment->Load()<0)
				return false;

			auto tags=segment->GetTags();
			for(int i=0; i<(tags?tags->GetTagCount():0); ++i) {
				auto tag=tags->GetTag(i);
				for(int j=0; j<tag->GetSimpleTagCount(); ++j) {
					if(std::strcmp(tag->GetSimpleTag(j)->GetTagName(), "Gapr16to12")==0)
						enc16=true;
				}
			}

			const mkvparser::Tracks* tracks=segment->GetTracks();
			const mkvparser::VideoTrack* video_track=nullptr;
			for(unsigned long i=0; i<tracks->GetTracksCount(); ++i) {
				const mkvparser::Track* track=tracks->GetTrackByIndex(i);
				if(track->GetType()==mkvparser::Track::kVideo) {
					video_track=static_cast<const mkvparser::VideoTrack*>(track);
#ifdef WITH_VP9
					if(std::string{"V_VP9"}==track->GetCodecId()) {
						is_vp9=true;
						break;
					}
#endif
#ifdef WITH_AV1
					if(std::string{"V_AV1"}==track->GetCodecId()) {
						is_av1=true;
						break;
					}
#endif
				}
			}
			if(!video_track)
				return false;
			if(!(is_vp9||is_av1))
				return false;

			auto video_track_index=video_track->GetNumber();
			const mkvparser::Cluster* cluster=segment->GetFirst();
			const mkvparser::Block* block=nullptr;
			const mkvparser::BlockEntry* block_entry=nullptr;
			int block_frame_index=0;
			bool eos=false;
			while(true) {
				bool be_eos=false;
				do {
					long status=0;
					bool get_new_block=false;
					if(!block_entry && !be_eos) {
						status=cluster->GetFirst(block_entry);
						get_new_block=true;
					} else if(be_eos || block_entry->EOS()) {
						cluster=segment->GetNext(cluster);
						if(!cluster || cluster->EOS()) {
							eos=true;
							break;
						}
						status=cluster->GetFirst(block_entry);
						be_eos=false;
						get_new_block=true;
					} else if(!block || block_frame_index==block->GetFrameCount()
							|| block->GetTrackNumber()!=video_track_index) {
						status=cluster->GetNext(block_entry, block_entry);
						if(!block_entry || block_entry->EOS()) {
							be_eos=true;
							continue;
						}
						get_new_block=true;
					}
					if(status||!block_entry) {
						eos=true;
						break;
					}
					if(get_new_block) {
						block=block_entry->GetBlock();
						if(!block) {
							eos=true;
							break;
						}
						block_frame_index=0;
					}

				} while(be_eos || block->GetTrackNumber()!=video_track_index);
				if(eos)
					break;

				auto frame=block->GetFrame(block_frame_index++);
				auto len=frame.len;
				std::vector<uint8_t> buf;
						if(len>static_cast<long long>(buf.size()))
							buf.resize(len);
						if(frame.Read(&reader, &buf[0]))
							gapr::report("Failed to read frame.");
				buffs.emplace_back(std::move(buf));
			}
			if(buffs.size()<=0)
				return false;

#ifdef WITH_VP9
			if(is_vp9)
				return probe_impl<vpx_codec_ctx_t>(video_track);
#endif
#ifdef WITH_AV1
			if(is_av1)
				return probe_impl<aom_codec_ctx_t>(video_track);
#endif
			return false;
		}
		template<typename CTX>
		bool probe_impl(const mkvparser::VideoTrack* video_track) {
			using Adapter=codec_adapter<CTX>;
			gapr::cube_type type;
			std::array<int32_t, 3> sizes;
			typename Adapter::codec_ctx_t ctx;
			typename Adapter::codec_dec_cfg_t cfg;
			cfg.w=sizes[0]=video_track->GetWidth();
			cfg.h=sizes[1]=video_track->GetHeight();
			cfg.threads=1;
			auto err=Adapter::codec_dec_init(&ctx, &cfg);
			if(err!=Adapter::codec_ok)
				gapr::report("Failed to init decoder: ", Adapter::codec_err_to_string(err));
			for(int i=0; i<1; ++i) {
				err=Adapter::codec_decode(&ctx, buffs[0].data(), buffs[0].size());
				if(err!=Adapter::codec_ok)
					gapr::report("Failed to decode frame: ", Adapter::codec_err_to_string(err));
			}
			//err=Adapter::codec_decode(&ctx, nullptr, 0);
			//if(err!=Adapter::codec_ok)
			//gapr::report("Failed to flush frame: ", Adapter::codec_err_to_string(err));
			typename Adapter::codec_iter_t iter=nullptr;
			if(auto img=Adapter::codec_get_frame(&ctx, &iter)) {
				switch(img->fmt) {
				case Adapter::img_fmt(uint8_t{}):
					//case VPX_IMG_FMT_YV12:
					type=gapr::cube_type::u8;
					break;
				case Adapter::img_fmt(uint16_t{}):
					type=gapr::cube_type::u16;
					break;
				default:
					return false;
				}
				auto bpv=voxel_size(type);
				firstFrame.resize(sizes[0]*sizes[1]*bpv);
				for(int32_t y=0; y<sizes[1]; y++) {
					memcpy(&firstFrame[y*sizes[0]*bpv], Adapter::get_img(img, y), sizes[0]*bpv);
				}
			} else {
				return false;
			}
			err=Adapter::codec_destroy(&ctx);
			if(err!=Adapter::codec_ok)
				gapr::report("Failed to destroy decoder: ", Adapter::codec_err_to_string(err));
			sizes[2]=buffs.size();
			set_info(type, sizes);
			return true;
		}

	template<typename T, bool Dec16=false>
	void copyImageImp(unsigned char* plane, int stride, char* ptr, int64_t ystride) {
		for(int32_t y=0; y<sizes()[1]; y++) {
		if constexpr(Dec16) {
			for(unsigned int x=0; x<sizes()[0]; ++x) {
				auto optr=reinterpret_cast<T*>(ptr+y*ystride);
				auto iptr=reinterpret_cast<T*>(plane+y*stride);
				optr[x]=dec16(iptr[x]);
			}
		} else {
			memcpy(ptr+y*ystride, plane+y*stride, sizes()[0]*sizeof(T));
		}
		}
	}
	void copyImage(unsigned char* plane, int stride, char* ptr, int64_t ystride) {
		switch(type()) {
			case gapr::cube_type::u8:
				copyImageImp<uint8_t>(plane, stride, ptr, ystride);
				break;
			case gapr::cube_type::u16:
				if(enc16) {
				copyImageImp<uint16_t, true>(plane, stride, ptr, ystride);
				} else {
				copyImageImp<uint16_t>(plane, stride, ptr, ystride);
				}
				break;
			default:
				break;
		}
	}
	template<typename CTX>
	void load_impl(size_t ileft, size_t iright, char* ptr, int64_t ystride, int64_t zstride) {
		gapr::print("i ", ileft, ", ", iright);
		using Adapter=codec_adapter<CTX>;
		auto width=sizes()[0];
		auto height=sizes()[1];

		typename Adapter::codec_ctx_t ctx;
		typename Adapter::codec_dec_cfg_t cfg;
		cfg.w=width;
		cfg.h=height;
		cfg.threads=1;
		auto err=Adapter::codec_dec_init(&ctx, &cfg);
		if(err!=Adapter::codec_ok)
			gapr::report("Failed to init decoder: ", Adapter::codec_err_to_string(err));
		bool got_img=false;
		size_t imgi=ileft;
		for(size_t i=ileft; i<iright || got_img; i++) {
			if(i<iright) {
				err=Adapter::codec_decode(&ctx, buffs[i].data(), buffs[i].size());
				if(err!=Adapter::codec_ok)
					gapr::report("Failed to decode frame: ", Adapter::codec_err_to_string(err));
			} else {
				err=Adapter::codec_decode(&ctx, nullptr, 0);
				if(err!=Adapter::codec_ok)
					gapr::report("Failed to flush frame: ", Adapter::codec_err_to_string(err));
			}

			got_img=false;
			typename Adapter::codec_iter_t iter=nullptr;
			while(auto img=Adapter::codec_get_frame(&ctx, &iter)) {
				got_img=true;
				copyImage(Adapter::get_img(img, 0), Adapter::get_img_stride(img), ptr+imgi*zstride, ystride);
				imgi++;
			}
		}
		err=Adapter::codec_destroy(&ctx);
		if(err!=Adapter::codec_ok)
			gapr::report("Failed to destroy decoder: ", Adapter::codec_err_to_string(err));
	}
	void do_load(char* ptr, int64_t ystride, int64_t zstride) override {
		if(!supported)
			return;
		auto imgType=type();
		auto width=sizes()[0];
		copyImage(&firstFrame[0], width*voxel_size(imgType), ptr, ystride);

//#pragma omp parallel
		{
#ifdef _WIN64
			auto id=0u;
			auto num=1u;
#else
			auto id=omp_get_thread_num();
			auto num=omp_get_num_threads();
#endif
			//size_t ileft=1;
			size_t ileft=0;
			if(id>0)
				ileft=1+((buffs.size()-1)*id+num-1)/num;
			size_t iright=buffs.size();
			if(id<num-1)
				iright=1+((buffs.size()-1)*(id+1)+num-1)/num;
			if(ileft<iright) {
#ifdef WITH_VP9
				if(is_vp9)
					load_impl<vpx_codec_ctx_t>(ileft, iright, ptr, ystride, zstride);
#endif
#ifdef WITH_AV1
				if(is_av1)
					load_impl<aom_codec_ctx_t>(ileft, iright, ptr, ystride, zstride);
#endif
			}
		}
	}
};


namespace gapr {
	std::unique_ptr<cube_loader> make_cube_loader_webm(Streambuf& file) {
		return std::make_unique<WebmLoader>(file);
	}
}

