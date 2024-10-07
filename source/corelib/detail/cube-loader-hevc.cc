#include "gapr/cube-loader.hh"

#include "gapr/streambuf.hh"
#include "gapr/cube.hh"

#include <chrono>
#include <atomic>
#include <list>
#include <mutex>

#ifdef HAVE_OPENMP
#include <omp.h>
#else
inline int omp_get_max_threads() { return 1; }
inline int omp_get_thread_num() { return 0; }
#endif

#define EXTENSION_360_VIDEO 0
#define RExt__HIGH_BIT_DEPTH_SUPPORT 1

//#pragma GCC diagnostic push
//#pragma GCC diagnostic ignored "-Wsign-compare"
//#pragma GCC diagnostic ignored "-Wclass-memaccess"
#include <TLibVideoIO/TVideoIOYuv.h>
#include <TLibCommon/TComList.h>
#include <TLibCommon/TComPicYuv.h>
#include <TLibDecoder/TDecTop.h>
#include <TLibCommon/CommonDef.h>
#include <TLibDecoder/AnnexBread.h>
#include <TLibDecoder/NALread.h>
//#pragma GCC diagnostic pop


static bool print_dbg{false};

class HevcLoader: public gapr::cube_loader {
	public:
		HevcLoader(gapr::Streambuf& file):
			gapr::cube_loader{file},
			fs{&file},
			supported{false}
		{
			supported=probe();
		}
		~HevcLoader() override { }
	private:
		std::istream fs;

		Bool m_respectDefDispWindow{false};
		bool supported;

		std::mutex mtx;
		using Slice=std::vector<UChar>;

		constexpr static std::size_t MAX_PAR{64};
		struct Pars {
			std::list<InputNALUnit> nalus;
			std::list<Slice> imgs;
			std::size_t width{0}, height{0};
			char padding[128];
		};

		std::array<Pars, MAX_PAR> pars{};

		void do_load(char* ptr, int64_t ystride, int64_t zstride) override;
		bool probe();
		bool probe_impl(std::size_t job_idx);

		bool  xWriteOutput      (std::size_t job_idx, Int& m_iPOCLastDisplay, TComList<TComPic*>* pcListPic , UInt tId); ///< write YUV to file
		bool  xFlushOutput      (std::size_t job_idx, Int& m_iPOCLastDisplay, TComList<TComPic*>* pcListPic ); ///< flush all remaining decoded pictures to file
		Bool write(std::size_t job_idx, TComPicYuv* pPicYuvUser, Int confLeft, Int confRight, Int confTop, Int confBottom, ChromaFormat format);
		Bool writePlane(std::size_t job_idx, Pel* src, Bool is16bit, UInt stride444, UInt width444, UInt height444, const ComponentID compID, const ChromaFormat srcFormat, const ChromaFormat fileFormat, const UInt fileBitDepth);
};


// ====================================================================================================================
// ====================================================================================================================
// Public member functions
// ====================================================================================================================

