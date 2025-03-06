/*
===========================================================================

Daemon BSD Source Code
Copyright (c) 2024 Daemon Developers
All rights reserved.

This file is part of the Daemon BSD Source Code (Daemon Source Code).

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
	* Redistributions of source code must retain the above copyright
	  notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above copyright
	  notice, this list of conditions and the following disclaimer in the
	  documentation and/or other materials provided with the distribution.
	* Neither the name of the Daemon developers nor the
	  names of its contributors may be used to endorse or promote products
	  derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL DAEMON DEVELOPERS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

===========================================================================
*/
// Material.cpp

#include "tr_local.h"
#include "Material.h"
#include "ShadeCommon.h"
#include "GeometryCache.h"

GLUBO materialsUBO( "materials", Util::ordinal( BufferBind::MATERIALS ), GL_MAP_WRITE_BIT, GL_MAP_INVALIDATE_RANGE_BIT );
GLBuffer texDataBuffer( "texData", Util::ordinal( BufferBind::TEX_DATA ), GL_MAP_WRITE_BIT, GL_MAP_FLUSH_EXPLICIT_BIT );
GLUBO lightMapDataUBO( "lightMapData", Util::ordinal( BufferBind::LIGHTMAP_DATA ), GL_MAP_WRITE_BIT, GL_MAP_FLUSH_EXPLICIT_BIT );

GLSSBO surfaceDescriptorsSSBO( "surfaceDescriptors", Util::ordinal( BufferBind::SURFACE_DESCRIPTORS ), GL_MAP_WRITE_BIT, GL_MAP_INVALIDATE_RANGE_BIT );
GLSSBO surfaceCommandsSSBO( "surfaceCommands", Util::ordinal( BufferBind::SURFACE_COMMANDS ), GL_MAP_WRITE_BIT, GL_MAP_FLUSH_EXPLICIT_BIT );
GLBuffer culledCommandsBuffer( "culledCommands", Util::ordinal( BufferBind::CULLED_COMMANDS ), GL_MAP_WRITE_BIT, GL_MAP_FLUSH_EXPLICIT_BIT );
GLUBO surfaceBatchesUBO( "surfaceBatches", Util::ordinal( BufferBind::SURFACE_BATCHES ), GL_MAP_WRITE_BIT, GL_MAP_INVALIDATE_RANGE_BIT );
GLBuffer atomicCommandCountersBuffer( "atomicCommandCounters", Util::ordinal( BufferBind::COMMAND_COUNTERS_ATOMIC ), GL_MAP_WRITE_BIT, GL_MAP_FLUSH_EXPLICIT_BIT );
GLSSBO portalSurfacesSSBO( "portalSurfaces", Util::ordinal( BufferBind::PORTAL_SURFACES ), GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT, 0 );

GLSSBO debugSSBO( "debug", Util::ordinal( BufferBind::DEBUG ), GL_MAP_WRITE_BIT, GL_MAP_INVALIDATE_RANGE_BIT );

PortalView portalStack[MAX_VIEWS];

MaterialSystem materialSystem;

static void ComputeDynamics( shaderStage_t* pStage ) {
	// TODO: Move color and texMatrices stuff to a compute shader
	pStage->colorDynamic = false;
	switch ( pStage->rgbGen ) {
		case colorGen_t::CGEN_IDENTITY:
		case colorGen_t::CGEN_ONE_MINUS_VERTEX:
		default:
		case colorGen_t::CGEN_IDENTITY_LIGHTING:
			/* Historically CGEN_IDENTITY_LIGHTING was done this way:

			  tess.svars.color = Color::White * tr.identityLight;

			But tr.identityLight is always 1.0f in Dæmon engine
			as the as the overbright bit implementation is fully
			software. */
		case colorGen_t::CGEN_VERTEX:
		case colorGen_t::CGEN_CONST:
		case colorGen_t::CGEN_ENTITY:
		case colorGen_t::CGEN_ONE_MINUS_ENTITY:
		{
			// TODO: Move this to some entity buffer once this is extended past BSP surfaces
			if ( backEnd.currentEntity ) {
				//
			} else {
				//
			}

			break;
		}

		case colorGen_t::CGEN_WAVEFORM:
		case colorGen_t::CGEN_CUSTOM_RGB:
		case colorGen_t::CGEN_CUSTOM_RGBs:
		{
			pStage->colorDynamic = true;
			break;
		}
	}

	switch ( pStage->alphaGen ) {
		default:
		case alphaGen_t::AGEN_IDENTITY:
		case alphaGen_t::AGEN_ONE_MINUS_VERTEX:
		case alphaGen_t::AGEN_VERTEX:
		case alphaGen_t::AGEN_CONST: {
		case alphaGen_t::AGEN_ENTITY:
		case alphaGen_t::AGEN_ONE_MINUS_ENTITY:
			// TODO: Move this to some entity buffer once this is extended past BSP surfaces
			/* if ( backEnd.currentEntity ) {
			} else {
			} */
			break;
		}

		case alphaGen_t::AGEN_WAVEFORM:
		case alphaGen_t::AGEN_CUSTOM:
		{
			pStage->colorDynamic = true;
			break;
		}
	}

	// Can we move this to a compute shader too?
	// Doesn't seem to be used much if at all, so probably not worth the effort to do that
	pStage->dynamic = pStage->dynamic || pStage->ifExp.numOps;
	pStage->dynamic = pStage->dynamic || pStage->alphaExp.numOps || pStage->alphaTestExp.numOps;
	pStage->dynamic = pStage->dynamic || pStage->rgbExp.numOps || pStage->redExp.numOps || pStage->greenExp.numOps || pStage->blueExp.numOps;
	pStage->dynamic = pStage->dynamic || pStage->deformMagnitudeExp.numOps;
	pStage->dynamic = pStage->dynamic || pStage->depthScaleExp.numOps
	                                  || pStage->fogDensityExp.numOps || pStage->fresnelBiasExp.numOps || pStage->fresnelPowerExp.numOps
	                                  || pStage->fresnelScaleExp.numOps || pStage->normalIntensityExp.numOps || pStage->refractionIndexExp.numOps;

	pStage->dynamic = pStage->dynamic || pStage->colorDynamic;
}

// UpdateSurface*() functions will actually write the uniform values to the SSBO
// Mirrors parts of the Render_*() functions in tr_shade.cpp

void UpdateSurfaceDataNONE( uint32_t*, shaderStage_t*, bool, bool, bool ) {
	ASSERT_UNREACHABLE();
}

void UpdateSurfaceDataNOP( uint32_t*, shaderStage_t*, bool, bool, bool ) {
}

void UpdateSurfaceDataGeneric3D( uint32_t* materials, shaderStage_t* pStage, bool mayUseVertexOverbright, bool, bool ) {
	// shader_t* shader = pStage->shader;

	materials += pStage->bufferOffset;

	// u_AlphaThreshold
	gl_genericShaderMaterial->SetUniform_AlphaTest( pStage->stateBits );

	// u_ColorModulate
	colorGen_t rgbGen = SetRgbGen( pStage );
	alphaGen_t alphaGen = SetAlphaGen( pStage );

	const bool styleLightMap = pStage->type == stageType_t::ST_STYLELIGHTMAP || pStage->type == stageType_t::ST_STYLECOLORMAP;
	gl_genericShaderMaterial->SetUniform_ColorModulateColorGen_Uint( rgbGen, alphaGen, mayUseVertexOverbright, styleLightMap );

	Tess_ComputeColor( pStage );
	gl_genericShaderMaterial->SetUniform_Color_Uint( tess.svars.color );

	bool hasDepthFade = pStage->hasDepthFade;
	if ( hasDepthFade ) {
		gl_genericShaderMaterial->SetUniform_DepthScale( pStage->depthFadeValue );
	}

	gl_genericShaderMaterial->WriteUniformsToBuffer( materials );
}

void UpdateSurfaceDataLightMapping( uint32_t* materials, shaderStage_t* pStage, bool, bool vertexLit, bool fullbright ) {
	shader_t* shader = pStage->shader;

	materials += pStage->bufferOffset;

	// u_ColorModulate
	colorGen_t rgbGen = SetRgbGen( pStage );
	alphaGen_t alphaGen = SetAlphaGen( pStage );

	Tess_ComputeColor( pStage );

	// HACK: This only has effect on vertex-lit surfaces
	if ( vertexLit ) {
		SetVertexLightingSettings( lightMode_t::VERTEX, rgbGen );
	}

	// u_ColorModulate
	gl_lightMappingShaderMaterial->SetUniform_ColorModulateColorGen_Uint( rgbGen, alphaGen, false, !fullbright );

	// u_Color
	gl_lightMappingShaderMaterial->SetUniform_Color_Uint( tess.svars.color );

	// u_AlphaThreshold
	gl_lightMappingShaderMaterial->SetUniform_AlphaTest( pStage->stateBits );

	// HeightMap
	if ( pStage->enableReliefMapping ) {
		float depthScale = RB_EvalExpression( &pStage->depthScaleExp, r_reliefDepthScale->value );
		depthScale *= shader->reliefDepthScale;

		gl_lightMappingShaderMaterial->SetUniform_ReliefDepthScale( depthScale );
		gl_lightMappingShaderMaterial->SetUniform_ReliefOffsetBias( shader->reliefOffsetBias );
	}

	// bind u_NormalScale
	if ( pStage->enableNormalMapping ) {
		vec3_t normalScale;
		SetNormalScale( pStage, normalScale );

		gl_lightMappingShaderMaterial->SetUniform_NormalScale( normalScale );
	}

	if ( pStage->enableSpecularMapping ) {
		float specExpMin = RB_EvalExpression( &pStage->specularExponentMin, r_specularExponentMin->value );
		float specExpMax = RB_EvalExpression( &pStage->specularExponentMax, r_specularExponentMax->value );

		gl_lightMappingShaderMaterial->SetUniform_SpecularExponent( specExpMin, specExpMax );
	}

	gl_lightMappingShaderMaterial->WriteUniformsToBuffer( materials );
}

void UpdateSurfaceDataReflection( uint32_t* materials, shaderStage_t* pStage, bool, bool, bool ) {
	shader_t* shader = pStage->shader;

	materials += pStage->bufferOffset;

	// bind u_ColorMap
	vec3_t position;
	if ( backEnd.currentEntity && ( backEnd.currentEntity != &tr.worldEntity ) ) {
		VectorCopy( backEnd.currentEntity->e.origin, position );
	} else {
		// FIXME position
		VectorCopy( backEnd.viewParms.orientation.origin, position );
	}

	cubemapProbe_t* probes[ 1 ];
	vec4_t trilerp;
	R_GetNearestCubeMaps( position, probes, trilerp, 1 );

	gl_reflectionShaderMaterial->SetUniform_ColorMapCubeBindless(
		GL_BindToTMU( 0, probes[0]->cubemap )
	);

	if ( pStage->enableNormalMapping ) {
		vec3_t normalScale;
		SetNormalScale( pStage, normalScale );

		gl_reflectionShaderMaterial->SetUniform_NormalScale( normalScale );
	}

	// u_depthScale u_reliefOffsetBias
	if ( pStage->enableReliefMapping ) {
		float depthScale = RB_EvalExpression( &pStage->depthScaleExp, r_reliefDepthScale->value );
		float reliefDepthScale = shader->reliefDepthScale;
		depthScale *= reliefDepthScale == 0 ? 1 : reliefDepthScale;
		gl_reflectionShaderMaterial->SetUniform_ReliefDepthScale( depthScale );
		gl_reflectionShaderMaterial->SetUniform_ReliefOffsetBias( shader->reliefOffsetBias );
	}

	gl_reflectionShaderMaterial->WriteUniformsToBuffer( materials );
}

void UpdateSurfaceDataSkybox( uint32_t* materials, shaderStage_t* pStage, bool, bool, bool ) {
	// shader_t* shader = pStage->shader;

	materials += pStage->bufferOffset;

	// u_AlphaThreshold
	gl_skyboxShaderMaterial->SetUniform_AlphaTest( GLS_ATEST_NONE );

	gl_skyboxShaderMaterial->WriteUniformsToBuffer( materials );
}

void UpdateSurfaceDataScreen( uint32_t* materials, shaderStage_t* pStage, bool, bool, bool ) {
	// shader_t* shader = pStage->shader;

	materials += pStage->bufferOffset;

	// bind u_CurrentMap
	/* FIXME: This is currently unused, but u_CurrentMap was made global for other shaders,
	this seems to be the only material system shader that might need it to not be global */
	gl_screenShaderMaterial->SetUniform_CurrentMapBindless( BindAnimatedImage( 0, &pStage->bundle[TB_COLORMAP] ) );

	gl_screenShaderMaterial->WriteUniformsToBuffer( materials );
}

void UpdateSurfaceDataHeatHaze( uint32_t* materials, shaderStage_t* pStage, bool, bool, bool ) {
	// shader_t* shader = pStage->shader;

	materials += pStage->bufferOffset;

	float deformMagnitude = RB_EvalExpression( &pStage->deformMagnitudeExp, 1.0 );
	gl_heatHazeShaderMaterial->SetUniform_DeformMagnitude( deformMagnitude );

	if ( pStage->enableNormalMapping ) {
		vec3_t normalScale;
		SetNormalScale( pStage, normalScale );

		// bind u_NormalScale
		gl_heatHazeShaderMaterial->SetUniform_NormalScale( normalScale );
	}

	gl_heatHazeShaderMaterial->WriteUniformsToBuffer( materials );
}

