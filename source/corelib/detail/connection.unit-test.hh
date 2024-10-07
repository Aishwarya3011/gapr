/* core/connection.unit-test.hh
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

#include <unordered_map>
#include <random>
#include <deque>
#include "gapr/utility.hh"
#include "gapr/fix-error-code.hh"

#include <cinttypes>

static constexpr char certif[]="-----BEGIN CERTIFICATE-----\n" 
"MIICgTCCAeqgAwIBAgIJALf5NW67J6tWMA0GCSqGSIb3DQEBCwUAMFgxCzAJBgNV\n" 
"BAYTAkNOMQ0wCwYDVQQIDARUZXN0MQ0wCwYDVQQHDARUZXN0MQ0wCwYDVQQKDARU\n" 
"ZXN0MQ0wCwYDVQQLDARUZXN0MQ0wCwYDVQQDDARUZXN0MB4XDTE5MDIyMDExMDIw\n" 
"MloXDTI5MDIxNzExMDIwMlowWDELMAkGA1UEBhMCQ04xDTALBgNVBAgMBFRlc3Qx\n" 
"DTALBgNVBAcMBFRlc3QxDTALBgNVBAoMBFRlc3QxDTALBgNVBAsMBFRlc3QxDTAL\n" 
"BgNVBAMMBFRlc3QwgZ8wDQYJKoZIhvcNAQEBBQADgY0AMIGJAoGBANNTCB0/DK2I\n" 
"MKGfwmEJ82B3Bd2PG72nHN6n83QK+WTOwsNrmU7znnySBKQVkD0BzEXWKNlBU78z\n" 
"OWYKKp9DPZx7H6Hedmx4nyWelDS3fsXx9m+0RB25+QUCCJ1xWw5YyS3wAx7XYNJ7\n" 
"FnoT04JwE9cpEckQ34HJhgwdYAmUtwmdAgMBAAGjUzBRMB0GA1UdDgQWBBRumGsq\n" 
"0w+2IYgUJnBajwJrDEi/izAfBgNVHSMEGDAWgBRumGsq0w+2IYgUJnBajwJrDEi/\n" 
"izAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3DQEBCwUAA4GBAAXpp48lohJp0uM6\n" 
"Omikp8OuIvwUZMMhV6OqlmrZXQYlFI7a8kX8fZbzcYPTtHNbJtdo/DejdKjwBC9O\n" 
"oRCZ/AXiSJh9YnoQH9gEMoIVIiqMGWU4utIve77YAFC92M5o5SsqL6y0e0P/WP5d\n" 
"OvZHZrATRG7Ni4XjOhuZ5sjUtY90\n" 
"-----END CERTIFICATE-----\n";

static constexpr char priv_key[]="-----BEGIN RSA PRIVATE KEY-----\n"
"MIICeAIBADANBgkqhkiG9w0BAQEFAASCAmIwggJeAgEAAoGBANNTCB0/DK2IMKGf\n" 
"wmEJ82B3Bd2PG72nHN6n83QK+WTOwsNrmU7znnySBKQVkD0BzEXWKNlBU78zOWYK\n" 
"Kp9DPZx7H6Hedmx4nyWelDS3fsXx9m+0RB25+QUCCJ1xWw5YyS3wAx7XYNJ7FnoT\n" 
"04JwE9cpEckQ34HJhgwdYAmUtwmdAgMBAAECgYEAnuQDadoKMkAAMMXqZQQSVimn\n" 
"41rCxOptrowhZNMLiVxc3Ip+jvpl48v2aVk9RmGfsbLAq/mOemiNc8eZHH52R17H\n" 
"RIt3O6CSDEgQbPSkD40s8VpHiZpdrgDrwjGxGyn/Qf+juU2j/qUkg/Yv/4TIkLPP\n" 
"rENC3+i9xR6GjvBGKZECQQD9os7Ja+gjkM48OlhZFQsWWm1GeTQPr2gdFlsrIhbP\n" 
"wEbDJwk2Gk+apy4wpSg5c1bmn1hiSKMpaC0EbLrigkwXAkEA1UtD/c8mjI2ZZOdF\n" 
"BBK4IQ5/yInJN19zoXYo71IGNhXpcjJXVQYGzwulcGiHsuAlLMVFwRFrQHlm1jl/\n" 
"aaIkawJBAKN7RxIpDU2vTl8ftEZm++iCjKC6CsZ5ZwAoosbgiBeMdY/gH13SA7FJ\n" 
"RMpyCcVOBJtN5egjrZdI4ItTkfnhxi0CQQDQsgAoyxMw2yuHqzHANoCq94DYOtkC\n" 
"sQZ2qSKMZ3lkWjQ8ZD1HF8p5sy+AuQZtYxt8ntkNe3mxcBfK7B9awCdZAkBpRF1C\n" 
"2LusBiuT2YR0gH//16rWSNXoSdTg6fF7BZLQ/C7KFZKhk85wlFObCRHcK2PhTZv4\n" 
"RmzphSne8GIIJKtK\n" 
"-----END RSA PRIVATE KEY-----";

namespace {
	using namespace gapr;
	struct ChkConnection {
		using ssl_context=gapr::connection::ssl_context;
		using io_context=boost::asio::io_context;
		using Buf=std::vector<char>;
		using endpoint=boost::asio::ip::tcp::endpoint;
		using socket=boost::asio::ip::tcp::socket;
		using error_code=boost::system::error_code;

		io_context io_ctx{1};
		ssl_context ssl_ctx{ssl_context::tls};

		endpoint start_acceptor() {
			using acceptor=boost::asio::ip::tcp::acceptor;
			error_code ec;
			auto addr=boost::asio::ip::make_address("127.0.0.1", ec);
			if(ec)
				throw std::system_error{to_std_error_code(ec)};

			do {
				auto acc=std::make_shared<acceptor>(io_ctx);
				endpoint bep{addr, 0};
				acc->open(bep.protocol(), ec);
				if(ec) break;
				acc->set_option(socket::reuse_address(true), ec);
				if(ec) break;
				acc->bind(bep, ec);
				if(ec) break;
				acc->listen(1, ec);
				if(ec) break;
				auto ep=acc->local_endpoint(ec);
				if(ec) break;

				acc->async_accept([acc,this](error_code ec, socket&& sock) {
					if(ec)
						throw std::system_error{to_std_error_code(ec)};
					auto ssl=std::make_unique<ba::ssl::stream<socket>>(std::move(sock), ssl_ctx);
					auto& ssl_=*ssl;
					ssl_.async_handshake(ssl->server, [ssl=std::move(ssl),this](error_code ec) mutable {
						if(ec)
							throw std::system_error{to_std_error_code(ec)};
						gapr::server_end srv{std::move(ssl)};
						gapr::print("handshake srv", 1,2,3);
						srv.async_shutdown([srv](error_code ec, socket&& s) {
							gapr::print("shutdown srv", 1,2,3);
							s.shutdown(socket::shutdown_both);
						});
						do_read(std::move(srv));
					});
				});
				return ep;
			} while(false);
			throw std::system_error{to_std_error_code(ec)};
		}

		void do_read(gapr::server_end&& req) {
			req.async_recv([this,req](error_code ec, const gapr::server_end::msg_hdr_in& hdr) mutable {
				if(ec) {
					if(ec==ba::error::eof)
						return;
					throw std::system_error{to_std_error_code(ec)};
				}
				do_read(gapr::server_end{req});
				do_read1(std::move(hdr), std::move(req));
			});
		}

		void start_connector(const endpoint& ep) {
			auto ssl=std::make_unique<ba::ssl::stream<socket>>(io_ctx, ssl_ctx);
			auto& sock=ssl->next_layer();
			sock.async_connect(ep, [ssl=std::move(ssl),this](error_code ec) mutable {
				if(ec)
					throw std::system_error{to_std_error_code(ec), "error conn"};
				gapr::print(1, 2, 3, "connected", 1,2,3);
				auto& ssl_=*ssl;
				ssl_.async_handshake(ssl->client, [ssl=std::move(ssl),this](error_code ec) mutable {
					if(ec)
						throw std::system_error{to_std_error_code(ec), "error hs"};
#if 0
					gapr::print("handshake cli");
					gapr::print("code: ", hdr.line_ptr());
#endif
					gapr::client_end cli{std::move(ssl)};
					cli.async_shutdown([cli](error_code ec, socket&& s) {
						gapr::print("shutdown cli", 1,2,3);
						s.shutdown(socket::shutdown_both);
					});
					do_write(std::move(cli));
				});
			});
		}

		int run() {
			ssl_ctx.use_certificate({certif, sizeof(certif)},
					gapr::connection::ssl_context::pem);
			ssl_ctx.use_private_key({priv_key, sizeof(priv_key)},
					gapr::connection::ssl_context::pem);

			prepare_bufs();

			auto ep=start_acceptor();
			start_connector(ep);
			io_ctx.run();

			return check_bufs();
		}

		int check_bufs() {
			int ndiff=0;
			while(!_sent_msgs.empty()) {
				auto it=_sent_msgs.begin();
				auto id=it->first;
				auto buf=std::move(it->second);
				_sent_msgs.erase(it);
				auto it2=_recv_msgs.find(id);
				if(it2==_recv_msgs.end()) {
					gapr::print("- ", id, ' ', buf.size());
					ndiff++;
					continue;
				}
				auto buf2=std::move(it2->second);
				_recv_msgs.erase(it2);
				if(buf!=buf2) {
					gapr::print("- ", id, ' ', buf.size());
					gapr::print("+ ", id, ' ', buf2.size());
					ndiff++;
					continue;
				}
			}
			while(!_recv_msgs.empty()) {
				auto it2=_recv_msgs.begin();
				auto id2=it2->first;
				auto buf2=std::move(it2->second);
				_recv_msgs.erase(it2);
				gapr::print("+ ", id2, ' ', buf2.size());
				ndiff++;
			}

			return ndiff;
		}


		void do_read2(gapr::server_end&& rmsg, uint64_t id, std::size_t idx) {
			auto& buf=_recv_msgs[id];
			buf.resize(idx+nchk1);
			rmsg.async_read(buffer_view{buf.data()+idx, nchk1}, [rmsg,this,id,idx](bs::error_code ec, std::size_t nbytes) mutable {
				if(ec) {
					if(ec==ba::error::eof) {
						auto& buf=_recv_msgs[id];
						buf.resize(idx+nbytes);
						return;
					}
					throw std::runtime_error("failed to read msg");
				}
				assert(nbytes==nchk1);
				do_read2(std::move(rmsg), id, idx+nbytes);
			});
		}

		void do_read1(gapr::server_end::msg_hdr_in hdr, gapr::server_end&& rmsg) {
			if(!hdr.tag_is("FILE")) {
				gapr::print("unknown tag");
				return;
			}
			uint64_t id;
			auto args=hdr.args();
			auto res=gapr::make_parser(id).from_dec(static_cast<const char*>(args.data()), args.size());
			if(!res.second || res.first!=args.size()) {
				gapr::print("unknown args");
				return;
			}
			auto ins=_recv_msgs.emplace(id, Buf{});
			if(!ins.second) {
				gapr::print("dup id: ", id);
				return;
			}

			gapr::server_end::msg_hdr rhdr{"OK"};
			switch(rmsg.type_in()) {
				case gapr::server_end::msg_type::hdr_only:
					//gapr::print("start read hdronly");
					break;
#if 0
					// nbytes_til_eof??? XXX
				case gapr::server_end::msg_type::one_chunk:
					//gapr::print("start read chunk");
					{
						auto& buf=_recv_msgs[id];
						buf.resize(rmsg.size_in());
						rmsg.async_read(buffer_view{buf.data(), buf.size()}, [rmsg](bs::error_code ec, std::size_t nbytes) {
							assert(ec==ba::error::eof);
						});
					}
					break;
#endif
				case gapr::server_end::msg_type::stream:
					//gapr::print("start read stream");
					do_read2(gapr::server_end{rmsg}, id, 0);
					break;
				default:
					gapr::print("unknown type");
					throw;
			}
			rmsg.async_send(std::move(rhdr), [rmsg](bs::error_code ec) {
			});
		}

		enum Type {
			HDR_ONLY,
			SINGLE_RAW,
			STR_RAW,
		};
		struct Msg {
			Type type;
			Buf body;
			uint64_t uid;
			Msg(Type type, Buf&& buf) noexcept: type{type}, body{std::move(buf)} { }
		};
		constexpr static std::size_t nchk1=1024;
		constexpr static std::size_t nchk0=2048;
		std::deque<Msg> msgs;
		std::unordered_map<uint64_t, Buf> _sent_msgs;
		std::unordered_map<uint64_t, Buf> _recv_msgs;
		void prepare_bufs() {
			gapr::print("preparing ");
			for(uint64_t i=0; i<1024; i++) {
				msgs.emplace_back(HDR_ONLY, Buf{});
			}
			gapr::print("preparing .");
			for(uint64_t i=0; i<1024; i++) {
				Buf mbuf(128);
				auto n=snprintf(mbuf.data(), mbuf.size(), "a longerrrrrrrrr     single_raw %" PRIu64 "\n", i);
				mbuf.resize(n);
				msgs.emplace_back(SINGLE_RAW, std::move(mbuf));
			}
			gapr::print("preparing ....");
			std::mt19937 gen(std::random_device{}());
			std::uniform_int_distribution<> dis(0, 2'000'000);
			for(uint64_t i=0; i<256; i++) {
				Buf mbuf(dis(gen));
				auto l=snprintf(mbuf.data(), mbuf.size(), "str_raw %" PRIu64 "\n", i);
				uint32_t o=l;
				auto p=mbuf.data();
				while(o<mbuf.size()) {
					p[o]=p[o-l];
					o++;
				}
				msgs.emplace_back(STR_RAW, std::move(mbuf));
			}
			gapr::print("preparing ......");
			std::shuffle(msgs.begin(), msgs.end(), std::random_device());
			std::shuffle(msgs.begin(), msgs.end(), std::random_device());
			gapr::print("preparing .......");
			uint64_t uid=10000;
			for(auto& msg: msgs)
				msg.uid=uid++;
			gapr::print("preparing done");
		}

		//cli: conn login select upload done serial
		//gui: commit... pull...

		void do_write(gapr::client_end&& clip) {
			if(msgs.empty()) {
				gapr::print("do_write ", "end");
				return;
			}
			auto msg=std::move(msgs.front());
			msgs.pop_front();
			gapr::print("do_write ", msg.uid, ':', msg.type);

			switch(msg.type) {
				case HDR_ONLY:
					{
						gapr::connection::msg_hdr hdr{"FILE", msg.uid};
						clip.async_send(std::move(hdr),
								[this,clip,uid=msg.uid](bs::error_code ec) mutable {
									if(ec)
										throw std::system_error{to_std_error_code(ec)};
									_sent_msgs[uid]=Buf{};
									clip.async_recv([this,clip](bs::error_code ec, gapr::client_end::msg_hdr_in hdr) mutable {
										if(ec)
											throw std::system_error{to_std_error_code(ec)};
										if(!msgs.empty())
											do_write(std::move(clip));
									});
									gapr::print("msgrecv");
								});
						gapr::print("msgsend");

					}
					break;
				case SINGLE_RAW:
					{
						gapr::connection::msg_hdr hdr{"FILE", msg.uid};
						std::string_view buf{msg.body.data(), msg.body.size()};
						//gapr::print("buf: ", msg.body.size());
						clip.async_send(std::move(hdr), 0, buf, [this,clip,uid=msg.uid,
								bdy=std::move(msg.body)](bs::error_code ec) mutable {
									if(ec)
										throw std::system_error{to_std_error_code(ec)};
									_sent_msgs[uid]=std::move(bdy);
									//gapr::print("written ", uid);
									clip.async_recv([this,clip](bs::error_code ec, gapr::client_end::msg_hdr_in hdr) mutable {
										if(ec)
											throw std::system_error{to_std_error_code(ec)};
										if(!msgs.empty())
											do_write(std::move(clip));
									});
								});
					}
					break;
				case STR_RAW:
					{
						gapr::connection::msg_hdr hdr{"FILE", msg.uid};
						clip.async_send(std::move(hdr), 0, msg.body.size(), [this,clip,bdy=std::move(msg.body),uid=msg.uid](error_code ec) mutable {
							if(ec)
								throw std::system_error{to_std_error_code(ec)};
							clip.async_recv([this,clip](error_code ec, gapr::client_end::msg_hdr_in res) mutable {
								if(ec)
									throw std::system_error{to_std_error_code(ec)};
								if(!msgs.empty())
									do_write(std::move(clip));
							});

							do_write1(std::move(clip), uid, std::move(bdy), 0);
						});
					}
					break;
			}
		}
		void do_write1(gapr::client_end&& clip, uint64_t uid, Buf&& bdy, std::size_t idx) {
			std::size_t len=bdy.size()-idx;
			std::string_view buf{bdy.data()+idx, len>nchk0?nchk0:len};
			clip.async_write(buf, len<=nchk0, [this,clip,uid,len,idx,bdy=std::move(bdy)](error_code ec, std::size_t nbytes) mutable {
				if(ec)
					throw std::system_error{to_std_error_code(ec)};
				assert(nbytes==(len>nchk0?nchk0:len));
				if(len<=nchk0) {
					_sent_msgs[uid]=std::move(bdy);
					return;
				}
				idx+=nbytes;
				do_write1(std::move(clip), uid, std::move(bdy), idx);
			});
		}
	};
}

namespace gapr_test { int chk_connection() {
	ChkConnection test{};
	return test.run();
} }

