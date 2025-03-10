/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2006-2011 Robert Beckebans <trebor_7@users.sourceforge.net>

This file is part of Daemon source code.

Daemon source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Daemon source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Daemon source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_shade.c
#include "tr_local.h"
#include "gl_shader.h"
#include "Material.h"
#include "ShadeCommon.h"

/*
=================================================================================
THIS ENTIRE FILE IS BACK END!
^ Not true, EnableAvailableFeatures() right below this isn't.

This file deals with applying shaders to surface data in the tess struct.
=================================================================================
*/

static void EnableAvailableFeatures()
{
	glConfig2.realtimeLighting = r_realtimeLighting.Get();

	if ( glConfig2.realtimeLighting )
	{
		if ( r_realtimeLightingRenderer.Get() == Util::ordinal( realtimeLightingRenderer_t::TILED ) )
		{
			if ( !glConfig2.uniformBufferObjectAvailable ) {
				Log::Warn( "Tiled dynamic light renderer disabled because GL_ARB_uniform_buffer_object is not available." );
				glConfig2.realtimeLighting = false;
			}

			if ( !glConfig2.textureIntegerAvailable ) {
				Log::Warn( "Tiled dynamic light renderer disabled because GL_EXT_texture_integer is not available." );
				glConfig2.realtimeLighting = false;
			}

			if ( glConfig2.max3DTextureSize == 0 )
			{
				Log::Warn( "Tiled dynamic light renderer disabled because of missing 3D texture support." );
				glConfig2.realtimeLighting = false;
			}

			// See below about ALU instructions on ATI R300 and Intel GMA 3.
			if ( !glConfig2.glCoreProfile && glConfig2.maxAluInstructions < 128 )
			{
				Log::Warn( "Tiled dynamic light rendered disabled because GL_MAX_PROGRAM_ALU_INSTRUCTIONS_ARB is too small: %d", glConfig2.maxAluInstructions );
				glConfig2.realtimeLighting = false;
			}
		}
	}

	if ( glConfig2.realtimeLighting ) {
		glConfig2.realtimeLightLayers = r_realtimeLightLayers.Get();

		if ( glConfig2.realtimeLightLayers > glConfig2.max3DTextureSize ) {
			glConfig2.realtimeLightLayers = glConfig2.max3DTextureSize;
			Log::Notice( "r_realtimeLightLayers exceeds maximum 3D texture size, using %i instead", glConfig2.max3DTextureSize );
		}

		Log::Notice( "Using %i dynamic light layers, %i dynamic lights available per tile", glConfig2.realtimeLightLayers,
			glConfig2.realtimeLightLayers * 16 );
	}

	glConfig2.colorGrading = r_colorGrading.Get();

	if ( glConfig2.colorGrading )
	{
		if ( glConfig2.max3DTextureSize == 0 )
		{
			Log::Warn( "Color grading disabled because of missing 3D texture support." );
			glConfig2.colorGrading = false;
		}
	}

	glConfig2.shadowingMode = shadowingMode_t( r_shadows.Get() );
	glConfig2.shadowMapping = glConfig2.shadowingMode >= shadowingMode_t::SHADOWING_ESM16;

	if ( glConfig2.shadowMapping )
	{
		if ( !glConfig2.textureFloatAvailable )
		{
			Log::Warn( "Shadow mapping disabled because ARB_texture_float is not available" );
			glConfig2.shadowMapping = false;
		}
	}

	glConfig2.deluxeMapping = r_deluxeMapping->integer;
	glConfig2.normalMapping = r_normalMapping->integer;
	glConfig2.specularMapping = r_specularMapping->integer;
	glConfig2.physicalMapping = r_physicalMapping->integer;
	glConfig2.reliefMapping = r_reliefMapping->integer;

	/* ATI R300 and Intel GMA 3 only have 64 ALU instructions, which is not enough for some shader
	variants. For example the lightMapping shader permutation with macros USE_GRID_LIGHTING and
	USE_GRID_DELUXE_MAPPING from the medium graphics preset requires 67 ALU.
	For comparison, ATI R400 and R500 have 512 of them. */
	if ( !glConfig2.glCoreProfile && glConfig2.maxAluInstructions < 128 )
	{
		static const std::pair<bool*, std::string> aluFeatures[] = {
			/* Normal mapping, specular mapping and physical mapping does nothing when deluxe mapping
			is disabled. Hardware that can't do deluxe mapping or normal mapping is not powerful
			enoough to do relief mapping. */
			{ &glConfig2.deluxeMapping, "Deluxe mapping" },
			{ &glConfig2.normalMapping, "Normal mapping" },
			{ &glConfig2.specularMapping, "Specular mapping" },
			{ &glConfig2.physicalMapping, "Physical mapping" },
			{ &glConfig2.reliefMapping, "Relief mapping" },
		};

		for ( auto& f : aluFeatures )
		{
			if ( *f.first )
			{
				Log::Warn( "%s disabled because GL_MAX_PROGRAM_ALU_INSTRUCTIONS_ARB is too small: %d", f.second, glConfig2.maxAluInstructions );
				*f.first = false;
			}
		}
	}


	// Disable features that require deluxe mapping to be enabled.
	glConfig2.normalMapping = glConfig2.deluxeMapping && glConfig2.normalMapping;
	glConfig2.specularMapping = glConfig2.deluxeMapping && glConfig2.specularMapping;
	glConfig2.physicalMapping = glConfig2.deluxeMapping && glConfig2.physicalMapping;

	glConfig2.bloom = r_bloom.Get();

	/* Motion blur is enabled by cg_motionblur which is a client cvar so we have to build it in all cases,
	unless unsupported by the hardware which is the only condition when the engine knows it is not used. */
	glConfig2.motionBlur = true;

	// This will be enabled later on by R_BuildCubeMaps()
	glConfig2.reflectionMapping = false;

	/* Intel GMA 3 only has 4 tex indirections, which is not enough for some shaders.
	For example blurX requires 6, contrast requires 5, motionblur requires 5…
	For comparison, ATI R300, R400 and R500 have 16 of them. We don't need a finer check as early R300
	hardware with 16 indirections would better not run that code for performance, so disabling the shader
	by mistake on an hypothetical lower-end hardware only supporting 8 indirections can't do harm. */
	if ( !glConfig2.glCoreProfile && glConfig2.maxTexIndirections < 16 )
	{
		static const std::pair<bool*, std::string> indirectFeatures[] = {
			{ &glConfig2.shadowMapping, "Shadow mapping" },
			{ &glConfig2.bloom, "Bloom" },
			{ &glConfig2.motionBlur, "Motion blur" },
		};

		for ( auto& f : indirectFeatures )
		{
			if ( *f.first )
			{
				Log::Warn( "%s disabled because GL_MAX_PROGRAM_NATIVE_TEX_INDIRECTIONS_ARB is too small: %d", f.second, glConfig2.maxTexIndirections );
				*f.first = false;
			}
		}
	}

	glConfig2.usingMaterialSystem = r_materialSystem.Get() && glConfig2.materialSystemAvailable;
	glConfig2.usingBindlessTextures = glConfig2.usingMaterialSystem ||
		( r_preferBindlessTextures.Get() && glConfig2.bindlessTexturesAvailable );
	glConfig2.usingGeometryCache = glConfig2.usingMaterialSystem && glConfig2.geometryCacheAvailable;
}

// For shaders that require map data for compile-time values 
void GLSL_InitWorldShaders() {
	// make sure the render thread is stopped
	R_SyncRenderThread();

	GL_CheckErrors();

	gl_shaderManager.GenerateWorldHeaders();

	// Material system shaders that are always loaded if material system is available
	if ( glConfig2.usingMaterialSystem ) {
		gl_shaderManager.LoadShader( gl_cullShader );
	}
}

static void GLSL_InitGPUShadersOrError()
{
	// make sure the render thread is stopped
	R_SyncRenderThread();

	GL_CheckErrors();

	gl_shaderManager.InitDriverInfo();

	/* It must be done before GenerateBuiltinHeaders() because glConfig2.realtimeLighting
	is read in GenEngineConstants(). */
	EnableAvailableFeatures();

	gl_shaderManager.GenerateBuiltinHeaders();

	// single texture rendering
	gl_shaderManager.LoadShader( gl_genericShader );

	// standard light mapping
	gl_shaderManager.LoadShader( gl_lightMappingShader );

	// Material system shaders that are always loaded if material system is available
	if ( glConfig2.usingMaterialSystem )
	{
		gl_shaderManager.LoadShader( gl_genericShaderMaterial );
		gl_shaderManager.LoadShader( gl_lightMappingShaderMaterial );

		gl_shaderManager.LoadShader( gl_clearSurfacesShader );
		gl_shaderManager.LoadShader( gl_processSurfacesShader );
		gl_shaderManager.LoadShader( gl_depthReductionShader );
	}

	if ( tr.world ) // this only happens with /glsl_restart
	{
		GLSL_InitWorldShaders();
	}

	if ( glConfig2.realtimeLighting )
	{
		realtimeLightingRenderer_t realtimeLightingRenderer = realtimeLightingRenderer_t( r_realtimeLightingRenderer.Get() );

		switch( realtimeLightingRenderer )
		{
		case realtimeLightingRenderer_t::LEGACY:
			// projective lighting ( Doom3 style )
			gl_shaderManager.LoadShader( gl_forwardLightingShader_projXYZ );

			// omni-directional specular bump mapping ( Doom3 style )
			gl_shaderManager.LoadShader( gl_forwardLightingShader_omniXYZ );

			// directional sun lighting ( Doom3 style )
			gl_shaderManager.LoadShader( gl_forwardLightingShader_directionalSun );
			break;
		case realtimeLightingRenderer_t::TILED:
			gl_shaderManager.LoadShader( gl_depthtile1Shader );
			gl_shaderManager.LoadShader( gl_depthtile2Shader );
			gl_shaderManager.LoadShader( gl_lighttileShader );
			DAEMON_FALLTHROUGH;
		default:
			/* Dynamic shadowing code also needs this shader.
			This code is not well known, so there may be a bug,
			but commit a09f03bc8e775d83ac5e057593eff4e88cdea7eb mentions this:

			> Use conventional shadow mapping code for inverse lights.
			> This re-enables shadows for players in the tiled renderer.
			> -- @gimhael

			See also https://github.com/DaemonEngine/Daemon/pull/606#pullrequestreview-912402293 */
			if ( glConfig2.shadowMapping )
			{
				// projective lighting ( Doom3 style )
				gl_shaderManager.LoadShader( gl_forwardLightingShader_projXYZ );
			}
		}
	}

	if ( glConfig2.reflectionMappingAvailable )
	{
		// bumped cubemap reflection for abitrary polygons ( EMBM )
		gl_shaderManager.LoadShader( gl_reflectionShader );

		if ( glConfig2.usingMaterialSystem )
		{
			gl_shaderManager.LoadShader( gl_reflectionShaderMaterial );
		}
	}

	if ( r_drawSky.Get() )
	{
		// skybox drawing for abitrary polygons
		gl_shaderManager.LoadShader( gl_skyboxShader );

		if ( glConfig2.usingMaterialSystem )
		{
			gl_shaderManager.LoadShader( gl_skyboxShaderMaterial );
		}
	}

	// Fog GLSL is always loaded and built because disabling fog is cheat.
	{
		// Q3A volumetric fog
		gl_shaderManager.LoadShader( gl_fogQuake3Shader );

		if ( glConfig2.usingMaterialSystem )
		{
			gl_shaderManager.LoadShader( gl_fogQuake3ShaderMaterial );
		}

		// global fog post process effect
		gl_shaderManager.LoadShader( gl_fogGlobalShader );
	}

	if ( r_heatHaze->integer )
	{
		// heatHaze post process effect
		gl_shaderManager.LoadShader( gl_heatHazeShader );

		if ( glConfig2.usingMaterialSystem )
		{
			gl_shaderManager.LoadShader( gl_heatHazeShaderMaterial );
		}
	}

	if ( glConfig2.bloom )
	{
		// screen post process effect
		gl_shaderManager.LoadShader( gl_screenShader );

		if ( glConfig2.usingMaterialSystem )
		{
			gl_shaderManager.LoadShader( gl_screenShaderMaterial );
		}

		// LDR bright pass filter
		gl_shaderManager.LoadShader( gl_contrastShader );
	}

	
	// portal process effect
	gl_shaderManager.LoadShader( gl_portalShader );

	// camera post process effect
	gl_shaderManager.LoadShader( gl_cameraEffectsShader );

	if ( glConfig2.bloom || glConfig2.shadowMapping )
	{
		// gaussian blur
		gl_shaderManager.LoadShader( gl_blurShader );
	}

	if ( glConfig2.shadowMapping )
	{
		// shadowmap distance compression
		gl_shaderManager.LoadShader( gl_shadowFillShader );

		// debug utils
		gl_shaderManager.LoadShader( gl_debugShadowMapShader );
	}

	if ( r_liquidMapping->integer != 0 )
	{
		gl_shaderManager.LoadShader( gl_liquidShader );

		if ( glConfig2.usingMaterialSystem )
		{
			gl_shaderManager.LoadShader( gl_liquidShaderMaterial );
		}
	}

	if ( glConfig2.motionBlur )
	{
		gl_shaderManager.LoadShader( gl_motionblurShader );
	}

	if ( r_ssao->integer )
	{
		if ( glConfig2.textureGatherAvailable )
		{
			gl_shaderManager.LoadShader( gl_ssaoShader );
		}
		else
		{
			Log::Warn("SSAO not used because GL_ARB_texture_gather is not available.");
		}
	}

	if ( r_FXAA->integer != 0 )
	{
		gl_shaderManager.LoadShader( gl_fxaaShader );
	}

	if ( r_lazyShaders.Get() == 0 )
	{
		gl_shaderManager.BuildAll();
	}
}

