#version 300 es
// vim: set filetype=cpp :vim

in highp vec2 pos;

void main(void) {
	gl_Position=vec4(pos, 0.0, 1.0);
}