Bool HevcLoader::writePlane(std::size_t job_idx, Pel* src, Bool is16bit,
		UInt stride444,
		UInt width444, UInt height444,
		const ComponentID compID,
		const ChromaFormat srcFormat,
		const ChromaFormat fileFormat,
		const UInt fileBitDepth)
{
	const UInt csx_file =getComponentScaleX(compID, fileFormat);
	const UInt csy_file =getComponentScaleY(compID, fileFormat);
	const UInt csx_src  =getComponentScaleX(compID, srcFormat);
	const UInt csy_src  =getComponentScaleY(compID, srcFormat);

	const UInt stride_src      = stride444>>csx_src;

	const UInt stride_file      = (width444 * (is16bit ? 2 : 1)) >> csx_file;
	const UInt width_file       = width444 >>csx_file;
	const UInt height_file      = height444>>csy_file;
	if(!pars[job_idx].width || !pars[job_idx].height) {
		pars[job_idx].width=width_file;
		pars[job_idx].height=height_file;
	}

	std::vector<UChar> bufVec(stride_file*height_file);
	UChar *buf=&(bufVec[0]);

	const UInt mask_y_file=(1<<csy_file)-1;
	const UInt mask_y_src =(1<<csy_src )-1;
	for(UInt y444=0; y444<height444; y444++)
	{
		if ((y444&mask_y_file)==0)
		{
			if (csx_file < csx_src)
			{
				// eg file is 444, source is 422.
				const UInt sx=csx_src-csx_file;
				if (!is16bit)
				{
					for (UInt x = 0; x < width_file; x++)
					{
						buf[x] = (UChar)(src[x>>sx]);
					}
				}
				else
				{
					for (UInt x = 0; x < width_file; x++)
					{
						buf[2*x  ] = (src[x>>sx]>>0) & 0xff;
						buf[2*x+1] = (src[x>>sx]>>8) & 0xff;
					}
				}
			}
			else
			{
				// eg file is 422, src is 444.
				const UInt sx=csx_file-csx_src;
				if (!is16bit)
				{
					for (UInt x = 0; x < width_file; x++)
					{
						buf[x] = (UChar)(src[x<<sx]);
					}
				}
				else
				{
					for (UInt x = 0; x < width_file; x++)
					{
						buf[2*x  ] = (src[x<<sx]>>0) & 0xff;
						buf[2*x+1] = (src[x<<sx]>>8) & 0xff;
					}
				}
			}
			buf+=stride_file;

		}

		if ((y444&mask_y_src)==0)
		{
			src += stride_src;
		}

	}
	pars[job_idx].imgs.emplace_back(std::move(bufVec));
	return true;
}
Bool HevcLoader::write(std::size_t job_idx, TComPicYuv* pPicYuvUser, Int confLeft, Int confRight, Int confTop, Int confBottom, ChromaFormat format)
{
	TComPicYuv *pPicYuv=pPicYuvUser;

	// compute actual YUV frame size excluding padding size


	TComPicYuv *dstPicYuv = NULL;
	Bool retval = true;
	if (format>=NUM_CHROMA_FORMAT)
	{
		format=pPicYuv->getChromaFormat();
	}

	dstPicYuv = pPicYuv;

	const Int  stride444 = dstPicYuv->getStride(COMPONENT_Y);
	const UInt width444  = dstPicYuv->getWidth(COMPONENT_Y) - confLeft - confRight;
	const UInt height444 = dstPicYuv->getHeight(COMPONENT_Y) -  confTop  - confBottom;

	if ((width444 == 0) || (height444 == 0))
	{
		printf ("\nWarning: writing %d x %d luma sample output picture!", width444, height444);
	}

	for(UInt comp=0; retval && comp<dstPicYuv->getNumberValidComponents(); comp++)
	{
		const ComponentID compID = ComponentID(comp);
		//const ChannelType ch=toChannelType(compID);
		const UInt csx = dstPicYuv->getComponentScaleX(compID);
		const UInt csy = dstPicYuv->getComponentScaleY(compID);
		const Int planeOffset =  (confLeft>>csx) + (confTop>>csy) * dstPicYuv->getStride(compID);
		bool is16bit=true;
		if (! writePlane(job_idx, dstPicYuv->getAddr(compID) + planeOffset, is16bit, stride444, width444, height444, compID, dstPicYuv->getChromaFormat(), format, is16bit?16:8))
		{
			retval=false;
		}
	}
	return retval;
}
/**
  - create internal class
  - initialize internal class
  - until the end of the bitstream, call decoding function in TDecTop class
  - delete allocated buffers
  - destroy internal class
  .
  */
