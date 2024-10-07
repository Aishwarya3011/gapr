#include "dshelper.hh"

#include <cstddef>
#include <cstring>
#include <limits>
#include <memory> 
#include <array>
#include <mutex>
#include <vector>
#include <cassert>
#include <fstream>

#include "gapr/detail/nrrd-output.hh"
#include "gapr/utility.hh"

#include "savewebm.hh"

#include "../corelib/windows-compat.hh"
#include "../corelib/libc-wrappers.hh"

#include "logger.h"

template<typename T>
struct ds_slice {
	std::array<std::unique_ptr<T[]>, 4> mip;
	std::array<std::unique_ptr<uint64_t[]>, 4> sum;
	std::unique_ptr<unsigned int[]> cnt;
	bool _initialized{false};
	std::mutex mtx;

	std::size_t fileoff{0};
	bool dirty{false};
};

template<typename T>
struct dshelper_impl: dshelper {
	explicit dshelper_impl(): dshelper{} { }
	~dshelper_impl() override { }

	void update(unsigned int z, unsigned int x, unsigned int y, unsigned int tileW, unsigned int tileH, const void* data_) override {
		assert(_initialized);
		unsigned int zz=z/dsz;
		std::unique_lock lck{_mtx};
		if(visited(z, x, y, tileW, tileH))
			return;
		_version.fetch_add(1);
		auto slice=gen_slice(zz);
		lck.unlock();

		unsigned int yblk_start=y/dsy;
		unsigned int yblk_end=(std::min(y+tileH, height)-1)/dsy+1;
		unsigned int xblk_start=x/dsx;
		unsigned int xblk_end=(std::min(x+tileW, width)-1)/dsx+1;
		std::vector<T> tmpmip;
		std::vector<uint64_t> tmpsum;
		std::vector<unsigned int> tmpcnt;
		for(unsigned int sampi=0; sampi<4 && sampi<spp; ++sampi) {
			auto data=&static_cast<const T*>(data_)[tileH*tileW*sampi];
			for(unsigned int yblk=yblk_start; yblk<yblk_end; ++yblk) {
				auto yy_start=std::max(yblk*dsy, y)-y;
				auto yy_end=std::min(std::min((yblk+1)*dsy, y+tileH), height)-y;
				for(unsigned int xblk=xblk_start; xblk<xblk_end; ++xblk) {
					auto xx_start=std::max(xblk*dsx, x)-x;
					auto xx_end=std::min(std::min((xblk+1)*dsx, x+tileW), width)-x;
					unsigned int xx_len=xx_end-xx_start;
					assert(xx_len<65535);
					static_assert(sizeof(T)<=16/8);
					uint64_t ss{0};
					T mm{0};
					for(unsigned int yy=yy_start; yy<yy_end; ++yy) {
						uint32_t sss{0};
						for(unsigned int xx=xx_start; xx<xx_end; ++xx) {
							auto v=data[xx+yy*tileW];
							sss+=v;
							if(mm<v)
								mm=v;
						}
						ss+=sss;
					}
					tmpmip.push_back(mm);
					tmpsum.push_back(ss);
					if(sampi==0)
						tmpcnt.push_back((yy_end-yy_start)*xx_len);
				}
			}
		}

		std::unique_lock lck2{slice->mtx};
		if(!slice->_initialized)
			init_slice(slice);
		auto ptrmip=&tmpmip[0];
		auto ptrsum=&tmpsum[0];
		for(unsigned int sampi=0; sampi<4 && sampi<spp; ++sampi) {
			auto ptrcnt=&tmpcnt[0];
			auto mipdata=slice->mip[sampi].get();
			auto sumdata=slice->sum[sampi].get();
			auto cntdata=slice->cnt.get();
			for(unsigned int yblk=yblk_start; yblk<yblk_end; ++yblk) {
				for(unsigned int xblk=xblk_start; xblk<xblk_end; ++xblk) {
					auto ss=*(ptrsum++);
					auto mm=*(ptrmip++);
					auto cc=*(ptrcnt++);
					if(cc==0)
						continue;
					auto& mmm=mipdata[xblk+yblk*dsw+dsh*dsw*sampi];
					mmm=std::max(mmm, mm);
					sumdata[xblk+yblk*dsw+dsh*dsw*sampi]+=ss;
					if(sampi==0)
						cntdata[xblk+yblk*dsw+dsh*dsw*sampi]+=cc;
				}
			}
		}
		slice->dirty=true;
		lck2.unlock();

		lck.lock();
		set_visited(z, x, y, tileW, tileH);
	}

