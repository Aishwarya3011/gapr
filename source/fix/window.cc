#ifdef _WIN64
#include <winsock2.h>
#endif
#define _USE_MATH_DEFINES

#include <QOpenGLFunctions_3_3_Core>
#define GAPR_OPENGL_USE_THIS
#include <QPointer>
#include <QMenu>
#include <QFile>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QWindow>

#include "window.hh"

#include "session-impl.hh"

class OpenGLFunctions: protected QOpenGLFunctions_3_3_Core {
	protected:
		void initialize() {
			if(!initializeOpenGLFunctions()) {
#if 0
				QOpenGLFunctions f;
				f.initializeOpenGLFunctions();
				auto v=f.glGetString(GL_VERSION);
				gapr::report("OpenGL 3.3 Core not available. (", v, ')');
#endif
				throw std::runtime_error{"failed to initialize opengl functions"};
			}
		}
};

class gapr::fix::Window::Adapter: protected OpenGLFunctions {
	protected:
		Adapter() { }
		~Adapter() { }
		void constructed(Window* window) {
			_window=window;
			_weak_ptr=QPointer{window};
		}

		struct Resources {
			struct Bytes: QByteArray {
				Bytes(QByteArray&& b): QByteArray{std::move(b)} { }
				operator bool() { return !isEmpty(); }
			};
			Bytes load(const char* path) {
				QString pfx{":"};
				pfx+=path;
				QFile f{pfx};
				if(!f.open(QIODevice::ReadOnly|QIODevice::Text))
					return QByteArray{};
				return f.readAll();
			}
		};

		Resources resources() { return Resources{}; }
		gapr::Application::executor_type ui_executor() const noexcept {
			return gapr::app().get_executor();
		}
		boost::asio::thread_pool& thread_pool() const noexcept {
			return gapr::app().thread_pool();
		}
		boost::asio::io_context& io_context() const noexcept {
			return gapr::app().io_context();
		}
		boost::asio::ssl::context& ssl_context() {
			return gapr::app().ssl_context();
		}

		void window_title(const std::string_view title) {
			_window->setWindowTitle(QString::fromUtf8(title.data(), title.size()));
		}
		void show_login_info(const std::string_view addr, const std::string_view user, const std::string_view tier,
				const std::string_view stage, const std::string_view tier2) {
			_window->_ui_property.addr->setText(QString::fromUtf8(addr.data(), addr.size()));
			_window->_ui_property.user->setText(QString::fromUtf8(user.data(), user.size()));
			_window->_ui_property.tier->setText(QString::fromUtf8(tier.data(), tier.size()));
			_window->_ui_property.stage->setText(QString::fromUtf8(stage.data(), stage.size()));
			_window->_ui_property.tier2->setText(QString::fromUtf8(tier2.data(), tier2.size()));
		}
		void opengl_error(const std::string_view msg) {
			critical_error("Unable to initialize canvas.", {}, msg);
		}
		void opengl_error(std::error_code ec, const std::string_view msg) {
			return opengl_error(msg);
		}
		void critical_error(const std::string_view err, const std::string_view info, const std::string_view detail) {
			auto err2=QString::fromUtf8(err.data(), err.size());
			auto info2=QString::fromUtf8(info.data(), info.size());
			auto detail2=QString::fromUtf8(detail.data(), detail.size());
			_window->critical_error(err2, info2, detail2);
		}
		void mark_busy() { QGuiApplication::setOverrideCursor(Qt::WaitCursor); }
		void unmark_busy() { QGuiApplication::restoreOverrideCursor(); }
		void make_current() const { _window->_ui.canvas->makeCurrent(); }
		void clear_current() const { _window->_ui.canvas->doneCurrent(); }
		void canvas_update() const { _window->_ui.canvas->update(); }

