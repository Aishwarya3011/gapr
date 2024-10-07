#version 330 core
// vim: set filetype=cpp :vim

uniform mat4 mProj;
uniform float umpp;

uniform uint idx;

uniform vec4 color_edge[5];

flat in vec3 cent0;
flat in vec3 cent1;
in vec2 vpos;

flat in uint nid0;
flat in uint pr_cnt_pair;

layout(location=0) out uint edgeId;
layout(location=1) out uint edgePos;
layout(location=2) out vec4 fColor;

void main(void) {
	vec2 dp=cent1.xy-cent0.xy;
	vec2 d0=vpos-cent0.xy;

	float a=-dot(dp, dp);
	float b=2*dot(d0, dp);
	float c=umpp*umpp-dot(d0, d0);

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
	} else if(a>0) {
		if(a+b>0) {
			float v0=(-b+delta)/2/a;
			if(t0<v0)
				t0=v0;
		} else {
			float v1=(-b-delta)/2/a;
			if(t1>v1)
				t1=v1;
		}
	} else {
		// a==0
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

	float dz=cent1.z-cent0.z;
	float dz2=dz*dz;

	float aa=4*a*(a-dz2);
	float bb=4*b*(a-dz2);
	float cc=b*b-4*dz2*c;

	float rt2=bb*bb-4*aa*cc;
	float rt=sign(rt2)*sqrt(abs(rt2));
	float ta, tb;
	if(aa>0) {
		ta=(-bb+rt)/aa/2;
		tb=(-bb-rt)/aa/2;
	} else if(aa<0) {
		ta=(-bb+rt)/aa/2;
		tb=(-bb-rt)/aa/2;
	} else {
		if(bb>0) {
			ta=tb=-cc/bb;
		} else if(bb<0) {
			ta=tb=-cc/bb;
		} else {
			ta=tb=t0;
		}
	}

	if(ta<t0)
		ta=t0;
	if(ta>t1)
		ta=t1;
	if(tb<t0)
		tb=t0;
	if(tb>t1)
		tb=t1;

	float t=t0;
	float v=t0*dz+cent0.z+sqrt(abs(a*t0*t0+b*t0+c));
	float v1=t1*dz+cent0.z+sqrt(abs(a*t1*t1+b*t1+c));
	if(v1>v) {
		v=v1;
		t=t1;
	}
	float va=ta*dz+cent0.z+sqrt(abs(a*ta*ta+b*ta+c));
	if(va>v) {
		v=va;
		t=ta;
	}
	float vb=tb*dz+cent0.z+sqrt(abs(a*tb*tb+b*tb+c));
	if(vb>v) {
		v=vb;
		t=tb;
	}

	float r=umpp;
	float alpha=0;
	// (r-umpp)^2<=r-sqrt(r^2-dz^2)<2*umpp
	float tt=clamp(t, 0, 1);
	tt=step(.5, (pr_cnt_pair&uint(1))*(1-tt)+(pr_cnt_pair>>1)*tt);
	alpha=1.0;//tt*.5+.5;

	//alpha=1-(v-t*dz-cent0.z)/r;
	vec4 ppos=mProj*vec4(vpos, v, 1.0);
	gl_FragDepth=(ppos.z/ppos.w+1)/2;

	edgeId=idx;
	edgePos=nid0*16u+uint(clamp(t, 0, 1)*16);
	//fColor=vec4(t, t0, t1, 1);
	//fColor=vec4(t, alpha, 0, 1);
	if(pr_cnt_pair!=uint(3)) {
		if(abs(t-.5)<.3)
			alpha=0.01;
		else
			alpha=0.7;
	}
	
	fColor=vec4(color_edge[0].rgb*tt+vec3(1.0, 0, 0)*(1-tt), alpha);
}

