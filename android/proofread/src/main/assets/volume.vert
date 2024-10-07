#version 300 es
// vim: set filetype=cpp :vim

in highp vec2 pos;
out highp vec2 pos_out;

void main(void) {
	pos_out=pos;
	gl_Position=vec4(pos, 0.0, 1.0);
}

