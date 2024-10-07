/*
 * cube2video: convert cubes to videos (lossy or lossless).
 * GOU Lingfeng <goulf@ion.ac.cn>
 *
 * Only 8-bit and 16-bit (converted to 12-bit) data are supported.
 *
 */

#include "savewebm.hh"

//#include <getopt.h>
//#include <iostream>
//#include <memory>
#include <chrono>
#include <vector>
#include <cstring>
#include <chrono>
#include <iostream>
#include <mutex>
//#include <sys/stat.h>
//#include <string.h>
//#include <libgen.h>

//#include "public/utils.h"
//#include "image.reader.h"
#include "gapr/cube-loader.hh"
#include "gapr/mem-file.hh"
#include "gapr/streambuf.hh"
#include "gapr/str-glue.hh"
#include "gapr/cube.hh"
#include "gapr/utility.hh"
#include "gapr/detail/nrrd-output.hh"
//#include "config-fnt.h"

#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>
#ifdef WITH_AOM
#include <aom/aom_encoder.h>
#include <aom/aomcx.h>
#endif
#include <mkvwriter.hpp>

#include "config.hh"

#include <boost/process/io.hpp>
#include <boost/process/child.hpp>
#include <boost/process/extend.hpp>
#include <boost/process/group.hpp>
#include <boost/process/handles.hpp>
#include <boost/process/pipe.hpp>
#ifdef __linux__
#include <fcntl.h>
#endif

#include "../corelib/libc-wrappers.hh"


#if 1
struct ConvParams {
///////////////
///////////////
	unsigned int quality, quality12; // [0, 63]
	int preset, preset12; // [-9, 9]
	unsigned int ncpu; // {1, 2}, little improvement if >=3.
	bool clamp;
	unsigned int passes;
	ConvParams():
		quality{24}, quality12{6},
	preset{-9}, preset12{-9},
	ncpu{1}, clamp{false}, passes{1}
	{ }
	//
	//-               quality{24}, quality12{3},
	//-       preset{-5}, preset12{-5},
	//

};
#else
struct ConvParams {
///////////////
///////////////
	unsigned int quality, quality12; // [0, 63]
	int preset, preset12; // [-9, 9]
	unsigned int ncpu; // {1, 2}, little improvement if >=3.
	bool clamp;
	ConvParams():
		quality{24}, quality12{6},
	preset{-9}, preset12{9},
	ncpu{1}, clamp{false}
	{ }
	//
	//-               quality{24}, quality12{3},
	//-       preset{-5}, preset12{-5},
	//

};
#endif
///
///uint16_t 12():12
///uint16_t 16():**12
///uint8_t 8():8


				//*******out meth
				//****clamp

/* Options */
#if 0
static const char* opts=":mq:p:s:j::";
static const struct option opts_long[]=
{
	{"msb", 0, nullptr, 'm'},
	{"quality", 1, nullptr, 'q'},
	{"preset", 1, nullptr, 'p'},
	{"stat", 1, nullptr, 's'},
	{"jobs", 2, nullptr, 'j'},
	{nullptr, 0, nullptr, 0}
};

#endif

gapr::cube_type decode(std::vector<char>& buf, const char* input_file, int64_t* pw, int64_t* ph, int64_t* pd) {
	auto str=gapr::make_streambuf(input_file);
	auto imageReader=gapr::make_cube_loader(input_file, *str);
	if(!imageReader)
		throw gapr::str_glue("Cannot read file: ", input_file);

	auto type=imageReader->type();
	if(type==gapr::cube_type::unknown)
		throw gapr::str_glue("Cannot get image type: ", input_file);

	switch(type) {
	case gapr::cube_type::u8:
	case gapr::cube_type::u16:
			break;
	case gapr::cube_type::u32:
	case gapr::cube_type::i8:
	case gapr::cube_type::i16:
	case gapr::cube_type::i32:
	case gapr::cube_type::f32:
			throw gapr::str_glue("Only 8/16 bit unsigned images supported.");
		default:
			throw gapr::str_glue("Voxel type not supported");
	}

	auto [w, h, d]=imageReader->sizes();
	if(false)
		throw gapr::str_glue("Cannot get image sizes: ", input_file);

	auto bpv=gapr::voxel_size(type);
	try {
		if(buf.size()<uint64_t(bpv)*w*h*d)
			buf.resize(voxel_size(type)*w*h*d);
	} catch(...) {
		throw gapr::str_glue("Failed to alloc cube memory");
	}

	imageReader->load(buf.data(), bpv*w, bpv*w*h);
	if(false)
		throw gapr::str_glue("Failed to read image data: ", input_file);

	*pw=w;
	*ph=h;
	*pd=d;
	return type;
}


template<typename CTX> struct codec_adapter;

