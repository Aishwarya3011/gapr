#version 300 es
// vim: set filetype=cpp :vim

uniform highp ivec3 pos_offset;
uniform highp mat4 mView;
uniform highp mat4 mProj;
uniform highp float thickness;

layout(location=0) in highp ivec3 pos;
layout(location=1) in highp uint misc;
layout(location=2) in highp uint dir3;

flat out highp vec3 cent;
flat out highp vec3 dir;
flat out highp int nid0;
flat out lowp uint pr_val;
out highp vec3 vpos;
out mediump float edge_pos;

void main(void) {
	highp vec4 pos_tmp=mView*vec4(vec3(pos-pos_offset)/1024.0, 1.0);
	cent=pos_tmp.xyz/pos_tmp.w;
	highp vec3 dir_tmp=vec3((dir3>>20)&1023u, (dir3>>10)&1023u, dir3&1023u)/511.5-1.0;
	highp vec4 vdir=mView*vec4(dir_tmp, 0.0);
	dir=vdir.xyz;
	highp float l=length(vdir.xy);
	highp vec2 dir2;
	highp float s;
	if(gl_VertexID%3!=0)
		s=1.0;
	else
		s=-1.0;
	highp float tval=thickness;
	pr_val=(misc>>16)&1u;
	if(pr_val==0u)
		tval/=2.0;
	if(l<0.000001) {
		dir2=vec2(-1.0, s)*tval;
	} else {
		dir2=vec2(-s*vdir.y-vdir.x, s*vdir.x-vdir.y)*(tval/l);
	}
	vpos=cent+vec3(dir2, 0);
	gl_Position=mProj*vec4(vpos, 1.0);
	nid0=gl_VertexID/6;
	if((gl_VertexID)%2==0)
		edge_pos=0.0;
	else
		edge_pos=1.0;
}

