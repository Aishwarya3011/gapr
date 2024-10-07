/* fix-pr4m/window.cc
 *
 * Copyright (C) 2018 GOU Lingfeng
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

//XXX retry only when network error. (reconnect)

#ifdef _WIN64
#include <winsock2.h>
#endif

#include "window.hh"

#include "gapr/edge-model.hh"
#include "canvas.hh"
#include "../fix/compute.hh"
#include "gapr/cube-builder.hh"

//#include "gapr/cube.hh"
//#include "gapr/detail/finally.hh"
//#include "gapr/future.hh"
//#include "gapr/mem-file.hh"
//#include "gapr/model.hh"
//#include "gapr/parser.hh"
#include "gapr/str-glue.hh"
#include "gapr/fiber.hh"
#include "gapr/streambuf.hh"
#include "gapr/trace-api.hh"
#include "gapr/timer.hh"
#include "gapr/gui/login-dialog.hh"

#include "gapr/gui/application.hh"
#include "gapr/gui/dialogs.hh"
#include "gapr/gui/range-widget.hh"
#include "gapr/gui/utility.hh"

#include <vector>
#include <deque>
#include <fstream>
#include <limits>
#include <optional>
#include <unordered_set>
#include <random>

#include <boost/asio/ip/tcp.hpp>

#include <QtWidgets/QtWidgets>

#include "ui_win-main.h"
#include "ui_dlg-display.h"
#include "ui_dlg-node-state.h"
#include "dialog-alt.hh"

#include "dup/serialize-delta.hh"

namespace ba=boost::asio;
namespace bs=boost::system;

using gapr::client_end;
using Window=gapr::proofread::Window;
using resolver=ba::ip::tcp::resolver;

#include "../fix/misc.hh"

constexpr static int MAX_SLICES=480;
constexpr static std::array<int, 4> SCALE_FACTORS{0, 1, 2, 3};

struct StateMask {
	// like that in canvas???
	using base_type=uint32_t;
	enum _base_type_: base_type {
		Model=1<<0,
		Path=1<<1,
		CurPos=1<<2,
		TgtPos=1<<3,
		ViewMode=1<<4,
		Cube=1<<5,
	};
};


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

struct Window::PRIV {
	Window& window;
	std::optional<Args> _args;

	Ui::Window _ui;
	Ui::DisplayDialog _ui_display;
	QDialog* _dlg_display{nullptr};
	QMenu* _popup_canvas{nullptr};
	QProgressBar* _progr{nullptr};

	std::unique_ptr<EndAsDialog> _dlg_end_as{};
	std::unique_ptr<gapr::PasswordDialog> _dlg_pw{};
	std::unique_ptr<gapr::login_dialog> _dlg_login{};
	std::unique_ptr<gapr::ErrorStateDialog> _dlg_err{};

	std::size_t _list_sel{SIZE_MAX};

	// XXX legacy
	gapr::cube_builder _cube_builder{gapr::app().get_executor(), gapr::app().thread_pool()};

	std::optional<resolver> _resolver;
	resolver::results_type _addrs{};
	resolver::endpoint_type _addr{ba::ip::address{}, 0};
	client_end _cur_conn{};
	client_end _conn_need_pw{};
	bool _initialized{false};
	std::string _data_secret;
	std::string _srv_info;
	gapr::tier _tier;
	gapr::stage _stage;
	gapr::tier _tier2;
	std::string _gecos;

	gapr::trace_api api;
	gapr::edge_model _model;
	gapr::commit_history _hist;
	uint64_t _latest_commit;

	std::vector<gapr::cube_info> _cube_infos;

	bool _states_valid{false};
	Position _cur_pos, _tgt_pos;
	Position _seed_pos;
	anchor_id _cur_anchor, _tgt_anchor;
	FixPos _cur_fix, _tgt_fix;
	std::vector<std::array<double, 4>> _xfunc_states;

	std::size_t _closeup_ch{0};
	gapr::cube _closeup_cube;
	std::array<unsigned int, 3> _closeup_offset;
	std::array<double, 6> _filter_bbox;

	//XXX state_section;
	std::atomic<bool> _cancel_flag{false};
	PreLock _prelock_model{}, _prelock_path{};
	gapr::delta_add_edge_ _path;

	std::default_random_engine rng{static_cast<unsigned int>(std::time(nullptr))};

	explicit PRIV(Window& window): window{window} { }
	~PRIV() { }
	void setupUi();

	std::array<double, 2> calc_xfunc(std::size_t ch) {
		auto& st=_xfunc_states[ch-1];
		auto& range=_cube_infos[ch-1].range;
		auto d=range[1]-range[0];
		return {range[0]+st[0]*d, range[1]+(st[1]-1)*d};
	}
	void calc_center(Position& pos, std::size_t ch) {
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
	double calc_defult_zoom(std::size_t ch) {
		auto& info=_cube_infos[ch-1];
		double max_d{0.0};
		for(unsigned int i=0; i<3; i++) {
			auto d=info.xform.resolution[i]*info.cube_sizes[i]*2;
			if(d>max_d)
				max_d=d;
		}
		return max_d*2/3;
	}


	void postConstruct() {
		//_cube_builder=new LoadThread{&window};

		//QObject::connect(_cube_builder, &LoadThread::threadWarning, session, &Session::loadThreadWarning);
		//QObject::connect(_cube_builder, &LoadThread::threadError, session, &Session::loadThreadError);
		_cube_builder.async_wait([this](std::error_code ec, int progr) {
			return cubeFinished(ec, progr);
		});
		//QObject::connect(_cube_builder, &LoadThread::updateProgress, session, &Session::updateProgress);

		//_cube_builder->getBoundingBox(&bbox);
		//_cube_builder->getMinVoxelSize(&mvol);

		_ui.canvas->set_model(_model);
		_cur_anchor=gapr::anchor_id{gapr::node_id{1}, {}, 0};
		// XXX
		_tgt_anchor=gapr::anchor_id{gapr::node_id{4}, {}, 0};
		_ui.canvas->set_current(_cur_pos);

		// XXX parse states right after receiving
		// put parsing results in mem. var.
		// do check while parsing.
		// XXX restore _xfunc_states
		// "xfunc_states.I"

		{
			// Assume parsing done and values in range.
			auto selc=_ui_display.select_closeup;
			QSignalBlocker _blk1{selc};
			std::size_t first_c{0};
			for(unsigned int i=0; i<_cube_infos.size(); i++) {
				if(_cube_infos[i].is_pattern()) {
					if(!first_c) {
						first_c=i+1;
						if(!_states_valid)
							_closeup_ch=first_c;
					}
					selc->addItem(QString::fromStdString(_cube_infos[i].name()), i+1);
					if(_closeup_ch==i+1)
						selc->setCurrentIndex(selc->count()-1);
				}
			}
			_ui_display.frame_closeup->setEnabled(first_c);
		}
		if(_closeup_ch) {
			_ui_display.xfunc_closeup->set_state(_xfunc_states[_closeup_ch-1]);
			_ui_display.xfunc_closeup->setEnabled(true);
			auto& info=_cube_infos[_closeup_ch-1];
			_ui.canvas->set_closeup_info(info.xform, info.sizes, info.cube_sizes, {});
			_ui.canvas->set_closeup_xfunc(calc_xfunc(_closeup_ch));
			if(!_cur_pos.valid()) {
				calc_center(_cur_pos, _closeup_ch);
			}
			_ui.canvas->set_closeup_zoom(calc_defult_zoom(_closeup_ch));
			//??**void set_closeup_cube(gapr::cube cube, std::string&& uri, std::array<unsigned int, 3> offset);
		} else {
			_ui_display.xfunc_closeup->setEnabled(false);
		}

		//startDownloadCube();
		_ui.canvas->apply_changes();

		_ui.xfunc_slave->set_master(_ui_display.xfunc_closeup);

		//_ui.canvas->set_directions(const std::array<double, 6>& dirs);

		//startDownloadCube();

		update_state(0);
		std::vector<edge_model::point> path;
		path.push_back(_cur_pos.point);
		path.push_back(_cur_pos.point);
		std::get<0>(path.back().first)+=20480;
		_ui.canvas->set_path_data(std::move(path));

		_ui.canvas->apply_changes();

		int mode=2;
		switch(_stage) {
			case gapr::stage::initial:
			case gapr::stage::frozen:
				mode=0;
				break;
			case gapr::stage::open:
				if(_tier2>gapr::tier::proofreader)
					mode=0;
				break;
			case gapr::stage::guarded:
				if(_tier2>gapr::tier::proofreader) {
					mode=0;
				} else if(_tier2>gapr::tier::annotator) {
					mode=1;
				}
				break;
			case gapr::stage::closed:
				if(_tier2>gapr::tier::annotator)
					mode=0;
				break;
		}
		const char* modes[]={
			"Readonly", "Proofread", "Edit"
		};
		_ui.mode->setText(modes[mode]);
		_ui.goto_next_cube->setEnabled(mode>1);
		_ui.neuron_create->setEnabled(mode>1);
		_ui.report_error->setEnabled(mode>0);
		_ui.reopen_error->setEnabled(mode>0);
		_ui.resolve_error->setEnabled(mode>1);
		_ui.tracing_connect->setEnabled(mode>1);
		_ui.tracing_extend->setEnabled(mode>1);
		_ui.tracing_branch->setEnabled(mode>1);
		_ui.tracing_end->setEnabled(mode>0);
		_ui.tracing_end_as->setEnabled(mode>0);
		_ui.act_rec_mark1->setEnabled(mode>0);
		_ui.act_rec_mark0->setEnabled(mode>0);
		_ui.tracing_delete->setEnabled(mode>1);
		_ui.tracing_examine->setEnabled(mode>0);

#if 0
		// XXX init VC
		//connect(dlg.get(), &QDialog::finished, this, &Window::ask_password_cb);
		// {
		//   catalog: sources
		//   target: clear
		//   commit_id: no_config use_latest
		// }
		try {
			while(!lines.empty()) {
				auto line=std::move(lines.back());
				lines.pop_back();
				if(line.key.compare("map-global")==0) {
					// if(empty)
					//   disable;
					auto r=parseValue<std::size_t>(line.val, map_g_);
					if(!r)
						gapr::report("line ", line.lineno, ": failed to parse ...");
				} else if(line.key.compare("map-global")==0) {
				} else {
					gapr::report("line ", line.lineno, ": unknown key");
				}
			}

			if(map_g_<_cube_infos.size() && !_cube_infos[map_g_].is_pattern)
				map_g=map_g_;
			if(map_c_<_cube_infos.size() && _cube_infos[map_c_].is_pattern)
				map_c=map_c_;
		} catch(const std::runtime_error& e) {
			; // XXX report
		}
		// cur_pos zoom_global zoom_closeup ;
		// cur_nid -> _init_nid=...;
		// right up -> viewer->set_dir();
		// view_mode -> set view_mode;
		// colors -> set_colors
		// channel_xfunc -> set xfunc

		// enable global view
		if(map_c!=SIZE_MAX) {
			// enable two other modes
		}
		// viewer update
		//viewer->update();
		void ViewerColorOptions::config(ViewerPriv* _vp, Options* opt) const {
			int v;
			if(opt->getInt("viewer.color.mode", &v))
				_vp->colorMode=v;
			else
				_vp->colorMode=colorModeDef;

			for(int i=0; i<COLOR_NUM; i++) {
				auto key=QString{"viewer.color.%1"}.arg(i);
				auto vals=opt->getIntList(key);
				if(vals.size()<3) {
					_vp->colors[i]=colorsDef[i];
				} else {
					_vp->colors[i]=QColor{vals[0], vals[1], vals[2]};
				}
			}
		}
#endif
	}

	void xfunc_closeup_changed(double low, double up) {
		if(_closeup_ch) {
			_xfunc_states[_closeup_ch-1][0]=low;
			_xfunc_states[_closeup_ch-1][1]=up;
			_ui.canvas->set_closeup_xfunc(calc_xfunc(_closeup_ch));
		}
		_ui.canvas->apply_changes();
	}
	void select_closeup_changed(int index) {
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
	}

	std::chrono::steady_clock::time_point _dload_t0;
	void startDownloadCube() {
		if(!_seed_pos.valid())
			return;
		gapr::node_attr pt{_seed_pos.point};
		auto map_c=_closeup_ch;
		if(map_c!=0) {
			if(_cube_builder.build(map_c, _cube_infos[map_c-1], {pt.pos(0), pt.pos(1), pt.pos(2)}, false, _closeup_cube?&_closeup_offset:nullptr)) {
				std::array<uint32_t, 6> bboxi;
				if(!_cube_builder.get_bbox(bboxi)) {
					_filter_bbox={0.0, 0.0, 0.0, -1.0};
				} else {
					calc_bbox(_cube_infos[_closeup_ch-1].xform, bboxi, _filter_bbox);
				}
				_progr->setValue(0);
				_progr->setVisible(true);
				_dload_t0=std::chrono::steady_clock::now();
			}
		}
	}
	void cubeFinished(std::error_code ec, int progr) {
		if(ec)
			gapr::print("error load cube: ", ec.message());
		if(progr==1001) {
			auto cube=_cube_builder.get();
			while(cube.data) {
				auto view=cube.data.view<void>();
				gapr::print("Cube: ", view.sizes(0), "x", view.sizes(1), "x", view.sizes(2), " +", view.ystride());
				gapr::print("cube_out: ", cube.chan);
				if(_closeup_ch==cube.chan) {
					_closeup_cube=cube.data;
					_closeup_offset=cube.offset;
					_ui.canvas->set_closeup_cube(std::move(cube.data), std::move(cube.uri), cube.offset);
				}
				cube=_cube_builder.get();
			}
			_ui.canvas->apply_changes();
		}
		if(progr==1001) {
			_progr->setVisible(false);
			auto dt=std::chrono::steady_clock::now()-_dload_t0;
			auto nms=std::chrono::duration_cast<std::chrono::milliseconds>(dt).count();
			gapr::print("Download takes: ", nms, "ms");
		} else {
			_progr->setValue(progr);
		}
		_cube_builder.async_wait([this](std::error_code ec, int progr) {
			return cubeFinished(ec, progr);
		});
	}
	std::string format_link(gapr::link_id l, gapr::edge_model::view model) {
		std::ostringstream oss;
		oss<<l;
		if(l.nodes[0]) {
			gapr::prop_id pid{l.nodes[0], "error"};
			if(auto it=model.props().find(pid); it!=model.props().end())
				oss<<"\nerror="<<it->second;
		}
		return oss.str();
	}
	void target_changed(const Position& pos, gapr::edge_model::view model) {
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
				_ui.canvas->clear_selection();
			_tgt_anchor=model.to_anchor(pos);
			auto txt=format_link(_tgt_anchor.link, model);
			_ui.tgt_node->setText(QString::fromStdString(txt));
			gapr::print("tgt_pos: ", _tgt_anchor.link.nodes[0].data, ':', _tgt_anchor.link.nodes[1].data);
			_tgt_fix=FixPos{};
#if 0
			if(tgtPos.point.valid() && tgtPos.edge && tgtPos.edge.tree()) {
				curRow=tgtPos.edge.tree().index();
				auto idx=model->index(curRow, 0, {});
				listView->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::Clear|QItemSelectionModel::SelectCurrent|QItemSelectionModel::Rows);
			}
#endif
			//updateActions;
			_ui.canvas->set_target(pos);
			_ui.canvas->apply_changes();
		}
	}
	void pick_position() {
		auto pos=_ui.canvas->pick();
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
	void pick_position(const gapr::fix::Position& pos) {
		edge_model::reader graph{_model};
		target_changed(pos, graph);
	}
#if 0
	void pickPosition(Edge edg, size_t idx, const Point* pos) {
		gapr::print("pick: ", pos.edge, ':', pos.index, ':', pos.point[0]);
		bool chg=false;
		if(tgtPos.edge!=edg) {
			tgtPos.edge=edg;
			chg=true;
		}
		const Point* p=nullptr;
		if(edg) {
			if(tgtPos.index!=idx) {
				tgtPos.index=idx;
				chg=true;
			}
			p=&edg.points()[idx];
		} else {
			p=pos;
		}
		if(p) {
			if(!(tgtPos.point==*p)) {
				tgtPos.point=*p;
				chg=true;
			}
		}
		if(chg) {
			////viewer->updatePosition(tgtPos, false);
			if(tgtPos.point.valid() && tgtPos.edge && tgtPos.edge.tree()) {
				curRow=tgtPos.edge.tree().index();
				auto idx=model->index(curRow, 0, {});
				listView->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::Clear|QItemSelectionModel::SelectCurrent|QItemSelectionModel::Rows);
			}
		}
	}
#endif

	// XXX do this when graph changed(edge changed): priv->path.path.clear();
	// change current edge binding
	// change target edge binding
	void jumpToPosition(const Position& pos, gapr::edge_model::view model) {
		_cur_pos=pos;
		_path.nodes.clear(); // path is valid only when it's in current cube

		_cur_anchor=model.to_anchor(_cur_pos);
		auto txt=format_link(_cur_anchor.link, model);
		_ui.cur_node->setText(QString::fromStdString(txt));
		gapr::print("cur_pos: ", _cur_anchor.link.nodes[0].data, ':', _cur_anchor.link.nodes[1].data);
		_cur_fix=FixPos{};

		_ui.canvas->set_current(_cur_pos);
		_ui.canvas->set_path_data({});
		_ui.canvas->apply_changes();
	}

	void critical_err(const std::string& err, const std::string& info, const std::string& detail) const {
		ba::post(gapr::app(), [this,
				err=QString::fromStdString(err),
				info=QString::fromStdString(info),
				detail=QString::fromStdString(detail)
		]() { window.critical_error(err, info, detail); });
	}
	void warning_msg(const std::string& err, const std::string& info) const {
		ba::post(gapr::app(), [this,
				err=QString::fromStdString(err),
				info=QString::fromStdString(info)
		]() { window.warning_msg(err, info); });
	}
	void get_passwd(const std::string& err) const {
		gapr::print("ask_passwd");
		gapr::str_glue msg{_args->user, '@', _args->host, ':', _args->port};
		ba::post(gapr::app(), [this,
				err=QString::fromStdString(err),
				msg=QString::fromStdString(msg.str())
		]() { window.ask_password(msg, err); });
	}
	void show_message(const std::string& msg) const {
		ba::post(gapr::app(), [this,
				msg=QString::fromStdString(msg)
		]() { window.show_message(msg); });
	}
	void ask_retry(const std::string& err, const std::string& info, const std::string& detail) const {
		ba::post(gapr::app(), [this,
				err=QString::fromStdString(err),
				info=QString::fromStdString(info),
				detail=QString::fromStdString(detail)
		]() { window.show_retry_dlg(err, info, detail); });
	}

	void start() {
		bs::error_code ec;
		auto addr=ba::ip::make_address(_args->host, ec);
		if(ec)
			return resolve();
		gapr::print("resolve");
		_addr.address(addr);
		_addr.port(_args->port);
		if(_args->passwd.empty())
			get_passwd({});
		else
			connect();
	}

	void resolve() {
		//*skip,*reuse
		if(!_resolver)
			_resolver.emplace(gapr::app().io_context());
		_resolver->async_resolve(_args->host, "0", [this](bs::error_code ec, const resolver::results_type& res) {
			if(ec) {
				gapr::str_glue err{"Unable to look up `", _args->host, "'."};
				return critical_err(err.str(), ec.message(), {});
			}
			assert(!res.empty());
			_addrs=res;
			if(_args->passwd.empty())
				get_passwd({});
			else
				connect();
		});
	}

	void got_passwd(std::string&& pw) {
		if(pw.empty())
			return get_passwd("Empty.");
		_args->passwd=std::move(pw);
		if(_conn_need_pw)
			login(std::move(_conn_need_pw));
		else
			connect();
	}

	void connect() {
		auto conn=client_end{gapr::app().io_context().get_executor(), gapr::app().ssl_context()};
		if(!_addr.port())
			return range_connect(_addrs.begin(), std::move(conn));

		gapr::print("connect");
		conn.async_connect(_addr, [this,conn](bs::error_code ec) mutable {
			if(ec) {
				gapr::str_glue err{"Unable to connect to [", _addr, "]."};
				ask_retry(err.str(), ec.message(), {});
				return;
			}
			handshake(std::move(conn));
		});
	}

	void range_connect(resolver::results_type::const_iterator it, client_end&& conn) {
		resolver::endpoint_type addr{it->endpoint().address(), _args->port};
		gapr::print("range_connect: ", addr);
		conn.async_connect(addr, [this,conn,addr,it](bs::error_code ec) mutable {
			if(ec) {
				gapr::str_glue msg{"Unable to connect to [", addr, "]: ", ec.message()};
				show_message(msg.str());
				++it;
				if(it!=_addrs.end())
					return range_connect(it, std::move(conn));
				gapr::str_glue err{"Unable to connect to `", _args->host, ':', _args->port, "'."};
				ask_retry(err.str(), "Tried each resolved address.", {});
				return;
			}
			_addr.address(it->endpoint().address());
			_addr.port(_args->port);
			handshake(std::move(conn));
		});
	}

	void handshake(client_end&& conn) {
		gapr::print(1, "handshake: ");
		auto fut=api.handshake(conn);
		auto ex=gapr::app().io_context().get_executor();
		std::move(fut).async_wait(ex, [this,conn=std::move(conn)](auto&& res) mutable {
			if(!res) try {
				auto ec=res.error_code();
				return ask_retry("Unable to handshake.", ec.message(), {});
			} catch(const std::runtime_error& e) {
				return critical_err("Unable to handshake.", e.what(), {});
			}
			_srv_info=std::move(res.get().banner);
			return login(std::move(conn));
		});
	}

	void login(client_end&& msg) {
		gapr::print(1, "login: ");
		auto fut=api.login(msg, _args->user, _args->passwd);
		auto ex=gapr::app().io_context().get_executor();
		std::move(fut).async_wait(ex, [this,msg=std::move(msg)](auto&& res) mutable {
			if(!res) try {
				auto ec=res.error_code();
				return ask_retry("Login error: network failure.", ec.message(), {});
			} catch(const std::runtime_error& e) {
				return critical_err("Login error.", e.what(), {});
			}
			if(res.get().tier==gapr::tier::locked)
				return critical_err("Login error.", "User locked.", {});
			if(res.get().tier>gapr::tier::locked) {
				_conn_need_pw=std::move(msg);
				return get_passwd(res.get().gecos);
			}
			_tier=res.get().tier;
			_gecos=std::move(res.get().gecos);
			return select(std::move(msg));
		});
	}

	void select(client_end&& msg) {
		auto fut=api.select(msg, _args->group);
		auto ex=gapr::app().io_context().get_executor();
		std::move(fut).async_wait(ex, [this,msg=std::move(msg)](auto&& res) mutable {
			if(!res) try {
				auto ec=res.error_code();
				return ask_retry("Select error: network failure.", ec.message(), {});
			} catch(const std::runtime_error& e) {
				return critical_err("Select error.", e.what(), {});
			}
			_latest_commit=res.get().commit_num;
			_data_secret=res.get().secret;
			_stage=res.get().stage;
			_tier2=res.get().tier2;
			if(!_initialized)
				return get_catalog(std::move(msg));
			_cur_conn=std::move(msg);
			ba::post(gapr::app(), [this,file=std::move(res.get())]() mutable {
				return window.enter_stage3();
			});
		});
	}

	//XXX WindowModified: operation not finished
	void get_catalog(client_end&& msg) {
		auto fut=api.get_catalog(msg);
		auto ex=gapr::app().io_context().get_executor();
		std::move(fut).async_wait(ex, [this,msg=std::move(msg)](auto&& res) mutable {
			if(!res) try {
				auto ec=res.error_code();
				return ask_retry("Unable to load catalog: network failure.", ec.message(), {});
			} catch(const std::runtime_error& e) {
				return critical_err("Unable to load catalog.", e.what(), {});
			}
			load_catalog(std::move(res.get().file));
			return get_state(std::move(msg));
		});
	}

	void load_catalog(gapr::mem_file&& file) {
		auto f=gapr::make_streambuf(std::move(file));
		std::istream str{f.get()};
		// XXX mesh in viewer
		std::vector<gapr::mesh_info> mesh_infos;
		auto base_url="https://"+_args->host+":"+std::to_string(_args->port)+"/api/data/"+_args->group+"/";
		gapr::parse_catalog(str, _cube_infos, mesh_infos, base_url);
		if(_cube_infos.empty()) {
			// no imaging data, any operation is invalid
			// XXX
			// report and terminate
			throw;
		}
		_xfunc_states.clear();
		for(auto& s: _cube_infos) {
			// XXX in thread_pool
			if(!s.xform.update_direction_inv())
				gapr::report("no inverse");
			s.xform.update_resolution();
			gapr::print("cube: ", s.location());
		}
		_xfunc_states.resize(_cube_infos.size(), {0.0, 1.0, 0.0, 1.0});
		for(auto& s: mesh_infos) {
			if(!s.xform.update_direction_inv())
				gapr::report("no inverse");
			s.xform.update_resolution();
			gapr::print("mesh: ", s.location());
		}
	}

	void get_state(client_end&& msg) {

		// XXX
		//test_commit(client_end::msg{msg}.recycle().alloc(), 0);


		auto fut=api.get_state(msg);
		auto ex=gapr::app().io_context().get_executor();
		std::move(fut).async_wait(ex, [this,msg=std::move(msg)](auto&& res) {
			if(!res) try {
				auto ec=res.error_code();
				return ask_retry("Unable to load state: network failure.", ec.message(), {});
			} catch(const std::runtime_error& e) {
				return critical_err("Unable to load state.", e.what(), {});
			}
			_cur_conn=std::move(msg);
			_initialized=true;

#if 0
			if(file) {
				auto f=gapr::make_streambuf(std::move(file));
				std::istream str{f.get()};
				state_section=load_section(str, "fix"); //check ini format, and return section lines
			}
#endif
			ba::post(gapr::app(), [this,file=std::move(res.get().file)]() mutable {
				window.enter_stage3();
			});
		});

	}

	void update_state(StateMask::base_type chg);

	bool check_connect() {
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

	struct ConnectSt {
		std::atomic<bool> cancel_flag{false};
		Position cur_pos, tgt_pos;
		unsigned int method;
	};
	std::shared_ptr<ConnectSt> _prev_conn;
	void tracing_connect() {
		if(!check_connect())
			return;
		fix::ConnectAlg alg{&_model, _cur_pos, _tgt_pos};
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
		auto ex1=gapr::app().thread_pool().get_executor();
		ba::post(ex1, [prom=std::move(prom),alg=std::move(alg)]() mutable {
			try {
				gapr::print("asdfasdf");
				std::move(prom).set(alg.compute());
				gapr::print("gggg");
			} catch(const std::runtime_error& e) {
				unlikely(std::move(prom), std::current_exception());
			}
		});

		auto ex2=gapr::app().get_executor();
		std::move(fut).async_wait(ex2, [this,cur_conn](gapr::likely<gapr::delta_add_edge_>&& res) {
		//XXX if(_priv->has_binding_to_gui) ...;
			if(!cur_conn->cancel_flag.load())
		do {
			if(!res) try {
				auto ec=res.error_code();
				std::cerr<<ec.message()<<'\n';
				break;
			} catch(const std::runtime_error& e) {
				std::cerr<<e.what()<<'\n';
				break;
			}
			_path=std::move(res.get());
#if 0
			double len=0;
			for(size_t i=1; i<priv->path.points.size(); i++) {
				len+=priv->path.points[i].distTo(priv->path.points[i-1]);
			}
			printMessage("Total length: ", len);
#endif
			auto pth=_path.nodes;
			_ui.canvas->set_path_data(std::move(pth));
		} while(false);
		//priv->viewer->setProgressData({});
		//priv->viewer->update();
		_ui.canvas->apply_changes();

		_prelock_model.end_read_async();
		update_state(StateMask::Model|StateMask::Path);
		});
	}

	template<gapr::delta_type Typ>
		bool model_prepare(gapr::node_id nid0, gapr::delta<Typ>&& delta) {
			auto ex1=gapr::app().thread_pool().get_executor();
			assert(ex1.running_in_this_thread());

			edge_model::loader loader{_model};
			return loader.load<true>(nid0, std::move(delta));
		}

	void update_model_stats(gapr::edge_model::view model) {
		std::ostringstream oss;
		//oss<<"Number of commits: "<<model.nodes().size();
		oss<<"Number of nodes: "<<model.nodes().size();
		oss<<"\nNumber of vertices: "<<model.vertices().size();
		oss<<"\nNumber of edges: "<<model.vertices().size();
		oss<<"\nNumber of annotations: "<<model.props().size()<<'\n';
	}

	Position fix_pos(const Position& pos, anchor_id anchor, gapr::edge_model::view model) {
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
	Position fix_pos(const Position& pos, FixPos fix, gapr::edge_model::view model, gapr::node_id nid0) {
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
	auto get_state_cachepath() const {
		auto tag_str=gapr::str_glue{nullptr, "\x01"}("model_cache2", _args->host, _args->port, _args->group).str();
		return gapr::get_cachepath(tag_str);
	}
	bool model_apply(bool last) {
		auto ex2=gapr::app().get_executor();
		assert(ex2.running_in_this_thread());

		edge_model::updater updater{_model};
		if(!updater.empty()) {
			if(!updater.apply())
				return false;

			gapr::print(1, "model changed");
			_ui.canvas->model_changed(updater.edges_del());

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
			auto txt=format_link(_tgt_anchor.link, updater);
			_ui.tgt_node->setText(QString::fromStdString(txt));
			update_model_stats(updater);
		}
		std::size_t nocache_siz;
		if(last && (nocache_siz=_model.nocache_inc(0))>16*1024*1024) {
			assert(_hist.tail_tip()==0);
			auto state_cnt=_hist.body_count();
			auto ex1=gapr::app().thread_pool().get_executor();
			gapr::print("state_cache cache overflow: ", nocache_siz, " dumping...");
			ba::post(ex1, [this,state_cnt,nocache_siz]() mutable {
				gapr::edge_model::reader reader{_model};
				auto file=reader.dump_state(state_cnt);
				auto buf=gapr::make_streambuf(std::move(file));
				// ZZZ
				if(true) {
					gapr::edge_model model2;
					{
						edge_model::loader loader2{model2};
						auto r=loader2.init(*buf);
						assert(r);
						buf->pubseekpos(0);
					}
					{
						edge_model::updater updater2{model2};
						updater2.apply();
					}
					{
						edge_model::reader reader2{model2};
						auto r=reader2.equal(reader);
						assert(r);
					}
				}
				auto cachepath=get_state_cachepath();
				save_cache_file(cachepath, *buf);
				_model.nocache_dec(nocache_siz);
			});
		}
		return true;
	}
	bool load_commits(gapr::fiber_ctx& ctx, client_end msg, uint64_t upto) {
		//ensure conn
		assert(!_prelock_model.can_read_async());
		if(_hist.body_count()>=upto)
			return true;
		gapr::timer<4> timer;
		gapr::mem_file commits_file;
		{
			gapr::promise<gapr::mem_file> prom{};
			gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
			auto ex1=gapr::app().thread_pool().get_executor();
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
		std::size_t total_size=commits_file.size();
		auto strmbuf=gapr::make_streambuf(std::move(commits_file));
		unsigned int cur_percent=0;
		unsigned int inc_percent=5;
		if(total_size<1*1024*1024)
			inc_percent=100;
		else if(total_size<10*1024*1024)
			inc_percent=25;

		while(_hist.body_count()<upto) {
			auto ex1=gapr::app().thread_pool().get_executor();
			gapr::promise<uint64_t> prom{};
			gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
			ba::post(ex1, [this,prom=std::move(prom),strmbuf=strmbuf.get()]() mutable {
				gapr::commit_info info;
				if(!info.load(*strmbuf))
					gapr::report("commit file no commit info");
				auto ex1=gapr::app().thread_pool().get_executor();
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

			auto pct=strmbuf->pubseekoff(0, std::ios::cur, std::ios::in)*100.0/total_size;
			if(pct>=cur_percent+inc_percent) {
				do {
					cur_percent+=inc_percent;
				} while(pct>=cur_percent+inc_percent);

				if(!model_filter(ctx))
					return false;
				gapr::promise<bool> prom{};
				gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
				auto ex2=gapr::app().get_executor();
				ba::post(ex2, [this,prom=std::move(prom)]() mutable {
					std::move(prom).set(model_apply(false));
				});
				if(!std::move(fib2).async_wait(gapr::yield{ctx}))
					return false;
			}
			timer.mark<3>();
		}
		_model.nocache_inc(total_size);
		gapr::print("load commits timming: ", timer);
		return true;
	}
	bool model_filter(gapr::fiber_ctx& ctx) {
		gapr::promise<bool> prom{};
		gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
		auto ex1=gapr::app().thread_pool().get_executor();
		ba::post(ex1, [this,prom=std::move(prom)]() mutable {
			auto ex1=gapr::app().thread_pool().get_executor();
			assert(ex1.running_in_this_thread());
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
	bool model_filter(gapr::fiber_ctx& ctx, const std::array<double, 6>& bbox) {
		gapr::print(1, "model filter", bbox[0], ',', bbox[1], ',', bbox[2], ',', bbox[3], ',', bbox[4], ',', bbox[5]);
		gapr::promise<bool> prom{};
		gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
		auto ex1=gapr::app().thread_pool().get_executor();
		ba::post(ex1, [this,prom=std::move(prom),bbox]() mutable {
			auto ex1=gapr::app().thread_pool().get_executor();
			assert(ex1.running_in_this_thread());
			edge_model::loader loader{_model};
			auto r=loader.filter(bbox);
			return std::move(prom).set(r);
		});
		if(!std::move(fib2).async_wait(gapr::yield{ctx}))
			return false;
		return true;
	}
	bool load_model_state(gapr::fiber_ctx& ctx, client_end msg) {
		//ensure conn
		assert(!_prelock_model.can_read_async());
		gapr::timer<3> timer;
		timer.mark<0>();

		auto cachepath=get_state_cachepath();
		bool use_existing{false};
		std::unique_ptr<std::streambuf> cachebuf;
		if(std::filesystem::is_regular_file(cachepath)) {
			auto sb=std::make_unique<std::filebuf>();
			if(sb->open(cachepath, std::ios::in|std::ios::binary)) {
				gapr::print("state_cache using cached state...");
				use_existing=true;
				cachebuf=std::move(sb);
			}
		}
		if(!use_existing) {
			gapr::fiber fib{ctx.get_executor(), api.get_model(msg)};
			auto cmt=std::move(fib).async_wait(gapr::yield{ctx});
			if(!cmt.file)
				return true;
			gapr::print("state_cache using downloaded state...");
			cachebuf=gapr::make_streambuf(std::move(cmt.file));
		}
		timer.mark<1>();

		gapr::promise<uint64_t> prom{};
		gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
		auto ex1=gapr::app().thread_pool().get_executor();
		ba::post(ex1, [this,use_existing,prom=std::move(prom),buf=std::move(cachebuf),cachepath=std::move(cachepath)]() mutable {
			auto ex1=gapr::app().thread_pool().get_executor();
			assert(ex1.running_in_this_thread());
			edge_model::loader loader{_model};
			auto r=loader.init(*buf);
			std::move(prom).set(r);
			if(r && !use_existing && buf->pubseekoff(0, std::ios::cur, std::ios::in)>16*1024*1024) {
				buf->pubseekpos(0);
				save_cache_file(cachepath, *buf);
			}
		});
		auto r=std::move(fib2).async_wait(gapr::yield{ctx});
		if(!r)
			return false;
		timer.mark<2>();
		_hist.body_count(r);
		gapr::print("load model_state timming: ", timer);
		return true;
	}
	gapr::future<bool> load_latest_commits() {
		// cur_conn
		if(!_prelock_model.can_write_later())
			return {};

		_prelock_model.begin_write_later();
		update_state(StateMask::Model);

		gapr::fiber fib{gapr::app().io_context().get_executor(), [this](gapr::fiber_ctx& ctx) {
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
	void latest_commits_loaded(gapr::likely<bool>&& res) {
		if(!res)
			window.critical_error("Error", "load commit error", "");
		if(!res.get())
			window.critical_error("Error", "load commit error2", "");
		if(!model_apply(true))
			window.critical_error("Error", "model apply error", "");
		_prelock_model.end_write_later();
		update_state(StateMask::Model);
	}
	gapr::future<bool> find_seed_pos() {
		// XXX softlock???
		// ***find candidate[cli] && ***soft lock[srv]
		// lock_many[**srv](candidate[**cli]...)
		// 
		gapr::edge_model::reader reader{_model};
		if(auto nid_next=get_seed_pos_random(reader, _tier2<=gapr::tier::annotator, rng())) {
			///////////////////nid_next=gapr::node_id{31516776};
			auto pos=reader.to_position(nid_next);
			if(pos.edge) {
				auto& e=reader.edges().at(pos.edge);
				jumpToPosition(Position{pos.edge, pos.index, e.points[pos.index/128]}, reader);
			} else {
				auto& v=reader.vertices().at(nid_next);
				jumpToPosition(Position{nid_next, v.attr.data()}, reader);
			}
			target_changed(Position{}, reader);
			_ui.canvas->apply_changes();
			update_state(0);
			_seed_pos=_cur_pos;
			_next_mask.clear();
			startDownloadCube();

			_prelock_model.begin_write_later();
			gapr::fiber fib{gapr::app().io_context().get_executor(), [this,bbox=_filter_bbox](gapr::fiber_ctx& ctx) mutable {
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
	void present_cube(gapr::likely<bool>&& res) {
		if(!res)
			window.critical_error("Error", "set filter error", "");
		if(!res.get())
			window.critical_error("Error", "set filter error2", "");
		if(!model_apply(true))
			window.critical_error("Error", "model apply error", "");
		_prelock_model.end_write_later();
		update_state(StateMask::Model);
	}

	bool check_extend() {
		// cur_conn
		if(!_prelock_path.can_write_later())
			return false;
		if(!_prelock_model.can_write_later())
			return false;
		if(_path.nodes.size()<2)
			return false;
		return true;
	}

	enum class SubmitRes {
		Deny,
		Accept,
		Retry
	};
	template<gapr::delta_type Typ>
		std::pair<SubmitRes, std::string> submit_commit(gapr::fiber_ctx& ctx, gapr::delta<Typ>&& delta) {
			gapr::promise<gapr::mem_file> prom{};
			gapr::fiber fib2{ctx.get_executor(), prom.get_future()};
			auto ex1=gapr::app().thread_pool().get_executor();
			ba::post(ex1, [&delta,prom=std::move(prom)]() mutable {
				gapr::mem_file payload;
				try {
					if(cannolize(delta)<0)
						throw;
					payload=serialize(delta);
					std::move(prom).set(std::move(payload));
				} catch(const std::runtime_error& e) {
					return unlikely(std::move(prom), std::current_exception());
				}
			});
			auto payload=std::move(fib2).async_wait(gapr::yield{ctx});
			auto payload_size=payload.size();

			auto msg=_cur_conn;
			gapr::fiber fib{ctx.get_executor(), api.commit(msg, Typ, std::move(payload), _hist.body_count(), _hist.tail_tip())};
			gapr::trace_api::commit_result res;
			try {
				res=std::move(fib).async_wait(gapr::yield{ctx});
			} catch(std::runtime_error& e) {
				return {SubmitRes::Deny, e.what()};
			}
			auto [nid0, cmt_id, upd]=res;
			gapr::print("commit res: ", nid0, cmt_id, upd);
			auto r=load_commits(ctx, _cur_conn, upd);
			if(!r)
				return {SubmitRes::Deny, "err load"};
			if(!nid0) {
				warning_msg("Commit failed", "Probably other modifications invalidated this one.\nRetry or proofread at another place.");
				if(cmt_id) {
					model_filter(ctx);
					return {SubmitRes::Retry, {}};
				} else {
					model_filter(ctx);
					return {SubmitRes::Deny, {}};
				}
			}
			gapr::promise<bool> prom2{};
			gapr::fiber fib3{ctx.get_executor(), prom2.get_future()};
			auto ex2=gapr::app().thread_pool().get_executor();
			ba::post(ex2, [this,nid0=nid0,prom2=std::move(prom2),&delta]() mutable {
				auto r=model_prepare(gapr::node_id{nid0}, std::move(delta));
				return std::move(prom2).set(r);
			});
			// XXX join edges
			if(!std::move(fib3).async_wait(gapr::yield{ctx}))
				return {SubmitRes::Deny, "err load2"};
			gapr::print("prepare ok");
			_model.nocache_inc(payload_size);
			_hist.add_tail(cmt_id);
			model_filter(ctx);
			return {SubmitRes::Accept, {}};
		}

	static bool check_extend_raised(gapr::edge_model::reader& reader, gapr::tier tier, const gapr::delta_add_edge_& path) {
		if(reader.raised() && tier>=gapr::tier::annotator) {
			unsigned int hit=0;
			for(auto l: {path.left, path.right}) {
				gapr::link_id link{l};
				if(link) {
					auto pos=reader.nodes().at(link.nodes[0]);
					if(pos.edge) {
						auto& e=reader.edges().at(pos.edge);
						if(e.raised)
							++hit;
					} else {
						auto& v=reader.vertices().at(pos.vertex);
						if(v.raised)
							++hit;
					}
				}
			}
			if(hit==0)
				return false;
		}
		return true;
	}
	static bool check_vertex_raised(gapr::edge_model::reader& reader, gapr::tier tier, const gapr::edge_model::vertex& vert) {
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
	static bool check_vert_raised(gapr::edge_model::reader& reader, gapr::tier tier, gapr::edge_model::vertex_id vert_id) {
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
	gapr::future<std::pair<SubmitRes, std::string>> start_extend() {
		if(!check_extend())
			return {};
		gapr::edge_model::reader reader{_model};
		if(!check_extend_raised(reader, _tier2, _path))
			return {};
#if 0
		if(priv->path.edge0 && priv->path.edge1) {
			Vertex v0{};
			if(priv->path.index0==0) {
				v0=priv->path.edge0.leftVertex();
			} else if(priv->path.index0==priv->path.edge0.points().size()-1) {
				v0=priv->path.edge0.rightVertex();
			}
			Vertex v1{};
			if(priv->path.index1==0) {
				v1=priv->path.edge1.leftVertex();
			} else if(priv->path.index1==priv->path.edge1.points().size()-1) {
				v1=priv->path.edge1.rightVertex();
			}
			if(v0 && v1 && v0==v1) {
				showWarning("Extend branch", "Loops not allowed", this);
				return;
			} else if(!v0 && !v1 && priv->path.edge0==priv->path.edge1
					&& priv->path.index0==priv->path.index1) {
				showWarning("Extend branch", "Loops not allowed", this);
				return;
			}
		}
#endif
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

		gapr::fiber fib{gapr::app().io_context().get_executor(), [this](gapr::fiber_ctx& ctx) {
			return submit_commit(ctx, gapr::delta_add_edge_{_path});
		}};
		return fib.get_future();
	}
	gapr::future<std::pair<SubmitRes, std::string>> start_branch() {
		if(!check_extend())
			return {};
		gapr::edge_model::reader reader{_model};
		if(!check_extend_raised(reader, _tier2, _path))
			return {};
#if 0
		if(priv->path.edge0 && priv->path.edge1) {
			Vertex v0{};
			if(priv->path.index0==0) {
				v0=priv->path.edge0.leftVertex();
			} else if(priv->path.index0==priv->path.edge0.points().size()-1) {
				v0=priv->path.edge0.rightVertex();
			}
			Vertex v1{};
			if(priv->path.index1==0) {
				v1=priv->path.edge1.leftVertex();
			} else if(priv->path.index1==priv->path.edge1.points().size()-1) {
				v1=priv->path.edge1.rightVertex();
			}
			if(v0 && v1 && v0==v1) {
				showWarning("Create branch", "Loops not allowed", this);
				return;
			} else if(!v0 && !v1 && priv->path.edge0==priv->path.edge1
					&& priv->path.index0==priv->path.index1) {
				showWarning("Create branch", "Loops not allowed", this);
				return;
			}
		}
#endif
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

		gapr::fiber fib{gapr::app().io_context().get_executor(), [this](gapr::fiber_ctx& ctx) {
			return submit_commit(ctx, gapr::delta_add_edge_{_path});
		}};
		return fib.get_future();
	}
	void finish_extend(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			window.critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			window.critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				_path.nodes.clear();
				_ui.canvas->set_path_data({});
				if(!res.get().second.empty())
					window.critical_error("Error", QString::fromStdString(res.get().second), "");
				break;
			case SubmitRes::Accept:
				_path.nodes.clear();
				_ui.canvas->set_path_data({});
				break;
			case SubmitRes::Retry:
#if 0
				if(can_reuse) {
					fix;
				}
#endif
				_path.nodes.clear();
				_ui.canvas->set_path_data({});
				break;
		}
		_ui.canvas->apply_changes();
		_prelock_path.end_write_later();
		_prelock_model.end_write_later();
		update_state(StateMask::Model|StateMask::Path);
	}

	bool check_refresh() {
		// cur_conn
		if(!_prelock_model.can_write_later())
			return false;
		return true;
	}

	bool do_refresh(gapr::fiber_ctx& ctx) {
		auto msg=_cur_conn;
		gapr::fiber fib{ctx.get_executor(), api.select(msg, _args->group)};
		auto upd=std::move(fib).async_wait(gapr::yield{ctx});
#if 0
		if(err)
			return {SubmitRes::Deny, "msg"};
#endif

		if(!load_commits(ctx, _cur_conn, upd.commit_num))
			return false;
		return model_filter(ctx);
	}
	gapr::future<bool> start_refresh() {
		if(!check_refresh())
			return {};
		_prelock_model.begin_write_later();
		update_state(StateMask::Model);

		gapr::fiber fib{gapr::app().io_context().get_executor(), [this](gapr::fiber_ctx& ctx) {
			return do_refresh(ctx);
		}};
		return fib.get_future();
	}
	void finish_refresh(gapr::likely<bool>&& res) {
		if(!res)
			window.critical_error("Error", "load commit error", "");
		if(!res.get())
			window.critical_error("Error", "load commit error2", "");
		if(!model_apply(true))
			window.critical_error("Error", "model apply error", "");
		_ui.canvas->apply_changes();
		_prelock_model.end_write_later();
		update_state(StateMask::Model|StateMask::Path);
	}

	bool filter_next_pos2(gapr::node_attr pos) const noexcept {
		for(unsigned int i=0; i<3; i++) {
			auto x=pos.pos(i);
			if(x<=_filter_bbox[i]+3)
				return false;
			if(x>=_filter_bbox[3+i]-3)
				return false;
		}
		return true;
	}
	std::unordered_set<gapr::node_id> _next_mask;

	void autosel_for_proofread(gapr::edge_model::view reader) {
		auto sel_nodes=[this](auto&& nodes_sel) {
			_ui.canvas->selection(std::move(nodes_sel));
			//_nodes_sel=std::move(nodes_sel);
			//_state_man.change(_state1_sel);
		};
		auto pick_tgt=[this,&reader](const auto& pos) {
			target_changed(pos, reader);
		};
		auto xform=&_cube_infos[_closeup_ch-1].xform;
		auto cube_off=_closeup_offset;
		auto cube_view=_closeup_cube.view<void>();
		auto cube_size=cube_view.sizes();

		auto chk_rng=[this,&xform,cube_off,&cube_view,cube_size](const auto& pos) {
			auto off=xform->to_offset_f(pos);
			for(unsigned int i=0; i<off.size(); ++i) {
				auto oi=off[i]-cube_off[i];
				if(oi<0)
					return false;
				if(oi>cube_size[i])
					return false;
			}
			return true;
		};
		sel_proofread_nodes(reader, _cur_pos, pick_tgt, sel_nodes, chk_rng, 30);
	}

	bool check_terminal() {
		// cur_conn
		if(!_prelock_model.can_write_later())
			return false;
		return true;
	}
	gapr::future<std::pair<SubmitRes, std::string>> start_terminal() {
		if(!check_terminal())
			return {};

		gapr::node_id nid{};
		gapr::edge_model::reader reader{_model};
		do {
			if(_cur_pos.edge) {
				auto& edg=reader.edges().at(_cur_pos.edge);
				nid=edg.nodes[_cur_pos.index/128];
				break;
			}
			nid=_cur_pos.vertex;
			if(!nid)
				break;
			auto vert=&reader.vertices().at(nid);
			if(!pred_empty_end{}(reader, nid, *vert))
				break;
			if(!check_vertex_raised(reader, _tier2, *vert))
				return {};

			_prelock_model.begin_write_later();
			update_state(StateMask::Model);

			gapr::fiber fib{gapr::app().io_context().get_executor(), [this,nid](gapr::fiber_ctx& ctx) {
				gapr::delta_add_prop_ delta;
				delta.link[0]=nid.data;
				delta.link[1]=0;
				delta.node=_cur_pos.point;
				delta.prop="state=end";
				return submit_commit(ctx, std::move(delta));
			}};
			return fib.get_future();
		} while(false);

		if(auto nid_next=find_next_node(reader, nid, gapr::node_attr{_cur_pos.point}, _tier2<=gapr::tier::annotator, _next_mask, _filter_bbox)) {
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
			autosel_for_proofread(reader);
			_ui.canvas->apply_changes();
			update_state(0);
			//XXX startDownloadCube();
		} else {
			window.on_goto_next_cube_triggered();
		}
		return {};
	}
	gapr::future<std::pair<SubmitRes, std::string>> start_end_as(std::string&& val) {
		// XXX
		if(!check_terminal())
			return {};

		gapr::node_id nid{};
		gapr::edge_model::reader reader{_model};
		do {
			if(_cur_pos.edge) {
				auto& edg=reader.edges().at(_cur_pos.edge);
				nid=edg.nodes[_cur_pos.index/128];
				break;
			}
			nid=_cur_pos.vertex;
			if(!nid)
				break;
			auto vert=&reader.vertices().at(nid);
			if(!pred_empty_end{}(reader, nid, *vert))
				break;
			if(!check_vertex_raised(reader, _tier2, *vert))
				return {};

			_prelock_model.begin_write_later();
			update_state(StateMask::Model);

			gapr::fiber fib{gapr::app().io_context().get_executor(), [this,nid,val=std::move(val)](gapr::fiber_ctx& ctx) {
				gapr::delta_add_prop_ delta;
				delta.link[0]=nid.data;
				delta.link[1]=0;
				delta.node=_cur_pos.point;
				delta.prop="state="+val;
				return submit_commit(ctx, std::move(delta));
			}};
			return fib.get_future();
		} while(false);

		if(auto nid_next=find_next_node(reader, nid, gapr::node_attr{_cur_pos.point}, _tier2<=gapr::tier::annotator, _next_mask, _filter_bbox)) {
			auto pos=reader.to_position(nid_next);
			if(pos.edge) {
				auto& e=reader.edges().at(pos.edge);
				jumpToPosition(Position{pos.edge, pos.index, e.points[pos.index/128]}, reader);
			} else {
				auto& v=reader.vertices().at(nid_next);
				jumpToPosition(Position{nid_next, v.attr.data()}, reader);
			}
			target_changed(Position{}, reader);
			_ui.canvas->apply_changes();
			update_state(0);
			//XXX startDownloadCube();
		} else {
			window.on_goto_next_cube_triggered();
		}
		return {};
	}
	void finish_terminal(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			window.critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			window.critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				if(!res.get().second.empty())
					window.critical_error("Error", QString::fromStdString(res.get().second), "");
				break;
			case SubmitRes::Accept:
				break;
			case SubmitRes::Retry:
				break;
		}
		_ui.canvas->apply_changes();
		_prelock_model.end_write_later();
		update_state(StateMask::Model);
	}
	struct sub_edge {
		gapr::edge_model::edge_id edge;
		uint32_t index0, index1;
	};
	sub_edge get_sub_edge() {
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
	bool check_delete() {
		// cur_conn
		if(!_prelock_model.can_write_later())
			return false;
		if(!get_sub_edge().edge)
			return false;
		return true;
	}
	gapr::future<std::pair<SubmitRes, std::string>> start_delete() {
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
		if(!check_edge_raised(reader, _tier2, edg))
			return {};
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
		for(auto n: nodes_del) {
			auto [it1, it2]=reader.props().per_node(gapr::node_id{n});
			for(auto it=it1; it!=it2; ++it)
				props.push_back(gapr::prop_id{it->first, std::string{it->second}}.data());
		}
		std::sort(props.begin(), props.end());

		_prelock_model.begin_write_later();
		update_state(StateMask::Model);

		gapr::fiber fib{gapr::app().io_context().get_executor(), [this,nodes=std::move(nodes),props=std::move(props)](gapr::fiber_ctx& ctx) {
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
	void finish_delete(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			window.critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			window.critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				if(!res.get().second.empty())
					window.critical_error("Error", QString::fromStdString(res.get().second), "");
				break;
			case SubmitRes::Accept:
				break;
			case SubmitRes::Retry:
				break;
		}
		_ui.canvas->apply_changes();
		_prelock_model.end_write_later();
		update_state(StateMask::Model);
	}
	void jump_next_node() {
		// check
		// cur_conn???
		//???if(!_prelock_model.can_write_later())
		gapr::node_id nid{};
		gapr::edge_model::reader reader{_model};
		do {
			if(_cur_pos.edge) {
				auto& edg=reader.edges().at(_cur_pos.edge);
				nid=edg.nodes[_cur_pos.index/128];
				_next_mask.emplace(nid);
				break;
			}
			nid=_cur_pos.vertex;
			if(!nid)
				break;
			_next_mask.emplace(nid);
		} while(false);
		{
			auto sub_edge=get_sub_edge();
			std::vector<node_id::data_type> nodes;
			if(sub_edge.edge) {
				auto& edg=reader.edges().at(sub_edge.edge);
				uint32_t idx=sub_edge.index0;
				do {
					gapr::node_attr pt{edg.points[idx]};
					_next_mask.emplace(edg.nodes[idx]);
					if(idx==sub_edge.index1)
						break;
					if(sub_edge.index1>sub_edge.index0)
						idx++;
					else
						idx--;
				} while(true);
			}
		}

		if(auto nid_next=find_next_node(reader, nid, gapr::node_attr{_cur_pos.point}, _tier2<=gapr::tier::annotator, _next_mask, _filter_bbox)) {
			auto pos=reader.to_position(nid_next);
			if(pos.edge) {
				auto& e=reader.edges().at(pos.edge);
				jumpToPosition(Position{pos.edge, pos.index, e.points[pos.index/128]}, reader);
			} else {
				auto& v=reader.vertices().at(nid_next);
				jumpToPosition(Position{nid_next, v.attr.data()}, reader);
			}
			target_changed(Position{}, reader);
			_ui.canvas->apply_changes();
			update_state(0);
		} else {
			// XXX enabled???
			window.on_goto_next_cube_triggered();
		}
	}
	void jump_next_cube() {
	}

	bool check_neuron_create() {
		// cur_conn
		if(!_prelock_model.can_write_later())
			return false;
		if(!_tgt_pos.valid())
			return false;
		return true;
	}

	gapr::future<std::pair<SubmitRes, std::string>> start_neuron_create(std::string&& name) {
		if(!check_neuron_create())
			return {};


		gapr::node_id nid{};
		gapr::edge_model::reader reader{_model};
		if(_tgt_pos.edge) {
			auto& edg=reader.edges().at(_tgt_pos.edge);
			if(!check_edge_raised(reader, _tier2, edg))
				return {};
			nid=edg.nodes[_tgt_pos.index/128];
		} else {
			if(!check_vert_raised(reader, _tier2, _tgt_pos.vertex))
				return {};
			nid=_tgt_pos.vertex;
		}
		auto it_root=reader.props().find(gapr::prop_id{nid, "root"});
		if(it_root!=reader.props().end())
			return {};
		_prelock_model.begin_write_later();
		update_state(StateMask::Model);
		//???list_ptr: point to created neuron
		gapr::fiber fib{gapr::app().io_context().get_executor(), [this,nid,name=std::move(name)](gapr::fiber_ctx& ctx) {
			gapr::delta_add_prop_ delta;
			delta.link[0]=nid.data;
			delta.link[1]=0;
			delta.node=_tgt_pos.point;
			delta.prop="root";
			if(!name.empty()) {
				delta.prop.push_back('=');
				delta.prop+=name;
			}
			return submit_commit(ctx, std::move(delta));
		}};
		return fib.get_future();
	}

	void finish_neuron_create(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			window.critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			window.critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				if(!res.get().second.empty())
					window.critical_error("Error", QString::fromStdString(res.get().second), "");
				break;
			case SubmitRes::Accept:
				break;
			case SubmitRes::Retry:
				break;
		}
		_ui.canvas->apply_changes();
		_prelock_model.end_write_later();
		update_state(StateMask::Model);
	}

	bool check_report_error() {
		// cur_conn
		if(!_prelock_model.can_write_later())
			return false;
		if(!_tgt_pos.valid())
			return false;
		return true;
	}

	gapr::future<std::pair<SubmitRes, std::string>> start_report_error(std::string&& name) {
		if(!check_report_error())
			return {};


		gapr::node_id nid{};
		gapr::edge_model::reader reader{_model};
		if(_tgt_pos.edge) {
			auto& edg=reader.edges().at(_tgt_pos.edge);
			if(!check_edge_raised(reader, _tier2, edg))
				return {};
			nid=edg.nodes[_tgt_pos.index/128];
		} else {
			if(!check_vert_raised(reader, _tier2, _tgt_pos.vertex))
				return {};
			nid=_tgt_pos.vertex;
		}
		auto it_root=reader.props().find(gapr::prop_id{nid, "error"});
		if(it_root!=reader.props().end())
			return {};
		_prelock_model.begin_write_later();
		update_state(StateMask::Model);
		//???list_ptr: point to created neuron
		gapr::fiber fib{gapr::app().io_context().get_executor(), [this,nid,name=std::move(name)](gapr::fiber_ctx& ctx) {
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

	void finish_report_error(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			window.critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			window.critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				if(!res.get().second.empty())
					window.critical_error("Error", QString::fromStdString(res.get().second), "");
				break;
			case SubmitRes::Accept:
				break;
			case SubmitRes::Retry:
				break;
		}
		_ui.canvas->apply_changes();
		_prelock_model.end_write_later();
		update_state(StateMask::Model);
	}

	bool check_resolve_error() {
		// cur_conn
		if(!_prelock_model.can_write_later())
			return false;
		if(!_tgt_pos.valid())
			return false;
		return true;
	}
	gapr::future<std::pair<SubmitRes, std::string>> start_resolve_error(std::string_view state) {
		if(!check_resolve_error())
			return {};

		gapr::node_id nid{};
		gapr::edge_model::reader reader{_model};
		if(_tgt_pos.edge) {
			auto& edg=reader.edges().at(_tgt_pos.edge);
			if(!check_edge_raised(reader, _tier2, edg))
				return {};
			nid=edg.nodes[_tgt_pos.index/128];
		} else {
			if(!check_vert_raised(reader, _tier2, _tgt_pos.vertex))
				return {};
			nid=_tgt_pos.vertex;
		}
		if(!nid)
			return {};
		if(auto it=reader.props().find({nid, "error"}); it!=reader.props().end()) {
			if(it->second==state)
				return {};
		} else {
			return {};
		}

		assert(nid);
		_prelock_model.begin_write_later();
		update_state(StateMask::Model);

		gapr::fiber fib{gapr::app().io_context().get_executor(), [this,nid,state](gapr::fiber_ctx& ctx) {
			gapr::delta_chg_prop_ delta;
			delta.node=nid.data;
			delta.prop="error";
			if(!state.empty()) {
				delta.prop.push_back('=');
				delta.prop+=state;
			}
			return submit_commit(ctx, std::move(delta));
		}};
		return fib.get_future();
	}
	void finish_resolve_error(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			window.critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			window.critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				if(!res.get().second.empty())
					window.critical_error("Error", QString::fromStdString(res.get().second), "");
				break;
			case SubmitRes::Accept:
				break;
			case SubmitRes::Retry:
				break;
		}
		_ui.canvas->apply_changes();
		_prelock_model.end_write_later();
		update_state(StateMask::Model);
	}

	bool check_examine() {
		// cur_conn
		if(!_prelock_model.can_write_later())
			return false;
		if(get_sub_edge().edge)
			return true;
		if(!_ui.canvas->selection().empty())
			return true;
		return false;
	}

	gapr::future<std::pair<SubmitRes, std::string>> start_examine() {
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
		if(auto& sel=_ui.canvas->selection(); !sel.empty()) {
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
			_ui.canvas->apply_changes();
			update_state(0);
			return {};
		}

		_prelock_model.begin_write_later();
		update_state(StateMask::Model|StateMask::Path);
		{
			_cur_fix=fix_pos;
			_tgt_fix=FixPos{nullptr};
		}

		gapr::fiber fib{gapr::app().io_context().get_executor(), [this,nodes=std::move(nodes)](gapr::fiber_ctx& ctx) {
			gapr::delta_proofread_ delta;
			delta.nodes=std::move(nodes);
			return submit_commit(ctx, std::move(delta));
		}};
		return fib.get_future();
	}
	void finish_examine(gapr::likely<std::pair<SubmitRes, std::string>>&& res) {
		if(!res)
			window.critical_error("Error", "load commit error", "");
		if(!model_apply(true))
			window.critical_error("Error", "model apply error", "");
		switch(res.get().first) {
			case SubmitRes::Deny:
				_path.nodes.clear();
				_ui.canvas->set_path_data({});
				if(!res.get().second.empty())
					window.critical_error("Error", QString::fromStdString(res.get().second), "");
				break;
			case SubmitRes::Accept:
				_path.nodes.clear();
				_ui.canvas->set_path_data({});
				_ui.canvas->clear_selection();
				break;
			case SubmitRes::Retry:
#if 0
				if(can_reuse) {
					fix;
				}
#endif
				_ui.canvas->set_path_data({});
				break;
		}
		_ui.canvas->apply_changes();
		_prelock_model.end_write_later();
		update_state(StateMask::Model|StateMask::Path);
	}

};

void Window::PRIV::setupUi() {
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
	_popup_canvas->addAction(_ui.reopen_error);
	_popup_canvas->addAction(_ui.resolve_error);
	//_popup_canvas->addSeparator();
	//_popup_canvas->addAction(actions[EDIT_ABORT]);

	_ui.statusbar->addPermanentWidget(new QLabel{_ui.statusbar}, 1);
	_progr=new QProgressBar{_ui.statusbar};
	_progr->setRange(0, 1000);
	_progr->setTextVisible(false);
	_ui.statusbar->addPermanentWidget(_progr, 0);

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
}

Window::Window():
	QMainWindow{nullptr},
	_priv{std::make_shared<PRIV>(*this)}
{
	/*! stage 0, window constructed */
	_priv->setupUi();
}

