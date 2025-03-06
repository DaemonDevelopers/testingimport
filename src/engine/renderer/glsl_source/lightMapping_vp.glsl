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

/* lightMapping_vp.glsl */

#insert common
#insert vertexSimple_vp
#insert vertexSkinning_vp
#insert vertexAnimation_vp
#insert shaderProfiler_vp

#if !defined(USE_BSP_SURFACE)
	#define USE_MODEL_SURFACE
#endif

#if !defined(USE_GRID_LIGHTING)
	#define USE_LIGHT_MAPPING
#endif

#if !defined(USE_MATERIAL_SYSTEM)
	uniform mat3x2 u_TextureMatrix;
#endif

#if defined(USE_MODEL_SURFACE)
	uniform mat4 u_ModelMatrix;
#endif

uniform mat4		u_ModelViewProjectionMatrix;

uniform float		u_Time;

uniform colorModulatePack u_ColorModulateColorGen;
uniform colorPack u_Color;

OUT(smooth) vec3	var_Position;
OUT(smooth) vec2	var_TexCoords;

#if defined(USE_LIGHT_MAPPING) || defined(USE_DELUXE_MAPPING)
	OUT(smooth) vec2 var_TexLight;
#endif

OUT(smooth) vec3	var_Tangent;
OUT(smooth) vec3	var_Binormal;
OUT(smooth) vec3	var_Normal;

OUT(smooth) vec4	var_Color;

void DeformVertex(inout vec4 pos, inout vec3 normal, inout vec2 st, inout vec4 color, in float time);

void main()
{
	#insert material_vp

	localBasis LB;
	vec4 position, color;
	vec2 texCoord, lmCoord;

	VertexFetch(position, LB, color, texCoord, lmCoord);

	// assign color
	ColorModulateColor( u_ColorModulateColorGen, u_Color, color );

	DeformVertex(position, LB.normal, texCoord, color, u_Time);

	// transform vertex position into homogenous clip-space
	gl_Position = u_ModelViewProjectionMatrix * position;

	#if defined(USE_BSP_SURFACE)
		// assign vertex Position
		var_Position = position.xyz;

		var_Tangent = LB.tangent;
		var_Binormal = LB.binormal;
		var_Normal = LB.normal;
	#else
		// transform position into world space
		var_Position = (u_ModelMatrix * position).xyz;

		var_Tangent = (u_ModelMatrix * vec4(LB.tangent, 0.0)).xyz;
		var_Binormal = (u_ModelMatrix * vec4(LB.binormal, 0.0)).xyz;
		var_Normal = (u_ModelMatrix * vec4(LB.normal, 0.0)).xyz;
	#endif

	#if defined(USE_LIGHT_MAPPING) || defined(USE_DELUXE_MAPPING)
		var_TexLight = lmCoord;
	#endif

	SHADER_PROFILER_SET

	// transform diffusemap texcoords
	var_TexCoords = (u_TextureMatrix * vec3(texCoord, 1.0)).st;

	// assign color
	var_Color = color;
}
