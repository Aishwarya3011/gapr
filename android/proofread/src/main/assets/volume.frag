#version 300 es
// vim: set filetype=cpp :vim

uniform mediump usampler3D tex3d_cube;
uniform highp vec2 xfunc_cube;
uniform highp mat4 mrTexViewProj; //=mrTex*mrView*mrProj;
uniform highp vec3 zparsCube;
uniform mediump vec3 color_volume;

in highp vec2 pos_out;
out mediump vec4 fColor;

highp vec2 getRange(highp vec2 rng0, highp mat4 mrTexCube) {
	highp float z0=rng0.x;
	highp float z1=rng0.y;
	highp vec3 mp=(mrTexCube*vec4(pos_out, 0.0, 1.0)).xyz;
	highp vec3 m2=mrTexCube[2].xyz;

	for(lowp int idx=0; idx<3; idx++) {
		highp float den=m2[idx];
		highp float num=mp[idx];
		if(den>0.00001) {
			z0=max(z0, -num/den);
			z1=min(z1, (1.0-num)/den);
		} else if(den<-0.00001) {
			z1=min(z1, -num/den);
			z0=max(z0, (1.0-num)/den);
		} else if(num<0.00001 || num>0.99999) {
			z0=999.0;
		}
	}
	return vec2(z0, z1);
}

void main(void) {
	highp mat4 mrTexCube=mrTexViewProj;
	highp vec2 rangeCube=getRange(zparsCube.xy, mrTexCube);

	if(rangeCube.x<=rangeCube.y) {
		highp float dz=zparsCube.z;
		highp float z1=rangeCube.y;
		highp float z=rangeCube.x;
		mediump uint max_voxel=0u;
		highp float max_z=-999.0;
		highp vec4 tpos0=mrTexCube*vec4(pos_out, 0.0, 1.0);
		highp vec4 tposd=mrTexCube[2];
		while(z<=z1) {
			highp vec4 tpos=tpos0+tposd*z;
			mediump uint texel=texture(tex3d_cube, tpos.xyz/tpos.w).r;
			if(texel>max_voxel) {
				max_voxel=texel;
				max_z=z;
			}
			z+=dz;
		}
		gl_FragDepth=(max_z+1.0)/2.0;
		highp int aaa=int(max_voxel);
		highp float a=float(aaa)/65535.0;
		if(a<xfunc_cube.x) {
			a=0.0;
		} else if(a>xfunc_cube.y) {
			a=1.0;
		} else {
			if(xfunc_cube.y-xfunc_cube.x<0.00001) {
				a=.5;
			} else {
				a=(a-xfunc_cube.x)/(xfunc_cube.y-xfunc_cube.x);
			}
		}
		//fColor=vec4(color_volume, a);
		fColor=vec4(1.0*a, gl_FragDepth, 0.0, 1.0);
		//fColor=vec4(color_volume*a, 1.0);
		//fColor=vec4(vec3(1.0, 1.0, 1.0)*a, 1.0);
		//fColor=vec4(1.0, 0, 0, 1.0);
	} else {
		discard;
	}
}