Window::~Window() {
	gapr::print("Window::~PRIV");
#if 0
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
#endif
}

void Window::set_args(Args&& args) {
	gapr::str_glue title{args.user, '@', args.host, ':', args.port, '/', args.group, "[*]"};
	setWindowTitle(QString::fromStdString(title.str()));
	_priv->_ui.tracing_examine->setEnabled(args.proofread);

	_priv->_args.emplace(std::move(args));
}

void Window::on_canvas_ready(std::error_code ec) {
	/*! stage 1, canvas initialized */

	gapr::print("canvas ready: ", ec.message());
	// XXX allow changing passwd when gl failed???
	if(ec)
		return critical_error(QStringLiteral("Unable to initialize canvas."), {}, QString::fromStdString(ec.message()));

	if(_priv->_args)
		return ba::post(gapr::app().io_context(), [this]() {
			_priv->start();
		});

	ba::post(gapr::app(), [this]() {
	auto dlg=std::make_unique<gapr::login_dialog>(this);
	dlg->setWindowModality(Qt::WindowModal);
	connect(dlg.get(), &QDialog::finished, this, &Window::on_login_dialog_finished);
	dlg->show();
	_priv->_dlg_login=std::move(dlg);
	});
	_priv->_ui.file_open->setEnabled(false);
}

