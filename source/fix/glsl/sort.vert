#version 330 core
// vim: set filetype=cpp :vim

layout(location=0) in vec3 pos;

out vec2 pos0;

void main(void) {
	pos0=pos.xy;
	gl_Position=vec4(pos0, 0, 1.0);
}

