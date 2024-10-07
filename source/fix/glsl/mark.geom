#version 330 core
// vim: set filetype=cpp :vim

layout(lines) in;
layout(triangle_strip, max_vertices=12) out;

uniform mat4 mProj;
uniform float umpp;

flat out vec3 cent0;
flat out vec3 cent1;
out vec2 vpos;

void main(void) {
	cent0=gl_in[0].gl_Position.xyz/gl_in[0].gl_Position.w;
	cent1=gl_in[1].gl_Position.xyz/gl_in[1].gl_Position.w;

	vec2 u=cent1.xy-cent0.xy;
	float l=length(u);
	//if(l<umpp) {
		//u=vec2(1, 0);
	//} else {
		u=u/l;
	//}

	vec2 v=vec2(-u.y, u.x);
	vpos=cent1.xy+u*umpp+v*umpp;
	gl_Position=mProj*vec4(vpos, cent1.z, 1.0);
	//gl_Position.z=-gl_Position.w;
	EmitVertex();
	vpos=cent1.xy+u*umpp-v*umpp;
	gl_Position=mProj*vec4(vpos, cent1.z, 1.0);
	//gl_Position.z=-gl_Position.w;
	EmitVertex();
	vpos=cent0.xy+u*umpp+v*umpp;
	gl_Position=mProj*vec4(vpos, cent0.z, 1.0);
	//gl_Position.z=-gl_Position.w;
	EmitVertex();
	vpos=cent0.xy+u*umpp-v*umpp;
	gl_Position=mProj*vec4(vpos, cent0.z, 1.0);
	//gl_Position.z=-gl_Position.w;
	EmitVertex();
	vpos=cent0.xy-u*umpp+v*umpp;
	gl_Position=mProj*vec4(vpos, cent0.z, 1.0);
	//gl_Position.z=-gl_Position.w;
	EmitVertex();
	vpos=cent0.xy-u*umpp-v*umpp;
	gl_Position=mProj*vec4(vpos, cent0.z, 1.0);
	//gl_Position.z=-gl_Position.w;
	EmitVertex();
}