void UpdateSurfaceDataLiquid( uint32_t* materials, shaderStage_t* pStage, bool, bool, bool ) {
	// shader_t* shader = pStage->shader;

	materials += pStage->bufferOffset;

	float fogDensity = RB_EvalExpression( &pStage->fogDensityExp, 0.001 );
	vec4_t fogColor;
	Tess_ComputeColor( pStage );
	VectorCopy( tess.svars.color.ToArray(), fogColor );

	gl_liquidShaderMaterial->SetUniform_RefractionIndex( RB_EvalExpression( &pStage->refractionIndexExp, 1.0 ) );
	gl_liquidShaderMaterial->SetUniform_FresnelPower( RB_EvalExpression( &pStage->fresnelPowerExp, 2.0 ) );
	gl_liquidShaderMaterial->SetUniform_FresnelScale( RB_EvalExpression( &pStage->fresnelScaleExp, 1.0 ) );
	gl_liquidShaderMaterial->SetUniform_FresnelBias( RB_EvalExpression( &pStage->fresnelBiasExp, 0.05 ) );
	gl_liquidShaderMaterial->SetUniform_FogDensity( fogDensity );
	gl_liquidShaderMaterial->SetUniform_FogColor( fogColor );

	// NOTE: specular component is computed by shader.
	// FIXME: physical mapping is not implemented.
	if ( pStage->enableSpecularMapping ) {
		float specMin = RB_EvalExpression( &pStage->specularExponentMin, r_specularExponentMin->value );
		float specMax = RB_EvalExpression( &pStage->specularExponentMax, r_specularExponentMax->value );
		gl_liquidShaderMaterial->SetUniform_SpecularExponent( specMin, specMax );
	}

	// bind u_CurrentMap
	gl_liquidShaderMaterial->SetUniform_CurrentMapBindless( GL_BindToTMU( 0, tr.currentRenderImage[backEnd.currentMainFBO] ) );

	// bind u_HeightMap u_depthScale u_reliefOffsetBias
	if ( pStage->enableReliefMapping ) {
		float depthScale;
		float reliefDepthScale;

		depthScale = RB_EvalExpression( &pStage->depthScaleExp, r_reliefDepthScale->value );
		reliefDepthScale = tess.surfaceShader->reliefDepthScale;
		depthScale *= reliefDepthScale == 0 ? 1 : reliefDepthScale;
		gl_liquidShaderMaterial->SetUniform_ReliefDepthScale( depthScale );
		gl_liquidShaderMaterial->SetUniform_ReliefOffsetBias( tess.surfaceShader->reliefOffsetBias );
	}

	// bind u_NormalScale
	if ( pStage->enableNormalMapping ) {
		vec3_t normalScale;
		// FIXME: NormalIntensity default was 0.5
		SetNormalScale( pStage, normalScale );

		gl_liquidShaderMaterial->SetUniform_NormalScale( normalScale );
	}

	gl_liquidShaderMaterial->WriteUniformsToBuffer( materials );
}

void UpdateSurfaceDataFog( uint32_t* materials, shaderStage_t* pStage, bool, bool, bool ) {
	// shader_t* shader = pStage->shader;

	materials += pStage->bufferOffset;

	gl_fogQuake3ShaderMaterial->WriteUniformsToBuffer( materials );
}

/*
* Buffer layout:
* // Static surfaces data:
* // Stage0:
* uniform0_0
* uniform0_1
* ..
* uniform0_x
* optional_struct_padding
* // Stage1:
* ..
* // Stage_y:
* uniform0_0
* uniform0_1
* ..
* uniform0_x
* optional_struct_padding
* ..
* // Dynamic surfaces data:
* // Same as the static layout
*/
// Buffer is separated into static and dynamic parts so we can just update the whole dynamic range at once
// This will generate the actual buffer with per-stage values AFTER materials are generated
void MaterialSystem::GenerateWorldMaterialsBuffer() {
	Log::Debug( "Generating materials buffer" );

	// Sort by padded size to avoid extra padding
	std::sort( materialStages.begin(), materialStages.end(),
		[&]( const shaderStage_t* lhs, const shaderStage_t* rhs ) {
			if ( !lhs->dynamic && rhs->dynamic ) {
				return true;
			}

			if ( !rhs->dynamic && lhs->dynamic ) {
				return false;
			}

			return lhs->paddedSize < rhs->paddedSize;
	} );

	uint32_t offset = 0;
	dynamicStagesOffset = 0;
	bool dynamicStagesOffsetSet = false;

	// Compute data size for stages
	for ( shaderStage_t* pStage : materialStages ) {
		const uint32_t paddedSize = pStage->paddedSize;
		const uint32_t padding = !paddedSize || offset % paddedSize == 0 ? 0 : paddedSize - ( offset % paddedSize );

		offset += padding;

		// Make sure padding is taken into account for dynamicStagesOffset
		if ( pStage->dynamic ) {
			if ( !dynamicStagesOffsetSet ) {
				dynamicStagesOffset = offset;
				dynamicStagesOffsetSet = true;
			}
		}

		pStage->materialOffset = paddedSize ? offset / paddedSize : 0;
		pStage->bufferOffset = offset;
		offset += paddedSize * pStage->variantOffset;
	}

	dynamicStagesSize = dynamicStagesOffsetSet ? offset - dynamicStagesOffset : 0;
	totalStageSize = offset;

	// 4 bytes per component
	materialsUBO.BufferData( offset, nullptr, GL_DYNAMIC_DRAW );
	uint32_t* materialsData = materialsUBO.MapBufferRange( offset );

	GenerateMaterialsBuffer( materialStages, offset, materialsData );

	for ( uint32_t materialPackID = 0; materialPackID < 3; materialPackID++ ) {
		for ( Material& material : materialPacks[materialPackID].materials ) {
			for ( drawSurf_t* drawSurf : material.drawSurfs ) {
				uint32_t stage = 0;
				for ( shaderStage_t* pStage = drawSurf->shader->stages; pStage < drawSurf->shader->lastStage; pStage++ ) {
					if ( drawSurf->materialIDs[stage] != material.id || drawSurf->materialPackIDs[stage] != materialPackID ) {
						stage++;
						continue;
					}

					// We need some of the values from the remapped stage, but material/materialPack ID has to come from pStage
					shaderStage_t* remappedStage = pStage->materialRemappedStage ? pStage->materialRemappedStage : pStage;
					const uint32_t SSBOOffset =
						remappedStage->materialOffset + remappedStage->variantOffsets[drawSurf->shaderVariant[stage]];

					tess.currentDrawSurf = drawSurf;

					tess.currentSSBOOffset = SSBOOffset;
					tess.materialID = drawSurf->materialIDs[stage];
					tess.materialPackID = drawSurf->materialPackIDs[stage];

					Tess_Begin( Tess_StageIteratorDummy, nullptr, nullptr, false, -1, 0 );
					rb_surfaceTable[Util::ordinal( *drawSurf->surface )]( drawSurf->surface );
					Tess_DrawElements();
					Tess_Clear();

					drawSurf->drawCommandIDs[stage] = lastCommandID;

					stage++;
				}
			}
		}
	}

	for ( shaderStage_t* pStage : materialStages ) {
		if ( pStage->dynamic ) {
			pStage->bufferOffset -= dynamicStagesOffset;
		}
	}

	materialsUBO.UnmapBuffer();
}

void MaterialSystem::GenerateMaterialsBuffer( std::vector<shaderStage_t*>& stages, const uint32_t size, uint32_t* materialsData ) {
	// Shader uniforms are set to 0 if they're not specified, so make sure we do that here too
	memset( materialsData, 0, size * sizeof( uint32_t ) );
	for ( shaderStage_t* pStage : stages ) {
		/* Stage variants are essentially copies of the same stage with slightly different values that
		normally come from a drawSurf_t */
		uint32_t variants = 0;
		for ( int i = 0; i < ShaderStageVariant::ALL && variants < pStage->variantOffset; i++ ) {
			if ( pStage->variantOffsets[i] != -1 ) {
				const bool mayUseVertexOverbright = i & ShaderStageVariant::VERTEX_OVERBRIGHT;
				const bool vertexLit = i & ShaderStageVariant::VERTEX_LIT;
				const bool fullbright = i & ShaderStageVariant::FULLBRIGHT;

				const uint32_t variantOffset = pStage->variantOffsets[i] * pStage->paddedSize;
				pStage->bufferOffset += variantOffset;

				pStage->surfaceDataUpdater( materialsData, pStage, mayUseVertexOverbright, vertexLit, fullbright );

				pStage->bufferOffset -= variantOffset;
				variants++;
			}
		}
	}
}

void MaterialSystem::GenerateTexturesBuffer( std::vector<TextureData>& textures, TexBundle* textureBundles ) {
	for ( TextureData& textureData : textures ) {
		for ( int i = 0; i < MAX_TEXTURE_BUNDLES; i++ ) {
			if ( textureData.texBundlesOverride[i] ) {
				textureBundles->textures[i] = textureData.texBundlesOverride[i]->texture->bindlessTextureHandle;
				continue;
			}

			const textureBundle_t* bundle = textureData.texBundles[i];
			if ( bundle && bundle->image[0] ) {
				if ( generatingWorldCommandBuffer ) {
					textureBundles->textures[i] = bundle->image[0]->texture->bindlessTextureHandle;
				} else {
					textureBundles->textures[i] = BindAnimatedImage( 0, bundle );
				}
			}
		}

		const int bundle = textureData.textureMatrixBundle;
		RB_CalcTexMatrix( textureData.texBundles[bundle], tess.svars.texMatrices[bundle] );
		/* We only actually need these 6 components to get the correct texture transformation,
		the other ones are unused */
		textureBundles->textureMatrix[0] = tess.svars.texMatrices[bundle][0];
		textureBundles->textureMatrix[1] = tess.svars.texMatrices[bundle][1];
		textureBundles->textureMatrix[2] = tess.svars.texMatrices[bundle][4];
		textureBundles->textureMatrix[3] = tess.svars.texMatrices[bundle][5];
		textureBundles->textureMatrix[4] = tess.svars.texMatrices[bundle][12];
		textureBundles->textureMatrix[5] = tess.svars.texMatrices[bundle][13];
		textureBundles++;
	}
}

