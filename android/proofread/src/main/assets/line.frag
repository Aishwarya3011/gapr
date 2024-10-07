#version 300 es
// vim: set filetype=cpp :vim

uniform mediump vec3 color;
uniform highp float thickness;

layout(location=0) out mediump vec4 fColor;

flat in highp vec3 cent;
flat in highp vec3 dir;
in highp vec3 vpos;

void main(void) {
	highp vec3 off=vpos-cent;
	if(dot(dir, off)<0.0) {
		if(dot(off, off)>thickness*thickness)
			discard;
		//vec4 ppos=mProj*vec4(vpos, v, 1.0);
		//gl_FragDepth=(ppos.z/ppos.w+1)/2;
	} else {
	}
	fColor=vec4(color, 1.0);
}

