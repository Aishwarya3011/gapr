#version 330 core
// vim: set filetype=cpp :vim

uniform sampler2D tex_opaque_depth;
uniform sampler2D tex_opaque_color;
uniform sampler2D tex_surface_depth;
uniform sampler2D tex_surface_color;
uniform sampler2D tex_volume0_depth;
uniform sampler2D tex_volume0_color;
#define nrep 1


in vec2 pos0;
layout(location=0) out vec4 fColor;

void main(void) {
	//int nrep=textureSamples(tex_opaque_depth);
	vec3 colorsum=vec3(0.0, 0.0, 0.0);

	for(int i=0; i<nrep; i++) {
	ivec2 texpos=ivec2(gl_FragCoord.xy);
	float opaque_depth=texelFetch(tex_opaque_depth, texpos, i).r;
	float surface_depth=texelFetch(tex_surface_depth, texpos, 0).r;
	float depth0;
	vec4 color0;
	float depth1;
	vec4 color1;
	depth0=opaque_depth;
	color0=texelFetch(tex_opaque_color, texpos, i);
	if(depth0<=surface_depth) {
		depth1=surface_depth;
		color1=texelFetch(tex_surface_color, texpos, 0);
	} else {
		depth1=depth0;
		color1=color0;
		depth0=surface_depth;
		color0=texelFetch(tex_surface_color, texpos, 0);
	}

	float alpha=color0.a+color1.a*(1-color0.a);
	vec3 color_sum=color0.a*color0.rgb+(alpha-color0.a)*color1.rgb;

	float aaa=0.0;
	if(depth0<1.0)
		aaa=1.0;
		float vol_depth=texelFetch(tex_volume0_depth, texpos, 0).r;
		vec4 vol_color=texelFetch(tex_volume0_color, texpos, 0);
		//if(vol_depth>=depth1) {
			//color_sum+=(1-alpha)*vol_color.rgb*vol_color.a;
		//} else if(vol_depth>=depth0) {
			//color_sum+=(1-color0.a)*vol_color.rgb*vol_color.a;
		//} else {
			color_sum=color_sum*(aaa*color0.a)+(1-aaa*color0.a)*vol_color.rgb*vol_color.a;
		//}



	//fColor=vec4(1.0, 0, 0, 1.0);
	//fColor=texelFetch(tex_opaque_color, texpos, 0);
	//fColor=vec4(depths[1-mi], 0, 0, 1);
	//return;
	colorsum+=color_sum/alpha;
	}
	fColor=vec4(colorsum/nrep, 1.0);
	//fColor=vec4(texelFetch(tex_volume0_color, texpos, 0).a, 1.0, 0.0, 1.0);
	//gl_FragDepth=depth0-1;
	//gl_FragDepth=depth0-1;
}