// This generates the buffers with indirect rendering commands etc.
void MaterialSystem::GenerateWorldCommandBuffer() {
	Log::Debug( "Generating world command buffer" );

	totalBatchCount = 0;

	uint32_t batchOffset = 0;
	uint32_t globalID = 0;
	for ( MaterialPack& pack : materialPacks ) {
		for ( Material& material : pack.materials ) {
			material.surfaceCommandBatchOffset = batchOffset;

			const uint32_t cmdCount = material.drawCommands.size();
			const uint32_t batchCount = cmdCount % SURFACE_COMMANDS_PER_BATCH == 0 ? cmdCount / SURFACE_COMMANDS_PER_BATCH
				: cmdCount / SURFACE_COMMANDS_PER_BATCH + 1;

			material.surfaceCommandBatchOffset = batchOffset;
			material.surfaceCommandBatchCount = batchCount;

			batchOffset += batchCount;
			material.globalID = globalID;

			totalBatchCount += batchCount;
			globalID++;
		}
	}

	Log::Debug( "Total batch count: %u", totalBatchCount );

	surfaceDescriptorsCount = totalDrawSurfs;
	descriptorSize = BOUNDING_SPHERE_SIZE + maxStages;
	surfaceDescriptorsSSBO.BufferData( surfaceDescriptorsCount * descriptorSize, nullptr, GL_STATIC_DRAW );
	uint32_t* surfaceDescriptors = surfaceDescriptorsSSBO.MapBufferRange( surfaceDescriptorsCount * descriptorSize );

	texDataBufferType = glConfig2.maxUniformBlockSize >= MIN_MATERIAL_UBO_SIZE ? GL_UNIFORM_BUFFER : GL_SHADER_STORAGE_BUFFER;

	texDataBuffer.BufferStorage( ( texData.size() + dynamicTexData.size() ) * TEX_BUNDLE_SIZE, 1, nullptr );
	texDataBuffer.MapAll();
	TexBundle* textureBundles = ( TexBundle* ) texDataBuffer.GetData();
	memset( textureBundles, 0, ( texData.size() + dynamicTexData.size() ) * TEX_BUNDLE_SIZE * sizeof( uint32_t ) );

	GenerateTexturesBuffer( texData, textureBundles );

	textureBundles += texData.size();

	GenerateTexturesBuffer( dynamicTexData, textureBundles );

	dynamicTexDataOffset = texData.size() * TEX_BUNDLE_SIZE;
	dynamicTexDataSize = dynamicTexData.size() * TEX_BUNDLE_SIZE;

	texDataBuffer.FlushAll();
	texDataBuffer.UnmapBuffer();

	lightMapDataUBO.BufferStorage( MAX_LIGHTMAPS * LIGHTMAP_SIZE, 1, nullptr );
	lightMapDataUBO.MapAll();
	uint64_t* lightMapData = ( uint64_t* ) lightMapDataUBO.GetData();
	memset( lightMapData, 0, MAX_LIGHTMAPS * LIGHTMAP_SIZE * sizeof( uint32_t ) );
	
	for ( uint32_t i = 0; i < tr.lightmaps.size(); i++ ) {
		if ( !tr.lightmaps[i]->texture->hasBindlessHandle ) {
			tr.lightmaps[i]->texture->GenBindlessHandle();
		}
		lightMapData[i * 2] = tr.lightmaps[i]->texture->bindlessTextureHandle;
	}
	for ( uint32_t i = 0; i < tr.deluxemaps.size(); i++ ) {
		if ( !tr.deluxemaps[i]->texture->hasBindlessHandle ) {
			tr.deluxemaps[i]->texture->GenBindlessHandle();
		}
		lightMapData[i * 2 + 1] = tr.deluxemaps[i]->texture->bindlessTextureHandle;
	}

	ASSERT_LE( tr.lightmaps.size(), 256 ); // Engine supports up to 256 lightmaps currently, so we use 8 bits to address them

	if ( tr.lightmaps.size() == 256 ) {
		/* It's very unlikely that this would actually happen, but put the warn here just in case
		If needed, another bit can be added to the lightmap address in rendering commands, but that would mean
		that its hex representation would no longer be easily "parsable" by just looking at it in a frame debugger */
		Log::Warn( "Material system only supports up to 255 lightmaps, got 256" );
	} else {
		if ( !tr.whiteImage->texture->hasBindlessHandle ) {
			tr.whiteImage->texture->GenBindlessHandle();
		}
		if ( !tr.blackImage->texture->hasBindlessHandle ) {
			tr.blackImage->texture->GenBindlessHandle();
		}
		// Use lightmap 255 for drawSurfs that use a full white image for their lightmap
		lightMapData[255 * 2] = tr.whiteImage->texture->bindlessTextureHandle;
		lightMapData[255 * 2 + 1] = tr.blackImage->texture->bindlessTextureHandle;
	}

	lightMapDataUBO.FlushAll();
	lightMapDataUBO.UnmapBuffer();

	surfaceCommandsCount = totalBatchCount * SURFACE_COMMANDS_PER_BATCH;

	surfaceCommandsSSBO.BufferStorage( surfaceCommandsCount * SURFACE_COMMAND_SIZE * MAX_VIEWFRAMES, 1, nullptr );
	surfaceCommandsSSBO.MapAll();
	SurfaceCommand* surfaceCommands = ( SurfaceCommand* ) surfaceCommandsSSBO.GetData();
	memset( surfaceCommands, 0, surfaceCommandsCount * sizeof( SurfaceCommand ) * MAX_VIEWFRAMES );

	culledCommandsBuffer.BufferStorage( surfaceCommandsCount * INDIRECT_COMMAND_SIZE * MAX_VIEWFRAMES, 1, nullptr );
	culledCommandsBuffer.MapAll();
	GLIndirectCommand* culledCommands = ( GLIndirectCommand* ) culledCommandsBuffer.GetData();
	memset( culledCommands, 0, surfaceCommandsCount * sizeof( GLIndirectCommand ) * MAX_VIEWFRAMES );
	culledCommandsBuffer.FlushAll();

	surfaceBatchesUBO.BufferData( MAX_SURFACE_COMMAND_BATCHES * SURFACE_COMMAND_BATCH_SIZE, nullptr, GL_STATIC_DRAW );
	SurfaceCommandBatch* surfaceCommandBatches =
		( SurfaceCommandBatch* ) surfaceBatchesUBO.MapBufferRange( MAX_SURFACE_COMMAND_BATCHES * SURFACE_COMMAND_BATCH_SIZE );

	// memset( (void*) surfaceCommandBatches, 0, MAX_SURFACE_COMMAND_BATCHES * SURFACE_COMMAND_BATCH_SIZE );
	// Fuck off gcc
	for ( int i = 0; i < MAX_SURFACE_COMMAND_BATCHES; i++ ) {
		surfaceCommandBatches[i] = {};
	}

	uint32_t id = 0;
	uint32_t matID = 0;
	for ( MaterialPack& pack : materialPacks ) {
		for ( Material& mat : pack.materials ) {
			for ( uint32_t i = 0; i < mat.surfaceCommandBatchCount; i++ ) {
				surfaceCommandBatches[id].materialIDs[0] = matID;
				surfaceCommandBatches[id].materialIDs[1] = mat.surfaceCommandBatchOffset;
				id++;
			}
			matID++;
		}
	}

	atomicCommandCountersBuffer.BufferStorage( MAX_COMMAND_COUNTERS * MAX_VIEWS, MAX_FRAMES, nullptr );
	atomicCommandCountersBuffer.MapAll();
	uint32_t* atomicCommandCounters = ( uint32_t* ) atomicCommandCountersBuffer.GetData();
	memset( atomicCommandCounters, 0, MAX_COMMAND_COUNTERS * MAX_VIEWFRAMES * sizeof( uint32_t ) );

	/* For use in debugging compute shaders
	Intended for use with Nsight Graphics to format the output */
	if ( r_materialDebug.Get() ) {
		const uint32_t debugSize = surfaceCommandsCount * 20;

		debugSSBO.BufferData( debugSize, nullptr, GL_STATIC_DRAW );
		uint32_t* debugBuffer = debugSSBO.MapBufferRange( debugSize );
		memset( debugBuffer, 0, debugSize * sizeof( uint32_t ) );
		debugSSBO.UnmapBuffer();
	}

	for ( int i = 0; i < tr.refdef.numDrawSurfs; i++ ) {
		const drawSurf_t* drawSurf = &tr.refdef.drawSurfs[i];

		if ( drawSurf->entity != &tr.worldEntity ) {
			continue;
		}

		shader_t* shader = drawSurf->shader;
		if ( !shader ) {
			continue;
		}

		shader = shader->remappedShader ? shader->remappedShader : shader;
		if ( shader->isSky || shader->isPortal || shader->autoSpriteMode ) {
			continue;
		}

		// Don't add SF_SKIP surfaces
		if ( *drawSurf->surface == surfaceType_t::SF_SKIP ) {
			continue;
		}

		// Depth prepass surfaces are added as stages to the main surface instead
		if ( drawSurf->materialSystemSkip ) {
			continue;
		}

		SurfaceDescriptor surface;
		VectorCopy( ( ( srfGeneric_t* ) drawSurf->surface )->origin, surface.boundingSphere.origin );
		surface.boundingSphere.radius = ( ( srfGeneric_t* ) drawSurf->surface )->radius;

		const bool depthPrePass = drawSurf->depthSurface != nullptr;

		if ( depthPrePass ) {
			const drawSurf_t* depthDrawSurf = drawSurf->depthSurface;
			const Material* material = &materialPacks[depthDrawSurf->materialPackIDs[0]]
				.materials[depthDrawSurf->materialIDs[0]];
			uint cmdID = material->surfaceCommandBatchOffset * SURFACE_COMMANDS_PER_BATCH + depthDrawSurf->drawCommandIDs[0];
			// Add 1 because cmd 0 == no-command
			surface.surfaceCommandIDs[0] = cmdID + 1;

			SurfaceCommand surfaceCommand;
			surfaceCommand.enabled = 0;
			surfaceCommand.drawCommand = material->drawCommands[depthDrawSurf->drawCommandIDs[0]].cmd;
			// We still need the textures for alpha-tested depth pre-pass surface commands
			surfaceCommand.drawCommand.baseInstance |= depthDrawSurf->texDataDynamic[0]
				? ( depthDrawSurf->texDataIDs[0] + texData.size() ) << TEX_BUNDLE_BITS
				: depthDrawSurf->texDataIDs[0] << TEX_BUNDLE_BITS;
			surfaceCommands[cmdID] = surfaceCommand;
		}

		uint32_t stage = 0;
		for ( shaderStage_t* pStage = drawSurf->shader->stages; pStage < drawSurf->shader->lastStage; pStage++ ) {
			const Material* material = &materialPacks[drawSurf->materialPackIDs[stage]].materials[drawSurf->materialIDs[stage]];
			uint32_t cmdID = material->surfaceCommandBatchOffset * SURFACE_COMMANDS_PER_BATCH + drawSurf->drawCommandIDs[stage];
			// Add 1 because cmd 0 == no-command
			surface.surfaceCommandIDs[stage + ( depthPrePass ? 1 : 0 )] = cmdID + 1;

			SurfaceCommand surfaceCommand;
			surfaceCommand.enabled = 0;
			surfaceCommand.drawCommand = material->drawCommands[drawSurf->drawCommandIDs[stage]].cmd;
			surfaceCommand.drawCommand.baseInstance |= drawSurf->texDataDynamic[stage]
				? ( drawSurf->texDataIDs[stage] + texData.size() ) << TEX_BUNDLE_BITS
				: drawSurf->texDataIDs[stage] << TEX_BUNDLE_BITS;
			surfaceCommand.drawCommand.baseInstance |= ( HasLightMap( drawSurf ) ? GetLightMapNum( drawSurf ) : 255 ) << LIGHTMAP_BITS;
			surfaceCommands[cmdID] = surfaceCommand;

			stage++;
		}

		if ( drawSurf->fogSurface ) {
			const drawSurf_t* fogDrawSurf = drawSurf->fogSurface;
			const Material* material = &materialPacks[fogDrawSurf->materialPackIDs[0]]
				.materials[fogDrawSurf->materialIDs[0]];
			uint cmdID = material->surfaceCommandBatchOffset * SURFACE_COMMANDS_PER_BATCH + fogDrawSurf->drawCommandIDs[0];
			// Add 1 because cmd 0 == no-command
			surface.surfaceCommandIDs[stage + ( depthPrePass ? 1 : 0 )] = cmdID + 1;

			SurfaceCommand surfaceCommand;
			surfaceCommand.enabled = 0;
			surfaceCommand.drawCommand = material->drawCommands[fogDrawSurf->drawCommandIDs[0]].cmd;
			surfaceCommands[cmdID] = surfaceCommand;
		}

		memcpy( surfaceDescriptors, &surface, descriptorSize * sizeof( uint32_t ) );
		surfaceDescriptors += descriptorSize;
	}

	for ( int i = 0; i < MAX_VIEWFRAMES; i++ ) {
		memcpy( surfaceCommands + surfaceCommandsCount * i, surfaceCommands, surfaceCommandsCount * sizeof( SurfaceCommand ) );
	}

	surfaceDescriptorsSSBO.UnmapBuffer();

	surfaceCommandsSSBO.UnmapBuffer();

	culledCommandsBuffer.UnmapBuffer();

	atomicCommandCountersBuffer.UnmapBuffer();

	surfaceBatchesUBO.UnmapBuffer();

	GL_CheckErrors();
}

void MaterialSystem::GenerateDepthImages( const int width, const int height, imageParams_t imageParms ) {
	imageParms.bits ^= ( IF_NOPICMIP | IF_PACKED_DEPTH24_STENCIL8 );
	imageParms.bits |= IF_ONECOMP32F;

	depthImageLevels = log2f( std::max( width, height ) ) + 1;

	depthImage = R_CreateImage( "_depthImage", nullptr, width, height, depthImageLevels, imageParms );
	GL_Bind( depthImage );

	int mipmapWidth = width;
	int mipmapHeight = height;
	for ( int i = 0; i < depthImageLevels; i++ ) {
		glTexImage2D( GL_TEXTURE_2D, i, GL_R32F, mipmapWidth, mipmapHeight, 0, GL_RED, GL_FLOAT, nullptr );
		mipmapWidth = mipmapWidth > 1 ? mipmapWidth >> 1 : 1;
		mipmapHeight = mipmapHeight > 1 ? mipmapHeight >> 1 : 1;
	}
}

void BindShaderNONE( Material* ) {
	ASSERT_UNREACHABLE();
}

void BindShaderNOP( Material* ) {
}

void BindShaderGeneric3D( Material* material ) {
	// Select shader permutation.
	gl_genericShaderMaterial->SetTCGenEnvironment( material->tcGenEnvironment );
	gl_genericShaderMaterial->SetTCGenLightmap( material->tcGen_Lightmap );
	gl_genericShaderMaterial->SetDepthFade( material->hasDepthFade );

	// Bind shader program.
	gl_genericShaderMaterial->BindProgram( material->deformIndex );

	// Set shader uniforms.
	if ( material->tcGenEnvironment ) {
		gl_genericShaderMaterial->SetUniform_ViewOrigin( backEnd.orientation.viewOrigin );
		gl_genericShaderMaterial->SetUniform_ViewUp( backEnd.orientation.axis[2] );
	}

	gl_genericShaderMaterial->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
	gl_genericShaderMaterial->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[glState.stackIndex] );

	gl_genericShaderMaterial->SetUniform_DepthMapBindless( GL_BindToTMU( 1, tr.currentDepthImage ) );

	// u_DeformGen
	gl_genericShaderMaterial->SetUniform_Time( backEnd.refdef.floatTime - backEnd.currentEntity->e.shaderTime );

	if ( r_profilerRenderSubGroups.Get() ) {
		gl_genericShaderMaterial->SetUniform_ProfilerZero();
		gl_genericShaderMaterial->SetUniform_ProfilerRenderSubGroups( GetShaderProfilerRenderSubGroupsMode( material->stateBits ) );
	}
}

