#include "gapr/mem-file.hh"

#include <boost/beast/http/message.hpp>
#include <boost/beast/http/error.hpp>

struct mem_file_body {
public:
	struct value_type {
		std::size_t off;
		gapr::mem_file file;
		gapr::mutable_mem_file mfile;
	};


	static std::uint64_t size(value_type const& body) {
		return body.file?body.file.size():body.mfile.size();
	}

	class reader {
		value_type& body_;
	public:
		template<bool isRequest, class Fields>
		explicit reader(boost::beast::http::header<isRequest, Fields>&, value_type& b) : body_(b) {
		}
		void init(boost::optional<std::uint64_t> const&, boost::beast::error_code& ec) {
			assert(body_.mfile);
			ec = {};
		}
		template<class ConstBufferSequence>
		std::size_t put(ConstBufferSequence const& buffers, boost::beast::error_code& ec) {
			std::size_t tot{0};
			for(auto it=boost::asio::buffer_sequence_begin(buffers); it!=boost::asio::buffer_sequence_end(buffers); ++it) {
				auto buf=*it;
				while(buf.size()>0) {
					auto obuf=body_.mfile.map_tail();
					auto n=obuf.size();
					if(n>buf.size())
						n=buf.size();
					std::memcpy(obuf.data(), buf.data(), n);
					body_.mfile.add_tail(n);
					buf+=n;
					tot+=n;
				}
			}
			ec = {};
			return tot;
		}
		void finish(boost::beast::error_code& ec) {
			ec = {};
		}
	};

	class writer {
		value_type const& body_;
		std::size_t off;
	public:
		using const_buffers_type=boost::asio::const_buffer;

		template<bool isRequest, class Fields>
		explicit writer(boost::beast::http::header<isRequest, Fields> const&, value_type const& b) : body_(b), off{0} {
		}

		void init(boost::beast::error_code& ec) {
			assert(body_.file);
			off=0;
			ec={};
		}

		boost::optional<std::pair<const_buffers_type, bool>> get(boost::beast::error_code& ec) {
			ec = {};
			auto buf=body_.file.map(off);
			off+=buf.size();
			return {{{buf.data(), buf.size()}, off<body_.file.size()}};
		}
	};
};