	mutable std::mutex _mtx;
	std::vector<std::unique_ptr<ds_slice<T>>> ds_slices;
	std::vector<uint8_t> visited_data;
	struct visited_info {
		std::size_t idx;
		unsigned int len{0};
		unsigned int nx{0};
		bool dirty;
	};
	std::vector<visited_info> visited_infos;
	bool visited(unsigned int z, unsigned int x, unsigned int y, unsigned int tileW, unsigned tileH) {
		assert(x%tileW==0);
		assert(y%tileH==0);
		while(visited_infos.size()<=z)
			visited_infos.emplace_back();
		auto& info=visited_infos[z];
		if(info.nx==0) {
			unsigned int nx=(width+tileW-1)/tileW;
			unsigned int ny=(height+tileH-1)/tileH;
			unsigned int nbytes=(nx*ny+7)/8;
			auto idx=visited_data.size();
			visited_data.resize(idx+nbytes, 0);
			info.idx=idx;
			info.len=nbytes;
			info.nx=nx;
			info.dirty=true;
			for(unsigned int off=nx*ny; off<nbytes*8; ++off)
				set_visited(off, info);
			return false;
		}
		unsigned int off=x/tileW+(y/tileH)*info.nx;
		assert(off/8<info.len);
		auto v=visited_data[info.idx+off/8]>>(off%8);
		return v&1;
	}
	void set_visited(unsigned int off, const visited_info& info) {
		assert(off/8<info.len);
		uint8_t v=1<<(off%8);
		visited_data[info.idx+off/8]|=v;
	}
	void set_visited(unsigned int z, unsigned int x, unsigned int y, unsigned int tilew, unsigned tileh) {
		auto& info=visited_infos[z];
		unsigned int off=x/tilew+(y/tileh)*info.nx;
		return set_visited(off, info);
	}

		/////////
		//////////
		
	//raw {
	//
	//
	//
	//o //file
	//}
	//
	//
	//
	//!x
	//!y
	//*z
	//
	//
	
	//
	double max_value() const override {
		T mm{0};
		unsigned int dsd;

		std::unique_lock lck{_mtx};
		dsd=ds_slices.size();
		for(unsigned int z=0; z<dsd; ++z) {
			auto slice=ds_slices[z].get();
			std::unique_lock lck2{slice->mtx};
			lck.unlock();
			for(unsigned int k=0; k<spp && k<4; ++k) {
				auto ptr=slice->mip[k].get();
				for(unsigned int i=0; i<dsw*dsh; ++i) {
					auto v=ptr[i];
					if(mm<v)
						mm=v;
				}
			}
			lck2.unlock();
			lck.lock();
		}
		lck.unlock();
		return mm*1.0/std::numeric_limits<T>::max();
	}

