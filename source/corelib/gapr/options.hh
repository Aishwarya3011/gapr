#ifndef _GAPR_OPTIONS_H__
#define _GAPR_OPTIONS_H__
// XXX should be for Qt, and should use QSettings

#include <string>
#include <vector>
#include <array>
#include <unordered_map>
//#include <array>

namespace gapr {

	class Options {
		public:
			Options();
			~Options();
			void reset();

			bool isChanged() const;
			void putLine(const std::string& l);
			std::vector<std::string> getLines() const;
			void removeKey(const std::string& key);

			bool getXform(const std::string& key, std::array<double, 9>* x, std::array<double, 3>* p0) const;
			void setXform(const std::string& key, const std::array<double, 9>& x, const std::array<double, 3>& p0);
			bool getXyz(const std::string& key, double* x, double* y, double* z) const;
			void setXyz(const std::string& key, double x, double y, double z);
			bool getXyzInt(const std::string& key, int* x, int* y, int* z) const;
			void setXyzInt(const std::string& key, int x, int y, int z);
			bool getInt(const std::string& key, int* v) const;
			void setInt(const std::string& key, int v);
			bool getDouble(const std::string& key, double* v) const;
			void setDouble(const std::string& key, double v);
			const std::string getString(const std::string& key) const;
			void setString(const std::string& key, const std::string& v);
			const std::vector<std::string> getStringList(const std::string& key) const;
			void setStringList(const std::string& key, const std::vector<std::string>& v);
			//const QList<int> getIntList(const std::string& key) const;
			//void setIntList(const std::string& key, const QList<int>& v);
			bool getXfunc(const std::string& key, double* x, double* y) const;
			void setXfunc(const std::string& key, double x, double y);

		private:
			bool changed;
			std::unordered_map<std::string, std::string> data;
	};

}

#endif
