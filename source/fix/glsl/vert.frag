#version 330 core
// vim: set filetype=cpp :vim

uniform uint idx;
uniform uint nid;
uniform vec4 color;

layout(location=0) out uint edgeId;
layout(location=1) out uint edgePos;
layout(location=2) out vec4 fColor;

void main(void) {
	edgeId=idx;
	edgePos=nid;
	fColor=color;
}

