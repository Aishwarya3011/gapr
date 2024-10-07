#version 330 core
// vim: set filetype=glsl :vim

uniform mediump vec4 color;

in mediump vec3 vnorm;

layout(location=0) out mediump vec4 fColor;

void main(void) {
	mediump vec3 eye=vec3(0, 0, -1);
	//vec3 light=vec3(.3, -.3, -.906);
	mediump vec3 light=vec3(0, 0, -1);
	mediump float a=1-0.9999*abs(dot(light, vnorm));
	if(vnorm.z<0)
		discard;
		//a=1.0;
	fColor=vec4(color.rgb, a);
}

