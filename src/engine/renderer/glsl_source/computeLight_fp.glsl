/*
===========================================================================
Copyright (C) 2009-2011 Robert Beckebans <trebor_7@users.sourceforge.net>

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
// computeLight_fp.glsl - Light computing helper functions

#define COMPUTELIGHT_GLSL

#if !defined(USE_BSP_SURFACE)
	#define USE_MODEL_SURFACE
#endif

#if !defined(USE_GRID_LIGHTING)
	#define USE_LIGHT_MAPPING
#endif

#if defined(USE_REFLECTIVE_SPECULAR)
uniform samplerCube u_EnvironmentMap0;
uniform samplerCube u_EnvironmentMap1;
uniform float u_EnvironmentInterpolation;

// Only the RGB components are meaningful
// FIXME: using reflective specular will always globally decrease the scene brightness
// because we're multiplying with something that can only be less than 1.
vec4 EnvironmentalSpecularFactor( vec3 viewDir, vec3 normal )
{
	vec4 envColor0 = textureCube(u_EnvironmentMap0, reflect( -viewDir, normal ) );
	vec4 envColor1 = textureCube(u_EnvironmentMap1, reflect( -viewDir, normal ) );
	return mix( envColor0, envColor1, u_EnvironmentInterpolation );
}
#endif // USE_REFLECTIVE_SPECULAR

// lighting helper functions

#if defined(USE_LIGHT_MAPPING)
	void TransformLightMap( inout vec3 lightColor, in float lightFactor, bool linearizeLightMap )
	{
		convertFromSRGB(lightColor, linearizeLightMap);

		// When doing vertex lighting with full-range overbright, this reads out
		// 1<<overbrightBits and serves for the overbright shift for vertex colors.
		lightColor *= lightFactor;
	}
#endif

#if defined(USE_GRID_LIGHTING) || defined(USE_GRID_DELUXE_MAPPING)
	void ReadLightGrid( in vec4 texel, in float lightFactor, in bool linearizeLightMap, out vec3 ambientColor, out vec3 lightColor) {
		float ambientScale = 2.0 * texel.a;
		float directedScale = 2.0 - ambientScale;
		ambientColor = ambientScale * texel.rgb;
		lightColor = directedScale * texel.rgb;

		/* The light grid conversion from sRGB is done in C++ code
		when loading it, so it's done before interpolation. */

		ambientColor *= lightFactor;
		lightColor *= lightFactor;
	}
#endif

#if defined(USE_DELUXE_MAPPING) || defined(USE_GRID_DELUXE_MAPPING) || defined(r_realtimeLighting)
	#if !defined(USE_PHYSICAL_MAPPING) && defined(r_specularMapping)
		uniform vec2 u_SpecularExponent;

		vec3 computeSpecularity( vec3 lightColor, vec4 materialColor, float NdotH ) {
			return lightColor * materialColor.rgb * pow(NdotH, u_SpecularExponent.x * materialColor.a + u_SpecularExponent.y) * r_SpecularScale;
		}
	#endif
#endif

#if defined(USE_DELUXE_MAPPING) || defined(USE_GRID_DELUXE_MAPPING) || (defined(r_realtimeLighting) && r_realtimeLightingRenderer == 1)
void computeDeluxeLight( vec3 lightDir, vec3 normal, vec3 viewDir, vec3 lightColor,
	vec4 diffuseColor, vec4 materialColor,
	inout vec4 color )
{
	vec3 H = normalize( lightDir + viewDir );

	#if defined(USE_PHYSICAL_MAPPING) || defined(r_specularMapping)
		float NdotH = clamp( dot( normal, H ), 0.0, 1.0 );
	#endif // USE_PHYSICAL_MAPPING || r_specularMapping

	// clamp( NdotL, 0.0, 1.0 ) is done below
	float NdotL = dot( normal, lightDir );

	#if !defined(USE_BSP_SURFACE) && defined(r_halfLambertLighting)
		// http://developer.valvesoftware.com/wiki/Half_Lambert
		NdotL = NdotL * 0.5 + 0.5;
		NdotL *= NdotL;
	#endif

	NdotL = clamp( NdotL, 0.0, 1.0 );

	#if defined(USE_PHYSICAL_MAPPING)
		// Daemon PBR packing defaults to ORM like glTF 2.0 defines
		// https://www.khronos.org/blog/art-pipeline-for-gltf
		// > ORM texture for Occlusion, Roughness, and Metallic
		// https://github.com/KhronosGroup/glTF/blob/master/specification/2.0/schema/material.pbrMetallicRoughness.schema.json
		// > The metalness values are sampled from the B channel. The roughness values are sampled from the G channel.
		// > These values are linear. If other channels are present (R or A), they are ignored for metallic-roughness calculations.
		// https://docs.blender.org/manual/en/2.80/addons/io_scene_gltf2.html
		// > glTF stores occlusion in the red (R) channel, allowing it to optionally share the same image
		// > with the roughness and metallic channels.
		float roughness = materialColor.g;
		float metalness = materialColor.b;

		float NdotV = clamp( dot( normal, viewDir ), 0.0, 1.0);
		float VdotH = clamp( dot( viewDir, H ), 0.0, 1.0);

		float alpha = roughness * roughness;
		float k = 0.125 * ( roughness + 1.0 ) * ( roughness + 1.0 );

		float D = alpha / ( ( NdotH * NdotH ) * (alpha * alpha - 1.0 ) + 1.0 );
		D *= D;

		float FexpNH = pow( 1.0 - NdotH, 5.0 );
		float FexpNV = pow( 1.0 - NdotV, 5.0 );
		vec3 F = mix( vec3( 0.04 ), diffuseColor.rgb, metalness );
		F += ( 1.0 - F ) * FexpNH;

		float G = NdotL / (NdotL * ( 1.0 - k ) + k );
		G *= NdotV / ( NdotV * ( 1.0 - k ) + k );

		vec3 diffuseBRDF = NdotL * diffuseColor.rgb * ( 1.0 - metalness );
		vec3 specularBRDF = vec3( ( D * F * G ) / max( 4.0 * NdotL * NdotV, 0.0001f ) );
		color.rgb += ( diffuseBRDF + specularBRDF ) * lightColor.rgb * NdotL;
		color.a = mix( diffuseColor.a, 1.0, FexpNV );

	#else // !USE_PHYSICAL_MAPPING
		color.rgb += lightColor.rgb * NdotL * diffuseColor.rgb;
		#if defined(r_specularMapping)
			color.rgb += computeSpecularity(lightColor.rgb, materialColor, NdotH);
		#endif // r_specularMapping
	#endif // !USE_PHYSICAL_MAPPING
}
#endif // defined(USE_DELUXE_MAPPING) || defined(USE_GRID_DELUXE_MAPPING) || (defined(r_realtimeLighting) && r_realtimeLightingRenderer == 1)