	std::pair<uint64_t, gapr::mem_file> downsample(std::promise<std::vector<uint16_t>> avg, cube_enc_opts enc_opts, uint64_t mipver, uint64_t avgver, unsigned int chan) const override {
		std::vector<T> data;
		unsigned int dsd;
		std::vector<uint16_t> dataavg;

		std::unique_lock lck{_mtx};
		auto curver=_version.load();
		dsd=ds_slices.size();
		if(curver>mipver)
			data.reserve(dsw*dsh*dsd);
		if(curver>avgver)
			dataavg.reserve(dsw*dsh*dsd);
		constexpr auto ui16max=std::numeric_limits<uint16_t>::max();
		for(unsigned int z=0; z<dsd; ++z) {
			auto slice=ds_slices[z].get();
			std::unique_lock lck2{slice->mtx};
			lck.unlock();

			if(curver>mipver) {
				auto ptr=slice->mip[chan].get();
				for(unsigned int i=0; i<dsw*dsh; ++i)
					data.push_back(ptr[i]);
			}
			if(curver>avgver) {
				auto ptr=slice->sum[chan].get();
				auto cnt=slice->cnt.get();
				for(unsigned int i=0; i<dsw*dsh; ++i) {
					if(cnt[i]==0)
						dataavg.push_back(ui16max);
					else
						dataavg.push_back(std::round(std::sqrt(ptr[i]/double{std::numeric_limits<T>::max()}/cnt[i])*(ui16max-256)));
				}
			}
			lck2.unlock();

			lck.lock();
		}
		lck.unlock();
		avg.set_value(std::move(dataavg));

		if(curver<=mipver)
			return {curver, gapr::mem_file{}};

		enc_opts.threads=2;
		gapr::mem_file ret{convert_webm(data.data(), dsw, dsh, dsd, enc_opts)};
		if(true) {
			std::ofstream fs{"/tmp/testconv.webm"};
			std::size_t o{0};
			while(true) {
				auto buf=ret.map(o);
				if(buf.size()==0)
					break;
				fs.write(buf.data(), buf.size());
				o+=buf.size();
			}
		}
		if(false) {
			std::ofstream fs{"/tmp/testconv.nrrd"};
			gapr::nrrd_output nrrd{fs, true};
			nrrd.header();
			nrrd.finish(data.data(), dsw, dsh, dsd);
		}
		return {curver, std::move(ret)};
	}
	/*
	void writecache(const std::filesystem::path& path, bool first) const override {
		fprintf(stderr, "Saving downsample...");
		unsigned int dsd;
		gapr::file_stream ofs;
		if(first)
			ofs={path, "wb"};
		else
			ofs={path, "r+b"};
		if(!ofs)
			throw std::runtime_error{"failed to open file"};
		auto write_buf=[ofs=ofs.lower()](const void* p, std::size_t n) {
			auto nn=std::fwrite(p, 1, n, ofs);
			if(nn!=n)
				throw std::runtime_error{"failed to write downsample"};
		};

		std::size_t toskip=0;
		std::unique_lock lck{_mtx};
		auto ver=_version.load();
		write_buf(&ver, sizeof(ver));
		dsd=ds_slices.size();
		for(unsigned int z=0; z<dsd; ++z) {
			auto slice=ds_slices[z].get();
			std::unique_lock lck2{slice->mtx};
			lck.unlock();

			auto curoff=std::ftell(ofs);
			if(curoff==-1)
				throw std::runtime_error{"failed to ftell"};
			if(slice->fileoff==0) {
				slice->fileoff=curoff+toskip;
			} else {
				assert(slice->fileoff==curoff+toskip);
			}
			bool dirty=slice->dirty;
			if(dirty || first) {
				if(toskip>0) {
					if(-1==std::fseek(ofs, toskip, SEEK_CUR))
						throw std::runtime_error{"failed to fseek"};
					toskip=0;
				}
				write_buf(slice->cnt.get(), sizeof(unsigned int)*dsw*dsh);
				for(unsigned int i=0; i<spp && i<4; ++i) {
					write_buf(slice->mip[i].get(), sizeof(T)*dsw*dsh);
					write_buf(slice->sum[i].get(), sizeof(uint64_t)*dsw*dsh);
				}
				slice->dirty=false;
			} else {
				std::size_t per_pix=sizeof(unsigned int)+std::min(spp, (unsigned short)4)*(sizeof(T)+sizeof(uint64_t));
				toskip+=dsw*dsh*per_pix;
			}
			lck2.unlock();
			lck.lock();
			unsigned int zz_start=z*dsz;
			unsigned int zz_end=std::min(std::size_t{(z+1)*dsz}, visited_infos.size());
			if(dirty || first || (z<fix_pads.size() && fix_pads[z])) {
				if(toskip>0) {
					if(-1==std::fseek(ofs, toskip, SEEK_CUR))
						throw std::runtime_error{"failed to fseek"};
					toskip=0;
				}
				for(auto zz=zz_start; zz<zz_end; ++zz) {
					auto& info=visited_infos[zz];
					write_buf(&info.len, sizeof(unsigned int));
					write_buf(&info.nx, sizeof(unsigned int));
					write_buf(&visited_data[info.idx], info.len);
				}
				if(std::fflush(ofs)!=0)
					throw std::runtime_error{"failed to fflush"};
			} else {
				for(auto zz=zz_start; zz<zz_end; ++zz) {
					auto& info=visited_infos[zz];
					toskip+=sizeof(unsigned int)*2+info.len;
				}
			}
		}
		lck.unlock();
		auto fd=::fileno(ofs);
		if(-1==::fsync(fd))
			throw std::runtime_error{"failed to sync data"};
		hint_discard_cache(ofs);
		if(!ofs.close())
			throw std::runtime_error{"failed to close downsample"};
		// if(first) {
		// 	gapr::file_stream dir{path.parent_path(), "r"};
		// 	if(-1==::fdatasync(::fileno(dir)))
		// 		throw std::runtime_error{"failed to sync dir"};
		// }
		if(first) {
			gapr::file_stream dir{path.parent_path(), "r"};
			if (!dir) {
				throw std::runtime_error{"failed to open directory for syncing"};
			}
			if(-1 == ::fsync(::fileno(dir))) {
				throw std::runtime_error{"failed to sync dir"};
			}
		}
		fprintf(stderr, "   DONE\n");
	}
	*/