void Window::critical_error(const QString& err, const QString& info, const QString& detail) {
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
}
void Window::warning_msg(const QString& err, const QString& info) {
	auto mbox=new QMessageBox{QMessageBox::Warning, QStringLiteral("Error"), err, QMessageBox::Close, this};
	if(!info.isEmpty()) {
		gapr::print("set info");
		mbox->setInformativeText(info);
	}
	mbox->setWindowModality(Qt::WindowModal);
	mbox->open();
}
void Window::show_retry_dlg(const QString& err, const QString& info, const QString& detail) {
	auto mbox=new QMessageBox{QMessageBox::Warning, QStringLiteral("Error"), err, QMessageBox::Close|QMessageBox::Retry, this};
	if(!info.isEmpty())
		mbox->setInformativeText(info);
	if(!detail.isEmpty())
		mbox->setDetailedText(detail);
	mbox->setWindowModality(Qt::WindowModal);
	connect(mbox, &QDialog::finished, this, &Window::show_retry_dlg_cb);
	mbox->open();
}

void Window::critical_error_cb(int result) {
	deleteLater();
}

void Window::ask_password(const QString& str, const QString& err) {
	auto dlg=std::make_unique<gapr::PasswordDialog>(err.isEmpty()?QStringLiteral("Authentication Required"):QStringLiteral("Login Error"), str, err, this);
	dlg->setWindowModality(Qt::WindowModal);
	connect(dlg.get(), &QDialog::finished, this, &Window::ask_password_cb);
	dlg->show();
	_priv->_dlg_pw=std::move(dlg);
}
void Window::ask_password_cb(int result) {
	auto dlg=_priv->_dlg_pw.release();
	dlg->deleteLater();
	if(result!=QDialog::Accepted)
		return deleteLater();

	auto pw=dlg->get_password();
	ba::post(gapr::app().io_context(), [this,
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
	ba::post(gapr::app().io_context(), [this]() { _priv->connect(); });
}

void Window::on_view_config_triggered() {
	//QDialog.parent=this;
	//XXX directly connect
	auto dlg=_priv->_dlg_display;
	if(dlg)
		dlg->show();
}

void Window::PRIV::update_state(StateMask::base_type chg) {
	if(chg&(StateMask::Model|StateMask::Path|StateMask::CurPos|StateMask::TgtPos|StateMask::ViewMode|StateMask::Cube)) {
		//_ui.tracing_connect->setEnabled(check_connect());
	}
}

void Window::enter_stage2() {
	/*! after login */
	// XXX allow selecting repo, and changing passwd
}
void Window::enter_stage3() {
	/*! catalog and state loaded */
	_priv->postConstruct();
	/////////////////////////////////////////////
#if 0
	do {
		auto fut=_priv->start_load_cube();
		if(!fut)
			break;
		auto ex2=gapr::app().get_executor();
		std::move(fut).async_wait(ex2, [priv=_priv](gapr::likely<gapr::delta_add_edge_>&& res) {
			priv->finish_load_cube(std::move(res));
			auto fut=_priv->start_load_cube();
			if(!fut)
				break;
		});
	} while(false);
	//start_xxx();
	if(!fut)
		return;
	fut.wait(...);
	//
	//////////////
	//////
		

		update_state();
		////////////////////////
		//
		//graph
		//

		/////////////
#endif
		auto fut=_priv->load_latest_commits();
		if(!fut)
			return;
		auto ex2=gapr::app().get_executor();
		std::move(fut).async_wait(ex2, [priv=_priv](gapr::likely<bool>&& res) {
			priv->latest_commits_loaded(std::move(res));
			auto fut=priv->find_seed_pos();
			auto ex2=gapr::app().get_executor();
			if(fut)
				std::move(fut).async_wait(ex2, [priv=std::move(priv)](gapr::likely<bool>&& res) {
					priv->present_cube(std::move(res));
				});
		});
}


void Window::on_xfunc_closeup_changed(double low, double up) {
	_priv->xfunc_closeup_changed(low, up);
}

void Window::on_file_open_triggered() {
	assert(_priv->_ui.file_open->isEnabled());

	auto dlg=std::make_unique<gapr::login_dialog>(this);
	dlg->setWindowModality(Qt::WindowModal);
	connect(dlg.get(), &QDialog::finished, this, &Window::on_login_dialog_finished);
	dlg->show();
	_priv->_dlg_login=std::move(dlg);
	_priv->_ui.file_open->setEnabled(false);
}

void Window::on_login_dialog_finished(int result) {
	assert(!_priv->_ui.file_open->isEnabled());
	auto dlg=_priv->_dlg_login.release();
	dlg->deleteLater();
	if(result!=QDialog::Accepted)
		return _priv->_ui.file_open->setEnabled(true);

	auto& args=_priv->_args;
	args.emplace();
	dlg->get(args->user, args->host, args->port, args->group, args->passwd);
	args->proofread=true;

	gapr::str_glue title{args->user, '@', args->host, ':', args->port, '/', args->group, "[*]"};
	setWindowTitle(QString::fromStdString(title.str()));
	_priv->_ui.tracing_examine->setEnabled(args->proofread);

	ba::post(gapr::app().io_context(), [this]() {
		_priv->start();
	});
}
void Window::on_file_close_triggered() {
	close();
}
void Window::on_file_launch_triggered() {
	gapr::app().show_start_window(this);
}
void Window::on_file_options_triggered() {
	gapr::app().show_options_dialog(*this);
}
void Window::on_file_quit_triggered() {
	gapr::app().request_quit();
}
void Window::on_goto_target_triggered() {
	//not SessionState::Invalid: SessionState::LoadingCatalog: SessionState::Readonly:
	if(!_priv->_tgt_pos.valid())
		return;
	edge_model::reader reader{_priv->_model};
	_priv->jumpToPosition(_priv->_tgt_pos, reader);
	//_priv->updateActions();
	_priv->startDownloadCube();
}
void Window::on_pick_current_triggered() {
	//not SessionState::Invalid: SessionState::LoadingCatalog: SessionState::Readonly:
	if(!_priv->_cur_pos.valid())
		return;
	_priv->pick_position(_priv->_cur_pos);
	//priv->viewer->update();
	//priv->updateActions();
}
void Window::on_goto_next_node_triggered() {
	_priv->jump_next_node();
}
void Window::on_goto_next_cube_triggered() {
	// XXX select and update???
	auto fut=_priv->load_latest_commits();
	if(!fut)
		return;
	auto ex2=gapr::app().get_executor();
	std::move(fut).async_wait(ex2, [priv=_priv](gapr::likely<bool>&& res) {
		priv->latest_commits_loaded(std::move(res));
		auto fut=priv->find_seed_pos();
		auto ex2=gapr::app().get_executor();
		if(fut)
			std::move(fut).async_wait(ex2, [priv=std::move(priv)](gapr::likely<bool>&& res) {
				priv->present_cube(std::move(res));
			});
	});
}

void Window::on_neuron_create_triggered() {
	auto fut=_priv->start_neuron_create({});
	if(!fut)
		return;
	auto ex2=gapr::app().get_executor();
	std::move(fut).async_wait(ex2, [priv=_priv](auto&& res) {
		priv->finish_neuron_create(std::move(res));
	});
}
void Window::on_report_error_triggered() {
	//experianced users can add "fixed" and then fix?
	auto fut=_priv->start_report_error(_priv->_tier2>gapr::tier::annotator?"":"deferred");
	if(!fut)
		return;
	auto ex2=gapr::app().get_executor();
	std::move(fut).async_wait(ex2, [priv=_priv](auto&& res) {
		priv->finish_report_error(std::move(res));
	});
}
void Window::on_reopen_error_triggered() {
	auto fut=_priv->start_resolve_error(_priv->_tier2>gapr::tier::annotator?"":"deferred");
	if(!fut)
		return;
	auto ex2=gapr::app().get_executor();
	std::move(fut).async_wait(ex2, [priv=_priv](auto&& res) {
		priv->finish_resolve_error(std::move(res));
	});
}

void Window::on_resolve_error_triggered() {
	auto dlg=std::make_unique<gapr::ErrorStateDialog>(this);
	dlg->setWindowModality(Qt::WindowModal);
	dlg->disable_wontfix();
	connect(dlg.get(), &QDialog::finished, this, &Window::error_state_dialog_finished);
	dlg->show();
	_priv->_dlg_err=std::move(dlg);
}
void Window::error_state_dialog_finished(int result) {
	auto dlg=_priv->_dlg_err.release();
	dlg->deleteLater();
	if(result!=QDialog::Accepted)
		return;
	auto fut=_priv->start_resolve_error(dlg->state());
	if(!fut)
		return;
	auto ex2=gapr::app().get_executor();
	std::move(fut).async_wait(ex2, [priv=_priv](auto&& res) {
		priv->finish_resolve_error(std::move(res));
	});
}

void Window::on_view_slice_toggled(bool checked) {
	auto& canvas=*_priv->_ui.canvas;
	canvas.set_slice_mode(checked);
	canvas.apply_changes();
}
void Window::on_view_data_only_toggled(bool checked) {
	auto& canvas=*_priv->_ui.canvas;
	canvas.set_data_only(checked);
	canvas.apply_changes();
}
void Window::on_tracing_connect_triggered() {
	_priv->tracing_connect();
}
void Window::on_tracing_extend_triggered() {
	auto fut=_priv->start_extend();
	gapr::print("start_extend ", static_cast<bool>(fut));
	if(!fut)
		return;
	auto ex2=gapr::app().get_executor();
	std::move(fut).async_wait(ex2, [priv=_priv](auto&& res) {
		gapr::print("finish_extend");
		priv->finish_extend(std::move(res));
	});
}

void Window::on_tracing_branch_triggered() {
	auto fut=_priv->start_branch();
	gapr::print("start_branch");
	if(!fut)
		return;
	auto ex2=gapr::app().get_executor();
	std::move(fut).async_wait(ex2, [priv=_priv](auto&& res) {
		gapr::print("finish_branch");
		priv->finish_extend(std::move(res));
	});
}
void Window::on_tracing_end_triggered() {
	auto fut=_priv->start_terminal();
	if(!fut)
		return;
	auto ex2=gapr::app().get_executor();
	std::move(fut).async_wait(ex2, [priv=_priv](auto&& res) {
		priv->finish_terminal(std::move(res));
	});
}
void Window::on_tracing_end_as_triggered() {
	if(!_priv->check_terminal())
		return;

	auto dlg=std::make_unique<EndAsDialog>(this);
	dlg->setWindowModality(Qt::WindowModal);
	connect(dlg.get(), &QDialog::finished, this, &Window::on_end_as_dialog_finished);
	dlg->show();
	_priv->_dlg_end_as=std::move(dlg);
}
void Window::on_tracing_delete_triggered() {
	auto fut=_priv->start_delete();
	if(!fut)
		return;
	auto ex2=gapr::app().get_executor();
	std::move(fut).async_wait(ex2, [priv=_priv](auto&& res) {
		priv->finish_delete(std::move(res));
	});
}
void Window::on_tracing_examine_triggered() {
	auto fut=_priv->start_examine();
	if(!fut)
		return;
	auto ex2=gapr::app().get_executor();
	std::move(fut).async_wait(ex2, [priv=_priv](auto&& res) {
		priv->finish_examine(std::move(res));
	});
}

void Window::on_end_as_dialog_finished(int result) {
	auto dlg=_priv->_dlg_end_as.release();
	dlg->deleteLater();
	if(result!=QDialog::Accepted)
		return;
	auto st=dlg->_ui.state->text().toStdString();
	auto fut=_priv->start_end_as(std::move(st));
	if(!fut)
		return;
	auto ex2=gapr::app().get_executor();
	std::move(fut).async_wait(ex2, [priv=_priv](auto&& res) {
		priv->finish_terminal(std::move(res));
	});
}
void Window::on_help_manual_triggered() {
	gapr::app().display_help(*this, "fix");
}
void Window::on_help_about_triggered() {
	//XXX
	const auto& icon=_priv->_ui.help_about->icon();

	for(auto s: icon.availableSizes())
		gapr::print("size: ", s.width(), 'x', s.height());

	gapr::app().show_about_dialog(*this);
}
void Window::closeEvent(QCloseEvent* event) {
#if 0
  void MainWindow::readSettings()
  {
      QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
      const QByteArray geometry = settings.value("geometry", QByteArray()).toByteArray();
      if (geometry.isEmpty()) {
          const QRect availableGeometry = QApplication::desktop()->availableGeometry(this);
          resize(availableGeometry.width() / 3, availableGeometry.height() / 2);
          move((availableGeometry.width() - width()) / 2,
               (availableGeometry.height() - height()) / 2);
      } else {
          restoreGeometry(geometry);
      }
  }
  void MainWindow::writeSettings()
  {
      QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
      settings.setValue("geometry", saveGeometry());
  }
//******window positions;
//********recently used files,
	if(maybeSave()) {
		QSettings settings("MyCompany", "MyApp");
		settings.setValue("geometry", saveGeometry());
		settings.setValue("windowState", saveState());
		event->accept();
	} else {
		event->ignore();
	}
#endif
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
void Window::on_canvas_pick_changed() {
	_priv->pick_position();
	//_priv->updateActions();
	//_priv->connectIfPossible();
	////
}
void Window::on_canvas_selection_changed() {
	_priv->_ui.canvas->set_target(Position{});
	_priv->_ui.canvas->apply_changes();
}
void Window::on_canvas_customContextMenuRequested(const QPoint& pos) {
	// XXX directly connect
	gapr::print("context menu");
	_priv->_popup_canvas->popup(_priv->_ui.canvas->mapToGlobal(pos));
}
void Window::on_view_refresh_triggered() {
	_priv->startDownloadCube();
	//reload meshes
	
	auto fut=_priv->start_refresh();
	gapr::print("start_refresh");
	if(!fut)
		return;
	auto ex2=gapr::app().get_executor();
	std::move(fut).async_wait(ex2, [priv=_priv](auto&& res) {
		gapr::print("finish_refresh");
		priv->finish_refresh(std::move(res));
	});
}

void Window::on_select_closeup_currentIndexChanged(int index) {
	return _priv->select_closeup_changed(index);
}
void Window::on_select_scale_currentIndexChanged(int index) {
	auto f=_priv->_ui_display.select_scale->itemData(index).value<int>();
	_priv->_ui.canvas->set_scale_factor(f);
	_priv->_ui.canvas->apply_changes();
}
void Window::on_total_slices_valueChanged(int value) {
	unsigned int v1=value;
	auto v0=_priv->_ui.canvas->slice_pars()[0];
	if(v0>v1) {
		v0=v1;
		QSignalBlocker _blk{_priv->_ui_display.shown_slices};
		_priv->_ui_display.shown_slices->setValue(v0);
	}
	_priv->_ui.canvas->set_slice_pars({v0, v1});
	_priv->_ui.canvas->apply_changes();
}

void Window::on_shown_slices_valueChanged(int value) {
	unsigned int v0=value;
	auto v1=_priv->_ui.canvas->slice_pars()[1];
	if(v1<v0) {
		v1=v0;
		QSignalBlocker _blk{_priv->_ui_display.total_slices};
		_priv->_ui_display.total_slices->setValue(v1);
	}
	_priv->_ui.canvas->set_slice_pars({v0, v1});
	_priv->_ui.canvas->apply_changes();
}
void Window::on_quality_button_box_accepted() {
	QSettings settings{this};
	settings.beginGroup(QStringLiteral("fix"));
	settings.setValue(QStringLiteral("scale-factor"), _priv->_ui_display.select_scale->currentData());
	auto x=_priv->_ui_display.shown_slices->value();
	auto y=_priv->_ui_display.total_slices->value();
	settings.setValue(QStringLiteral("slice-params"), QPoint{x, y});
	settings.endGroup();
}

#if 0
class ViewerVolumeOptions: public OptionsPage {
}
void SessionChannelOptions::saveConfig(SessionPriv* _vp, Options* options) const {
	for(int i=0; i<3; i++) {
		if(_vp->cube_maps[i]>0) {
			options->setInt(QString{"channel.map.%1"}.arg(i), _vp->cube_maps[i]);
			options->setXfunc(QString{"channel.xfunc.%1"}.arg(_vp->cube_maps[i]), _vp->xfuncs[i].first, _vp->xfuncs[i].second);
		} else {
			options->removeKey(QString{"channel.map.%1"}.arg(i));
		}
	}
}
void SessionChannelOptions::saveXfuncs(SessionPriv* _vp) {
	auto options=&_vp->options;
	for(int i=0; i<xfuncsSaved.size(); i++) {
		options->setXfunc(QString{"channel.xfunc.%1"}.arg(i), xfuncsSaved[i].first, xfuncsSaved[i].second);
	}
}
#endif

#include "window.moc"