void BindShaderLightMapping( Material* material ) {
	// Select shader permutation.
	gl_lightMappingShaderMaterial->SetBspSurface( material->bspSurface );
	gl_lightMappingShaderMaterial->SetDeluxeMapping( material->enableDeluxeMapping );
	gl_lightMappingShaderMaterial->SetGridLighting( material->enableGridLighting );
	gl_lightMappingShaderMaterial->SetGridDeluxeMapping( material->enableGridDeluxeMapping );
	gl_lightMappingShaderMaterial->SetHeightMapInNormalMap( material->hasHeightMapInNormalMap );
	gl_lightMappingShaderMaterial->SetReliefMapping( material->enableReliefMapping );
	/* Reflective specular setting is different here than in ProcessMaterialLightMapping(),
	because we don't have cubemaps built yet at this point, but for the purposes of the material ordering there's no difference */
	gl_lightMappingShaderMaterial->SetReflectiveSpecular( glConfig2.reflectionMapping && material->enableSpecularMapping && !( tr.refdef.rdflags & RDF_NOCUBEMAP ) );
	gl_lightMappingShaderMaterial->SetPhysicalShading( material->enablePhysicalMapping );

	// Bind shader program.
	gl_lightMappingShaderMaterial->BindProgram( material->deformIndex );

	// Set shader uniforms.
	if ( tr.world ) {
		gl_lightMappingShaderMaterial->SetUniform_LightGridOrigin( tr.world->lightGridGLOrigin );
		gl_lightMappingShaderMaterial->SetUniform_LightGridScale( tr.world->lightGridGLScale );
	}
	// FIXME: else

	// bind u_LightGrid1
	if ( material->enableGridLighting ) {
		gl_lightMappingShaderMaterial->SetUniform_LightGrid1Bindless( GL_BindToTMU( BIND_LIGHTMAP, tr.lightGrid1Image ) );
	}

	// bind u_LightGrid2
	if ( material->enableGridDeluxeMapping ) {
		gl_lightMappingShaderMaterial->SetUniform_LightGrid2Bindless( GL_BindToTMU( BIND_DELUXEMAP, tr.lightGrid2Image ) );
	}

	if ( glConfig2.realtimeLighting ) {
		gl_lightMappingShaderMaterial->SetUniformBlock_Lights( tr.dlightUBO );

		// bind u_LightTiles
		if ( r_realtimeLightingRenderer.Get() == Util::ordinal( realtimeLightingRenderer_t::TILED ) ) {
			gl_lightMappingShaderMaterial->SetUniform_LightTilesBindless(
				GL_BindToTMU( BIND_LIGHTTILES, tr.lighttileRenderImage )
			);
		}
	}

	gl_lightMappingShaderMaterial->SetUniform_ViewOrigin( backEnd.orientation.viewOrigin );
	gl_lightMappingShaderMaterial->SetUniform_numLights( backEnd.refdef.numLights );
	gl_lightMappingShaderMaterial->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
	gl_lightMappingShaderMaterial->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[glState.stackIndex] );

	// u_DeformGen
	gl_lightMappingShaderMaterial->SetUniform_Time( backEnd.refdef.floatTime - backEnd.currentEntity->e.shaderTime );

	// TODO: Move this to a per-entity buffer
	if ( glConfig2.reflectionMapping && !( tr.refdef.rdflags & RDF_NOCUBEMAP ) ) {
		bool isWorldEntity = backEnd.currentEntity == &tr.worldEntity;

		vec3_t position;
		if ( backEnd.currentEntity && !isWorldEntity ) {
			VectorCopy( backEnd.currentEntity->e.origin, position );
			return;
		} else {
			// FIXME position
			VectorCopy( backEnd.orientation.viewOrigin, position );
		}

		cubemapProbe_t* probes[2];
		vec4_t trilerp;
		// TODO: Add a code path that would assign a cubemap to each tile for the tiled renderer
		R_GetNearestCubeMaps( position, probes, trilerp, 2 );
		const cubemapProbe_t* cubeProbeNearest = probes[0];
		const cubemapProbe_t* cubeProbeSecondNearest = probes[1];

		const float interpolation = 1.0 - trilerp[0];

		if ( r_logFile->integer ) {
			GLimp_LogComment( va( "Probe 0 distance = %f, probe 1 distance = %f, interpolation = %f\n",
				Distance( position, probes[0]->origin ), Distance( position, probes[1]->origin ), interpolation ) );
		}

		// bind u_EnvironmentMap0
		gl_lightMappingShaderMaterial->SetUniform_EnvironmentMap0Bindless(
			GL_BindToTMU( BIND_ENVIRONMENTMAP0, cubeProbeNearest->cubemap )
		);

		// bind u_EnvironmentMap1
		gl_lightMappingShaderMaterial->SetUniform_EnvironmentMap1Bindless(
			GL_BindToTMU( BIND_ENVIRONMENTMAP1, cubeProbeSecondNearest->cubemap )
		);

		// bind u_EnvironmentInterpolation
		gl_lightMappingShaderMaterial->SetUniform_EnvironmentInterpolation( interpolation );
	}

	if ( r_profilerRenderSubGroups.Get() ) {
		gl_lightMappingShaderMaterial->SetUniform_ProfilerZero();
		gl_lightMappingShaderMaterial->SetUniform_ProfilerRenderSubGroups( GetShaderProfilerRenderSubGroupsMode( material->stateBits ) );
	}
}

void BindShaderReflection( Material* material ) {
	// Select shader permutation.
	gl_reflectionShaderMaterial->SetHeightMapInNormalMap( material->hasHeightMapInNormalMap );
	gl_reflectionShaderMaterial->SetReliefMapping( material->enableReliefMapping );

	// Bind shader program.
	gl_reflectionShaderMaterial->BindProgram( material->deformIndex );

	// Set shader uniforms.
	gl_reflectionShaderMaterial->SetUniform_ViewOrigin( backEnd.viewParms.orientation.origin );
	gl_reflectionShaderMaterial->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
	gl_reflectionShaderMaterial->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[glState.stackIndex] );
}

void BindShaderSkybox( Material* material ) {
	// Bind shader program.
	gl_skyboxShaderMaterial->BindProgram( material->deformIndex );

	// Set shader uniforms.
	gl_skyboxShaderMaterial->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[glState.stackIndex] );
}

void BindShaderScreen( Material* material ) {
	// Bind shader program.
	gl_screenShaderMaterial->BindProgram( material->deformIndex );

	// Set shader uniforms.
	gl_screenShaderMaterial->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[glState.stackIndex] );
}

void BindShaderHeatHaze( Material* material ) {
	// Bind shader program.
	gl_heatHazeShaderMaterial->BindProgram( material->deformIndex );

	// Set shader uniforms.
	gl_heatHazeShaderMaterial->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[glState.stackIndex] );

	gl_heatHazeShaderMaterial->SetUniform_ModelViewMatrixTranspose( glState.modelViewMatrix[glState.stackIndex] );
	gl_heatHazeShaderMaterial->SetUniform_ProjectionMatrixTranspose( glState.projectionMatrix[glState.stackIndex] );
	gl_heatHazeShaderMaterial->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[glState.stackIndex] );

	// bind u_CurrentMap
	gl_heatHazeShaderMaterial->SetUniform_CurrentMapBindless(
		GL_BindToTMU( 1, tr.currentRenderImage[backEnd.currentMainFBO] )
	);

	gl_heatHazeShaderMaterial->SetUniform_DeformEnable( true );

	// draw to background image
	R_BindFBO( tr.mainFBO[1 - backEnd.currentMainFBO] );
}

void BindShaderLiquid( Material* material ) {
	// Select shader permutation.
	gl_liquidShaderMaterial->SetHeightMapInNormalMap( material->hasHeightMapInNormalMap );
	gl_liquidShaderMaterial->SetReliefMapping( material->enableReliefMapping );
	gl_liquidShaderMaterial->SetGridDeluxeMapping( material->enableGridDeluxeMapping );
	gl_liquidShaderMaterial->SetGridLighting( material->enableGridLighting );

	// Bind shader program.
	gl_liquidShaderMaterial->BindProgram( material->deformIndex );

	// Set shader uniforms.
	gl_liquidShaderMaterial->SetUniform_ViewOrigin( backEnd.viewParms.orientation.origin );
	gl_liquidShaderMaterial->SetUniform_UnprojectMatrix( backEnd.viewParms.unprojectionMatrix );
	gl_liquidShaderMaterial->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
	gl_liquidShaderMaterial->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[glState.stackIndex] );

	// depth texture
	gl_liquidShaderMaterial->SetUniform_DepthMapBindless( GL_BindToTMU( 2, tr.currentDepthImage ) );

	// bind u_PortalMap
	gl_liquidShaderMaterial->SetUniform_PortalMapBindless( GL_BindToTMU( 1, tr.portalRenderImage ) );
}

void BindShaderFog( Material* material ) {
	// Bind shader program.
	gl_fogQuake3ShaderMaterial->BindProgram( 0 );

	// Set shader uniforms.
	const fog_t* fog = material->fog;

	// all fogging distance is based on world Z units
	vec4_t fogDistanceVector;
	vec3_t local;
	VectorSubtract( backEnd.orientation.origin, backEnd.viewParms.orientation.origin, local );
	fogDistanceVector[0] = -backEnd.orientation.modelViewMatrix[2];
	fogDistanceVector[1] = -backEnd.orientation.modelViewMatrix[6];
	fogDistanceVector[2] = -backEnd.orientation.modelViewMatrix[10];
	fogDistanceVector[3] = DotProduct( local, backEnd.viewParms.orientation.axis[0] );

	// scale the fog vectors based on the fog's thickness
	VectorScale( fogDistanceVector, fog->tcScale, fogDistanceVector );
	fogDistanceVector[3] *= fog->tcScale;

	// rotate the gradient vector for this orientation
	float eyeT;
	vec4_t fogDepthVector;
	if ( fog->hasSurface ) {
		fogDepthVector[0] = fog->surface[0] * backEnd.orientation.axis[0][0] +
			fog->surface[1] * backEnd.orientation.axis[0][1] + fog->surface[2] * backEnd.orientation.axis[0][2];
		fogDepthVector[1] = fog->surface[0] * backEnd.orientation.axis[1][0] +
			fog->surface[1] * backEnd.orientation.axis[1][1] + fog->surface[2] * backEnd.orientation.axis[1][2];
		fogDepthVector[2] = fog->surface[0] * backEnd.orientation.axis[2][0] +
			fog->surface[1] * backEnd.orientation.axis[2][1] + fog->surface[2] * backEnd.orientation.axis[2][2];
		fogDepthVector[3] = -fog->surface[3] + DotProduct( backEnd.orientation.origin, fog->surface );

		eyeT = DotProduct( backEnd.orientation.viewOrigin, fogDepthVector ) + fogDepthVector[3];
	} else {
		Vector4Set( fogDepthVector, 0, 0, 0, 1 );
		eyeT = 1; // non-surface fog always has eye inside
	}

	// see if the viewpoint is outside
	// this is needed for clipping distance even for constant fog
	fogDistanceVector[3] += 1.0 / 512;

	gl_fogQuake3ShaderMaterial->SetUniform_FogDistanceVector( fogDistanceVector );
	gl_fogQuake3ShaderMaterial->SetUniform_FogDepthVector( fogDepthVector );
	gl_fogQuake3ShaderMaterial->SetUniform_FogEyeT( eyeT );

	gl_fogQuake3ShaderMaterial->SetUniform_ColorGlobal_Uint( fog->color );

	gl_fogQuake3ShaderMaterial->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
	gl_fogQuake3ShaderMaterial->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[glState.stackIndex] );

	gl_fogQuake3ShaderMaterial->SetUniform_Time( backEnd.refdef.floatTime - backEnd.currentEntity->e.shaderTime );

	// bind u_ColorMap
	gl_fogQuake3ShaderMaterial->SetUniform_FogMapBindless(
		GL_BindToTMU( 0, tr.fogImage )
	);
}

void ProcessMaterialNONE( Material*, shaderStage_t*, drawSurf_t* ) {
	ASSERT_UNREACHABLE();
}

void ProcessMaterialNOP( Material*, shaderStage_t*, drawSurf_t* ) {
}

// ProcessMaterial*() are essentially same as BindShader*(), but only set the GL program id to the material,
// without actually binding it
void ProcessMaterialGeneric3D( Material* material, shaderStage_t* pStage, drawSurf_t* ) {
	material->shader = gl_genericShaderMaterial;

	material->tcGenEnvironment = pStage->tcGen_Environment;
	material->tcGen_Lightmap = pStage->tcGen_Lightmap;
	material->deformIndex = pStage->deformIndex;

	gl_genericShaderMaterial->SetTCGenEnvironment( pStage->tcGen_Environment );
	gl_genericShaderMaterial->SetTCGenLightmap( pStage->tcGen_Lightmap );

	bool hasDepthFade = pStage->hasDepthFade;
	material->hasDepthFade = hasDepthFade;
	gl_genericShaderMaterial->SetDepthFade( hasDepthFade );

	material->program = gl_genericShaderMaterial->GetProgram( pStage->deformIndex );
}

