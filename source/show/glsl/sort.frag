#version 330 core
// vim: set filetype=cpp :vim

uniform mediump sampler2D tex_edges_depth;
uniform mediump sampler2D tex_edges_color;
uniform mediump sampler2D tex_opaque_depth;
uniform mediump sampler2D tex_opaque_color;
uniform mediump sampler2D tex_volume0_depth;
uniform mediump sampler2D tex_volume0_color;
uniform mediump int volume_scale;
uniform mediump ivec2 offset;

layout(location=0) out mediump vec4 fColor;

void main(void) {
	mediump ivec2 texpos=ivec2(gl_FragCoord.xy);
	mediump float opaque_depth=texelFetch(tex_opaque_depth, texpos, 0).r;
	mediump float edges_depth=texelFetch(tex_edges_depth, texpos+offset, 0).r;
	mediump float depth0;
	mediump vec4 color0;
	// XXX opaque, currently used for meshes
	mediump vec4 opaque_col=texelFetch(tex_opaque_color, texpos, 0);
	mediump vec4 edges_col=texelFetch(tex_edges_color, texpos+offset, 0);
	if(edges_depth<opaque_depth || opaque_col.a<.1) {
		depth0=edges_depth;
		color0=edges_col;
	} else {
		depth0=opaque_depth;
		opaque_col.a=clamp((opaque_col.a-.1)/.9, 0, 1);
		opaque_col.a=opaque_col.a*opaque_col.a;
		color0=vec4(opaque_col.rgb, 1.0)*opaque_col.a+edges_col*(1-opaque_col.a);
	}

	mediump float alpha=color0.a;

	mediump float aaa=0.0;
	if(depth0<1.0)
		aaa=1.0;
		mediump float vol_depth=texelFetch(tex_volume0_depth, texpos/volume_scale, 0).r;
		mediump vec4 vol_color=texelFetch(tex_volume0_color, texpos/volume_scale, 0);
		vol_color=vec4(1.0-0.0*vol_depth, 1.0-0.0*vol_depth, 1.0, vol_color.r);
		//if(vol_depth>=depth0) {
			//color_sum+=(1-color0.a)*vol_color.rgb*vol_color.a;
		//} else {
			mediump vec3 color_sum=color0.rgb*(aaa*color0.a)+(1.0-aaa*color0.a)*vol_color.rgb*vol_color.a;
			alpha=aaa*color0.a+(1.0-aaa*color0.a)*vol_color.a;
		//}



	//fColor=vec4(1.0, 0, 0, 1.0);
	//fColor=texelFetch(tex_opaque_color, texpos, 0);
	//fColor=vec4(depths[1-mi], 0, 0, 1);
	//return;
	 fColor=vec4(color_sum, alpha);
	//fColor=vec4(texelFetch(tex_edges_depth, texpos, 0).rgb, 1.0);
	//fColor=vec4(texelFetch(tex_volume0_color, texpos, 0).a, 1.0, 0.0, 1.0);
	//gl_FragDepth=depth0-1;
	//gl_FragDepth=depth0-1;
}

