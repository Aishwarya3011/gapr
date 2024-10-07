#ifndef _GAPR_INCLUDE_GUI_UTILITY_H_
#define _GAPR_INCLUDE_GUI_UTILITY_H_

#include "gapr/utility.hh"

#include <QString>

class QWidget;
class QMatrix4x4;

namespace gapr {

	GAPR_GUI_DECL void showError(const QString& msg, QWidget* par=nullptr);

	inline std::ostream& operator<<(std::ostream& os, const QString& str) {
		return os<<str.toLocal8Bit().constData();
	}

	void dumpMatrix(const char* s, const QMatrix4x4& m);
}
//



#if 0
//#include<QTextStream>
class QWidget;
#include <QHash>
//class QUrl;

#define forceSegFault() do { auto p=static_cast<int*>(nullptr); *p=0xbad; } while(0)
namespace std {
	template<> struct hash<QString> {
		size_t operator()(const QString& s) const { return qHash(s); }
	};
}
namespace std {
	template<typename T1, typename T2> struct hash<std::pair<T1, T2>> {
		size_t operator()(const std::pair<T1, T2>& s) const { return hash<T1>{}(s.first)^hash<T2>{}(s.second); }
	};
}



	void showMessage(const QString& title, const QString& msg, QWidget* par=nullptr);
	void showWarning(const QString& title, const QString& msg, QWidget* par=nullptr);

	void openManual(QWidget* par);

//bool readTiff(const QString& fn, uint8_t* buf, int64_t ystride, int64_t zstride);
//bool saveTiff(const QString& fn, const uint8_t* buf, int64_t width, int64_t height, int64_t depth, double xres, double yres, double zres);
//bool isUrl(const QString& s);

//QString getCubeFileName(const QString& pat, int32_t x, int32_t y, int32_t z);

//bool calcInverse(const std::array<double, 9>& dir, std::array<double, 9>& rdir);
//bool checkSingularity(const std::array<double, 9>& mat);

}

namespace std {
	template<> struct hash<QString> {
		size_t operator()(const QString& s) const { return qHash(s); }
	};
}

#endif






#endif
