#version 330 core
// vim: set filetype=cpp :vim

uniform mat4 mView;

uniform ivec3 p0int;

layout(location=0) in ivec3 pos;

void main(void) {
	gl_Position=mView*vec4((pos-p0int)/1024.0, 1.0);
}

