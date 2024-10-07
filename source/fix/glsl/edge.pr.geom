#version 330 core
// vim: set filetype=cpp :vim

layout(lines) in;
layout(triangle_strip, max_vertices=12) out;

uniform mat4 mProj;
uniform float umpp;

flat in uint nid[];
flat in uint pr_cnt[];

flat out vec3 cent0;
flat out vec3 cent1;
out vec2 vpos;

flat out uint nid0;
flat out uint pr_cnt_pair;

void main(void) {
	cent0=gl_in[0].gl_Position.xyz/gl_in[0].gl_Position.w;
	cent1=gl_in[1].gl_Position.xyz/gl_in[1].gl_Position.w;

	nid0=nid[0];
	pr_cnt_pair=(pr_cnt[0]|(pr_cnt[1]<<1));
	float umppx=umpp;
	if(pr_cnt_pair!=uint(3))
		umppx/=2;

	vec2 u=cent1.xy-cent0.xy;
	float l=length(u);
	if(l<umppx) {
		vec2 cent;
		float z;
		cent=cent0.xy;
		z=cent0.z;
		vpos=cent+vec2(umppx, umppx);
		gl_Position=mProj*vec4(vpos, z, 1.0);
		EmitVertex();
		vpos=cent+vec2(umppx, -umppx);
		gl_Position=mProj*vec4(vpos, z, 1.0);
		EmitVertex();
		vpos=cent+vec2(-umppx, umppx);
		gl_Position=mProj*vec4(vpos, z, 1.0);
		EmitVertex();
		vpos=cent+vec2(-umppx, -umppx);
		gl_Position=mProj*vec4(vpos, z, 1.0);
		EmitVertex();
	} else {
		u=u/l;
		vec2 v=vec2(-u.y, u.x);
		vpos=cent1.xy+u*umppx+v*umppx;
		gl_Position=mProj*vec4(vpos, cent1.z, 1.0);
		EmitVertex();
		vpos=cent1.xy+u*umppx-v*umppx;
		gl_Position=mProj*vec4(vpos, cent1.z, 1.0);
		EmitVertex();
		vpos=cent0.xy+u*umppx+v*umppx;
		gl_Position=mProj*vec4(vpos, cent0.z, 1.0);
		EmitVertex();
		vpos=cent0.xy+u*umppx-v*umppx;
		gl_Position=mProj*vec4(vpos, cent0.z, 1.0);
		EmitVertex();
		vpos=cent0.xy-u*umppx+v*umppx;
		gl_Position=mProj*vec4(vpos, cent0.z, 1.0);
		EmitVertex();
		vpos=cent0.xy-u*umppx-v*umppx;
		gl_Position=mProj*vec4(vpos, cent0.z, 1.0);
		EmitVertex();
	}
	EndPrimitive();
}

