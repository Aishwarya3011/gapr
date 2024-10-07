/* core/buffer.cc
 *
 * Copyright (C) 2019 GOU Lingfeng
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

//*****

#include "gapr/buffer.hh"
#include "gapr/mem-file.hh"
#include "gapr/utility.hh"

#include <new>
#include <cstdlib>


using gapr::buffer_PRIV;
using gapr::mem_file_PRIV;

buffer_PRIV::Head* buffer_PRIV::alloc(uint32_t len) {
	auto buf=new char[sizeof(buffer_PRIV::Head)+len];
	auto ptr=buf+sizeof(buffer_PRIV::Head);
	return new(buf) buffer_PRIV::Head{len, ptr};
}
void buffer_PRIV::destroy(Head* p) noexcept {
	p->~Head();
	delete[] reinterpret_cast<char*>(p);
}

mem_file_PRIV::Head* mem_file_PRIV::alloc(bool flat) {
	static_assert(BLK_SIZ1>=sizeof(char*)+sizeof(Head));
	auto buf=new char[BLK_SIZ1];
	if(flat)
		return new(buf+BLK_SIZ1-sizeof(Head)) Head{BLK_SIZ1-sizeof(Head), buf};
	return new(buf) Head{buf+sizeof(Head), (BLK_SIZ1-sizeof(Head))/sizeof(char*)};
}
void mem_file_PRIV::destroy(Head* p) noexcept {
	if(p->is_flat()) {
		auto buf=p->_ptr;
		p->~Head();
		delete[] static_cast<char*>(buf);
	} else {
		for(std::size_t i=0; i<p->count; i++)
			delete[] static_cast<char**>(p->_ptr)[i];
		p->~Head();
		delete[] reinterpret_cast<char*>(p);
	}
}


gapr::buffer_view mem_file_PRIV::Head::map(std::size_t off) const noexcept {
	//fprintf(stderr, "map: %zu/%zu\n", off, len);
	assert(off<=len);
	if(off>=len)
		return {nullptr, 0};

	if(is_flat())
		return {static_cast<char*>(_ptr)+off, len-off};

	auto left=len-off;
	if(off<SIZE_OPT) {
		auto i=off/BLK_SIZ1;
		auto r=off%BLK_SIZ1;
		if(left>BLK_SIZ1-r)
			left=BLK_SIZ1-r;
		return {static_cast<char**>(_ptr)[i]+r, left};
	}
	auto delta=(off-SIZE_OPT);
	auto i=delta/BLK_SIZ2;
	auto r=delta%BLK_SIZ2;
	if(left>BLK_SIZ2-r)
		left=BLK_SIZ2-r;
	return {static_cast<char**>(_ptr)[COUNT_OPT+i]+r, left};
}

gapr::buffer_view gapr::mutable_mem_file::map_tail() {
	auto ntail=_p->ntail;
	if(_p->is_flat()) {
		if(ntail>0) {
			auto len=_p->len;
			return {static_cast<char*>(_p->_ptr)+len, mem_file_PRIV::BLK_SIZ1-sizeof(mem_file_PRIV::Head)-len};
		}

		auto buf=new char[mem_file_PRIV::BLK_SIZ1];
		auto p=new(buf) mem_file_PRIV::Head{buf+sizeof(mem_file_PRIV::Head), (mem_file_PRIV::BLK_SIZ1-sizeof(mem_file_PRIV::Head))/sizeof(char*)};
		p->count=1;
		auto len=p->len=_p->len;
		p->ntail=sizeof(mem_file_PRIV::Head);
		buf=static_cast<char*>(_p->_ptr);
		static_cast<char**>(p->_ptr)[0]=buf;
		_p->~Head();
		_p=p;
		return {buf+len, mem_file_PRIV::BLK_SIZ1-len};
	}
	if(ntail>0) {
		auto i=_p->count-1;
		auto sz=i<mem_file_PRIV::COUNT_OPT?mem_file_PRIV::BLK_SIZ1:mem_file_PRIV::BLK_SIZ2;
		return {static_cast<char**>(_p->_ptr)[i]+sz-ntail, ntail};
	}
	auto max_count=_p->max_count;
	auto cnt=_p->count;
	if(cnt>=max_count) {
		auto sz=(max_count*sizeof(char*)+sizeof(mem_file_PRIV::Head))*2;
		auto buf=new char[sz];
		auto p=new(buf) mem_file_PRIV::Head{buf+sizeof(mem_file_PRIV::Head), (sz-sizeof(mem_file_PRIV::Head))/sizeof(char*)};
		p->count=cnt;
		p->len=_p->len;
		for(std::size_t i=0; i<cnt; i++)
			static_cast<char**>(p->_ptr)[i]=static_cast<char**>(_p->_ptr)[i];
		_p->~Head();
		delete[] reinterpret_cast<char*>(_p);
		_p=p;
	}
	auto sz=cnt<mem_file_PRIV::COUNT_OPT?mem_file_PRIV::BLK_SIZ1:mem_file_PRIV::BLK_SIZ2;
	auto buf=new char[sz];
	static_cast<char**>(_p->_ptr)[cnt]=buf;
	_p->count++;
	_p->ntail=sz;
	return {buf, sz};
}
