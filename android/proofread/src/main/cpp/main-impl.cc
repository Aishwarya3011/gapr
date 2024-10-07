constexpr static int VOLUME_SCALE=3;
std::array<double, 2> Engine::calc_xfunc(std::size_t ch) {
	auto& st=_xfunc_states[ch-1];
	auto& range=_cube_infos[ch-1].range;
	auto d=range[1]-range[0];
	return {range[0]+st[0]*d, range[1]+(st[1]-1)*d};
}
void Engine::calc_center(Position& pos, std::size_t ch) {
	auto& info=_cube_infos[ch-1];
	gapr::node_attr pt;
	for(unsigned int i=0; i<3; i++) {
		double x{0.0};
		for(unsigned int j=0; j<3; j++)
			x+=info.xform.direction[i+j*3]*info.sizes[j];
		pt.pos(i, x/2+info.xform.origin[i]);
	}
	pt.misc.data=1;
	pos.point=pt.data();
}
double Engine::calc_default_zoom(std::size_t ch, unsigned int n) {
	auto& info=_cube_infos[ch-1];
	double max_d{0.0};
	for(unsigned int i=0; i<3; i++) {
		auto s=(n==0?info.sizes[i]:info.cube_sizes[i]*n);
		auto d=info.xform.resolution[i]*s;
		if(d>max_d)
			max_d=d;
	}
	return max_d/3;
}

void Engine::xfunc_closeup_changed(double low, double up) {
	if(_closeup_ch) {
		_xfunc_states[_closeup_ch-1][0]=low;
		_xfunc_states[_closeup_ch-1][1]=up;
		set_closeup_xfunc(calc_xfunc(_closeup_ch));
	}
	apply_changes();
}
void Engine::select_closeup_changed(int index) {
#if 0
	auto map_g=_ui_display.select_closeup->itemData(index).value<std::size_t>();
	if(map_g==_closeup_ch)
		return;
	if(_closeup_ch) {
		_xfunc_states[_closeup_ch][2]=_ui_display.xfunc_closeup->minimum();
		_xfunc_states[_closeup_ch][3]=_ui_display.xfunc_closeup->maximum();
	}
	_closeup_ch=map_g;
	if(_closeup_ch) {
		_ui_display.xfunc_closeup->set_state(_xfunc_states[_closeup_ch-1]);
		_ui_display.xfunc_closeup->setEnabled(true);
		auto& info=_cube_infos[_closeup_ch-1];
		_ui.canvas->set_closeup_info(info.xform, info.sizes, info.cube_sizes, {});
		_ui.canvas->set_closeup_xfunc(calc_xfunc(_closeup_ch));
		//??**void set_closeup_cube(gapr::cube cube, std::string&& uri);
		startDownloadCube();
	} else {
		_ui_display.xfunc_closeup->setEnabled(false);
	}
#endif
}

void Engine::startDownloadCube() {
	gapr::node_attr pt{_cur_pos.point};
	auto map_c=_closeup_ch;
	if(map_c!=0) {
		if(_cube_builder.build(map_c, _cube_infos[map_c-1], {pt.pos(0), pt.pos(1), pt.pos(2)}, false, _closeup_cube?&_closeup_offset:nullptr)) {
			std::array<uint32_t, 6> bboxi;
			if(!_cube_builder.get_bbox(bboxi)) {
				_filter_bbox={0.0, 0.0, 0.0, -1.0};
			} else {
				calc_bbox(_cube_infos[_closeup_ch-1].xform, bboxi, _filter_bbox);
			}
			call_java(100, "0");
		}
	}
}
void Engine::cubeFinished(std::error_code ec, int progr) {
	if(ec)
		LOGV("error load cube: %s", ec.message().c_str());
	if(progr==1001) {
		// XXX hide progr
		auto cube=_cube_builder.get();
		while(cube.data) {
			gapr::print("cube_out: ", cube.chan);
			if(_closeup_ch==cube.chan) {
				_closeup_cube=cube.data;
				_closeup_offset=cube.offset;
				set_closeup_cube(std::move(cube.data), std::move(cube.uri), cube.offset);
			}

			cube=_cube_builder.get();
		}
		apply_changes();
	}
	std::ostringstream oss;
	oss<<progr;
	call_java(101, oss.str().c_str());
	_cube_builder.async_wait([this](std::error_code ec, int progr) {
		return cubeFinished(ec, progr);
	});
}
void Engine::target_changed(const Position& pos, gapr::edge_model::view model) {
	bool chg=false;
	if(_tgt_pos.edge!=pos.edge)
		chg=true;
	if(pos.edge) {
		if(_tgt_pos.index!=pos.index)
			chg=true;
	} else {
		if(_tgt_pos.vertex!=pos.vertex)
			chg=true;
	}
	if(!(_tgt_pos.point==pos.point))
		chg=true;
	//gapr::print("tgt_chg: ", chg, ' ', pos.edge, ':', pos.index, ':');
	if(chg) {
		_tgt_pos=pos;
		if(pos.valid())
			clear_selection();
		_tgt_anchor=model.to_anchor(pos);
		gapr::print("tgt_pos: ", _tgt_anchor.link.nodes[0].data, ':', _tgt_anchor.link.nodes[1].data);
		_tgt_fix=FixPos{};
		//updateActions;
		set_target(pos);
		apply_changes();
	}
}
void Engine::pick_position() {
	auto pos=pick();
	edge_model::reader graph{_model};
	if(!pos.edge && !pos.vertex) {
		target_changed(pos, graph);
		return;
	}
	{
		if(pos.edge) {
			auto edge_id=pos.edge;
			auto pickPos=pos.index;
			auto& e=graph.edges().at(edge_id);
			// XXX disable between-node picking for now
			{
				auto r=pickPos%128;
				pickPos=pickPos/128*128+(r>64?128:0);
			}
			auto idx=pickPos/128;
			if(idx>e.points.size()-1)
				idx=e.points.size()-1;
			gapr::node_attr p{e.points[idx]};
			gapr::node_attr p0{e.points[0]};
			gapr::node_attr p1{e.points[e.points.size()-1]};
			do {
				if(idx==0)
					break;
				if(idx==e.points.size()-1)
					break;
				if(e.points.size()<7)
					break;
				if(p.dist_to(p0)<p0.misc.r()) {
					pickPos=(idx=0)*128;
					break;
				}
				if(p.dist_to(p1)<p1.misc.r()) {
					pickPos=(idx=e.points.size()-1)*128;
					break;
				}
			} while(false);
			if(auto r=pickPos%128; r!=0) {
				gapr::node_attr p2{e.points[idx+1]};
				for(unsigned int i=0; i<3; i++) {
					auto x0=p.pos(i);
					auto x1=p2.pos(i);
					p.pos(i, (x0*(128-r)+x1*r)/128);
				}
				pos=Position{edge_id, pickPos, p.data()};
			} else {
				if(idx==0) {
					pos=Position{e.left, p0.data()};
				} else if(idx==e.points.size()-1) {
					pos=Position{e.right, p1.data()};
				} else {
					pos=Position{edge_id, pickPos, p.data()};
				}
			}
		} else if(pos.vertex) {
			auto it=graph.vertices().find(pos.vertex);
			if(it!=graph.vertices().end()) {
				pos=Position{pos.vertex, it->second.attr.data()};
			} else {
				auto pos2=graph.nodes().at(pos.vertex);
				assert(pos2.edge);
				auto& edg=graph.edges().at(pos2.edge);
				pos=Position{pos2.edge, pos2.index, edg.points[pos2.index/128]};
			}
		}
	}
	target_changed(pos, graph);
}
void Engine::pick_position(const gapr::fix::Position& pos) {
	edge_model::reader graph{_model};
	target_changed(pos, graph);
}

// XXX do this when graph changed(edge changed): priv->path.path.clear();
// change current edge binding
// change target edge binding
void Engine::jumpToPosition(const Position& pos, gapr::edge_model::view model) {
	_cur_pos=pos;
	_path.nodes.clear(); // path is valid only when it's in current cube

	_cur_anchor=model.to_anchor(_cur_pos);
	gapr::print("cur_pos: ", _cur_anchor.link.nodes[0].data, ':', _cur_anchor.link.nodes[1].data);
	_cur_fix=FixPos{};

	set_current(_cur_pos);
	set_cursor(0, 0, false);
	set_path_data({});
	apply_changes();
}

void Engine::critical_err(const std::string& err, const std::string& info, const std::string& detail) const {
#if 000
	ba::post(_ui_ctx, [this,
			err=QString::fromStdString(err),
			info=QString::fromStdString(info),
			detail=QString::fromStdString(detail)
	]() { window.critical_error(err, info, detail); });
#endif
}
void Engine::get_passwd(const std::string& err) const {
#if 000
	gapr::print("ask_passwd");
	gapr::str_glue msg{_args->user, '@', _args->host, ':', _args->port};
	ba::post(_ui_ctx, [this,
			err=QString::fromStdString(err),
			msg=QString::fromStdString(msg.str())
	]() { window.ask_password(msg, err); });
#endif
}
void Engine::show_message(const std::string& msg) const {
#if 000
	ba::post(_ui_ctx, [this,
			msg=QString::fromStdString(msg)
	]() { window.show_message(msg); });
#endif
}
void Engine::ask_retry(const std::string& err, const std::string& info, const std::string& detail) const {
#if 000
	ba::post(_ui_ctx, [this,
			err=QString::fromStdString(err),
			info=QString::fromStdString(info),
			detail=QString::fromStdString(detail)
	]() { window.show_retry_dlg(err, info, detail); });
#endif
}



void Engine::got_passwd(std::string&& pw) {
#if 000
	if(pw.empty())
		return get_passwd("Empty.");
	_args->passwd=std::move(pw);
	if(_conn_need_pw)
		login(std::move(_conn_need_pw));
	else
		connect();
#endif
}





bool Engine::check_connect() {
	if(!_prelock_model.can_read_async())
		return false;
	if(!_prelock_path.can_write_later())
		return false;
	if(!_cur_pos.valid())
		return false;
	if(!_tgt_pos.valid())
		return false;
	if(!_closeup_cube)
		return false;
	// XXX in cube
	return true;
}

void Engine::on_tracing_connect_triggered() {
	if(!check_connect())
		return;
	gapr::print("sc: asdf");
	ConnectAlg alg{&_model, _cur_pos, _tgt_pos};
	alg.args.cube=_closeup_cube;
	alg.args.offset=_closeup_offset;
	alg.args.xform=&_cube_infos[_closeup_ch-1].xform;
	alg.args.xfunc=calc_xfunc(_closeup_ch);
	if(_prev_conn) {
		if(alg.args.cur_pos==_prev_conn->cur_pos && alg.args.tgt_pos==_prev_conn->tgt_pos)
			alg.args.method=_prev_conn->method+1;
		_prev_conn->cancel_flag=true;
	}
	auto cur_conn=std::make_shared<ConnectSt>();
	cur_conn->method=alg.args.method;
	cur_conn->cur_pos=alg.args.cur_pos;
	cur_conn->tgt_pos=alg.args.tgt_pos;
	alg.args.cancel=&cur_conn->cancel_flag;

	_prelock_model.begin_read_async();
	_prev_conn=cur_conn;
	update_state(StateMask::Model|StateMask::Path);

	gapr::promise<gapr::delta_add_edge_> prom;
	auto fut=prom.get_future();
	auto ex1=_pool.get_executor();
	ba::post(ex1, [prom=std::move(prom),alg=std::move(alg)]() mutable {
		try {
			gapr::print("asdfasdf");
			std::move(prom).set(alg.compute());
			gapr::print("gggg");
		} catch(const std::runtime_error& e) {
			unlikely(std::move(prom), std::current_exception());
		}
	});

	auto ex2=_ui_ctx.get_executor();
	std::move(fut).async_wait(ex2, [this,cur_conn](gapr::likely<gapr::delta_add_edge_>&& res) {
	//XXX if(_priv->has_binding_to_gui) ...;
		if(!cur_conn->cancel_flag.load())
	do {
		if(!res) try {
			auto ec=std::move(res).error();
			std::cerr<<ec.code().message()<<'\n';
			break;
		} catch(const std::runtime_error& e) {
			std::cerr<<e.what()<<'\n';
			break;
		}
		_path=std::move(res.get());
		auto pth=_path.nodes;
		set_path_data(std::move(pth));
	} while(false);
	//priv->viewer->setProgressData({});
	//priv->viewer->update();
	apply_changes();

	_prelock_model.end_read_async();
	update_state(StateMask::Model|StateMask::Path);
	});
}


void Engine::update_model_stats(gapr::edge_model::view model) {
	std::ostringstream oss;
	//oss<<"Number of commits: "<<model.nodes().size();
	oss<<"Number of nodes: "<<model.nodes().size();
	oss<<"\nNumber of vertices: "<<model.vertices().size();
	oss<<"\nNumber of edges: "<<model.vertices().size();
	oss<<"\nNumber of annotations: "<<model.props().size()<<'\n';
}