const char* nalu_types[]={
	"NAL_UNIT_CODED_SLICE_TRAIL_N",
	"NAL_UNIT_CODED_SLICE_TRAIL_R",

	"NAL_UNIT_CODED_SLICE_TSA_N",
	"NAL_UNIT_CODED_SLICE_TSA_R",

	"NAL_UNIT_CODED_SLICE_STSA_N",
	"NAL_UNIT_CODED_SLICE_STSA_R",

	"NAL_UNIT_CODED_SLICE_RADL_N",
	"NAL_UNIT_CODED_SLICE_RADL_R",

	"NAL_UNIT_CODED_SLICE_RASL_N",
	"NAL_UNIT_CODED_SLICE_RASL_R",

	"NAL_UNIT_RESERVED_VCL_N10",
	"NAL_UNIT_RESERVED_VCL_R11",
	"NAL_UNIT_RESERVED_VCL_N12",
	"NAL_UNIT_RESERVED_VCL_R13",
	"NAL_UNIT_RESERVED_VCL_N14",
	"NAL_UNIT_RESERVED_VCL_R15",

	"NAL_UNIT_CODED_SLICE_BLA_W_LP",
	"NAL_UNIT_CODED_SLICE_BLA_W_RADL",
	"NAL_UNIT_CODED_SLICE_BLA_N_LP",
	"NAL_UNIT_CODED_SLICE_IDR_W_RADL",
	"NAL_UNIT_CODED_SLICE_IDR_N_LP",
	"NAL_UNIT_CODED_SLICE_CRA",
	"NAL_UNIT_RESERVED_IRAP_VCL22",
	"NAL_UNIT_RESERVED_IRAP_VCL23",

	"NAL_UNIT_RESERVED_VCL24",
	"NAL_UNIT_RESERVED_VCL25",
	"NAL_UNIT_RESERVED_VCL26",
	"NAL_UNIT_RESERVED_VCL27",
	"NAL_UNIT_RESERVED_VCL28",
	"NAL_UNIT_RESERVED_VCL29",
	"NAL_UNIT_RESERVED_VCL30",
	"NAL_UNIT_RESERVED_VCL31",

	"NAL_UNIT_VPS",
	"NAL_UNIT_SPS",
	"NAL_UNIT_PPS",
	"NAL_UNIT_ACCESS_UNIT_DELIMITER",
	"NAL_UNIT_EOS",
	"NAL_UNIT_EOB",
	"NAL_UNIT_FILLER_DATA",
	"NAL_UNIT_PREFIX_SEI",
	"NAL_UNIT_SUFFIX_SEI",

	"NAL_UNIT_RESERVED_NVCL41",
	"NAL_UNIT_RESERVED_NVCL42",
	"NAL_UNIT_RESERVED_NVCL43",
	"NAL_UNIT_RESERVED_NVCL44",
	"NAL_UNIT_RESERVED_NVCL45",
	"NAL_UNIT_RESERVED_NVCL46",
	"NAL_UNIT_RESERVED_NVCL47",
	"NAL_UNIT_UNSPECIFIED_48",
	"NAL_UNIT_UNSPECIFIED_49",
	"NAL_UNIT_UNSPECIFIED_50",
	"NAL_UNIT_UNSPECIFIED_51",
	"NAL_UNIT_UNSPECIFIED_52",
	"NAL_UNIT_UNSPECIFIED_53",
	"NAL_UNIT_UNSPECIFIED_54",
	"NAL_UNIT_UNSPECIFIED_55",
	"NAL_UNIT_UNSPECIFIED_56",
	"NAL_UNIT_UNSPECIFIED_57",
	"NAL_UNIT_UNSPECIFIED_58",
	"NAL_UNIT_UNSPECIFIED_59",
	"NAL_UNIT_UNSPECIFIED_60",
	"NAL_UNIT_UNSPECIFIED_61",
	"NAL_UNIT_UNSPECIFIED_62",
	"NAL_UNIT_UNSPECIFIED_63",
	"NAL_UNIT_INVALID",
};

