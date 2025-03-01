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

/* forwardLighting_vp.glsl */

#insert common
#insert vertexSimple_vp
#insert vertexSkinning_vp
#insert vertexAnimation_vp

uniform mat3x2		u_TextureMatrix;
uniform mat4		u_LightAttenuationMatrix;
uniform mat4		u_ModelMatrix;
uniform mat4		u_ModelViewProjectionMatrix;

uniform colorModulatePack u_ColorModulateColorGen;
uniform colorPack u_Color;

uniform float		u_Time;

OUT(smooth) vec3	var_Position;
OUT(smooth) vec2	var_TexCoords;

OUT(smooth) vec4	var_TexAttenuation;

OUT(smooth) vec4	var_Tangent;
OUT(smooth) vec4	var_Binormal;
OUT(smooth) vec4	var_Normal;

OUT(smooth) vec4	var_Color;

void DeformVertex( inout vec4 pos,
		   inout vec3 normal,
		   inout vec2 st,
		   inout vec4 color,
		   in    float time);

void	main()
{
	vec4 position;
	localBasis LB;
	vec2 texCoord, lmCoord;
	vec4 color;

	VertexFetch( position, LB, color, texCoord, lmCoord);

	// assign color
	ColorModulateColor( u_ColorModulateColorGen, u_Color, color );

	DeformVertex( position,
		      LB.normal,
		      texCoord.st,
		      color,
		      u_Time);

	// transform vertex position into homogenous clip-space
	gl_Position = u_ModelViewProjectionMatrix * position;

	// transform position into world space
	var_Position = (u_ModelMatrix * position).xyz;

	var_Tangent.xyz = mat3(u_ModelMatrix) * LB.tangent;
	var_Binormal.xyz = mat3(u_ModelMatrix) * LB.binormal;
	var_Normal.xyz = mat3(u_ModelMatrix) * LB.normal;

	// calc light xy,z attenuation in light space
	var_TexAttenuation = u_LightAttenuationMatrix * position;

	// transform diffusemap texcoords
	var_TexCoords = (u_TextureMatrix * vec3(texCoord, 1.0)).st;

	var_Color = color;
}