	void writecache(const std::filesystem::path& path, bool first) const override {
		Logger::instance().logMessage(__FILE__, "Saving downsample...");

		unsigned int dsd;
		gapr::file_stream ofs;
		if(first)
			ofs={path, "wb"};
		else
			ofs={path, "r+b"};

		if(!ofs) {
			Logger::instance().logMessage(__FILE__, "Failed to open file.");
			throw std::runtime_error{"failed to open file"};
		}

		Logger::instance().logMessage(__FILE__, "Opened file successfully.");

		auto write_buf=[ofs=ofs.lower()](const void* p, std::size_t n) {
			auto nn=std::fwrite(p, 1, n, ofs);
			if(nn!=n) {
				Logger::instance().logMessage(__FILE__, "Failed to write downsample.");
				throw std::runtime_error{"failed to write downsample"};
			}
		};

		std::size_t toskip=0;
		std::unique_lock lck{_mtx};
		auto ver=_version.load();
		write_buf(&ver, sizeof(ver));
		dsd=ds_slices.size();

		Logger::instance().logMessage(__FILE__, "Version and slices size written.");

		for(unsigned int z=0; z<dsd; ++z) {
			auto slice=ds_slices[z].get();
			std::unique_lock lck2{slice->mtx};
			lck.unlock();

			auto curoff=std::ftell(ofs);
			if(curoff==-1) {
				Logger::instance().logMessage(__FILE__, "Failed to get file position (ftell).");
				throw std::runtime_error{"failed to ftell"};
			}
			if(slice->fileoff==0) {
				slice->fileoff=curoff+toskip;
			} else {
				assert(slice->fileoff==curoff+toskip);
			}

			bool dirty=slice->dirty;
			if(dirty || first) {
				if(toskip>0) {
					if(-1==std::fseek(ofs, toskip, SEEK_CUR)) {
						Logger::instance().logMessage(__FILE__, "Failed to seek in file (fseek).");
						throw std::runtime_error{"failed to fseek"};
					}
					toskip=0;
				}
				write_buf(slice->cnt.get(), sizeof(unsigned int)*dsw*dsh);
				for(unsigned int i=0; i<spp && i<4; ++i) {
					write_buf(slice->mip[i].get(), sizeof(T)*dsw*dsh);
					write_buf(slice->sum[i].get(), sizeof(uint64_t)*dsw*dsh);
				}
				slice->dirty=false;
			} else {
				std::size_t per_pix=sizeof(unsigned int)+std::min(spp, (unsigned short)4)*(sizeof(T)+sizeof(uint64_t));
				toskip+=dsw*dsh*per_pix;
			}
			lck2.unlock();
			lck.lock();
		}

		Logger::instance().logMessage(__FILE__, "Finished writing cache.");

		lck.unlock();
		auto fd=::fileno(ofs);
		if(-1==::fsync(fd)) {
			Logger::instance().logMessage(__FILE__, "Failed to sync data to disk (fsync).");
			throw std::runtime_error{"failed to sync data"};
		}

		Logger::instance().logMessage(__FILE__, "Data synced to disk successfully.");

		hint_discard_cache(ofs);
		if(!ofs.close()) {
			Logger::instance().logMessage(__FILE__, "Failed to close file.");
			throw std::runtime_error{"failed to close downsample"};
		}

		if (first) {
			gapr::file_stream dir{path.parent_path(), "r"};
			if (!dir) {
				Logger::instance().logMessage(__FILE__, "Failed to open directory for syncing.");
			} else {
				if (-1 == ::fsync(::fileno(dir))) {
					Logger::instance().logMessage(__FILE__, "Failed to sync directory (fsync). This may be due to the file system or environment limitations.");
				} else {
					Logger::instance().logMessage(__FILE__, "Directory synced successfully.");
				}
			}
		}

		Logger::instance().logMessage(__FILE__, "   DONE");
	}

	
	ds_slice<T>* gen_slice(unsigned int zz) {
		while(ds_slices.size()<=zz)
			ds_slices.emplace_back();
		auto slice=ds_slices[zz].get();
		if(!slice) {
			ds_slices[zz]=std::make_unique<ds_slice<T>>();
			slice=ds_slices[zz].get();
		}
		return slice;
	}
	void init_slice(ds_slice<T>* slice) {
		for(unsigned int i=0; i<4 && i<spp; ++i) {
			auto mm=std::make_unique<T[]>(dsw*dsh);
			std::memset(&mm[0], 0, dsw*dsh*sizeof(T));
			slice->mip[i]=std::move(mm);
			auto ss=std::make_unique<uint64_t[]>(dsw*dsh);
			std::memset(&ss[0], 0, dsw*dsh*sizeof(uint64_t));
			slice->sum[i]=std::move(ss);
		}
		auto cc=std::make_unique<unsigned int[]>(dsw*dsh);
		std::memset(&cc[0], 0, dsw*dsh*sizeof(unsigned int));
		slice->cnt=std::move(cc);
		slice->dirty=true;
		slice->_initialized=true;
	}
	/*
	void loadcache(const std::filesystem::path& path, unsigned int depth) override {
		assert(_initialized);
		gapr::file_stream ofs{path, "rb"};
		if(!ofs)
			throw std::runtime_error{"failed to open file"};
		// auto read_buf=[ofs=ofs.lower()](void* p, std::size_t n) {
		// 	auto nn=std::fread(p, 1, n, ofs);
		// 	if(nn!=n)
		// 		throw std::runtime_error{"failed to read downsample"};
		// };
		Logger::instance().logMessage(__FILE__, "Starting to read downsample data.");

		auto read_buf=[ofs=ofs.lower()](void* p, std::size_t n) {
			auto nn = std::fread(p, 1, n, ofs);
			if(nn != n) {
				Logger::instance().logMessage(__FILE__, "Failed to read downsample. Expected size: " + std::to_string(n) + ", but got: " + std::to_string(nn));
				throw std::runtime_error{"failed to read downsample"};
			}
		};

		Logger::instance().logMessage(__FILE__, "Finished reading downsample data.");
		unsigned int dsd=(depth+dsz-1)/dsz;

		hint_sequential_read(ofs);
		std::unique_lock lck{_mtx};
		uint64_t ver;
		read_buf(&ver, sizeof(ver));
		_version.store(ver);
		for(unsigned int z=0; z<dsd; ++z) {
			auto slice=gen_slice(z);
			std::unique_lock lck2{slice->mtx};
			lck.unlock();

			if(!slice->_initialized)
				init_slice(slice);
			auto curoff=std::ftell(ofs);
			if(curoff==-1)
				throw std::runtime_error{"failed to ftell"};
			if(slice->fileoff==0) {
				slice->fileoff=curoff;
			} else {
				assert(slice->fileoff==static_cast<std::size_t>(curoff));
			}
			{
				read_buf(slice->cnt.get(), sizeof(unsigned int)*dsw*dsh);
				for(unsigned int i=0; i<spp && i<4; ++i) {
					read_buf(slice->mip[i].get(), sizeof(T)*dsw*dsh);
					read_buf(slice->sum[i].get(), sizeof(uint64_t)*dsw*dsh);
				}
				slice->dirty=false;
			}
			lck2.unlock();

			lck.lock();
			unsigned int zz_start=z*dsz;
			unsigned int zz_end=std::min((z+1)*dsz, depth);
			for(auto zz=zz_start; zz<zz_end; ++zz) {
				while(visited_infos.size()<=zz)
					visited_infos.emplace_back();
				auto& info=visited_infos[zz];
				assert(info.nx==0);
				read_buf(&info.len, sizeof(unsigned int));
				read_buf(&info.nx, sizeof(unsigned int));
				auto idx=visited_data.size();
				visited_data.resize(idx+info.len, 0);
				read_buf(&visited_data[idx], info.len);
				info.idx=idx;
				info.dirty=false;
			}
		}
		lck.unlock();
		hint_discard_cache(ofs);
	}
	*/