Position Engine::fix_pos(const Position& pos, anchor_id anchor, gapr::edge_model::view model) {
	if(anchor.link) {
		if(anchor.link.on_node()) {
			auto p=model.to_position(anchor.link.nodes[0]);
			if(p.edge) {
				auto e=model.edges().at(p.edge);
				return Position{p.edge, p.index, e.points[p.index/128]};
			} else {
				if(p.vertex) {
					auto it=model.vertices().find(p.vertex);
					if(it==model.vertices().end()) {
						return Position{pos.point};
					} else {
						return Position{p.vertex, it->second.attr.data()};
					}
				} else {
					return Position{pos.point};
				}
			}
		} else {
			// XXX rebind link
			return pos;
		}
	} else {
		return pos;
	}
}
Position Engine::fix_pos(const Position& pos, FixPos fix, gapr::edge_model::view model, gapr::node_id nid0) {
	gapr::node_id nid;
	switch(fix.state) {
		case FixPos::Empty:
			return pos;
		case FixPos::Future:
			nid=nid0.offset(fix.offset);
			break;
		case FixPos::Null:
			break;
		case FixPos::Node:
			nid=fix.node;
			break;
	}
	if(!nid)
		return {};

	auto p=model.to_position(nid);
	if(p.edge) {
		auto e=model.edges().at(p.edge);
		return Position{p.edge, p.index, e.points[p.index/128]};
	} else {
		if(p.vertex) {
			auto v=model.vertices().at(p.vertex);
			return Position{p.vertex, v.attr.data()};
		} else {
			return Position{pos.point};
		}
	}
}
bool Engine::model_apply(bool last) {
	//auto ex2=_ui_ctx.get_executor();
	//assert(ex2.running_in_this_thread());

	edge_model::updater updater{_model};
	if(!updater.empty()) {
		if(!updater.apply())
			return false;

		gapr::print(1, "model changed");
		model_changed(updater.edges_del());

		updater.nid0();
		//this{model}
		if(!last || !_cur_fix)
			jumpToPosition(fix_pos(_cur_pos, _cur_anchor, updater), updater);
		if(!last || !_tgt_fix)
			target_changed(fix_pos(_tgt_pos, _tgt_anchor, updater), updater);

		//upgrade cur/tgt;
	}
	if(last) {
		auto nid0=updater.nid0();
		if(_cur_fix && nid0)
			jumpToPosition(fix_pos(_cur_pos, _cur_fix, updater, nid0), updater);
		if(_tgt_fix && nid0)
			target_changed(fix_pos(_tgt_pos, _tgt_fix, updater, nid0), updater);
		_cur_anchor=updater.to_anchor(_cur_pos);
		_tgt_anchor=updater.to_anchor(_tgt_pos);
		_cur_fix=FixPos{};
		_tgt_fix=FixPos{};
		update_model_stats(updater);
	}
	return true;
}
bool Engine::load_commits(gapr::fiber_ctx& ctx, client_end msg, uint64_t upto) {
	//ensure conn
	assert(!_prelock_model.can_read_async());
	if(_hist.body_count()>=upto)
		return true;
	int i=0;
	gapr::timer<4> timer;
	gapr::mem_file commits_file;
	{
		gapr::promise<gapr::mem_file> prom{};
		gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
		auto ex1=_pool.get_executor();
		ba::post(ex1, [hist=_hist,prom=std::move(prom)]() mutable {
			gapr::mem_file hist_data;
			try {
				hist_data=serialize(hist);
				std::move(prom).set(std::move(hist_data));
			} catch(const std::runtime_error& e) {
				return unlikely(std::move(prom), std::current_exception());
			}
		});
		auto hist_data=std::move(fib2).async_wait(gapr::yield{ctx});
		timer.mark<0>();
		gapr::fiber fib{ctx.get_executor(), api.get_commits(msg, std::move(hist_data), upto)};
		auto cmts=std::move(fib).async_wait(gapr::yield{ctx});
		commits_file=std::move(cmts.file);
		timer.mark<1>();
	}
	auto strmbuf=gapr::make_streambuf(std::move(commits_file));

	while(_hist.body_count()<upto) {
		auto ex1=_pool.get_executor();
		gapr::promise<uint64_t> prom{};
		gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
		ba::post(ex1, [this,prom=std::move(prom),strmbuf=strmbuf.get()]() mutable {
			gapr::commit_info info;
			if(!info.load(*strmbuf))
				gapr::report("commit file no commit info");
			auto ex1=_pool.get_executor();
			assert(ex1.running_in_this_thread());
			(void)ex1;
			gapr::edge_model::loader loader{_model};
			auto r=gapr::delta_variant::visit<bool>(gapr::delta_type{info.type},
					[&loader,&info,strmbuf](auto typ) {
						gapr::delta<typ> delta;
						if(!gapr::load(delta, *strmbuf))
							gapr::report("commit file no delta");
						if(!loader.load(gapr::node_id{info.nid0}, std::move(delta)))
							return false;
						return true;
					});
			if(!r)
				return std::move(prom).set(std::numeric_limits<uint64_t>::max());
			return std::move(prom).set(info.id);
		});

		auto next_id=std::move(fib2).async_wait(gapr::yield{ctx});
		if(next_id!=_hist.body_count())
			return false;
		timer.mark<2>();
		_hist.body_count(next_id+1);
		i++;

		if(i%200==0) {
			if(!model_filter(ctx))
				return false;
			gapr::promise<bool> prom{};
			gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
			auto ex2=_ui_ctx.get_executor();
			ba::post(ex2, [this,prom=std::move(prom)]() mutable {
				std::move(prom).set(model_apply(false));
			});
			if(!std::move(fib2).async_wait(gapr::yield{ctx}))
				return false;
		}
		timer.mark<3>();
	}
	gapr::print("load commits timming: ", timer);
	return true;
}
bool Engine::model_filter(gapr::fiber_ctx& ctx) {
	gapr::promise<bool> prom{};
	gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
	auto ex1=_pool.get_executor();
	ba::post(ex1, [this,prom=std::move(prom)]() mutable {
		//auto ex1=_pool.get_executor();
		//assert(ex1.running_in_this_thread());
		edge_model::loader loader{_model};
		if(!loader.merge())
			return std::move(prom).set(false);
		auto r=loader.filter();
		return std::move(prom).set(r);
	});
	if(!std::move(fib2).async_wait(gapr::yield{ctx}))
		return false;
	return true;
}
bool Engine::model_filter(gapr::fiber_ctx& ctx, const std::array<double, 6>& bbox) {
	gapr::print(1, "model filter", bbox[0], ',', bbox[1], ',', bbox[2], ',', bbox[3], ',', bbox[4], ',', bbox[5]);
	gapr::promise<bool> prom{};
	gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
	auto ex1=_pool.get_executor();
	ba::post(ex1, [this,prom=std::move(prom),bbox]() mutable {
		//auto ex1=_pool.get_executor();
		//assert(ex1.running_in_this_thread());
		edge_model::loader loader{_model};
		auto r=loader.filter(bbox);
		return std::move(prom).set(r);
	});
	if(!std::move(fib2).async_wait(gapr::yield{ctx}))
		return false;
	return true;
}
bool Engine::load_model_state(gapr::fiber_ctx& ctx, client_end msg) {
	//ensure conn
	assert(!_prelock_model.can_read_async());
	gapr::timer<3> timer;
	timer.mark<0>();
	std::unique_ptr<std::streambuf> cachebuf;
	gapr::fiber fib{ctx.get_executor(), api.get_model(msg)};
	auto cmt=std::move(fib).async_wait(gapr::yield{ctx});
	timer.mark<1>();
	if(!cmt.file)
		return true;
	cachebuf=gapr::make_streambuf(std::move(cmt.file));

	gapr::promise<uint64_t> prom{};
	gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
	auto ex1=_pool.get_executor();
	ba::post(ex1, [this,prom=std::move(prom),buf=std::move(cachebuf)]() mutable {
		//auto ex1=_pool.get_executor();
		//assert(ex1.running_in_this_thread());
		edge_model::loader loader{_model};
		auto r=loader.init(*buf);
		return std::move(prom).set(r);
	});
	auto r=std::move(fib2).async_wait(gapr::yield{ctx});
	if(!r)
		return false;
	timer.mark<2>();
	_hist.body_count(r);
	gapr::print("load model_state timming: ", timer);
	return true;
}
gapr::future<bool> Engine::load_latest_commits() {
	// cur_conn
	if(!_prelock_model.can_write_later())
		return {};

	_prelock_model.begin_write_later();
	update_state(StateMask::Model);

	gapr::fiber fib{_io_ctx.get_executor(), [this](gapr::fiber_ctx& ctx) {
		if(1 && _hist.body_count()==0 && _hist.tail().empty()) {
			if(!load_model_state(ctx, _cur_conn))
				return false;
		}
		if(!_hist.tail().empty()) {
			if(_hist.tail_tip()+1>_latest_commit)
				_latest_commit=_hist.tail_tip()+1;
		}
		if(!load_commits(ctx, _cur_conn, _latest_commit))
			return false;
		return model_filter(ctx);
	}};
	return fib.get_future();
}
void Engine::latest_commits_loaded(gapr::likely<bool>&& res) {
	if(!res)
		critical_error("Error", "load commit error", "");
	if(!res.get())
		critical_error("Error", "load commit error2", "");
	if(!model_apply(true))
		critical_error("Error", "model apply error", "");
	_prelock_model.end_write_later();
	update_state(StateMask::Model);
}
gapr::future<bool> Engine::find_seed_pos() {
	// XXX softlock???
	// ***find candidate[cli] && ***soft lock[srv]
	// lock_many[**srv](candidate[**cli]...)
	// 
	gapr::edge_model::reader reader{_model};
	if(auto nid_next=get_seed_pos_random(reader)) {
		auto pos=reader.to_position(nid_next);
		if(pos.edge) {
			auto& e=reader.edges().at(pos.edge);
			jumpToPosition(Position{pos.edge, pos.index, e.points[pos.index/128]}, reader);
		} else {
			auto& v=reader.vertices().at(nid_next);
			jumpToPosition(Position{nid_next, v.attr.data()}, reader);
		}
		target_changed(Position{}, reader);
		apply_changes();
		update_state(0);
		startDownloadCube();

		_prelock_model.begin_write_later();
		gapr::fiber fib{_io_ctx.get_executor(), [this,bbox=_filter_bbox](gapr::fiber_ctx& ctx) mutable {
			for(unsigned int i=0; i<3; i++) {
				bbox[i]-=30;
				bbox[3+i]+=30;
			}
			return model_filter(ctx, bbox);
		}};
		return fib.get_future();
	}
	return {};
}
void Engine::present_cube(gapr::likely<bool>&& res) {
	if(!res)
		critical_error("Error", "set filter error", "");
	if(!res.get())
		critical_error("Error", "set filter error2", "");
	if(!model_apply(true))
		critical_error("Error", "model apply error", "");
	_prelock_model.end_write_later();
	update_state(StateMask::Model);
	_prog_state=PROG_READY;
	call_java(22, "1001");
}

bool Engine::check_extend() {
	// cur_conn
	if(!_prelock_path.can_write_later())
		return false;
	if(!_prelock_model.can_write_later())
		return false;
	if(_path.nodes.size()<2)
		return false;
	return true;
}

gapr::future<std::pair<Engine::SubmitRes, std::string>> Engine::start_extend() {
	if(!check_extend())
		return {};
	_prelock_path.begin_write_later();
	_prelock_model.begin_write_later();
	update_state(StateMask::Model|StateMask::Path);
	{
		gapr::link_id left{_path.left};
		unsigned int off=_path.nodes.size()-2;
		if(!left || !left.on_node())
			off+=1;
		gapr::link_id right{_path.right};
		if(!right || !right.on_node())
			_cur_fix=FixPos{off};
		else
			_cur_fix=FixPos{right.nodes[0]};
		_tgt_fix=FixPos{nullptr};
	}

	gapr::fiber fib{_io_ctx.get_executor(), [this](gapr::fiber_ctx& ctx) {
		return submit_commit(ctx, gapr::delta_add_edge_{_path});
	}};
	return fib.get_future();
}
gapr::future<std::pair<Engine::SubmitRes, std::string>> Engine::start_branch() {
	if(!check_extend())
		return {};
	_prelock_path.begin_write_later();
	_prelock_model.begin_write_later();
	update_state(StateMask::Model|StateMask::Path);
	{
		gapr::link_id left{_path.left};
		if(!left || !left.on_node()) {
			_cur_fix=FixPos{0u};
		} else {
			_cur_fix=FixPos{left.nodes[0]};
		}
		_tgt_fix=FixPos{nullptr};
	}

	gapr::fiber fib{_io_ctx.get_executor(), [this](gapr::fiber_ctx& ctx) {
		return submit_commit(ctx, gapr::delta_add_edge_{_path});
	}};
	return fib.get_future();
}
void Engine::finish_extend(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
	if(!res)
		critical_error("Error", "load commit error", "");
	if(!model_apply(true))
		critical_error("Error", "model apply error", "");
	switch(res.get().first) {
		case SubmitRes::Deny:
			_path.nodes.clear();
			set_path_data({});
			if(!res.get().second.empty())
				critical_error("Error", res.get().second, "");
			break;
		case SubmitRes::Accept:
			_path.nodes.clear();
			set_path_data({});
			break;
		case SubmitRes::Retry:
			_path.nodes.clear();
			set_path_data({});
			break;
	}
	apply_changes();
	_prelock_path.end_write_later();
	_prelock_model.end_write_later();
	update_state(StateMask::Model|StateMask::Path);
}

bool Engine::check_refresh() {
	// cur_conn
	if(!_prelock_model.can_write_later())
		return false;
	return true;
}

bool Engine::do_refresh(gapr::fiber_ctx& ctx) {
	auto msg=_cur_conn;
	gapr::fiber fib{ctx.get_executor(), api.select(msg, _args->group)};
	auto upd=std::move(fib).async_wait(gapr::yield{ctx});

	if(!load_commits(ctx, _cur_conn, upd.commit_num))
		return false;
	return model_filter(ctx);
}
gapr::future<bool> Engine::start_refresh() {
	if(!check_refresh())
		return {};
	_prelock_model.begin_write_later();
	update_state(StateMask::Model);

	gapr::fiber fib{_io_ctx.get_executor(), [this](gapr::fiber_ctx& ctx) {
		return do_refresh(ctx);
	}};
	return fib.get_future();
}
void Engine::finish_refresh(gapr::likely<bool>&& res) {
	if(!res)
		critical_error("Error", "load commit error", "");
	if(!res.get())
		critical_error("Error", "load commit error2", "");
	if(!model_apply(true))
		critical_error("Error", "model apply error", "");
	apply_changes();
	_prelock_model.end_write_later();
	update_state(StateMask::Model|StateMask::Path);
}

gapr::node_id Engine::get_seed_pos_random_edg(gapr::edge_model::view model) {
	unsigned int idx0=rng()%model.edges().size()+1;
	unsigned int idx=0;
	double dist{INFINITY};
	gapr::node_id n{};
	for(auto& [eid, edg]: model.edges()) {
		idx++;
		gapr::node_id nn{};
		for(std::size_t i=0; i<edg.nodes.size(); i++) {
			gapr::node_attr a{edg.points[i]};
			if(!a.misc.coverage()) {
				nn=edg.nodes[i];
				break;
			}
		}
		if(!nn)
			continue;
		auto d=std::abs(idx0+0.1-idx);
		if(d<dist) {
			dist=d;
			n=nn;
		}
	}
	gapr::print(1, "next pos edg: ", n.data);
	return n;
}
gapr::node_id Engine::get_seed_pos_random(gapr::edge_model::view model) {
	if(auto n=get_seed_pos_random_edg(model); n)
		return n;
	unsigned int idx0=rng()%model.vertices().size()+1;
	unsigned int idx=0;
	double dist{INFINITY};
	gapr::node_id n{};
	for(auto& [vid, vert]: model.vertices()) {
		idx++;
		if(vert.edges.size()>=2)
			continue;
		if(model.props().find(gapr::prop_id{vid, "state"})!=model.props().end())
			continue;
		auto d=std::abs(idx0+0.1-idx);
		if(d<dist) {
			dist=d;
			n=vid;
		}
	}
	gapr::print(2, "random seed ", n.data);
	return n;
}
bool Engine::filter_next_pos2(gapr::node_attr pos) const noexcept {
	for(unsigned int i=0; i<3; i++) {
		auto x=pos.pos(i);
		if(x<=_filter_bbox[i]+3)
			return false;
		if(x>=_filter_bbox[3+i]-3)
			return false;
	}
	return true;
}
bool Engine::filter_next_pos(gapr::node_attr pos) const noexcept {
	for(unsigned int i=0; i<3; i++) {
		auto x=pos.pos(i);
		if(x<=_filter_bbox[i]+6)
			return false;
		if(x>=_filter_bbox[3+i]-6)
			return false;
	}
	return true;
}
gapr::node_id Engine::get_next_pos(gapr::edge_model::view model, const std::unordered_set<gapr::node_id>& mask, gapr::node_attr pos) {
	double dist{INFINITY};
	gapr::node_id n{};
	for(auto& [vid, vert]: model.vertices()) {
		if(mask.find(vid)!=mask.end())
			continue;
		if(vert.edges.size()>=2)
			continue;
		if(model.props().find(gapr::prop_id{vid, "state"})!=model.props().end())
			continue;
		if(!filter_next_pos(vert.attr))
			continue;
		if(_next_mask.find(vid)!=_next_mask.end())
			continue;
		auto d=pos.dist_to(vert.attr);
		if(d<dist) {
			dist=d;
			n=vid;
		}
	}
	gapr::print("nearest next ", n.data);
	return n;
}
gapr::node_id Engine::get_next_pos_edg(gapr::edge_model::view model, gapr::node_id cur, bool mask_cur, gapr::node_attr::data_type _pos) {
	double dist{INFINITY};
	gapr::node_id n{};
	gapr::node_attr pos{_pos};
	for(auto& [eid, edg]: model.edges()) {
		gapr::node_id nn{};
		for(std::size_t i=0; i<edg.nodes.size(); i++) {
			gapr::node_attr a{edg.points[i]};
			if(mask_cur && cur==edg.nodes[i])
				continue;
			if(!filter_next_pos(a))
				continue;
			if(_next_mask.find(edg.nodes[i])!=_next_mask.end())
				continue;
			if(!a.misc.coverage()) {
				nn=edg.nodes[i];
				auto d=pos.dist_to(a);
				if(d<dist) {
					dist=d;
					n=nn;
				}
				break;
			}
		}
	}
	return n;
}
gapr::node_id Engine::get_next_pos(gapr::edge_model::view model, gapr::node_id cur, bool mask_cur, gapr::node_attr::data_type pos) {
	return find_next_node(model, cur, gapr::node_attr{pos}, _tier2<=gapr::tier::annotator, _next_mask, _filter_bbox);
	if(auto n=get_next_pos_edg(model, cur, mask_cur, pos); n)
		return n;
	gapr::print("cur: ", cur.data);
	std::unordered_set<gapr::node_id> mask;
	if(cur) {
		std::deque<gapr::node_id> todo;
		auto cur_vert=&model.vertices().at(cur);
		while(!mask_cur) {
			if(cur_vert->edges.size()>=2)
				break;
			if(model.props().find(gapr::prop_id{cur, "state"})!=model.props().end())
				break;
			if(!filter_next_pos(cur_vert->attr))
				break;
			if(_next_mask.find(cur)!=_next_mask.end())
				break;
			return cur;
		}
		mask.insert(cur);
		do {
			for(auto p: cur_vert->edges) {
				auto& edg=model.edges().at(p.first);
				auto next=p.second?edg.left:edg.right;
				auto [it, ins]=mask.emplace(next);
				if(ins)
					todo.push_back(next);
			}
			if(todo.empty())
				break;
			cur=todo.front();
			gapr::print("cur: ", cur.data);
			todo.pop_front();
			auto cur_vert=&model.vertices().at(cur);
			while(true) {
				if(cur_vert->edges.size()>=2)
					break;
				if(model.props().find(gapr::prop_id{cur, "state"})!=model.props().end())
					break;
				if(!filter_next_pos(cur_vert->attr))
					break;
				if(_next_mask.find(cur)!=_next_mask.end())
					break;
				return cur;
			}
		} while(true);
	}
	return get_next_pos(model, mask, gapr::node_attr{pos});
}