		void display_login() { return _window->display_login(); }
		void ui_ask_password(const std::string_view msg, const std::string_view err) const {
			auto err2=QString::fromUtf8(err.data(), err.size());
			auto msg2=QString::fromUtf8(msg.data(), msg.size());
			_window->ask_password(msg2, err2);
		}
		void ui_critical_error(const std::string_view err, const std::string_view info, const std::string_view detail) const {
			auto err2=QString::fromUtf8(err.data(), err.size());
			auto info2=QString::fromUtf8(info.data(), info.size());
			auto detail2=QString::fromUtf8(detail.data(), detail.size());
			_window->critical_error(err2, info2, detail2);
		}
		void ui_message(const std::string_view msg) const {
			auto msg2=QString::fromUtf8(msg.data(), msg.size());
			_window->show_message(msg2);
		}
		void ui_ask_retry(const std::string_view err, const std::string_view info, const std::string_view detail) const {
			auto err2=QString::fromUtf8(err.data(), err.size());
			auto info2=QString::fromUtf8(info.data(), info.size());
			auto detail2=QString::fromUtf8(detail.data(), detail.size());
			_window->show_retry_dlg(err2, info2, detail2);
		}
		void ui_init_list() {
			_window->init_list();
		}
		void ui_list_select(gapr::node_id root) {
			_window->list_select(root);
		}
		void ui_list_update(gapr::edge_model::updater& updater) {
			_window->_list_model->update(updater);
		}
		void ui_set_description(const std::string_view desc) {
			_window->_ui_property.pos_info->setText(QString::fromUtf8(desc.data(), desc.size()));
		}
		void ui_set_statistics(const std::string_view stats) {
			_window->_ui_property.model_stats->setText(QString::fromUtf8(stats.data(), stats.size()));
			_window->_ui_property.update_details->setEnabled(true);
		}
		void ui_set_detailed_statistics(const std::string_view details) {
			auto e=_window->_ui_property.model_stats;
			auto s=e->toPlainText();
			s+="\n\n";
			s.append(QString::fromUtf8(details.data(), details.size()));
			e->setText(s);
		}
		void ui_canvas_cursor(bool has_model, bool can_write) {
			auto curs=Qt::CrossCursor;
			if(!has_model)
				curs=Qt::ForbiddenCursor;
			else if(!can_write)
				curs=Qt::BusyCursor;
			_window->_ui.canvas->setCursor(curs);
		}
		class ComboHelper {
			public:
				ComboHelper(QComboBox* c): c{c}, b{c} {
					add("Disabled", 0, false);
				}
				void add(std::string_view name, unsigned int id, bool sel) {
					c->addItem(QString::fromUtf8(name.data(), name.size()), id);
					if(sel)
						c->setCurrentIndex(c->count()-1);
				}
			private:
				QComboBox* c;
				QSignalBlocker b;
		};
		ComboHelper ui_combo_global_init() {
			return {_window->_ui_channels.select_global};
		}
		ComboHelper ui_combo_closeup_init() {
			return {_window->_ui_channels.select_closeup};
		}
		void ui_enable_channels(bool a, bool b) {
			_window->_ui_channels.frame_global->setEnabled(a);
			_window->_ui_channels.frame_closeup->setEnabled(b);
		}
		void ui_enable_global_xfunc(const std::array<double, 4>& s) {
			auto x=_window->_ui_channels.xfunc_global;
			x->set_state(s);
			x->setEnabled(true);
		}
		void ui_disable_global_xfunc() {
			_window->_ui_channels.xfunc_global->setEnabled(false);
		}
		void ui_enable_closeup_xfunc(const std::array<double, 4>& s) {
			auto x=_window->_ui_channels.xfunc_closeup;
			x->set_state(s);
			x->setEnabled(true);
		}
		void ui_disable_closeup_xfunc() {
			_window->_ui_channels.xfunc_closeup->setEnabled(false);
		}
		void ui_xfunc_set_default(unsigned int i) {
			_window->_ui.xfunc_slave->set_master(i?_window->_ui_channels.xfunc_closeup:_window->_ui_channels.xfunc_global);
		}
		std::array<double, 2> ui_closeup_xfunc_minmax() {
			auto x=_window->_ui_channels.xfunc_closeup;
			return {x->minimum(), x->maximum()};
		}
		std::array<double, 2> ui_global_xfunc_minmax() {
			auto x=_window->_ui_channels.xfunc_global;
			return {x->minimum(), x->maximum()};
		}
		void ui_adjust_shown_slices(unsigned int v) {
			auto s=_window->_ui_quality.shown_slices;
			QSignalBlocker _blk{s};
			s->setValue(v);
		}
		void ui_adjust_total_slices(unsigned int v) {
			auto s=_window->_ui_quality.total_slices;
			QSignalBlocker _blk{s};
			s->setValue(v);
		}