void ProcessMaterialLightMapping( Material* material, shaderStage_t* pStage, drawSurf_t* drawSurf ) {
	material->shader = gl_lightMappingShaderMaterial;

	gl_lightMappingShaderMaterial->SetBspSurface( drawSurf->bspSurface );

	lightMode_t lightMode;
	deluxeMode_t deluxeMode;
	SetLightDeluxeMode( drawSurf, pStage->type, lightMode, deluxeMode );

	bool enableDeluxeMapping = ( deluxeMode == deluxeMode_t::MAP );
	bool enableGridLighting = ( lightMode == lightMode_t::GRID );
	bool enableGridDeluxeMapping = ( deluxeMode == deluxeMode_t::GRID );

	DAEMON_ASSERT( !( enableDeluxeMapping && enableGridDeluxeMapping ) );

	material->enableDeluxeMapping = enableDeluxeMapping;
	material->enableGridLighting = enableGridLighting;
	material->enableGridDeluxeMapping = enableGridDeluxeMapping;
	material->hasHeightMapInNormalMap = pStage->hasHeightMapInNormalMap;
	material->enableReliefMapping = pStage->enableReliefMapping;
	material->enableNormalMapping = pStage->enableNormalMapping;
	material->enableSpecularMapping = pStage->enableSpecularMapping;
	material->enablePhysicalMapping = pStage->enablePhysicalMapping;
	material->deformIndex = pStage->deformIndex;

	gl_lightMappingShaderMaterial->SetDeluxeMapping( enableDeluxeMapping );

	gl_lightMappingShaderMaterial->SetGridLighting( enableGridLighting );

	gl_lightMappingShaderMaterial->SetGridDeluxeMapping( enableGridDeluxeMapping );

	gl_lightMappingShaderMaterial->SetHeightMapInNormalMap( pStage->hasHeightMapInNormalMap );

	gl_lightMappingShaderMaterial->SetReliefMapping( pStage->enableReliefMapping );

	gl_lightMappingShaderMaterial->SetReflectiveSpecular( pStage->enableSpecularMapping );

	gl_lightMappingShaderMaterial->SetPhysicalShading( pStage->enablePhysicalMapping );

	material->program = gl_lightMappingShaderMaterial->GetProgram( pStage->deformIndex );
}

void ProcessMaterialReflection( Material* material, shaderStage_t* pStage, drawSurf_t* /* drawSurf */ ) {
	material->shader = gl_reflectionShaderMaterial;

	material->hasHeightMapInNormalMap = pStage->hasHeightMapInNormalMap;
	material->enableReliefMapping = pStage->enableReliefMapping;
	material->deformIndex = pStage->deformIndex;

	gl_reflectionShaderMaterial->SetHeightMapInNormalMap( pStage->hasHeightMapInNormalMap );

	gl_reflectionShaderMaterial->SetReliefMapping( pStage->enableReliefMapping );

	material->program = gl_reflectionShaderMaterial->GetProgram( pStage->deformIndex );
}

void ProcessMaterialSkybox( Material* material, shaderStage_t* pStage, drawSurf_t* /* drawSurf */ ) {
	material->shader = gl_skyboxShaderMaterial;

	material->deformIndex = pStage->deformIndex;

	material->program = gl_skyboxShaderMaterial->GetProgram( pStage->deformIndex );
}

void ProcessMaterialScreen( Material* material, shaderStage_t* pStage, drawSurf_t* /* drawSurf */ ) {
	material->shader = gl_screenShaderMaterial;

	material->deformIndex = pStage->deformIndex;

	material->program = gl_screenShaderMaterial->GetProgram( pStage->deformIndex );
}

void ProcessMaterialHeatHaze( Material* material, shaderStage_t* pStage, drawSurf_t* ) {
	material->shader = gl_heatHazeShaderMaterial;

	material->deformIndex = pStage->deformIndex;

	material->program = gl_heatHazeShaderMaterial->GetProgram( pStage->deformIndex );
}

void ProcessMaterialLiquid( Material* material, shaderStage_t* pStage, drawSurf_t* drawSurf ) {
	material->shader = gl_liquidShaderMaterial;

	lightMode_t lightMode;
	deluxeMode_t deluxeMode;
	SetLightDeluxeMode( drawSurf, pStage->type, lightMode, deluxeMode );

	material->hasHeightMapInNormalMap = pStage->hasHeightMapInNormalMap;
	material->enableReliefMapping = pStage->enableReliefMapping;
	material->deformIndex = pStage->deformIndex;
	material->enableGridDeluxeMapping = true;
	material->enableGridLighting = true;

	gl_liquidShaderMaterial->SetHeightMapInNormalMap( pStage->hasHeightMapInNormalMap );

	gl_liquidShaderMaterial->SetReliefMapping( pStage->enableReliefMapping );

	gl_liquidShaderMaterial->SetGridDeluxeMapping( deluxeMode == deluxeMode_t::GRID );

	gl_liquidShaderMaterial->SetGridLighting( lightMode == lightMode_t::GRID );

	material->program = gl_liquidShaderMaterial->GetProgram( pStage->deformIndex );
}

void ProcessMaterialFog( Material* material, shaderStage_t* pStage, drawSurf_t* drawSurf ) {
	material->shader = gl_fogQuake3ShaderMaterial;
	material->fog = tr.world->fogs + drawSurf->fog;

	material->program = gl_fogQuake3ShaderMaterial->GetProgram( pStage->deformIndex );
}

void MaterialSystem::AddStage( drawSurf_t* drawSurf, shaderStage_t* pStage, uint32_t stage,
	const bool mayUseVertexOverbright, const bool vertexLit, const bool fullbright ) {
	const int variant = ( mayUseVertexOverbright ? ShaderStageVariant::VERTEX_OVERBRIGHT : 0 )
		| ( vertexLit ? ShaderStageVariant::VERTEX_LIT : 0 )
		| ( fullbright ? ShaderStageVariant::FULLBRIGHT : 0 );

	if ( pStage->variantOffsets[variant] == -1 ) {
		pStage->variantOffsets[variant] = pStage->variantOffset;
		pStage->variantOffset++;
	}

	drawSurf->shaderVariant[stage] = variant;

	// Look for a stage that will have the same data layout and data + data changes themselves
	for ( shaderStage_t* pStage2 : materialStages ) {
		if ( pStage == pStage2 ) {
			return;
		}

		if ( pStage->shaderBinder != pStage2->shaderBinder ) {
			continue;
		}

		if ( pStage->dynamic != pStage2->dynamic ) {
			continue;
		}

		if ( pStage->ifExp != pStage2->ifExp ) {
			continue;
		}

		if ( pStage->rgbGen != pStage2->rgbGen ) {
			continue;
		}

		if ( pStage->rgbGen == colorGen_t::CGEN_WAVEFORM && pStage->rgbWave != pStage2->rgbWave ) {
			continue;
		}

		if ( pStage->rgbGen == colorGen_t::CGEN_CUSTOM_RGB && pStage->rgbExp != pStage2->rgbExp ) {
			continue;
		}

		if ( pStage->rgbGen == colorGen_t::CGEN_CUSTOM_RGBs &&
			!( pStage->redExp == pStage2->redExp && pStage->greenExp == pStage2->greenExp && pStage->blueExp == pStage2->blueExp ) ) {
			continue;
		}

		if ( ( pStage->type == stageType_t::ST_STYLELIGHTMAP || pStage->type == stageType_t::ST_STYLECOLORMAP
			|| pStage2->type == stageType_t::ST_STYLELIGHTMAP || pStage2->type == stageType_t::ST_STYLECOLORMAP ) && pStage->type != pStage2->type ) {
			continue;
		}

		if ( pStage->alphaGen != pStage2->alphaGen ) {
			continue;
		}

		if ( pStage->alphaGen == alphaGen_t::AGEN_WAVEFORM && pStage->alphaWave != pStage2->alphaWave ) {
			continue;
		}

		if ( pStage->alphaGen == alphaGen_t::AGEN_CUSTOM && pStage->alphaExp != pStage2->alphaExp ) {
			continue;
		}

		if ( pStage->constantColor.Red() != pStage2->constantColor.Red() || pStage->constantColor.Green() != pStage2->constantColor.Green()
			|| pStage->constantColor.Blue() != pStage2->constantColor.Blue() || pStage->constantColor.Alpha() != pStage2->constantColor.Alpha() ) {
			continue;
		}

		if ( pStage->depthFadeValue != pStage2->depthFadeValue ) {
			continue;
		}

		// Only GLS_ATEST_BITS affect stage data, the other bits go into the material
		if ( ( pStage->stateBits & GLS_ATEST_BITS ) != ( pStage2->stateBits & GLS_ATEST_BITS ) ) {
			continue;
		}

		if ( pStage->refractionIndexExp != pStage2->refractionIndexExp || pStage->specularExponentMin != pStage2->specularExponentMin
			|| pStage->specularExponentMax != pStage2->specularExponentMax || pStage->fresnelPowerExp != pStage2->fresnelPowerExp
			|| pStage->fresnelScaleExp != pStage2->fresnelScaleExp || pStage->fresnelBiasExp != pStage2->fresnelBiasExp
			|| !VectorCompare( pStage->normalScale, pStage2->normalScale ) || pStage->normalIntensityExp != pStage2->normalIntensityExp
			|| pStage->fogDensityExp != pStage2->fogDensityExp || pStage->depthScaleExp != pStage2->depthScaleExp ) {
			continue;
		}

		pStage->materialRemappedStage = pStage2;

		if ( pStage2->variantOffsets[variant] == -1 ) {
			pStage2->variantOffsets[variant] = pStage2->variantOffset;
			pStage2->variantOffset++;
		}

		return;
	}

	// Add at the back if we haven't found any matching ones
	materialStages.emplace_back( pStage );

	if ( pStage->dynamic ) {
		dynamicStages.emplace_back( pStage );
	}
}

void MaterialSystem::ProcessStage( drawSurf_t* drawSurf, shaderStage_t* pStage, shader_t* shader, uint32_t* packIDs, uint32_t& stage,
	uint32_t& previousMaterialID ) {
	lightMode_t lightMode;
	deluxeMode_t deluxeMode;
	SetLightDeluxeMode( drawSurf, pStage->type, lightMode, deluxeMode );
	const bool mayUseVertexOverbright = pStage->type == stageType_t::ST_COLORMAP && drawSurf->bspSurface && pStage->shaderBinder == BindShaderGeneric3D;
	const bool vertexLit = lightMode == lightMode_t::VERTEX && pStage->shaderBinder == BindShaderLightMapping;
	const bool fullbright = lightMode == lightMode_t::FULLBRIGHT && pStage->shaderBinder == BindShaderLightMapping;

	ComputeDynamics( pStage );

	Material material;

	uint32_t materialPack = 0;
	if ( shader->sort == Util::ordinal( shaderSort_t::SS_DEPTH ) ) {
		materialPack = 0;
	} else if ( shader->sort >= Util::ordinal( shaderSort_t::SS_ENVIRONMENT_FOG )
		&& shader->sort <= Util::ordinal( shaderSort_t::SS_OPAQUE ) ) {
		materialPack = 1;
	} else {
		materialPack = 2;
	}
	material.sort = materialPack;
	uint32_t id = packIDs[materialPack];

	// In surfaces with multiple stages each consecutive stage must be drawn after the previous stage,
	// except if an opaque stage follows a transparent stage etc.
	if ( stage > 0 ) {
		material.useSync = true;
		material.syncMaterial = previousMaterialID;
	}

	material.stateBits = pStage->stateBits;
	// GLS_ATEST_BITS don't matter here as they don't change GL state
	material.stateBits &= GLS_DEPTHFUNC_BITS | GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_POLYMODE_LINE | GLS_DEPTHTEST_DISABLE
		| GLS_COLORMASK_BITS | GLS_DEPTHMASK_TRUE;
	material.shaderBinder = pStage->shaderBinder;
	material.cullType = shader->cullType;
	material.usePolygonOffset = shader->polygonOffset;

	material.bspSurface = drawSurf->bspSurface;
	pStage->materialProcessor( &material, pStage, drawSurf );
	pStage->paddedSize = material.shader->GetPaddedSize();

	// HACK: Copy the shaderStage_t and drawSurf_t that we need into the material, so we can use it with glsl_restart
	material.refStage = pStage;
	material.refDrawSurf = *drawSurf;
	material.refDrawSurf.entity = nullptr;
	material.refDrawSurf.depthSurface = nullptr;
	material.refDrawSurf.fogSurface = nullptr;

	std::vector<Material>& materials = materialPacks[materialPack].materials;
	std::vector<Material>::iterator currentSearchIt = materials.begin();
	std::vector<Material>::iterator materialIt;
	// Look for this material in the ones we already have
	while ( true ) {
		materialIt = std::find( currentSearchIt, materials.end(), material );
		if ( materialIt == materials.end() ) {
			break;
		}
		if ( material.useSync && materialIt->id < material.syncMaterial ) {
			currentSearchIt = materialIt + 1;
		} else {
			break;
		}
	}

	// Add it at the back if not found
	if ( materialIt == materials.end() ) {
		material.id = id;
		previousMaterialID = id;
		materials.emplace_back( material );
		id++;
	} else {
		previousMaterialID = materialIt->id;
	}

	pStage->useMaterialSystem = true;
	pStage->initialized = true;

	AddStage( drawSurf, pStage, stage, mayUseVertexOverbright, vertexLit, fullbright );
	AddStageTextures( drawSurf, stage, &materials[previousMaterialID] );

	if ( std::find( materials[previousMaterialID].drawSurfs.begin(), materials[previousMaterialID].drawSurfs.end(), drawSurf )
		== materials[previousMaterialID].drawSurfs.end() ) {
		materials[previousMaterialID].drawSurfs.emplace_back( drawSurf );
	}

	drawSurf->materialIDs[stage] = previousMaterialID;
	drawSurf->materialPackIDs[stage] = materialPack;

	packIDs[materialPack] = id;

	stage++;
}