bool is_idr(NalUnitType type) {
	switch(type) {
		case NAL_UNIT_CODED_SLICE_CRA:
		case NAL_UNIT_CODED_SLICE_IDR_W_RADL:
			return true;
		default: break;
	}
	return false;
}
bool HevcLoader::probe() {
	auto t000=std::chrono::steady_clock::now();

	std::size_t nbytes=0;
	std::list<InputNALUnit> nalus;
	{
		InputByteStream bytestream(fs);

		while (!!fs) {
			AnnexBStats stats = AnnexBStats();
			nalus.emplace_front();
			auto& nalu=nalus.front();
			byteStreamNALUnit(bytestream, nalu.getBitstream().getFifo(), stats);
			if(nalu.getBitstream().getFifo().empty()) {
				nalus.pop_front();
			} else {
				read(nalu);
				if(nalu.m_nalUnitType == NAL_UNIT_EOS)
					nalus.pop_front();
				nbytes+=nalu.getBitstream().getFifo().size();
			}
		}
	}

	std::array<bool, MAX_PAR> no_first{false, };

#ifdef _WIN64
	std::size_t num_par=1;
#else
	std::size_t num_par=omp_get_max_threads();
#endif
	if(num_par>MAX_PAR)
		num_par=MAX_PAR;
	if(num_par>4)
		num_par=4; // worse when bigger

	{
		std::size_t acc_size{0};
		std::size_t idx=num_par-1;
		while(!nalus.empty()) {
			auto n=nalus.front().getBitstream().getFifo().size();
			if(idx<=0 || (acc_size+n)*num_par<nbytes*(num_par-idx)) {
				pars[idx].nalus.splice(pars[idx].nalus.begin(), nalus, nalus.begin());
				acc_size+=n;
				continue;
			}
			auto it_slice=pars[idx].nalus.begin();
			while(it_slice!=pars[idx].nalus.end()) {
				if(it_slice->m_nalUnitType!=NAL_UNIT_VPS
						&& it_slice->m_nalUnitType!=NAL_UNIT_SPS
						&& it_slice->m_nalUnitType!=NAL_UNIT_PPS)
					break;
				++it_slice;
			}
			if(it_slice==pars[idx].nalus.end()) {
				pars[idx].nalus.clear();
				idx--;
				continue;
			}

			if(!is_idr(it_slice->m_nalUnitType)) {
				std::size_t dist=0;
				std::size_t dist2=0;
				auto it=nalus.begin();
				while(it!=nalus.end()) {
					auto n=it->getBitstream().getFifo().size();
					dist+=n;
					dist2+=1;
					if(is_idr(it->m_nalUnitType))
						break;
					++it;
				}
				if(it==nalus.end()) {
					while(!nalus.empty()) {
						auto n=nalus.front().getBitstream().getFifo().size();
						pars[idx].nalus.splice(pars[idx].nalus.begin(), nalus, nalus.begin());
						acc_size+=n;
					}
					break;
				}
				if(it==nalus.begin() || dist*(num_par*4)<nbytes || dist2<6) {
					while(true) {
						auto it2=nalus.begin();
						auto n=it2->getBitstream().getFifo().size();
						pars[idx].nalus.splice(pars[idx].nalus.begin(), nalus, it2);
						acc_size+=n;
						if(it2==it)
							break;
					}
					if(print_dbg)
						fprintf(stdout, "move idr\n");
				} else {
					pars[idx].nalus.emplace_front(*it);
					no_first[idx]=true;
					if(print_dbg)
						fprintf(stdout, "copy idr\n");
				}
				it_slice=pars[idx].nalus.begin();
			}

			bool has_vps{false}, has_sps{false}, has_pps{false};
			if(it_slice!=pars[idx].nalus.begin()) {
				do {
					--it_slice;
					switch(it_slice->m_nalUnitType) {
						case NAL_UNIT_VPS:
							has_vps=true;
							break;
						case NAL_UNIT_SPS:
							has_sps=true;
							break;
						case NAL_UNIT_PPS:
							has_pps=true;
							break;
						default: break;
					}
				} while(it_slice!=pars[idx].nalus.begin());
			}

			auto it=nalus.begin();
			while((!has_vps || !has_sps || !has_pps) && it!=nalus.end()) {
				bool hit{it==nalus.begin()};
				switch(it->m_nalUnitType) {
					case NAL_UNIT_VPS:
						hit=hit||!has_vps;
						has_vps=true;
						break;
					case NAL_UNIT_SPS:
						hit=hit||!has_sps;
						has_sps=true;
						break;
					case NAL_UNIT_PPS:
						hit=hit||!has_pps;
						has_pps=true;
						break;
					default:
						hit=false;
				}
				if(hit) {
					if(it!=nalus.begin()) {
						pars[idx].nalus.emplace_front(*it);
						++it;
					} else {
						auto n=it->getBitstream().getFifo().size();
						pars[idx].nalus.splice(pars[idx].nalus.begin(), nalus, it);
						acc_size+=n;
						it=nalus.begin();
					}
				} else {
					++it;
				}
			}
			idx--;
		}
	}

	if(print_dbg) {
		for(std::size_t idx=0; idx<num_par; idx++) {
			fprintf(stdout, "GRP %zu %d\n", idx, no_first[idx]);
			for(auto& nal: pars[idx].nalus) {
				fprintf(stdout, "   NAL: %s %zu\n", nalu_types[nal.m_nalUnitType], nal.getBitstream().getFifo().size());
			}
		}
	}

	std::array<bool, MAX_PAR> errs_arr{false, };
	auto t_prepare=std::chrono::steady_clock::now();
#pragma omp parallel for
	for(std::size_t idx=0; idx<num_par; idx++) {
		if(!pars[idx].nalus.empty())
			if(!probe_impl(idx))
				errs_arr[idx]=true;
	}
	auto t_done=std::chrono::steady_clock::now();
	if(errs_arr[0])
		return false;
	for(std::size_t idx=1; idx<num_par; idx++) {
		if(errs_arr[idx])
			return false;
		if(pars[idx].imgs.empty())
			continue;
		if(pars[0].width==0) {
			pars[0].width=pars[idx].width;
		} else {
			if(pars[0].width!=pars[idx].width)
				return false;
		}
		if(pars[0].height==0) {
			pars[0].height=pars[idx].height;
		} else {
			if(pars[0].height!=pars[idx].height)
				return false;
		}
		if(no_first[idx])
			pars[idx].imgs.pop_front();
		while(!pars[idx].imgs.empty())
			pars[0].imgs.splice(pars[0].imgs.end(), pars[idx].imgs, pars[idx].imgs.begin());
	}

	auto t111=std::chrono::steady_clock::now();
	if(print_dbg) {
		fprintf(stdout, "Total time: %zdus (%zdus + %zdus + %zdus)\n",
				(ssize_t)std::chrono::duration_cast<std::chrono::microseconds>(t111 - t000).count(),
				(ssize_t)std::chrono::duration_cast<std::chrono::microseconds>(t_prepare - t000).count(),
				(ssize_t)std::chrono::duration_cast<std::chrono::microseconds>(t_done-t_prepare).count(),
				(ssize_t)std::chrono::duration_cast<std::chrono::microseconds>(t111-t_done).count()
				);
		fflush(stdout);
	}
	set_info(gapr::cube_type::u16, {static_cast<int32_t>(pars[0].width), static_cast<int32_t>(pars[0].height), static_cast<int32_t>(pars[0].imgs.size())});
	return true;
}
#if 0
#endif
bool HevcLoader::probe_impl(std::size_t job_idx) {
	auto t000=std::chrono::steady_clock::now();

	Int m_iSkipFrame{0};
	TDecTop m_cTDecTop; // 10ms ctor
	Int m_iPOCLastDisplay{-MAX_INT};


	Int                 poc;
	TComList<TComPic*>* pcListPic = NULL;


	std::array<std::chrono::steady_clock::duration, 10> durs;

	auto t00=std::chrono::steady_clock::now();
	{
		std::lock_guard<std::mutex> lck{mtx};
		// create & initialize internal classes
		m_cTDecTop.create();
		// initialize decoder class
		m_cTDecTop.init();
	}
	auto t11=std::chrono::steady_clock::now();
	m_cTDecTop.setDecodedPictureHashSEIEnabled(0/*1*/);


	// main decoder loop
	Bool openedReconFile = false; // reconstruction file not yet opened. (must be performed after SPS is seen)
	Bool loopFiltered = false;

	auto& nalus=pars[job_idx].nalus;
	auto t_prepare=std::chrono::steady_clock::now();
	while (!nalus.empty())
	{

		std::list<InputNALUnit> cur_nalu;
		cur_nalu.splice(cur_nalu.end(), nalus, nalus.begin());
		auto& nalu=cur_nalu.front();

		Bool bNewPicture = false;
		{
			nalu.getBitstream().resetToStart();
			readNalUnitHeader(nalu);

			auto t_0=std::chrono::steady_clock::now();
			// XXX bad parallel performance
			bNewPicture = m_cTDecTop.decode(nalu, m_iSkipFrame, m_iPOCLastDisplay);
			auto t_1=std::chrono::steady_clock::now();
			durs[0]+=t_1-t_0;
			if (bNewPicture)
			{
				nalus.splice(nalus.begin(), cur_nalu, cur_nalu.begin());
			}
		}

		if ( (bNewPicture || !!nalus.empty() || nalu.m_nalUnitType == NAL_UNIT_EOS) &&
				!m_cTDecTop.getFirstSliceInSequence () )
		{
			if (!loopFiltered || !nalus.empty())
			{
				auto t_0=std::chrono::steady_clock::now();
				m_cTDecTop.executeLoopFilters(poc, pcListPic);
				auto t_1=std::chrono::steady_clock::now();
				durs[1]+=t_1-t_0;
			}
			loopFiltered = (nalu.m_nalUnitType == NAL_UNIT_EOS);
			if (nalu.m_nalUnitType == NAL_UNIT_EOS)
			{
				m_cTDecTop.setFirstSliceInSequence(true);
			}
		}
		else if ( (bNewPicture || !!nalus.empty() || nalu.m_nalUnitType == NAL_UNIT_EOS ) &&
				m_cTDecTop.getFirstSliceInSequence () ) 
		{
			m_cTDecTop.setFirstSliceInPicture (true);
		}

		if( pcListPic )
		{
			if (!openedReconFile ) {
				const BitDepths &bitDepths=pcListPic->front()->getPicSym()->getSPS().getBitDepths(); // use bit depths of first reconstructed picture.

				bool is16bit{false};

				if (bitDepths.recon[CHANNEL_TYPE_LUMA] > 8)
				{
					is16bit=true;
				}
				if(!is16bit) {
					std::cerr<<"err not 16bit\n";
					return false;
				}

				openedReconFile = true;
			}
			if( bNewPicture )
			{
				auto t_0=std::chrono::steady_clock::now();
				if(!xWriteOutput( job_idx, m_iPOCLastDisplay, pcListPic, nalu.m_temporalId )) {
					return false;
				}
				auto t_1=std::chrono::steady_clock::now();
				durs[2]+=t_1-t_0;
			}
			if ( (bNewPicture || nalu.m_nalUnitType == NAL_UNIT_CODED_SLICE_CRA) && m_cTDecTop.getNoOutputPriorPicsFlag() )
			{
				m_cTDecTop.checkNoOutputPriorPics( pcListPic );
				m_cTDecTop.setNoOutputPriorPicsFlag (false);
			}
			if ( bNewPicture &&
					(   nalu.m_nalUnitType == NAL_UNIT_CODED_SLICE_IDR_W_RADL
						 || nalu.m_nalUnitType == NAL_UNIT_CODED_SLICE_IDR_N_LP
						 || nalu.m_nalUnitType == NAL_UNIT_CODED_SLICE_BLA_N_LP
						 || nalu.m_nalUnitType == NAL_UNIT_CODED_SLICE_BLA_W_RADL
						 || nalu.m_nalUnitType == NAL_UNIT_CODED_SLICE_BLA_W_LP ) )
			{
				auto t_0=std::chrono::steady_clock::now();
				xFlushOutput( job_idx, m_iPOCLastDisplay, pcListPic );
				auto t_1=std::chrono::steady_clock::now();
				durs[3]+=t_1-t_0;
			}
			if (nalu.m_nalUnitType == NAL_UNIT_EOS)
			{
				auto t_0=std::chrono::steady_clock::now();
				if(!xWriteOutput( job_idx, m_iPOCLastDisplay, pcListPic, nalu.m_temporalId )) {
					return false;
				}
				auto t_1=std::chrono::steady_clock::now();
				durs[4]+=t_1-t_0;
				m_cTDecTop.setFirstSliceInPicture (false);
			}
			if(!bNewPicture && nalu.m_nalUnitType >= NAL_UNIT_CODED_SLICE_TRAIL_N && nalu.m_nalUnitType <= NAL_UNIT_RESERVED_VCL31)
			{
				auto t_0=std::chrono::steady_clock::now();
				if(!xWriteOutput(job_idx, m_iPOCLastDisplay,  pcListPic, nalu.m_temporalId )) {
					return false;
				}
				auto t_1=std::chrono::steady_clock::now();
				durs[5]+=t_1-t_0;
			}
		}
	}

	auto t_0=std::chrono::steady_clock::now();
	xFlushOutput( job_idx, m_iPOCLastDisplay, pcListPic );
	auto t_1=std::chrono::steady_clock::now();
	durs[6]+=t_1-t_0;
	auto t_done=std::chrono::steady_clock::now();
	// delete buffers
	{
		std::lock_guard<std::mutex> lck{mtx};
		m_cTDecTop.deletePicBuffer();
		// destroy internal classes
		m_cTDecTop.destroy();
		auto t_111=std::chrono::steady_clock::now();
		if(print_dbg) {
			fprintf(stdout, "[T%d] Thread time: %zuus (%zuus + %zuus + %zuus) (%zuus)\n       :",
					omp_get_thread_num(),
					(ssize_t)std::chrono::duration_cast<std::chrono::microseconds>(t_111 - t000).count(),
					(ssize_t)std::chrono::duration_cast<std::chrono::microseconds>(t_prepare - t000).count(),
					(ssize_t)std::chrono::duration_cast<std::chrono::microseconds>(t_done-t_prepare).count(),
					(ssize_t)std::chrono::duration_cast<std::chrono::microseconds>(t_111-t_done).count(),
					(ssize_t)std::chrono::duration_cast<std::chrono::microseconds>(t11-t00).count());
			std::chrono::steady_clock::duration tot_dur{};
			for(auto dur: durs) {
				fprintf(stdout, " %zdus", (ssize_t)std::chrono::duration_cast<std::chrono::microseconds>(dur).count());
				tot_dur+=dur;
			}
			fprintf(stdout, " = %zdus\n", (ssize_t)std::chrono::duration_cast<std::chrono::microseconds>(tot_dur).count());
		}
	}

	return true;
}

