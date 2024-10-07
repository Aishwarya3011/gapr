#version 330 core
// vim: set filetype=cpp :vim

layout(lines) in;
layout(triangle_strip, max_vertices=6) out;

uniform highp mat4 mProj;
#ifdef DIFF_MODE
uniform highp float thickness;
#else
uniform highp vec2 thickness;
#endif

#ifdef PICK_MODE
flat in highp int nid[];
#endif /* PICK_MODE */
#ifdef DIFF_MODE
flat in highp uint misc_vo[];
#else
flat in highp uint pr_cnt[];
#endif /* DIFF_MODE */

out highp vec2 vpos;
flat out highp float vdist;
#ifndef PICK_MODE
flat out mediump float refl;
#ifdef DIFF_MODE
flat out highp uint misc_eo;
#else
flat out highp uint pr_cnt_pair;
#endif /* DIFF_MODE */
#else /* PICK_MODE */
flat out highp int nid0;
#endif /* PICK_MODE */


void main(void) {
	highp vec3 cent0=gl_in[0].gl_Position.xyz/gl_in[0].gl_Position.w;
	highp vec3 cent1=gl_in[1].gl_Position.xyz/gl_in[1].gl_Position.w;
	highp vec3 dir_tmp=cent1-cent0;
	highp float l2=dot(dir_tmp.xy, dir_tmp.xy);
	highp vec2 dir2;
	highp float rad_tmp;
#ifdef DIFF_MODE
	rad_tmp=(misc_vo[1]>>16)*thickness;
#else
	highp uint pr_tmp=(pr_cnt[0]|(pr_cnt[1]<<1));
	rad_tmp=(pr_tmp!=uint(3)?1.0:thickness.x)*thickness.y;
#endif
	if(l2<0.000001) {
		dir2=vec2(1.0, 0.0)*rad_tmp;
	} else {
		dir2=dir_tmp.xy*rad_tmp/sqrt(l2);
	}
	highp float vdist_tmp=dot(dir2, dir_tmp.xy)/(rad_tmp*rad_tmp);
#ifndef PICK_MODE
	highp float z2=dir_tmp.z*dir_tmp.z;
	mediump float refl_tmp=l2/(z2+l2);
#endif /* ! PICK_MODE */

	vpos=vec2(-1.0, 1.0);
	vdist=vdist_tmp;
#ifndef PICK_MODE
	refl=refl_tmp;
#ifdef DIFF_MODE
	misc_eo=misc_vo[1];
#else
	pr_cnt_pair=pr_tmp;
#endif /* DIFF_MODE */
#else /* PICK_MODE */
	nid0=nid[0];
#endif /* PICK_MODE */
	gl_Position=mProj*vec4(cent0+vec3(-dir2.y-dir2.x, dir2.x-dir2.y, 0.0), 1.0);
	EmitVertex();

	vpos=vec2(-1.0, -1.0);
	vdist=vdist_tmp;
#ifndef PICK_MODE
	refl=refl_tmp;
#ifdef DIFF_MODE
	misc_eo=misc_vo[1];
#else
	pr_cnt_pair=pr_tmp;
#endif /* DIFF_MODE */
#else /* PICK_MODE */
	nid0=nid[0];
#endif /* PICK_MODE */
	gl_Position=mProj*vec4(cent0+vec3(dir2.y-dir2.x, -dir2.x-dir2.y, 0.0), 1.0);
	EmitVertex();

	vpos=vec2(vdist_tmp+1.0, 1.0);
	vdist=vdist_tmp;
#ifndef PICK_MODE
	refl=refl_tmp;
#ifdef DIFF_MODE
	misc_eo=misc_vo[1];
#else
	pr_cnt_pair=pr_tmp;
#endif /* DIFF_MODE */
#else /* PICK_MODE */
	nid0=nid[0];
#endif /* PICK_MODE */
	gl_Position=mProj*vec4(cent1+vec3(-dir2.y+dir2.x, dir2.x+dir2.y, 0.0), 1.0);
	EmitVertex();

	vpos=vec2(vdist_tmp+1.0, -1.0);
	vdist=vdist_tmp;
#ifndef PICK_MODE
	refl=refl_tmp;
#ifdef DIFF_MODE
	misc_eo=misc_vo[1];
#else
	pr_cnt_pair=pr_tmp;
#endif /* DIFF_MODE */
#else /* PICK_MODE */
	nid0=nid[0];
#endif /* PICK_MODE */
	gl_Position=mProj*vec4(cent1+vec3(dir2.y+dir2.x, -dir2.x+dir2.y, 0.0), 1.0);
	EmitVertex();

	EndPrimitive();
}