bool Engine::check_terminal() {
	// cur_conn
	if(!_prelock_model.can_write_later())
		return false;
	return true;
}
gapr::future<std::pair<Engine::SubmitRes, std::string>> Engine::start_terminal() {
	if(!check_terminal())
		return {};

	gapr::node_id nid{};
	bool mask_cur{false};
	gapr::edge_model::reader reader{_model};
	do {
		if(_cur_pos.edge) {
			auto& edg=reader.edges().at(_cur_pos.edge);
			if(_cur_pos.index/(128/2)<edg.points.size())
				nid=edg.left;
			else
				nid=edg.right;
			break;
		}
		nid=_cur_pos.vertex;
		if(!nid)
			break;
		mask_cur=true;
		auto vert=&reader.vertices().at(nid);
		if(vert->edges.size()>=2)
			break;
		auto it_state=reader.props().find(gapr::prop_id{nid, "state"});
		if(it_state!=reader.props().end())
			break;

		_prelock_model.begin_write_later();
		update_state(StateMask::Model);

		if(auto next_nid=get_next_pos(reader, nid, mask_cur, _cur_pos.point)) {
			_cur_fix=FixPos{next_nid};
			_tgt_fix=FixPos{nullptr};
		}
		gapr::fiber fib{_io_ctx.get_executor(), [this,nid](gapr::fiber_ctx& ctx) {
			gapr::delta_add_prop_ delta;
			delta.link[0]=nid.data;
			delta.link[1]=0;
			delta.node=_cur_pos.point;
			delta.prop="state=end";
			return submit_commit(ctx, std::move(delta));
		}};
		return fib.get_future();
	} while(false);

	if(auto nid_next=get_next_pos(reader, nid, mask_cur, _cur_pos.point)) {
		gapr::print(1, "nid_next: ", nid_next.data);
		auto pos=reader.to_position(nid_next);
		if(pos.edge) {
			auto& e=reader.edges().at(pos.edge);
			jumpToPosition(Position{pos.edge, pos.index, e.points[pos.index/128]}, reader);
		} else {
			auto& v=reader.vertices().at(nid_next);
			jumpToPosition(Position{nid_next, v.attr.data()}, reader);
		}
		target_changed(Position{}, reader);
		auto sel_nodes=[this](auto&& nodes_sel) {
			set_selection(std::move(nodes_sel));
		};
		auto pick_tgt=[this,&reader](const auto& pos) {
			target_changed(pos, reader);
		};
		struct boundary_checker {
			gapr::affine_xform xform;
			std::array<unsigned int, 3> offset;
			std::array<unsigned int, 3> size;
			bool valid;
			boundary_checker(const gapr::cube_info& cubeinfo, const std::array<unsigned int, 3>& offset, const gapr::cube& cube):
				xform{cubeinfo.xform}, offset{offset}, size{0,0,0}, valid{false}
			{
				if(cube) {
					auto cube_view=cube.view<void>();
					size=cube_view.sizes();
					valid=true;
				}
			}
			bool operator()(const gapr::vec3<double>& pos) {
				if(!valid)
					return true;
				auto off=xform.to_offset_f(pos);
				for(unsigned int i=0; i<off.size(); ++i) {
					auto oi=off[i]-offset[i];
					if(oi<0)
						return false;
					if(oi>size[i])
						return false;
				}
				return true;
			}
		};
		boundary_checker chk_rng{_cube_infos[_closeup_ch-1], _closeup_offset, _closeup_cube};
		sel_proofread_nodes(reader, _cur_pos, pick_tgt, sel_nodes, chk_rng, 30);
		apply_changes();
		update_state(0);
		//XXX startDownloadCube();
	} else {
		on_goto_next_cube_triggered();
	}
	return {};
}
gapr::future<std::pair<Engine::SubmitRes, std::string>> Engine::start_end_as(std::string&& val) {
	// XXX
	if(!check_terminal())
		return {};

	gapr::node_id nid{};
	bool mask_cur{false};
	gapr::edge_model::reader reader{_model};
	do {
		if(_cur_pos.edge) {
			auto& edg=reader.edges().at(_cur_pos.edge);
			if(_cur_pos.index/(128/2)<edg.points.size())
				nid=edg.left;
			else
				nid=edg.right;
			break;
		}
		nid=_cur_pos.vertex;
		if(!nid)
			break;
		mask_cur=true;
		auto vert=&reader.vertices().at(nid);
		if(vert->edges.size()>=2)
			break;
		auto it_state=reader.props().find(gapr::prop_id{nid, "state"});
		if(it_state!=reader.props().end())
			break;

		_prelock_model.begin_write_later();
		update_state(StateMask::Model);

		if(auto next_nid=get_next_pos(reader, nid, mask_cur, _cur_pos.point)) {
			_cur_fix=FixPos{next_nid};
			_tgt_fix=FixPos{nullptr};
		}
		gapr::fiber fib{_io_ctx.get_executor(), [this,nid,val=std::move(val)](gapr::fiber_ctx& ctx) {
			gapr::delta_add_prop_ delta;
			delta.link[0]=nid.data;
			delta.link[1]=0;
			delta.node=_cur_pos.point;
			delta.prop="state="+val;
			return submit_commit(ctx, std::move(delta));
		}};
		return fib.get_future();
	} while(false);

	if(auto nid_next=get_next_pos(reader, nid, mask_cur, _cur_pos.point)) {
		auto& v=reader.vertices().at(nid_next);
		jumpToPosition(Position{nid_next, v.attr.data()}, reader);
		target_changed(Position{}, reader);
		apply_changes();
		update_state(0);
		//XXX startDownloadCube();
	} else {
		on_goto_next_cube_triggered();
	}
	return {};
}
void Engine::finish_terminal(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
	if(!res)
		critical_error("Error", "load commit error", "");
	if(!model_apply(true))
		critical_error("Error", "model apply error", "");
	switch(res.get().first) {
		case SubmitRes::Deny:
			if(!res.get().second.empty())
				critical_error("Error", res.get().second, "");
			break;
		case SubmitRes::Accept:
			break;
		case SubmitRes::Retry:
			break;
	}
	apply_changes();
	_prelock_model.end_write_later();
	update_state(StateMask::Model);
}
Engine::sub_edge Engine::get_sub_edge() {
	// XXX move to edge_model
	gapr::edge_model::reader reader{_model};
	edge_model::edge_id id{0};
	do {
		if(_cur_pos.edge) {
			if(_tgt_pos.edge) {
				if(_cur_pos.edge!=_tgt_pos.edge)
					break;
				id=_cur_pos.edge;
			} else if(_tgt_pos.vertex) {
				id=_cur_pos.edge;
			} else {
				break;
			}
		} else if(_cur_pos.vertex) {
			if(_tgt_pos.edge) {
				id=_tgt_pos.edge;
			} else if(_tgt_pos.vertex) {
				auto& vert1=reader.vertices().at(_cur_pos.vertex);
				auto& vert2=reader.vertices().at(_tgt_pos.vertex);
				unsigned int hits=0;
				for(auto [eid1, dir1]: vert1.edges) {
					for(auto [eid2, dir2]: vert2.edges) {
						if(eid1==eid2) {
							id=eid1;
							hits++;
						}
					}
				}
				if(hits!=1) {
					id=0;
					break;
				}
			} else {
				break;
			}
		} else {
			break;
		}
	} while(false);
	if(id==0)
		return sub_edge{0, 0, 0};
	uint32_t idx0{0}, idx1{0};
	auto& edg=reader.edges().at(id);
	if(_cur_pos.edge) {
		idx0=_cur_pos.index/128;
	} else if(_cur_pos.vertex) {
		if(_cur_pos.vertex==edg.left) {
			idx0=0;
		} else if(_cur_pos.vertex==edg.right) {
			idx0=edg.nodes.size()-1;
		} else {
			return sub_edge{0, 0, 0};
		}
	} else {
		assert(0);
	}
	if(_tgt_pos.edge) {
		idx1=_tgt_pos.index/128;
	} else if(_tgt_pos.vertex) {
		if(_tgt_pos.vertex==edg.left) {
			idx1=0;
		} else if(_tgt_pos.vertex==edg.right) {
			idx1=edg.nodes.size()-1;
		} else {
			return sub_edge{0, 0, 0};
		}
	} else {
		assert(0);
	}
	return sub_edge{id, idx0, idx1};
}
bool Engine::check_delete() {
	// cur_conn
	if(!_prelock_model.can_write_later())
		return false;
	if(!get_sub_edge().edge)
		return false;
	return true;
}
gapr::future<std::pair<Engine::SubmitRes, std::string>> Engine::start_delete() {
	//XXX delete vertex???
	//vv 0/not/delv 1dele
	//ev dele
	//ee 0/not/breake 1+/dele
	//e0 not/breake/dele
	//0v not/delv
	//0e not/breake/dele
	if(!check_delete())
		return {};

	gapr::edge_model::reader reader{_model};
	auto sub_edge=get_sub_edge();
	// XXX
	if(sub_edge.index0==sub_edge.index1)
		return {};
	std::vector<std::pair<node_id::data_type, std::string>> props;
	std::vector<node_id::data_type> nodes;
	bool rm_left{false}, rm_right{false};
	if(_cur_pos.edge) {
	} else if(_cur_pos.vertex) {
		auto& vert=reader.vertices().at(_cur_pos.vertex);
		if(vert.edges.size()<2)
			rm_left=true;
	} else {
	}
	if(_tgt_pos.edge) {
	} else if(_tgt_pos.vertex) {
		auto& vert=reader.vertices().at(_tgt_pos.vertex);
		if(vert.edges.size()<2)
			rm_right=true;
	} else {
	}
	if(rm_left)
		nodes.push_back(0);
	auto& edg=reader.edges().at(sub_edge.edge);
	gapr::print("del edge: ", sub_edge.edge, ':', sub_edge.index0, '.', sub_edge.index1);
	uint32_t idx=sub_edge.index0;
	do {
		nodes.push_back(edg.nodes[idx].data);
		if(idx==sub_edge.index1)
			break;
		if(sub_edge.index1>sub_edge.index0)
			idx++;
		else
			idx--;
	} while(true);
	if(rm_left)
		nodes[0]=nodes[1];
	if(rm_right) {
		auto last=nodes.back();
		nodes.push_back(last);
	}
	std::unordered_set<gapr::node_id> nodes_del;
	for(std::size_t i=1; i+1<nodes.size(); i++) {
		nodes_del.emplace(nodes[i]);
	}
	for(auto& [id, val]: reader.props()) {
		if(nodes_del.find(id.node)!=nodes_del.end()) {
			props.push_back(gapr::prop_id{id}.data());
		}
	}
	std::sort(props.begin(), props.end());

	_prelock_model.begin_write_later();
	update_state(StateMask::Model);

	gapr::fiber fib{_io_ctx.get_executor(), [this,nodes=std::move(nodes),props=std::move(props)](gapr::fiber_ctx& ctx) {
		gapr::delta_del_patch_ delta;
		delta.props=std::move(props);
		delta.nodes=std::move(nodes);
		for(auto& p: delta.props)
			gapr::print("del prop: ", p.first, ':', p.second);
		for(auto& n: delta.nodes)
			gapr::print("del node: ", n);
		return submit_commit(ctx, std::move(delta));
	}};
	return fib.get_future();
}
void Engine::finish_delete(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
	if(!res)
		critical_error("Error", "load commit error", "");
	if(!model_apply(true))
		critical_error("Error", "model apply error", "");
	switch(res.get().first) {
		case SubmitRes::Deny:
			if(!res.get().second.empty())
				critical_error("Error", res.get().second, "");
			break;
		case SubmitRes::Accept:
			break;
		case SubmitRes::Retry:
			break;
	}
	apply_changes();
	_prelock_model.end_write_later();
	update_state(StateMask::Model);
}
void Engine::jump_next_node() {
	// check
	// cur_conn???
	//???if(!_prelock_model.can_write_later())
	gapr::node_id nid{};
	gapr::edge_model::reader reader{_model};
	do {
		if(_cur_pos.edge) {
			auto& edg=reader.edges().at(_cur_pos.edge);
			_next_mask.emplace(edg.nodes[_cur_pos.index/128]);
			if(_cur_pos.index/(128/2)<edg.points.size())
				nid=edg.left;
			else
				nid=edg.right;
			break;
		}
		nid=_cur_pos.vertex;
		if(!nid)
			break;
		_next_mask.emplace(nid);
	} while(false);

	if(auto nid_next=get_next_pos(reader, nid, false, _cur_pos.point)) {
		auto pos=reader.to_position(nid_next);
		if(pos.edge) {
			auto& e=reader.edges().at(pos.edge);
			jumpToPosition(Position{pos.edge, pos.index, e.points[pos.index/128]}, reader);
		} else {
			auto& v=reader.vertices().at(nid_next);
			jumpToPosition(Position{nid_next, v.attr.data()}, reader);
		}
		target_changed(Position{}, reader);
		apply_changes();
		update_state(0);
	} else {
		// XXX enabled???
		on_goto_next_cube_triggered();
	}
}
void Engine::jump_next_cube() {
}

bool Engine::check_neuron_create() {
	// cur_conn
	if(!_prelock_model.can_write_later())
		return false;
	if(!_tgt_pos.valid())
		return false;
	return true;
}

