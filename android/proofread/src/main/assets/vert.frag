#version 300 es
// vim: set filetype=cpp :vim

uniform mediump vec3 color;
uniform highp uint idx;
uniform highp int nid;

layout(location=0) out mediump vec4 fColor;
layout(location=1) out highp uint edgeId;
layout(location=2) out highp int edgePos;

void main(void) {
	fColor=vec4(color, 1.0);
	edgeId=idx;
	edgePos=nid;
}