	void loadcache(const std::filesystem::path& path, unsigned int depth) override {
		Logger::instance().logMessage(__FILE__, "Attempting to load cache.");
		
		gapr::file_stream ofs{path, "rb"};
		if(!ofs) {
			Logger::instance().logMessage(__FILE__, "Failed to open file for reading: " + path.string());
			throw std::runtime_error{"failed to open file"};
		}

		Logger::instance().logMessage(__FILE__, "File opened successfully.");

		auto read_buf=[ofs=ofs.lower()](void* p, std::size_t n) {
			auto nn = std::fread(p, 1, n, ofs);
			if(nn != n) {
				Logger::instance().logMessage(__FILE__, "Failed to read downsample. Expected: " + std::to_string(n) + ", but got: " + std::to_string(nn));
				if (std::feof(ofs)) {
					Logger::instance().logMessage(__FILE__, "Reached end of file unexpectedly.");
				} else if (std::ferror(ofs)) {
					Logger::instance().logMessage(__FILE__, "File read error occurred.");
				}
				throw std::runtime_error{"failed to read downsample"};
			}
		};

		unsigned int dsd = (depth + dsz - 1) / dsz;

		// More detailed logging to track the loading process
		Logger::instance().logMessage(__FILE__, "Loading version and initializing slices.");
		
		std::unique_lock lck{_mtx};
		uint64_t ver;
		
		Logger::instance().logMessage(__FILE__, "File position before reading version: " + std::to_string(std::ftell(ofs)));
		read_buf(&ver, sizeof(ver));  // Version read
		Logger::instance().logMessage(__FILE__, "Version read successfully. File position: " + std::to_string(std::ftell(ofs)));

		_version.store(ver);

		for(unsigned int z = 0; z < dsd; ++z) {
			Logger::instance().logMessage(__FILE__, "Processing slice " + std::to_string(z));
			
			auto slice = gen_slice(z);
			std::unique_lock lck2{slice->mtx};
			lck.unlock();

			if(!slice->_initialized)
				init_slice(slice);
			
			Logger::instance().logMessage(__FILE__, "File position before reading slice count: " + std::to_string(std::ftell(ofs)));
			read_buf(slice->cnt.get(), sizeof(unsigned int) * dsw * dsh);  // Read count data
			Logger::instance().logMessage(__FILE__, "Slice count data read successfully.");

			for(unsigned int i = 0; i < spp && i < 4; ++i) {
				Logger::instance().logMessage(__FILE__, "File position before reading mip data: " + std::to_string(std::ftell(ofs)));
				read_buf(slice->mip[i].get(), sizeof(T) * dsw * dsh);      // Read mip data
				Logger::instance().logMessage(__FILE__, "Mip data read successfully.");

				Logger::instance().logMessage(__FILE__, "File position before reading sum data: " + std::to_string(std::ftell(ofs)));
				read_buf(slice->sum[i].get(), sizeof(uint64_t) * dsw * dsh); // Read sum data
				Logger::instance().logMessage(__FILE__, "Sum data read successfully.");
			}
			
			slice->dirty = false;
			lck2.unlock();
			lck.lock();
		}

		Logger::instance().logMessage(__FILE__, "Cache loaded successfully.");
	}

