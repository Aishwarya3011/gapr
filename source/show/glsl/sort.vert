#version 330 core
// vim: set filetype=cpp :vim

layout(location=0) in highp vec2 pos;

void main(void) {
	gl_Position=vec4(pos, 0.0, 1.0);
}

