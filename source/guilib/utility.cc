#include "gapr/gui/utility.hh"

#include <QMessageBox>
#include <QMatrix4x4>

void gapr::showError(const QString& msg, QWidget* par) {
	QMessageBox mbox{QMessageBox::Critical, "Error", msg, QMessageBox::Ok, par};
	mbox.setDefaultButton(QMessageBox::Ok);
	mbox.exec();
}

void gapr::dumpMatrix(const char* s, const QMatrix4x4& m) {
	fprintf(stderr, "%s: \n", s);
	for(int i=0; i<4; i++) {
		fprintf(stderr, "   ");
		for(int j=0; j<4; j++) {
			fprintf(stderr, "%lf, ", m(i, j));
		}
		fprintf(stderr, "\n");
	}
}

#if 0
#include <QtWidgets/QtWidgets>

#include <sstream>
#include <cmath>

#include <tiffio.h>


void gapr::showMessage(const QString& title, const QString& msg, QWidget* par) {
	QMessageBox mbox{QMessageBox::Information, title, msg, QMessageBox::Ok, par};
	mbox.setDefaultButton(QMessageBox::Ok);
	mbox.exec();
}

void gapr::showWarning(const QString& title, const QString& msg, QWidget* par) {
	QMessageBox mbox{QMessageBox::Warning, title, msg, QMessageBox::Ok, par};
	mbox.setDefaultButton(QMessageBox::Ok);
	mbox.exec();
}


void gapr::openManual(QWidget* par) {
	auto pthExec=QCoreApplication::applicationDirPath();
	QDir dirExec{pthExec};
	auto manualFile=dirExec.absoluteFilePath("../" PACKAGE_DATADIR "/doc/" PACKAGE "/index.html");
	if(QFile{manualFile}.exists()) {
		if(!QDesktopServices::openUrl(QUrl{manualFile}))
			showWarning("Show manual", "Failed to open local copy of manual.", par);
	} else {
		QMessageBox mbox(QMessageBox::Question, "Open online manual", "Local copy of manual cannot be found. Open the online manual?", QMessageBox::Yes|QMessageBox::No, par);
		mbox.setDefaultButton(QMessageBox::Yes);
		auto res=mbox.exec();
		if(res==QMessageBox::Yes) {
			if(!QDesktopServices::openUrl(QUrl{PACKAGE_URL "manual/"}))
				showWarning("Show manual", "Failed to open online manual.", par);
		}
	}
}

/*
QString pathOfSwapFile(const QString& s) {
	auto pathStd=QDir{s}.canonicalPath();
	auto pathStdEnc=pathStd.toUtf8();
	auto pathStdEncStr=pathStdEnc.toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals);
	return pathOfFnt()+"/swap-of-path-"+pathStdEncStr;
}
QString pathOfSwapFile(const QUrl& u) {
	auto urlStd=u.adjusted(QUrl::RemovePassword|QUrl::RemoveUserInfo|QUrl::NormalizePathSegments);
	auto urlStdEnc=urlStd.toEncoded();
	auto urlStdEncStr=urlStdEnc.toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals);
	return pathOfFnt()+"/swap-of-url-"+urlStdEncStr;
}
*/

QString pathOfFnt() {
#ifdef _WIN64
	auto p=QDir::home().absoluteFilePath(PACKAGE);
#else
	auto p=QDir::home().absoluteFilePath("." PACKAGE);
#endif
	auto d=QDir{p};
	if(!d.exists()) {
		d.mkpath(".");
	}
	return p;
}
QStringList pathOfFntPlugins() {
	QStringList pths;
#ifdef _WIN64
	auto pthHome=QDir::home().absoluteFilePath(PACKAGE "/plugins");
#else
	auto pthHome=QDir::home().absoluteFilePath("." PACKAGE "/plugins");
#endif
	if(QDir{pthHome}.exists()) {
		pths<<pthHome;
	}
	auto pthExec=QCoreApplication::applicationDirPath();
	QDir dirExec{pthExec};
	if(dirExec.cd("../" PACKAGE_MODULEDIR)) {
		pths<<dirExec.absolutePath();
	} else {
		pths<<pthExec;
	}
	return pths;
}

