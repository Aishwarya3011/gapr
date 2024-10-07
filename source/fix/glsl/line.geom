#version 330 core
// vim: set filetype=cpp :vim

layout(lines) in;
layout(triangle_strip, max_vertices=12) out;

uniform mat4 mProj;
uniform float umpp;

void main(void) {
	vec3 centa=gl_in[0].gl_Position.xyz/gl_in[0].gl_Position.w;
	vec3 centb=gl_in[1].gl_Position.xyz/gl_in[1].gl_Position.w;
	vec3 cent0, cent1;
	if(centa.z<centb.z) {
		cent0=centa;
		cent1=centb;
	} else {
		cent1=centa;
		cent0=centb;
	}

	vec2 u=cent1.xy-cent0.xy;
	float l=length(u);
	if(l<umpp) {
		u=vec2(1, 0);
	} else {
		u=u/l;
	}

	vec2 v=vec2(-u.y, u.x);
	vec2 vpos=cent1.xy+u*umpp+v*umpp;
	gl_Position=mProj*vec4(vpos, cent1.z, 1.0);
	EmitVertex();
	vpos=cent1.xy+u*umpp-v*umpp;
	gl_Position=mProj*vec4(vpos, cent1.z, 1.0);
	EmitVertex();
	vpos=cent0.xy+u*umpp+v*umpp;
	gl_Position=mProj*vec4(vpos, cent0.z, 1.0);
	EmitVertex();
	vpos=cent0.xy+u*umpp-v*umpp;
	gl_Position=mProj*vec4(vpos, cent0.z, 1.0);
	EmitVertex();
	vpos=cent0.xy-u*umpp+v*umpp;
	gl_Position=mProj*vec4(vpos, cent0.z, 1.0);
	EmitVertex();
	vpos=cent0.xy-u*umpp-v*umpp;
	gl_Position=mProj*vec4(vpos, cent0.z, 1.0);
	EmitVertex();
}