void GLSL_InitGPUShaders()
{
	/*
	 Without a shaderpath option, the shader debugging cycle is like this:
	 1. Change shader file(s).
	 2. Recompile app to pickup the new *.glsl changes.
	 3. Run the app and get to the point required to check work.
	 4. If the change failed or succeeded but you want to make more changes restart at step 1.

	 Alternatively, if -set shaderpath "daemon/src/engine/renderer/glsl_source"
	 is set on the command line, the cycle is:
	 1. Start the app.
	 2. Change shader file(s)
	 3. Do /glsl_restart at the app console to reload them. If there is a problem and
	    r_lazyShaders < 2, the app will revert to the last working changes in shaders.cpp, so no need
	    to restart the app. OTOH if r_lazyShaders = 2 you can iterate really fast, but
	    you die if you make a typo.
	 4. If further changes are needed, repeat from step 3.

	 Note that Daemon will respond by listing the files it thinks are different.
	 If this matches your expectations then it's not an error.
	 */

	auto shaderPath = GetShaderPath();
	if (shaderPath.empty())
		shaderKind = ShaderKind::BuiltIn;
	else
		shaderKind = ShaderKind::External;

	bool externalFailed = false;
	if (shaderKind == ShaderKind::External)
	{
		try
		{
			Log::Warn("Loading external shaders.");
			GLSL_InitGPUShadersOrError();
			if ( r_lazyShaders.Get() == 1 && tr.world != nullptr )
			{
				gl_shaderManager.BuildAll();
			}
			Log::Warn("External shaders in use.");
		}
		catch (const ShaderException& e)
		{
			Log::Warn("External shaders failed: %s", e.what());
			Log::Warn("Attempting to use built in shaders instead.");
			shaderKind = ShaderKind::BuiltIn;
			externalFailed = true;
		}
	}

	if (shaderKind == ShaderKind::BuiltIn)
	{
		// Let the user know if we are transitioning from external to
		// built-in shaders. We won't alert them if we were already using
		// built-in shaders as this is the normal case.
		try
		{
			GLSL_InitGPUShadersOrError();
		}
		catch (const ShaderException&e)
		{
			Sys::Error("Built-in shaders failed: %s", e.what()); // Fatal.
		};
		if (externalFailed)
			Log::Warn("Now using built-in shaders because external shaders failed.");
	}
}

void GLSL_ShutdownGPUShaders()
{
	R_SyncRenderThread();

	gl_shaderManager.FreeAll();

	gl_genericShader = nullptr;
	gl_genericShaderMaterial = nullptr;
	gl_cullShader = nullptr;
	gl_depthReductionShader = nullptr;
	gl_clearSurfacesShader = nullptr;
	gl_processSurfacesShader = nullptr;
	gl_lightMappingShader = nullptr;
	gl_lightMappingShaderMaterial = nullptr;
	gl_forwardLightingShader_omniXYZ = nullptr;
	gl_forwardLightingShader_projXYZ = nullptr;
	gl_forwardLightingShader_directionalSun = nullptr;
	gl_shadowFillShader = nullptr;
	gl_reflectionShader = nullptr;
	gl_reflectionShaderMaterial = nullptr;
	gl_skyboxShader = nullptr;
	gl_skyboxShaderMaterial = nullptr;
	gl_fogQuake3Shader = nullptr;
	gl_fogQuake3ShaderMaterial = nullptr;
	gl_fogGlobalShader = nullptr;
	gl_heatHazeShader = nullptr;
	gl_heatHazeShaderMaterial = nullptr;
	gl_screenShader = nullptr;
	gl_screenShaderMaterial = nullptr;
	gl_portalShader = nullptr;
	gl_contrastShader = nullptr;
	gl_cameraEffectsShader = nullptr;
	gl_blurShader = nullptr;
	gl_debugShadowMapShader = nullptr;
	gl_liquidShader = nullptr;
	gl_liquidShaderMaterial = nullptr;
	gl_motionblurShader = nullptr;
	gl_ssaoShader = nullptr;
	gl_depthtile1Shader = nullptr;
	gl_depthtile2Shader = nullptr;
	gl_lighttileShader = nullptr;
	gl_fxaaShader = nullptr;

	GL_BindNullProgram();
}

void GLSL_FinishGPUShaders()
{
	R_SyncRenderThread();

	gl_shaderManager.BuildAll();
}

/*
==================
Tess_DrawElements
==================
*/
void Tess_DrawElements()
{
	int i;

	if ( ( tess.numIndexes == 0 || tess.numVertexes == 0 ) && tess.multiDrawPrimitives == 0 )
	{
		return;
	}

	// move tess data through the GPU, finally
	if ( glState.currentVBO && glState.currentIBO )
	{
		if ( tess.multiDrawPrimitives )
		{
			if ( !materialSystem.generatingWorldCommandBuffer ) {
				glMultiDrawElements( GL_TRIANGLES, tess.multiDrawCounts, GL_INDEX_TYPE, ( const GLvoid** ) tess.multiDrawIndexes, tess.multiDrawPrimitives );
			}

			backEnd.pc.c_multiDrawElements++;
			backEnd.pc.c_multiDrawPrimitives += tess.multiDrawPrimitives;

			backEnd.pc.c_vboVertexes += tess.numVertexes;

			for ( i = 0; i < tess.multiDrawPrimitives; i++ )
			{
				backEnd.pc.c_multiVboIndexes += tess.multiDrawCounts[ i ];
				backEnd.pc.c_indexes += tess.multiDrawCounts[ i ];
				if ( materialSystem.generatingWorldCommandBuffer ) {
					materialSystem.AddDrawCommand( tess.materialID, tess.materialPackID, tess.currentSSBOOffset,
						( GLuint ) tess.multiDrawCounts[i], tess.multiDrawOffsets[i] );
				}
			}
		}
		else
		{
			uintptr_t base = 0;

			if( glState.currentIBO == tess.ibo ) {
				base = tess.indexBase * sizeof( glIndex_t );
			}

			if ( materialSystem.generatingWorldCommandBuffer ) {
				materialSystem.AddDrawCommand( tess.materialID, tess.materialPackID, tess.currentSSBOOffset, tess.numIndexes, tess.indexBase );
			} else {
				glDrawRangeElements( GL_TRIANGLES, 0, tess.numVertexes, tess.numIndexes, GL_INDEX_TYPE, BUFFER_OFFSET( base ) );
			}

			backEnd.pc.c_drawElements++;

			backEnd.pc.c_vboVertexes += tess.numVertexes;
			backEnd.pc.c_vboIndexes += tess.numIndexes;

			backEnd.pc.c_indexes += tess.numIndexes;
			backEnd.pc.c_vertexes += tess.numVertexes;
		}
	}
	else
	{
		if ( materialSystem.generatingWorldCommandBuffer ) {
			materialSystem.AddDrawCommand( tess.materialID, tess.materialPackID, tess.currentSSBOOffset, tess.numIndexes, tess.indexBase );
		} else {
			glDrawElements( GL_TRIANGLES, tess.numIndexes, GL_INDEX_TYPE, tess.indexes );
		}

		backEnd.pc.c_drawElements++;

		backEnd.pc.c_indexes += tess.numIndexes;
		backEnd.pc.c_vertexes += tess.numVertexes;
	}
}

/*
==================
Tess_DrawArrays
==================
*/
void Tess_DrawArrays( GLenum elementType )
{
	if ( tess.numVertexes == 0 )
	{
		return;
	}

	/* Move tess data through the GPU, finally.

	Radeon R300 small ALU is known to fail on this glDrawArrays call:

	> r300 FP: Compiler Error:
	> ../src/gallium/drivers/r300/compiler/r300_fragprog_emit.c::emit_alu(): Too many ALU instructions
	> Using a dummy shader instead.
	> r300 FP: Compiler Error:
	> build_loop_info: Cannot find condition for if
	> Using a dummy shader instead.

	See https://github.com/DaemonEngine/Daemon/issues/344 */

	glDrawArrays( elementType, 0, tess.numVertexes );

	backEnd.pc.c_drawElements++;

	backEnd.pc.c_indexes += tess.numIndexes;
	backEnd.pc.c_vertexes += tess.numVertexes;

	if ( glState.currentVBO )
	{
		backEnd.pc.c_vboVertexes += tess.numVertexes;
		backEnd.pc.c_vboIndexes += tess.numIndexes;
	}
}

/*
=============================================================

SURFACE SHADERS

=============================================================
*/

alignas(16) shaderCommands_t tess;

/*
================
DrawTris

Draws triangle outlines for debugging
================
*/
static void DrawTris()
{
	int deform = 0;

	GLimp_LogComment( "--- DrawTris ---\n" );

	gl_genericShader->SetVertexSkinning( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning );
	gl_genericShader->SetVertexAnimation( tess.vboVertexAnimation );
	gl_genericShader->SetTCGenEnvironment( false );
	gl_genericShader->SetTCGenLightmap( false );
	gl_genericShader->SetDepthFade( false );

	if( tess.surfaceStages != tess.surfaceLastStage ) {
		deform = tess.surfaceStages[ 0 ].deformIndex;
	}

	gl_genericShader->BindProgram( deform );

	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );

	// u_AlphaThreshold
	gl_genericShader->SetUniform_AlphaTest( GLS_ATEST_NONE );

	if ( r_showBatches->integer || r_showLightBatches->integer )
	{
		gl_genericShader->SetUniform_Color( Color::Color::Indexed( backEnd.pc.c_batches % 8 ) );
	}
	else if ( glState.currentVBO == tess.vbo )
	{
		gl_genericShader->SetUniform_Color( Color::Red );
	}
	else if ( glState.currentVBO )
	{
		gl_genericShader->SetUniform_Color( Color::Blue );
	}
	else
	{
		gl_genericShader->SetUniform_Color( Color::White );
	}

	gl_genericShader->SetUniform_ColorModulateColorGen( colorGen_t::CGEN_CONST, alphaGen_t::AGEN_CONST );
	gl_genericShader->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
	gl_genericShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

	if ( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning )
	{
		gl_genericShader->SetUniform_Bones( tess.numBones, tess.bones );
	}

	// u_DeformGen
	gl_genericShader->SetUniform_Time( backEnd.refdef.floatTime - backEnd.currentEntity->e.shaderTime );

	// bind u_ColorMap
	gl_genericShader->SetUniform_ColorMapBindless(
		GL_BindToTMU( 0, tr.whiteImage )
	);
	gl_genericShader->SetUniform_TextureMatrix( tess.svars.texMatrices[ TB_COLORMAP ] );
	gl_genericShader->SetRequiredVertexPointers();

	glDepthRange( 0, 0 );

	Tess_DrawElements();

	glDepthRange( 0, 1 );
}

/*
==============
Tess_Begin

We must set some things up before beginning any tesselation,
because a surface may be forced to perform a Tess_End due
to overflow.
==============
*/
// *INDENT-OFF*
void Tess_Begin( void ( *stageIteratorFunc )(),
                 shader_t *surfaceShader, shader_t *lightShader,
                 bool skipTangents,
                 int lightmapNum,
                 int fogNum,
                 bool bspSurface )
{
	if ( tess.numIndexes || tess.numVertexes || tess.multiDrawPrimitives )
	{
		Log::Warn( "Tess_Begin: unflushed data: numVertexes=%d numIndexes=%d multiDrawPrimitives=%d",
		           tess.numVertexes, tess.numIndexes, tess.multiDrawPrimitives );
	}

	tess.numIndexes = 0;
	tess.numVertexes = 0;
	tess.multiDrawPrimitives = 0;

	tess.stageIteratorFunc = stageIteratorFunc;

	tess.surfaceShader = surfaceShader;
	tess.lightShader = lightShader;

	tess.skipTangents = skipTangents;
	tess.lightmapNum = lightmapNum;
	tess.fogNum = fogNum;
	tess.bspSurface = bspSurface;

	// materials are optional (some debug drawing code doesn't use them)
	if ( tess.surfaceShader )
	{
		if ( tess.surfaceShader->remappedShader )
		{
			tess.surfaceShader = surfaceShader->remappedShader;
		}

		if ( tess.surfaceShader->isSky )
		{
			tess.stageIteratorFunc = &Tess_StageIteratorSky;
		}

		tess.surfaceStages = tess.surfaceShader->stages;
		tess.surfaceLastStage = tess.surfaceShader->lastStage;
	}
	else
	{
		tess.surfaceStages = nullptr;
		tess.surfaceLastStage = nullptr;
	}

	Tess_MapVBOs( tess.surfaceShader && tess.surfaceShader->autoSpriteMode != 0 );

	if ( !tess.stageIteratorFunc )
	{
		Sys::Error( "tess.stageIteratorFunc == NULL" );
	}

	if ( r_logFile->integer )
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va( "--- Tess_Begin( surfaceShader = %s, lightShader = %s, skipTangents = %i, lightmapNum = %i, fogNum = %i) ---\n", tess.surfaceShader->name, tess.lightShader ? tess.lightShader->name : nullptr, tess.skipTangents, tess.lightmapNum, tess.fogNum ) );
	}
}