#ifdef WITH_AV1
template<>
struct codec_adapter<aom_codec_ctx_t> {
	using codec_ctx_t=aom_codec_ctx_t;
	using codec_err_t=aom_codec_err_t;
	using image_t=aom_image_t;
	using codec_enc_cfg_t=aom_codec_enc_cfg_t;
	using codec_iter_t=aom_codec_iter_t;
	static constexpr char const* codec_id="V_AV1";
	static constexpr char const* codec_name="AV9";
	static constexpr aom_codec_err_t codec_ok=AOM_CODEC_OK;
	static aom_img_fmt_t img_fmt(uint8_t) { return AOM_IMG_FMT_I420; }
	static aom_img_fmt_t img_fmt(uint16_t) { return AOM_IMG_FMT_I42016; }
	static aom_bit_depth_t bit_depth(uint8_t) { return AOM_BITS_8; }
	static aom_bit_depth_t bit_depth(uint16_t) { return AOM_BITS_12; }
	static aom_codec_flags_t codec_flags(uint8_t) { return 0; }
	static aom_codec_flags_t codec_flags(uint16_t) { return AOM_CODEC_USE_HIGHBITDEPTH; }
	static aom_rc_mode rc_mode() { return AOM_Q; }
	static aom_kf_mode kf_mode() { return AOM_KF_AUTO; }
	static constexpr aom_codec_cx_pkt_kind frame_pkt() {
		return AOM_CODEC_CX_FRAME_PKT;
	}
	static constexpr aom_codec_cx_pkt_kind stats_pkt() {
		return AOM_CODEC_STATS_PKT;
	}
	static constexpr aom_codec_cx_pkt_kind pfmb_stats_pkt() {
		return AOM_CODEC_FPMB_STATS_PKT;
	}
	static aom_enc_pass one_pass() { return AOM_RC_ONE_PASS; }
	static aom_enc_pass first_pass() { return AOM_RC_FIRST_PASS; }
	static aom_enc_pass second_pass() { return AOM_RC_SECOND_PASS; }
	static aom_fixed_buf_t fixed_buf(std::vector<char>& buf) {
		return aom_fixed_buf_t{&buf[0], buf.size()};
	}
	static bool is_key(aom_codec_frame_flags_t flags) {
		return flags&AOM_FRAME_IS_KEY;
	}
	static std::string codec_version() {
		std::string s="libaom-";
		s+=aom_codec_version_str();
		return s;
	}
	static const char* codec_err_to_string(aom_codec_err_t err) {
		return aom_codec_err_to_string(err);
	}
	static aom_codec_err_t codec_enc_init(aom_codec_ctx_t* ctx, aom_codec_iface_t* iface, aom_codec_enc_cfg_t* cfg, aom_codec_flags_t flags) {
		return aom_codec_enc_init(ctx, iface, cfg, flags);
	}
	static aom_codec_err_t codec_destroy(aom_codec_ctx_t* ctx) {
		return aom_codec_destroy(ctx);
	}
	static aom_codec_iface_t* codec_iface() {
		return aom_codec_av1_cx();
	}
	static void set_img(aom_image_t* img, unsigned char* data) {
		img->planes[AOM_PLANE_Y]=data;
	}
	static void set_img(aom_image_t* img, int ystride, unsigned char* zeros) {
		img->stride[AOM_PLANE_Y]=ystride;
		img->stride[AOM_PLANE_U]=img->stride[AOM_PLANE_V]=0;
		img->planes[AOM_PLANE_U]=img->planes[AOM_PLANE_V]=zeros;
		//img->cs=AOM_CS_UNKNOWN;
		//img->range=AOM_CR_FULL_RANGE;
	}
	static aom_codec_err_t codec_enc_config(aom_codec_iface_t* iface, aom_codec_enc_cfg_t* cfg) {
		return aom_codec_enc_config_default(iface, cfg, AOM_USAGE_GOOD_QUALITY);
		//return aom_codec_enc_config_default(iface, cfg, AOM_USAGE_REALTIME);
		//return aom_codec_enc_config_default(iface, cfg, AOM_USAGE_ALL_INTRA);
		//AOM_USAGE_REALTIME
		//AOM_USAGE_ALL_INTRA
	}
	static aom_codec_err_t codec_encode(aom_codec_ctx_t* ctx, const aom_image_t* img, int64_t z) {
		//g/b
		//g
		return aom_codec_encode(ctx, img, z, 1, 0);
		//0|AOM_EFLAG_FORCE_KF
	}
	static const aom_codec_cx_pkt_t* codec_get_cx_data(aom_codec_ctx_t *ctx, aom_codec_iter_t *iter) {
		return aom_codec_get_cx_data(ctx, iter);
	}
	static void codec_controls(aom_codec_ctx_t* ctx, int preset, int quality) {
#ifdef XXX
		aom_codec_err_t err;
		//int 0..9
		err=aom_codec_control(ctx, AOME_SET_CPUUSED, preset);
		if(err!=codec_ok)
			throw gapr::str_glue("Failed to set codec param: ", codec_err_to_string(err));
		//int
		//AOM_TUNE_PSNR
		//AOM_TUNE_SSIM
		//AOM_TUNE_VMAF_WITH_PREPROCESSING
		//AOM_TUNE_VMAF_WITHOUT_PREPROCESSING
		//AOM_TUNE_VMAF_MAX_GAIN
		//AOM_TUNE_VMAF_NEG_MAX_GAIN
		//AOM_TUNE_BUTTERAUGLI
		err=aom_codec_control(ctx, AOME_SET_TUNING, AOM_TUNE_PSNR);
		if(err!=codec_ok)
			throw gapr::str_glue("Failed to set codec param: ", codec_err_to_string(err));
		//unsigned int 0..63
		err=aom_codec_control(ctx, AOME_SET_CQ_LEVEL, quality);
		if(err!=codec_ok)
			throw gapr::str_glue("Failed to set codec param: ", codec_err_to_string(err));
		//unsigned int 0/1
		err=aom_codec_control(ctx, AV1E_SET_FRAME_PARALLEL_DECODING, 1);
		if(err!=codec_ok)
			throw gapr::str_glue("Failed to set codec param: ", codec_err_to_string(err));
#endif
	}
};
#endif

