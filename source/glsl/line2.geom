#version 330 core
// vim: set filetype=glsl :vim

layout(lines) in;
layout(triangle_strip, max_vertices=6) out;

uniform highp vec2 pixel_size;


void main(void) {
	highp vec3 centa=gl_in[0].gl_Position.xyz/gl_in[0].gl_Position.w;
	highp vec3 centb=gl_in[1].gl_Position.xyz/gl_in[1].gl_Position.w;
	highp vec3 cent0, cent1;
	if(centa.z<centb.z) {
		cent0=centa;
		cent1=centb;
	} else {
		cent1=centa;
		cent0=centb;
	}

	highp vec2 u=(cent1.xy-cent0.xy)/pixel_size;
	highp float l=length(u);
	if(l<0.01) {
		u=vec2(1, 0);
	} else {
		u=u/l;
	}
	vec2 v=vec2(-u.y, u.x)*pixel_size;
	u=u*pixel_size;

	gl_Position=vec4(cent1.xy+u+v, cent1.z, 1.0);
	EmitVertex();
	gl_Position=vec4(cent1.xy+u-v, cent1.z, 1.0);
	EmitVertex();
	gl_Position=vec4(cent0.xy-u+v, cent0.z, 1.0);
	EmitVertex();
	gl_Position=vec4(cent0.xy-u-v, cent0.z, 1.0);
	EmitVertex();
}