void SetNormalScale( const shaderStage_t *pStage, vec3_t normalScale )
{
	float normalIntensity = RB_EvalExpression( &pStage->normalIntensityExp, 1.0 );

	// Normal intensity is only applied on X and Y.
	normalScale[ 0 ] = pStage->normalScale[ 0 ] * normalIntensity;
	normalScale[ 1 ] = pStage->normalScale[ 1 ] * normalIntensity;

	/* The GLSL code disables normal map scaling when normal Z scale
	is equal to zero. It means normal map scaling is disabled when
	r_normalScale is set to zero. This is cool enough to be kept as
	a feature. Normal Z component equal to zero would be wrong anyway.
	r_normalScale is only applied on Z. */
	normalScale[ 2 ] = pStage->normalScale[ 2 ] * r_normalScale->value;
}

// *INDENT-ON*

void Render_NONE( shaderStage_t * )
{
	ASSERT_UNREACHABLE();
}

void Render_NOP( shaderStage_t * )
{
}

void Render_generic3D( shaderStage_t *pStage )
{
	GLimp_LogComment( "--- Render_generic3D ---\n" );

	GL_State( pStage->stateBits );

	bool hasDepthFade = pStage->hasDepthFade;
	bool needDepthMap = pStage->hasDepthFade;

	// choose right shader program ----------------------------------
	gl_genericShader->SetVertexSkinning( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning );
	gl_genericShader->SetVertexAnimation( tess.vboVertexAnimation );
	gl_genericShader->SetTCGenEnvironment( pStage->tcGen_Environment );
	gl_genericShader->SetTCGenLightmap( pStage->tcGen_Lightmap );
	gl_genericShader->SetDepthFade( hasDepthFade );
	gl_genericShader->BindProgram( pStage->deformIndex );
	// end choose right shader program ------------------------------

	// set uniforms
	if ( pStage->tcGen_Environment )
	{
		// calculate the environment texcoords in object space
		gl_genericShader->SetUniform_ViewOrigin( backEnd.orientation.viewOrigin );
		gl_genericShader->SetUniform_ViewUp( backEnd.orientation.axis[ 2 ] );
	}

	// u_AlphaThreshold
	gl_genericShader->SetUniform_AlphaTest( pStage->stateBits );

	// u_ColorModulate
	colorGen_t rgbGen = SetRgbGen( pStage );
	alphaGen_t alphaGen = SetAlphaGen( pStage );

	// Here, it's safe to multiply the overbright factor for vertex lighting into the color gen`
	// since the `generic` fragment shader only takes a single input color. `lightMapping` on the
	// hand needs to know the real diffuse color, hence the separate u_LightFactor.
	bool mayUseVertexOverbright = pStage->type == stageType_t::ST_COLORMAP && tess.bspSurface;
	const bool styleLightMap = pStage->type == stageType_t::ST_STYLELIGHTMAP || pStage->type == stageType_t::ST_STYLECOLORMAP;
	gl_genericShader->SetUniform_ColorModulateColorGen( rgbGen, alphaGen, mayUseVertexOverbright, styleLightMap );

	// u_Color
	gl_genericShader->SetUniform_Color( tess.svars.color );

	gl_genericShader->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
	gl_genericShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

	// u_Bones
	if ( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning )
	{
		gl_genericShader->SetUniform_Bones( tess.numBones, tess.bones );
	}

	// u_VertexInterpolation
	if ( tess.vboVertexAnimation )
	{
		gl_genericShader->SetUniform_VertexInterpolation( glState.vertexAttribsInterpolation );
	}

	// u_DeformGen
	gl_genericShader->SetUniform_Time( backEnd.refdef.floatTime - backEnd.currentEntity->e.shaderTime );

	// bind u_ColorMap
	if ( pStage->type == stageType_t::ST_STYLELIGHTMAP )
	{
		gl_genericShader->SetUniform_ColorMapBindless(
			GL_BindToTMU( 0, GetLightMap( &tess ) )
		);
	}
	else
	{
		gl_genericShader->SetUniform_ColorMapBindless( BindAnimatedImage( 0, &pStage->bundle[TB_COLORMAP] ) );
	}

	gl_genericShader->SetUniform_TextureMatrix( tess.svars.texMatrices[ TB_COLORMAP ] );

	if ( hasDepthFade )
	{
		gl_genericShader->SetUniform_DepthScale( pStage->depthFadeValue );
	}

	if ( needDepthMap )
	{
		gl_genericShader->SetUniform_DepthMapBindless(
			GL_BindToTMU( 1, tr.currentDepthImage )
		);
	}

	if ( r_profilerRenderSubGroups.Get() && !( pStage->stateBits & GLS_DEPTHMASK_TRUE ) && !tr.skipSubgroupProfiler ) {
		const uint mode = GetShaderProfilerRenderSubGroupsMode( pStage->stateBits );
		if( mode == 0 ) {
			return;
		}

		GL_State( pStage->stateBits & ~( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) );

		gl_genericShader->SetUniform_ProfilerZero();
		gl_genericShader->SetUniform_ProfilerRenderSubGroups( mode );
	}

	gl_genericShader->SetRequiredVertexPointers();

	Tess_DrawElements();

	GL_CheckErrors();
}

void Render_generic( shaderStage_t *pStage )
{
	if ( backEnd.projection2D )
	{
		constexpr uint32_t lockBits = GLS_DEPTHMASK_TRUE | GLS_DEPTHTEST_DISABLE;
		glState.glStateBitsMask = ~lockBits;
		GL_State( GLS_DEPTHTEST_DISABLE );
		glState.glStateBitsMask = lockBits;
		tr.skipSubgroupProfiler = true;

		Render_generic3D( pStage );

		glState.glStateBitsMask = 0;
		tr.skipSubgroupProfiler = false;
		return;
	}

	Render_generic3D( pStage );
}

void Render_lightMapping( shaderStage_t *pStage )
{
	GLimp_LogComment( "--- Render_lightMapping ---\n" );

	lightMode_t lightMode;
	deluxeMode_t deluxeMode;
	SetLightDeluxeMode( &tess, pStage->type, lightMode, deluxeMode );

	// u_Map, u_DeluxeMap
	image_t *lightmap = SetLightMap( &tess, lightMode );
	image_t *deluxemap = SetDeluxeMap( &tess, deluxeMode );

	// u_ColorModulate
	colorGen_t rgbGen = SetRgbGen( pStage );
	alphaGen_t alphaGen = SetAlphaGen( pStage );

	SetVertexLightingSettings( lightMode, rgbGen );

	uint32_t stateBits = pStage->stateBits;

	if ( lightMode == lightMode_t::MAP && r_showLightMaps->integer )
	{
		stateBits &= ~( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_ATEST_BITS );
	}

	bool enableDeluxeMapping = ( deluxeMode == deluxeMode_t::MAP );
	bool enableGridLighting = ( lightMode == lightMode_t::GRID );
	bool enableGridDeluxeMapping = ( deluxeMode == deluxeMode_t::GRID );

	DAEMON_ASSERT( !( enableDeluxeMapping && enableGridDeluxeMapping ) );

	// Not implemented yet in PBR code.
	bool enableReflectiveSpecular =
		pStage->enableSpecularMapping && glConfig2.reflectionMapping
		&& !( tr.refdef.rdflags & RDF_NOCUBEMAP );

	GL_State( stateBits );

	// choose right shader program ----------------------------------

	gl_lightMappingShader->SetVertexSkinning( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning );

	gl_lightMappingShader->SetVertexAnimation( tess.vboVertexAnimation );

	gl_lightMappingShader->SetBspSurface( tess.bspSurface );

	gl_lightMappingShader->SetDeluxeMapping( enableDeluxeMapping );

	gl_lightMappingShader->SetGridLighting( enableGridLighting );

	gl_lightMappingShader->SetGridDeluxeMapping( enableGridDeluxeMapping );

	gl_lightMappingShader->SetHeightMapInNormalMap( pStage->hasHeightMapInNormalMap );

	gl_lightMappingShader->SetReliefMapping( pStage->enableReliefMapping );

	gl_lightMappingShader->SetReflectiveSpecular( enableReflectiveSpecular );

	gl_lightMappingShader->SetPhysicalShading( pStage->enablePhysicalMapping );

	gl_lightMappingShader->BindProgram( pStage->deformIndex );
	// end choose right shader program ------------------------------

	// now we are ready to set the shader program uniforms
	vec3_t viewOrigin;

	if ( tess.bspSurface )
	{
		VectorCopy( backEnd.orientation.viewOrigin, viewOrigin ); // in world space
	}
	else
	{
		VectorCopy( backEnd.viewParms.orientation.origin, viewOrigin ); // in world space

		if ( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning )
		{
			gl_lightMappingShader->SetUniform_Bones( tess.numBones, tess.bones );
		}

		// u_VertexInterpolation
		if ( tess.vboVertexAnimation )
		{
			gl_lightMappingShader->SetUniform_VertexInterpolation( glState.vertexAttribsInterpolation );
		}

		gl_lightMappingShader->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
	}

	// u_ViewOrigin
	gl_lightMappingShader->SetUniform_ViewOrigin( viewOrigin );

	gl_lightMappingShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

	if ( glConfig2.realtimeLighting &&
	     r_realtimeLightingRenderer.Get() == Util::ordinal( realtimeLightingRenderer_t::TILED ) )
	{
		gl_lightMappingShader->SetUniform_numLights( tr.refdef.numLights );

		if ( backEnd.refdef.numShaderLights > 0 )
		{
			gl_lightMappingShader->SetUniformBlock_Lights( tr.dlightUBO );

			// bind u_LightTiles
			gl_lightMappingShader->SetUniform_LightTilesBindless(
				GL_BindToTMU( BIND_LIGHTTILES, tr.lighttileRenderImage )
			);
		}
	}

	// u_DeformGen
	gl_lightMappingShader->SetUniform_Time( backEnd.refdef.floatTime - backEnd.currentEntity->e.shaderTime );

	// u_ColorModulate
	gl_lightMappingShader->SetUniform_ColorModulateColorGen( rgbGen, alphaGen, false, lightMode != lightMode_t::FULLBRIGHT );

	// u_Color
	gl_lightMappingShader->SetUniform_Color( tess.svars.color );

	// u_AlphaThreshold
	gl_lightMappingShader->SetUniform_AlphaTest( pStage->stateBits );

	// bind u_HeightMap
	if ( pStage->enableReliefMapping )
	{
		float depthScale = RB_EvalExpression( &pStage->depthScaleExp, r_reliefDepthScale->value );
		depthScale *= tess.surfaceShader->reliefDepthScale;

		gl_lightMappingShader->SetUniform_ReliefDepthScale( depthScale );
		gl_lightMappingShader->SetUniform_ReliefOffsetBias( tess.surfaceShader->reliefOffsetBias );

		// FIXME: if there is both, embedded heightmap in normalmap is used instead of standalone heightmap
		if ( !pStage->hasHeightMapInNormalMap )
		{
			gl_lightMappingShader->SetUniform_HeightMapBindless(
				GL_BindToTMU( BIND_HEIGHTMAP, pStage->bundle[TB_HEIGHTMAP].image[0] )
			);
		}
	}

	// bind u_DiffuseMap
	gl_lightMappingShader->SetUniform_DiffuseMapBindless(
		GL_BindToTMU( BIND_DIFFUSEMAP, pStage->bundle[TB_DIFFUSEMAP].image[0] )
	);

	if ( pStage->type != stageType_t::ST_LIGHTMAP )
	{
		gl_lightMappingShader->SetUniform_TextureMatrix( tess.svars.texMatrices[ TB_DIFFUSEMAP ] );
	}

	// bind u_NormalMap
	if ( !!r_normalMapping->integer || pStage->hasHeightMapInNormalMap )
	{
		gl_lightMappingShader->SetUniform_NormalMapBindless(
			GL_BindToTMU( BIND_NORMALMAP, pStage->bundle[TB_NORMALMAP].image[0] )
		);
	}

	// bind u_NormalScale
	if ( pStage->enableNormalMapping )
	{
		vec3_t normalScale;
		SetNormalScale( pStage, normalScale );

		gl_lightMappingShader->SetUniform_NormalScale( normalScale );
	}

	// bind u_MaterialMap
	if ( pStage->enableSpecularMapping || pStage->enablePhysicalMapping )
	{
		gl_lightMappingShader->SetUniform_MaterialMapBindless(
			GL_BindToTMU( BIND_MATERIALMAP, pStage->bundle[TB_MATERIALMAP].image[0] )
		);
	}

	if ( pStage->enableSpecularMapping )
	{
		float specExpMin = RB_EvalExpression( &pStage->specularExponentMin, r_specularExponentMin->value );
		float specExpMax = RB_EvalExpression( &pStage->specularExponentMax, r_specularExponentMax->value );

		gl_lightMappingShader->SetUniform_SpecularExponent( specExpMin, specExpMax );
	}

	if ( enableReflectiveSpecular )
	{
		bool isWorldEntity = backEnd.currentEntity == &tr.worldEntity;

		vec3_t position;
		if ( backEnd.currentEntity && !isWorldEntity )
		{
			VectorCopy( backEnd.currentEntity->e.origin, position );
		}
		else
		{
			// FIXME position
			VectorCopy( backEnd.viewParms.orientation.origin, position );
		}

		cubemapProbe_t* probes[2];
		vec4_t trilerp;
		// TODO: Add a code path that would assign a cubemap to each tile for the tiled renderer
		R_GetNearestCubeMaps( position, probes, trilerp, 2 );
		const cubemapProbe_t* cubeProbeNearest = probes[0];
		const cubemapProbe_t* cubeProbeSecondNearest = probes[1];

		const float interpolation = 1.0 - trilerp[0];

		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "Probe 0 distance = %f, probe 1 distance = %f, interpolation = %f\n",
					Distance( position, probes[0]->origin ), Distance( position, probes[1]->origin ), interpolation ) );
		}

		// bind u_EnvironmentMap0
		gl_lightMappingShader->SetUniform_EnvironmentMap0Bindless(
			GL_BindToTMU( BIND_ENVIRONMENTMAP0, cubeProbeNearest->cubemap )
		);

		// bind u_EnvironmentMap1
		gl_lightMappingShader->SetUniform_EnvironmentMap1Bindless(
			GL_BindToTMU( BIND_ENVIRONMENTMAP1, cubeProbeSecondNearest->cubemap )
		);

		// bind u_EnvironmentInterpolation
		gl_lightMappingShader->SetUniform_EnvironmentInterpolation( interpolation );
	}

	// bind u_LightGridOrigin and u_LightGridScale to compute light grid position
	if ( enableGridLighting || enableGridDeluxeMapping )
	{
		if( tr.world )
		{
			gl_lightMappingShader->SetUniform_LightGridOrigin( tr.world->lightGridGLOrigin );
			gl_lightMappingShader->SetUniform_LightGridScale( tr.world->lightGridGLScale );
		}
		// FIXME: else
	}

	// bind u_LightMap
	if ( !enableGridLighting ) {
		gl_lightMappingShader->SetUniform_LightMapBindless(
			GL_BindToTMU( BIND_LIGHTMAP, lightmap )
		);
	} else {
		gl_lightMappingShader->SetUniform_LightGrid1Bindless( GL_BindToTMU( BIND_LIGHTMAP, lightmap ) );
	}

	// bind u_DeluxeMap
	if ( !enableGridDeluxeMapping ) {
		gl_lightMappingShader->SetUniform_DeluxeMapBindless(
			GL_BindToTMU( BIND_DELUXEMAP, deluxemap )
		);
	} else {
		gl_lightMappingShader->SetUniform_LightGrid2Bindless( GL_BindToTMU( BIND_DELUXEMAP, deluxemap ) );
	}

	// bind u_GlowMap
	if ( !!r_glowMapping->integer )
	{
		gl_lightMappingShader->SetUniform_GlowMapBindless(
			GL_BindToTMU( BIND_GLOWMAP, pStage->bundle[TB_GLOWMAP].image[0] )
		);
	}

	if ( r_profilerRenderSubGroups.Get() && !( pStage->stateBits & GLS_DEPTHMASK_TRUE ) ) {
		const uint mode = GetShaderProfilerRenderSubGroupsMode( stateBits );
		if ( mode == 0 ) {
			return;
		}

		GL_State( stateBits & ~( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) );

		gl_lightMappingShader->SetUniform_ProfilerZero();
		gl_lightMappingShader->SetUniform_ProfilerRenderSubGroups( mode );
	}

	gl_lightMappingShader->SetRequiredVertexPointers();

	Tess_DrawElements();

	GL_CheckErrors();
}