template<>
struct codec_adapter<vpx_codec_ctx_t> {
	using codec_ctx_t=vpx_codec_ctx_t;
	using codec_err_t=vpx_codec_err_t;
	using image_t=vpx_image_t;
	using codec_enc_cfg_t=vpx_codec_enc_cfg_t;
	using codec_iter_t=vpx_codec_iter_t;
	static constexpr char const* codec_id="V_VP9";
	static constexpr char const* codec_name="VP9";
	static constexpr vpx_codec_err_t codec_ok=VPX_CODEC_OK;
	static vpx_img_fmt_t img_fmt(uint8_t) { return VPX_IMG_FMT_I420; }
	static vpx_img_fmt_t img_fmt(uint16_t) { return VPX_IMG_FMT_I42016; }
	static vpx_bit_depth_t bit_depth(uint8_t) { return VPX_BITS_8; }
	static vpx_bit_depth_t bit_depth(uint16_t) { return VPX_BITS_12; }
	static vpx_codec_flags_t codec_flags(uint8_t) { return 0; }
	static vpx_codec_flags_t codec_flags(uint16_t) { return VPX_CODEC_USE_HIGHBITDEPTH; }
	static vpx_rc_mode rc_mode() { return VPX_Q; }
	static vpx_kf_mode kf_mode() { return VPX_KF_AUTO; }
	static constexpr vpx_codec_cx_pkt_kind frame_pkt() {
		return VPX_CODEC_CX_FRAME_PKT;
	}
	static constexpr vpx_codec_cx_pkt_kind stats_pkt() {
		return VPX_CODEC_STATS_PKT;
	}
	static constexpr vpx_codec_cx_pkt_kind pfmb_stats_pkt() {
		return VPX_CODEC_FPMB_STATS_PKT;
	}
	static vpx_enc_pass one_pass() { return VPX_RC_ONE_PASS; }
	static vpx_enc_pass first_pass() { return VPX_RC_FIRST_PASS; }
	static vpx_enc_pass second_pass() { return VPX_RC_LAST_PASS; }
	static vpx_fixed_buf_t fixed_buf(std::vector<char>& buf) {
		return vpx_fixed_buf_t{&buf[0], buf.size()};
	}
	static bool is_key(vpx_codec_frame_flags_t flags) {
		return flags&VPX_FRAME_IS_KEY;
	}
	static std::string codec_version() {
		std::string s="libvpx-";
		s+=vpx_codec_version_str();
		return s;
	}
	static const char* codec_err_to_string(vpx_codec_err_t err) {
		return vpx_codec_err_to_string(err);
	}
	static vpx_codec_err_t codec_enc_init(vpx_codec_ctx_t* ctx, vpx_codec_iface_t* iface, vpx_codec_enc_cfg_t* cfg, vpx_codec_flags_t flags) {
		return vpx_codec_enc_init(ctx, iface, cfg, flags);
	}
	static vpx_codec_err_t codec_destroy(vpx_codec_ctx_t* ctx) {
		return vpx_codec_destroy(ctx);
	}
	static vpx_codec_iface_t* codec_iface() {
		return vpx_codec_vp9_cx();
	}
	static void set_img(vpx_image_t* img, unsigned char* data) {
		img->planes[VPX_PLANE_Y]=data;
	}
	static void set_img(vpx_image_t* img, int ystride, unsigned char* zeros) {
		img->stride[VPX_PLANE_Y]=ystride;
		img->stride[VPX_PLANE_ALPHA]=img->stride[VPX_PLANE_U]=img->stride[VPX_PLANE_V]=0;
		img->planes[VPX_PLANE_U]=img->planes[VPX_PLANE_V]=zeros;
		//img->cs=VPX_CS_UNKNOWN;
		//img->range=VPX_CR_FULL_RANGE;
	}
	static vpx_codec_err_t codec_enc_config(vpx_codec_iface_t* iface, vpx_codec_enc_cfg_t* cfg) {
		return vpx_codec_enc_config_default(iface, cfg, 0);
	}
	static vpx_codec_err_t codec_encode(vpx_codec_ctx_t* ctx, const vpx_image_t* img, int64_t z) {
		return vpx_codec_encode(ctx, img, z, 1, 0, VPX_DL_GOOD_QUALITY);
		//return vpx_codec_encode(ctx, img, z, 1, 0, 1000);
	}
	static const vpx_codec_cx_pkt_t* codec_get_cx_data(vpx_codec_ctx_t *ctx, vpx_codec_iter_t *iter) {
		return vpx_codec_get_cx_data(ctx, iter);
	}
	static void codec_controls(codec_ctx_t* ctx, int preset, int quality) {
		vpx_codec_err_t err;
		//int -9..9
		err=vpx_codec_control(ctx, VP8E_SET_CPUUSED, preset);
		if(err!=codec_ok)
			throw gapr::str_glue("Failed to set codec param: ", codec_err_to_string(err));
		//int VP8_TUNE_PSNR/VP8_TUNE_SSIM
		err=vpx_codec_control(ctx, VP8E_SET_TUNING, VP8_TUNE_PSNR);
		if(err!=codec_ok)
			throw gapr::str_glue("Failed to set codec param: ", codec_err_to_string(err));
		//unsigned int 0..63
		err=vpx_codec_control(ctx, VP8E_SET_CQ_LEVEL, quality);
		if(err!=codec_ok)
			throw gapr::str_glue("Failed to set codec param: ", codec_err_to_string(err));
		//unsigned int
		err=vpx_codec_control(ctx, VP9E_SET_FRAME_PARALLEL_DECODING, 1);
		if(err!=codec_ok)
			throw gapr::str_glue("Failed to set codec param: ", codec_err_to_string(err));
	}
};

