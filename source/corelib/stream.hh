/* gapr/stream.hh
 *
 * Copyright (C) 2020 GOU Lingfeng
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

//@@@@
#ifndef _GAPR_INCLUDE_STREAM_HH_
#define _GAPR_INCLUDE_STREAM_HH_

//#include <sstream>

namespace gapr {

	constexpr static std::size_t STRM_MIN_BUF_SIZ=128;

	class stream_input {
		public:
			stream_input(const stream_input&) =delete;
			stream_input& operator=(const stream_input&) =delete;

			std::pair<const char*, std::size_t> buffer() {
				if(_buf)
					return {_buf+_idx, _siz-_idx};
				if(!_siz)
					return {nullptr, 0};

				auto idx=_idx;
				_idx=0;
				std::tie(_buf, _siz)=buffer_impl(idx);
				return {_buf, _siz};
			}
			void consume(std::size_t n) {
				_idx+=n;
				if(_idx+STRM_MIN_BUF_SIZ>_siz)
					_buf=nullptr;
			}

			std::size_t offset() const noexcept;

		protected:
			stream_input(): _buf{nullptr}, _idx{0}, _siz{1} { }
			virtual ~stream_input() { }

		private:
			const char* _buf;
			std::size_t _idx;
			std::size_t _siz;
			virtual std::pair<const char*, std::size_t> buffer_impl(std::size_t n) =0;
			virtual std::size_t offset_impl() const noexcept =0;
	};

	template<typename Impl>
		class stream_input_impl: public stream_input {
			public:
				explicit stream_input_impl(Impl& impl): _impl{impl} { }
				~stream_input_impl() override { }

			private:
				std::pair<const char*, std::size_t> buffer_impl(std::size_t n) override {
					if(n)
						_impl.consume(n);
					return _impl.buffer();
				}
				std::size_t offset_impl() const noexcept override {
					return 0;
				}
				Impl& _impl;
		};

	class stream_output {
		public:
			stream_output(const stream_output&) =delete;
			stream_output& operator=(const stream_output&) =delete;

			std::pair<char*, std::size_t> buffer() {
				if(_buf)
					return {_buf+_idx, _siz-_idx};
				if(!_siz)
					return {nullptr, 0};

				auto idx=_idx;
				_idx=0;
				std::tie(_buf, _siz)=buffer_impl(idx);
				return {_buf, _siz};
			}
			void commit(std::size_t n) {
				_idx+=n;
				if(_idx+STRM_MIN_BUF_SIZ>_siz)
					_buf=nullptr;
			}
			void flush() {
				auto idx=_idx;
				_idx=0;
				flush_impl(idx);
			}

			std::size_t offset() const noexcept;

		protected:
			stream_output(): _buf{nullptr}, _idx{0}, _siz{1} { }
			virtual ~stream_output() { }

		private:
			char* _buf;
			std::size_t _idx;
			std::size_t _siz;
			virtual void flush_impl(std::size_t n) =0;
			virtual std::pair<char*, std::size_t> buffer_impl(std::size_t n) =0;
			virtual std::size_t offset_impl() const noexcept =0;
	};

	template<typename Impl>
		class stream_output_impl: public stream_output {
			public:
				explicit stream_output_impl(Impl& impl): _impl{impl} { }
				~stream_output_impl() override { }

			private:
				void flush_impl(std::size_t n) override {
					if(n)
						_impl.commit(n);
					_impl.flush();
				}
				std::pair<char*, std::size_t> buffer_impl(std::size_t n) override {
					if(n)
						_impl.commit(n);
					return _impl.buffer();
				}
				std::size_t offset_impl() const noexcept override {
					return 0;
				}
				Impl& _impl;
		};

}

#endif

