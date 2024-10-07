#version 330 core
// vim: set filetype=glsl :vim

uniform highp mat4 mView;
uniform highp mat4 mProj;
uniform highp ivec3 pos_offset;

layout(location=0) in highp ivec3 pos;
layout(location=1) in mediump vec3 norm;

out mediump vec3 vnorm;

void main(void) {
	vnorm=mat3(mView)*norm.zyx;
	gl_Position=mProj*(mView*vec4((pos-pos_offset)/1024.0, 1.0));
}

