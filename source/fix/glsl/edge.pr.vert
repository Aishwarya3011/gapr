#version 330 core
// vim: set filetype=cpp :vim

uniform mat4 mView;

uniform ivec3 p0int;

layout(location=0) in ivec3 pos;
layout(location=1) in uint misc;

flat out uint nid;
flat out uint pr_cnt;

void main(void) {
	gl_Position=mView*vec4((pos-p0int)/1024.0, 1.0);
	nid=uint(max(gl_VertexID, 0));
	pr_cnt=(misc>>16)&uint(1);
}
