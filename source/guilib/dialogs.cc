#include "gapr/gui/dialogs.hh"

#include "ui_dlg-secret.h"
//#include <QtWidgets/QtWidgets>
#include <QIcon>

//#include <memory>
struct gapr::PasswordDialog::PRIV {
	Ui::secret_dialog ui;
};

gapr::PasswordDialog::PasswordDialog(const QString& title, const QString& url, const QString& err, QWidget* parent): QDialog{parent}, _priv{std::make_unique<PRIV>()}
{
	_priv->ui.setupUi(this);
	_priv->ui.url->setText(url);
	if(err.isEmpty())
		_priv->ui.err_msg->hide();
	else
		_priv->ui.err_msg->setText(err);

	auto icon=QIcon::fromTheme(QStringLiteral("dialog-password"));
	_priv->ui.icon->setPixmap(icon.pixmap(QSize{64, 64}));

	setWindowTitle(title);

	// XXX on_...
	connect(_priv->ui.buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(_priv->ui.buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
gapr::PasswordDialog::~PasswordDialog() { }

QString gapr::PasswordDialog::get_password() const {
	return _priv->ui.passwd->text();
}
#if 0
bool PositionDialog::getPosition(Point* pos, const std::array<double, 6>& bbox, QWidget* par) {
	std::unique_ptr<PositionDialog> dlg{new PositionDialog(pos, bbox, par)};
	
	bool res=dlg->exec()==QDialog::Accepted;
	if(res) {
		pos->x(dlg->spinBoxes[0]->value());
		pos->y(dlg->spinBoxes[1]->value());
		pos->z(dlg->spinBoxes[2]->value());
	}
	return res;
}
PositionDialog::PositionDialog(Point* pos, const std::array<double, 6>& bbox, QWidget* par):
	QDialog{par}, _bbox(bbox)
{
	std::array<double, 3> vals{pos->x(), pos->y(), pos->z()};
	for(int i=0; i<3; i++) {
		if(_bbox[i*2]>_bbox[i*2+1])
			_bbox[i*2]=_bbox[i*2+1]=vals[i];
		if(_bbox[2*i]==_bbox[2*i+1]) {
			_bbox[2*i]-=1;
			_bbox[2*i+1]+=1;
		}
	}

	setWindowTitle("Pick a position");

	QGridLayout* layout=new QGridLayout(this);
	layout->setColumnStretch(1, 1);
	layout->setRowStretch(3, 1);

	QLabel* label;
	label=new QLabel("X: ", this);
	layout->addWidget(label, 0, 0, 1, 1);
	label->setAlignment(Qt::AlignRight|Qt::AlignBaseline);
	label=new QLabel("Y: ", this);
	layout->addWidget(label, 1, 0, 1, 1);
	label->setAlignment(Qt::AlignRight|Qt::AlignBaseline);
	label=new QLabel("Z: ", this);
	layout->addWidget(label, 2, 0, 1, 1);
	label->setAlignment(Qt::AlignRight|Qt::AlignBaseline);
	label=new QLabel("Choose X, Y, Z coordinates to pick a position.", this);
	layout->addWidget(label, 3, 0, 1, 3);
	label->setAlignment(Qt::AlignLeft|Qt::AlignBaseline);
	label->setWordWrap(true);

	//


	for(int i=0; i<3; i++) {
		sliders[i]=new QSlider(Qt::Horizontal, this);
		layout->addWidget(sliders[i], i, 1, 1, 1);
		sliders[i]->setTickPosition(QSlider::NoTicks);
		sliders[i]->setMinimum(0);
		sliders[i]->setSingleStep(100);
		sliders[i]->setMaximum(10000);
		sliders[i]->setMinimumWidth(120);
		sliders[i]->setValue(lrint(10000*(vals[i]-_bbox[2*i])/(_bbox[2*i+1]-_bbox[2*i])));
		connect(sliders[i], &QSlider::valueChanged, [this, i](int v) { this->sliderChanged(i, v); });

		spinBoxes[i]=new QDoubleSpinBox(this);
		layout->addWidget(spinBoxes[i], i, 2, 1, 1);
		//spinBoxes[i]->setDecimals(2);
		spinBoxes[i]->setMinimum(_bbox[2*i]);
		spinBoxes[i]->setSingleStep(0.1);
		spinBoxes[i]->setMaximum(_bbox[2*i+1]);
		spinBoxes[i]->setValue(vals[i]);
		spinBoxes[i]->setSuffix("µm");
		spinBoxes[i]->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
		spinBoxes[i]->setAccelerated(true);
		connect(spinBoxes[i], static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [this, i](double v) { this->spinBoxChanged(i, v); });
	}

	auto btnBox=new QDialogButtonBox{QDialogButtonBox::Ok|QDialogButtonBox::Cancel, Qt::Horizontal, this};
	layout->addWidget(btnBox, 4, 0, 1, 3);
	connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	btnBox->button(QDialogButtonBox::Ok)->setAutoDefault(true);
}

template<typename T, typename V>
static void setValueNoSignal(T* obj, V val) {
	bool prev=obj->blockSignals(true);
	obj->setValue(val);
	obj->blockSignals(prev);
}
void PositionDialog::sliderChanged(int i, int value) {
	setValueNoSignal(spinBoxes[i], _bbox[2*i]+value*(_bbox[2*i+1]-_bbox[2*i])/10000);
}
void PositionDialog::spinBoxChanged(int i, double d) {
	setValueNoSignal(sliders[i], lrint(10000*(d-_bbox[2*i])/(_bbox[2*i+1]-_bbox[2*i])));
}

bool PositionDialog::getPosition(Point* pos, const std::array<double, 6>& bbox, QWidget* par) {
	std::unique_ptr<PositionDialog> dlg{new PositionDialog(pos, bbox, par)};
	
	bool res=dlg->exec()==QDialog::Accepted;
	if(res) {
		pos->x(dlg->spinBoxes[0]->value());
		pos->y(dlg->spinBoxes[1]->value());
		pos->z(dlg->spinBoxes[2]->value());
	}
	return res;
}
PositionDialog::PositionDialog(Point* pos, const std::array<double, 6>& bbox, QWidget* par):
	QDialog{par}, _bbox(bbox)
{
	std::array<double, 3> vals{pos->x(), pos->y(), pos->z()};
	for(int i=0; i<3; i++) {
		if(_bbox[i*2]>_bbox[i*2+1])
			_bbox[i*2]=_bbox[i*2+1]=vals[i];
		if(_bbox[2*i]==_bbox[2*i+1]) {
			_bbox[2*i]-=1;
			_bbox[2*i+1]+=1;
		}
	}

	setWindowTitle("Pick a position");

	QGridLayout* layout=new QGridLayout(this);
	layout->setColumnStretch(1, 1);
	layout->setRowStretch(3, 1);

	QLabel* label;
	label=new QLabel("X: ", this);
	layout->addWidget(label, 0, 0, 1, 1);
	label->setAlignment(Qt::AlignRight|Qt::AlignBaseline);
	label=new QLabel("Y: ", this);
	layout->addWidget(label, 1, 0, 1, 1);
	label->setAlignment(Qt::AlignRight|Qt::AlignBaseline);
	label=new QLabel("Z: ", this);
	layout->addWidget(label, 2, 0, 1, 1);
	label->setAlignment(Qt::AlignRight|Qt::AlignBaseline);
	label=new QLabel("Choose X, Y, Z coordinates to pick a position.", this);
	layout->addWidget(label, 3, 0, 1, 3);
	label->setAlignment(Qt::AlignLeft|Qt::AlignBaseline);
	label->setWordWrap(true);

	//


	for(int i=0; i<3; i++) {
		sliders[i]=new QSlider(Qt::Horizontal, this);
		layout->addWidget(sliders[i], i, 1, 1, 1);
		sliders[i]->setTickPosition(QSlider::NoTicks);
		sliders[i]->setMinimum(0);
		sliders[i]->setSingleStep(100);
		sliders[i]->setMaximum(10000);
		sliders[i]->setMinimumWidth(120);
		sliders[i]->setValue(lrint(10000*(vals[i]-_bbox[2*i])/(_bbox[2*i+1]-_bbox[2*i])));
		connect(sliders[i], &QSlider::valueChanged, [this, i](int v) { this->sliderChanged(i, v); });

		spinBoxes[i]=new QDoubleSpinBox(this);
		layout->addWidget(spinBoxes[i], i, 2, 1, 1);
		//spinBoxes[i]->setDecimals(2);
		spinBoxes[i]->setMinimum(_bbox[2*i]);
		spinBoxes[i]->setSingleStep(0.1);
		spinBoxes[i]->setMaximum(_bbox[2*i+1]);
		spinBoxes[i]->setValue(vals[i]);
		spinBoxes[i]->setSuffix("µm");
		spinBoxes[i]->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
		spinBoxes[i]->setAccelerated(true);
		connect(spinBoxes[i], static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [this, i](double v) { this->spinBoxChanged(i, v); });
	}

	auto btnBox=new QDialogButtonBox{QDialogButtonBox::Ok|QDialogButtonBox::Cancel, Qt::Horizontal, this};
	layout->addWidget(btnBox, 4, 0, 1, 3);
	connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	btnBox->button(QDialogButtonBox::Ok)->setAutoDefault(true);
}

template<typename T, typename V>
static void setValueNoSignal(T* obj, V val) {
	bool prev=obj->blockSignals(true);
	obj->setValue(val);
	obj->blockSignals(prev);
}
void PositionDialog::sliderChanged(int i, int value) {
	setValueNoSignal(spinBoxes[i], _bbox[2*i]+value*(_bbox[2*i+1]-_bbox[2*i])/10000);
}
void PositionDialog::spinBoxChanged(int i, double d) {
	setValueNoSignal(sliders[i], lrint(10000*(d-_bbox[2*i])/(_bbox[2*i+1]-_bbox[2*i])));
}
#endif

