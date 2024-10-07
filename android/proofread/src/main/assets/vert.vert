#version 300 es
// vim: set filetype=cpp :vim

uniform highp ivec3 pos_offset;
uniform highp mat4 mView;
uniform highp mat4 mProj;
uniform highp ivec4 center;
uniform highp float umpp;

in highp vec3 pos;

void main(void) {
	highp vec4 vpos=mView*vec4(vec3(center.xyz-pos_offset)/1024.0, 1.0);
	gl_Position=mProj*vec4(vpos.xyz/vpos.w+umpp*float(center.w)*pos, 1.0);
}

