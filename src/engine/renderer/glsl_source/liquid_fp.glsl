/*
===========================================================================
Copyright (C) 2007-2009 Robert Beckebans <trebor_7@users.sourceforge.net>

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

/* liquid_fp.glsl */

#insert common
#insert computeLight_fp
#insert reliefMapping_fp

#define LIQUID_GLSL

uniform sampler2D	u_CurrentMap;
uniform sampler2D	u_PortalMap;
uniform sampler2D	u_DepthMap;

uniform vec3		u_ViewOrigin;

uniform int u_ColorModulateColorGen;

uniform float		u_FogDensity;
uniform vec3		u_FogColor;
uniform float		u_RefractionIndex;
uniform float		u_FresnelPower;
uniform float		u_FresnelScale;
uniform float		u_FresnelBias;
uniform mat4		u_ModelMatrix;
uniform mat4		u_UnprojectMatrix;

uniform sampler3D       u_LightGrid1;
uniform sampler3D       u_LightGrid2;
uniform vec3            u_LightGridOrigin;
uniform vec3            u_LightGridScale;

IN(smooth) vec3		var_Position;
IN(smooth) vec2		var_TexCoords;
IN(smooth) vec3		var_Tangent;
IN(smooth) vec3		var_Binormal;
IN(smooth) vec3		var_Normal;

DECLARE_OUTPUT(vec4)

void	main()
{
	#insert material_fp

	// compute incident ray
	vec3 viewDir = normalize(u_ViewOrigin - var_Position);

	mat3 tangentToWorldMatrix = mat3(var_Tangent.xyz, var_Binormal.xyz, var_Normal.xyz);
	if(gl_FrontFacing)
	{
		tangentToWorldMatrix = -tangentToWorldMatrix;
	}

	// calculate the screen texcoord in the 0.0 to 1.0 range
	vec2 texScreen = gl_FragCoord.st / r_FBufSize;
	vec2 texNormal = var_TexCoords;

#if defined(USE_RELIEF_MAPPING)

	// compute texcoords offset from heightmap
	vec2 texOffset = ReliefTexOffset(texNormal, viewDir, tangentToWorldMatrix, u_HeightMap);

	texScreen += texOffset;
	texNormal += texOffset;
#endif

	// compute normal in world space from normalmap
	#if defined(r_normalMapping)
		vec3 normal = NormalInWorldSpace(texNormal, tangentToWorldMatrix, u_NormalMap);
	#else // !r_normalMapping
		vec3 normal = NormalInWorldSpace(texNormal, tangentToWorldMatrix);
	#endif // !r_normalMapping

	// compute fresnel term
	float fresnel = clamp(u_FresnelBias + pow(1.0 - dot(viewDir, normal), u_FresnelPower) *
			u_FresnelScale, 0.0, 1.0);

	vec3 refractColor;
	vec4 reflectColor;
	vec4 color;

#if defined(r_liquidMapping)
	refractColor = texture2D(u_CurrentMap, texScreen).rgb;
	reflectColor.rgb = texture2D(u_PortalMap, texScreen).rgb;
	reflectColor.a = 1.0;
#else // !r_liquidMapping
	// dummy fallback color
	refractColor = vec3(0.7, 0.7, 0.7);
	reflectColor = vec4(0.7, 0.7, 0.7, 1.0);
#endif // !r_liquidMapping

	color.rgb = mix(refractColor, reflectColor.rgb, fresnel);
	color.a = 1.0;

	if(u_FogDensity > 0.0)
	{
		// reconstruct vertex position in world space
		float depth = texture2D(u_DepthMap, texScreen).r;
		vec4 P = u_UnprojectMatrix * vec4(gl_FragCoord.xy, depth, 1.0);
		P.xyz /= P.w;

		// calculate fog distance
		float fogDistance = distance(P.xyz, var_Position);

		// calculate fog exponent
		float fogExponent = fogDistance * u_FogDensity;

		// calculate fog factor
		float fogFactor = exp2(-abs(fogExponent));

		color.rgb = mix(u_FogColor, color.rgb, fogFactor);
	}

	vec3 lightGridPos = (var_Position - u_LightGridOrigin) * u_LightGridScale;

	// compute light color from light grid
	vec3 ambientColor, lightColor;
	#if defined(USE_GRID_LIGHTING) || defined(USE_GRID_DELUXE_MAPPING)
		float lightFactor = ColorModulateToLightFactor( u_ColorModulateColorGen );
		ReadLightGrid(texture3D(u_LightGrid1, lightGridPos), lightFactor, ambientColor, lightColor);
	#else // !( defined(USE_GRID_LIGHTING) && defined(USE_GRID_DELUXE_MAPPING) )
		ambientColor = vec3( 0.0, 0.0, 0.0 );
		lightColor = vec3( 0.0, 0.0, 0.0 );
	#endif

	// compute light direction in world space
	vec4 texel = texture3D(u_LightGrid2, lightGridPos);
	vec3 lightDir = normalize(texel.xyz - (128.0 / 255.0));

	vec4 diffuse = vec4(0.0, 0.0, 0.0, 1.0);

	// compute the specular term
	#if defined(USE_DELUXE_MAPPING) || defined(USE_GRID_DELUXE_MAPPING)
		computeDeluxeLight(lightDir, normal, viewDir, lightColor, diffuse, reflectColor, color);
	#else // !USE_DELUXE_MAPPING && !USE_GRID_DELUXE_MAPPING
		computeLight(lightColor, diffuse, color);
	#endif // !USE_DELUXE_MAPPING && !USE_GRID_DELUXE_MAPPING

	outputColor = color;

#if defined(r_showNormalMaps)
	// convert normal to [0,1] color space
	normal = normal * 0.5 + 0.5;
	outputColor = vec4(normal, 1.0);
#endif
}