class Tiff {
	private:
		TIFF* ptr;
	public:
		Tiff(const char* p, const char* m) {
			ptr=TIFFOpen(p, m);
		}
		~Tiff() {
			if(ptr) {
				TIFFClose(ptr);
				ptr=nullptr;
			}
		}
		operator TIFF*() { return ptr; }
		bool operator!() { return !ptr; }
};
bool readTiff(const QString& fn, uint8_t* buf, int64_t ystride, int64_t zstride) {
	Tiff tif(fn.toLocal8Bit().data(), "r");
	if(!tif) {
		gapr::print("Failed to open: ", fn);
		return false;
	}
	int32_t z=0;
	do {
		uint32 ww, hh;
		if(!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &ww)
				|| !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &hh)) {
			gapr::print("Failed to get width/height: ", fn);
			return false;
		}
		for(uint32 y=0; y<hh; y++) {
			if(TIFFReadScanline(tif, buf+ystride*y+zstride*z, y, 0)<0) {
				gapr::print("Failed to read scanline: ", fn);
				return false;
			}
		}
		z++;
	} while(TIFFReadDirectory(tif));
	return true;
}

const char* writeTags(TIFF* tif, int64_t w, int64_t h, int64_t d, double xres, double yres, double zres, int32_t z) {
	uint32 ww=w;
	if(!TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, ww)) {
		return "IMAGEWIDTH";
	}
	uint32 hh=h;
	if(!TIFFSetField(tif, TIFFTAG_IMAGELENGTH, hh)) {
		return "IMAGELENGTH";
	}
	float xresf=10000/xres;
	if(!TIFFSetField(tif, TIFFTAG_XRESOLUTION, xresf)) {
		return "XRESOLUTION";
	}
	float yresf=10000/yres;
	if(!TIFFSetField(tif, TIFFTAG_YRESOLUTION, yresf)) {
		return "YRESOLUTION";
	}
	uint16 resunit=RESUNIT_CENTIMETER;
	if(!TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, resunit)) {
		return "RESOLUTIONUNIT";
	}
	uint16 format=SAMPLEFORMAT_UINT;
	if(!TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, format)) {
		return "SAMPLEFORMAT";
	}
	uint16 bps=8;
	if(!TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, bps)) {
		return "BITSPERSAMPLE";
	}
	uint16 spp=1;
	if(!TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, spp)) {
		return "SAMPLESPERPIXEL";
	}
	uint32 rps=TIFFDefaultStripSize(tif, 0);
	if(!TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, rps)) {
		return "ROWSPERSTRIP";
	}
	uint16 planar=PLANARCONFIG_CONTIG;
	if(!TIFFSetField(tif, TIFFTAG_PLANARCONFIG, planar)) {
		return "PLANARCONFIG";
	}
	uint16 orient=ORIENTATION_TOPLEFT;
	if(!TIFFSetField(tif, TIFFTAG_ORIENTATION, orient)) {
		return "ORIENTATION";
	}
	uint16 photom=PHOTOMETRIC_MINISBLACK;
	if(!TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, photom)) {
		return "PHOTOMETRIC";
	}
	uint16 compr=COMPRESSION_LZW; // LZW
	if(!TIFFSetField(tif, TIFFTAG_COMPRESSION, compr)) {
		return "COMPRESSION";
	}
	uint16 pred=1; // 1 2
	if(!TIFFSetField(tif, TIFFTAG_PREDICTOR, pred)) {
		return "PREDICTOR";
	}
	if(!TIFFSetField(tif, TIFFTAG_SOFTWARE, PACKAGE_NAME " (" PACKAGE_VERSION ")")) {
		return "SOFTWARE";
	}
	std::ostringstream oss;
	oss<<"Saved cube. ";
	oss<<"depth: "<<d<<" pixels. ";
	oss<<"Z resolution: "<<10000/zres<<" pixels/cm. ";
	oss<<"Z offset: "<<z<<" pixels.";
	if(!TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, oss.str().c_str())) {
		return "IMAGEDESCRIPTION";
	}
	return nullptr;
}
bool saveTiff(const QString& fn, const uint8_t* buf, int64_t width, int64_t height, int64_t depth, double xres, double yres, double zres) {
	Tiff otif(fn.toLocal8Bit().data(), "w");
	if(!otif) {
		//std::cerr<<"Cannot open file '"<<filename<<"' for write.\n";
		return false;
	}
	for(int32_t z=0; z<depth; z++) {
		auto failedTag=writeTags(otif, width, height, depth, xres, yres, zres, z);
		if(failedTag) {
			//std::cerr<<"Error writing "<<failedTag<<" field: '"<<fn<<"'.\n";
			return false;
		}
		for(uint32 r=0; r<height; r++) {
			if(TIFFWriteScanline(otif, const_cast<uint8_t*>(buf+z*width*height+width*r), r, 0)<0) {
				//std::cerr<<"Error write scanline: '"<<filename<<"'.\n";
				return false;
			}
		}
		if(!TIFFWriteDirectory(otif)) {
			return false;
		}
	}
	return true;
}