///////////////////////////////////////////////////////

template<typename T> unsigned char* prepare_uv_plane(std::vector<char>& buf, int64_t width);
template<> unsigned char* prepare_uv_plane<uint8_t>(std::vector<char>& buf, int64_t width) {
	auto prev_size=buf.size();
	size_t size=(width+1)/2;
	if(prev_size<size)
		buf.resize(size, 0x80);
	return reinterpret_cast<unsigned char*>(&buf[0]);
}
template<> unsigned char* prepare_uv_plane<uint16_t>(std::vector<char>& buf, int64_t width) {
	auto prev_size=buf.size();
	size_t size=(width+1)/2*2;
	if(prev_size<size)
		buf.resize(size, 0x00);
	for(size_t i=prev_size+1; i<size; i+=2)
		buf[i]=0x08;
	return reinterpret_cast<unsigned char*>(&buf[0]);
}

static std::mutex _fork_mtx{};

template<typename T>
void convert_webm_filter(mkvmuxer::IMkvWriter& writer, T* data, unsigned int w, unsigned int h, unsigned int d, cube_enc_opts opts) {
	assert(opts.filter);
	std::ostringstream cmd{};
	cmd<<*opts.filter;
	if(opts.cq_level)
		cmd<<" --cq-level="<<*opts.cq_level;
	if(opts.cpu_used)
		cmd<<" --cpu-used="<<*opts.cpu_used;
	if(opts.threads)
		cmd<<" --threads="<<*opts.threads;
	if(opts.passes)
		cmd<<" --passes="<<*opts.passes;

	namespace bp=boost::process;
	struct use_pipeout_fds: bp::extend::handler, bp::extend::uses_handles {
		bp::pipe::native_handle_type src,sink;
		explicit constexpr use_pipeout_fds(const bp::pipe& pipe) noexcept:
			src{pipe.native_source()}, sink{pipe.native_sink()} { }
		auto get_used_handles() const {
			return std::array{src, sink};
		}
	};
	bp::group pgrp;
	bp::child c;
	std::unique_lock lck{_fork_mtx};
	bp::ipstream in;
	bp::opstream out;
	c=bp::child{cmd.str(), bp::limit_handles, pgrp, use_pipeout_fds{in.pipe()}, bp::std_in<out, bp::std_out>in, bp::std_err>stderr};
	if(!c.valid())
		throw std::runtime_error{"failed to launch"};
	lck.unlock();

	{
		gapr::nrrd_output nrrd{out, false};
		nrrd.header();
		nrrd.comment("convert temp file");
		nrrd.finish(data, w, h, d);
		out.close();
		out.pipe().close();
	}

	{
		std::string chk;
		std::getline(in, chk);
		if(!in)
			throw std::runtime_error{"error read chk"};
		int64_t totl{0};
		if(auto err=gapr::parse_tuple(&chk[0], &totl); !err)
			throw std::runtime_error{"error parse chk"};
		char buf[4096];
		while(true) {
			in.read(buf, 4096);
			std::size_t n=in.gcount();
			if(writer.Write(&buf[0], n)!=0)
				throw std::runtime_error{"error write file"};
			if(!in) {
				if(!in.eof())
					throw std::runtime_error{"error read"};
				break;
			}
		}
		if(writer.Position()!=totl)
			throw std::runtime_error{"error validate chk"};
	}
	c.wait();
	if(c.exit_code()!=0)
		throw std::runtime_error{"retcode nonzero"};
}