/* This will only generate the materials themselves
*  A material represents a distinct global OpenGL state (e. g. blend function, depth test, depth write etc.)
*  Materials can have a dependency on other materials to make sure that consecutive stages are rendered in the proper order */
void MaterialSystem::GenerateWorldMaterials() {
	R_SyncRenderThread();

	const int current_r_nocull = r_nocull->integer;
	const int current_r_drawworld = r_drawworld->integer;
	r_nocull->integer = 1;
	r_drawworld->integer = 1;
	generatingWorldCommandBuffer = true;

	Log::Debug( "Generating world materials" );

	++tr.viewCountNoReset;
	R_AddWorldSurfaces();

	Log::Notice( "World bounds: min: %f %f %f max: %f %f %f", tr.viewParms.visBounds[0][0], tr.viewParms.visBounds[0][1],
		tr.viewParms.visBounds[0][2], tr.viewParms.visBounds[1][0], tr.viewParms.visBounds[1][1], tr.viewParms.visBounds[1][2] );
	VectorCopy( tr.viewParms.visBounds[0], worldViewBounds[0] );
	VectorCopy( tr.viewParms.visBounds[1], worldViewBounds[1] );

	backEnd.currentEntity = &tr.worldEntity;

	totalDrawSurfs = 0;

	uint32_t packIDs[3] = { 0, 0, 0 };

	for ( int i = 0; i < tr.refdef.numDrawSurfs; i++ ) {
		drawSurf_t* drawSurf = &tr.refdef.drawSurfs[i];
		if ( drawSurf->entity != &tr.worldEntity ) {
			continue;
		}

		shader_t* shader = drawSurf->shader;
		if ( !shader ) {
			continue;
		}

		shader = shader->remappedShader ? shader->remappedShader : shader;
		if ( shader->isSky || shader->isPortal || shader->autoSpriteMode ) {
			continue;
		}

		// Don't add SF_SKIP surfaces
		if ( *drawSurf->surface == surfaceType_t::SF_SKIP ) {
			continue;
		}

		// Only add the main surface for surfaces with depth pre-pass or fog to the total count
		if ( !drawSurf->materialSystemSkip ) {
			totalDrawSurfs++;
		}

		uint32_t stage = 0;
		uint32_t previousMaterialID = 0;
		for ( shaderStage_t* pStage = drawSurf->shader->stages; pStage < drawSurf->shader->lastStage; pStage++ ) {
			ProcessStage( drawSurf, pStage, shader, packIDs, stage, previousMaterialID );
		}
	}

	GenerateWorldMaterialsBuffer();

	uint32_t totalCount = 0;
	for ( MaterialPack& pack : materialPacks ) {
		totalCount += pack.materials.size();
	}
	Log::Notice( "Generated %u materials from %u surfaces", totalCount, tr.refdef.numDrawSurfs );
	Log::Notice( "Materials UBO: total: %.2f kb, dynamic: %.2f kb, texData: %.2f kb",
		totalStageSize * 4 / 1024.0f, dynamicStagesSize * 4 / 1024.0f,
		( texData.size() + dynamicTexData.size() ) * TEX_BUNDLE_SIZE * 4 / 1024.0f );

	/* for ( const MaterialPack& materialPack : materialPacks ) {
		Log::Notice( "materialPack sort: %i %i", Util::ordinal( materialPack.fromSort ), Util::ordinal( materialPack.toSort ) );
		for ( const Material& material : materialPack.materials ) {
			Log::Notice( "id: %u, useSync: %b, sync: %u, program: %i, stateBits: %u, total drawSurfs: %u, shader: %s, vbo: %s, ibo: %s"
				", culling: %i",
				material.id, material.useSync, material.syncMaterial, material.program, material.stateBits, material.drawSurfs.size(),
				material.shader->GetName(), material.vbo->name, material.ibo->name, material.cullType );
		}
	} */

	r_nocull->integer = current_r_nocull;
	r_drawworld->integer = current_r_drawworld;
	AddAllWorldSurfaces();

	GeneratePortalBoundingSpheres();

	generatedWorldCommandBuffer = true;
}

void MaterialSystem::AddAllWorldSurfaces() {
	GenerateWorldCommandBuffer();

	generatingWorldCommandBuffer = false;
}

void MaterialSystem::GLSLRestart() {
	for ( MaterialPack& materialPack : materialPacks ) {
		for ( Material& material : materialPack.materials ) {
			/* We only really need to reset material.shader here,
			but we keep a copy of the original shaderStage_t and drawSurf_t used for it so we don't change the actual material data */
			material.refStage->materialProcessor( &material, material.refStage, &material.refDrawSurf );
		}
	}
}

void MaterialSystem::AddStageTextures( drawSurf_t* drawSurf, const uint32_t stage, Material* material ) {
	TextureData textureData;
	const shaderStage_t* pStage = &drawSurf->shader->stages[stage];

	int bundleNum = 0;
	bool dynamic = false;
	for ( int i = 0; i < MAX_TEXTURE_BUNDLES; i++ ) {
		const textureBundle_t& bundle = pStage->bundle[i];

		if ( bundle.isVideoMap ) {
			material->AddTexture( tr.cinematicImage[bundle.videoMapHandle]->texture );
			continue;
		}

		for ( image_t* image : bundle.image ) {
			if ( image ) {
				material->AddTexture( image->texture );
			}
		}

		if ( bundle.numImages > 1 || bundle.numTexMods > 0 ) {
			textureData.textureMatrixBundle = i;
			dynamic = true;
		}

		textureData.texBundles[bundleNum] = &bundle;
		bundleNum++;
	}

	// Add lightmap and deluxemap for this surface to the material as well
	lightMode_t lightMode;
	deluxeMode_t deluxeMode;
	SetLightDeluxeMode( drawSurf, pStage->type, lightMode, deluxeMode );

	// u_Map, u_DeluxeMap
	image_t* lightmap = SetLightMap( drawSurf, lightMode );
	image_t* deluxemap = SetDeluxeMap( drawSurf, deluxeMode );

	material->AddTexture( lightmap->texture );
	material->AddTexture( deluxemap->texture );

	if ( pStage->type == stageType_t::ST_STYLELIGHTMAP ) {
		textureData.texBundlesOverride[TB_COLORMAP] = lightmap;
	}

	std::vector<TextureData>& textures = dynamic ? dynamicTexData : texData;

	std::vector<TextureData>::iterator it = std::find( textures.begin(), textures.end(), textureData );
	if ( it == textures.end() ) {
		drawSurf->texDataIDs[stage] = textures.size();
		textures.emplace_back( textureData );
	} else {
		drawSurf->texDataIDs[stage] = it - textures.begin();
	}
	drawSurf->texDataDynamic[stage] = dynamic;

	if ( glConfig2.realtimeLighting ) {
		if ( r_realtimeLightingRenderer.Get() == Util::ordinal( realtimeLightingRenderer_t::TILED ) ) {
			material->AddTexture( tr.lighttileRenderImage->texture );
		}
	}
}

// Dynamic surfaces are those whose values in the SSBO can be updated
void MaterialSystem::UpdateDynamicSurfaces() {
	if ( dynamicStagesSize > 0 ) {
		uint32_t* materialsData = materialsUBO.MapBufferRange( dynamicStagesOffset, dynamicStagesSize );

		GenerateMaterialsBuffer( dynamicStages, dynamicStagesSize, materialsData );

		materialsUBO.UnmapBuffer();
	}

	if ( dynamicTexDataSize > 0 ) {
		TexBundle* textureBundles =
			( TexBundle* ) texDataBuffer.MapBufferRange( dynamicTexDataOffset, dynamicTexDataSize );

		GenerateTexturesBuffer( dynamicTexData, textureBundles );

		texDataBuffer.UnmapBuffer();
	}

	GL_CheckErrors();
}

void MaterialSystem::UpdateFrameData() {
	atomicCommandCountersBuffer.BindBufferBase( GL_SHADER_STORAGE_BUFFER, Util::ordinal( BufferBind::COMMAND_COUNTERS_STORAGE ) );

	gl_clearSurfacesShader->BindProgram( 0 );
	gl_clearSurfacesShader->SetUniform_Frame( nextFrame );
	gl_clearSurfacesShader->DispatchCompute( MAX_VIEWS, 1, 1 );

	atomicCommandCountersBuffer.UnBindBufferBase( GL_SHADER_STORAGE_BUFFER, Util::ordinal( BufferBind::COMMAND_COUNTERS_STORAGE ) );

	GL_CheckErrors();
}

void MaterialSystem::QueueSurfaceCull( const uint32_t viewID, const vec3_t origin, const frustum_t* frustum ) {
	VectorCopy( origin, frames[nextFrame].viewFrames[viewID].origin );
	memcpy( frames[nextFrame].viewFrames[viewID].frustum, frustum, sizeof( frustum_t ) );
	frames[nextFrame].viewCount++;
}

void MaterialSystem::DepthReduction() {
	if ( r_lockpvs->integer ) {
		if ( !PVSLocked ) {
			lockedDepthImage = depthImage;
		}

		return;
	}

	int width = depthImage->width;
	int height = depthImage->height;

	gl_depthReductionShader->BindProgram( 0 );

	uint32_t globalWorkgroupX = ( width + 7 ) / 8;
	uint32_t globalWorkgroupY = ( height + 7 ) / 8;

	GL_Bind( tr.currentDepthImage );
	glBindImageTexture( 2, depthImage->texnum, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F );

	gl_depthReductionShader->SetUniform_InitialDepthLevel( true );
	gl_depthReductionShader->SetUniform_ViewWidth( width );
	gl_depthReductionShader->SetUniform_ViewHeight( height );
	gl_depthReductionShader->DispatchCompute( globalWorkgroupX, globalWorkgroupY, 1 );

	for ( int i = 0; i < depthImageLevels; i++ ) {
		width = width > 1 ? width >> 1 : 1;
		height = height > 1 ? height >> 1 : 1;

		globalWorkgroupX = ( width + 7 ) / 8;
		globalWorkgroupY = ( height + 7 ) / 8;

		glBindImageTexture( 1, depthImage->texnum, i, GL_FALSE, 0, GL_READ_ONLY, GL_R32F );
		glBindImageTexture( 2, depthImage->texnum, i + 1, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F );

		gl_depthReductionShader->SetUniform_InitialDepthLevel( false );
		gl_depthReductionShader->SetUniform_ViewWidth( width );
		gl_depthReductionShader->SetUniform_ViewHeight( height );
		gl_depthReductionShader->DispatchCompute( globalWorkgroupX, globalWorkgroupY, 1 );

		glMemoryBarrier( GL_SHADER_IMAGE_ACCESS_BARRIER_BIT );
	}
}

