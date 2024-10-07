#version 330 core
// vim: set filetype=cpp :vim

uniform mat4 mView;
uniform ivec4 center;

uniform ivec3 p0int;
uniform float umpp;

layout(location=0) in vec3 pos;

void main(void) {
	vec4 vpos=mView*vec4((center.xyz-p0int)/1024.0, 1.0);
	gl_Position=vec4(vpos.xyz/vpos.w+pos*center.w*umpp, 1.0);
}