gapr::future<std::pair<Engine::SubmitRes, std::string>> Engine::start_neuron_create(std::string&& name) {
	if(!check_neuron_create())
		return {};


	gapr::node_id nid{};
	gapr::edge_model::reader reader{_model};
	if(_tgt_pos.edge) {
		auto& edg=reader.edges().at(_tgt_pos.edge);
		nid=edg.nodes[_tgt_pos.index/128];
	} else {
		nid=_tgt_pos.vertex;
	}
	auto it_root=reader.props().find(gapr::prop_id{nid, "root"});
	if(it_root!=reader.props().end())
		return {};
	_prelock_model.begin_write_later();
	update_state(StateMask::Model);
	//???list_ptr: point to created neuron
	gapr::fiber fib{_io_ctx.get_executor(), [this,nid,name=std::move(name)](gapr::fiber_ctx& ctx) {
		gapr::delta_add_prop_ delta;
		delta.link[0]=nid.data;
		delta.link[1]=0;
		delta.node=_tgt_pos.point;
		delta.prop="root";
		if(!name.empty()) {
			delta.prop.push_back('=');
			delta.prop+=name;
		}
		//gapr::print("neuron create: ", delta.type);
		return submit_commit(ctx, std::move(delta));
	}};
	return fib.get_future();
}

void Engine::finish_neuron_create(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
	if(!res)
		critical_error("Error", "load commit error", "");
	if(!model_apply(true))
		critical_error("Error", "model apply error", "");
	switch(res.get().first) {
		case SubmitRes::Deny:
			if(!res.get().second.empty())
				critical_error("Error", res.get().second, "");
			break;
		case SubmitRes::Accept:
			break;
		case SubmitRes::Retry:
			break;
	}
	apply_changes();
	_prelock_model.end_write_later();
	update_state(StateMask::Model);
}

bool Engine::check_report_error() {
	// cur_conn
	if(!_prelock_model.can_write_later())
		return false;
	if(!_tgt_pos.valid())
		return false;
	return true;
}

gapr::future<std::pair<Engine::SubmitRes, std::string>> Engine::start_report_error(std::string&& name) {
	if(!check_report_error())
		return {};


	gapr::node_id nid{};
	gapr::edge_model::reader reader{_model};
	if(_tgt_pos.edge) {
		auto& edg=reader.edges().at(_tgt_pos.edge);
		nid=edg.nodes[_tgt_pos.index/128];
	} else {
		nid=_tgt_pos.vertex;
	}
	auto it_root=reader.props().find(gapr::prop_id{nid, "error"});
	if(it_root!=reader.props().end())
		return {};
	_prelock_model.begin_write_later();
	update_state(StateMask::Model);
	//???list_ptr: point to created neuron
	gapr::fiber fib{_io_ctx.get_executor(), [this,nid,name=std::move(name)](gapr::fiber_ctx& ctx) {
		gapr::delta_add_prop_ delta;
		delta.link[0]=nid.data;
		delta.link[1]=0;
		delta.node=_tgt_pos.point;
		delta.prop="error";
		if(!name.empty()) {
			delta.prop.push_back('=');
			delta.prop+=name;
		}
		return submit_commit(ctx, std::move(delta));
	}};
	return fib.get_future();
}

void Engine::finish_report_error(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
	if(!res)
		critical_error("Error", "load commit error", "");
	if(!model_apply(true))
		critical_error("Error", "model apply error", "");
	switch(res.get().first) {
		case SubmitRes::Deny:
			if(!res.get().second.empty())
				critical_error("Error", res.get().second, "");
			break;
		case SubmitRes::Accept:
			break;
		case SubmitRes::Retry:
			break;
	}
	apply_changes();
	_prelock_model.end_write_later();
	update_state(StateMask::Model);
}


bool Engine::check_examine() {
	// cur_conn
	if(!_prelock_model.can_write_later())
		return false;
	if(get_sub_edge().edge)
		return true;
	if(!_nodes_sel.empty())
		return true;
	return false;
}

	[[maybe_unused]] static bool check_vertex_raised(gapr::edge_model::reader& reader, gapr::tier tier, const gapr::edge_model::vertex& vert) {
		if(reader.raised() && tier>=gapr::tier::annotator) {
			if(!vert.raised)
				return false;
		}
		return true;
	}
	static bool check_edge_raised(gapr::edge_model::reader& reader, gapr::tier tier, const gapr::edge_model::edge& edg) {
		if(reader.raised() && tier>=gapr::tier::annotator) {
			if(!edg.raised)
				return false;
		}
		return true;
	}
	[[maybe_unused]] static bool check_vert_raised(gapr::edge_model::reader& reader, gapr::tier tier, gapr::edge_model::vertex_id vert_id) {
		if(reader.raised() && tier>=gapr::tier::annotator) {
			if(vert_id==gapr::node_id{})
				return false;
			auto& vert=reader.vertices().at(vert_id);
			if(!vert.raised)
				return false;
		}
		return true;
	}
	static bool check_node_raised(gapr::edge_model::reader& reader, gapr::tier tier, gapr::node_id node) {
		if(reader.raised() && tier>=gapr::tier::annotator) {
			assert(node!=gapr::node_id{});
			auto pos=reader.nodes().at(node);
			if(pos.edge) {
				auto& edg=reader.edges().at(pos.edge);
				if(!edg.raised)
					return false;
			} else {
				auto& vert=reader.vertices().at(pos.vertex);
				if(!vert.raised)
					return false;
			}
		}
		return true;
	}
gapr::future<std::pair<Engine::SubmitRes, std::string>> Engine::start_examine() {
	if(!check_examine())
		return {};

	gapr::edge_model::reader reader{_model};
	auto sub_edge=get_sub_edge();
	std::vector<node_id::data_type> nodes;
	FixPos fix_pos{};
	if(sub_edge.edge) {
	auto& edg=reader.edges().at(sub_edge.edge);
	fix_pos=FixPos{edg.nodes[sub_edge.index1]};
	uint32_t idx=sub_edge.index0;
	do {
		gapr::node_attr pt{edg.points[idx]};
		if(!pt.misc.coverage())
			nodes.push_back(edg.nodes[idx].data);
		if(idx==sub_edge.index1)
			break;
		if(sub_edge.index1>sub_edge.index0)
			idx++;
		else
			idx--;
	} while(true);
		if(!check_edge_raised(reader, _tier2, edg))
			nodes.clear();
	}
	if(auto& sel=_nodes_sel; !sel.empty()) {
		fix_pos=FixPos{};
		nodes.clear();
		for(auto& [n, attr]: sel) {
			if(!attr.misc.coverage())
				nodes.push_back(n.data);
		}
		if(!nodes.empty() && !check_node_raised(reader, _tier2, gapr::node_id{nodes[0]}))
			nodes.clear();
	}
	if(nodes.empty()) {
		if(fix_pos) {
		jumpToPosition(_tgt_pos, reader);
		target_changed(Position{}, reader);
		}
		apply_changes();
		update_state(0);
		return {};
	}

	_prelock_model.begin_write_later();
	update_state(StateMask::Model|StateMask::Path);
	{
		_cur_fix=fix_pos;
		_tgt_fix=FixPos{nullptr};
	}

	gapr::fiber fib{_io_ctx.get_executor(), [this,nodes=std::move(nodes)](gapr::fiber_ctx& ctx) {
		gapr::delta_proofread_ delta;
		delta.nodes=std::move(nodes);
		return submit_commit(ctx, std::move(delta));
	}};
	return fib.get_future();
}
void Engine::finish_examine(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
	if(!res)
		critical_error("Error", "load commit error", "");
	if(!model_apply(true))
		critical_error("Error", "model apply error", "");
	switch(res.get().first) {
		case SubmitRes::Deny:
			_path.nodes.clear();
			set_path_data({});
			if(!res.get().second.empty())
				critical_error("Error", res.get().second, "");
			break;
		case SubmitRes::Accept:
			_path.nodes.clear();
			set_path_data({});
			clear_selection();
			break;
		case SubmitRes::Retry:
			set_path_data({});
			break;
	}
	apply_changes();
	_prelock_model.end_write_later();
	update_state(StateMask::Model|StateMask::Path);
}
void Engine::setupUi() {
#if 000
	_dlg_display=new QDialog{&window};
	_ui_display.setupUi(_dlg_display);

	_ui.setupUi(&window);

	_popup_canvas=new QMenu{QStringLiteral("&Canvas"), &window};
	_popup_canvas->addAction(_ui.goto_target);
	_popup_canvas->addAction(_ui.pick_current);
	_popup_canvas->addSeparator();
	_popup_canvas->addAction(_ui.neuron_create);
	_popup_canvas->addSeparator();
	_popup_canvas->addAction(_ui.report_error);
	//_popup_canvas->addSeparator();
	//_popup_canvas->addAction(actions[EDIT_ABORT]);

	int scale_factor;
	std::array<unsigned int, 2> slice_params;
	{
		QSettings settings{&window};
		settings.beginGroup(QStringLiteral("fix"));
		if(auto v=settings.value(QStringLiteral("scale-factor")); v.type()==QVariant::Int) {
			scale_factor=v.value<int>();
		} else {
			scale_factor=_ui.canvas->devicePixelRatio()-1;
		}
		if(auto v=settings.value(QStringLiteral("slice-params")); v.type()==QVariant::Point) {
			auto p=v.value<QPoint>();
			auto x=p.x();
			auto y=p.y();
			if(y>MAX_SLICES)
				y=MAX_SLICES;
			else if(y<0)
				y=0;
			if(x<0)
				x=0;
			else if(x>y)
				x=y;
			slice_params[0]=x;
			slice_params[1]=y;
		} else {
			slice_params[0]=slice_params[1]=MAX_SLICES;
		}
		settings.endGroup();
	}

	{
		auto scale=scale_factor<0?1.0/(1-scale_factor):scale_factor+1;
		int k{-1};
		double match{INFINITY};
		QSignalBlocker _blk{_ui_display.select_scale};
		for(auto f: SCALE_FACTORS) {
			double ff;
			QString name;
			if(f<0) {
				name=QStringLiteral("1/%1").arg(1-f);
				ff=1.0/(1-f);
			} else {
				name=QStringLiteral("%1").arg(1+f);
				ff=(1+f);
			}
			_ui_display.select_scale->addItem(name, QVariant{f});
			auto m=std::abs(std::log(ff/scale));
			if(m<match) {
				match=m;
				k=_ui_display.select_scale->count()-1;
				scale_factor=f;
			}
		}
		if(k!=-1)
			_ui_display.select_scale->setCurrentIndex(k);
		else
			_ui_display.select_scale->setEnabled(false);
	}
	{
		QSignalBlocker _blk0{_ui_display.total_slices};
		QSignalBlocker _blk1{_ui_display.shown_slices};
		_ui_display.total_slices->setMaximum(MAX_SLICES);
		_ui_display.total_slices->setMinimum(0);
		_ui_display.total_slices->setValue(slice_params[1]);
		_ui_display.shown_slices->setMaximum(MAX_SLICES);
		_ui_display.shown_slices->setMinimum(0);
		_ui_display.shown_slices->setValue(slice_params[0]);
	}
	_ui.canvas->set_scale_factor(scale_factor);
	_ui.canvas->set_slice_pars(slice_params);
	_ui.canvas->apply_changes();
#endif
}

#if 0
Window::Window():
	QMainWindow{nullptr},
	_priv{std::make_shared<PRIV>(*this)}
{
	/*! stage 0, window constructed */
	_priv->setupUi();
}

Window::~Window() {
	gapr::print("Window::~PRIV");
	auto _cube_builder=_priv->_cube_builder;
	if(_cube_builder) {
		if(_cube_builder->isRunning()) {
			gapr::print("stop load thread");
			_cube_builder->stop();
			if(!_cube_builder->wait()) {
				_cube_builder->terminate();
			}
		}
		delete _cube_builder;
	}
}
#endif


void Engine::critical_error(const std::string& err, const std::string& info, const std::string& detail) {
#if 000
	auto mbox=new QMessageBox{QMessageBox::Critical, QStringLiteral("Critical Error"), err, QMessageBox::Close, this};
	if(!info.isEmpty()) {
		gapr::print("set info");
		mbox->setInformativeText(info);
	}
	if(!detail.isEmpty()) {
		gapr::print("set detail");
		mbox->setDetailedText(detail);
	}
	mbox->setWindowModality(Qt::WindowModal);
	connect(mbox, &QDialog::finished, this, &QObject::deleteLater);
	mbox->open();
#endif
}
#if 000
void Engine::show_retry_dlg(const QString& err, const QString& info, const QString& detail) {
	auto mbox=new QMessageBox{QMessageBox::Warning, QStringLiteral("Error"), err, QMessageBox::Close|QMessageBox::Retry, this};
	if(!info.isEmpty())
		mbox->setInformativeText(info);
	if(!detail.isEmpty())
		mbox->setDetailedText(detail);
	mbox->setWindowModality(Qt::WindowModal);
	connect(mbox, &QDialog::finished, this, &Window::show_retry_dlg_cb);
	mbox->open();
}

void Engine::critical_error_cb(int result) {
	deleteLater();
}

void Engine::ask_password(const QString& str, const QString& err) {
	auto dlg=std::make_unique<gapr::PasswordDialog>(err.isEmpty()?QStringLiteral("Authentication Required"):QStringLiteral("Login Error"), str, err, this);
	dlg->setWindowModality(Qt::WindowModal);
	connect(dlg.get(), &QDialog::finished, this, &Window::ask_password_cb);
	dlg->show();
	_priv->_dlg_pw=std::move(dlg);
}
void Engine::ask_password_cb(int result) {
	auto dlg=_priv->_dlg_pw.release();
	dlg->deleteLater();
	if(result!=QDialog::Accepted)
		return deleteLater();

	auto pw=dlg->get_password();
	ba::post(_io_ctx, [this,
			pw=pw.toStdString()
	]() mutable { _priv->got_passwd(std::move(pw)); });
}

void Window::show_message(const QString& str) {
	_priv->_ui.statusbar->showMessage(str, 2000);
}
void Window::show_retry_dlg_cb(int result) {
	if(result!=QMessageBox::Retry) {
		deleteLater();
		return;
	}
	ba::post(_io_ctx, [this]() { _priv->connect(); });
}
#endif

void Engine::on_view_config_triggered() {
#if 000
	//QDialog.parent=this;
	//XXX directly connect
	auto dlg=_priv->_dlg_display;
	if(dlg)
		dlg->show();
#endif
}

void Engine::update_state(StateMask::base_type chg) {
	if(chg&(StateMask::Model|StateMask::Path|StateMask::CurPos|StateMask::TgtPos|StateMask::ViewMode|StateMask::Cube)) {
		//_ui.tracing_connect->setEnabled(check_connect());
	}
}


void Engine::enter_stage2() {
	/*! after login */
	// XXX allow selecting repo, and changing passwd
}


void Engine::on_xfunc_closeup_changed(double low, double up) {
	xfunc_closeup_changed(low, up);
}

void Engine::on_file_open_triggered() {
#if 000
	assert(_priv->_ui.file_open->isEnabled());

	auto dlg=std::make_unique<gapr::login_dialog>(this);
	dlg->setWindowModality(Qt::WindowModal);
	connect(dlg.get(), &QDialog::finished, this, &Window::on_login_dialog_finished);
	dlg->show();
	_priv->_dlg_login=std::move(dlg);
	_priv->_ui.file_open->setEnabled(false);
#endif
}