static void Render_shadowFill( shaderStage_t *pStage )
{
	uint32_t      stateBits;

	GLimp_LogComment( "--- Render_shadowFill ---\n" );

	// remove blend modes
	stateBits = pStage->stateBits;
	stateBits &= ~( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );

	GL_State( stateBits );

	gl_shadowFillShader->SetVertexSkinning( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning );
	gl_shadowFillShader->SetVertexAnimation( glState.vertexAttribsInterpolation > 0 );

	gl_shadowFillShader->SetMacro_LIGHT_DIRECTIONAL( backEnd.currentLight->l.rlType == refLightType_t::RL_DIRECTIONAL );

	gl_shadowFillShader->BindProgram( pStage->deformIndex );

	gl_shadowFillShader->SetRequiredVertexPointers();

	if ( r_debugShadowMaps->integer )
	{
		gl_shadowFillShader->SetUniform_Color( Color::Color::Indexed( backEnd.pc.c_batches % 8 ) );
	}

	// u_AlphaThreshold
	gl_shadowFillShader->SetUniform_AlphaTest( pStage->stateBits );

	if ( backEnd.currentLight->l.rlType != refLightType_t::RL_DIRECTIONAL )
	{
		gl_shadowFillShader->SetUniform_LightOrigin( backEnd.currentLight->origin );
		gl_shadowFillShader->SetUniform_LightRadius( backEnd.currentLight->sphereRadius );
	}

	gl_shadowFillShader->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
	gl_shadowFillShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

	// u_Bones
	if ( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning )
	{
		gl_shadowFillShader->SetUniform_Bones( tess.numBones, tess.bones );
	}

	// u_VertexInterpolation
	if ( tess.vboVertexAnimation )
	{
		gl_shadowFillShader->SetUniform_VertexInterpolation( glState.vertexAttribsInterpolation );
	}

	// u_DeformGen
	gl_shadowFillShader->SetUniform_Time( backEnd.refdef.floatTime - backEnd.currentEntity->e.shaderTime );

	// bind u_ColorMap
	if ( ( pStage->stateBits & GLS_ATEST_BITS ) != 0 )
	{
		gl_shadowFillShader->SetUniform_ColorMapBindless(
			GL_BindToTMU( 0, pStage->bundle[TB_COLORMAP].image[0] )
		);
		gl_shadowFillShader->SetUniform_TextureMatrix( tess.svars.texMatrices[ TB_COLORMAP ] );
	}
	else
	{
		gl_shadowFillShader->SetUniform_ColorMapBindless(
			GL_BindToTMU( 0, tr.whiteImage )
		);
	}

	Tess_DrawElements();

	GL_CheckErrors();
}

static void Render_forwardLighting_DBS_omni( shaderStage_t *pStage,
    shaderStage_t *attenuationXYStage,
    shaderStage_t *attenuationZStage, trRefLight_t *light )
{
	vec3_t     viewOrigin;
	vec3_t     lightOrigin;
	float      shadowTexelSize;

	GLimp_LogComment( "--- Render_forwardLighting_DBS_omni ---\n" );

	bool shadowCompare = ( glConfig2.shadowMapping && !light->l.noShadows && light->shadowLOD >= 0 );

	// choose right shader program ----------------------------------
	gl_forwardLightingShader_omniXYZ->SetVertexSkinning( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning );
	gl_forwardLightingShader_omniXYZ->SetVertexAnimation( glState.vertexAttribsInterpolation > 0 );

	gl_forwardLightingShader_omniXYZ->SetHeightMapInNormalMap( pStage->hasHeightMapInNormalMap );

	gl_forwardLightingShader_omniXYZ->SetReliefMapping( pStage->enableReliefMapping );

	gl_forwardLightingShader_omniXYZ->SetShadowing( shadowCompare );

	gl_forwardLightingShader_omniXYZ->BindProgram( pStage->deformIndex );
	// end choose right shader program ------------------------------

	// now we are ready to set the shader program uniforms

	// u_ColorModulate
	colorGen_t rgbGen = SetRgbGen( pStage );
	alphaGen_t alphaGen = SetAlphaGen( pStage );

	gl_forwardLightingShader_omniXYZ->SetUniform_ColorModulateColorGen( rgbGen, alphaGen );

	// u_Color
	gl_forwardLightingShader_omniXYZ->SetUniform_Color( tess.svars.color );

	// u_AlphaThreshold
	gl_forwardLightingShader_omniXYZ->SetUniform_AlphaTest( pStage->stateBits );

	// bind u_HeightMap
	if ( pStage->enableReliefMapping )
	{
		float depthScale = RB_EvalExpression( &pStage->depthScaleExp, r_reliefDepthScale->value );
		depthScale *= tess.surfaceShader->reliefDepthScale;

		gl_forwardLightingShader_omniXYZ->SetUniform_ReliefDepthScale( depthScale );
		gl_forwardLightingShader_omniXYZ->SetUniform_ReliefOffsetBias( tess.surfaceShader->reliefOffsetBias );

		// FIXME: if there is both, embedded heightmap in normalmap is used instead of standalone heightmap
		if ( !pStage->hasHeightMapInNormalMap )
		{
			gl_forwardLightingShader_omniXYZ->SetUniform_HeightMapBindless(
				GL_BindToTMU( 15, pStage->bundle[TB_HEIGHTMAP].image[0] ) 
			);
		}
	}

	// set uniforms
	VectorCopy( backEnd.viewParms.orientation.origin, viewOrigin );
	VectorCopy( light->origin, lightOrigin );
	Color::Color lightColor = tess.svars.color;

	if ( shadowCompare )
	{
		shadowTexelSize = 1.0f / shadowMapResolutions[ light->shadowLOD ];
	}
	else
	{
		shadowTexelSize = 1.0f;
	}

	gl_forwardLightingShader_omniXYZ->SetUniform_ViewOrigin( viewOrigin );

	gl_forwardLightingShader_omniXYZ->SetUniform_LightOrigin( lightOrigin );
	gl_forwardLightingShader_omniXYZ->SetUniform_LightColor( lightColor.ToArray() );
	gl_forwardLightingShader_omniXYZ->SetUniform_LightRadius( light->sphereRadius );
	gl_forwardLightingShader_omniXYZ->SetUniform_LightScale( light->l.scale );
	gl_forwardLightingShader_omniXYZ->SetUniform_LightAttenuationMatrix( light->attenuationMatrix2 );

	GL_CheckErrors();

	if ( shadowCompare )
	{
		gl_forwardLightingShader_omniXYZ->SetUniform_ShadowTexelSize( shadowTexelSize );
		gl_forwardLightingShader_omniXYZ->SetUniform_ShadowBlur( r_shadowBlur->value );
	}

	GL_CheckErrors();

	gl_forwardLightingShader_omniXYZ->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
	gl_forwardLightingShader_omniXYZ->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

	// u_Bones
	if ( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning )
	{
		gl_forwardLightingShader_omniXYZ->SetUniform_Bones( tess.numBones, tess.bones );
	}

	// u_VertexInterpolation
	if ( tess.vboVertexAnimation )
	{
		gl_forwardLightingShader_omniXYZ->SetUniform_VertexInterpolation( glState.vertexAttribsInterpolation );
	}

	gl_forwardLightingShader_omniXYZ->SetUniform_Time( backEnd.refdef.floatTime - backEnd.currentEntity->e.shaderTime );

	GL_CheckErrors();

	// bind u_DiffuseMap
	gl_forwardLightingShader_omniXYZ->SetUniform_DiffuseMapBindless(
		GL_BindToTMU( 0, pStage->bundle[TB_DIFFUSEMAP].image[0] )
	);

	if ( pStage->type != stageType_t::ST_LIGHTMAP )
	{
		gl_forwardLightingShader_omniXYZ->SetUniform_TextureMatrix( tess.svars.texMatrices[ TB_DIFFUSEMAP ] );
	}

	// bind u_NormalMap
	gl_forwardLightingShader_omniXYZ->SetUniform_NormalMapBindless(
		GL_BindToTMU( 1, pStage->bundle[TB_NORMALMAP].image[0] )
	);

	// bind u_NormalScale
	if ( pStage->enableNormalMapping )
	{
		vec3_t normalScale;
		SetNormalScale( pStage, normalScale );

		gl_forwardLightingShader_omniXYZ->SetUniform_NormalScale( normalScale );
	}

	// FIXME: physical mapping is not implemented.
	if ( pStage->enableSpecularMapping )
	{
		float minSpec = RB_EvalExpression( &pStage->specularExponentMin, r_specularExponentMin->value );
		float maxSpec = RB_EvalExpression( &pStage->specularExponentMax, r_specularExponentMax->value );

		gl_forwardLightingShader_omniXYZ->SetUniform_SpecularExponent( minSpec, maxSpec );

		// bind u_MaterialMap
		gl_forwardLightingShader_omniXYZ->SetUniform_MaterialMapBindless(
			GL_BindToTMU( 2, pStage->bundle[TB_MATERIALMAP].image[0] )
		);
	}

	// bind u_AttenuationMapXY
	gl_forwardLightingShader_omniXYZ->SetUniform_AttenuationMapXYBindless(
		BindAnimatedImage( 3, &attenuationXYStage->bundle[TB_COLORMAP]) );

	// bind u_AttenuationMapZ
	gl_forwardLightingShader_omniXYZ->SetUniform_AttenuationMapZBindless(
		BindAnimatedImage( 4, &attenuationZStage->bundle[TB_COLORMAP] ) );

	// bind u_ShadowMap
	if ( shadowCompare )
	{
		gl_forwardLightingShader_omniXYZ->SetUniform_ShadowMapBindless(
			GL_BindToTMU( 5, tr.shadowCubeFBOImage[light->shadowLOD] ) );
		gl_forwardLightingShader_omniXYZ->SetUniform_ShadowClipMapBindless(
			GL_BindToTMU( 7, tr.shadowClipCubeFBOImage[light->shadowLOD] ) );
	}

	// bind u_RandomMap
	gl_forwardLightingShader_omniXYZ->SetUniform_RandomMapBindless(
		GL_BindToTMU( 6, tr.randomNormalsImage )
	);

	gl_forwardLightingShader_omniXYZ->SetRequiredVertexPointers();

	Tess_DrawElements();

	GL_CheckErrors();
}

