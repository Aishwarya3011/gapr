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
flat in highp float vdist;
#ifndef PICK_MODE
flat in mediump float refl;
#ifdef DIFF_MODE
flat in highp uint misc_eo;
#else
flat in highp uint pr_cnt_pair;
#endif /* DIFF_MODE */
#else /* PICK_MODE */
flat in highp int nid0;
#endif /* PICK_MODE */

void main(void) {
	highp float mx=max(abs(vpos.x*2-vdist)-vdist, 0.0);
	highp float y2=vpos.y*vpos.y;
	if(mx*mx+y2*4>4.0)
		discard;
#ifndef PICK_MODE
	mediump float shade=refl*(1-y2);
#ifndef DIFF_MODE
#define ALPHA alpha
#define COLOR_IDX mix(colors[0], vec3(1.0, 0.0, 0.0), tt)
	mediump float tt=0.0;
	mediump float alpha=1.0;
	if(pr_cnt_pair!=uint(3)) {
		if(abs(vpos.x/vdist-.5)<.3)
			alpha=0.01;
		else
			alpha=0.7;
		tt=1.0;
	}
#else /* DIFF_MODE */
#define ALPHA 1.0
#define COLOR_IDX colors[(misc_eo&65535u)]
#endif /* DIFF_MODE */
	fColor=vec4((.6+.4*shade)*COLOR_IDX, ALPHA);
#else /* PICK_MODE */
	edgeId=edge_id;
	edgePos=nid0*16+int(clamp(vpos.x/vdist, 0.0, 1.0)*16.0);
#endif /* PICK_MODE */
}

