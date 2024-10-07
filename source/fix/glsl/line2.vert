#version 330 core
// vim: set filetype=glsl :vim

uniform highp mat4 mView;
uniform highp mat4 mProj;

layout(location=0) in highp vec3 pos;


void main(void) {
	gl_Position=mProj*(mView*vec4(pos, 1.0));
}