static void Render_forwardLighting_DBS_proj( shaderStage_t *pStage,
    shaderStage_t *attenuationXYStage,
    shaderStage_t *attenuationZStage, trRefLight_t *light )
{
	vec3_t     viewOrigin;
	vec3_t     lightOrigin;
	float      shadowTexelSize;

	GLimp_LogComment( "--- Render_forwardLighting_DBS_proj ---\n" );

	bool shadowCompare = ( glConfig2.shadowMapping && !light->l.noShadows && light->shadowLOD >= 0 );

	// choose right shader program ----------------------------------
	gl_forwardLightingShader_projXYZ->SetVertexSkinning( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning );
	gl_forwardLightingShader_projXYZ->SetVertexAnimation( glState.vertexAttribsInterpolation > 0 );

	gl_forwardLightingShader_projXYZ->SetHeightMapInNormalMap( pStage->hasHeightMapInNormalMap );

	gl_forwardLightingShader_projXYZ->SetReliefMapping( pStage->enableReliefMapping );

	gl_forwardLightingShader_projXYZ->SetShadowing( shadowCompare );

	gl_forwardLightingShader_projXYZ->BindProgram( pStage->deformIndex );
	// end choose right shader program ------------------------------

	// now we are ready to set the shader program uniforms

	// u_ColorModulate
	colorGen_t rgbGen = SetRgbGen( pStage );
	alphaGen_t alphaGen = SetAlphaGen( pStage );

	gl_forwardLightingShader_projXYZ->SetUniform_ColorModulateColorGen( rgbGen, alphaGen );

	// u_Color
	gl_forwardLightingShader_projXYZ->SetUniform_Color( tess.svars.color );

	// u_AlphaThreshold
	gl_forwardLightingShader_projXYZ->SetUniform_AlphaTest( pStage->stateBits );

	// bind u_HeightMap
	if ( pStage->enableReliefMapping )
	{
		float depthScale = RB_EvalExpression( &pStage->depthScaleExp, r_reliefDepthScale->value );
		depthScale *= tess.surfaceShader->reliefDepthScale;

		gl_forwardLightingShader_projXYZ->SetUniform_ReliefDepthScale( depthScale );
		gl_forwardLightingShader_projXYZ->SetUniform_ReliefOffsetBias( tess.surfaceShader->reliefOffsetBias );

		// FIXME: if there is both, embedded heightmap in normalmap is used instead of standalone heightmap
		if ( !pStage->hasHeightMapInNormalMap )
		{
			gl_forwardLightingShader_projXYZ->SetUniform_HeightMapBindless(
				GL_BindToTMU( 15, pStage->bundle[TB_HEIGHTMAP].image[0] )
			);
		}
	}

	// set uniforms
	VectorCopy( backEnd.viewParms.orientation.origin, viewOrigin );
	VectorCopy( light->origin, lightOrigin );
	Color::Color lightColor = tess.svars.color;

	if ( shadowCompare )
	{
		shadowTexelSize = 1.0f / shadowMapResolutions[ light->shadowLOD ];
	}
	else
	{
		shadowTexelSize = 1.0f;
	}

	gl_forwardLightingShader_projXYZ->SetUniform_ViewOrigin( viewOrigin );

	gl_forwardLightingShader_projXYZ->SetUniform_LightOrigin( lightOrigin );
	gl_forwardLightingShader_projXYZ->SetUniform_LightColor( lightColor.ToArray() );
	gl_forwardLightingShader_projXYZ->SetUniform_LightRadius( light->sphereRadius );
	gl_forwardLightingShader_projXYZ->SetUniform_LightScale( light->l.scale );
	gl_forwardLightingShader_projXYZ->SetUniform_LightAttenuationMatrix( light->attenuationMatrix2 );

	GL_CheckErrors();

	if ( shadowCompare )
	{
		gl_forwardLightingShader_projXYZ->SetUniform_ShadowTexelSize( shadowTexelSize );
		gl_forwardLightingShader_projXYZ->SetUniform_ShadowBlur( r_shadowBlur->value );
		gl_forwardLightingShader_projXYZ->SetUniform_ShadowMatrix( light->shadowMatrices );
	}

	GL_CheckErrors();

	gl_forwardLightingShader_projXYZ->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
	gl_forwardLightingShader_projXYZ->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

	// u_Bones
	if ( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning )
	{
		gl_forwardLightingShader_projXYZ->SetUniform_Bones( tess.numBones, tess.bones );
	}

	// u_VertexInterpolation
	if ( tess.vboVertexAnimation )
	{
		gl_forwardLightingShader_projXYZ->SetUniform_VertexInterpolation( glState.vertexAttribsInterpolation );
	}

	gl_forwardLightingShader_projXYZ->SetUniform_Time( backEnd.refdef.floatTime - backEnd.currentEntity->e.shaderTime );

	GL_CheckErrors();

	// bind u_DiffuseMap
	gl_forwardLightingShader_projXYZ->SetUniform_DiffuseMapBindless(
		GL_BindToTMU( 0, pStage->bundle[TB_DIFFUSEMAP].image[0] )
	);

	if ( pStage->type != stageType_t::ST_LIGHTMAP )
	{
		gl_forwardLightingShader_projXYZ->SetUniform_TextureMatrix( tess.svars.texMatrices[ TB_DIFFUSEMAP ] );
	}

	// bind u_NormalMap
	gl_forwardLightingShader_projXYZ->SetUniform_NormalMapBindless(
		GL_BindToTMU( 1, pStage->bundle[TB_NORMALMAP].image[0] )
	);

	// bind u_NormalScale
	if ( pStage->enableNormalMapping )
	{
		vec3_t normalScale;
		SetNormalScale( pStage, normalScale );

		gl_forwardLightingShader_projXYZ->SetUniform_NormalScale( normalScale );
	}

	// bind u_MaterialMap
	gl_forwardLightingShader_projXYZ->SetUniform_MaterialMapBindless(
		GL_BindToTMU( 2, pStage->bundle[TB_MATERIALMAP].image[0] )
	);

	// FIXME: physical mapping is not implemented.
	if ( pStage->enableSpecularMapping )
	{
		float minSpec = RB_EvalExpression( &pStage->specularExponentMin, r_specularExponentMin->value );
		float maxSpec = RB_EvalExpression( &pStage->specularExponentMax, r_specularExponentMax->value );

		gl_forwardLightingShader_projXYZ->SetUniform_SpecularExponent( minSpec, maxSpec );
	}

	// bind u_AttenuationMapXY
	gl_forwardLightingShader_projXYZ->SetUniform_AttenuationMapXYBindless(
		BindAnimatedImage( 3, &attenuationXYStage->bundle[TB_COLORMAP] ) );

	// bind u_AttenuationMapZ
	gl_forwardLightingShader_projXYZ->SetUniform_AttenuationMapZBindless(
		BindAnimatedImage( 4, &attenuationZStage->bundle[TB_COLORMAP] ) );

	// bind u_ShadowMap
	if ( shadowCompare )
	{
		gl_forwardLightingShader_projXYZ->SetUniform_ShadowMap0Bindless(
			GL_BindToTMU( 5, tr.shadowMapFBOImage[light->shadowLOD] )
		);
		gl_forwardLightingShader_projXYZ->SetUniform_ShadowClipMap0Bindless(
			GL_BindToTMU( 7, tr.shadowClipMapFBOImage[light->shadowLOD] )
		);
	}

	// bind u_RandomMap
	gl_forwardLightingShader_projXYZ->SetUniform_RandomMapBindless(
		GL_BindToTMU( 6, tr.randomNormalsImage )
	);

	gl_forwardLightingShader_projXYZ->SetRequiredVertexPointers();

	Tess_DrawElements();

	GL_CheckErrors();
}

static void Render_forwardLighting_DBS_directional( shaderStage_t *pStage, trRefLight_t *light )
{
	vec3_t     viewOrigin;
	vec3_t     lightDirection;
	float      shadowTexelSize;

	GLimp_LogComment( "--- Render_forwardLighting_DBS_directional ---\n" );

	bool shadowCompare = ( glConfig2.shadowMapping && !light->l.noShadows && light->shadowLOD >= 0 );

	// choose right shader program ----------------------------------
	gl_forwardLightingShader_directionalSun->SetVertexSkinning( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning );
	gl_forwardLightingShader_directionalSun->SetVertexAnimation( glState.vertexAttribsInterpolation > 0 );

	gl_forwardLightingShader_directionalSun->SetHeightMapInNormalMap( pStage->hasHeightMapInNormalMap );

	gl_forwardLightingShader_directionalSun->SetReliefMapping( pStage->enableReliefMapping );

	gl_forwardLightingShader_directionalSun->SetShadowing( shadowCompare );

	gl_forwardLightingShader_directionalSun->BindProgram( pStage->deformIndex );
	// end choose right shader program ------------------------------

	// now we are ready to set the shader program uniforms

	// u_ColorModulate
	colorGen_t rgbGen = SetRgbGen( pStage );
	alphaGen_t alphaGen = SetAlphaGen( pStage );

	gl_forwardLightingShader_directionalSun->SetUniform_ColorModulateColorGen( rgbGen, alphaGen );

	// u_Color
	gl_forwardLightingShader_directionalSun->SetUniform_Color( tess.svars.color );

	// u_AlphaThreshold
	gl_forwardLightingShader_directionalSun->SetUniform_AlphaTest( pStage->stateBits );

	// bind u_HeightMap
	if ( pStage->enableReliefMapping )
	{
		float depthScale = RB_EvalExpression( &pStage->depthScaleExp, r_reliefDepthScale->value );
		depthScale *= tess.surfaceShader->reliefDepthScale;

		gl_forwardLightingShader_directionalSun->SetUniform_ReliefDepthScale( depthScale );
		gl_forwardLightingShader_directionalSun->SetUniform_ReliefOffsetBias( tess.surfaceShader->reliefOffsetBias );

		// FIXME: if there is both, embedded heightmap in normalmap is used instead of standalone heightmap
		if ( !pStage->hasHeightMapInNormalMap )
		{
			gl_forwardLightingShader_directionalSun->SetUniform_HeightMapBindless(
				GL_BindToTMU( 15, pStage->bundle[TB_HEIGHTMAP].image[0] )
			);
		}
	}

	// set uniforms
	VectorCopy( backEnd.viewParms.orientation.origin, viewOrigin );

	VectorCopy( tr.sunDirection, lightDirection );

	Color::Color lightColor = tess.svars.color;

	if ( shadowCompare )
	{
		shadowTexelSize = 1.0f / sunShadowMapResolutions[ light->shadowLOD ];
	}
	else
	{
		shadowTexelSize = 1.0f;
	}

	gl_forwardLightingShader_directionalSun->SetUniform_ViewOrigin( viewOrigin );

	gl_forwardLightingShader_directionalSun->SetUniform_LightDir( lightDirection );
	gl_forwardLightingShader_directionalSun->SetUniform_LightColor( lightColor.ToArray() );
	gl_forwardLightingShader_directionalSun->SetUniform_LightRadius( light->sphereRadius );
	gl_forwardLightingShader_directionalSun->SetUniform_LightScale( light->l.scale );
	gl_forwardLightingShader_directionalSun->SetUniform_LightAttenuationMatrix( light->attenuationMatrix2 );

	GL_CheckErrors();

	if ( shadowCompare )
	{
		gl_forwardLightingShader_directionalSun->SetUniform_ShadowMatrix( light->shadowMatricesBiased );
		gl_forwardLightingShader_directionalSun->SetUniform_ShadowParallelSplitDistances( backEnd.viewParms.parallelSplitDistances );
		gl_forwardLightingShader_directionalSun->SetUniform_ShadowTexelSize( shadowTexelSize );
		gl_forwardLightingShader_directionalSun->SetUniform_ShadowBlur( r_shadowBlur->value );
	}

	GL_CheckErrors();

	gl_forwardLightingShader_directionalSun->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
	gl_forwardLightingShader_directionalSun->SetUniform_ViewMatrix( backEnd.viewParms.world.viewMatrix );
	gl_forwardLightingShader_directionalSun->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

	// u_Bones
	if ( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning )
	{
		gl_forwardLightingShader_directionalSun->SetUniform_Bones( tess.numBones, tess.bones );
	}

	// u_VertexInterpolation
	if ( tess.vboVertexAnimation )
	{
		gl_forwardLightingShader_directionalSun->SetUniform_VertexInterpolation( glState.vertexAttribsInterpolation );
	}

	gl_forwardLightingShader_directionalSun->SetUniform_Time( backEnd.refdef.floatTime - backEnd.currentEntity->e.shaderTime );

	GL_CheckErrors();

	// bind u_DiffuseMap
	gl_forwardLightingShader_directionalSun->SetUniform_DiffuseMapBindless(
		GL_BindToTMU( 0, pStage->bundle[TB_DIFFUSEMAP].image[0] )
	);

	if ( pStage->type != stageType_t::ST_LIGHTMAP )
	{
		gl_forwardLightingShader_directionalSun->SetUniform_TextureMatrix( tess.svars.texMatrices[ TB_DIFFUSEMAP ] );
	}

	// bind u_NormalMap
	gl_forwardLightingShader_directionalSun->SetUniform_NormalMapBindless(
		GL_BindToTMU( 1, pStage->bundle[TB_NORMALMAP].image[0] )
	);

	// bind u_NormalScale
	if ( pStage->enableNormalMapping )
	{
		vec3_t normalScale;
		SetNormalScale( pStage, normalScale );

		gl_forwardLightingShader_directionalSun->SetUniform_NormalScale( normalScale );
	}

	// bind u_MaterialMap
	gl_forwardLightingShader_directionalSun->SetUniform_MaterialMapBindless(
		GL_BindToTMU( 2, pStage->bundle[TB_MATERIALMAP].image[0] )
	);

	// FIXME: physical mapping is not implemented.
	if ( pStage->enableSpecularMapping )
	{
		float minSpec = RB_EvalExpression( &pStage->specularExponentMin, r_specularExponentMin->value );
		float maxSpec = RB_EvalExpression( &pStage->specularExponentMax, r_specularExponentMax->value );
		gl_forwardLightingShader_directionalSun->SetUniform_SpecularExponent( minSpec, maxSpec );
	}

	// bind u_ShadowMap
	if ( shadowCompare )
	{
		gl_forwardLightingShader_directionalSun->SetUniform_ShadowMap0Bindless(
			GL_BindToTMU( 5, tr.sunShadowMapFBOImage[ 0 ] )
		);
		gl_forwardLightingShader_directionalSun->SetUniform_ShadowClipMap0Bindless(
			GL_BindToTMU( 10, tr.sunShadowClipMapFBOImage[ 0 ] )
		);

		if ( r_parallelShadowSplits->integer >= 1 )
		{
			gl_forwardLightingShader_directionalSun->SetUniform_ShadowMap1Bindless(
				GL_BindToTMU( 6, tr.sunShadowMapFBOImage[ 1 ] )
			);
			gl_forwardLightingShader_directionalSun->SetUniform_ShadowClipMap1Bindless(
				GL_BindToTMU( 11, tr.sunShadowClipMapFBOImage[ 1 ] )
			);
		}

		if ( r_parallelShadowSplits->integer >= 2 )
		{
			gl_forwardLightingShader_directionalSun->SetUniform_ShadowMap2Bindless(
				GL_BindToTMU( 7, tr.sunShadowMapFBOImage[ 2 ] )
			);
			gl_forwardLightingShader_directionalSun->SetUniform_ShadowClipMap2Bindless(
				GL_BindToTMU( 12, tr.sunShadowClipMapFBOImage[ 2 ] )
			);
		}

		if ( r_parallelShadowSplits->integer >= 3 )
		{
			gl_forwardLightingShader_directionalSun->SetUniform_ShadowMap3Bindless(
				GL_BindToTMU( 8, tr.sunShadowMapFBOImage[ 3 ] )
			);
			gl_forwardLightingShader_directionalSun->SetUniform_ShadowClipMap3Bindless(
				GL_BindToTMU( 13, tr.sunShadowClipMapFBOImage[ 3 ] )
			);
		}

		if ( r_parallelShadowSplits->integer >= 4 )
		{
			gl_forwardLightingShader_directionalSun->SetUniform_ShadowMap4Bindless(
				GL_BindToTMU( 9, tr.sunShadowMapFBOImage[ 4 ] )
			);
			gl_forwardLightingShader_directionalSun->SetUniform_ShadowClipMap4Bindless(
				GL_BindToTMU( 14, tr.sunShadowClipMapFBOImage[ 4 ] )
			);
		}
	}

	gl_forwardLightingShader_directionalSun->SetRequiredVertexPointers();

	Tess_DrawElements();

	GL_CheckErrors();
}