void Engine::on_file_close_triggered() {
	//close();
}
void Engine::on_file_launch_triggered() {
	//_ui_ctx.show_start_window(this);
}
void Engine::on_file_options_triggered() {
	//_ui_ctx.show_options_dialog(*this);
}
void Engine::on_file_quit_triggered() {
	//_ui_ctx.request_quit();
}
void Engine::on_goto_target_triggered() {
	//not SessionState::Invalid: SessionState::LoadingCatalog: SessionState::Readonly:
	if(!_tgt_pos.valid())
		return;
	edge_model::reader reader{_model};
	jumpToPosition(_tgt_pos, reader);
	//_priv->updateActions();
	//_priv->startDownloadCube();
}
void Engine::on_pick_current_triggered() {
	//not SessionState::Invalid: SessionState::LoadingCatalog: SessionState::Readonly:
	if(!_cur_pos.valid())
		return;
	pick_position(_cur_pos);
	//priv->viewer->update();
	//priv->updateActions();
}
void Engine::on_goto_next_node_triggered() {
	jump_next_node();
}
void Engine::on_goto_next_cube_triggered() {
	// XXX select and update???
	call_java(21, "-1");
	auto fut=load_latest_commits();
	if(!fut)
		return;
	auto ex2=_ui_ctx.get_executor();
	std::move(fut).async_wait(ex2, [this](gapr::likely<bool>&& res) {
		latest_commits_loaded(std::move(res));
		call_java(22, "1001");
		auto fut=find_seed_pos();
		auto ex2=_ui_ctx.get_executor();
		if(fut)
			std::move(fut).async_wait(ex2, [this](gapr::likely<bool>&& res) {
				present_cube(std::move(res));
			});
	});
}

void Engine::on_neuron_create_triggered() {
	auto fut=start_neuron_create({});
	if(!fut)
		return;
	auto ex2=_ui_ctx.get_executor();
	std::move(fut).async_wait(ex2, [this](auto&& res) {
		finish_neuron_create(std::move(res));
	});
}
void Engine::on_report_error_triggered() {
	auto fut=start_report_error({});
	if(!fut)
		return;
	auto ex2=_ui_ctx.get_executor();
	std::move(fut).async_wait(ex2, [this](auto&& res) {
		finish_report_error(std::move(res));
	});
}

void Engine::on_view_slice_toggled(bool checked) {
	set_slice_mode(checked);
	apply_changes();
}
void Engine::on_view_data_only_toggled(bool checked) {
	set_data_only(checked);
	apply_changes();
}
void Engine::on_tracing_extend_triggered() {
	auto fut=start_extend();
	gapr::print("start_extend ", static_cast<bool>(fut));
	if(!fut)
		return;
	auto ex2=_ui_ctx.get_executor();
	std::move(fut).async_wait(ex2, [this](auto&& res) {
		gapr::print("finish_extend");
		finish_extend(std::move(res));
	});
}

void Engine::on_tracing_branch_triggered() {
	auto fut=start_branch();
	gapr::print("start_branch");
	if(!fut)
		return;
	auto ex2=_ui_ctx.get_executor();
	std::move(fut).async_wait(ex2, [this](auto&& res) {
		gapr::print("finish_branch");
		finish_extend(std::move(res));
	});
}
void Engine::on_tracing_end_triggered() {
	auto fut=start_terminal();
	if(!fut)
		return;
	auto ex2=_ui_ctx.get_executor();
	std::move(fut).async_wait(ex2, [this](auto&& res) {
		finish_terminal(std::move(res));
	});
}
void Engine::on_tracing_end_as_triggered() {
	if(!check_terminal())
		return;

#if 000
	auto dlg=std::make_unique<EndAsDialog>(this);
	dlg->setWindowModality(Qt::WindowModal);
	connect(dlg.get(), &QDialog::finished, this, &Window::on_end_as_dialog_finished);
	dlg->show();
	_priv->_dlg_end_as=std::move(dlg);
#endif
}
void Engine::on_tracing_delete_triggered() {
	auto fut=start_delete();
	if(!fut)
		return;
	auto ex2=_ui_ctx.get_executor();
	std::move(fut).async_wait(ex2, [this](auto&& res) {
		finish_delete(std::move(res));
	});
}
void Engine::on_tracing_examine_triggered() {
	auto fut=start_examine();
	if(!fut)
		return;
	auto ex2=_ui_ctx.get_executor();
	std::move(fut).async_wait(ex2, [this](auto&& res) {
		finish_examine(std::move(res));
	});
}

void Engine::on_end_as_dialog_finished(int result) {
#if 0
	auto dlg=_dlg_end_as.release();
	dlg->deleteLater();
	if(result!=QDialog::Accepted)
		return;
	auto st=dlg->_ui.state->text().toStdString();
	auto fut=start_end_as(std::move(st));
	if(!fut)
		return;
	auto ex2=_ui_ctx.get_executor();
	std::move(fut).async_wait(ex2, [](auto&& res) {
		finish_terminal(std::move(res));
	});
#endif
}
void Engine::on_end_as_dialog_finished2() {
	auto st=std::string{"Error"};
	auto fut=start_end_as(std::move(st));
	if(!fut)
		return;
	auto ex2=_ui_ctx.get_executor();
	std::move(fut).async_wait(ex2, [this](auto&& res) {
		finish_terminal(std::move(res));
	});
}
void Engine::on_help_manual_triggered() {
	//_ui_ctx.display_help(*this, "fix");
}
void Engine::on_help_about_triggered() {
#if 000
	//XXX
	const auto& icon=_ui.help_about->icon();

	for(auto s: icon.availableSizes())
		gapr::print("size: ", s.width(), 'x', s.height());

	//_ui_ctx.show_about_dialog(*this);
#endif
}
#if 000
void Window::closeEvent(QCloseEvent* event) {
	gapr::print("close");
	QMainWindow::closeEvent(event);
	deleteLater();
}
void Window::changeEvent(QEvent* event) {
	switch(event->type()) {
		case QEvent::LanguageChange:
			_priv->_ui_display.retranslateUi(_priv->_dlg_display);
			_priv->_ui.retranslateUi(this);
			break;
		default:
			break;
	}
	QMainWindow::changeEvent(event);
}
#endif
void Engine::on_canvas_pick_changed() {
	pick_position();
	//_priv->updateActions();
	//_priv->connectIfPossible();
	////
}
#if 0
void Engine::on_canvas_customContextMenuRequested(const QPoint& pos) {
	// XXX directly connect
	gapr::print("context menu");
	_priv->_popup_canvas->popup(_priv->_ui.canvas->mapToGlobal(pos));
}
#endif
void Engine::on_view_refresh_triggered() {
	//XXX _priv->startDownloadCube();
	//reload meshes

	auto fut=start_refresh();
	gapr::print("start_refresh");
	if(!fut)
		return;
	auto ex2=_ui_ctx.get_executor();
	std::move(fut).async_wait(ex2, [this](auto&& res) {
		gapr::print("finish_refresh");
		finish_refresh(std::move(res));
	});
}

void Engine::on_select_closeup_currentIndexChanged(int index) {
	return select_closeup_changed(index);
}
void Engine::on_select_scale_currentIndexChanged(int index) {
#if 000
	auto f=_priv->_ui_display.select_scale->itemData(index).value<int>();
	_priv->_ui.canvas->set_scale_factor(f);
	_priv->_ui.canvas->apply_changes();
#endif
}
void Engine::on_total_slices_valueChanged(int value) {
	unsigned int v1=value;
	auto v0=slice_pars()[0];
	if(v0>v1) {
		v0=v1;
		//QSignalBlocker _blk{_priv->_ui_display.shown_slices};
		//_ui_display.shown_slices->setValue(v0);
	}
	set_slice_pars({v0, v1});
	apply_changes();
}

void Engine::on_shown_slices_valueChanged(int value) {
	unsigned int v0=value;
	auto v1=slice_pars()[1];
	if(v1<v0) {
		v1=v0;
		//QSignalBlocker _blk{_priv->_ui_display.total_slices};
		//_priv->_ui_display.total_slices->setValue(v1);
	}
	set_slice_pars({v0, v1});
	apply_changes();
}
void Engine::on_quality_button_box_accepted() {
#if 000
	QSettings settings{this};
	settings.beginGroup(QStringLiteral("fix"));
	settings.setValue(QStringLiteral("scale-factor"), _priv->_ui_display.select_scale->currentData());
	auto x=_priv->_ui_display.shown_slices->value();
	auto y=_priv->_ui_display.total_slices->value();
	settings.setValue(QStringLiteral("slice-params"), QPoint{x, y});
	settings.endGroup();
#endif
}

#if 0
	Canvas& canvas;
	OpenGLFunctions funcs;
	bool funcs_ok{false};
	ViewerShared* viewerShared{nullptr};

	GLsizei fbo_width, fbo_height;
	GLsizei fbo_width_alloc, fbo_height_alloc;

	FramebufferOpaque fbo_opaque{funcs};
	FramebufferEdges fbo_edges{funcs};
	FramebufferCubes fbo_cubes{funcs};
	FramebufferScale fbo_scale{funcs};

	int pbiPath{0}, pbiFixed;


	vec3<GLfloat> colors[COLOR_NUM];

	//Inset specific
	double _inset_zoom;
	std::array<double, 3> _inset_center;
	//asis inset (closeup)

	//MVP
	struct Matrices {
		QMatrix4x4 mView, mrView, mProj, mrProj;
		QMatrix4x4 cube_mat;
	} _mat;

	double global_min_voxel;
	double global_max_dim;
	int global_cube_tex{0};

	double closeup_min_voxel;
	double closeup_max_dim;

	/////////////////////////////////////

	double _dpr;


	// Volume
	int slice_delta{0};

	MouseState mouse;
	QMatrix4x4 mrViewProj;
	QVector3D _prev_up, _prev_rgt;

	//need? init? cfg control ctor/dtor/func
	QVector3D pickA{}, pickB{};
	//bool colorMode;
	unsigned int _changes1{0};
#endif


#if 000
	explicit PRIV(Canvas& canvas): canvas{canvas} {
		viewerShared=new ViewerShared{};
		//viewerShared{Tracer::instance()->viewerShared()},
		//colors{}, colorMode{false},
		//curPos{}, tgtPos{},
		//mView{}, mrView{}, mProj{}, mrProj{},
		//scale_factor{0}, fbo_width{0}, fbo_height{0},
		//funcs{nullptr},

	}
	~PRIV() {
		if(!funcs_ok)
			return;

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
		for(size_t i=1; i<pathBuffers.size(); i++) {
			glDeleteBuffers(1, &pathBuffers[i].vbo);
			glDeleteVertexArrays(1, &pathBuffers[i].vao);
		}
		//glDeleteBuffers(1, &vbo_progr);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);

		viewerShared->deinitialize(&funcs);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
#endif

	int Engine::createPbi() {
		if(pathBuffers[0].prev_unused) {
			auto i=pathBuffers[0].prev_unused;
			pathBuffers[0].prev_unused=pathBuffers[i].prev_unused;
			return i;
		}
		PathBuffer pbItem;
		//
		pbItem.create(&PointGL::pos, &PointGL::misc, &PointGL::dir3);
		//std::array<GLint, 3>
		//std::array<GLuint, 1>

		pathBuffers.emplace_back(pbItem);
		return pathBuffers.size()-1;
	}
	void Engine::freePbi(int i) {
		pathBuffers[i].prev_unused=pathBuffers[0].prev_unused;
		pathBuffers[0].prev_unused=i;
	}
	void Engine::paintFinish(GLuint fbo) {
		auto scale_factor=_scale_factor;
		bool scale=scale_factor!=0;
		if(scale) {
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_scale);
			glViewport(0, 0, fbo_width, fbo_height);
			glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
		} else {
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
			glViewport(0, 0, fbo_width, fbo_height);
			glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
		}
		glUseProgram(*_prog_sort);
		glBindVertexArray(*_vao);
		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, fbo_edges.depth());
		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, fbo_edges.color());
		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_2D, fbo_opaque.depth());
		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_2D, fbo_opaque.color());
		glActiveTexture(GL_TEXTURE6);
		glBindTexture(GL_TEXTURE_2D, fbo_cubes.depth());
		glActiveTexture(GL_TEXTURE7);
		glBindTexture(GL_TEXTURE_2D, fbo_cubes.color());
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		if(_cursor_show) {
			glUseProgram(*_prog_cursor);
			glBindVertexArray(*_vao_fixed);
			auto radU=_closeup_zoom;
			auto& mat=_mat;
			float umpp;
			if(scale_factor+1<_dpr) {
				umpp=radU*_dpr/(scale_factor+1)/fbo_width;
			} else {
				umpp=radU/fbo_width;
			}
			glUniformMatrix4fv(_prog_cursor_proj, 1, false, &mat.mProj(0, 0));
			glUniform3fv(_prog_cursor_color, 1, &colors[C_ATTENTION][0]);
			vec3<float> a(_cursor_x, _cursor_y, -.9);
			a=mat.mrProj*a;
			glUniform3fv(_prog_cursor_center, 1, &a[0]);
			glUniform1f(_prog_cursor_size, umpp);
			glDisable(GL_DEPTH_TEST);
			//glEnable(GL_DEPTH_CLAMP);
			glDrawArrays(GL_TRIANGLES, 60, 3*8);
			glEnable(GL_DEPTH_TEST);
			//glDisable(GL_DEPTH_CLAMP);
		}

		if(scale) {
			glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_scale);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
			int width, height;
			width=fbo_width*(1+scale_factor);
			height=fbo_height*(1+scale_factor);
			glViewport(0, 0, width, height);
			glBlitFramebuffer(0, 0, fbo_width, fbo_height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}
	}

void Engine::paintEdge() {
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_edges);
	glViewport(0, 0, fbo_width, fbo_height);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
	if(_data_only)
		return;

	auto scale_factor=_scale_factor;
	auto radU=_closeup_zoom;
	auto& mat=_mat;
	float umpp;
	if(scale_factor+1<_dpr) {
		umpp=radU*_dpr/(scale_factor+1)/fbo_width;
	} else {
		umpp=radU/fbo_width;
	}

	glDisable(GL_BLEND);

	glUseProgram(*_prog_edge);
	gapr::node_attr p{_cur_pos.point};
	glUniform3i(_prog_edge_offset, p.ipos[0], p.ipos[1], p.ipos[2]);
	glUniformMatrix4fv(_prog_edge_view, 1, false, &mat.mView(0, 0));
	glUniformMatrix4fv(_prog_edge_proj, 1, false, &mat.mProj(0, 0));
	LOGV("offset: %d %d %d", p.ipos[0], p.ipos[1], p.ipos[2]);

	auto num_path=_path_data.size();
	if(num_path) {
		glUniform1f(_prog_edge_thick, umpp);
		glUniform3fv(_prog_edge_color0, 1, &colors[C_ATTENTION][0]);
		glUniform1ui(_prog_edge_id, GLuint{0});
		glBindVertexArray(pathBuffers[pbiPath]);
		glDrawArrays(GL_TRIANGLES, 0, (num_path-1)*6);
	}

	edge_model::reader reader{*_model2};

#if 0
	Tree curTree{};
	if(curPos.edge && curPos.edge.tree())
		curTree=curPos.edge.tree();
