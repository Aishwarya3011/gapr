#ifndef _FNT_DIALOG_H_
#define _FNT_DIALOG_H_

#include "gapr/config.hh"

#include <memory>

#include <QDialog>

namespace gapr {
	class PasswordDialog: public QDialog {
		Q_OBJECT
		public:
			GAPR_GUI_DECL PasswordDialog(const QString& title, const QString& url, const QString& err, QWidget* parent=nullptr);
			~PasswordDialog();

			GAPR_GUI_DECL QString get_password() const;
		private:
			struct PRIV;
			std::unique_ptr<PRIV> _priv;
	};
}


#if 0
class Point;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QDialogButtonBox;
class QLineEdit;

class PositionDialog : public QDialog {
	std::array<double, 6> _bbox;
	std::array<QSlider*, 3> sliders;
	std::array<QDoubleSpinBox*, 3> spinBoxes;
	void sliderChanged(int i, int value);
	void spinBoxChanged(int i, double d);
	public:
	static bool getPosition(Point* pos, const std::array<double, 6>& bbox, QWidget* par);
	PositionDialog(Point* pos, const std::array<double, 6>& bbox, QWidget* par);
};

#endif

#endif