void Render_reflection_CB( shaderStage_t *pStage )
{
	GLimp_LogComment( "--- Render_reflection_CB ---\n" );

	GL_State( pStage->stateBits );

	// choose right shader program ----------------------------------
	gl_reflectionShader->SetHeightMapInNormalMap( pStage->hasHeightMapInNormalMap );

	gl_reflectionShader->SetReliefMapping( pStage->enableReliefMapping );

	gl_reflectionShader->SetVertexSkinning( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning );
	gl_reflectionShader->SetVertexAnimation( glState.vertexAttribsInterpolation > 0 );

	gl_reflectionShader->BindProgram( pStage->deformIndex );
	// end choose right shader program ------------------------------

	gl_reflectionShader->SetUniform_ViewOrigin( backEnd.viewParms.orientation.origin );  // in world space

	gl_reflectionShader->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
	gl_reflectionShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

	// u_Bones
	if ( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning )
	{
		gl_reflectionShader->SetUniform_Bones( tess.numBones, tess.bones );
	}

	// u_VertexInterpolation
	if ( tess.vboVertexAnimation )
	{
		gl_reflectionShader->SetUniform_VertexInterpolation( glState.vertexAttribsInterpolation );
	}

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

	gl_reflectionShader->SetUniform_ColorMapCubeBindless(
		GL_BindToTMU( 0, probes[0]->cubemap )
	);

	// bind u_NormalMap
	gl_reflectionShader->SetUniform_NormalMapBindless(
		GL_BindToTMU( 1, pStage->bundle[TB_NORMALMAP].image[0] )
	);

	// bind u_NormalScale
	if ( pStage->enableNormalMapping )
	{
		vec3_t normalScale;
		SetNormalScale( pStage, normalScale );

		gl_reflectionShader->SetUniform_NormalScale( normalScale );
	}

	gl_reflectionShader->SetUniform_TextureMatrix( tess.svars.texMatrices[ TB_NORMALMAP ] );

	// bind u_HeightMap u_depthScale u_reliefOffsetBias
	if ( pStage->enableReliefMapping )
	{
		float depthScale = RB_EvalExpression( &pStage->depthScaleExp, r_reliefDepthScale->value );
		depthScale *= tess.surfaceShader->reliefDepthScale;

		gl_reflectionShader->SetUniform_ReliefDepthScale( depthScale );
		gl_reflectionShader->SetUniform_ReliefOffsetBias( tess.surfaceShader->reliefOffsetBias );

		// FIXME: if there is both, embedded heightmap in normalmap is used instead of standalone heightmap
		if ( !pStage->hasHeightMapInNormalMap )
		{
			gl_reflectionShader->SetUniform_HeightMapBindless(
				GL_BindToTMU( 15, pStage->bundle[TB_HEIGHTMAP].image[0] )
			);
		}
	}

	gl_reflectionShader->SetRequiredVertexPointers();

	Tess_DrawElements();

	GL_CheckErrors();
}

void Render_skybox( shaderStage_t *pStage )
{
	GLimp_LogComment( "--- Render_skybox ---\n" );

	GL_State( pStage->stateBits );

	gl_skyboxShader->BindProgram( pStage->deformIndex );

	gl_skyboxShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

	// bind u_ColorMap
	gl_skyboxShader->SetUniform_ColorMapCubeBindless(
		GL_BindToTMU( 0, pStage->bundle[TB_COLORMAP].image[0] )
	);

	// u_AlphaThreshold
	gl_skyboxShader->SetUniform_AlphaTest( GLS_ATEST_NONE );

	gl_skyboxShader->SetRequiredVertexPointers();

	Tess_DrawElements();

	GL_CheckErrors();
}

void Render_screen( shaderStage_t *pStage )
{
	GLimp_LogComment( "--- Render_screen ---\n" );

	GL_State( pStage->stateBits );

	gl_screenShader->BindProgram( pStage->deformIndex );

	{
		GL_VertexAttribsState( ATTR_POSITION );
		glVertexAttrib4fv( ATTR_INDEX_COLOR, tess.svars.color.ToArray() );
	}

	gl_screenShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

	// bind u_CurrentMap
	gl_screenShader->SetUniform_CurrentMapBindless( BindAnimatedImage( 0, &pStage->bundle[TB_COLORMAP] ) );

	Tess_DrawElements();

	GL_CheckErrors();
}

/* This doesn't render the portal itself but the texture
blended to it to fade it with distance. */
void Render_portal( shaderStage_t *pStage )
{
	GLimp_LogComment( "--- Render_portal ---\n" );

	GL_State( pStage->stateBits );

	// enable shader, set arrays
	gl_portalShader->BindProgram( pStage->deformIndex );

	{
		GL_VertexAttribsState( ATTR_POSITION | ATTR_TEXCOORD );
		glVertexAttrib4fv( ATTR_INDEX_COLOR, tess.svars.color.ToArray() );
	}

	gl_portalShader->SetUniform_InversePortalRange( 1 / tess.surfaceShader->portalRange );

	gl_portalShader->SetUniform_ModelViewMatrix( glState.modelViewMatrix[ glState.stackIndex ] );
	gl_portalShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

	// bind u_CurrentMap
	gl_portalShader->SetUniform_CurrentMapBindless( BindAnimatedImage( 0, &pStage->bundle[TB_COLORMAP] ) );

	Tess_DrawElements();

	GL_CheckErrors();
}

void Render_heatHaze( shaderStage_t *pStage )
{
	uint32_t      stateBits;
	float         deformMagnitude;

	GLimp_LogComment( "--- Render_heatHaze ---\n" );

	// remove alpha test
	stateBits = pStage->stateBits;
	stateBits &= ~GLS_ATEST_BITS;
	stateBits &= ~GLS_DEPTHMASK_TRUE;

	GL_State( stateBits );

	// choose right shader program ----------------------------------
	gl_heatHazeShader->SetVertexSkinning( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning );
	gl_heatHazeShader->SetVertexAnimation( glState.vertexAttribsInterpolation > 0 );

	gl_heatHazeShader->BindProgram( pStage->deformIndex );
	// end choose right shader program ------------------------------

	// set uniforms

	deformMagnitude = RB_EvalExpression( &pStage->deformMagnitudeExp, 1.0 );
	gl_heatHazeShader->SetUniform_DeformMagnitude( deformMagnitude );

	gl_heatHazeShader->SetUniform_ModelViewMatrixTranspose( glState.modelViewMatrix[ glState.stackIndex ] );
	gl_heatHazeShader->SetUniform_ProjectionMatrixTranspose( glState.projectionMatrix[ glState.stackIndex ] );
	gl_heatHazeShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

	// u_Bones
	if ( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning )
	{
		gl_heatHazeShader->SetUniform_Bones( tess.numBones, tess.bones );
	}

	// u_VertexInterpolation
	if ( tess.vboVertexAnimation )
	{
		gl_heatHazeShader->SetUniform_VertexInterpolation( glState.vertexAttribsInterpolation );
	}

	// u_DeformGen
	if ( tess.surfaceShader->numDeforms )
	{
		gl_heatHazeShader->SetUniform_Time( backEnd.refdef.floatTime - backEnd.currentEntity->e.shaderTime );
	}

	// draw to background image
	R_BindFBO( tr.mainFBO[ 1 - backEnd.currentMainFBO ] );

	// bind u_NormalMap
	gl_heatHazeShader->SetUniform_NormalMapBindless(
		GL_BindToTMU( 0, pStage->bundle[TB_NORMALMAP].image[0] ) 
	);

	if ( pStage->enableNormalMapping )
	{
		gl_heatHazeShader->SetUniform_TextureMatrix( tess.svars.texMatrices[ TB_NORMALMAP ] );

		vec3_t normalScale;
		SetNormalScale( pStage, normalScale );

		// bind u_NormalScale
		gl_heatHazeShader->SetUniform_NormalScale( normalScale );
	}

	// bind u_CurrentMap
	gl_heatHazeShader->SetUniform_CurrentMapBindless(
		GL_BindToTMU( 1, tr.currentRenderImage[backEnd.currentMainFBO] ) 
	);

	gl_heatHazeShader->SetRequiredVertexPointers();

	Tess_DrawElements();

	// copy to foreground image
	R_BindFBO( tr.mainFBO[ backEnd.currentMainFBO ] );
	gl_heatHazeShader->SetUniform_CurrentMapBindless(
		GL_BindToTMU( 1, tr.currentRenderImage[1 - backEnd.currentMainFBO] ) 
	);
	gl_heatHazeShader->SetUniform_DeformMagnitude( 0.0f );
	Tess_DrawElements();

	GL_CheckErrors();
}

void Render_liquid( shaderStage_t *pStage )
{
	vec3_t        viewOrigin;
	float         fogDensity;
	vec3_t        fogColor;

	GLimp_LogComment( "--- Render_liquid ---\n" );

	// Tr3B: don't allow blend effects
	GL_State( pStage->stateBits & ~( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_DEPTHMASK_TRUE ) );

	lightMode_t lightMode;
	deluxeMode_t deluxeMode;
	SetLightDeluxeMode( &tess, pStage->type, lightMode, deluxeMode );

	// choose right shader program
	gl_liquidShader->SetHeightMapInNormalMap( pStage->hasHeightMapInNormalMap );

	gl_liquidShader->SetReliefMapping( pStage->enableReliefMapping );

	gl_liquidShader->SetGridDeluxeMapping( deluxeMode == deluxeMode_t::GRID );

	gl_liquidShader->SetGridLighting( lightMode == lightMode_t::GRID );

	// enable shader, set arrays
	gl_liquidShader->BindProgram( pStage->deformIndex );
	gl_liquidShader->SetRequiredVertexPointers();

	// set uniforms
	VectorCopy( backEnd.viewParms.orientation.origin, viewOrigin ); // in world space

	fogDensity = RB_EvalExpression( &pStage->fogDensityExp, 0.001 );
	VectorCopy( tess.svars.color.ToArray(), fogColor );

	gl_liquidShader->SetUniform_ViewOrigin( viewOrigin );
	gl_liquidShader->SetUniform_RefractionIndex( RB_EvalExpression( &pStage->refractionIndexExp, 1.0 ) );
	gl_liquidShader->SetUniform_FresnelPower( RB_EvalExpression( &pStage->fresnelPowerExp, 2.0 ) );
	gl_liquidShader->SetUniform_FresnelScale( RB_EvalExpression( &pStage->fresnelScaleExp, 1.0 ) );
	gl_liquidShader->SetUniform_FresnelBias( RB_EvalExpression( &pStage->fresnelBiasExp, 0.05 ) );
	gl_liquidShader->SetUniform_FogDensity( fogDensity );
	gl_liquidShader->SetUniform_FogColor( fogColor );

	gl_liquidShader->SetUniform_UnprojectMatrix( backEnd.viewParms.unprojectionMatrix );
	gl_liquidShader->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
	gl_liquidShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

	// NOTE: specular component is computed by shader.
	// FIXME: physical mapping is not implemented.
	if ( pStage->enableSpecularMapping )
	{
		float specMin = RB_EvalExpression( &pStage->specularExponentMin, r_specularExponentMin->value );
		float specMax = RB_EvalExpression( &pStage->specularExponentMax, r_specularExponentMax->value );
		gl_liquidShader->SetUniform_SpecularExponent( specMin, specMax );
	}

	// bind u_CurrentMap
	gl_liquidShader->SetUniform_CurrentMapBindless( GL_BindToTMU( 0, tr.currentRenderImage[backEnd.currentMainFBO] ) );

	// bind u_PortalMap
	gl_liquidShader->SetUniform_PortalMapBindless( GL_BindToTMU( 1, tr.portalRenderImage ) );

	// depth texture
	gl_liquidShader->SetUniform_DepthMapBindless( GL_BindToTMU( 2, tr.currentDepthImage ) );

	// bind u_HeightMap u_depthScale u_reliefOffsetBias
	if ( pStage->enableReliefMapping )
	{
		float depthScale = RB_EvalExpression( &pStage->depthScaleExp, r_reliefDepthScale->value );
		depthScale *= tess.surfaceShader->reliefDepthScale;

		gl_liquidShader->SetUniform_ReliefDepthScale( depthScale );
		gl_liquidShader->SetUniform_ReliefOffsetBias( tess.surfaceShader->reliefOffsetBias );

		// FIXME: if there is both, embedded heightmap in normalmap is used instead of standalone heightmap
		if ( !pStage->hasHeightMapInNormalMap )
		{
			gl_liquidShader->SetUniform_HeightMapBindless( GL_BindToTMU( 15, pStage->bundle[TB_HEIGHTMAP].image[0] ) );
		}
	}

	// bind u_NormalMap
	gl_liquidShader->SetUniform_NormalMapBindless( GL_BindToTMU( 3, pStage->bundle[TB_NORMALMAP].image[0] ) );

	// bind u_NormalScale
	if ( pStage->enableNormalMapping )
	{
		vec3_t normalScale;
		// FIXME: NormalIntensity default was 0.5
		SetNormalScale( pStage, normalScale );

		gl_liquidShader->SetUniform_NormalScale( normalScale );
	}

	gl_liquidShader->SetUniform_TextureMatrix( tess.svars.texMatrices[ TB_NORMALMAP ] );

	Tess_DrawElements();

	GL_CheckErrors();
}

