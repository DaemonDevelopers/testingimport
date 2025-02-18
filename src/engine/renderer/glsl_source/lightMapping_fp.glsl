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

/* lightMapping_fp.glsl */

#insert common
#insert colorSpace
#insert computeLight_fp
#insert reliefMapping_fp

#define LIGHTMAPPING_GLSL

uniform sampler2D	u_DiffuseMap;
uniform sampler2D	u_MaterialMap;
uniform sampler2D	u_GlowMap;

uniform float		u_AlphaThreshold;
uniform vec3		u_ViewOrigin;

uniform uint u_ColorModulateColorGen;

IN(smooth) vec3		var_Position;
IN(smooth) vec2		var_TexCoords;
IN(smooth) vec4		var_Color;
IN(smooth) vec3		var_Tangent;
IN(smooth) vec3		var_Binormal;
IN(smooth) vec3		var_Normal;

uniform sampler2D u_LightMap;
uniform sampler3D u_LightGrid1;

uniform sampler2D u_DeluxeMap;
uniform sampler3D u_LightGrid2;

#if defined(USE_LIGHT_MAPPING) || defined(USE_DELUXE_MAPPING)
	IN(smooth) vec2 var_TexLight;
#endif

#if defined(USE_GRID_LIGHTING) || defined(USE_GRID_DELUXE_MAPPING)
	uniform vec3 u_LightGridOrigin;
	uniform vec3 u_LightGridScale;
#endif

#if defined(USE_MATERIAL_SYSTEM)
	uniform bool u_ShowTris;
	uniform vec3 u_MaterialColour;
#endif

#insert shaderProfiler_fp

DECLARE_OUTPUT(vec4)