void MaterialSystem::CullSurfaces() {
	if ( r_gpuOcclusionCulling.Get() ) {
		DepthReduction();
	}

	surfaceDescriptorsSSBO.BindBufferBase();
	surfaceCommandsSSBO.BindBufferBase();
	culledCommandsBuffer.BindBufferBase( GL_SHADER_STORAGE_BUFFER );
	surfaceBatchesUBO.BindBufferBase();
	atomicCommandCountersBuffer.BindBufferBase( GL_ATOMIC_COUNTER_BUFFER );

	if ( totalPortals > 0 ) {
		portalSurfacesSSBO.BindBufferBase();
	}

	if ( r_materialDebug.Get() ) {
		debugSSBO.BindBufferBase();
	}

	GL_CheckErrors();

	for ( uint32_t view = 0; view < frames[nextFrame].viewCount; view++ ) {
		vec3_t origin;
		frustum_t* frustum = &frames[nextFrame].viewFrames[view].frustum;

		vec4_t frustumPlanes[6];
		for ( int i = 0; i < 6; i++ ) {
			VectorCopy( PVSLocked ? lockedFrustums[view][i].normal : frustum[0][i].normal, frustumPlanes[i] );
			frustumPlanes[i][3] = PVSLocked ? lockedFrustums[view][i].dist : frustum[0][i].dist;
		}
		matrix_t viewMatrix;
		if ( PVSLocked ) {
			VectorCopy( lockedOrigins[view], origin );
			MatrixCopy( lockedViewMatrix, viewMatrix );
		} else {
			VectorCopy( frames[nextFrame].viewFrames[view].origin, origin );
			MatrixCopy( backEnd.viewParms.world.modelViewMatrix, viewMatrix );
		}

		gl_cullShader->BindProgram( 0 );
		uint32_t globalWorkGroupX = totalDrawSurfs % MAX_COMMAND_COUNTERS == 0 ?
			totalDrawSurfs / MAX_COMMAND_COUNTERS : totalDrawSurfs / MAX_COMMAND_COUNTERS + 1;
		GL_Bind( depthImage );
		gl_cullShader->SetUniform_Frame( nextFrame );
		gl_cullShader->SetUniform_ViewID( view );
		gl_cullShader->SetUniform_TotalDrawSurfs( totalDrawSurfs );
		gl_cullShader->SetUniform_UseFrustumCulling( r_gpuFrustumCulling.Get() );
		gl_cullShader->SetUniform_UseOcclusionCulling( r_gpuOcclusionCulling.Get() );
		gl_cullShader->SetUniform_CameraPosition( origin );
		gl_cullShader->SetUniform_ModelViewMatrix( viewMatrix );
		gl_cullShader->SetUniform_FirstPortalGroup( globalWorkGroupX );
		gl_cullShader->SetUniform_TotalPortals( totalPortals );
		gl_cullShader->SetUniform_ViewWidth( depthImage->width );
		gl_cullShader->SetUniform_ViewHeight( depthImage->height );
		gl_cullShader->SetUniform_SurfaceCommandsOffset( surfaceCommandsCount * ( MAX_VIEWS * nextFrame + view ) );
		gl_cullShader->SetUniform_P00( glState.projectionMatrix[glState.stackIndex][0] );
		gl_cullShader->SetUniform_P11( glState.projectionMatrix[glState.stackIndex][5] );

		if ( totalPortals > 0 ) {
			globalWorkGroupX += totalPortals % 64 == 0 ?
				totalPortals / 64 : totalPortals / 64 + 1;
		}

		if ( PVSLocked ) {
			if ( r_lockpvs->integer == 0 ) {
				PVSLocked = false;
			}
		}
		if ( r_lockpvs->integer == 1 && !PVSLocked ) {
			PVSLocked = true;
			for ( int i = 0; i < 6; i++ ) {
				VectorCopy( frustum[0][i].normal, lockedFrustums[view][i].normal );
				lockedFrustums[view][i].dist = frustum[0][i].dist;
			}
			VectorCopy( origin, lockedOrigins[view] );
			MatrixCopy( viewMatrix, lockedViewMatrix );
		}

		gl_cullShader->SetUniform_Frustum( frustumPlanes );

		gl_cullShader->DispatchCompute( globalWorkGroupX, 1, 1 );

		gl_processSurfacesShader->BindProgram( 0 );
		gl_processSurfacesShader->SetUniform_Frame( nextFrame );
		gl_processSurfacesShader->SetUniform_ViewID( view );
		gl_processSurfacesShader->SetUniform_SurfaceCommandsOffset( surfaceCommandsCount * ( MAX_VIEWS * nextFrame + view ) );

		glMemoryBarrier( GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT );
		gl_processSurfacesShader->DispatchCompute( totalBatchCount, 1, 1 );
	}

	surfaceDescriptorsSSBO.UnBindBufferBase();
	surfaceCommandsSSBO.UnBindBufferBase();
	culledCommandsBuffer.UnBindBufferBase( GL_SHADER_STORAGE_BUFFER );
	surfaceBatchesUBO.UnBindBufferBase();
	atomicCommandCountersBuffer.UnBindBufferBase( GL_ATOMIC_COUNTER_BUFFER );

	if ( totalPortals > 0 ) {
		portalSurfacesSSBO.UnBindBufferBase();
	}

	if ( r_materialDebug.Get() ) {
		debugSSBO.UnBindBufferBase();
	}

	GL_CheckErrors();
}

void MaterialSystem::StartFrame() {
	if ( !generatedWorldCommandBuffer ) {
		return;
	}
	
	frames[nextFrame].viewCount = 0;
}

void MaterialSystem::EndFrame() {
	if ( !generatedWorldCommandBuffer ) {
		return;
	}

	currentFrame = nextFrame;
	nextFrame++;
	if ( nextFrame >= MAX_FRAMES ) {
		nextFrame = 0;
	}
}

void MaterialSystem::GeneratePortalBoundingSpheres() {
	Log::Debug( "Generating portal bounding spheres" );

	totalPortals = portalSurfacesTmp.size();

	if ( totalPortals == 0 ) {
		return;
	}

	// FIXME: This only requires distance, origin and radius can be moved to surfaceDescriptors SSBO,
	// drawSurfID is not needed as it's the same as the index in portalSurfacesSSBO
	PortalSurface* portalSurfs = new PortalSurface[totalPortals * sizeof( PortalSurface ) * MAX_VIEWFRAMES];

	uint32_t index = 0;
	for ( drawSurf_t* drawSurf : portalSurfacesTmp ) {
		Tess_MapVBOs( /*forceCPU=*/ true );
		Tess_Begin( Tess_StageIteratorDummy, nullptr, nullptr, true, -1, 0 );
		rb_surfaceTable[Util::ordinal( *( drawSurf->surface ) )]( drawSurf->surface );
		const int numVerts = tess.numVertexes;
		vec3_t portalCenter{ 0.0, 0.0, 0.0 };
		for ( int vertIndex = 0; vertIndex < numVerts; vertIndex++ ) {
			VectorAdd( portalCenter, tess.verts[vertIndex].xyz, portalCenter );
		}
		VectorScale( portalCenter, 1.0 / numVerts, portalCenter );

		float furthestDistance = 0.0;
		for ( int vertIndex = 0; vertIndex < numVerts; vertIndex++ ) {
			const float distance = Distance( portalCenter, tess.verts[vertIndex].xyz );
			furthestDistance = distance > furthestDistance ? distance : furthestDistance;
		}

		Tess_Clear();

		portalSurfaces.emplace_back( *drawSurf );
		PortalSurface sphere;
		VectorCopy( portalCenter, sphere.origin );
		sphere.radius = furthestDistance;
		sphere.drawSurfID = portalSurfaces.size() - 1;
		sphere.distance = -1;

		portalBounds.emplace_back( sphere );
		for ( uint32_t i = 0; i < MAX_FRAMES; i++ ) {
			for ( uint32_t j = 0; j < MAX_VIEWS; j++ ) {
				portalSurfs[index + ( i * MAX_VIEWS + j ) * totalPortals] = sphere;
			}
		}
		index++;
	}

	portalSurfacesSSBO.BufferStorage( totalPortals * PORTAL_SURFACE_SIZE * MAX_VIEWS, 2, portalSurfs );
	portalSurfacesSSBO.MapAll();

	portalSurfacesTmp.clear();
}

void MaterialSystem::InitGLBuffers() {
	materialsUBO.GenBuffer();
	texDataBuffer.GenBuffer();
	lightMapDataUBO.GenBuffer();

	surfaceDescriptorsSSBO.GenBuffer();
	surfaceCommandsSSBO.GenBuffer();
	culledCommandsBuffer.GenBuffer();
	surfaceBatchesUBO.GenBuffer();
	atomicCommandCountersBuffer.GenBuffer();

	portalSurfacesSSBO.GenBuffer();

	if ( r_materialDebug.Get() ) {
		debugSSBO.GenBuffer();
	}
}

void MaterialSystem::FreeGLBuffers() {
	materialsUBO.DelBuffer();
	texDataBuffer.DelBuffer();
	lightMapDataUBO.DelBuffer();

	surfaceDescriptorsSSBO.DelBuffer();
	surfaceCommandsSSBO.DelBuffer();
	culledCommandsBuffer.DelBuffer();
	surfaceBatchesUBO.DelBuffer();
	atomicCommandCountersBuffer.DelBuffer();

	portalSurfacesSSBO.DelBuffer();

	if ( r_materialDebug.Get() ) {
		debugSSBO.DelBuffer();
	}
}

void MaterialSystem::Free() {
	generatedWorldCommandBuffer = false;

	materialStages.clear();
	dynamicStages.clear();
	autospriteSurfaces.clear();
	portalSurfaces.clear();
	portalSurfacesTmp.clear();
	portalBounds.clear();
	skyShaders.clear();
	renderedMaterials.clear();
	texData.clear();
	dynamicTexData.clear();

	R_SyncRenderThread();

	surfaceCommandsSSBO.UnmapBuffer();
	culledCommandsBuffer.UnmapBuffer();
	atomicCommandCountersBuffer.UnmapBuffer();
	texDataBuffer.UnmapBuffer();
	lightMapDataUBO.UnmapBuffer();

	if ( totalPortals > 0 ) {
		portalSurfacesSSBO.UnmapBuffer();

		for ( PortalView& portalView : portalStack ) {
			portalView.drawSurf = nullptr;
			memset( portalView.views, 0, MAX_VIEWS * sizeof( uint32_t ) );
			portalView.count = 0;
		}
	}

	currentFrame = 0;
	nextFrame = 1;
	maxStages = 0;

	for ( MaterialPack& pack : materialPacks ) {
		for ( Material& material : pack.materials ) {
			material.drawCommands.clear();
			material.drawSurfs.clear();
		}
		pack.materials.clear();
	}
}

// This gets the information for the surface vertex/index data through Tess
void MaterialSystem::AddDrawCommand( const uint32_t materialID, const uint32_t materialPackID, const uint32_t materialsSSBOOffset,
									 const GLuint count, const GLuint firstIndex ) {
	cmd.cmd.count = count;
	cmd.cmd.firstIndex = firstIndex;
	cmd.cmd.baseInstance = materialsSSBOOffset;
	cmd.materialsSSBOOffset = materialsSSBOOffset;

	materialPacks[materialPackID].materials[materialID].drawCommands.emplace_back( cmd );
	lastCommandID = materialPacks[materialPackID].materials[materialID].drawCommands.size() - 1;
	cmd.textureCount = 0;
}

void MaterialSystem::AddTexture( Texture* texture ) {
	if ( cmd.textureCount >= MAX_DRAWCOMMAND_TEXTURES ) {
		Sys::Drop( "Exceeded max DrawCommand textures" );
	}
	cmd.textures[cmd.textureCount] = texture;
	cmd.textureCount++;
}

bool MaterialSystem::AddPortalSurface( uint32_t viewID, PortalSurface* portalSurfs ) {
	uint32_t portalViews[MAX_VIEWS] {};
	uint32_t count = 0;

	frames[nextFrame].viewFrames[viewID].viewCount = 0;
	portalStack[viewID].count = 0;

	PortalSurface* tmpSurfs = new PortalSurface[totalPortals];
	memcpy( tmpSurfs, portalSurfs + viewID * totalPortals, totalPortals * sizeof( PortalSurface ) );
	std::sort( tmpSurfs, tmpSurfs + totalPortals,
		[]( const PortalSurface& lhs, const PortalSurface& rhs ) {
			return lhs.distance < rhs.distance;
		} );

	for ( uint32_t i = 0; i < totalPortals; i++ ) {
		PortalSurface* portalSurface = &tmpSurfs[i];
		if ( portalSurface->distance == -1 ) { // -1 is set if the surface is culled
			continue;
		}
		
		uint32_t portalViewID = viewCount + 1;
		// This check has to be done first so we can correctly determine when we get to MAX_VIEWS - 1 amount of views
		screenRect_t surfRect;
		bool offScreenOrOutOfRange = 0 != PortalOffScreenOrOutOfRange(
			&portalSurfaces[ portalSurface->drawSurfID ], surfRect );
		Tess_Clear();
		if ( offScreenOrOutOfRange ) {
			continue;
		}

		if ( portalViewID == MAX_VIEWS ) {
			continue;
		}

		portalViews[count] = portalViewID;
		frames[nextFrame].viewFrames[portalViewID].portalSurfaceID = portalSurface->drawSurfID;
		frames[nextFrame].viewFrames[viewID].viewCount++;

		portalStack[viewID].views[count] = portalViewID;
		portalStack[portalViewID].drawSurf = &portalSurfaces[portalSurface->drawSurfID];
		portalStack[viewID].count++;

		count++;
		viewCount++;

		if ( count == MAX_VIEWS || viewCount == MAX_VIEWS ) {
			return false;
		}

		for ( uint32_t j = 0; j < frames[currentFrame].viewFrames[viewID].viewCount; j++ ) {
			uint32_t subView = frames[currentFrame].viewFrames[viewID].portalViews[j];
			if ( subView != 0 && portalSurface->drawSurfID == frames[currentFrame].viewFrames[subView].portalSurfaceID ) {
				if ( !AddPortalSurface( subView, portalSurfs ) ) {
					return false;
				}

				portalViewID = subView;
				break;
			}
		}
	}

	memcpy( frames[nextFrame].viewFrames[viewID].portalViews, portalViews, MAX_VIEWS * sizeof( uint32_t ) );

	return true;
}