#endif
	for(auto eid: reader.edges_filter()) {
		auto& e=reader.edges().at(eid);
		auto color=colors[C_TYPE_0];
#if 0
		if(/*e.type()*/0!=0) {
			color=colors[C_TYPE_1];
		} else if(e.inLoop() || !e.tree()) {
			color=colors[C_ATTENTION];
		} else if(e.tree() /*&& e.tree()==curTree*/) {
			color=colors[C_TYPE_2];
		}
#endif

		//if(e.tree() && e.tree().selected()) {
		glUniform1f(_prog_edge_thick, 2*umpp);
		//} else {
		//viewerShared->progs[P_EDGE_PR]->setUniformValue("umpp", umpp);
		//}

#if 0
		if(curPos.edge && curPos.edge==e) {
			if(tgtPos.edge && tgtPos.edge==e) {
				auto ep=EdgePriv::get(e);
				glUniform1ui(idx_loc, static_cast<GLuint>(ep->index+1));
				glBindVertexArray(pathBuffers[ep->vaoi].vao);
				auto a=curPos.index;
				auto b=tgtPos.index;
				if(a>b)
					std::swap(a, b);
				if(b-a>0) {
					viewerShared->progs[P_EDGE_PR]->setUniformValue("color_edge[0]", color);
					glDrawArrays(GL_LINE_STRIP, a, b-a+1);

				}
				if(a>0 || ep->points.size()-b>1) {
					viewerShared->progs[P_EDGE_PR]->setUniformValue("color_edge[0]", color.darker(180));
					if(a>0) {
						glDrawArrays(GL_LINE_STRIP, 0, a+1);
					}
					if(ep->points.size()-b>1) {
						glDrawArrays(GL_LINE_STRIP, b, ep->points.size()-b);
					}
				}
				continue;
			}
		} else {
			if(!tgtPos.edge || tgtPos.edge!=e) {
				color=color.darker(180);
			}
		}
#endif


		auto [it_vaoi, it_ok]=_edge_vaoi.emplace(eid, 0);
		if(it_vaoi->second==0) {
			auto pathGL=pathToPathGL(e.points);
			int vaoi=createPbi();
			glBindBuffer(GL_ARRAY_BUFFER, pathBuffers[vaoi].buffer());
			glBufferData(GL_ARRAY_BUFFER, sizeof(PointGL)*pathGL.size(), pathGL.data(), GL_STATIC_DRAW);
			it_vaoi->second=vaoi;
		}
		glUniform1ui(_prog_edge_id, static_cast<GLuint>(eid+1));
		glUniform3fv(_prog_edge_color0, 1, &color[0]);
		glUniform3fv(_prog_edge_color1, 1, &colors[C_ALERT][0]);
		//auto ep=EdgePriv::get(e);
		glBindVertexArray(pathBuffers[it_vaoi->second]);
		glDrawArrays(GL_TRIANGLES, 0, (e.points.size()-1)*6);
		if(auto e=glGetError(); e!=GL_NO_ERROR)
			LOGV("gl error: %d", e);
	}

	glBindVertexArray(*_vao_fixed);
	glUseProgram(*_prog_vert);
	glUniformMatrix4fv(_prog_vert_view, 1, false, &mat.mView(0, 0));
	glUniformMatrix4fv(_prog_vert_proj, 1, false, &mat.mProj(0, 0));
	glUniform1f(_prog_vert_size, umpp);
	glUniform3i(_prog_vert_offset, p.ipos[0], p.ipos[1], p.ipos[2]);
	glUniform1ui(_prog_vert_idx, GLuint{1});

	for(auto& pid: reader.props_filter()) {
		if(pid.key=="error") {
			auto pos=reader.to_position(pid.node);
			gapr::node_attr::data_type pt;
			if(pos.edge) {
				auto& e=reader.edges().at(pos.edge);
				pt=e.points[pos.index/128];
			} else if(pos.vertex) {
				auto& v=reader.vertices().at(pid.node);
				pt=v.attr.data();
			} else {
				continue;
			}
			auto color=colors[C_ALERT];
			int rad=6;
			glUniform3fv(_prog_vert_color, 1, &color[0]);
			glUniform1i(_prog_vert_nid, GLint(pid.node.data));
			glUniform4i(_prog_vert_center, pt.first[0], pt.first[1], pt.first[2], rad);
			glDrawArrays(GL_TRIANGLE_FAN, 200, 32);
		}
	}

	for(auto vid: reader.vertices_filter()) {
		auto& v=reader.vertices().at(vid);
#if 0
		bool darker=false;
		if(curPos.edge && (curPos.edge.leftVertex()==v || curPos.edge.rightVertex()==v)) {
			if(tgtPos.edge && (tgtPos.edge.leftVertex()==v || tgtPos.edge.rightVertex()==v)) {
				if(curPos.edge==tgtPos.edge) {
					//darker=true;
				} else {
					darker=true;
				}
			}
		} else {
			if(!tgtPos.edge || (tgtPos.edge.leftVertex()!=v && tgtPos.edge.rightVertex()!=v)) {
				darker=true;
			}
		}
		//
		QColor color;
		if(v.inLoop() || !v.finished()) {
			color=colors[C_ATTENTION];
		} else if(v.tree() /*&& v.tree()==curTree*/) {
			color=colors[C_TYPE_2];
			if(darker)
				color=color.darker(180);
		} else {
			color=colors[C_TYPE_0];
			if(darker)
				color=color.darker(180);
		}
#endif
		auto color=colors[C_TYPE_0];
		auto it_state=reader.props().find(gapr::prop_id{vid, "state"});
		if(v.edges.size()<2) {
			if(it_state==reader.props().end()) {
				color=colors[C_ATTENTION];
			} else if(it_state->second!="end") {
				color=colors[C_ALERT];
			}
		}

		int rad=8;
		auto it_root=reader.props().find(gapr::prop_id{vid, "root"});
		if(it_root!=reader.props().end())
			rad=12;
#if 0
		if(v.tree() && v.tree().root()==v) {
			rad=12;
		} else if(v.neighbors().size()>1) {
			rad=6;
		}
#endif

		glUniform3fv(_prog_vert_color, 1, &color[0]);
		glUniform1i(_prog_vert_nid, GLint(vid.data));
		auto& p=v.attr;
		glUniform4i(_prog_vert_center, p.ipos[0], p.ipos[1], p.ipos[2], rad);
		glDrawArrays(GL_TRIANGLE_FAN, 200, 32);
		//if(v.tree() && v.tree().selected()) {
		//glDrawArrays(GL_LINES, 44, 16);
		//}
	}

	glEnable(GL_BLEND);
}
void Engine::paintOpaque() {
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_opaque);
	glViewport(0, 0, fbo_width, fbo_height);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

	//glViewport(0, 0, fbo_width, fbo_height);
	auto scale_factor=_scale_factor;
	auto radU=_closeup_zoom;
	auto& mat=_mat;
	float umpp;
	if(scale_factor+1<_dpr) {
		umpp=radU*_dpr/(scale_factor+1)/fbo_width;
	} else {
		umpp=radU/fbo_width;
	}

	//pick
	glUseProgram(*_prog_line);
	gapr::node_attr p{_cur_pos.point};
	glUniform3i(_prog_line_offset, p.ipos[0], p.ipos[1], p.ipos[2]);
	glUniformMatrix4fv(_prog_line_view, 1, false, &mat.mView(0, 0));
	glUniformMatrix4fv(_prog_line_proj, 1, false, &mat.mProj(0, 0));
	glUniform1f(_prog_line_thick, umpp*.6);
	glUniform3fv(_prog_line_color, 1, &colors[C_ATTENTION][0]);
	if(pickA!=pickB) {
		glBindVertexArray(pathBuffers[pbiFixed]);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}

	glBindVertexArray(*_vao_fixed);
	glUseProgram(*_prog_mark);
	glUniformMatrix4fv(_prog_mark_view, 1, false, &mat.mView(0, 0));
	glUniformMatrix4fv(_prog_mark_proj, 1, false, &mat.mProj(0, 0));
	glUniform1f(_prog_mark_size, umpp);
	glUniform3i(_prog_mark_offset, p.ipos[0], p.ipos[1], p.ipos[2]);
	auto& tgtPos=_tgt_pos;
	if(tgtPos.valid()) {
		glUniform3fv(_prog_mark_color, 1, &colors[C_ATTENTION][0]);
		gapr::node_attr pt{tgtPos.point};
		glUniform4i(_prog_mark_center, pt.ipos[0], pt.ipos[1], pt.ipos[2], 20);
		if(tgtPos.edge) {
			glDrawArrays(GL_TRIANGLES, 96, 24);
		} else {
			glDrawArrays(GL_TRIANGLES, 84, 12);
		}
		// Neurons, in circle, mark
	}
	auto& curPos=_cur_pos;
	if(curPos.valid()) {
		glUniform3fv(_prog_mark_color, 1, &colors[C_ATTENTION][0]);
		gapr::node_attr pt{curPos.point};
		glUniform4i(_prog_mark_center, pt.ipos[0], pt.ipos[1], pt.ipos[2], 16);
		glDrawArrays(GL_LINE_LOOP, 200, 32);
		// Neurons, in circle, mark
	}
	if(!_nodes_sel.empty()) {
		glUniform3fv(_prog_mark_color, 1, &colors[C_AXIS][0]);
		//viewerShared->progs[P_MARK]->setUniformValue("umpp", umpp);
		for(auto& [n, pt]: _nodes_sel) {
			glUniform4i(_prog_mark_center, pt.ipos[0], pt.ipos[1], pt.ipos[2], 7);
			glDrawArrays(GL_LINES, 200, 32);
		}
	}
	////////////////////////////////////

#if 0
	viewerShared->progs[P_NODE]->bind();
	locp=viewerShared->progs[P_NODE]->uniformLocation("p0int");
	glUniform3i(locp, p._x, p._y, p._z);
	viewerShared->progs[P_NODE]->setUniformValue("mView", mView);
	viewerShared->progs[P_NODE]->setUniformValue("mProj", mProj);
	viewerShared->progs[P_NODE]->setUniformValue("color[0]", colors[C_TYPE_4]);
	viewerShared->progs[P_NODE]->setUniformValue("color[1]", colors[C_TYPE_4]);
	viewerShared->progs[P_NODE]->setUniformValue("color[2]", colors[C_TYPE_4]);
	viewerShared->progs[P_NODE]->setUniformValue("color[3]", colors[C_TYPE_4]);
	viewerShared->progs[P_NODE]->setUniformValue("umpp", 10*umpp);
	for(auto e: graph.edges()) {
		auto ep=EdgePriv::get(e);
		glBindVertexArray(pathBuffers[ep->vaoi].vao);
		glDrawArrays(GL_POINTS, 0, e.points().size());
	}
#endif
}
void Engine::paintBlank() {
	int width, height;
	auto scale_factor=_scale_factor;
	width=fbo_width*(1+scale_factor);
	height=fbo_height*(1+scale_factor);
	glViewport(0, 0, width, height);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

	glUseProgram(*_prog_cursor);
	glBindVertexArray(*_vao_fixed);
	auto radU=30.0;
	float umpp;
	if(scale_factor+1<_dpr) {
		umpp=radU*_dpr/(scale_factor+1)/fbo_width;
	} else {
		umpp=radU/fbo_width;
	}
	mat4<float> proj, rproj;
	double z_clip=.5;
	float vspan=radU*height/width;
	proj.ortho(-radU, radU, -vspan, vspan, (1-z_clip)*radU, (1+z_clip)*radU, &rproj);
	glUniformMatrix4fv(_prog_cursor_proj, 1, false, &proj(0, 0));
	glUniform3fv(_prog_cursor_color, 1, &colors[C_ATTENTION][0]);
	vec3<float> a(0.0, 0.0, -.9);
	a=rproj*a;
	glUniform3fv(_prog_cursor_center, 1, &a[0]);
	glUniform1f(_prog_cursor_size, umpp);
	glDrawArrays(GL_TRIANGLES, 60, 3*8);
}

void Engine::paintVolume() {
	// XXX
	glDisable(GL_DEPTH_TEST);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_cubes);
	glViewport(0, 0, fbo_width/VOLUME_SCALE, fbo_height/VOLUME_SCALE);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
	if(closeup_cube_tex) {
		glUseProgram(*_prog_volume);
		glUniform3fv(_volume_color, 1, &colors[C_CHAN0][0]);
		glBindVertexArray(*_vao);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, *closeup_cube_tex);
		glUniform2f(_volume_xfunc, _closeup_xfunc[0], _closeup_xfunc[1]);
		mat4<GLfloat> mat_inv=_mat.cube_mat*_mat.mrView*_mat.mrProj;
		glUniformMatrix4fv(_volume_mat_inv, 1, false, &mat_inv(0, 0));
		{
			auto mvs=closeup_min_voxel;
			auto radU=_closeup_zoom;
			auto slice_pars=_slice_pars;
			LOGV("xfunc [%f %f] %lf %lf,%lf %u,%u", _closeup_xfunc[0], _closeup_xfunc[1], _closeup_zoom, closeup_min_voxel, closeup_max_dim, _slice_pars[0], _slice_pars[1]);
			if(_slice_mode) {
				glUniform3f(_volume_zpars, -1.0*slice_delta/slice_pars[1],
						-1.0*slice_delta/slice_pars[1], std::max(1.0/slice_pars[1], 9*mvs/radU/8));
			} else {
				glUniform3f(_volume_zpars, -1.0*slice_pars[0]/slice_pars[1],
						1.0*slice_pars[0]/slice_pars[1], std::max(1.0/slice_pars[1], 9*mvs/radU/8));
			}
		}
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}
	glEnable(GL_DEPTH_TEST);
}

	void Engine::updateCubeMatrix() {
		// Depends on: *_cur_pos, cube.xform, cube.data
		std::array<double, 3> p0;
		gapr::affine_xform* xform;
		std::array<unsigned int, 3> p1;
		std::array<std::size_t, 3> size;
		gapr::node_attr pt{_cur_pos.point};
		for(unsigned int i=0; i<3; i++)
			p0[i]=pt.pos(i);
		xform=&_closeup_xform;
		p1=_closeup_offset;
		auto cube_view=_closeup_cube.view<const char>();
		size={cube_view.width_adj(), cube_view.sizes(1), cube_view.sizes(2)};
		//cl cl
		auto& mat=_mat;
		for(unsigned int i=0; i<3; i++)
			p0[i]-=xform->origin[i];
		for(int i=0; i<4; i++)
			mat.cube_mat(3, i)=i<3?0:1;
		auto& rdir=xform->direction_inv;
		for(int i=0; i<3; i++) {
			double v=0.0;
			for(int j=0; j<3; j++) {
				mat.cube_mat(i, j)=rdir[i+j*3]/size[i];
				v+=rdir[i+j*3]*p0[j];
			}
			mat.cube_mat(i, 3)=(v-p1[i])/size[i];
		}
	}
	void Engine::updateInsetCenter() {
		auto& xform=_closeup_xform;
		auto sizes=_closeup_sizes;
		double z=0.0;
		for(unsigned int i=0; i<3; i++) {
			double x{0.0};
			for(unsigned int j=0; j<3; j++)
				x+=xform.direction[i+j*3]*sizes[j];
			z+=x*x;
			_inset_center[i]=x/2+xform.origin[i];
		}
		_inset_zoom=std::sqrt(z)/2;
	}
	void Engine::updateZoomMinMax() {
		gapr::affine_xform* xform;
		std::array<unsigned int, 3> size;
		double* outa;
		double* outb;
		xform=&_closeup_xform;
		for(unsigned int i=0; i<3; i++)
			size[i]=_closeup_cube_sizes[i]*3;
		outa=&closeup_min_voxel;
		outb=&closeup_max_dim;

		double vb=0;
		double va=INFINITY;
		for(unsigned int i=0; i<3; i++) {
			auto res=xform->resolution[i];
			if(va>res)
				va=res;
			auto vv=res*size[i];
			if(vv>vb)
				vb=vv;
		}
		*outa=va;
		*outb=vb;
	}
	void Engine::updateMVPMatrices() {
		// Depends on: *rgt, *radU, *up, *fbo_height, *fbo_width
		double radU;
		GLsizei w, h;
		double z_clip;
		radU=_closeup_zoom;
		w=fbo_width;
		h=fbo_height;
		z_clip=.5;
		auto& mat=_mat;
		auto& dir=_direction;
		vec3<GLfloat> rgt{(float)dir[3], (float)dir[4], (float)dir[5]};
		vec3<GLfloat> up{(float)dir[0], (float)dir[1], (float)dir[2]};
		mat.mView.look_at(rgt*radU, vec3<GLfloat>{0, 0, 0}, up, &mat.mrView);

		float vspan=radU*h/w;
		mat.mProj.ortho(-radU, radU, -vspan, vspan, (1-z_clip)*radU, (1+z_clip)*radU, &mat.mrProj);
	}
	void Engine::updateCubeTexture() {
		gapr::print("update cube ", 1);
		auto pi=&closeup_cube_tex;
		auto cub=&_closeup_cube;
		if(*pi)
			viewerShared->recycle(std::move(*pi));
		std::ostringstream oss;
		oss<<'@';
		for(unsigned int i=0; i<3; i++)
			oss<<_closeup_offset[i]<<':';
		oss<<_closeup_uri;
		*pi=viewerShared->get_tex3d(oss.str(), [cub](Texture3D& tex) {
			LOGV("upload texture...");
			tex.create();
			glBindTexture(GL_TEXTURE_3D, tex);
			gapr::gl::upload_texture3d(*cub);
		});
	}
	void Engine::updateInsetAxis() {
		int ch=0;
		glBindBuffer(GL_ARRAY_BUFFER, pathBuffers[pbiFixed].buffer());
		[[maybe_unused]] auto ptr=static_cast<PointGL*>(glMapBufferRange(GL_ARRAY_BUFFER, (2+ch*24)*sizeof(PointGL), 24*sizeof(PointGL), GL_MAP_WRITE_BIT|GL_MAP_INVALIDATE_RANGE_BIT));

		auto size=_closeup_sizes;
		auto& xform=_closeup_xform;
		uint32_t p0[3]={0, 0, 0};
		//for(int i=0; i<3; i++) {
		//gapr::print(size[i]);
		//}
		for(int dir=0; dir<3; dir++) {
			for(int i=0; i<2; i++) {
				for(int j=0; j<2; j++) {
					for(int k=0; k<2; k++) {
						double pp[3];
						for(int l=0; l<3; l++)
							pp[l]=xform.origin[l]
								+xform.direction[l+dir*3]*(k*size[dir]+p0[dir])
								+xform.direction[l+(dir+1)%3*3]*(j*size[(dir+1)%3]+p0[(dir+1)%3])
								+xform.direction[l+(dir+2)%3*3]*(i*size[(dir+2)%3]+p0[(dir+2)%3]);
#if 000
						gapr::node_attr p{pp[0], pp[1], pp[2]};
						ptr[dir*8+i*4+j*2+k]=p.data();
#endif
					}
				}
			}
		}
		glUnmapBuffer(GL_ARRAY_BUFFER);
	}
	void Engine::resizeFrameBuffers() {
		auto [ww, hh]=std::make_pair(_surface_w, _surface_h);
		int width, height;
		auto scale_factor=_scale_factor;
		width=(ww+scale_factor)/(1+scale_factor);
		height=(hh+scale_factor)/(1+scale_factor);
		fbo_width=width;
		fbo_height=height;
		if(fbo_width>fbo_width_alloc || fbo_width+2*FBO_ALLOC<fbo_width_alloc ||
				fbo_height>fbo_height_alloc || fbo_height+2*FBO_ALLOC<fbo_height_alloc) {
			fbo_width_alloc=(fbo_width+FBO_ALLOC)/FBO_ALIGN*FBO_ALIGN;
			fbo_height_alloc=(fbo_height+FBO_ALLOC)/FBO_ALIGN*FBO_ALIGN;

			//glActiveTexture(GL_TEXTURE0);
			fbo_scale.resize(fbo_width_alloc, fbo_height_alloc);
			fbo_opaque.resize(fbo_width_alloc, fbo_height_alloc);
			fbo_edges.resize(fbo_width_alloc, fbo_height_alloc);
			fbo_cubes.resize(fbo_width_alloc, fbo_height_alloc);

			//updateMVPMatrices();
		}
	}
	void Engine::set_dpr(double dpr) {
		_dpr=dpr;
		_changes1|=mask<chg::dpr>;
	}
	std::array<double, 6> Engine::handleRotate(const MouseState& state) {
		vec3<GLfloat> a(0, 0, -1);
		vec3<GLfloat> b((state.x-state.down_x)/100.0, -(state.y-state.down_y)/100.0, -1);
		a=mrViewProj*a;
		a/=a.mag();
		b=mrViewProj*b;
		b/=b.mag();
		auto norm=cross(a, b);
		mat4<> mat{};
		float proj=dot(a, b);
		if(proj<-1) proj=-1;
		if(proj>1) proj=1;
		float r=acos(proj);
		mat.rotate(-r, norm);
		auto up=mat*_prev_up;
		auto rgt=mat*_prev_rgt;
		return {up[0], up[1], up[2], rgt[0], rgt[1], rgt[2]};
	}