static void handle_16bit(bool clamp, uint16_t* ptr, std::size_t n);
template<typename CTX, typename T>
void encode(mkvmuxer::IMkvWriter& writer, T* cube_, std::vector<char>& zerobuf, int64_t width, int64_t height, int64_t depth, cube_enc_opts opts) {
	if(opts.filter)
		return convert_webm_filter(writer, cube_, width, height, depth, opts);
	ConvParams pars{};
	if(opts.cq_level)
		pars.quality=pars.quality12=*opts.cq_level;
	if(opts.cpu_used)
		pars.preset=pars.preset12=*opts.cpu_used;
	if(opts.threads)
		pars.ncpu=*opts.threads;
	if(opts.passes)
		pars.passes=*opts.passes;
	pars.clamp=false;
	if constexpr(std::is_same_v<T, uint16_t>)
		handle_16bit(pars.clamp, cube_, width*height*depth);
	auto cube=reinterpret_cast<unsigned char*>(cube_);

	using Adapter=codec_adapter<CTX>;
	std::vector<char> buf_stats{};
	std::vector<char> buf_mb_stats{};

	auto preset=std::is_same<T, uint16_t>::value?pars.preset12:pars.preset;
	auto quality=std::is_same<T, uint16_t>::value?pars.quality12:pars.quality;

	auto t0=std::chrono::steady_clock::now();
	auto iface=Adapter::codec_iface();
	if(!iface)
		throw gapr::str_glue("Failed to get ", Adapter::codec_name, " interface.");

	typename Adapter::image_t img;
	memset(&img, 0, sizeof(img));
	img.img_data=cube;
	img.fmt=Adapter::img_fmt(T{});
	img.bit_depth=sizeof(T)*8;
	img.w=img.d_w=img.r_w=width;
	img.h=img.d_h=img.r_h=height;
	img.x_chroma_shift=1;
	img.y_chroma_shift=1;
	img.bps=std::is_same<T, uint16_t>::value?24:12;
	auto ystride=width*sizeof(T);
	Adapter::set_img(&img, ystride, prepare_uv_plane<T>(zerobuf, width));

	typename Adapter::codec_enc_cfg_t cfg;
	auto err=Adapter::codec_enc_config(iface, &cfg);
	if(err!=Adapter::codec_ok)
		throw gapr::str_glue("Failed to get config: ", Adapter::codec_err_to_string(err));
	//XXX cfg.g_usage=VPX_CQ;
	//std::cerr<<cfg.g_usage<<"\n";
	cfg.g_threads=pars.ncpu;
	cfg.g_profile=std::is_same<T, uint16_t>::value?2:0;
	cfg.g_w=width;
	cfg.g_h=height;
	cfg.g_bit_depth=Adapter::bit_depth(T{});
	cfg.g_input_bit_depth=std::is_same<T, uint16_t>::value?12:8;
	cfg.g_timebase.num=50;
	cfg.g_timebase.den=1000;
	//cfg.g_lag_in_frames=;
	cfg.rc_dropframe_thresh=0;
	//cfg.rc_resize_allowed=0;
	cfg.rc_end_usage=Adapter::rc_mode();
	cfg.kf_mode=Adapter::kf_mode();
	cfg.kf_min_dist=0;
	cfg.kf_max_dist=0;

	typename Adapter::codec_ctx_t ctx;
	// XXX twopass not working well (bad quality), maybe no reusing cfg/encoder...
	if(pars.passes>1) {
	cfg.g_pass=Adapter::first_pass();
	err=Adapter::codec_enc_init(&ctx, iface, &cfg, Adapter::codec_flags(T{}));
	if(err!=Adapter::codec_ok)
		throw gapr::str_glue("Failed to init encoder: ", Adapter::codec_err_to_string(err));
	Adapter::codec_controls(&ctx, preset, quality);

	bool got_pkt=false;
	for(int64_t z=0; z<=depth || got_pkt; z++) {
		if(z<depth) {
			Adapter::set_img(&img, cube+ystride*height*z);
		}
		err=Adapter::codec_encode(&ctx, z<depth?&img:nullptr, z);
		if(err!=Adapter::codec_ok)
			throw gapr::str_glue("Failed to encode frame: ", Adapter::codec_err_to_string(err));

		got_pkt=false;
		typename Adapter::codec_iter_t iter=nullptr;
		while(auto pkt=Adapter::codec_get_cx_data(&ctx, &iter)) {
			const char* ptr;
			switch(pkt->kind) {
			case Adapter::stats_pkt():
					ptr=static_cast<char*>(pkt->data.twopass_stats.buf);
					buf_stats.reserve(buf_stats.size()+pkt->data.twopass_stats.sz);
					buf_stats.insert(buf_stats.end(), ptr, ptr+pkt->data.twopass_stats.sz);
					got_pkt=true;
					break;
			case Adapter::pfmb_stats_pkt():
					ptr=static_cast<char*>(pkt->data.firstpass_mb_stats.buf);
					buf_mb_stats.reserve(buf_mb_stats.size()+pkt->data.firstpass_mb_stats.sz);
					buf_mb_stats.insert(buf_mb_stats.end(), ptr, ptr+pkt->data.firstpass_mb_stats.sz);
					break;
				default:
					break;
			}
		}
	}
	err=Adapter::codec_destroy(&ctx);
	if(err!=Adapter::codec_ok)
		throw gapr::str_glue("Failed to destroy context: ", Adapter::codec_err_to_string(err));
	}

	mkvmuxer::Segment segment{};
	segment.Init(&writer);
	segment.set_mode(mkvmuxer::Segment::kFile);
	segment.OutputCues(true);
	if(std::is_same_v<T, uint16_t> && !pars.clamp) {
		auto tags=segment.AddTag();
		tags->add_simple_tag("Gapr16to12", "1");
	}
	mkvmuxer::SegmentInfo* seginfo=segment.GetSegmentInfo();
	seginfo->set_timecode_scale(1000000);
	std::string verstr{PACKAGE_NAME " ("};
	(verstr+=Adapter::codec_version())+=")";
	seginfo->set_writing_app(verstr.c_str());
	auto track_id=segment.AddVideoTrack(width, height, 1);
	mkvmuxer::VideoTrack* track=static_cast<mkvmuxer::VideoTrack*>(segment.GetTrackByNumber(track_id));
	track->SetStereoMode(mkvmuxer::VideoTrack::kMono);
	track->set_codec_id(Adapter::codec_id);
	track->set_display_width(width);
	track->set_display_height(height);

	if(pars.passes>1) {
	cfg.g_pass=Adapter::second_pass();
	cfg.rc_twopass_stats_in=Adapter::fixed_buf(buf_stats);
	cfg.rc_firstpass_mb_stats_in=Adapter::fixed_buf(buf_mb_stats);
	} else {
	cfg.g_pass=Adapter::one_pass();
	}
	err=Adapter::codec_enc_init(&ctx, iface, &cfg, Adapter::codec_flags(T{}));
	if(err!=Adapter::codec_ok)
		throw std::runtime_error{gapr::str_glue("Failed to init encoder: ", Adapter::codec_err_to_string(err)).str()};
	Adapter::codec_controls(&ctx, preset, quality);

	bool get_pkt=false;
	for(int64_t z=0; z<=depth || get_pkt; z++) {
		if(z<depth) {
			Adapter::set_img(&img, cube+ystride*height*z);
		}
		err=Adapter::codec_encode(&ctx, z<depth?&img:nullptr, z);
		if(err!=Adapter::codec_ok)
			throw gapr::str_glue("Failed to encode frame: ", Adapter::codec_err_to_string(err));

		get_pkt=false;
		typename Adapter::codec_iter_t iter=nullptr;
		while(auto pkt=Adapter::codec_get_cx_data(&ctx, &iter)) {
			int64_t pts_ns;
			switch(pkt->kind) {
			case Adapter::frame_pkt():
					pts_ns=pkt->data.frame.pts*1000000000ll*cfg.g_timebase.num/cfg.g_timebase.den;
					segment.AddFrame(static_cast<uint8_t*>(pkt->data.frame.buf), pkt->data.frame.sz, 1, pts_ns, Adapter::is_key(pkt->data.frame.flags));
					get_pkt=true;
					break;
				default:
					break;
			}
		}
	}
	err=Adapter::codec_destroy(&ctx);
	if(err!=Adapter::codec_ok)
		throw gapr::str_glue("Failed to destroy context: ", Adapter::codec_err_to_string(err));

	segment.Finalize();
	auto t1=std::chrono::steady_clock::now();
	if(0)
	std::cerr<<std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count()<<" ms\n";

}

