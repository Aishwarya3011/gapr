/* gapr/vec3.hh
 *
 * Copyright (C) 2020 GOU Lingfeng
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
#ifndef _GAPR_INCLUDE_VEC3_HH_
#define _GAPR_INCLUDE_VEC3_HH_

#include <array>
#include <cmath>
#include <ostream>

namespace gapr {

template<typename T=double>
class vec3 {
	public:
		constexpr vec3() noexcept { }
		constexpr vec3(T v0, T v1, T v2) noexcept: _v{v0, v1, v2} { }
		template<typename T2>
		constexpr vec3(const vec3<T2>& r) noexcept {
			for(unsigned int i=0; i<3; i++)
				_v[i]=r[i];
		}
		template<typename T2>
		constexpr vec3& operator=(const vec3<T2>& r) noexcept {
			for(unsigned int i=0; i<3; i++)
				_v[i]=r[i];
			return *this;
		}

		constexpr T& operator[](unsigned int i) noexcept {
			return _v[i];
		}
		constexpr const T& operator[](unsigned int i) const noexcept {
			return const_cast<vec3&>(*this)[i];
		}

		T mag2() const {
			T s{0};
			for(unsigned int i=0; i<3; i++) {
				auto v=_v[i];
				s+=v*v;
			}
			return s;
		}
		T mag() const { return std::sqrt(mag2()); }

		operator const std::array<T, 3>&() const noexcept { return _v; }

	private:
		std::array<T, 3> _v;
};

template<typename T1, typename T2>
inline constexpr bool operator==(const vec3<T1>& a, const vec3<T2>& b) noexcept {
	return a[0]==b[0] && a[1]==b[1] && a[2]==b[2];
}
template<typename T1, typename T2>
inline constexpr bool operator!=(const vec3<T1>& a, const vec3<T2>& b) noexcept {
	return !(a==b);
}
template<typename T, typename T2, typename=std::enable_if_t<std::is_arithmetic_v<T2>>>
inline vec3<T> operator*(T2 a, const vec3<T>& b) noexcept {
	vec3<T> r;
	for(unsigned int i=0; i<3; i++)
		r[i]=a*b[i];
	return r;
}
template<typename T, typename T2>
inline vec3<T> operator*(const vec3<T>& b, T2 a) noexcept {
	return a*b;
}
template<typename T, typename T2>
inline T dot(const vec3<T>& b, const vec3<T2> a) noexcept {
	double r{0};
	for(unsigned int i=0; i<3; i++)
		r+=a[i]*b[i];
	return r;
}
template<typename T, typename T2, typename=std::enable_if_t<std::is_arithmetic_v<T2>>>
inline vec3<T> operator/(const vec3<T>& b, T2 a) noexcept {
	vec3<T> r;
	for(unsigned int i=0; i<3; i++)
		r[i]=b[i]/a;
	return r;
}
template<typename T, typename T2, typename=std::enable_if_t<std::is_arithmetic_v<T2>>>
inline vec3<T>& operator/=(vec3<T>& b, T2 a) noexcept {
	for(unsigned int i=0; i<3; i++)
		b[i]/=a;
	return b;
}
template<typename T, typename T2, typename=std::enable_if_t<std::is_arithmetic_v<T2>>>
inline vec3<T>& operator+=(vec3<T>& b, vec3<T2> a) noexcept {
	for(unsigned int i=0; i<3; i++)
		b[i]+=a[i];
	return b;
}
template<typename T>
inline vec3<T> operator-(const vec3<T>& a, const vec3<T>& b) noexcept {
	vec3<T> r;
	for(unsigned int i=0; i<3; i++)
		r[i]=a[i]-b[i];
	return r;
}
template<typename T>
inline vec3<T>& operator-=(vec3<T>& a, const vec3<T>& b) noexcept {
	for(unsigned int i=0; i<3; i++)
		a[i]-=b[i];
	return a;
}
template<typename T>
inline vec3<T> operator+(const vec3<T>& a, const vec3<T>& b) noexcept {
	vec3<T> r;
	for(unsigned int i=0; i<3; i++)
		r[i]=a[i]+b[i];
	return r;
}
template<typename T>
inline vec3<T> operator-(const vec3<T>& a) noexcept {
	vec3<T> r;
	for(unsigned int i=0; i<3; i++)
		r[i]=-a[i];
	return r;
}

template<typename T>
inline vec3<T> cross(const vec3<T>& a, const vec3<T>& b) {
	vec3<T> r;
	r[0]=a[1]*b[2]-a[2]*b[1];
	r[1]=a[2]*b[0]-a[0]*b[2];
	r[2]=a[0]*b[1]-a[1]*b[0];
	return r;
}


template<typename T=double>
class mat4 {
	public:
		T& operator()(unsigned int i, unsigned int j) noexcept {
			return elem(i, j);
		}
		const T& operator()(unsigned int i, unsigned int j) const noexcept {
			return const_cast<mat4&>(*this)(i, j);
		}
		template<typename T2>
		mat4& operator=(const mat4<T2>& r) noexcept {
			for(unsigned int j=0; j<4; j++)
				for(unsigned int i=0; i<4; i++)
					elem(i, j)=r(i, j);
			return *this;
		}

		void set_identity() noexcept {
			for(unsigned int i=0; i<4; i++) {
				for(unsigned int j=0; j<4; j++)
					elem(i, j)=(i==j?1:0);
			}
		}
		template<typename T2>
		void look_at(const vec3<T2>& eye, const vec3<T2>& center, const vec3<T2>& up, mat4<T>* inv=nullptr) {
			//2 *same
			auto f=center-eye;
			f/=f.mag();
			auto u=up/up.mag();
			auto s=cross(f, u);
			s/=s.mag();
			u=cross(s, f);
			for(unsigned int j=0; j<3; j++) {
				elem(0, j)=s[j];
				elem(1, j)=u[j];
				elem(2, j)=-f[j];
				elem(3, j)=0;
			}
			for(unsigned int i=0; i<3; i++) {
				double s{0};
				for(unsigned int j=0; j<3; j++)
					s+=elem(i, j)*eye[j];
				elem(i, 3)=-s;
			}
			elem(3, 3)=1;
			if(inv) {
				auto& ii=inv->_v;
				for(unsigned int j=0; j<3; j++) {
					ii[j+4*0]=s[j];
					ii[j+4*1]=u[j];
					ii[j+4*2]=-f[j];
					ii[3+4*j]=0;
					ii[j+4*3]=eye[j];
				}
				ii[3+4*3]=1;
			}
		}
		void ortho(T left, T right, T bottom, T top, T near_plane, T far_plane, mat4<T>* inv=nullptr) {
			vec3<T> a, b;
			a[0]=2/(right-left);
			a[1]=2/(top-bottom);
			a[2]=-2/(far_plane-near_plane);
			b[0]=-(right+left)/(right-left);
			b[1]=-(top+bottom)/(top-bottom);
			b[2]=-(far_plane+near_plane)/(far_plane-near_plane);
			for(unsigned int j=0; j<3; j++) {
				for(unsigned int i=0; i<4; i++)
					elem(i, j)=(i==j)?a[i]:0;
				elem(j, 3)=b[j];
			}
			elem(3, 3)=1;
			if(inv) {
				auto& ii=inv->_v;
				for(unsigned int j=0; j<3; j++) {
					for(unsigned int i=0; i<4; i++)
						ii[i+4*j]=(i==j)?1/a[i]:0;
				}
				for(unsigned int i=0; i<3; i++) {
					double s{0};
					for(unsigned int j=0; j<3; j++)
						s+=ii[i+4*j]*b[j];
					ii[i+4*3]=-s;
				}
				ii[3+4*3]=1;
			}
		}
		void rotate(T angle_rad, const vec3<T>& axis) {
			auto c=std::cos(angle_rad);
			auto s=std::sin(angle_rad);
			auto a=axis/axis.mag();
			for(unsigned int j=0; j<3; j++) {
				for(unsigned int i=0; i<3; i++) {
					double d;
					if(i<j)
						d=s*a[2-(i+j+2)%3]*((j-i-1)?1.0:-1.0);
					else if(i>j)
						d=s*a[2-(i+j+2)%3]*((i-j-1)?-1.0:1.0);
					else
						d=c;
					elem(i, j)=a[i]*a[j]*(1-c)+d;
				}
				elem(3, j)=0;
				elem(j, 3)=0;
			}
			elem(3, 3)=1;
		}
		void inverse(const mat4<T>& m) {
			auto det3=[&m](unsigned int ii, unsigned int jj) noexcept ->double {
				std::array<std::array<double, 3>, 3> a;
				for(unsigned int j=0, j1=0; j<4; j++) {
					if(j==jj)
						continue;
					for(unsigned int i=0, i1=0; i<4; i++) {
						if(i==ii)
							continue;
						a[j1][i1++]=m(i, j);
					}
					j1++;
				}
				return a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])
					+a[0][1]*(a[1][2]*a[2][0]-a[1][0]*a[2][2])
					+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
			};
			for(unsigned int j=0; j<4; j++)
				for(unsigned int i=0; i<4; i++)
					elem(i, j)=((i+j)%2==0)?det3(j, i):-det3(j, i);
			double d=m(0,0)*elem(0,0)+m(1,0)*elem(0,1)
				+m(2,0)*elem(0,2)+m(3,0)*elem(0,3);
			for(unsigned int j=0; j<4; j++)
				for(unsigned int i=0; i<4; i++)
					elem(i, j)/=d;
		}

	private:
		std::array<T, 4*4> _v;
		T& elem(unsigned int i, unsigned int j) noexcept { return _v[i+4*j]; }
};

template<typename T, typename T2>
inline vec3<T> operator*(const mat4<T>& a, const vec3<T2>& b) noexcept {
	vec3<T> r;
	double c=a(3, 3);
	for(unsigned int j=0; j<3; j++)
		c+=a(3, j)*b[j];
	for(unsigned int i=0; i<3; i++) {
		double s=a(i, 3);
		for(unsigned int j=0; j<3; j++)
			s+=a(i, j)*b[j];
		r[i]=s/c;
	}
	return r;
}

template<typename T, typename T2>
inline mat4<T> operator*(const mat4<T>& a, const mat4<T2>& b) noexcept {
	mat4<T> r;
	for(unsigned int j=0; j<4; j++) {
		for(unsigned int i=0; i<4; i++) {
			double s=0;
			for(unsigned int k=0; k<4; k++) {
				s+=a(i, k)*b(k, j);
			}
			r(i, j)=s;
		}
	}
	return r;
}

template<typename T>
inline std::ostream& operator<<(std::ostream& s, const mat4<T>& m) {
	s<<'[';
	for(unsigned int i=0; i<4; i++) {
		if(i>0)
			s<<"; ";
		for(unsigned int j=0; j<4; j++) {
			if(j>0)
				s<<' ';
			s<<m(i, j);
		}
	}
	return s<<']';
}
template<typename T>
inline std::ostream& operator<<(std::ostream& s, const vec3<T>& m) {
	s<<'[';
	for(unsigned int j=0; j<3; j++) {
		if(j>0)
			s<<' ';
		s<<m[j];
	}
	return s<<']';
}

}

#endif
