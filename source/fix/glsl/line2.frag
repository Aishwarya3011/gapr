#version 330 core
// vim: set filetype=glsl :vim

uniform mediump vec3 color;

layout(location=0) out mediump vec4 fColor;


void main(void) {
	fColor=vec4(color, 1.0);
}

