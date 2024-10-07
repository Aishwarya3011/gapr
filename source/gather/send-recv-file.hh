#include <boost/asio/dispatch.hpp>

static void async_send_file_loop(gapr::mem_file&& file, gapr::server_end&& msg, std::size_t sz, std::size_t i) {
	auto buf=file.map(i);
	msg.async_write(buf, i+buf.size()>=sz, [file=std::move(file),msg,sz,i](bs_error_code ec, std::size_t nbytes) mutable {
		if(ec)
			return;
		if(i+nbytes>=sz) {
			gapr::print("send file ended");
			return;
		}
		async_send_file_loop(std::move(file), std::move(msg), sz, i+nbytes);
	});
}
static void async_send_file(ba::io_context& ctx, ba::thread_pool& pool, gapr::server_end::msg_hdr&& hdr, std::unique_ptr<std::streambuf>&& str, gapr::server_end&& msg) {
	gapr::promise<gapr::mem_file> prom{};
	auto fut=prom.get_future();
	boost::asio::dispatch(pool, [str=std::move(str),prom=std::move(prom)]() mutable {
		gapr::print("read begin");
		gapr::mutable_mem_file of{true};
		do {
			auto buf=of.map_tail();
			auto n=str->sgetn(buf.data(), buf.size());
			of.add_tail(n);
			if(n<0)
				throw;
			if(static_cast<std::size_t>(n)<buf.size())
				break;
		} while(true);
		gapr::print("read done");
		std::move(prom).set(std::move(of));
	});
	std::move(fut).async_wait(ctx.get_executor(), [hdr=std::move(hdr),msg=std::move(msg)](gapr::likely<gapr::mem_file>&& file) mutable {
		if(!file)
			throw;
		auto sz=file.value().size();
		gapr::print("read done: ", sz);
		auto buf=file.value().map(0);
		if(buf.size()==sz) {
			msg.async_send(std::move(hdr), 0, buf, [msg,file=std::move(file.value()),sz](bs_error_code ec) mutable {
				if(ec)
					return;
				gapr::print("send file ended");
			});
			return;
		}
		msg.async_send(std::move(hdr), 0, sz, [msg,file=std::move(file.value()),sz](bs_error_code ec) mutable {
			if(ec)
				return;
			async_send_file_loop(std::move(file), std::move(msg), sz, 0);
		});
	});
}

static void async_send_file_loop_x(gapr::mem_file&& file, gapr::server_end&& msg, std::size_t sz, std::size_t i, bool is_last, gapr::promise<bool>&& prom) {
	auto buf=file.map(i);
	msg.async_write(buf, is_last && i+buf.size()>=sz, [file=std::move(file),msg,sz,i,is_last,prom=std::move(prom)](bs_error_code ec, std::size_t nbytes) mutable {
		if(ec)
			return std::move(prom).set(ec);
		if(i+nbytes>=sz) {
			return std::move(prom).set(true);
		}
		async_send_file_loop_x(std::move(file), std::move(msg), sz, i+nbytes, is_last, std::move(prom));
	});
}
static void async_send_files(ba::io_context& ctx, ba::thread_pool& pool, std::shared_ptr<gather_model> model, gapr::server_end&& msg, gapr::commit_history&& hist, uint64_t upto) {
	auto cur_id=hist.body_count();
	assert(cur_id<upto);
	hist.body_count(cur_id+1);

	auto str=model->get_commit(cur_id);
	if(!str)
		return;
	gapr::promise<gapr::mem_file> prom{};
	auto fut=prom.get_future();
	boost::asio::dispatch(pool, [str=std::move(str),prom=std::move(prom)]() mutable {
		gapr::mutable_mem_file of{true};
		do {
			auto buf=of.map_tail();
			auto n=str->sgetn(buf.data(), buf.size());
			of.add_tail(n);
			if(n<0)
				throw;
			if(static_cast<std::size_t>(n)<buf.size())
				break;
		} while(true);
		std::move(prom).set(std::move(of));
	});
	std::move(fut).async_wait(ctx.get_executor(), [&ctx,&pool,model=std::move(model),msg=std::move(msg),hist=std::move(hist),upto](gapr::likely<gapr::mem_file>&& file) mutable {
		if(!file)
			throw;
		auto sz=file.value().size();
		gapr::promise<bool> prom2{};
		auto fut2=prom2.get_future();
		bool is_last=hist.body_count()>=upto;
		async_send_file_loop_x(std::move(file.value()), gapr::server_end{msg}, sz, 0, is_last, std::move(prom2));
		std::move(fut2).async_wait(ctx.get_executor(), [&ctx,&pool,model=std::move(model),msg=std::move(msg),hist=std::move(hist),upto](gapr::likely<bool>&& res) mutable {
			if(!res)
				throw;
			if(!res.value())
				return;
			if(hist.body_count()<upto)
				async_send_files(ctx, pool, std::move(model), std::move(msg), std::move(hist), upto);
		});
	});
}
static void async_send_file_enc(ba::io_context& ctx, ba::thread_pool& pool, gapr::server_end::msg_hdr&& hdr, std::unique_ptr<std::streambuf>&& str, gapr::server_end&& msg) {
	// XXX
	return async_send_file(ctx, pool, std::move(hdr), std::move(str), std::move(msg));
}
static void async_send_mem_file(ba::io_context& ctx, gapr::server_end::msg_hdr&& hdr, gapr::mem_file&& file, gapr::server_end&& msg) {
	auto sz=file.size();
	gapr::print("read done: ", sz);
	msg.async_send(std::move(hdr), 0, sz, [msg,file=std::move(file),sz](bs_error_code ec) mutable {
		if(ec)
			return;
		async_send_file_loop(std::move(file), std::move(msg), sz, 0);
	});
}
static void receive_file_func(gapr::promise<gapr::mem_file>&& prom, gapr::server_end&& msg, gapr::mutable_mem_file&& file) {
	auto buf=file.map_tail();
	msg.async_read(buf, [file=std::move(file),prom=std::move(prom),msg](bs_error_code ec, std::size_t nbytes) mutable {
		gapr::print("read stream : ", ec.message());
		if(ec) {
			if(ec==boost::asio::error::eof) {
				file.add_tail(nbytes);
				return std::move(prom).set(std::move(file));
			}
			return std::move(prom).set_error(to_std_error_code(ec));
		}
		file.add_tail(nbytes);
		receive_file_func(std::move(prom), std::move(msg), std::move(file));
	});
}
static void receive_file(gapr::promise<gapr::mem_file>&& prom, gapr::server_end&& msg) {
	gapr::mutable_mem_file file{true};
	return receive_file_func(std::move(prom), std::move(msg), std::move(file));
}
static gapr::future<gapr::mem_file> read_stream(gapr::server_end& msg) {
	gapr::promise<gapr::mem_file> prom;
	auto fut=prom.get_future();
	auto msg2=msg;
	receive_file(std::move(prom), std::move(msg2));
	return fut;
}

// XXX
static void discard_input(gapr::server_end& msg) {
	gapr::promise<gapr::mem_file> prom;
	auto msg2=msg;
	receive_file(std::move(prom), std::move(msg2));
}