bool isUrl(const QString& s) {
	QRegExp regex{"^[a-zA-Z]+://.*/.*$", Qt::CaseInsensitive, QRegExp::RegExp};
	return regex.exactMatch(s);
}

QString getCubeFileName(const QString& pat, int32_t x, int32_t y, int32_t z) {
	QString str;
	QTextStream ss{&str};
	for(int i=0; i<pat.length(); ) {
		auto cc=pat[i];
		if(cc!=QChar{'<'}) {
			ss<<pat[i++];
			continue;
		}

		int state=0;
		QChar fill{' '};
		int width=0;
		int32_t val=0;

		int j;
		for(j=i+1; j<pat.length(); ) {
			auto c=pat[j];
			if(state==0) {
				fill=c;
				state=1;
				j++;
			} else if(state==1) {
				if(c.isDigit()) {
					width=width*10+c.digitValue();
					j++;
					state=2;
				} else if(c==QChar{'>'}) {
					j++;
					state=6;
					break;
				} else {
					state=3;
				}
			} else if(state==2) {
				if(c.isDigit()) {
					width=width*10+c.digitValue();
					j++;
				} else {
					state=3;
				}
			} else if(state==3) {
				if(c==QChar{'x'} || c==QChar{'X'}) {
					val=x;
					j++;
					state=4;
				} else if(c==QChar{'y'} || c==QChar{'Y'}) {
					val=y;
					j++;
					state=4;
				} else if(c==QChar{'z'} || c==QChar{'Z'}) {
					val=z;
					j++;
					state=4;
				} else {
					break;
				}
			} else if(state==4) {
				if(c==QChar{'>'}) {
					state=5;
					j++;
					break;
				} else {
					break;
				}
			}
		}
	//
		if(state==5) {
			auto prev_f=ss.padChar();
			auto prev_w=ss.fieldWidth();
			ss.setPadChar(fill);
			ss.setFieldWidth(width);
			ss<<val;
			ss.setPadChar(prev_f);
			ss.setFieldWidth(prev_w);
			i=j;
		} else if(state==6) {
			ss<<fill;
			i=j;
		} else {
			ss<<cc;
			i++;
		}
	}
	return str;
}