		void enable_file_open(bool v) {
			_window->_ui.file_open->setEnabled(false);
		}
		void canvas_bind_default_fb() {
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _window->_ui.canvas->defaultFramebufferObject());
		}
		void ui_enable_goto_position(int enabled) {
			_window->_ui.goto_position->setEnabled(enabled);
			_window->_ui.next_error->setEnabled(enabled);
		}
		void ui_enable_goto_target(int enabled) {
			_window->_ui.goto_target->setEnabled(enabled);
		}
		void ui_enable_pick_current(int enabled) {
			_window->_ui.pick_current->setEnabled(enabled);
		}
		void ui_enable_clear_end(int enabled) {
			_window->_ui.clear_state->setEnabled(enabled);
		}
		void ui_enable_resolve_error(int enabled) {
			_window->_ui.resolve_error->setEnabled(enabled);
		}
		void ui_enable_report_error(int enabled) {
			_window->_ui.report_error->setEnabled(enabled);
		}
		void ui_enable_raise_node(int enabled) {
			_window->_ui.raise_node->setEnabled(enabled);
		}
		void ui_enable_select_noise(int enabled) {
			_window->_ui.select_noise->setEnabled(enabled);
		}
		void ui_enable_autosel_length(int enabled) {
			_window->_ui.autosel_length->setEnabled(enabled);
		}
		void ui_enable_tracing_branch(int enabled) {
			_window->_ui.tracing_branch->setEnabled(enabled);
		}
		void ui_enable_tracing_extend(int enabled) {
			_window->_ui.tracing_extend->setEnabled(enabled);
		}
		void ui_enable_tracing_attach(int enabled) {
			_window->_ui.tracing_attach->setEnabled(enabled);
		}
		void ui_enable_tracing_end(int enabled) {
			_window->_ui.tracing_end->setEnabled(enabled);
		}
		void ui_enable_tracing_end_as(int enabled) {
			_window->_ui.tracing_end_as->setEnabled(enabled);
		}
		void ui_enable_tracing_delete(int enabled) {
			_window->_ui.tracing_delete->setEnabled(enabled);
		}
		void ui_enable_tracing_examine(int enabled) {
			_window->_ui.tracing_examine->setEnabled(enabled);
		}
		void ui_enable_file_props(int enabled) {
			_window->_ui.file_props->setEnabled(enabled);
		}
		void ui_enable_view_mode(int enabled) {
			_window->_ui.view_global->setEnabled(enabled&1);
			_window->_ui.view_closeup->setEnabled(enabled&2);
			_window->_ui.view_mixed->setEnabled(enabled&2);
		}
		void ui_enable_view_channels(int enabled) {
			_window->_ui.view_channels->setEnabled(enabled);
		}
		void ui_enable_file_open(int enabled) {
			_window->_ui.file_open->setEnabled(enabled);
		}
		void ui_enable_view_data_only(int enabled) {
			_window->_ui.view_data_only->setEnabled(enabled);
		}
		void ui_enable_view_slice(int enabled) {
			_window->_ui.view_slice->setEnabled(enabled);
		}
		void ui_enable_file_close(int enabled) {
			_window->_ui.file_close->setEnabled(enabled);
		}
		void ui_enable_view_refresh(int enabled) {
			_window->_ui.view_refresh->setEnabled(enabled);
		}
		void ui_enable_create_neuron(int enabled) {
			_window->_ui.neuron_create->setEnabled(enabled);
		}
		void ui_enable_rename_neuron(int enabled) {
			_window->_ui.neuron_rename->setEnabled(enabled);
		}
		void ui_enable_remove_neuron(int enabled) {
			_window->_ui.neuron_remove->setEnabled(enabled);
		}
		void ui_enable_tracing_connect(int enabled) {
			_window->_ui.tracing_connect->setEnabled(enabled);
		}
		void ui_enable_view_highlight(int enabled) {
			if(enabled<0) {
				_window->_ui.view_hl_loop->setEnabled(false);
				_window->_ui.view_hl_upstream->setEnabled(false);
				_window->_ui.view_hl_downstream->setEnabled(false);
				_window->_ui.view_hl_neuron->setEnabled(false);
				_window->_ui.view_hl_raised->setEnabled(false);
				_window->_ui.view_hl_orphan->setEnabled(false);
				_window->_ui.view_hl_reset->setEnabled(false);
			} else {
				_window->_ui.view_hl_loop->setEnabled(enabled);
				_window->_ui.view_hl_upstream->setEnabled(enabled);
				_window->_ui.view_hl_downstream->setEnabled(enabled);
				_window->_ui.view_hl_neuron->setEnabled(enabled);
				_window->_ui.view_hl_raised->setEnabled(enabled);
				_window->_ui.view_hl_orphan->setEnabled(enabled);
				_window->_ui.view_hl_reset->setEnabled(!enabled);
			}
		}
		void ui_indicate_can_edit(int v) {
			auto icon=QIcon::fromTheme(v?"changes-allow-symbolic":"changes-prevent-symbolic");
			auto l=_window->_ro_indicator;
			l->setPixmap(icon.pixmap(l->sizeHint()));
		}
		void ui_show_progress(int v) {
			auto w=_window->_loading_progr;
			w->setEnabled(v);
			if(!v)
				w->setValue(0);
		}
		void ui_update_progress(int v) {
			_window->_loading_progr->setValue(v);
		}

	private:
		gapr::fix::Window* _window;
		QPointer<gapr::fix::Window> _weak_ptr;

		void post_io() {
			//post io capture[std::shared_ptr<Session>]
		}
		void post_ui() {
			//post ui capture[sess s]() {
			//win  **!!w(!cc/!!set) .lock
			//  
			//  base
			//  memb
			//}
		}
};