void MaterialSystem::AddPortalSurfaces() {
	if ( totalPortals == 0 ) {
		return;
	}

	if ( r_lockpvs->integer ) {
		return;
	}

	portalSurfacesSSBO.BindBufferBase();
	PortalSurface* portalSurfs = ( PortalSurface* ) portalSurfacesSSBO.GetCurrentAreaData();
	viewCount = 0;
	// This will recursively find potentially visible portals in each view based on the data read back from the GPU
	// It only fills up an array up to MAX_VIEWS, the actual views are still added in R_MirrowViewBySurface()
	AddPortalSurface( 0, portalSurfs );
	portalSurfacesSSBO.AreaIncr();
}

// autosprite[2] is not implemented in material system, draw them old-fashionedly
void MaterialSystem::AddAutospriteSurfaces() {
	tr.currentEntity = &tr.worldEntity;

	for ( const drawSurf_t &drawSurf : autospriteSurfaces )
	{
		R_AddDrawSurf( drawSurf.surface, drawSurf.shader,
		               drawSurf.lightmapNum(), drawSurf.fogNum(), drawSurf.bspSurface );
	}
}

void MaterialSystem::RenderMaterials( const shaderSort_t fromSort, const shaderSort_t toSort, const uint32_t viewID ) {
	if ( !r_drawworld->integer ) {
		return;
	}

	if ( r_materialSystemSkip.Get() ) {
		return;
	}

	if ( frameStart ) {
		renderedMaterials.clear();
		UpdateDynamicSurfaces();
		UpdateFrameData();

		// Make sure compute dispatches from the last frame finished writing to memory
		glMemoryBarrier( GL_COMMAND_BARRIER_BIT );
		frameStart = false;
	}

	materialsUBO.BindBufferBase();

	geometryCache.Bind();

	for ( MaterialPack& materialPack : materialPacks ) {
		if ( materialPack.fromSort >= fromSort && materialPack.toSort <= toSort ) {
			for ( Material& material : materialPack.materials ) {
				RenderMaterial( material, viewID );
				renderedMaterials.emplace_back( &material );
			}
		}
	}

	glBindVertexArray( backEnd.currentVAO );

	// Draw the skybox here because we skipped R_AddWorldSurfaces()
	const bool environmentFogDraw = ( fromSort <= shaderSort_t::SS_ENVIRONMENT_FOG ) && ( toSort >= shaderSort_t::SS_ENVIRONMENT_FOG );
	const bool environmentNoFogDraw = ( fromSort <= shaderSort_t::SS_ENVIRONMENT_NOFOG ) && toSort >= ( shaderSort_t::SS_ENVIRONMENT_NOFOG );
	if ( tr.hasSkybox && ( environmentFogDraw || environmentNoFogDraw ) ) {
		const bool noFogPass = toSort >= shaderSort_t::SS_ENVIRONMENT_NOFOG;
		for ( shader_t* skyShader : skyShaders ) {
			if ( skyShader->noFog != noFogPass ) {
				continue;
			}

			tr.drawingSky = true;
			Tess_Begin( Tess_StageIteratorSky, skyShader, nullptr, false, -1, 0, false );
			Tess_End();
		}
	}
}

void MaterialSystem::RenderIndirect( const Material& material, const uint32_t viewID, const GLenum mode = GL_TRIANGLES ) {
	glMultiDrawElementsIndirectCountARB( mode, GL_UNSIGNED_INT,
		BUFFER_OFFSET( material.surfaceCommandBatchOffset * SURFACE_COMMANDS_PER_BATCH * sizeof( GLIndirectCommand )
		               + ( surfaceCommandsCount * ( MAX_VIEWS * currentFrame + viewID )
		               * sizeof( GLIndirectCommand ) ) ),
		material.globalID * sizeof( uint32_t )
		+ ( MAX_COMMAND_COUNTERS * ( MAX_VIEWS * currentFrame + viewID ) ) * sizeof( uint32_t ),
		material.drawCommands.size(), 0 );
}

void MaterialSystem::RenderMaterial( Material& material, const uint32_t viewID ) {
	uint32_t stateBits = material.stateBits;

	if ( r_profilerRenderSubGroups.Get() ) {
		const int materialID = r_profilerRenderSubGroupsStage.Get();
		if ( materialID != -1 ) {
			// Make sure we don't skip depth pre-pass materials; ID starts at opaque materials because we can't use this with depth materials
			if ( ( material.globalID >= materialPacks[0].materials.size() ) && ( material.globalID != materialID + materialPacks[0].materials.size() ) ) {
				return;
			}
		}

		switch ( r_profilerRenderSubGroupsMode.Get() ) {
			case Util::ordinal( shaderProfilerRenderSubGroupsMode::VS_OPAQUE ):
			case Util::ordinal( shaderProfilerRenderSubGroupsMode::FS_OPAQUE ):
				if ( material.stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) {
					return;
				}
				break;
			case Util::ordinal( shaderProfilerRenderSubGroupsMode::VS_TRANSPARENT ):
			case Util::ordinal( shaderProfilerRenderSubGroupsMode::FS_TRANSPARENT ):
				if ( material.stateBits & ~( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) {
					return;
				}
				break;
			case Util::ordinal( shaderProfilerRenderSubGroupsMode::VS_ALL ):
			case Util::ordinal( shaderProfilerRenderSubGroupsMode::FS_ALL ):
			default:
				break;
		}

		stateBits &= ~( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
	}

	if( material.shaderBinder == BindShaderFog ) {
		if ( r_noFog->integer || !r_wolfFog->integer || ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
			return;
		}
	}

	backEnd.currentEntity = &tr.worldEntity;

	GL_State( stateBits );
	if ( material.usePolygonOffset ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		GL_PolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	} else {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
	GL_Cull( material.cullType );

	backEnd.orientation = backEnd.viewParms.world;
	GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );

	material.shaderBinder( &material );

	if ( !material.texturesResident ) {
		for ( Texture* texture : material.textures ) {
			if ( !texture->IsResident() ) {
				texture->MakeResident();

				bool resident = glIsTextureHandleResidentARB( texture->bindlessTextureHandle );

				if ( resident ) {
					continue;
				}

				for ( Material* mat : renderedMaterials ) {
					Log::Warn( "Making material %u textures non-resident (%u)", mat->id, mat->textures.size() );
					for ( Texture* tex : mat->textures ) {
						if ( tex->IsResident() ) {
							tex->MakeNonResident();
						}
					}
					mat->texturesResident = false;
				}

				texture->MakeResident();

				resident = glIsTextureHandleResidentARB( texture->bindlessTextureHandle );

				if( !resident ) {
					Log::Warn( "Not enough texture space! Some textures may be missing" );
					break;
				}
			}
		}
	}
	material.texturesResident = true;

	culledCommandsBuffer.BindBuffer( GL_DRAW_INDIRECT_BUFFER );

	atomicCommandCountersBuffer.BindBuffer( GL_PARAMETER_BUFFER_ARB );

	texDataBuffer.BindBufferBase( texDataBufferType );
	lightMapDataUBO.BindBufferBase();

	if ( r_showGlobalMaterials.Get() && material.sort != 0
		&& ( material.shaderBinder == BindShaderLightMapping || material.shaderBinder == BindShaderGeneric3D ) ) {
		vec3_t color;
		/* Some simple random modifiers to make the colors more contrasting
		Maybe we can use some better way of assigning colors here? */
		static vec3_t colors[6] = { { 0.75, 0.25, 0.25 }, { 0.75, 0.75, 0.25 }, { 0.25, 0.75, 0.25 }, { 0.25, 0.75, 0.75 },
			{ 0.25, 0.25, 0.75 }, { 0.75, 0.25, 0.75 } };

		switch ( r_showGlobalMaterials.Get() ) {
			case Util::ordinal( MaterialDebugMode::DEPTH ):
			{
				// Even though this is for depth materials, we don't actually draw anything on depth pass
				if ( material.sort != 1 ) {
					return;
				}

				const float id = ( float ) material.id / ( materialPacks[0].materials.size() + 2 ) + 1;

				color[0] = std::min( id, 1 / 3.0f ) * 3.0 * colors[int( material.id * 6.0
					/ materialPacks[0].materials.size() )][0];
				color[1] = Math::Clamp( id - 1 / 3.0, 0.0, 1 / 3.0 ) * 3.0 * colors[int( material.id * 6.0
					/ materialPacks[0].materials.size() )][1];
				color[2] = Math::Clamp( id - 2 / 3.0, 0.0, 1 / 3.0 ) * 3.0 * colors[int( material.id * 6.0
					/ materialPacks[0].materials.size() )][2];

				break;
			}
			case Util::ordinal( MaterialDebugMode::OPAQUE ):
			{
				if ( material.sort != 1 ) {
					return;
				}

				const float id = ( float ) ( material.id + 1 )
					/ ( materialPacks[1].materials.size() + materialPacks[2].materials.size() + 2 );

				color[0] = std::min( id, 1 / 3.0f ) * 3.0 * colors[int( material.id * 6.0
					/ ( materialPacks[1].materials.size() + materialPacks[2].materials.size() ) )][0];
				color[1] = Math::Clamp( id - 1 / 3.0, 0.0, 1 / 3.0 ) * 3.0 * colors[int( material.id * 6.0
					/ ( materialPacks[1].materials.size() + materialPacks[2].materials.size() ) )][1];
				color[2] = Math::Clamp( id - 2 / 3.0, 0.0, 1 / 3.0 ) * 3.0 * colors[int( material.id * 6.0
					/ ( materialPacks[1].materials.size() + materialPacks[2].materials.size() ) )][2];

				break;
			}
			case Util::ordinal( MaterialDebugMode::OPAQUE_TRANSPARENT ):
			{
				if ( material.sort == 0 ) {
					return;
				}

				const float id = ( float ) ( material.id + 1 )
					/ ( materialPacks[1].materials.size() + materialPacks[2].materials.size() + 2 ) + 1;

				color[0] = std::min( id, 1 / 3.0f ) * 3.0 * colors[int( material.id * 6.0
					/ ( materialPacks[1].materials.size() + materialPacks[2].materials.size() ) )][0];
				color[1] = Math::Clamp( id - 1 / 3.0, 0.0, 1 / 3.0 ) * 3.0 * colors[int( material.id * 6.0
					/ ( materialPacks[1].materials.size() + materialPacks[2].materials.size() ) )][1];
				color[2] = Math::Clamp( id - 2 / 3.0, 0.0, 1 / 3.0 ) * 3.0 * colors[int( material.id * 6.0
					/ ( materialPacks[1].materials.size() + materialPacks[2].materials.size() ) )][2];

				break;
			}
			default:
				break;
		}

		if ( material.shaderBinder == BindShaderLightMapping ) {
			gl_lightMappingShaderMaterial->SetUniform_MaterialColour( color );
		} else if ( material.shaderBinder == BindShaderGeneric3D ) {
			gl_genericShaderMaterial->SetUniform_MaterialColour( color );
		}
	}

	RenderIndirect( material, viewID );

	if( material.shaderBinder == BindShaderHeatHaze ) {
		// Hack: Use a global uniform for heatHaze with material system to avoid duplicating all of the shader stage data
		gl_heatHazeShaderMaterial->SetUniform_CurrentMapBindless(
			GL_BindToTMU( 1, tr.currentRenderImage[1 - backEnd.currentMainFBO] )
		);

		gl_heatHazeShaderMaterial->SetUniform_DeformEnable( false );

		// copy to foreground image
		R_BindFBO( tr.mainFBO[backEnd.currentMainFBO] );

		RenderIndirect( material, viewID );
	}

	if ( r_showTris->integer
		&& ( material.stateBits & GLS_DEPTHMASK_TRUE ) == 0
		&& ( material.shaderBinder == &BindShaderLightMapping || material.shaderBinder == &BindShaderGeneric3D ) )
	{
		if ( material.shaderBinder == &BindShaderLightMapping ) {
			gl_lightMappingShaderMaterial->SetUniform_ShowTris( 1 );
		} else if ( material.shaderBinder == &BindShaderGeneric3D ) {
			gl_genericShaderMaterial->SetUniform_ShowTris( 1 );
		}

		GL_State( GLS_DEPTHTEST_DISABLE );
		RenderIndirect( material, viewID, GL_LINES );

		if ( material.shaderBinder == &BindShaderLightMapping ) {
			gl_lightMappingShaderMaterial->SetUniform_ShowTris( 0 );
		} else if ( material.shaderBinder == &BindShaderGeneric3D ) {
			gl_genericShaderMaterial->SetUniform_ShowTris( 0 );
		}
	}

	culledCommandsBuffer.UnBindBuffer( GL_DRAW_INDIRECT_BUFFER );

	atomicCommandCountersBuffer.UnBindBuffer( GL_PARAMETER_BUFFER_ARB );

	texDataBuffer.UnBindBufferBase( texDataBufferType );
	lightMapDataUBO.UnBindBufferBase();

	if ( material.usePolygonOffset ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}
