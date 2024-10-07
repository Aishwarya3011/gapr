#version 300 es
// vim: set filetype=cpp :vim

uniform highp mat4 mProj;
uniform highp vec3 center;
uniform highp float umpp;

in highp vec3 pos;

void main(void) {
	gl_Position=mProj*vec4(center+umpp*pos, 1.0);
}