double _prev_zoom;
	double Engine::handleZoom(const MouseState& state) {
		auto dd=std::exp(-(state.y-state.down_y)/1024.0);
		return _prev_zoom*dd;
	}
vec3<> Engine::toLocal(const gapr::node_attr& p) const {
	gapr::node_attr d;
	for(unsigned int i=0; i<3; i++)
		d.ipos[i]=p.ipos[i]-_cur_pos.point.first[i];
	return {d.pos(0), d.pos(1), d.pos(2)};
}
	gapr::node_attr Engine::toGlobal(const vec3<>& p) const {
		gapr::node_attr pt{_cur_pos.point};
		auto x=p[0]+pt.pos(0);
		auto y=p[1]+pt.pos(1);
		auto z=p[2]+pt.pos(2);
		return {x, y, z};
	}

	/*! enable snapping, by *pixel. XXX not radius or distance.
	 * 
	 */
	bool Engine::pickPoint(float px, float py, Position& pos) {
		gapr::print("pick: ", px, ' ', py);
		int pick_size;
		int fbx, fby;
		/////////
		/////////////////
		auto scale_factor=_scale_factor;
		if(scale_factor+1<_dpr) {
			pick_size=PICK_SIZE*_dpr/(scale_factor+1);
			//fbx=x/(1+scale_factor);
			//fby=y/(1+scale_factor);
		} else {
			pick_size=PICK_SIZE;
			//fbx=x/(1+scale_factor);
			//fby=y/(1+scale_factor);
		}

		fbx=((px+1)*fbo_width-1.0)/2;
		fby=((1-py)*fbo_height-1.0)/2;
		//float px=(fbx*2+1.0)/fbo_width-1;
		//float py=1-(fby*2+1.0)/fbo_height;
		auto& m=_mat;
		if(_slice_mode) {
			auto slice_pars=_slice_pars;
			float depth=-1.0*slice_delta/slice_pars[1];
			vec3<GLfloat> lp=m.mrView*(m.mrProj*vec3<float>(px, py, depth));
			auto p=toGlobal(lp);
			pos=Position{p.data()};
			return true;
		}

		bool ret=false;
		//canvas.makeCurrent();
		if(fbx-pick_size>=0 && fby-pick_size>=0
				&& fby+pick_size<fbo_height
				&& fbx+pick_size<fbo_width) {
			int pickdr2=pick_size*pick_size+1;
			int pickI=-1;
			std::vector<GLuint> bufIdx((2*pick_size+1)*(2*pick_size+1));
			glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_edges);
			glReadBuffer(GL_COLOR_ATTACHMENT1);
			glReadPixels(fbx-pick_size, fbo_height-1-fby-pick_size, pick_size*2+1, pick_size*2+1, GL_RED_INTEGER, GL_UNSIGNED_INT, &bufIdx[0]);

			for(int dy=-pick_size; dy<=pick_size; dy++) {
				for(int dx=-pick_size; dx<=pick_size; dx++) {
					int i=dx+pick_size+(dy+pick_size)*(2*pick_size+1);
					if(bufIdx[i]>0) {
						auto dr2=dx*dx+dy*dy;
						if(dr2<pickdr2) {
							pickI=i;
							pickdr2=dr2;
						}
					}
				}
			}

			if(pickI>=0) {
				edge_model::edge_id edge_id=bufIdx[pickI]-1;
				glReadBuffer(GL_COLOR_ATTACHMENT2);
				std::vector<GLuint> bufPos((2*pick_size+1)*(2*pick_size+1));
				glReadPixels(fbx-pick_size, fbo_height-1-fby-pick_size, pick_size*2+1, pick_size*2+1, GL_RED_INTEGER, GL_INT, &bufPos[0]);
				//doneCurrent();

				if(edge_id==0) {
					edge_model::vertex_id vid{bufPos[pickI]};
					gapr::print("pick node: ", edge_id, ' ', vid.data);
					pos=Position{vid, {}};
					return true;
				}

				uint32_t pickPos=bufPos[pickI]*8;
				pos=Position{edge_id, pickPos, {}};

				return true;
			}

			pick_size/=VOLUME_SCALE;
			int mdx=0, mdy=0;
			GLubyte maxv=0;
			std::vector<GLubyte> bufRed((2*pick_size+1)*(2*pick_size+1)*4);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_cubes);
			gl::check_error(glGetError(), "err bind fb");
			glReadBuffer(GL_COLOR_ATTACHMENT0);
			gl::check_error(glGetError(), "err read buf");
			glReadPixels(fbx/VOLUME_SCALE-pick_size, fbo_height/VOLUME_SCALE-1-fby/VOLUME_SCALE-pick_size, pick_size*2+1, pick_size*2+1, GL_RGBA, GL_UNSIGNED_BYTE, &bufRed[0]);
			gl::check_error(glGetError(), "err read pix");
			for(int dy=-pick_size; dy<=pick_size; dy++) {
				for(int dx=-pick_size; dx<=pick_size; dx++) {
					int i=dx+pick_size+(dy+pick_size)*(2*pick_size+1);
					auto v=bufRed[i*4];
					gapr::print("v: ", (int)v, ' ', (int)bufRed[i*4+1], ' ', (int)bufRed[i*4+2], ' ', (int)bufRed[i*4+3]);
					if(v>maxv) {
						maxv=v;
						mdx=dx;
						mdy=dy;
					}
				}
			}
			if(maxv>0) {
				GLfloat mvaldepth=bufRed[(mdx+pick_size+(mdy+pick_size)*(2*pick_size+1))*4+1]/255.0;
				//glReadPixels(fbx/VOLUME_SCALE+mdx, fbo_height/VOLUME_SCALE-1-fby/VOLUME_SCALE+mdy, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &mvaldepth);
				// XXX 
				//glReadPixels(fbx/VOLUME_SCALE+mdx, fbo_height/VOLUME_SCALE-1-fby/VOLUME_SCALE+mdy, 1, 1, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, &mvaldepth);
			//gl::check_error(glGetError(), "err read pix2");
			gapr::print("read pix: ", mdx, ' ', mdy, ' ', mvaldepth);
				//canvas.doneCurrent();
				auto xx=mdx+pick_size+fbx-pick_size;
				auto yy=-mdy-pick_size+fby+pick_size;
				vec3<float> pp((xx*2+1.0)/fbo_width-1, 1-(yy*2+1.0)/fbo_height, mvaldepth*2-1);
				pp=m.mrView*(m.mrProj*pp);
				auto p=toGlobal(pp);
				pos=Position{p.data()};
				ret=true;
			}
		}

		vec3<> a(px, py, -1);
		vec3<> b(px, py, 1);
		a=m.mrView*(m.mrProj*a);
		b=m.mrView*(m.mrProj*b);
		vec3<> c=a-b;
		auto lenc=c.mag();
		c=c/lenc;
		vec3<> selC=pickA-pickB;
		if(auto l=selC.mag(); l>0.00001)
			selC/=l;
		gapr::print("selC: ", selC[0], ' ', selC[1], ' ', selC[2]);
		gapr::print("c: ", c[0], ' ', c[1], ' ', c[2]);
		vec3<> d=cross(c, selC);
		gapr::print("d: ", d[0], ' ', d[1], ' ', d[2]);

		mat4<> mat{};
		for(unsigned int i=0; i<3; i++) {
			mat(i, 0)=c[i];
			mat(i, 1)=-selC[i];
			mat(i, 2)=d[i];
			mat(3, i)=mat(i, 3)=0;
		}
		mat(3, 3)=1;
		mat4<> matr;
		matr.inverse(mat);
		vec3<> s=matr*(pickB-b);
		gapr::print("s: ", s[0], ' ', s[1], ' ', s[2]);
		//printMessage("pickpoint %1 %2 %3", d.length(), s.x(), lenc);
		if(d.mag()<0.1 || s[0]<0 || s[0]>lenc) {
			pickA=a;
			pickB=b;
			glBindBuffer(GL_ARRAY_BUFFER, pathBuffers[pbiFixed].buffer());
			auto ptr=static_cast<PointGL*>(glMapBufferRange(GL_ARRAY_BUFFER, 0*sizeof(PointGL), 6*sizeof(PointGL), GL_MAP_WRITE_BIT|GL_MAP_INVALIDATE_RANGE_BIT));
			auto pa=toGlobal(pickA);
			auto pb=toGlobal(pickB);
			{
				std::array<PointGL, 6> seg;
				pathToPathGL({pa.data(), pb.data()}, b-a, seg);
				for(unsigned int i=0; i<seg.size(); i++)
					ptr[i]=seg[i];
			}
			glUnmapBuffer(GL_ARRAY_BUFFER);
			//canvas.doneCurrent();
			return ret;
		}
		//canvas.doneCurrent();
		vec3<> t=b+s[0]*c;
		auto gt=toGlobal(t);
		pickA=pickB=vec3<>();
		pos=Position{gt.data()};
		return true;
	}
	void Engine::clearSelection() {
		pickA=pickB=vec3<float>();
	}
	void Engine::edges_removed(const std::vector<edge_model::edge_id>& edges_del) {
		for(auto eid: edges_del) {
			auto it=_edge_vaoi.find(eid);
			if(it!=_edge_vaoi.end()) {
				auto vaoi=it->second;
				if(vaoi!=0)
					freePbi(vaoi);
				_edge_vaoi.erase(it);
			}
		}
	}

void Engine::resizeGL(int w, int h) {
	//assert(has context)
	_surface_w=w;
	_surface_h=h;
	resizeFrameBuffers();
	updateMVPMatrices();
	_redraw_all=true;
}

