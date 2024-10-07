#version 330 core
// vim: set filetype=cpp :vim

#ifndef PICK_MODE
uniform mediump vec3 colors[8];
#else /* PICK_MODE */
uniform highp uint edge_id;
#endif /* PICK_MODE */

#ifndef PICK_MODE
layout(location=0) out mediump vec4 fColor;
#else /* PICK_MODE */
layout(location=0) out highp uint edgeId;
layout(location=1) out highp int edgePos;
#endif /* PICK_MODE */

in highp vec2 vpos;
#ifndef PICK_MODE
#ifdef DIFF_MODE
flat in highp uint misc_eo;
#endif /* DIFF_MODE */
#else /* PICK_MODE */
flat in highp int nid0;
#endif /* PICK_MODE */


void main(void) {
	mediump float l2=dot(vpos, vpos);
	if(l2>1.0)
		discard;
#ifndef PICK_MODE
	mediump float shade=1-l2;
#ifndef DIFF_MODE
#define COLOR_IDX 0
#else /* DIFF_MODE */
#define COLOR_IDX (misc_eo&65535u)
#endif /* DIFF_MODE */
	fColor=vec4((.6+.4*shade)*colors[COLOR_IDX], 1.0);
#else /* PICK_MODE */
	edgeId=edge_id;
	edgePos=nid0;
#endif /* PICK_MODE */
}

