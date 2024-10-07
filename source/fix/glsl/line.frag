#version 330 core
// vim: set filetype=cpp :vim

uniform vec3 color;

out vec4 fColor;

void main(void) {
	fColor=vec4(color, 1.0);
}

