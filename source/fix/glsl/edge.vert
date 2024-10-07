#version 330 core
// vim: set filetype=cpp :vim

uniform highp ivec3 pos_offset;
uniform highp mat4 mView;

layout(location=0) in highp ivec3 pos;
layout(location=1) in highp uint misc;

#ifdef PICK_MODE
flat out highp int nid;
#endif /* PICK_MODE */
#ifdef DIFF_MODE
flat out highp uint misc_vo;
#else
flat out highp uint pr_cnt;
#endif /* DIFF_MODE */

void main(void) {
#ifdef PICK_MODE
	nid=gl_VertexID;
#endif /* PICK_MODE */
#ifdef DIFF_MODE
	misc_vo=misc;
#else
	pr_cnt=(misc>>16)&uint(1);
#endif /* DIFF_MODE */
#if 0
	// XXX for noisy position.../add noise once in edge_model (optional)
	uint rrr=uint(gl_VertexID);
	rrr=rrr*uint(0x27d4eb2d)+(rrr>>uint(4));
	rrr+=edge_id;
	rrr=rrr*uint(0x27d4eb2d)+(rrr>>uint(4));
	ivec3 rr=ivec3(
			int((rrr>>uint(9))&uint(0xff))-int(0x80),
			int((rrr>>uint(18))&uint(0xff))-int(0x80),
			int((rrr>>uint(27))&uint(0xff))-int(0x80));
#endif
	gl_Position=mView*vec4(vec3(pos-pos_offset)/1024.0, 1.0);
}