uint16_t enc16(uint16_t x) {
	if(x<1024)
		return x;
	unsigned int i=2;
	x>>=1;
	while(x>=1024) {
		x>>=1;
		i+=1;
	}
	// now 512<=x<1024, only 9 bits can vary
	x=x&0x1ff;
	return (i<<9)|x;
}
//here or there
template<bool clamp>
static void handle_16bit(uint16_t* ptr, std::size_t n) {
	auto ptre=ptr+n;
	while(ptr<ptre) {
		auto v=*ptr;
		if constexpr(clamp) {
			if(v>=0x1000)
				v=0xfff;
		} else {
			v=enc16(v);
		}
		assert(v<0x1000);
		//*ptr=v>>4;
		*ptr=v;
		ptr++;
	}
}
static void handle_16bit(bool clamp, uint16_t* ptr, std::size_t n) {
	if(clamp) {
		handle_16bit<true>(ptr, n);
	} else {
		handle_16bit<false>(ptr, n);
	}
}

void convert_file(const char* input_fn, const char* output_fn, cube_enc_opts opts, std::vector<char>& buf, std::vector<char>& zerobuf) {
	int64_t w, h, d;
	auto type=decode(buf, input_fn, &w, &h, &d);
	gapr::file_stream webm_file{output_fn, "wb"};
	if(!webm_file)
		throw gapr::str_glue("Failed to open output file.");
	mkvmuxer::MkvWriter writer{webm_file};
	switch(type) {
	case gapr::cube_type::u8:
			encode<vpx_codec_ctx_t, uint8_t>(writer, reinterpret_cast<uint8_t*>(buf.data()), zerobuf, w, h, d, opts);
			break;
	case gapr::cube_type::u16:
			encode<vpx_codec_ctx_t, uint16_t>(writer, reinterpret_cast<uint16_t*>(buf.data()), zerobuf, w, h, d, opts);
			break;
	case gapr::cube_type::u32:
	case gapr::cube_type::i8:
	case gapr::cube_type::i16:
	case gapr::cube_type::i32:
	case gapr::cube_type::f32:
			throw gapr::str_glue("Only 8/16 bit unsigned images supported.");
		default:
			throw gapr::str_glue("Voxel type not supported");
	}
	if(fclose(webm_file)!=0)
		throw gapr::str_glue("Failed to close file.");
}