// ====================================================================================================================
// Protected member functions
// ====================================================================================================================




/** \param pcListPic list of pictures to be written to file
  \param tId       temporal sub-layer ID
  */
bool HevcLoader::xWriteOutput(std::size_t job_idx, Int& m_iPOCLastDisplay,  TComList<TComPic*>* pcListPic, UInt tId )
{
	if (pcListPic->empty())
	{
		return true;
	}

	TComList<TComPic*>::iterator iterPic   = pcListPic->begin();
	Int numPicsNotYetDisplayed = 0;
	Int dpbFullness = 0;
	const TComSPS* activeSPS = &(pcListPic->front()->getPicSym()->getSPS());
	Int numReorderPicsHighestTid;
	Int maxDecPicBufferingHighestTid;
	UInt maxNrSublayers = activeSPS->getMaxTLayers();

	if(-1 == -1 || -1 >= maxNrSublayers)
	{
		numReorderPicsHighestTid = activeSPS->getNumReorderPics(maxNrSublayers-1);
		maxDecPicBufferingHighestTid =  activeSPS->getMaxDecPicBuffering(maxNrSublayers-1); 
	}
	else
	{
		numReorderPicsHighestTid = activeSPS->getNumReorderPics(-1);
		maxDecPicBufferingHighestTid = activeSPS->getMaxDecPicBuffering(-1); 
	}

	while (iterPic != pcListPic->end())
	{
		TComPic* pcPic = *(iterPic);
		if(pcPic->getOutputMark() && pcPic->getPOC() > m_iPOCLastDisplay)
		{
			numPicsNotYetDisplayed++;
			dpbFullness++;
		}
		else if(pcPic->getSlice( 0 )->isReferenced())
		{
			dpbFullness++;
		}
		iterPic++;
	}

	iterPic = pcListPic->begin();

	if (numPicsNotYetDisplayed>2)
	{
		iterPic++;
	}

	TComPic* pcPic = *(iterPic);
	if (numPicsNotYetDisplayed>2 && pcPic->isField()) { //Field Decoding
		std::cerr<<"err 4\n";
		return false;
	}
	else if (!pcPic->isField()) //Frame Decoding
	{
		iterPic = pcListPic->begin();

		while (iterPic != pcListPic->end())
		{
			pcPic = *(iterPic);

			if(pcPic->getOutputMark() && pcPic->getPOC() > m_iPOCLastDisplay &&
					(numPicsNotYetDisplayed >  numReorderPicsHighestTid || dpbFullness > maxDecPicBufferingHighestTid))
			{
				numPicsNotYetDisplayed--;
				if(pcPic->getSlice(0)->isReferenced() == false)
				{
					dpbFullness--;
				}

				{
					const Window &conf    = pcPic->getConformanceWindow();
					const Window  defDisp = m_respectDefDispWindow ? pcPic->getDefDisplayWindow() : Window();

					write( job_idx, pcPic->getPicYuvRec(), 
							conf.getWindowLeftOffset() + defDisp.getWindowLeftOffset(),
							conf.getWindowRightOffset() + defDisp.getWindowRightOffset(),
							conf.getWindowTopOffset() + defDisp.getWindowTopOffset(),
							conf.getWindowBottomOffset() + defDisp.getWindowBottomOffset(),
							NUM_CHROMA_FORMAT);
				}


				// update POC of display order
				m_iPOCLastDisplay = pcPic->getPOC();

				// erase non-referenced picture in the reference picture list after display
				if ( !pcPic->getSlice(0)->isReferenced() && pcPic->getReconMark() == true )
				{
					pcPic->setReconMark(false);

					// mark it should be extended later
					pcPic->getPicYuvRec()->setBorderExtension( false );
				}
				pcPic->setOutputMark(false);
			}

			iterPic++;
		}
	}
	return true;
}