void Engine::handleMotion(const AInputEvent* event) {
	auto action=AMotionEvent_getAction(event);
	std::size_t ptr_idx;
	auto N=AMotionEvent_getPointerCount(event);
	auto handle_down=[this](const AInputEvent* event, std::size_t idx) {
		auto time=AMotionEvent_getEventTime(event);
		auto id=AMotionEvent_getPointerId(event, idx);
		auto x=AMotionEvent_getX(event, idx);
		auto y=AMotionEvent_getY(event, idx);
		auto size=AMotionEvent_getSize(event, idx);
		if(size>1.0)
			return;
		LOGV("motion_down %d %f %f %f", id, x, y, size);
		auto [it, ins]=pointers.emplace(id, MouseState{});
		auto& state=it->second;
		state.id=id;
		state.down_time=time;
		state.down_x=state.x=x;
		state.down_y=state.y=y;
		state.move_cnt=0;

		for(std::size_t i=0; i<control_areas.size(); i+=5) {
			auto ptr=&control_areas.data()[i];
			auto m=ptr[0];
			auto l=ptr[1];
			if(x<=l)
				continue;
			auto r=ptr[3];
			if(x>=r)
				continue;
			auto t=ptr[2];
			if(y<=t)
				continue;
			auto b=ptr[4];
			if(y>=b)
				continue;
			if(m/100==1) {
				auto p=(2*x-l-r)/(r-l);
				auto q=(2*y-t-b)/(b-t);
				if(p*p+q*q>=1)
					continue;
			}
			state.mode=(MouseMode)m;
			break;
		}
		switch(state.mode) {
			case MouseMode::Rot:
				{
					auto& mat=_mat;
					mrViewProj=mat.mrView*mat.mrProj;
					auto& dir=_direction;
					_prev_up={(float)dir[0], (float)dir[1], (float)dir[2]};
					_prev_rgt={(float)dir[3], (float)dir[4], (float)dir[5]};
				}
				break;
			case MouseMode::Zoom:
				if(_model2) {
					_prev_zoom=_closeup_zoom;
				}
				break;
			case MouseMode::DataOnly:
				set_data_only(true);
				break;
			case MouseMode::Xfunc:
				{
					auto v=_prev_xfunc=_xfunc_states[_closeup_ch-1][1];
					char buf[10];
					std::snprintf(buf, 10, "%.5lf", v);
					call_java(120, buf);
				}
				break;
			case MouseMode::Xfunc2:
				{
					auto v=_prev_xfunc2=_xfunc_states[_closeup_ch-1][0];
					char buf[10];
					std::snprintf(buf, 10, "%.5lf", v);
					call_java(121, buf);
				}
				break;
			case MouseMode::SkipMisc:
				break;
			case MouseMode::PrEnd:
				break;
			case MouseMode::ExtBr:
				break;
			case MouseMode::JumpAddRep:
				break;
			case MouseMode::Cursor:
				if(_cursor_show) {
					_prev_cursor_x=_cursor_x;
					_prev_cursor_y=_cursor_y;
				} else {
					auto& mat=_mat;
					auto pos=&_tgt_pos;
					if(!pos->valid())
						pos=&_cur_pos;
					auto p=toLocal(gapr::node_attr{pos->point});
					p=mat.mProj*(mat.mView*p);
					_prev_cursor_x=p[0];
					_prev_cursor_y=p[1];
				}
				set_cursor(_prev_cursor_x, _prev_cursor_y, true);
				break;
			default:
				break;
		}
		apply_changes();
	};
	auto handle_up=[this](const AInputEvent* event, std::size_t idx) {
		auto id=AMotionEvent_getPointerId(event, idx);
		auto node=pointers.extract(id);
		if(!node)
			return;
		auto& state=node.mapped();
		auto time=AMotionEvent_getEventTime(event);
		auto x=AMotionEvent_getX(event, idx);
		auto y=AMotionEvent_getY(event, idx);
		//auto size=AMotionEvent_getSize(event, idx);
		auto get_gesture=[&state,&time,y,this]() ->int {
			if(time-state.down_time<200'000'000) {
				if(state.move_cnt<5 && std::abs(y-state.down_y)<3*_dpr)
					return 1;
			}
			if(time-state.down_time<500'000'000) {
				if(y-state.down_y>16*_dpr)
					return 3;
				if(y-state.down_y<-16*_dpr)
					return 2;
			}
			// XXX visual feedback if cancelled or executed
			return 0;
		};
		switch(state.mode) {
		case MouseMode::Rot:
			break;
		case MouseMode::Zoom:
			break;
		case MouseMode::DataOnly:
			set_data_only(false);
			break;
		case MouseMode::Xfunc:
			call_java(120, "");
			break;
		case MouseMode::Xfunc2:
			call_java(121, "");
			break;
		case MouseMode::SkipMisc:
			switch(get_gesture()) {
			case 1:
				on_goto_next_node_triggered();
				break;
			case 2:
				on_neuron_create_triggered();
				break;
			case 3:
				on_tracing_delete_triggered();
				break;
			default:
				break;
			}
			break;
		case MouseMode::PrEnd:
			switch(get_gesture()) {
			case 1:
				on_tracing_end_triggered();
				break;
			case 2:
				on_tracing_examine_triggered();
				break;
			case 3:
				on_end_as_dialog_finished2();
				break;
			default:
				break;
			}
			break;
		case MouseMode::ExtBr:
			switch(get_gesture()) {
			case 1:
				on_tracing_connect_triggered();
				break;
			case 2:
				on_tracing_extend_triggered();
				break;
			case 3:
				on_tracing_branch_triggered();
				break;
			default:
				break;
			}
			break;
		case MouseMode::JumpAddRep:
			switch(get_gesture()) {
			case 1:
				on_goto_target_triggered();
				break;
			case 2:
				on_report_error_triggered();
				break;
			case 3:
				break;
			default:
				break;
			}
			break;
		case MouseMode::Cursor:
			LOGV("timediff %" PRId64, time-state.down_time);
			if(_model2) {
				auto scale_factor=_scale_factor;
				auto dx=((x-state.down_x)/(1+scale_factor)*2)/fbo_width;
				auto dy=-((y-state.down_y)/(1+scale_factor)*2)/fbo_height;
				if(state.move_cnt<5 && time-state.down_time<200'000'000) {
					// XXX
					if(pickPoint(_prev_cursor_x+dx, _prev_cursor_y+dy, _pick_pos)) {
						gapr::node_attr p{_pick_pos.point};
						gapr::print("pos: ", p.pos(0), ' ', p.pos(1), ' ', p.pos(2));
						on_canvas_pick_changed();
						set_cursor(_prev_cursor_x+dx, _prev_cursor_y+dy, false);
					} else {
						set_cursor(_prev_cursor_x+dx, _prev_cursor_y+dy, true);
					}
					//state.mode=MouseMode::Nul;
				} else {
					set_cursor(_prev_cursor_x+dx, _prev_cursor_y+dy, true);
				}
			}
			break;
		default:
			break;
		}
		apply_changes();
		///////////////////////
	// XXX
#if 0
	if(event->buttons()==Qt::RightButton) {
		return;
	}
#endif
#if 0
	switch(m.mode) {
		case MouseMode::Pan:
			break;
		case MouseMode::Drag:
			//unsetCursor();
			//Q_EMIT endDrag();
			break;
		case MouseMode::Nul:
			return;
	}
	m.mode=MouseMode::Nul;
#endif
	};
	auto handle_move=[this](const AInputEvent* event, std::size_t idx) {
		auto id=AMotionEvent_getPointerId(event, idx);
		auto it=pointers.find(id);
		if(it==pointers.end())
			return;
		auto& state=it->second;
		auto x=AMotionEvent_getX(event, idx);
		auto y=AMotionEvent_getY(event, idx);
		//auto size=AMotionEvent_getSize(event, idx);
		if(x!=state.x || y!=state.y) {
			state.x=x;
			state.y=y;
			state.move_cnt++;
			switch(state.mode) {
				case MouseMode::Rot:
					set_directions(handleRotate(state));
					break;
				case MouseMode::Zoom:
					if(_model2)
						set_closeup_zoom(handleZoom(state));
					break;
				case MouseMode::DataOnly:
					break;
				case MouseMode::Xfunc:
					if(_closeup_ch) {
						auto a=_xfunc_states[_closeup_ch-1][0];
						auto v=_xfunc_states[_closeup_ch-1][1]=(_prev_xfunc-a)*std::exp((state.x-state.down_x)/(256*_dpr))+a;
						set_closeup_xfunc(calc_xfunc(_closeup_ch));
						char buf[10];
						std::snprintf(buf, 10, "%.5lf", v);
						call_java(120, buf);
					}
					break;
				case MouseMode::Xfunc2:
					if(_closeup_ch) {
						auto b=_xfunc_states[_closeup_ch-1][1];
						auto v=_xfunc_states[_closeup_ch-1][0]=b-(b-_prev_xfunc2)*std::exp((state.x-state.down_x)/(-256*_dpr));
						set_closeup_xfunc(calc_xfunc(_closeup_ch));
						char buf[10];
						std::snprintf(buf, 10, "%.5lf", v);
						call_java(121, buf);
					}
					break;
			case MouseMode::SkipMisc:
				break;
			case MouseMode::PrEnd:
				break;
			case MouseMode::ExtBr:
				break;
			case MouseMode::JumpAddRep:
				break;
				case MouseMode::Cursor:
					{
						auto scale_factor=_scale_factor;
						auto dx=((x-state.down_x)/(1+scale_factor)*2)/fbo_width;
						auto dy=-((y-state.down_y)/(1+scale_factor)*2)/fbo_height;
						set_cursor(_prev_cursor_x+dx, _prev_cursor_y+dy, true);
					}
					break;
				case MouseMode::Nul:
					return;
				default:
					break;
			}
		}
		apply_changes();
	};
#if 0
	if(_slice_mode) {
		auto radU=_closeup_zoom;
		d=d/radU;
		if(d>0 && d<1) d=1;
		if(d<0 && d>-1) d=-1;
		_priv->slice_delta+=d;
		_changes0|=mask<chg::slice_delta>;
	} else {
		auto dd=std::pow(0.96, d);
		set_closeup_zoom(_closeup_zoom*dd);
	}
#endif

	switch(action&AMOTION_EVENT_ACTION_MASK) {
		case AMOTION_EVENT_ACTION_DOWN:
			assert(N==1);
			handle_down(event, 0);
			break;
		case AMOTION_EVENT_ACTION_UP:
			assert(N==1);
			handle_up(event, 0);
			break;
		case AMOTION_EVENT_ACTION_MOVE:
			for(std::size_t i=0; i<N; i++)
				handle_move(event, i);
			break;
		case AMOTION_EVENT_ACTION_POINTER_DOWN:
			ptr_idx=(action&AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)>>AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
			for(std::size_t i=0; i<N; i++) {
				if(i!=ptr_idx)
					handle_move(event, i);
				else
					handle_down(event, i);
			}
			break;
		case AMOTION_EVENT_ACTION_POINTER_UP:
			ptr_idx=(action&AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)>>AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
			for(std::size_t i=0; i<N; i++) {
				if(i!=ptr_idx)
					handle_move(event, i);
				else
					handle_up(event, i);
			}
			break;
		case AMOTION_EVENT_ACTION_CANCEL:
		case AMOTION_EVENT_ACTION_OUTSIDE:
		case AMOTION_EVENT_ACTION_HOVER_MOVE:
		case AMOTION_EVENT_ACTION_SCROLL:
		case AMOTION_EVENT_ACTION_HOVER_ENTER:
		case AMOTION_EVENT_ACTION_HOVER_EXIT:
		case AMOTION_EVENT_ACTION_BUTTON_PRESS:
		case AMOTION_EVENT_ACTION_BUTTON_RELEASE:
		default:
			LOGV("unhandeled motion action: %d", action);
			return;
	}
}

#if 0
namespace {
	struct EndAsDialog: QDialog {
		gapr::proofread::Ui::NodeStateDialog _ui;
		explicit EndAsDialog(QWidget* parent=nullptr): QDialog{parent} {
			_ui.setupUi(this);
		}
		~EndAsDialog() { }

		Q_SLOT void on_button_box_accepted() {
			return accept();
		}
		Q_SLOT void on_button_box_rejected() {
			return reject();
		}
		Q_SLOT void on_listWidget_currentItemChanged(QListWidgetItem* item, QListWidgetItem*) {
			_ui.state->setText(item->text());
		}
		Q_SLOT void on_listWidget_itemActivated(QListWidgetItem* item) {
			accept();
		}
		Q_OBJECT
	};
}


Canvas::Canvas(QWidget* parent):
	QOpenGLWidget{parent}, _priv{std::make_unique<PRIV>(*this)}
{
	gapr::print("canvas ctor");
	//setContextMenuPolicy(PreventContextMenu);

	//setMinimumSize(QSize(480, 320));
	//setFocusPolicy(Qt::WheelFocus);

	//setAcceptDrops(true);

	if(auto dbg_flags=getenv("GAPR_DEBUG"); dbg_flags) {
		std::regex r{"\\bfps\\b", std::regex::icase};
		if(std::regex_search(dbg_flags, r)) {
			auto timer=new QTimer{this};
			timer->start(10);
			connect(timer, &QTimer::timeout, this, static_cast<void (Canvas::*)()>(&Canvas::repaint));
			_priv->_debug_fps=true;
		}
	}
}
Canvas::~Canvas() {
	/*! gl context n.a. (available in QOpenGLWidget::~QOpenGLWidget()) */
}



void Canvas::edges_removed(const std::vector<edge_model::edge_id>& edges_del) {
	_priv->edges_removed(edges_del);
}

void Canvas::handle_screen_changed(QScreen* screen) {
	assert(_changes0==0);
	auto dpr=screen->devicePixelRatio();
	_priv->set_dpr(dpr);
	update();
}
#endif

void Engine::apply_changes_stage1() {
	auto chg=_changes0;
	_changes0=0;
	//fprintf(stderr, "Chg %08X\n", chg);
	_changes1|=chg;
	bool upd=false;
	bool upd_cursor=false;
	if(chg&mask<chg::cursor>) {
		upd_cursor=true;
	}
	if(chg&mask<chg::data_only>) {
		upd=true;
	}
	if(chg&mask<chg::slice_mode>) {
		if(_slice_mode)
			slice_delta=0;
		upd=true;
	}
	if(chg&mask<chg::slice_delta>) {
		int n=_slice_pars[1];
		if(slice_delta<-n) slice_delta=-n;
		if(slice_delta>n) slice_delta=n;
	}
	if(chg&mask<chg::closeup_info, chg::direction>) {
		updateInsetCenter();
		updateZoomMinMax();
		upd=true;
	}
	if(chg&mask<chg::closeup_zoom, chg::scale_factor, chg::dpr, chg::direction>) {
		[[maybe_unused]] auto prev=_closeup_zoom;
		if(_closeup_zoom>1*closeup_max_dim) {
			_closeup_zoom=1*closeup_max_dim;
		} else if(_closeup_zoom<64*closeup_min_voxel) {
			_closeup_zoom=64*closeup_min_voxel;
		}
		LOGV("zoom: %lf %lf", prev, _closeup_zoom);
		updateMVPMatrices();
		upd=true;
	}
	if(chg&mask<chg::cur_pos>) {
		clearSelection();
		upd=true;
	}
	if(chg&mask<chg::closeup_cube, chg::cur_pos>) {
		if(_closeup_cube) {
			updateCubeMatrix();
			upd=true;
		}
	}
	if(chg&mask<chg::slice_delta, chg::closeup_zoom, chg::closeup_xfunc, chg::tgt_pos, chg::path_data, chg::model_update>)
		upd=true;
	if(chg&mask<chg::sel>)
		upd=true;
	if(chg&mask<chg::model>) {
#if 0
		auto curs=Qt::CrossCursor;
		if(!_model)
			curs=Qt::ForbiddenCursor;
		else if(0/*_busy*/)
			curs=Qt::BusyCursor;
		setCursor(curs);
#endif
	}

	{
	auto chg=_changes1;
	if(chg) {
		//make current
	//////////////////////////////////////////////////////////////////////////
		_changes1=0;
		if(chg&mask<chg::closeup_info>) {
			updateInsetAxis();
		}
		if(chg&mask<chg::closeup_cube>) {
			//QGuiApplication::setOverrideCursor(Qt::WaitCursor);
			updateCubeTexture();
			//QGuiApplication::restoreOverrideCursor();
		}
		if(chg&mask<chg::scale_factor, chg::dpr>) {
			if(funcs_ok)
				resizeFrameBuffers();
		}
		if(chg&mask<chg::path_data>) {
			auto pathGL=pathToPathGL(_path_data);
			glBindBuffer(GL_ARRAY_BUFFER, pathBuffers[pbiPath].buffer());
			glBufferData(GL_ARRAY_BUFFER, sizeof(PointGL)*pathGL.size(), pathGL.data(), GL_STATIC_DRAW);
		}
	}
	}
	if(upd) {
		_redraw_all=true;
		update();
	} else if(upd_cursor) {
		//_redraw_all=false;
		update();
	}
}





#include "gapr/detail/template-class-static-data.hh"

void* template_class_static_data_3() {
	return &gapr::detail::template_class<int>::data;
}