void batchConvert(const ConvParams& pars, const char* input_pat, const char* output_pat, const char* stat_pat, int ncpu) {
			//auto input_fn=patternSubst(input_pat, x, y, z);
			//auto output_fn=patternSubst(output_pat, x, y, z);
			//auto output_path=output_fn;
			//dirname(&output_path[0]);
			//std::string stat_path{};
			{
				//std::cout<<ni<<'/'<<nn<<' '<<x<<':'<<y<<':'<<z<<'\n';
				//createDirectory(output_path);
				//if(stat_pat)
					//createDirectory(stat_path);
			}

			//convert(pars, input_fn.c_str(), output_fn.c_str(), buf, zerobuf, 1);
}
#if 0
				case 'x':
					if(!parseTuple(optarg, &pars.xleft, &pars.xdelta, &pars.xright))
						throw gapr::str_glue("invalid argument for '", argv[prev_optind], "'\nPlease give 3 integers separated by ':'.\n");
					if(pars.xleft>=pars.xright)
						throw gapr::str_glue("invalid argument for '", argv[prev_optind], "'\nPlease give a valid range.\n");
					if(pars.xdelta==0)
						throw gapr::str_glue("invalid argument for '", argv[prev_optind], "'\nSetep size must be positive.\n");
					break;
				case 'y':
					if(!parseTuple(optarg, &pars.yleft, &pars.ydelta, &pars.yright))
						throw gapr::str_glue("invalid argument for '", argv[prev_optind], "'\nPlease give 3 integers separated by ':'.\n");
					if(pars.yleft>=pars.yright)
						throw gapr::str_glue("invalid argument for '", argv[prev_optind], "'\nPlease give a valid range.\n");
					if(pars.ydelta==0)
						throw gapr::str_glue("invalid argument for '", argv[prev_optind], "'\nSetep size must be positive.\n");
					break;
				case 'z':
					if(!parseTuple(optarg, &pars.zleft, &pars.zdelta, &pars.zright))
						throw gapr::str_glue("invalid argument for '", argv[prev_optind], "'\nPlease give 3 integers separated by ':'.\n");
					if(pars.zleft>=pars.zright)
						throw gapr::str_glue("invalid argument for '", argv[prev_optind], "'\nPlease give a valid range.\n");
					if(pars.zdelta==0)
						throw gapr::str_glue("invalid argument for '", argv[prev_optind], "'\nSetep size must be positive.\n");
					break;
				case 'm':
					pars.use_msb=true;
					break;
				case 'q':
					if(!parseTuple<int>(optarg, &pars.quality))
						throw gapr::str_glue("invalid argument for '", argv[prev_optind], "'.\nPlease give an integer.\n");
					if(pars.quality<0 || pars.quality>=64)
						throw gapr::str_glue("invalid argument for '", argv[prev_optind], "'.\nThe range for quality is [0, 64).\n");
					pars.quality12=pars.quality;
					break;
				case 'p':
					if(!parseTuple<int>(optarg, &pars.preset))
						throw gapr::str_glue("invalid argument for '", argv[prev_optind], "'.\nPlease give an integer.\n");
					if(pars.preset<-8 || pars.preset>8)
						throw gapr::str_glue("invalid argument for '", argv[prev_optind], "'.\nThe range for preset is [-8, 8].\n");
					pars.preset12=pars.preset;
					break;
					if(optarg) {
						if(!parseTuple<int>(optarg, &ncpu))
							throw gapr::str_glue("invalid argument for '", argv[prev_optind], "'.\nPlease give an integer.\n");
						if(ncpu<=0)
							throw gapr::str_glue("invalid argument for '", argv[prev_optind], "'.\nNeed a positive integer.\n");
					} else {
						ncpu=getNumberOfCpus();
						if(ncpu<1)
							ncpu=1;
					}
					break;
