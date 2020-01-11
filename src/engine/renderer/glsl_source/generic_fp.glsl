/*
===========================================================================
Copyright (C) 2006-2011 Robert Beckebans <trebor_7@users.sourceforge.net>

This file is part of XreaL source code.

XreaL source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

XreaL source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with XreaL source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

/* generic_fp.glsl */

#insert colorSpace

#define GENERIC_GLSL

uniform sampler2D	u_ColorMap;
uniform float		u_AlphaThreshold;

#if defined(USE_MATERIAL_SYSTEM)
	uniform bool u_ShowTris;
	uniform vec3 u_MaterialColour;
#endif

IN(smooth) vec2		var_TexCoords;
IN(smooth) vec4		var_Color;

#if defined(USE_DEPTH_FADE)
IN(smooth) vec2         var_FadeDepth;
uniform sampler2D       u_DepthMap;
#endif

#insert shaderProfiler_fp

DECLARE_OUTPUT(vec4)

void	main()
{
	#insert material_fp

	#if defined(USE_MATERIAL_SYSTEM)
		if( u_ShowTris ) {
			outputColor = vec4( 0.0, 0.0, 1.0, 1.0 );
			return;
		}
	#endif

	vec4 color = texture2D(u_ColorMap, var_TexCoords);

	if( abs(color.a + u_AlphaThreshold) <= 1.0 )
	{
		discard;
		return;
	}

#if !defined(GENERIC_2D)
	convertFromSRGB(color.rgb, u_LinearizeTexture);
#endif

#if defined(USE_DEPTH_FADE)
	float depth = texture2D(u_DepthMap, gl_FragCoord.xy / r_FBufSize).x;
	float fadeDepth = 0.5 * var_FadeDepth.x / var_FadeDepth.y + 0.5;
	color.a *= smoothstep(gl_FragCoord.z, fadeDepth, depth);
#endif

	color *= var_Color;
	
	SHADER_PROFILER_SET( color )

	outputColor = color;

// Debugging.
#if defined(r_showVertexColors) && !defined(GENERIC_2D)
	outputColor = vec4(0.0, 0.0, 0.0, 0.0);
#elif defined(USE_MATERIAL_SYSTEM) && defined(r_showGlobalMaterials)
	outputColor.rgb = u_MaterialColour;
#endif
}
