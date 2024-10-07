#version 300 es
// vim: set filetype=cpp :vim

uniform mediump vec3 colors[2];
uniform highp float thickness;
uniform highp uint edge_id;

layout(location=0) out mediump vec4 fColor;
layout(location=1) out highp uint edgeId;
layout(location=2) out highp int edgePos;

flat in highp vec3 cent;
flat in highp vec3 dir;
flat in highp int nid0;
flat in lowp uint pr_val;
in highp vec3 vpos;
in mediump float edge_pos;

void main(void) {
	highp vec3 off=vpos-cent;
	if(dot(dir, off)<0.0) {
		if(dot(off, off)>thickness*thickness)
			discard;
		//vec4 ppos=mProj*vec4(vpos, v, 1.0);
		//gl_FragDepth=(ppos.z/ppos.w+1)/2;
	} else {
	}
	mediump float alpha=1.0;
	if(pr_val==0u) {
		if(abs(edge_pos-.5)<.3)
			alpha=0.01;
		else
			alpha=0.4;
	}
	if(pr_val!=0u) {
		fColor=vec4(colors[0], alpha);
	} else {
		fColor=vec4(colors[1], alpha);
	}
	edgeId=edge_id;
	edgePos=nid0*16+int(clamp(edge_pos, 0.0, 1.0)*16.0);
}