void main()
{
	#insert material_fp

	#if defined(USE_MATERIAL_SYSTEM)
		if( u_ShowTris ) {
			outputColor = vec4( 0.0, 0.0, 1.0, 1.0 );
			return;
		}
	#endif

	// Compute view direction in world space.
	vec3 viewDir = normalize(u_ViewOrigin - var_Position);

	vec2 texCoords = var_TexCoords;

	mat3 tangentToWorldMatrix = mat3(var_Tangent.xyz, var_Binormal.xyz, var_Normal.xyz);

	#if defined(USE_RELIEF_MAPPING)
		// Compute texcoords offset from heightmap.
		#if defined(USE_HEIGHTMAP_IN_NORMALMAP)
			vec2 texOffset = ReliefTexOffset(texCoords, viewDir, tangentToWorldMatrix, u_NormalMap);
		#else
			vec2 texOffset = ReliefTexOffset(texCoords, viewDir, tangentToWorldMatrix, u_HeightMap);
		#endif

		texCoords += texOffset;
	#endif

	// Compute the diffuse term.
	vec4 diffuse = texture2D(u_DiffuseMap, texCoords);

	// Apply vertex blend operation like: alphaGen vertex.
	diffuse.a *= var_Color.a;

	if(abs(diffuse.a + u_AlphaThreshold) <= 1.0)
	{
		discard;
		return;
	}

	bool linearizeColorMap = ( u_LinearizeTexture & 0x1 ) != 0;
	bool linearizeLightMap = ( u_LinearizeTexture & 0x2 ) != 0;
	bool linearizeMaterialMap = ( u_LinearizeTexture & 0x4 ) != 0;

	convertFromSRGB(diffuse.rgb, linearizeColorMap);

	diffuse.rgb *= var_Color.rgb;

	// Compute normal in world space from normalmap.
	#if defined(r_normalMapping)
		vec3 normal = NormalInWorldSpace(texCoords, tangentToWorldMatrix, u_NormalMap);
	#else // !r_normalMapping
		vec3 normal = NormalInWorldSpace(texCoords, tangentToWorldMatrix);
	#endif // !r_normalMapping

	#if defined(r_specularMapping) || defined(r_physicalMapping)
		// Compute the material term.
		vec4 material = texture2D(u_MaterialMap, texCoords);

		convertFromSRGB(material.rgb, linearizeMaterialMap);
	#elif ( defined(r_realtimeLighting) && r_realtimeLightingRenderer == 1 )\
		|| defined( USE_DELUXE_MAPPING ) || defined(USE_GRID_DELUXE_MAPPING )
		// The computeDynamicLights function requires this variable to exist.
		vec4 material = vec4( 0.0, 0.0, 0.0, 1.0 );
	#endif

	// Compute final color.
	vec4 color;
	color.a = diffuse.a;

	#if defined(USE_GRID_LIGHTING) || defined(USE_GRID_DELUXE_MAPPING)
		// Compute light grid position.
		vec3 lightGridPos = (var_Position - u_LightGridOrigin) * u_LightGridScale;
	#endif

	#if defined(USE_DELUXE_MAPPING)
		// Compute light direction in world space from deluxe map.
		vec4 deluxe = texture2D(u_DeluxeMap, var_TexLight);
		vec3 lightDir = normalize(2.0 * deluxe.xyz - 1.0);
	#elif defined(USE_GRID_DELUXE_MAPPING)
		// Compute light direction in world space from light grid.
		vec4 texel = texture3D(u_LightGrid2, lightGridPos);
		vec3 lightDir = normalize(texel.xyz - (128.0 / 255.0));
	#endif
	
	float lightFactor = ColorModulateToLightFactor( u_ColorModulateColorGen );
	#if defined(USE_LIGHT_MAPPING)
		// Compute light color from world space lightmap.
		vec3 lightColor = texture2D(u_LightMap, var_TexLight).rgb;
		TransformLightMap(lightColor, lightFactor, linearizeLightMap);
		color.rgb = vec3(0.0);
	#else
		// Compute light color from lightgrid.
		vec3 ambientColor, lightColor;
		ReadLightGrid(texture3D(u_LightGrid1, lightGridPos), lightFactor, linearizeLightMap, ambientColor, lightColor);
		color.rgb = ambientColor * r_AmbientScale * diffuse.rgb;
	#endif

	#if defined(USE_LIGHT_MAPPING) && defined(USE_DELUXE_MAPPING)
		/* Lightmaps generated by q3map2 don't store the raw light value, but
		they store light premultiplied with the dot product of the light
		direction and surface normal. The line is just an attempt to reverse
		that and get the original light values.

		The lightmap stores the light in this way because for the diffuse
		lighting formula the outgoing light is equal to the incoming light
		multiplied by the above dot product multiplied by the surface albedo.
		So this premultiplication means that the diffuse lighting value can
		be calculated with a single multiply operation.

		But specular lighting and/or normal mapping formulas are more complex,
		and so you need the true light value to get correct lighting.
		Obviously the data is not good enough to recover the original color
		in all cases. The lower bound was an arbitrary chose factor to
		prevent too small divisors resulting in too bright lights.

		Increasing the value should reduce these artifacts. -- gimhael
		https://github.com/DaemonEngine/Daemon/issues/299#issuecomment-606186347
		*/

		// Divide by cosine term to restore original light color.
		lightColor /= clamp(dot(normalize(var_Normal), lightDir), 0.3, 1.0);
	#endif

	// Blend static light.
	#if defined(USE_DELUXE_MAPPING) || defined(USE_GRID_DELUXE_MAPPING)
		#if defined(USE_REFLECTIVE_SPECULAR)
			vec4 modifiedSpecular = material * EnvironmentalSpecularFactor(viewDir, normal);
			computeDeluxeLight(lightDir, normal, viewDir, lightColor, diffuse, modifiedSpecular, color);
		#else // !USE_REFLECTIVE_SPECULAR
			computeDeluxeLight(lightDir, normal, viewDir, lightColor, diffuse, material, color);
		#endif // !USE_REFLECTIVE_SPECULAR
	#else
		computeLight(lightColor, diffuse, color);
	#endif

	// Blend dynamic lights.
	#if defined(r_realtimeLighting) && r_realtimeLightingRenderer == 1
		computeDynamicLights(var_Position, normal, viewDir, diffuse, material, color, u_LightTiles);
	#endif

	// Add Rim Lighting to highlight the edges on model entities.
	#if defined(r_rimLighting) && defined(USE_MODEL_SURFACE) && defined(USE_GRID_LIGHTING)
		float mul1, mul2;
		if ( linearizeLightMap )
		{
			mul1 = 0.033; // convertFromSRGB( 0.2 )
			mul2 = 0.448; // convertFromSRGB( 0.7 )
		}
		else
		{
			mul1 = 0.2;
			mul2 = 0.7;
		}

		float rim = pow(1.0 - clamp(dot(normal, viewDir), 0.0, 1.0), r_RimExponent);
		vec3 emission = ambientColor * rim * rim * mul1;
		color.rgb += mul2 * emission;
	#endif

	#if defined(r_glowMapping)
		// Blend glow map.
		vec3 glow = texture2D(u_GlowMap, texCoords).rgb;

		convertFromSRGB(glow, linearizeColorMap);

		color.rgb += glow;
	#endif
	
	SHADER_PROFILER_SET( color )

	outputColor = color;


	// Debugging.
	#if defined(r_showNormalMaps)
		// Convert normal to [0,1] color space.
		normal = normal * 0.5 + 0.5;
		outputColor = vec4(normal, 1.0);
	#elif defined(r_showMaterialMaps)
		outputColor = material;
	#elif defined(r_showLightMaps) && defined(USE_LIGHT_MAPPING)
		outputColor = texture2D(u_LightMap, var_TexLight);
	#elif defined(r_showDeluxeMaps) && defined(USE_DELUXE_MAPPING)
		outputColor = texture2D(u_DeluxeMap, var_TexLight);
	#elif defined(USE_REFLECTIVE_SPECULAR) && defined(r_showReflectionMaps)
		vec4 envColor0 = textureCube(u_EnvironmentMap0, reflect(-viewDir, normal));
		vec4 envColor1 = textureCube(u_EnvironmentMap1, reflect(-viewDir, normal));

		outputColor = vec4( mix(envColor0, envColor1, u_EnvironmentInterpolation).rgb, 1.0 );
	#elif defined(r_showVertexColors)
		/* We need to keep the texture alpha channel so impact
		marks like creep don't fully overwrite the world texture. */
		#if defined(USE_BSP_SURFACE)
			outputColor.rgb = vec3(1.0, 1.0, 1.0);
			outputColor *= var_Color;
		#else
			outputColor.rgb = vec3(0.0, 0.0, 0.0);
		#endif
	#elif defined(USE_MATERIAL_SYSTEM) && defined(r_showGlobalMaterials)
		outputColor.rgb = u_MaterialColour + lightColor.rgb * u_MaterialColour;
	#endif
}
