#version 330 core
// vim: set filetype=cpp :vim

uniform sampler3D tex3d_cube;
uniform vec2 xfunc_cube;

uniform mat4 mrView;
uniform mat4 mrProj;
uniform mat4 mrTex;

uniform vec3 zparsCube;

uniform vec3 color_volume;

in vec2 pos0;

layout(location=0) out vec4 fColor;

////




vec2 getRange(vec2 rng0, mat4 mrTex) {
	float z0=rng0.x;
	float z1=rng0.y;

	vec4 plane=vec4(0, 0, 1.0, 0)*mrTex;
	float den=dot(vec3(0, 0, 1), plane.xyz);
	float num=dot(vec4(pos0, 0, 1.0), plane);
	if(den>0) {
		z0=max(z0, -num/den);
	} else if(den<0) {
		z1=min(z1, -num/den);
	} else if(num<0) {
		return vec2(z0, z0-1);
	}
	plane=vec4(0, 0, 1.0, -1.0)*mrTex;
	den=dot(vec3(0, 0, 1), plane.xyz);
	num=dot(vec4(pos0, 0, 1.0), plane);
	if(den>0) {
		z1=min(z1, -num/den);
	} else if(den<0) {
		z0=max(z0, -num/den);
	} else if(num>0) {
		return vec2(z0, z0-1);
	}

	plane=vec4(0, 1.0, 0, 0)*mrTex;
	den=dot(vec3(0, 0, 1), plane.xyz);
	num=dot(vec4(pos0, 0, 1.0), plane);
	if(den>0) {
		z0=max(z0, -num/den);
	} else if(den<0) {
		z1=min(z1, -num/den);
	} else if(num<0) {
		return vec2(z0, z0-1);
	}
	plane=vec4(0, 1.0, 0, -1.0)*mrTex;
	den=dot(vec3(0, 0, 1), plane.xyz);
	num=dot(vec4(pos0, 0, 1.0), plane);
	if(den>0) {
		z1=min(z1, -num/den);
	} else if(den<0) {
		z0=max(z0, -num/den);
	} else if(num>0) {
		return vec2(z0, z0-1);
	}

	plane=vec4(1.0, 0, 0, 0)*mrTex;
	den=dot(vec3(0, 0, 1), plane.xyz);
	num=dot(vec4(pos0, 0, 1.0), plane);
	if(den>0) {
		z0=max(z0, -num/den);
	} else if(den<0) {
		z1=min(z1, -num/den);
	} else if(num<0) {
		return vec2(z0, z0-1);
	}
	plane=vec4(1.0, 0, 0, -1.0)*mrTex;
	den=dot(vec3(0, 0, 1), plane.xyz);
	num=dot(vec4(pos0, 0, 1.0), plane);
	if(den>0) {
		z1=min(z1, -num/den);
	} else if(den<0) {
		z0=max(z0, -num/den);
	} else if(num>0) {
		return vec2(z0, z0-1);
	}
	return vec2(z0, z1);
}


void main(void) {
	//XXX collapse them
	mat4 mrTexCube=mrTex*mrView*mrProj;
	vec2 rangeCube=getRange(zparsCube.xy, mrTexCube);

	if(rangeCube.x<=rangeCube.y) {
		//fColor=vec4(color_volume.rgb, 1.0);
		//return;
		float dz=zparsCube.z;
		float z1=rangeCube.y;
		float z=rangeCube.x;
		vec2 maxvz=vec2(-1.0, 0.0);
		vec4 tpos0=mrTexCube*vec4(pos0, 0, 1.0);
		vec4 tposd=mrTexCube[2];
		while(z<=z1) {
			vec4 tpos=tpos0+tposd*z;
			float texel=texture(tex3d_cube, tpos.xyz/tpos.w).r;
			float stp=step(texel, maxvz.x);
			maxvz=mix(vec2(texel, (z+1)/2), maxvz, stp);
			z+=dz;
		}
		gl_FragDepth=maxvz.y;
		float a;
		if(maxvz.x<xfunc_cube.x) {
			a=0;
		} else if(maxvz.x>xfunc_cube.y) {
			a=1;
		} else {
			if(xfunc_cube.y-xfunc_cube.x==0) {
				a=.5;
			} else {
				a=(maxvz.x-xfunc_cube.x)/(xfunc_cube.y-xfunc_cube.x);
			}
		}
		fColor=vec4(color_volume, a);
		//fColor=vec4(1.0, 0, 0, 1.0);
	} else {
		discard;
		//fColor=vec4(1.0, 0, 0, 1.0);
	}
}