void Render_fog( shaderStage_t* pStage )
{
	if ( r_noFog->integer || !r_wolfFog->integer || ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) )
	{
		return;
	}

	const fog_t* fog = tr.world->fogs + tess.fogNum;

	if ( r_logFile->integer )
	{
		GLimp_LogComment( va( "--- Render_fog( fogNum = %i, originalBrushNumber = %i ) ---\n", tess.fogNum, fog->originalBrushNumber ) );
	}

	// all fogging distance is based on world Z units
	vec4_t fogDistanceVector;
	vec3_t local;
	VectorSubtract( backEnd.orientation.origin, backEnd.viewParms.orientation.origin, local );
	fogDistanceVector[ 0 ] = -backEnd.orientation.modelViewMatrix[ 2 ];
	fogDistanceVector[ 1 ] = -backEnd.orientation.modelViewMatrix[ 6 ];
	fogDistanceVector[ 2 ] = -backEnd.orientation.modelViewMatrix[ 10 ];
	fogDistanceVector[ 3 ] = DotProduct( local, backEnd.viewParms.orientation.axis[ 0 ] );

	// scale the fog vectors based on the fog's thickness
	VectorScale( fogDistanceVector, fog->tcScale, fogDistanceVector );
	fogDistanceVector[3] *= fog->tcScale;

	// rotate the gradient vector for this orientation
	float eyeT;
	vec4_t fogDepthVector;
	if ( fog->hasSurface )
	{
		fogDepthVector[ 0 ] = fog->surface[ 0 ] * backEnd.orientation.axis[ 0 ][ 0 ] +
		                      fog->surface[ 1 ] * backEnd.orientation.axis[ 0 ][ 1 ] + fog->surface[ 2 ] * backEnd.orientation.axis[ 0 ][ 2 ];
		fogDepthVector[ 1 ] = fog->surface[ 0 ] * backEnd.orientation.axis[ 1 ][ 0 ] +
		                      fog->surface[ 1 ] * backEnd.orientation.axis[ 1 ][ 1 ] + fog->surface[ 2 ] * backEnd.orientation.axis[ 1 ][ 2 ];
		fogDepthVector[ 2 ] = fog->surface[ 0 ] * backEnd.orientation.axis[ 2 ][ 0 ] +
		                      fog->surface[ 1 ] * backEnd.orientation.axis[ 2 ][ 1 ] + fog->surface[ 2 ] * backEnd.orientation.axis[ 2 ][ 2 ];
		fogDepthVector[ 3 ] = -fog->surface[ 3 ] + DotProduct( backEnd.orientation.origin, fog->surface );

		eyeT = DotProduct( backEnd.orientation.viewOrigin, fogDepthVector ) + fogDepthVector[ 3 ];
	}
	else
	{
		Vector4Set( fogDepthVector, 0, 0, 0, 1 );
		eyeT = 1; // non-surface fog always has eye inside
	}

	// see if the viewpoint is outside
	// this is needed for clipping distance even for constant fog
	fogDistanceVector[ 3 ] += 1.0 / 512;

	GL_State( pStage->stateBits );

	gl_fogQuake3Shader->SetVertexSkinning( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning );
	gl_fogQuake3Shader->SetVertexAnimation( glState.vertexAttribsInterpolation > 0 );

	gl_fogQuake3Shader->BindProgram( 0 );

	gl_fogQuake3Shader->SetUniform_FogDistanceVector( fogDistanceVector );
	gl_fogQuake3Shader->SetUniform_FogDepthVector( fogDepthVector );
	gl_fogQuake3Shader->SetUniform_FogEyeT( eyeT );

	// u_Color
	gl_fogQuake3Shader->SetUniform_ColorGlobal( fog->color );

	gl_fogQuake3Shader->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
	gl_fogQuake3Shader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

	// u_Bones
	if ( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning )
	{
		gl_fogQuake3Shader->SetUniform_Bones( tess.numBones, tess.bones );
	}

	// u_VertexInterpolation
	if ( tess.vboVertexAnimation )
	{
		gl_fogQuake3Shader->SetUniform_VertexInterpolation( glState.vertexAttribsInterpolation );
	}

	gl_fogQuake3Shader->SetUniform_Time( backEnd.refdef.floatTime - backEnd.currentEntity->e.shaderTime );

	// bind u_ColorMap
	gl_fogQuake3Shader->SetUniform_FogMapBindless(
		GL_BindToTMU( 0, tr.fogImage ) 
	);

	gl_fogQuake3Shader->SetRequiredVertexPointers();

	Tess_DrawElements();

	GL_CheckErrors();
}

/*
===============
Tess_ComputeColor
===============
*/
void Tess_ComputeColor( shaderStage_t *pStage )
{
	float rgb;
	float red;
	float green;
	float blue;
	float alpha;

	GLimp_LogComment( "--- Tess_ComputeColor ---\n" );

	// rgbGen
	switch ( pStage->rgbGen )
	{
		case colorGen_t::CGEN_IDENTITY:
		case colorGen_t::CGEN_ONE_MINUS_VERTEX:
		default:
		case colorGen_t::CGEN_IDENTITY_LIGHTING:
			/* Historically CGEN_IDENTITY_LIGHTING was done this way:

			  tess.svars.color = Color::White * tr.identityLight;

			But tr.identityLight is always 1.0f in Dæmon engine
			as the as the overbright bit implementation is fully
			software. */
			{
				tess.svars.color = Color::White;
				break;
			}

		case colorGen_t::CGEN_VERTEX:
			{
				tess.svars.color = Color::Color();
				break;
			}

		case colorGen_t::CGEN_CONST:
			{
				tess.svars.color = pStage->constantColor;
				break;
			}

		case colorGen_t::CGEN_ENTITY:
			{
				if ( backEnd.currentLight )
				{
					tess.svars.color = Color::Adapt( backEnd.currentLight->l.color );
					tess.svars.color.Clamp();
				}
				else if ( backEnd.currentEntity )
				{
					tess.svars.color = backEnd.currentEntity->e.shaderRGBA;
					tess.svars.color.Clamp();
				}
				else
				{
					tess.svars.color = Color::White;
				}

				break;
			}

		case colorGen_t::CGEN_ONE_MINUS_ENTITY:
			{
				if ( backEnd.currentLight )
				{
					tess.svars.color.SetRed( 1.0 - Math::Clamp( backEnd.currentLight->l.color[ 0 ], 0.0f, 1.0f ) );
					tess.svars.color.SetGreen( 1.0 - Math::Clamp( backEnd.currentLight->l.color[ 1 ], 0.0f, 1.0f ) );
					tess.svars.color.SetBlue( 1.0 - Math::Clamp( backEnd.currentLight->l.color[ 2 ], 0.0f, 1.0f ) );
				}
				else if ( backEnd.currentEntity )
				{
					tess.svars.color = backEnd.currentEntity->e.shaderRGBA;
					tess.svars.color.Clamp();
				}
				else
				{
					tess.svars.color = Color::Color();
				}

				break;
			}

		case colorGen_t::CGEN_WAVEFORM:
			{
				float      glow;
				waveForm_t *wf;

				wf = &pStage->rgbWave;

				if ( wf->func == genFunc_t::GF_NOISE )
				{
					glow = wf->base + R_NoiseGet4f( 0, 0, 0, ( backEnd.refdef.floatTime + wf->phase ) * wf->frequency ) * wf->amplitude;
				}
				else
				{
					/* Historically this value was multiplied by
					tr.identityLight but tr.identityLight is always 1.0f
					in Dæmon engine as the as the overbright bit
					implementation is fully software. */

					glow = RB_EvalWaveForm( wf );
				}

				glow = Math::Clamp( glow, 0.0f, 1.0f );

				tess.svars.color = Color::White * glow;
				break;
			}

		case colorGen_t::CGEN_CUSTOM_RGB:
			{
				rgb = Math::Clamp( RB_EvalExpression( &pStage->rgbExp, 1.0 ), 0.0f, 1.0f );

				tess.svars.color = Color::White * rgb;
				break;
			}

		case colorGen_t::CGEN_CUSTOM_RGBs:
			{
				if ( backEnd.currentLight )
				{
					red = Math::Clamp( RB_EvalExpression( &pStage->redExp, backEnd.currentLight->l.color[ 0 ] ), 0.0f, 1.0f );
					green = Math::Clamp( RB_EvalExpression( &pStage->greenExp, backEnd.currentLight->l.color[ 1 ] ), 0.0f, 1.0f );
					blue = Math::Clamp( RB_EvalExpression( &pStage->blueExp, backEnd.currentLight->l.color[ 2 ] ), 0.0f, 1.0f );
				}
				else if ( backEnd.currentEntity )
				{
					red =
					  Math::Clamp( RB_EvalExpression( &pStage->redExp, backEnd.currentEntity->e.shaderRGBA.Red() * ( 1.0 / 255.0 ) ), 0.0f, 1.0f );
					green =
					  Math::Clamp( RB_EvalExpression( &pStage->greenExp, backEnd.currentEntity->e.shaderRGBA.Green() * ( 1.0 / 255.0 ) ), 0.0f, 1.0f );
					blue =
					  Math::Clamp( RB_EvalExpression( &pStage->blueExp, backEnd.currentEntity->e.shaderRGBA.Blue() * ( 1.0 / 255.0 ) ), 0.0f, 1.0f );
				}
				else
				{
					red = Math::Clamp( RB_EvalExpression( &pStage->redExp, 1.0 ), 0.0f, 1.0f );
					green = Math::Clamp( RB_EvalExpression( &pStage->greenExp, 1.0 ), 0.0f, 1.0f );
					blue = Math::Clamp( RB_EvalExpression( &pStage->blueExp, 1.0 ), 0.0f, 1.0f );
				}

				tess.svars.color.SetRed( red );
				tess.svars.color.SetGreen( green );
				tess.svars.color.SetBlue( blue );
				break;
			}
	}

	// alphaGen
	switch ( pStage->alphaGen )
	{
		default:
		case alphaGen_t::AGEN_PORTAL:
		case alphaGen_t::AGEN_IDENTITY:
		case alphaGen_t::AGEN_ONE_MINUS_VERTEX:
			{
				tess.svars.color.SetAlpha( 1.0 );

				break;
			}

		case alphaGen_t::AGEN_VERTEX:
			{
				tess.svars.color.SetAlpha( 0.0 );
				break;
			}

		case alphaGen_t::AGEN_CONST:
			{
				tess.svars.color.SetAlpha( pStage->constantColor.Alpha() * ( 1.0 / 255.0 ) );

				break;
			}

		case alphaGen_t::AGEN_ENTITY:
			{
				if ( backEnd.currentLight )
				{
					tess.svars.color.SetAlpha( 1.0 ); // FIXME ?
				}
				else if ( backEnd.currentEntity )
				{
					tess.svars.color.SetAlpha( Math::Clamp( backEnd.currentEntity->e.shaderRGBA.Alpha() * ( 1.0 / 255.0 ), 0.0, 1.0 ) );
				}
				else
				{
					tess.svars.color.SetAlpha( 1.0 );
				}

				break;
			}

		case alphaGen_t::AGEN_ONE_MINUS_ENTITY:
			{
				if ( backEnd.currentLight )
				{
					tess.svars.color.SetAlpha( 0.0 ); // FIXME ?
				}
				else if ( backEnd.currentEntity )
				{
					tess.svars.color.SetAlpha( 1.0 - Math::Clamp( backEnd.currentEntity->e.shaderRGBA.Alpha() * ( 1.0 / 255.0 ), 0.0, 1.0 ) );
				}
				else
				{
					tess.svars.color.SetAlpha( 0.0 );
				}

				break;
			}

		case alphaGen_t::AGEN_WAVEFORM:
			{
				float      glow;
				waveForm_t *wf;

				wf = &pStage->alphaWave;

				glow = RB_EvalWaveFormClamped( wf );

				tess.svars.color.SetAlpha( glow );
				break;
			}

		case alphaGen_t::AGEN_CUSTOM:
			{
				alpha = Math::Clamp( RB_EvalExpression( &pStage->alphaExp, 1.0 ), 0.0f, 1.0f );

				tess.svars.color.SetAlpha( alpha );
				break;
			}
	}
}

