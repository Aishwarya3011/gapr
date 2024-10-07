#include <fstream>
#include "dup/serialize-delta.hh"

#include "misc.hh"

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

enum class MouseMode {
	Nul,
	Rot=101,
	Zoom=102,
	DataOnly=103,
	Xfunc=104,
	SkipMisc=105,
	PrEnd=106,
	ExtBr=107,
	JumpAddRep=108,
	Xfunc2=109,
	Cursor=201,
};
struct MouseState {
	int32_t id;
	MouseMode mode{MouseMode::Nul};
	int64_t down_time;
	float down_x, down_y;
	float x, y;
	int32_t move_cnt;
};

/*! the same as gapr::node_attr::data_type,
 * use GLuint to ensure OpenGL compatibility.
 */
struct PointGL {
	std::array<GLint, 3> pos;
	GLuint misc;
	GLuint dir3;
};

void pathToPathGL(const std::array<gapr::edge_model::point, 2>& points, gapr::vec3<double> dir, std::array<PointGL, 6>& out) {
	auto enc_dir=[](const gapr::vec3<double>& dir) -> GLuint {
		GLuint s=0;
		unsigned int i=0;
		do {
			auto v=std::lround((dir[i++]+1)*511.5);
			if(v<0)
				v=0;
			else if(v>1023)
				v=1023;
			s=s|v;
			if(i>=3)
				return s;
			s<<=10;
		} while(true);
	};
	auto l=dir.mag();
	if(l>.1)
		dir/=l;
	auto dir0=enc_dir(dir);
	dir=-dir;
	auto dir1=enc_dir(dir);
	for(unsigned int k=0; k<3; k++) {
		out[2*k]=PointGL{points[0].first, points[0].second, dir0};
		out[2*k+1]=PointGL{points[1].first, points[1].second, dir1};
	}
}

const std::vector<PointGL> pathToPathGL(const std::vector<gapr::edge_model::point>& points) {
	std::vector<PointGL> path;
	if(points.size()<2)
		return path;
	path.reserve((points.size()-1)*6);
	auto get_pos=[](const gapr::edge_model::point& pt) -> gapr::vec3<double> {
		gapr::vec3<double> pos;
		gapr::node_attr a{pt};
		for(unsigned int i=0; i<3; i++)
			pos[i]=a.pos(i);
		return pos;
	};
	auto prev_pos=get_pos(points[0]);
	std::array<PointGL, 6> seg;
	for(std::size_t i=1; i<points.size(); i++) {
		auto& pt=points[i];
		auto& prev_pt=points[i-1];
		auto cur_pos=get_pos(pt);
		pathToPathGL({prev_pt, pt}, cur_pos-prev_pos, seg);
		for(auto& p: seg)
			path.push_back(p);
		prev_pos=cur_pos;
	}
	return path;
}

