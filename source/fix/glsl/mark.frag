#version 330 core
// vim: set filetype=cpp :vim

uniform vec3 color;
uniform float umpp;

flat in vec3 cent0;
flat in vec3 cent1;
in vec2 vpos;

layout(location=0) out vec4 fColor;

void main(void) {
	vec2 dp=cent1.xy-cent0.xy;
	vec2 d0=vpos-cent0.xy;

	float a=-dot(dp, dp);
	float b=2*dot(d0, dp);
	float c=4*umpp*umpp-dot(d0, d0);

	float t0=0;
	float t1=1;
	float det=b*b-4*a*c;
	float delta=sign(det)*sqrt(abs(det));
	if(a<0) {
		float v0=(-b+delta)/2/a;
		if(t0<v0)
			t0=v0;
		float v1=(-b-delta)/2/a;
		if(t1>v1)
			t1=v1;
	} else {
		if(b>0) {
			if(0>t0*b+c)
				t0=-c/b;
		} else if(b<0) {
			if(0>t1*b+c)
				t1=-c/b;
		} else {
			if(0>c+t0*b) {
				t0=1;
			}
			if(0>c+t1*b) {
				t1=0;
			}
		}
	}

	if(t0>t1)
		discard;
	fColor=vec4(color, 1.0);
}

