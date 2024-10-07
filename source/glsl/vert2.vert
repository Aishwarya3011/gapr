#version 330 core
// vim: set filetype=cpp :vim

uniform highp ivec3 pos_offset;
uniform highp mat4 mView;

layout(location=0) in highp ivec3 pos;
layout(location=1) in highp uint misc;

#ifdef PICK_MODE
flat out highp int nid;
#endif
flat out highp uint misc_vo;


void main(void) {
#ifdef PICK_MODE
	nid=gl_VertexID;
#endif
	misc_vo=misc;
	gl_Position=mView*vec4(vec3(pos-pos_offset)/1024.0, 1.0);
}