	bool finished(unsigned int z) const override {
		assert(_initialized);
		std::lock_guard lck{_mtx};
		if(visited_infos.size()<=z)
			return false;
		auto& info=visited_infos[z];
		if(info.nx==0)
			return false;
		for(unsigned int i=0; i<info.len; ++i) {
			auto v=visited_data[info.idx+i];
			if(v!=0xff)
				return false;
		}
		return true;
	}
	std::vector<bool> fix_pads;
	std::vector<std::array<unsigned int, 2>> missing(unsigned int z, unsigned int tilew, unsigned int tileh) override {
		assert(_initialized);
		std::vector<std::array<unsigned int, 2>> res;
		unsigned int nx=(width+tilew-1)/tilew;
		unsigned int ny=(height+tileh-1)/tileh;
		unsigned int nbytes=(nx*ny+7)/8;
		uint8_t pad=0;
		for(unsigned int off=nx*ny; off<nbytes*8; ++off)
			pad|=1<<(off%8);
		std::unique_lock lck{_mtx};
		if(visited_infos.size()<=z || visited_infos[z].nx==0) {
			lck.unlock();
			for(unsigned int y=0; y<height; y+=tileh)
				for(unsigned int x=0; x<width; x+=tilew)
					res.emplace_back(std::array{x, y});
			return res;
		}
		auto& info=visited_infos[z];
		assert(info.nx==nx);
		std::vector<std::pair<unsigned int, uint8_t>> missing;
		for(unsigned int i=0; i<info.len; ++i) {
			auto v=visited_data[info.idx+i];
			if(v!=0xff)
				missing.emplace_back(i, v);
		}
		if(missing.empty())
			return res;
		{
			auto [i, v]=missing.back();
			if(i+1==info.len && ((~v)&pad)) {
				fprintf(stderr, "fixpad 0x%02hhx 0x%02hhx\n", v, pad);
				visited_data[info.idx+i]|=pad;
				auto zz=z/dsz;
				if(zz+1>fix_pads.size())
					fix_pads.resize(std::max(ds_slices.size(), std::size_t{zz+1}), false);
				fix_pads[zz]=true;
				v|=pad;
				if(v==0xff)
					missing.pop_back();
				else
					missing.back().second=v;
			}
		}
		lck.unlock();
		for(auto [i, v]: missing) {
			for(unsigned int j=0; j<8; ++j) {
				if((v>>j)&1)
					continue;
				unsigned int off=i*8+j;
				assert(off<nx*ny);
				res.emplace_back(std::array{(off%nx)*tilew, (off/nx)*tileh});
			}
		}
		return res;
	}
};

std::shared_ptr<dshelper> dshelper::create_u8() {
	return std::make_shared<dshelper_impl<uint8_t>>();
}
std::shared_ptr<dshelper> dshelper::create_u16() {
	return std::make_shared<dshelper_impl<uint16_t>>();
}