/** \param pcListPic list of pictures to be written to file
*/
bool HevcLoader::xFlushOutput( std::size_t job_idx, int& m_iPOCLastDisplay, TComList<TComPic*>* pcListPic )
{
	if(!pcListPic || pcListPic->empty())
	{
		return true;
	}
	TComList<TComPic*>::iterator iterPic   = pcListPic->begin();

	iterPic   = pcListPic->begin();
	TComPic* pcPic = *(iterPic);

	if (pcPic->isField()) { //Field Decoding
		std::cerr<<"err 2\n";
		return false;
	}
	else //Frame decoding
	{
		while (iterPic != pcListPic->end())
		{
			pcPic = *(iterPic);

			if ( pcPic->getOutputMark() )
			{
				// write to file
				{
					const Window &conf    = pcPic->getConformanceWindow();
					const Window  defDisp = m_respectDefDispWindow ? pcPic->getDefDisplayWindow() : Window();

					write( job_idx, pcPic->getPicYuvRec(),
							conf.getWindowLeftOffset() + defDisp.getWindowLeftOffset(),
							conf.getWindowRightOffset() + defDisp.getWindowRightOffset(),
							conf.getWindowTopOffset() + defDisp.getWindowTopOffset(),
							conf.getWindowBottomOffset() + defDisp.getWindowBottomOffset(),
							NUM_CHROMA_FORMAT);
				}


				// update POC of display order
				m_iPOCLastDisplay = pcPic->getPOC();

				// erase non-referenced picture in the reference picture list after display
				if ( !pcPic->getSlice(0)->isReferenced() && pcPic->getReconMark() == true )
				{
					pcPic->setReconMark(false);

					// mark it should be extended later
					pcPic->getPicYuvRec()->setBorderExtension( false );
				}
				pcPic->setOutputMark(false);
			}
			if(pcPic != NULL)
			{
				pcPic->destroy();
				delete pcPic;
				pcPic = NULL;
			}
			iterPic++;
		}
	}
	pcListPic->clear();
	m_iPOCLastDisplay = -MAX_INT;
	return true;
}