#endif


template<typename T>
void compare_impl(T* ptr, T* ptr2, int64_t w, int64_t h, int64_t d) {
	std::vector<int64_t> hist(1<<(sizeof(T)*8), 0);
	for(int64_t i=0; i<w*h*d; i++) {
		double v1=ptr[i];
		double v2=ptr2[i];
		T diff=v2-v1;
		hist[diff]++;
	}
	for(size_t i=0; i<hist.size(); i++) {
		if(hist[i]) {
			int d;
			if(i>30000)
				d=i-65536;
			else
				d=i;
			fprintf(stdout, "%d %" PRId64 "\n", d, hist[i]);
		}
	}
}
void compare(const char* input_fn, const char* output_fn) {
	std::vector<char> buf,buf2;
	int64_t w, h, d;
	int64_t w2, h2, d2;
	auto type=decode(buf, input_fn, &w, &h, &d);
	auto type2=decode(buf2, output_fn, &w2, &h2, &d2);
	assert(type==type2);
	assert(w==w2);
	assert(h==h2);
	assert(d==d2);
	switch(type) {
	case gapr::cube_type::u8:
		compare_impl<uint8_t>(reinterpret_cast<uint8_t*>(&buf[0]), reinterpret_cast<uint8_t*>(&buf2[0]), w, h, d);
		break;
	case gapr::cube_type::u16:
		compare_impl<uint16_t>(reinterpret_cast<uint16_t*>(&buf[0]), reinterpret_cast<uint16_t*>(&buf2[0]), w, h, d);
		break;
	default:
		throw gapr::str_glue("Voxel type not supported");
	}
}

class mem_file_writer : public mkvmuxer::IMkvWriter {
public:
	mem_file_writer(): mkvmuxer::IMkvWriter{} { }
	~mem_file_writer() override { }
	mem_file_writer(const mem_file_writer&) =delete;
	mem_file_writer& operator=(const mem_file_writer&) =delete;

	mkvmuxer::int64 Position() const override {
		//fprintf(stderr, "tell: %zd\n", _off);
		return _off;
	}
	mkvmuxer::int32 Position(mkvmuxer::int64 position) override {
		_off=position;
		//fprintf(stderr, "seek: %zd\n", _off);
		return 0;
	}
	bool Seekable() const override {
		return true;
	}
	mkvmuxer::int32 Write(const void* buffer, mkvmuxer::uint32 length) override {
		//fprintf(stderr, "pre write: %zd %u %p\n", _off, length, buffer);
		std::size_t prev_off=_off;
		while(_off<_file.size()) {
			if(length<=0)
				break;
			auto buf=_file.map(_off);
			auto n=buf.size();
			if(n>length)
				n=length;
			if(n+_off>_file.size())
				n=_file.size()-_off;
			std::memcpy(buf.data(), buffer, n);
			buffer=static_cast<const char*>(buffer)+n;
			length-=n;
			_off+=n;
		}
		while(length>0) {
			auto buf=_file.map_tail();
			auto n=buf.size();
			if(n>length)
				n=length;
			std::memcpy(buf.data(), buffer, n);
			_file.add_tail(n);
			_off+=n;
			buffer=static_cast<const char*>(buffer)+n;
			length-=n;
		}
		//fprintf(stderr, "write: %zd\n", _off);
		return length==0?0:-1;
		//return (_off-prev_off==length)?0:-1;
	}
	void ElementStartNotify(mkvmuxer::uint64 element_id, mkvmuxer::int64 position) override { }
	void open() {
		_file=gapr::mutable_mem_file{false};
		_off=0;
	}
	gapr::mutable_mem_file mem_file() && noexcept {
		return std::move(_file);
	}

private:
	gapr::mutable_mem_file _file;
	std::size_t _off;
};

template<typename T>
gapr::mem_file convert_webm(T* data, unsigned int w, unsigned int h, unsigned int d, cube_enc_opts opts) {
	mem_file_writer writer;
	writer.open();
	std::vector<char> zerobuf;
	encode<vpx_codec_ctx_t, T>(writer, data, zerobuf, w, h, d, opts);
	return std::move(writer).mem_file();
}
template gapr::mem_file convert_webm<uint8_t>(uint8_t* data, unsigned int w, unsigned int h, unsigned int d, cube_enc_opts opts);
template gapr::mem_file convert_webm<uint16_t>(uint16_t* data, unsigned int w, unsigned int h, unsigned int d, cube_enc_opts opts);