bool checkSingularity(const std::array<double, 9>& mat) {
	std::array<double, 3> vals;
	std::array<double, 9> mat2;
	for(int i=0; i<3; i++) {
		for(int j=0; j<3; j++) {
			mat2[i+j*3]=mat[i+0*3]*mat[j+0*3]+mat[i+1*3]*mat[j+1*3]+mat[i+2*3]*mat[j+2*3];
		}
	}
	auto M=[&mat2](int i, int j)->double { return mat2[i+j*3]; };
	auto b=-M(2,2)-M(1,1)-M(0,0);
	auto c=(M(1,1)+M(0,0))*M(2,2)-M(1,2)*M(2,1)-M(0,2)*M(2,0)+M(0,0)*M(1,1)-M(0,1)*M(1,0);
	auto d=(M(0,1)*M(1,0)-M(0,0)*M(1,1))*M(2,2)+(M(0,0)*M(1,2)-M(0,2)*M(1,0))*M(2,1)+(M(0,2)*M(1,1)-M(0,1)*M(1,2))*M(2,0);

	auto v0=b*b-3*c;
	auto v1=b*(2*b*b-9*c)+27*d;
	auto v2=v1*v1-4*v0*v0*v0;

	if(v2<0) {
		auto theta=acos(-v1/2/sqrt(v0*v0*v0))/3;
		auto len=sqrt(v0);
		vals[0]=(-2*cos(M_PI/3+theta)*len-b)/3;
		vals[1]=(2*cos(2*M_PI/3+theta)*len-b)/3;
		vals[2]=(2*cos(theta)*len-b)/3;
	} else {
		auto v5=cbrt((sqrt(v2)-v1)/2)-cbrt((sqrt(v2)+v1)/2);
		vals[0]=vals[1]=-(v5/2+b)/3;
		vals[2]=(v5-b)/3;
	}
	for(auto& v: vals) {
		if(v<0)
			v=0;
		v=sqrt(v);
	}
	std::sort(vals.begin(), vals.end());
	if(vals[2]==0)
		return false;
	if(vals[0]/vals[2]<.00001)
		return false;
	return true;
}
bool checkSingularity(const std::array<double, 9>& mat) {
	std::array<double, 3> vals;
	std::array<double, 9> mat2;
	for(int i=0; i<3; i++) {
		for(int j=0; j<3; j++) {
			mat2[i+j*3]=mat[i+0*3]*mat[j+0*3]+mat[i+1*3]*mat[j+1*3]+mat[i+2*3]*mat[j+2*3];
		}
	}
	auto M=[&mat2](int i, int j)->double { return mat2[i+j*3]; };
	auto b=-M(2,2)-M(1,1)-M(0,0);
	auto c=(M(1,1)+M(0,0))*M(2,2)-M(1,2)*M(2,1)-M(0,2)*M(2,0)+M(0,0)*M(1,1)-M(0,1)*M(1,0);
	auto d=(M(0,1)*M(1,0)-M(0,0)*M(1,1))*M(2,2)+(M(0,0)*M(1,2)-M(0,2)*M(1,0))*M(2,1)+(M(0,2)*M(1,1)-M(0,1)*M(1,2))*M(2,0);

	auto v0=b*b-3*c;
	auto v1=b*(2*b*b-9*c)+27*d;
	auto v2=v1*v1-4*v0*v0*v0;

	if(v2<0) {
		auto theta=acos(-v1/2/sqrt(v0*v0*v0))/3;
		auto len=sqrt(v0);
		vals[0]=(-2*cos(M_PI/3+theta)*len-b)/3;
		vals[1]=(2*cos(2*M_PI/3+theta)*len-b)/3;
		vals[2]=(2*cos(theta)*len-b)/3;
	} else {
		auto v5=cbrt((sqrt(v2)-v1)/2)-cbrt((sqrt(v2)+v1)/2);
		vals[0]=vals[1]=-(v5/2+b)/3;
		vals[2]=(v5-b)/3;
	}
	for(auto& v: vals) {
		if(v<0)
			v=0;
		v=sqrt(v);
	}
	std::sort(vals.begin(), vals.end());
	if(vals[2]==0)
		return false;
	if(vals[0]/vals[2]<.00001)
		return false;
	return true;
}

#endif