/*
===============
Tess_ComputeTexMatrices
===============
*/
void Tess_ComputeTexMatrices( shaderStage_t *pStage )
{
	GLimp_LogComment( "--- Tess_ComputeTexMatrices ---\n" );

	matrix_t *matrix = tess.svars.texMatrices;
	matrix_t *lastMatrix = matrix + MAX_TEXTURE_BUNDLES;

	textureBundle_t *bundle = pStage->bundle;

	for ( ; matrix < lastMatrix; matrix++, bundle++ )
	{
		RB_CalcTexMatrix( bundle, *matrix );
	}
}

// Used for things which are never intended to be rendered
// (or in the case of Tess_InstantQuad, they're rendered but not via Tess_End)
void Tess_StageIteratorDummy()
{
	Log::Warn( "non-drawing tessellation overflow" );
}

void Tess_StageIteratorDebug()
{
	// log this call
	if ( r_logFile->integer )
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va( "--- Tess_StageIteratorDebug( %i vertices, %i triangles ) ---\n", tess.numVertexes, tess.numIndexes / 3 ) );
	}

	GL_CheckErrors();

	if ( !glState.currentVBO || !glState.currentIBO || glState.currentVBO == tess.vbo || glState.currentIBO == tess.ibo )
	{
		Tess_UpdateVBOs( );

		// Just set all attribs that are used by any debug drawing (and that the current VBO supports)
		if ( glState.currentVBO )
		{
			GL_VertexAttribsState(
				glState.currentVBO->attribBits & ( ATTR_POSITION | ATTR_COLOR | ATTR_TEXCOORD ) );
		}
	}

	Tess_DrawElements();
}

void Tess_StageIteratorColor()
{
	// log this call
	if ( r_logFile->integer )
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va
		                  ( "--- Tess_StageIteratorColor( %s, %i vertices, %i triangles ) ---\n", tess.surfaceShader->name,
		                    tess.numVertexes, tess.numIndexes / 3 ) );
	}

	GL_CheckErrors();

	if ( tess.surfaceShader->autoSpriteMode != 0 )
	{
		Tess_AutospriteDeform( tess.surfaceShader->autoSpriteMode );
	}

	if ( !glState.currentVBO || !glState.currentIBO || glState.currentVBO == tess.vbo || glState.currentIBO == tess.ibo )
	{
		Tess_UpdateVBOs( );
	}

	// set face culling appropriately
	if( backEnd.currentEntity->e.renderfx & RF_SWAPCULL )
		GL_Cull( ReverseCull( tess.surfaceShader->cullType ) );
	else
		GL_Cull( tess.surfaceShader->cullType );

	// set polygon offset if necessary
	if ( tess.surfaceShader->polygonOffset )
	{
		glEnable( GL_POLYGON_OFFSET_FILL );
		GL_PolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

	// call shader function
	int stage = 0;
	for ( shaderStage_t *pStage = tess.surfaceStages; pStage < tess.surfaceLastStage; pStage++ )
	{
		if ( !RB_EvalExpression( &pStage->ifExp, 1.0 ) && !( materialSystem.generatingWorldCommandBuffer && pStage->useMaterialSystem ) )
		{
			continue;
		}

		if ( r_profilerRenderSubGroups.Get() && !backEnd.projection2D && !( pStage->stateBits & GLS_DEPTHMASK_TRUE ) ) {
			const int stageID = r_profilerRenderSubGroupsStage.Get();
			if( ( stageID != -1 ) && ( stageID != stage ) ) {
				stage++;
				continue;
			}
		}

		Tess_ComputeColor( pStage );
		Tess_ComputeTexMatrices( pStage );

		if ( materialSystem.generatingWorldCommandBuffer && pStage->useMaterialSystem ) {
			tess.currentSSBOOffset = pStage->materialOffset;
			tess.materialID = tess.currentDrawSurf->materialIDs[stage];
			tess.materialPackID = tess.currentDrawSurf->materialPackIDs[stage];
		}

		pStage->colorRenderer( pStage );

		stage++;
	}

	// reset polygon offset
	if ( tess.surfaceShader->polygonOffset )
	{
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}

void Tess_StageIteratorPortal() {
	// log this call
	if ( r_logFile->integer ) {
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va
		( "--- Tess_StageIteratorPortal( %s, %i vertices, %i triangles ) ---\n", tess.surfaceShader->name,
			tess.numVertexes, tess.numIndexes / 3 ) );
	}

	GL_CheckErrors();

	if ( tess.surfaceShader->autoSpriteMode != 0 )
	{
		Tess_AutospriteDeform( tess.surfaceShader->autoSpriteMode );
	}

	if ( !glState.currentVBO || !glState.currentIBO || glState.currentVBO == tess.vbo || glState.currentIBO == tess.ibo ) {
		Tess_UpdateVBOs();
	}

	// set face culling appropriately
	if ( backEnd.currentEntity->e.renderfx & RF_SWAPCULL )
		GL_Cull( ReverseCull( tess.surfaceShader->cullType ) );
	else
		GL_Cull( tess.surfaceShader->cullType );

	// call shader function
	for ( shaderStage_t *pStage = tess.surfaceStages; pStage < tess.surfaceLastStage; pStage++ )
	{
		if ( !RB_EvalExpression( &pStage->ifExp, 1.0 ) ) {
			continue;
		}

		Render_generic3D( pStage );
	}
}

void Tess_StageIteratorShadowFill()
{
	// log this call
	if ( r_logFile->integer )
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va
		                  ( "--- Tess_StageIteratorShadowFill( %s, %i vertices, %i triangles ) ---\n", tess.surfaceShader->name,
		                    tess.numVertexes, tess.numIndexes / 3 ) );
	}

	GL_CheckErrors();

	if ( tess.surfaceShader->autoSpriteMode != 0 )
	{
		Tess_AutospriteDeform( tess.surfaceShader->autoSpriteMode );
	}

	if ( !glState.currentVBO || !glState.currentIBO || glState.currentVBO == tess.vbo || glState.currentIBO == tess.ibo )
	{
		Tess_UpdateVBOs( );
	}

	// set face culling appropriately
	if( backEnd.currentEntity->e.renderfx & RF_SWAPCULL )
		GL_Cull( ReverseCull( tess.surfaceShader->cullType ) );
	else
		GL_Cull( tess.surfaceShader->cullType );

	// set polygon offset if necessary
	if ( tess.surfaceShader->polygonOffset )
	{
		glEnable( GL_POLYGON_OFFSET_FILL );
		GL_PolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

	// call shader function
	for ( shaderStage_t *pStage = tess.surfaceStages; pStage < tess.surfaceLastStage; pStage++ )
	{
		if ( !RB_EvalExpression( &pStage->ifExp, 1.0 ) )
		{
			continue;
		}

		Tess_ComputeTexMatrices( pStage );

		if ( pStage->doShadowFill )
		{
			Render_shadowFill( pStage );
		}
	}

	// reset polygon offset
	glDisable( GL_POLYGON_OFFSET_FILL );
}

void Tess_StageIteratorLighting()
{
	trRefLight_t  *light;

	// log this call
	if ( r_logFile->integer )
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va
		                  ( "--- Tess_StageIteratorLighting( %s, %s, %i vertices, %i triangles ) ---\n", tess.surfaceShader->name,
		                    tess.lightShader->name, tess.numVertexes, tess.numIndexes / 3 ) );
	}

	GL_CheckErrors();

	light = backEnd.currentLight;

	if ( tess.surfaceShader->autoSpriteMode != 0 )
	{
		Tess_AutospriteDeform( tess.surfaceShader->autoSpriteMode );
	}

	if ( !glState.currentVBO || !glState.currentIBO || glState.currentVBO == tess.vbo || glState.currentIBO == tess.ibo )
	{
		Tess_UpdateVBOs( );
	}

	// set OpenGL state for lighting
	if ( light->l.inverseShadows )
	{
		GL_State( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE_MINUS_SRC_COLOR );
	}
	else
	{
		if ( tess.surfaceShader->sort > Util::ordinal(shaderSort_t::SS_OPAQUE) )
		{
			GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
		}
		else
		{
			GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
		}
	}

	// set face culling appropriately
	if( backEnd.currentEntity->e.renderfx & RF_SWAPCULL )
		GL_Cull( ReverseCull( tess.surfaceShader->cullType ) );
	else
		GL_Cull( tess.surfaceShader->cullType );

	// set polygon offset if necessary
	if ( tess.surfaceShader->polygonOffset )
	{
		glEnable( GL_POLYGON_OFFSET_FILL );
		GL_PolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

	// call shader function
	shaderStage_t *attenuationZStage = &tess.lightShader->stages[ 0 ];

	for ( shaderStage_t *pStage = tess.surfaceStages; pStage < tess.surfaceLastStage; pStage++ )
	{
		if ( !RB_EvalExpression( &pStage->ifExp, 1.0 ) )
		{
			continue;
		}

		Tess_ComputeTexMatrices( pStage );

		for ( shaderStage_t *attenuationXYStage = tess.lightShader->stages;
			attenuationXYStage < tess.lightShader->lastStage;
			attenuationXYStage++ )
		{
			if ( attenuationXYStage->type != stageType_t::ST_ATTENUATIONMAP_XY )
			{
				continue;
			}

			if ( !RB_EvalExpression( &attenuationXYStage->ifExp, 1.0 ) )
			{
				continue;
			}

			Tess_ComputeColor( attenuationXYStage );
			R_ComputeFinalAttenuation( attenuationXYStage, light );

			if ( pStage->doForwardLighting )
			{
				switch( light->l.rlType )
				{
					case refLightType_t::RL_OMNI:
						Render_forwardLighting_DBS_omni( pStage, attenuationXYStage, attenuationZStage, light );
						break;
					case refLightType_t::RL_PROJ:
						Render_forwardLighting_DBS_proj( pStage, attenuationXYStage, attenuationZStage, light );
						break;
					case refLightType_t::RL_DIRECTIONAL:
						Render_forwardLighting_DBS_directional( pStage, light );
						break;
					default:
						ASSERT_UNREACHABLE();
						break;
				};
			}
		}
	}

	// reset polygon offset
	if ( tess.surfaceShader->polygonOffset )
	{
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
}

void Tess_Clear()
{
	tess.vboVertexSkinning = false;
	tess.vboVertexAnimation = false;

	// clear shader so we can tell we don't have any unclosed surfaces
	tess.multiDrawPrimitives = 0;
	tess.numIndexes = 0;
	tess.numVertexes = 0;

	// TODO: stop constantly binding VBOs we aren't going to use!
	bool usingMapBufferRange = ( !glConfig2.bufferStorageAvailable || !glConfig2.syncAvailable )
                               && glConfig2.mapBufferRangeAvailable;
	if ( tess.verts != nullptr && tess.verts != tess.vertsBuffer && usingMapBufferRange )
	{
		R_BindVBO( tess.vbo );
		glUnmapBuffer( GL_ARRAY_BUFFER );
		R_BindIBO( tess.ibo );
		glUnmapBuffer( GL_ELEMENT_ARRAY_BUFFER );
	}

	// This is important after doing CPU-only tessellation with Tess_MapVBOs( true ).
	// A lot of code relies on a behavior of Tess_Begin: automatically map the
	// default VBO *if tess.verts is null*.
	tess.verts = nullptr;
	tess.indexes = nullptr;
}

/*
=================
Tess_End

Render tesselated data
=================
*/
void Tess_End()
{
	if ( ( tess.numIndexes == 0 || tess.numVertexes == 0 ) && tess.multiDrawPrimitives == 0 && !tr.drawingSky )
	{
		return;
	}

	// for debugging of sort order issues, stop rendering after a given sort value
	bool skip = r_debugSort->integer && tess.surfaceShader != nullptr
		&& r_debugSort->integer < tess.surfaceShader->sort;
	if ( !skip )
	{
		// update performance counter
		backEnd.pc.c_batches++;

		GL_CheckErrors();

		// call off to shader specific tess end function
		tess.stageIteratorFunc();

		if ( tess.stageIteratorFunc != Tess_StageIteratorShadowFill &&
		     tess.stageIteratorFunc != Tess_StageIteratorDebug &&
		     tess.stageIteratorFunc != Tess_StageIteratorDummy )
		{
			// draw debugging stuff
			if ( r_showTris->integer || r_showBatches->integer || ( r_showLightBatches->integer && ( tess.stageIteratorFunc == Tess_StageIteratorLighting ) ) )
			{
				// Skybox triangle rendering is done in Tess_StageIteratorSky()
				if ( tess.stageIteratorFunc != Tess_StageIteratorSky ) {
					DrawTris();
				}
			}
		}
	}

	Tess_Clear();

	GLimp_LogComment( "--- Tess_End ---\n" );

	GL_CheckErrors();
}
