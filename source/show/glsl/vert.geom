#version 330 core
// vim: set filetype=cpp :vim

layout(points) in;
layout(triangle_strip, max_vertices=6) out;

uniform highp mat4 mProj;
uniform highp float thickness;

#ifdef PICK_MODE
flat in highp int nid[];
#endif /* PICK_MODE */
flat in highp uint misc_vo[];

out highp vec2 vpos;
#ifndef PICK_MODE
#ifdef DIFF_MODE
flat out highp uint misc_eo;
#endif /* DIFF_MODE */
#else /* PICK_MODE */
flat out highp int nid0;
#endif /* PICK_MODE */


void main(void) {
	highp vec3 cent0=gl_in[0].gl_Position.xyz/gl_in[0].gl_Position.w;
#ifndef DIFF_MODE
	highp float rad=misc_vo[0]*thickness;
#else /* DIFF_MODE */
	highp float rad=(misc_vo[0]>>16)*thickness;
#endif /* DIFF_MODE */

	vpos=vec2(-1.0, 1.0);
#ifndef PICK_MODE
#ifdef DIFF_MODE
	misc_eo=misc_vo[0];
#endif /* DIFF_MODE */
#else /* PICK_MODE */
	nid0=nid[0];
#endif /* PICK_MODE */
	gl_Position=mProj*vec4(cent0+vec3(-rad, rad, 0.0), 1.0);
	EmitVertex();

	vpos=vec2(-1.0, -1.0);
#ifndef PICK_MODE
#ifdef DIFF_MODE
	misc_eo=misc_vo[0];
#endif /* DIFF_MODE */
#else /* PICK_MODE */
	nid0=nid[0];
#endif /* PICK_MODE */
	gl_Position=mProj*vec4(cent0+vec3(-rad, -rad, 0.0), 1.0);
	EmitVertex();

	vpos=vec2(1.0, 1.0);
#ifndef PICK_MODE
#ifdef DIFF_MODE
	misc_eo=misc_vo[0];
#endif /* DIFF_MODE */
#else /* PICK_MODE */
	nid0=nid[0];
#endif /* PICK_MODE */
	gl_Position=mProj*vec4(cent0+vec3(rad, rad, 0.0), 1.0);
	EmitVertex();

	vpos=vec2(1.0, -1.0);
#ifndef PICK_MODE
#ifdef DIFF_MODE
	misc_eo=misc_vo[0];
#endif /* DIFF_MODE */
#else /* PICK_MODE */
	nid0=nid[0];
#endif /* PICK_MODE */
	gl_Position=mProj*vec4(cent0+vec3(rad, -rad, 0.0), 1.0);
	EmitVertex();

	EndPrimitive();
}