using Window=gapr::fix::Window;

Window::Window(Session* session):
	QMainWindow{nullptr}, _session{session->shared_from_this()}
{
	_dlg_channels=new QDialog{this};
	_ui_channels.setupUi(_dlg_channels);

	_dlg_quality=new QDialog{this};
	_ui_quality.setupUi(_dlg_quality);

	_dlg_property=new QDialog{this};
	_ui_property.setupUi(_dlg_property);

	_ui.setupUi(this);
	_ui.canvas->bind(session);

	_ro_indicator=new QLabel{this};
	_loading_progr=new QProgressBar{this};
	_loading_progr->setTextVisible(false);
	_loading_progr->setMaximumWidth(128);
	_loading_progr->setMaximum(1000);
	_ui.statusbar->addPermanentWidget(_ro_indicator, 0);
	_ui.statusbar->addPermanentWidget(_loading_progr, 0);

	_popup_canvas=new QMenu{QStringLiteral("&Canvas"), this};
	_popup_canvas->addAction(_ui.goto_target);
	_popup_canvas->addAction(_ui.pick_current);
	_popup_canvas->addAction(_ui.goto_position);
	_popup_canvas->addSeparator();
	_popup_canvas->addAction(_ui.neuron_create);
	_popup_canvas->addSeparator();
	_popup_canvas->addAction(_ui.resolve_error);
	_popup_canvas->addSeparator();
	_popup_canvas->addAction(_ui.raise_node);
	//_popup_canvas->addSeparator();
	//_popup_canvas->addAction(actions[EDIT_ABORT]);

	_popup_list=new QMenu{QStringLiteral("&Neuron"), this};
	_popup_list->addAction(_ui.neuron_create);
	_popup_list->addAction(_ui.neuron_rename);
	_popup_list->addAction(_ui.neuron_remove);

	int scale_factor;
	std::array<unsigned int, 2> slice_params;
	{
		QSettings settings{this};
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

		if(auto v=settings.value(QStringLiteral("autosel-length")); v.canConvert<double>()) {
			session->set_autosel_len(v.value<double>());
		} else {
			session->set_autosel_len(30);
		}
		settings.endGroup();
	}

	{
		auto scale=scale_factor<0?1.0/(1-scale_factor):scale_factor+1;
		int k{-1};
		double match{INFINITY};
		QSignalBlocker _blk{_ui_quality.select_scale};
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
			_ui_quality.select_scale->addItem(name, QVariant{f});
			auto m=std::abs(std::log(ff/scale));
			if(m<match) {
				match=m;
				k=_ui_quality.select_scale->count()-1;
				scale_factor=f;
			}
		}
		if(k!=-1)
			_ui_quality.select_scale->setCurrentIndex(k);
		else
			_ui_quality.select_scale->setEnabled(false);
	}
	{
		QSignalBlocker _blk0{_ui_quality.total_slices};
		QSignalBlocker _blk1{_ui_quality.shown_slices};
		_ui_quality.total_slices->setMaximum(MAX_SLICES);
		_ui_quality.total_slices->setMinimum(0);
		_ui_quality.total_slices->setValue(slice_params[1]);
		_ui_quality.shown_slices->setMaximum(MAX_SLICES);
		_ui_quality.shown_slices->setMinimum(0);
		_ui_quality.shown_slices->setValue(slice_params[0]);
	}
}