#if !defined(USE_DELUXE_MAPPING) && !defined(USE_GRID_DELUXE_MAPPING)
	void computeLight( in vec3 lightColor, vec4 diffuseColor, inout vec4 color ) {
		color.rgb += lightColor.rgb * diffuseColor.rgb;
	}
#endif // !defined(USE_DELUXE_MAPPING) && !defined(USE_GRID_DELUXE_MAPPING)

#if defined(r_realtimeLighting) && r_realtimeLightingRenderer == 1

struct Light {
	vec3 center;
	float radius;
	vec3 color;
	float type;
	vec3 direction;
	float angle;
};

layout(std140) uniform u_Lights {
	Light lights[MAX_REF_LIGHTS];
};

#define GetLight( idx ) lights[idx]

uniform int u_numLights;

void computeDynamicLight( uint idx, vec3 P, vec3 normal, vec3 viewDir, vec4 diffuse,
	vec4 material, inout vec4 color )
{
	Light light = GetLight( idx );
	vec3 L;
	float attenuation;

	if( light.type == 0.0 ) {
		// point light
		L = light.center.xyz - P;
		// 2.57 ~= 8.0 ^ ( 1.0 / 2.2 ), adjusted after overbright changes
		float t = 1.0 + 2.57 * length( L ) / light.radius;
		// Quadratic attenuation function instead of linear because of overbright changes
		attenuation = 1.0 / ( t * t );
		L = normalize( L );
	} else if( light.type == 1.0 ) {
		// spot light
		L = light.center - P;
		// 2.57 ~= 8.0 ^ ( 1.0 / 2.2 ), adjusted after overbright changes
		float t = 1.0 + 2.57 * length( L ) / light.radius;
		// Quadratic attenuation function instead of linear because of overbright changes
		attenuation = 1.0 / ( t * t );
		L = normalize( L );

		if( dot( L, light.direction ) <= light.angle ) {
			attenuation = 0.0;
		}
	} else if( light.type == 2.0 ) {
		// sun (directional) light
		L = light.direction;
		attenuation = 1.0;
	}

	computeDeluxeLight(
		L, normal, viewDir, attenuation * attenuation * light.color,
		diffuse, material, color );
}

const int lightsPerLayer = 16;

#define idxs_t uvec4

uniform usampler3D u_LightTiles;

const vec3 tileScale = vec3( r_tileStep, 1.0 / float( NUM_LIGHT_LAYERS ) );

idxs_t fetchIdxs( in vec3 coords, in usampler3D u_LightTiles ) {
	return texture3D( u_LightTiles, coords );
}

// 8 bits per light ID
uint nextIdx( in uint count, in idxs_t idxs ) {
	return ( idxs[count / 4] >> ( 8 * ( count % 4 ) ) ) & 0xFFu;
}

void computeDynamicLights( vec3 P, vec3 normal, vec3 viewDir, vec4 diffuse, vec4 material,
	inout vec4 color, in usampler3D u_LightTiles )
{
	if( u_numLights == 0 ) {
		return;
	}

	vec2 tile = floor( gl_FragCoord.xy * ( 1.0 / float( TILE_SIZE ) ) ) + 0.5;

	for( uint layer = 0; layer < NUM_LIGHT_LAYERS; layer++ ) {
		uint lightCount = 0;
		idxs_t idxs = fetchIdxs( tileScale * vec3( tile, float( layer ) + 0.5 ), u_LightTiles );

		for( uint i = 0; i < lightsPerLayer; i++ ) {
			uint idx = nextIdx( lightCount, idxs );

			if( idx == 0 ) {
				break;
			}

			/* Light IDs are stored relative to the layer
			Subtract 1 because 0 means there's no light */
			idx = ( idx - 1 ) * NUM_LIGHT_LAYERS + layer;

			computeDynamicLight( idx, P, normal, viewDir, diffuse, material, color );
			lightCount++;
		}
	}

	#if defined(r_showLightTiles)
		if ( lightCount > 0 ) {
			color = vec4( float( lightCount ) / u_numLights, float( lightCount ) / u_numLights,
				float( lightCount ) / u_numLights, 1.0 );
		}
	#endif
}

#endif // defined(r_realtimeLighting) && r_realtimeLightingRenderer == 1
