/* lib/gui/range-widget.cc
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

//@@@@
#include "gapr/gui/range-widget.hh"

#include "gapr/utility.hh"

#include <QtWidgets/QtWidgets>

#include "ui_wid-range.h"
#include "ui_wid-range-slave.h"

constexpr static int RANGEI_MAX=1000;

inline bool gapr::range_widget::can_shrink() const noexcept {
	return _range[0]<_range[1] &&
		(_limits[0]!=_range[0] || _limits[1]!=_range[1]);
}
inline bool gapr::range_widget::can_reset() const noexcept {
	return _limits[0]!=0 || _limits[1]!=1 || _range[0]!=0 || _range[1]!=1;
}
template<int I> inline bool gapr::range_widget::set_range(double v) noexcept {
	return (_range[I]!=v)?(_range[I]=v, true):false;
}
template<int I> inline bool gapr::range_widget::set_rangei(int v) noexcept {
	return (_rangei[I]!=v)?(_rangei[I]=v, true):false;
}
inline int gapr::range_widget::to_int(double x) const noexcept {
	return std::lround(RANGEI_MAX*(x-_limits[0])/(_limits[1]-_limits[0]));
}
constexpr inline double gapr::range_widget::to_float(int x) const noexcept {
	return x*(_limits[1]-_limits[0])/RANGEI_MAX+_limits[0];
}

static void setSliderValueImpl(QSlider* slider, int v) {
	if(v>RANGEI_MAX)
		v=RANGEI_MAX;
	if(v<0)
		v=0;
	QSignalBlocker blk{*slider};
	slider->setValue(v);
}

struct gapr::range_widget::PRIV {
	Ui::range_widget ui;
	range_widget_slave* _slave{nullptr};
	QSlider* _slider0;
	QSlider* _slider1;
	QToolButton* _slave_shrink;
	QToolButton* _slave_reset;
	QMetaObject::Connection _conn0;
	QMetaObject::Connection _conn1;
	QMetaObject::Connection _conn2;
	QMetaObject::Connection _conn3;
	template<int I> void setSliderValue(int v) const {
		auto slider=I?ui.slider1:ui.slider0;
		setSliderValueImpl(slider, v);
	}
	template<int I> void setSliderSlave(int v) const {
		auto slider2=I?_slider1:_slider0;
		if(slider2)
			setSliderValueImpl(slider2, v);
	}
	template<int I> void setSliderBoth(int v) const {
		setSliderValue<I>(v);
		setSliderSlave<I>(v);
	}
	template<int I> void setLineValue(double v) const {
		auto line=I?ui.line1:ui.line0;
		QSignalBlocker blk{*line};
		line->setText(QString::number(v, 'f', 3));
	}
	void updateButtons(bool canShrink, bool canReset) const {
		ui.shrink->setEnabled(canShrink);
		ui.reset->setEnabled(canReset);
		if(_slave) {
			_slave_shrink->setEnabled(canShrink);
			_slave_reset->setEnabled(canReset);
		}
	}
	void setup(QWidget* widget) {
		ui.setupUi(widget);

		ui.slider0->setMaximum(RANGEI_MAX);
		ui.slider0->setMinimum(0);
		setSliderValue<0>(0);
		ui.slider1->setMaximum(RANGEI_MAX);
		ui.slider1->setMinimum(0);
		setSliderValue<1>(RANGEI_MAX);
		setLineValue<0>(0);
		setLineValue<1>(1);

		auto validator=new QDoubleValidator{widget};
		ui.line0->setValidator(validator);
		ui.line1->setValidator(validator);
	}

	void set_slave() {
		if(_slider0) {
			if(!disconnect(_conn0))
				gapr::report("failed to disconnect");
			_slider0=nullptr;
		}
		if(_slider1) {
			if(!disconnect(_conn1))
				gapr::report("failed to disconnect");
			_slider1=nullptr;
		}
		if(_slave_shrink) {
			if(!disconnect(_conn2))
				gapr::report("failed to disconnect");
			_slave_shrink=nullptr;
		}
		if(_slave_reset) {
			if(!disconnect(_conn3))
				gapr::report("failed to disconnect");
			_slave_reset=nullptr;
		}
		_slave=nullptr;
	}
	void set_slave(Ui::range_widget_slave& slave_ui, gapr::range_widget* widget) {
		auto conn0=connect(slave_ui.slider0, &QSlider::valueChanged, widget, &range_widget::slave_slider0_changed);
		if(conn0) {
			_conn0=conn0;
			_slider0=slave_ui.slider0;
		}
		auto conn1=connect(slave_ui.slider1, &QSlider::valueChanged, widget, &range_widget::slave_slider1_changed);
		if(conn1) {
			_conn1=conn1;
			_slider1=slave_ui.slider1;
		}
		auto conn2=connect(slave_ui.shrink, &QToolButton::clicked, widget, &range_widget::on_shrink_clicked);
		if(conn2) {
			_conn2=conn2;
			_slave_shrink=slave_ui.shrink;
		}
		auto conn3=connect(slave_ui.reset, &QToolButton::clicked, widget, &range_widget::on_reset_clicked);
		if(conn3) {
			_conn3=conn3;
			_slave_reset=slave_ui.reset;
		}
	}
};

gapr::range_widget::range_widget(QWidget* parent):
	QWidget{parent},
	_priv{std::make_unique<PRIV>()},
	_limits{0.0, 1.0},
	_range{0.0, 1.0},
	_rangei{0, RANGEI_MAX}
{
	_priv->setup(this);
}

gapr::range_widget::~range_widget() { }

void gapr::range_widget::set_state(const std::array<double, 4>& state) {
	auto [x0, x1, l0, l1]=state;
	if(x0>x1)
		x1=x0;
	if(l1<x1)
		l1=x1;
	_limits[1]=l1;
	if(l0>x0)
		l0=x0;
	_limits[0]=l0;
	bool chgs[2]={
		set_range<0>(x0),
		set_range<1>(x1)
	};
	bool chgs_i[2]={
		set_rangei<0>(to_int(x0)),
		set_rangei<1>(to_int(x1))
	};
	if(chgs[0])
		_priv->setLineValue<0>(_range[0]);
	if(chgs[1])
		_priv->setLineValue<1>(_range[1]);
	if(chgs_i[0])
		_priv->setSliderBoth<0>(_rangei[0]);
	if(chgs_i[1])
		_priv->setSliderBoth<1>(_rangei[1]);
	_priv->updateButtons(can_shrink(), can_reset());
	//Q_EMIT this->changed(x0, x1);
}

void gapr::range_widget::on_line0_editingFinished() {
	auto x=_priv->ui.line0->text().toDouble();
	if(!set_range<0>(x))
		return;
	if(x<_limits[0]) {
		_limits[0]=x;
		auto x1i=to_int(_range[1]);
		if(set_rangei<1>(x1i))
			_priv->setSliderBoth<1>(x1i);
	} else if(x>_range[1]) {
		if(x>_limits[1])
			_limits[1]=x;
		auto x1i=to_int(x);
		if(set_rangei<1>(x1i))
			_priv->setSliderBoth<1>(x1i);
		_range[1]=x;
		_priv->setLineValue<1>(x);
	}
	auto xi=to_int(x);
	if(set_rangei<0>(xi))
		_priv->setSliderBoth<0>(xi);
	_priv->updateButtons(can_shrink(), can_reset());
	Q_EMIT this->changed(x, _range[1]);
}
void gapr::range_widget::on_line1_editingFinished() {
	auto x=_priv->ui.line1->text().toDouble();
	if(!set_range<1>(x))
		return;
	if(x>_limits[1]) {
		_limits[1]=x;
		auto x0i=to_int(_range[0]);
		if(set_rangei<0>(x0i))
			_priv->setSliderBoth<0>(x0i);
	} else if(x<_range[0]) {
		if(x<_limits[0])
			_limits[0]=x;
		auto x0i=to_int(x);
		if(set_rangei<0>(x0i))
			_priv->setSliderBoth<0>(x0i);
		_range[0]=x;
		_priv->setLineValue<0>(x);
	}
	auto xi=to_int(x);
	if(set_rangei<1>(xi))
		_priv->setSliderBoth<1>(xi);
	_priv->updateButtons(can_shrink(), can_reset());
	Q_EMIT this->changed(_range[0], x);
}

void gapr::range_widget::slider0_changed(int x) {
	if(x>_rangei[1]) {
		auto x1f=to_float(x);
		if(set_range<1>(x1f))
			_priv->setLineValue<1>(x1f);
		_rangei[1]=x;
		_priv->setSliderBoth<1>(x);
	}
	auto xf=to_float(x);
	if(set_range<0>(xf))
		_priv->setLineValue<0>(xf);
	_priv->updateButtons(can_shrink(), can_reset());
	Q_EMIT this->changed(_range[0], _range[1]);
}
void gapr::range_widget::slider1_changed(int x) {
	if(x<_rangei[0]) {
		auto x0f=to_float(x);
		if(set_range<0>(x0f))
			_priv->setLineValue<0>(x0f);
		_rangei[0]=x;
		_priv->setSliderBoth<0>(x);
	}
	auto xf=to_float(x);
	if(set_range<1>(xf))
		_priv->setLineValue<1>(xf);
	_priv->updateButtons(can_shrink(), can_reset());
	Q_EMIT this->changed(_range[0], _range[1]);
}
void gapr::range_widget::on_slider0_valueChanged(int x) {
	if(!set_rangei<0>(x))
		return;
	_priv->setSliderSlave<0>(x);
	slider0_changed(x);
}
void gapr::range_widget::on_slider1_valueChanged(int x) {
	if(!set_rangei<1>(x))
		return;
	_priv->setSliderSlave<1>(x);
	slider1_changed(x);
}
void gapr::range_widget::slave_slider0_changed(int x) {
	if(!set_rangei<0>(x))
		return;
	_priv->setSliderValue<0>(x);
	slider0_changed(x);
}
void gapr::range_widget::slave_slider1_changed(int x) {
	if(!set_rangei<1>(x))
		return;
	_priv->setSliderValue<1>(x);
	slider1_changed(x);
}

void gapr::range_widget::on_shrink_clicked(bool) {
	if(_range[0]>=_range[1])
		return;
	bool chg_lim{false};
	if(_limits[0]!=_range[0]) {
		_limits[0]=_range[0];
		chg_lim=true;
	}
	if(_limits[1]!=_range[1]) {
		_limits[1]=_range[1];
		chg_lim=true;
	}
	if(!chg_lim)
		return;
	bool chgs_i[2]={
		set_rangei<0>(to_int(_range[0])),
		set_rangei<1>(to_int(_range[1]))
	};
	if(chgs_i[0])
		_priv->setSliderBoth<0>(_rangei[0]);
	if(chgs_i[1])
		_priv->setSliderBoth<1>(_rangei[1]);
	_priv->updateButtons(can_shrink(), can_reset());
}

void gapr::range_widget::on_reset_clicked(bool) {
	bool chgs[2]={
		set_range<0>(0.0),
		set_range<1>(1.0)
	};
	bool chg_lim{false};
	if(_limits[0]!=0) {
		_limits[0]=0;
		chg_lim=true;
	}
	if(_limits[1]!=1) {
		_limits[1]=1;
		chg_lim=true;
	}
	if(!chgs[0] && !chgs[1] && !chg_lim)
		return;
	if(chgs[0])
		_priv->setLineValue<0>(_range[0]);
	if(chgs[1])
		_priv->setLineValue<1>(_range[1]);
	bool chgs_i[2]={
		(chgs[0]||chg_lim)?set_rangei<0>(to_int(0.0)):false,
		(chgs[1]||chg_lim)?set_rangei<1>(to_int(1.0)):false
	};
	if(chgs_i[0])
		_priv->setSliderBoth<0>(_rangei[0]);
	if(chgs_i[1])
		_priv->setSliderBoth<1>(_rangei[1]);
	_priv->updateButtons(can_shrink(), can_reset());
	Q_EMIT this->changed(0.0, 1.0);
}

struct gapr::range_widget_slave::PRIV {
	Ui::range_widget_slave ui;
	template<int I> void setSliderValue(int v) const {
		auto slider=I?ui.slider1:ui.slider0;
		setSliderValueImpl(slider, v);
	}
	void setup(QWidget* widget) {
		ui.setupUi(widget);

		ui.slider0->setMaximum(RANGEI_MAX);
		ui.slider0->setMinimum(0);
		setSliderValue<0>(0);
		ui.slider1->setMaximum(RANGEI_MAX);
		ui.slider1->setMinimum(0);
		setSliderValue<1>(RANGEI_MAX);
	}
};

gapr::range_widget_slave::range_widget_slave(QWidget* parent):
	QWidget{parent},
	_priv{std::make_unique<PRIV>()},
	_master{nullptr}
{
	_priv->setup(this);
}

gapr::range_widget_slave::~range_widget_slave() {
}

void gapr::range_widget_slave::on_slider0_valueChanged(int x) {
}
void gapr::range_widget_slave::on_slider1_valueChanged(int x) {
}
//_slave_slider0;
//_slave_slider1;
//conn0;
//conn1;

void gapr::range_widget_slave::set_master(range_widget* master) {
	auto& priv=*master->_priv;
	if(auto slave2=priv._slave) {
		priv.set_slave();
		slave2->_master=nullptr;
	}
	if(_master)
		_master->set_slave();
	_master=&priv;
	setSliderValueImpl(_priv->ui.slider0, priv.ui.slider0->value());
	setSliderValueImpl(_priv->ui.slider1, priv.ui.slider1->value());
	_priv->ui.shrink->setEnabled(priv.ui.shrink->isEnabled());
	_priv->ui.reset->setEnabled(priv.ui.reset->isEnabled());
	priv.set_slave(_priv->ui, master);
}

