#version 330 core
// vim: set filetype=cpp :vim

uniform sampler2D tex_edges_depth;
uniform sampler2D tex_edges_color;
uniform sampler2D tex_opaque_depth;
uniform sampler2D tex_opaque_color;
uniform sampler2D tex_volume0_depth;
uniform sampler2D tex_volume0_color;


in vec2 pos0;
layout(location=0) out vec4 fColor;

void main(void) {
	ivec2 texpos=ivec2(gl_FragCoord.xy);
	float opaque_depth=texelFetch(tex_opaque_depth, texpos, 0).r;
	float edges_depth=texelFetch(tex_edges_depth, texpos, 0).r;
	float depth0;
	vec4 color0;
	if(edges_depth<opaque_depth) {
		depth0=edges_depth;
		color0=texelFetch(tex_edges_color, texpos, 0);
	} else {
		depth0=opaque_depth;
		color0=texelFetch(tex_opaque_color, texpos, 0);
	}

	float alpha=color0.a;

	float aaa=0.0;
	if(depth0<1.0)
		aaa=1.0;
		float vol_depth=texelFetch(tex_volume0_depth, texpos, 0).r;
		vec4 vol_color=texelFetch(tex_volume0_color, texpos, 0);
		//if(vol_depth>=depth0) {
			//color_sum+=(1-color0.a)*vol_color.rgb*vol_color.a;
		//} else {
			vec3 color_sum=color0.rgb*(aaa*color0.a)+(1-aaa*color0.a)*vol_color.rgb*vol_color.a;
			alpha=aaa*color0.a+(1.0-aaa*color0.a)*vol_color.a;
		//}



	//fColor=vec4(1.0, 0, 0, 1.0);
	//fColor=texelFetch(tex_opaque_color, texpos, 0);
	//fColor=vec4(depths[1-mi], 0, 0, 1);
	//return;
	fColor=vec4(color_sum, alpha);
	//fColor=vec4(texelFetch(tex_volume0_color, texpos, 0).a, 1.0, 0.0, 1.0);
	//gl_FragDepth=depth0-1;
	//gl_FragDepth=depth0-1;
}