Window::~Window() {
	gapr::print("Window::~PRIV");
#if 0
	auto loadThread=_priv->_cube_builder;
	if(loadThread) {
		if(loadThread->isRunning()) {
			gapr::print("stop load thread");
			loadThread->stop();
			if(!loadThread->wait()) {
				loadThread->terminate();
			}
		}
		delete loadThread;
	}
#endif
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


void Window::show_retry_dlg_cb(int result) {
	if(result!=QMessageBox::Retry) {
		deleteLater();
		return;
	}
	_session->retry_connect();
}


void Window::enter_stage2() {
	/*! after login */
	// XXX allow selecting repo, and changing passwd
}

void Window::on_file_open_triggered() {
#if 0
	void MainWindow::open()
	{
		if (maybeSave()) {
			QString fileName = QFileDialog::getOpenFileName(this);
			if (!fileName.isEmpty())
		}
	}
	//QDialog.parent=this;
#endif
	assert(_ui.file_open->isEnabled());

	auto dlg=std::make_unique<gapr::login_dialog>(this);
	dlg->setWindowModality(Qt::WindowModal);
	connect(dlg.get(), &QDialog::finished, this, &Window::login_dialog_finished);
	dlg->show();
	_dlg_login=std::move(dlg);
	_ui.file_open->setEnabled(false);
}

void Window::login_dialog_finished(int result) {
	assert(!_ui.file_open->isEnabled());
	auto dlg=_dlg_login.release();
	dlg->deleteLater();
	if(result!=QDialog::Accepted)
		return _ui.file_open->setEnabled(true);

	gapr::fix::Session::Args args;
	dlg->get(args.user, args.host, args.port, args.group, args.passwd);
	_session->args(std::move(args));
	_session->login();
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

#if 0

	void dropEvent(QDropEvent* drop) override;
	void dragEnterEvent(QDragEnterEvent* drop) override;


	public:

	static ViewerShared* createShared();
	static void destroyShared(ViewerShared* viewerShared);

	void setProgressData(const std::vector<int32_t>& dat);


	bool clearSelection();


	void attachOptionsDlg();
	void detachOptionsDlg();

	QImage takeScreenshot();
	void toggleRotate();


#endif



using Canvas=gapr::fix::Canvas;

/*
l click pickpos/picknode/subedge_sel(chg tgt)
l click-drag-release rotate/?drag_obj
m rot zoom/scroll
r click popup
m click-drag-release ?Pan(chg pos)
*/

/* ???
 * multisampling
 * offscreen rendering on worker threads, to gen. texture
 * shared texture
 * asynchronous texture uploads
 */

/*! dpr scale fbo  edge msaa
 *  1   1     640  1    1
 *  1   2     320  1    1
 *  1   4     160  1    1
 *  1   1/2   640  1    2
 *  1   1/4   640  1    4
 *  2   1     1280 2    1
 *  2   2     640  1    1
 *  2   4     320  1    1
 *  2   1/2   1280 2    2
 *  2   1/4   1280 2    4
 *  4   1     2560 4    1
 *  4   2     1280 2    1
 *  4   4     640  1    1
 *  4   1/2   2560 4    2
 */

Canvas::Canvas(QWidget* parent):
	QOpenGLWidget{parent}
{
	gapr::print("canvas ctor");
	//setContextMenuPolicy(PreventContextMenu);

	//setMinimumSize(QSize(480, 320));
	//setFocusPolicy(Qt::WheelFocus);

	//setAcceptDrops(true);
}
void Canvas::bind(Session* session) {
	_session=session->shared_from_this();
}
Canvas::~Canvas() {
	/*! gl context n.a. (available in QOpenGLWidget::~QOpenGLWidget()) */
	_session->deinit_opengl();
}

void Canvas::initializeGL() {
	/*! gl context available */
	auto dpr=devicePixelRatio();
	auto w=width();
	auto h=height();
	_session->init_opengl(dpr, w*dpr, h*dpr);
	auto par=nativeParentWidget();
	gapr::print("par: ", par);
	connect(par->windowHandle(), &QWindow::screenChanged,
			this, &Canvas::handle_screen_changed);
}
void Canvas::paintGL() {
	/*! gl context available
	 * viewport ready;
	 * glClear() asap.
	 */
	//gapr::print("paint GL");

//#define USE_PAINTER
#ifdef USE_PAINTER
	QPainter painter;
	if(!painter.begin(this))
		return;
	painter.drawText(QPointF{100.0, 100.0}, QStringLiteral("hello"));
	painter.beginNativePainting();
#endif

	_session->canvas_render();
#ifdef USE_PAINTER
	painter.endNativePainting();
	painter.drawText(QPointF{100.0, 200.0}, QStringLiteral("world"));
	//painter.end();
#endif
	//gapr::print("paint GL end");

}

void Canvas::resizeGL(int w, int h) {
	/*! gl context available */
	auto dpr=devicePixelRatio();
	_session->canvas_resize(w*dpr, h*dpr);
}

void Canvas::wheelEvent(QWheelEvent* event) {
	double d=event->angleDelta().y()/120.0;
	_session->canvas_scroll(d);
}
void Canvas::mousePressEvent(QMouseEvent* event) {
	auto dpr=devicePixelRatio();
	auto pt=event->localPos();
	int btn=0;
	switch(event->buttons()) {
		case Qt::RightButton:
			btn=2;
			break;
		case Qt::LeftButton:
			btn=1;
			break;
	}
	if(btn==2) {
		return;
	}
	auto modifs=event->modifiers();
	_session->canvas_pressed(btn, pt.x(), pt.y(), modifs&Qt::ShiftModifier, modifs&Qt::ControlModifier, modifs&Qt::AltModifier, false);
}
void Canvas::mouseReleaseEvent(QMouseEvent* event) {
	auto dpr=devicePixelRatio();
	auto pt=event->localPos();
	_session->canvas_released(0, pt.x(), pt.y());
}
void Canvas::mouseMoveEvent(QMouseEvent* event) {
	auto dpr=devicePixelRatio();
	auto pt=event->localPos();
	_session->canvas_motion(pt.x(), pt.y());
}


void gapr::fix::Canvas::handle_screen_changed(QScreen* screen) {
	auto dpr=screen->devicePixelRatio();
	_session->canvas_scale_factor_changed(dpr);
}


#if 0
bool Viewer::sliceMode() {
	return priv->slice_mode;
}
bool Viewer::edgeVisibility() {
	return priv->show_edges;
}

	void initGraphPbi() {
		for(auto e: graph.edges()) {
			auto ep=EdgePriv::get(e);
			if(ep->vaoi<=0) {
				auto pathGL=pathToPathGL(ep->points);
				int vaoi=_vao_man.alloc();
				gl(BindBuffer, funcs)(GL_ARRAY_BUFFER, pathBuffers[vaoi].vbo);
				gl(BufferData, funcs)(GL_ARRAY_BUFFER, sizeof(PointGL)*pathGL.size(), pathGL.data(), GL_STATIC_DRAW);
				ep->vaoi=vaoi;
			}
		}
	}



void Viewer::dropEvent(QDropEvent* drop) {
	auto data=drop->mimeData();
	if(data->hasFormat("text/uri-list")) {
		auto urls=data->urls();
		if(urls.size()==1 && urls[0].isLocalFile()) {
			auto file=urls[0].toLocalFile();
			if(file.endsWith(QLatin1String{".txt"}, Qt::CaseInsensitive)) {
				priv->loadColormap(file);
				update();
			} else if(file.endsWith(QLatin1String{".obj"}, Qt::CaseInsensitive)) {
				priv->loadSurface(file);
				update();
			}
		}
	}
	/*
		if(urls.size()==1) {
		QFile f{urls[0].toLocalFile()};
		if(!f.open(QIODevice::ReadOnly))
		throwError("Failed to open");
		QTextStream fs{&f};
		int group;
		std::vector<int> groups{};
		while(true) {
		fs>>group;
		auto stat=fs.status();
		if(stat!=QTextStream::Ok) {
		if(stat==QTextStream::ReadPastEnd)
		break;
		throwError("Failed to read line");
		}
		printMessage("COLOR ", group);
		groups.push_back(group);
		}
		std::swap(priv->colormap, groups);
		for(auto i: groups) {
		printMessage("COLOR ", i);
		}
		update();
		}
		*/
}
void Viewer::dragEnterEvent(QDragEnterEvent* drop) {
	if(drop->mimeData()->hasFormat("text/uri-list")) {
		auto urls=drop->mimeData()->urls();
		if(urls.size()==1 && urls[0].isLocalFile()) {
			auto file=urls[0].toLocalFile();
			if(file.endsWith(QLatin1String{".txt"}, Qt::CaseInsensitive)
					|| file.endsWith(QLatin1String{".obj"}, Qt::CaseInsensitive)) {
				drop->accept();
			}
		}
	}
}

QImage Viewer::takeScreenshot() {
	if(!priv->graph)
		return {};

	makeCurrent();
	priv->paintEdge();
	priv->paintOpaque();
	priv->paintSurface();
	priv->paintVolume();
	priv->paintSorted(priv->fbo_scale);
	gl(Finish, priv->funcs)();

	QImage img{priv->fbo_width, priv->fbo_height, QImage::Format_ARGB32};
	gl(BindFramebuffer, priv->funcs)(GL_READ_FRAMEBUFFER, priv->fbo_scale);
	gl(ReadBuffer, priv->funcs)(GL_COLOR_ATTACHMENT0);
	gl(ReadPixels, priv->funcs)(0, 0, priv->fbo_width, priv->fbo_height, GL_BGRA, GL_UNSIGNED_BYTE, img.bits());
	doneCurrent();
	return img.mirrored(false, true);
}

#endif

#if 0
class ViewerColorOptions: public OptionsPage {
	ViewerPriv* vp;

	QColor colorsSet[COLOR_NUM];
	QColor colorsDef[COLOR_NUM];
	bool colorsNotDef[COLOR_NUM];

	bool colorModeSet;
	bool colorModeDef;

	bool disabled;
	bool noupdate;

	QCheckBox* checkMode;
	ColorWidget* colorWidgets[COLOR_NUM];

	static ViewerColorOptions* _instance;

	ViewerColorOptions();
	~ViewerColorOptions();

	void getState(bool* a, bool* b, bool* c, bool* d) const override;
	void setStateImp(SessionState ts) override;
	void useDefaultImp() override;
	void saveDefaultImp() override;
	void applyChangesImp() override { }
	void resetChangesImp() override { }

	private Q_SLOTS:
	void checkModeToggled(bool s);
	void colorChanged(int i, const QColor& c);

	public:
	void attach(ViewerPriv* _vp);

	static ViewerColorOptions* instance() {
		if(!_instance)
			_instance=new ViewerColorOptions{};
		return _instance;
	}
};

#endif
#ifdef _FNT_VIEWER_OPTIONS_H_


ViewerColorOptions* ViewerColorOptions::_instance{nullptr};
OptionsPage* OptionsPage::viewerColorOptions() {
	return ViewerColorOptions::instance();
}

void ViewerColorOptions::attach(ViewerPriv* _vp) {
	vp=_vp;
	if(vp) {
		checkMode->setChecked(vp->colorMode);
		for(int i=0; i<COLOR_NUM; i++) {
			colorWidgets[i]->setColor(vp->colors[i]);
		}
	} else {
		checkMode->setChecked(colorModeDef);
		for(int i=0; i<COLOR_NUM; i++) {
			colorWidgets[i]->setColor(colorsDef[i]);
		}
	}
	notifyChange();
}
ViewerColorOptions::ViewerColorOptions():
	OptionsPage{"Color", "Color Settings"}, vp{nullptr}, disabled{true},
	noupdate{false}
{
	auto options=Tracer::instance()->options();

	int v;
	if(!options->getInt("viewer.color.mode", &v)) {
		v=0;
	}
	colorModeSet=colorModeDef=v;

	checkMode=new QCheckBox{"Different colors for each neuron", this};
	layout()->addWidget(checkMode, 0);
	checkMode->setChecked(colorModeDef);
	checkMode->setToolTip("If checked, colors are used to denote different neurons.\nOtherwise, colors are used to denote types of branches.");
	connect(checkMode, &QCheckBox::stateChanged, this, &ViewerColorOptions::checkModeToggled);

	auto flayout=new QFormLayout{};
	layout()->addLayout(flayout, 0);
	for(int i=0; i<COLOR_NUM; i++) {
		auto vals=Tracer::instance()->options()->getIntList(QString{"viewer.color.%1"}.arg(i));
		QColor color{colorData[i].def};
		if(vals.size()>=3) {
			color=QColor{vals[0], vals[1], vals[2]};
		}

		colorsSet[i]=colorsDef[i]=color;
		colorsNotDef[i]=false;

		colorWidgets[i]=new ColorWidget{colorData[i].title, colorData[i].desc, this};
		colorWidgets[i]->setColor(color);
		connect(colorWidgets[i], &ColorWidget::colorChanged, [this, i](const QColor& c) { colorChanged(i, c); });
		flayout->addRow(colorData[i].title, colorWidgets[i]);
	}
	layout()->addStretch(1);
}

ViewerColorOptions::~ViewerColorOptions() {
}
void ViewerColorOptions::getState(bool* a, bool* b, bool* c, bool* d) const {
	*a=false;
	*b=false;
	bool notDefault=colorModeSet!=colorModeDef;
	for(int i=0; i<COLOR_NUM; i++) {
		if(colorsNotDef[i]) {
			notDefault=true;
			break;
		}
	}
	*c=notDefault;
	*d=notDefault;
}
void ViewerColorOptions::setStateImp(SessionState ts) {
	switch(ts) {
		case SessionState::Invalid:
		case SessionState::LoadingCatalog:
		case SessionState::Readonly:
			disabled=true;
			break;
		case SessionState::Ready:
		case SessionState::LoadingCubes:
		case SessionState::Computing:
			disabled=false;
			break;
	}
}
void ViewerColorOptions::useDefaultImp() {
	auto oldv=noupdate;
	noupdate=true;
	if(colorModeDef!=colorModeSet) {
		checkMode->setCheckState(colorModeDef?Qt::Checked:Qt::Unchecked);
	}
	for(int i=0; i<COLOR_NUM; i++) {
		if(colorsNotDef[i]) {
			colorWidgets[i]->setColor(colorsDef[i]);
		}
	}
	noupdate=oldv;
	if(!noupdate && vp && !disabled)
		vp->viewer->update();
}
void ViewerColorOptions::saveDefaultImp() {
	if(colorModeSet!=colorModeDef) {
		colorModeDef=colorModeSet;
		if(colorModeSet) {
			Tracer::instance()->options()->setInt("viewer.color.mode", colorModeSet);
		} else {
			Tracer::instance()->options()->removeKey("viewer.color.mode");
		}
	}
	for(int i=0; i<COLOR_NUM; i++) {
		if(colorsNotDef[i]) {
			colorsNotDef[i]=false;
			colorsDef[i]=colorsSet[i];
			auto key=QString{"viewer.color.%1"}.arg(i);
			if(colorsSet[i]!=colorData[i].def) {
				Tracer::instance()->options()->setIntList(key, {colorsSet[i].red(), colorsSet[i].green(), colorsSet[i].blue()});
			} else {
				Tracer::instance()->options()->setIntList(key, {});
			}
		}
	}
}
void ViewerColorOptions::checkModeToggled(bool s) {
	colorModeSet=s;
	if(vp && !disabled) {
		vp->colorMode=colorModeSet;
		if(!noupdate)
			vp->viewer->update();
	}
	notifyChange();
}
void ViewerColorOptions::colorChanged(int i, const QColor& col) {
	if(col!=colorsSet[i]) {
		colorsSet[i]=col;
		colorsNotDef[i]=col!=colorsDef[i];
		if(vp && !disabled) {
			vp->colors[i]=col;
			if(!noupdate)
				vp->viewer->update();
		}
		notifyChange();
	}
}


#endif

QMainWindow* gapr::fix::Window::create(std::optional<Session::Args>&& args) {
	auto session=std::make_shared<SessionImpl<Adapter, OpenGLFunctions>>();
	if(args)
		session->args(std::move(*args));
	auto window=new Window{session.get()};
	session->constructed(window);
	return window;
}

