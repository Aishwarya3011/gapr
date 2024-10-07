#version 300 es
// vim: set filetype=cpp :vim

uniform mediump vec3 color;

out mediump vec4 fColor;

void main(void) {
	fColor=vec4(color, 1.0);
}

