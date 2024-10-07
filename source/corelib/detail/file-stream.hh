namespace gapr {

	class file_input {
		public:
			explicit file_input(FILE* f): _f{f}, _buf{new char[BUFSIZ+STRM_MIN_BUF_SIZ]} {
			}
			~file_input() { }
			file_input(const file_input&) =delete;
			file_input& operator=(const file_input&) =delete;

			std::pair<const char*, std::size_t> buffer() {
				if(!_f) {
					if(_idx<_siz)
						return {_buf.get()+_idx, _siz-_idx};
					return {nullptr, 0};
				}
				if(_idx+STRM_MIN_BUF_SIZ<=_siz)
					return {_buf.get()+_idx, _siz-_idx};
				if(_idx>=STRM_MIN_BUF_SIZ) {
					std::memcpy(_buf.get(), _buf.get()+_idx, _siz-_idx);
					_siz-=_idx;
					_idx=0;
				}
				auto k=BUFSIZ+STRM_MIN_BUF_SIZ-_siz;
				if(k>BUFSIZ)
					k=BUFSIZ;
				auto r=std::fread(_buf.get()+_siz, 1, k, _f);
				fprintf(stderr, "read: %zd\n", r);
				if(r<k)
					_f=nullptr;
				_siz+=r;
				return {_buf.get()+_idx, _siz-_idx};
			}
			void consume(std::size_t n) {
				_idx+=n;
			}

			std::size_t offset() const noexcept {
				return 0;
			}
		private:
			FILE* _f;
			std::unique_ptr<char[]> _buf;
			std::size_t _idx{0};
			std::size_t _siz{0};
	};

	class file_output {
		public:
			explicit file_output(FILE* f): _f{f}, _buf{new char[BUFSIZ+STRM_MIN_BUF_SIZ]} {
				_off=ftell(f);
			}
			~file_output() { }
			file_output(const file_output&) =delete;
			file_output& operator=(const file_output&) =delete;

			std::pair<char*, std::size_t> buffer() {
				assert(_idx<BUFSIZ);
				return {_buf.get()+_idx, BUFSIZ+STRM_MIN_BUF_SIZ-_idx};
			}
			void commit(std::size_t n) {
				assert(_idx+n<=BUFSIZ+STRM_MIN_BUF_SIZ);
				_idx+=n;
				if(_idx>=BUFSIZ) {
					auto r=std::fwrite(_buf.get(), 1, BUFSIZ, _f);
					fprintf(stderr, "write: %zd\n", r);
					if(r!=BUFSIZ) {
						_f=nullptr;
						throw std::runtime_error{"error fwrite"};
					}
					_off+=BUFSIZ;
					_idx-=BUFSIZ;
					std::memcpy(_buf.get(), _buf.get()+BUFSIZ, _idx);
				}
			}
			void flush() {
				if(_idx>0) {
					auto r=std::fwrite(_buf.get(), 1, _idx, _f);
					fprintf(stderr, "write: %zd\n", r);
					if(r!=_idx) {
						_f=nullptr;
						throw std::runtime_error{"error fwrite"};
					}
					_off+=_idx;
					_idx=0;
				}
				//chain flush???
			}

			std::size_t offset() const noexcept {
				return _off+_idx;
			}

		private:
			FILE* _f;
			std::unique_ptr<char[]> _buf;
			std::size_t _idx{0};
			std::size_t _off;
	};

}