#if 0
bool HevcLoader::getType(CubeType* type) {
	if(!supported)
		return false;
	*type=CubeType::U16;
	return true;
}
bool HevcLoader::getSizes(int64_t* w, int64_t* h, int64_t* d) {
	if(!supported)
		return false;
	*w=pars[0].width;
	*h=pars[0].height;
	*d=pars[0].imgs.size();
	return true;
}
#endif

void HevcLoader::do_load(char* ptr, int64_t ystride, int64_t zstride) {
	if(!supported)
		return;

	if(print_dbg)
		fprintf(stdout, "[T%d] DIM: %zu %zu %zu\n", omp_get_thread_num(), pars[0].width, pars[0].height, pars[0].imgs.size());
	auto it=pars[0].imgs.begin();
	auto ys=it->size()/pars[0].height;
	std::size_t z=0;
	auto bpv=voxel_size(gapr::cube_type::u16);
	for(; it!=pars[0].imgs.end(); ++it) {
		for(size_t y=0; y<pars[0].height; y++) {
			auto qq=&(*it)[y*ys];
			auto pp=ptr+y*ystride+z*zstride;
#if 0
			if(!isLittleEndian()) {
				if(bpv==2) {
					swapBytes<uint16_t>(ptr, x0, y0, z0, ystride, zstride);
				} else if(bpv==4) {
					swapBytes<uint32_t>(ptr, x0, y0, z0, ystride, zstride);
				} else if(bpv==8) {
					swapBytes<uint64_t>(ptr, x0, y0, z0, ystride, zstride);
				}
			}
#endif
			std::copy(qq, qq+pars[0].width*bpv, pp);
		}
		z++;
	}
	/////

}


namespace gapr {
	std::unique_ptr<cube_loader> make_cube_loader_hevc(Streambuf& file) {
		return std::make_unique<HevcLoader>(file);
	}
}

