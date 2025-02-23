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
// tr_init.c -- functions that are not called every frame
#include "tr_local.h"
#include "framework/CvarSystem.h"
#include "DetectGLVendors.h"
#include "Material.h"
#include "GeometryCache.h"

#ifdef _WIN32
	extern "C" {
		// Use dGPU if both dGPU and iGPU are available
		// https://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf
		__declspec( dllexport ) uint32_t NvOptimusEnablement = 0x00000001;
		// https://gpuopen.com/learn/amdpowerxpressrequesthighperformance/
		__declspec( dllexport ) uint32_t AmdPowerXpressRequestHighPerformance = 0x00000001;
	}
#endif

	glconfig_t  glConfig;
	glconfig2_t glConfig2;

	glstate_t   glState;

	static void GfxInfo_f();

	cvar_t      *r_glMajorVersion;
	cvar_t      *r_glMinorVersion;
	cvar_t      *r_glProfile;
	Cvar::Cvar<bool> r_glDebugProfile( "r_glDebugProfile", "Enable GL debug message callback", Cvar::NONE, false );
	Cvar::Range<Cvar::Cvar<int>> r_glDebugMode( "r_glDebugMode",
		"GL debug message callback mode: 0: none, 1: error, 2: deprecated, 3: undefined, 4: portability, 5: performance,"
		"6: other, 7: all", Cvar::NONE,
		Util::ordinal( glDebugModes_t::GLDEBUG_NONE ),
		Util::ordinal( glDebugModes_t::GLDEBUG_NONE ),
		Util::ordinal( glDebugModes_t::GLDEBUG_ALL ) );
	cvar_t      *r_glAllowSoftware;
	cvar_t      *r_glExtendedValidation;

	cvar_t      *r_ignore;

	cvar_t      *r_znear;
	cvar_t      *r_zfar;

	cvar_t      *r_smp;
	cvar_t      *r_showSmp;
	cvar_t      *r_skipBackEnd;

	cvar_t      *r_lodBias;
	cvar_t      *r_lodScale;

	cvar_t      *r_norefresh;
	cvar_t      *r_drawentities;
	cvar_t      *r_drawworld;
	cvar_t      *r_drawpolies;
	cvar_t      *r_speeds;
	cvar_t      *r_novis;
	cvar_t      *r_nocull;
	cvar_t      *r_facePlaneCull;
	cvar_t      *r_nocurves;
	cvar_t      *r_lightScissors;
	cvar_t      *r_noLightVisCull;
	cvar_t      *r_noInteractionSort;
	Cvar::Range<Cvar::Cvar<int>> r_realtimeLightingRenderer( "r_realtimeLightingRenderer",
		"renderer for real time lights: 0: legacy, 1: tiled", Cvar::NONE,
		Util::ordinal(realtimeLightingRenderer_t::TILED),
		Util::ordinal(realtimeLightingRenderer_t::LEGACY),
		Util::ordinal(realtimeLightingRenderer_t::TILED) );
	Cvar::Cvar<bool> r_realtimeLighting( "r_realtimeLighting", "Enable realtime light rendering", Cvar::NONE, true );
	Cvar::Range<Cvar::Cvar<int>> r_realtimeLightLayers( "r_realtimeLightLayers", "Dynamic light layers per tile, each layer holds 16 lights",
		Cvar::NONE, 4, 1, MAX_REF_LIGHTS / 16 );
	cvar_t      *r_realtimeLightingCastShadows;
	cvar_t      *r_precomputedLighting;
	Cvar::Cvar<int> r_overbrightDefaultExponent("r_overbrightDefaultExponent", "default map light color shift (multiply by 2^x)", Cvar::NONE, 2);
	Cvar::Cvar<bool> r_overbrightDefaultClamp("r_overbrightDefaultClamp", "clamp lightmap colors to 1 (in absence of map worldspawn value)", Cvar::NONE, false);
	Cvar::Cvar<bool> r_overbrightIgnoreMapSettings("r_overbrightIgnoreMapSettings", "force usage of r_overbrightDefaultClamp / r_overbrightDefaultExponent, ignoring worldspawn", Cvar::NONE, false);
	Cvar::Range<Cvar::Cvar<int>> r_lightMode("r_lightMode", "lighting mode: 0: fullbright (cheat), 1: vertex light, 2: grid light (cheat), 3: light map", Cvar::NONE, Util::ordinal(lightMode_t::MAP), Util::ordinal(lightMode_t::FULLBRIGHT), Util::ordinal(lightMode_t::MAP));
	Cvar::Cvar<bool> r_colorGrading( "r_colorGrading", "Use color grading", Cvar::NONE, true );
	Cvar::Cvar<bool> r_preferBindlessTextures( "r_preferBindlessTextures", "use bindless textures even when material system is off", Cvar::NONE, false );
	Cvar::Cvar<bool> r_materialSystem( "r_materialSystem", "Use Material System", Cvar::NONE, false );
	Cvar::Cvar<bool> r_gpuFrustumCulling( "r_gpuFrustumCulling", "Use frustum culling on the GPU for the Material System", Cvar::NONE, true );
	Cvar::Cvar<bool> r_gpuOcclusionCulling( "r_gpuOcclusionCulling", "Use occlusion culling on the GPU for the Material System", Cvar::NONE, false );
	Cvar::Cvar<bool> r_materialSystemSkip( "r_materialSystemSkip", "Temporarily skip Material System rendering, using only core renderer instead", Cvar::NONE, false );
	cvar_t      *r_lightStyles;
	cvar_t      *r_exportTextures;
	cvar_t      *r_heatHaze;
	cvar_t      *r_noMarksOnTrisurfs;

	/* Default value is 1: Delay GLSL shader build until first map load.

	This makes possible for the user to quickly reach the game main menu
	without building all GLSL shaders and set a low graphics preset before
	joining a game and loading a map.

	Doing so prevents building unwanted or unsupported GLSL shaders on slow
	and/or old hardware and drastically reduce first startup time. */
	Cvar::Range<Cvar::Cvar<int>> r_lazyShaders(
		"r_lazyShaders", "build GLSL shaders (0) on startup, (1) on map load or (2) when used",
		Cvar::NONE, 1, 0, 2);

	cvar_t      *r_checkGLErrors;
	cvar_t      *r_logFile;

	cvar_t      *r_colorbits;

	cvar_t      *r_drawBuffer;

	Cvar::Range<Cvar::Cvar<int>> r_shadows( "cg_shadows", "shadowing mode", Cvar::NONE,
		Util::ordinal(shadowingMode_t::SHADOWING_BLOB),
		Util::ordinal(shadowingMode_t::SHADOWING_NONE),
		Util::ordinal(shadowingMode_t::SHADOWING_EVSM32) );
	cvar_t      *r_softShadows;
	cvar_t      *r_softShadowsPP;
	cvar_t      *r_shadowBlur;

	cvar_t      *r_shadowMapSizeUltra;
	cvar_t      *r_shadowMapSizeVeryHigh;
	cvar_t      *r_shadowMapSizeHigh;
	cvar_t      *r_shadowMapSizeMedium;
	cvar_t      *r_shadowMapSizeLow;

	cvar_t      *r_shadowMapSizeSunUltra;
	cvar_t      *r_shadowMapSizeSunVeryHigh;
	cvar_t      *r_shadowMapSizeSunHigh;
	cvar_t      *r_shadowMapSizeSunMedium;
	cvar_t      *r_shadowMapSizeSunLow;

	cvar_t      *r_shadowLodBias;
	cvar_t      *r_shadowLodScale;
	cvar_t      *r_noShadowPyramids;
	cvar_t      *r_cullShadowPyramidFaces;
	cvar_t      *r_debugShadowMaps;
	cvar_t      *r_noLightFrustums;
	cvar_t      *r_shadowMapLinearFilter;
	cvar_t      *r_lightBleedReduction;
	cvar_t      *r_overDarkeningFactor;
	cvar_t      *r_shadowMapDepthScale;
	cvar_t      *r_parallelShadowSplits;
	cvar_t      *r_parallelShadowSplitWeight;

	cvar_t      *r_mode;
	cvar_t      *r_nobind;
	cvar_t      *r_singleShader;
	cvar_t      *r_picMip;
	cvar_t      *r_imageMaxDimension;
	cvar_t      *r_ignoreMaterialMinDimension;
	cvar_t      *r_ignoreMaterialMaxDimension;
	cvar_t      *r_replaceMaterialMinDimensionIfPresentWithMaxDimension;
	Cvar::Range<Cvar::Cvar<int>> r_imageFitScreen("r_imageFitScreen", "downscale “fitscreen” images to fit the screen size: 0: disable, 1: downscale as much as possible without being smaller than screen size (default), 2: downscale to never be larger then screen size", Cvar::NONE, 1, 0, 2);
	cvar_t      *r_finish;
	Cvar::Modified<Cvar::Cvar<std::string>> r_textureMode(
		"r_textureMode", "default texture filter mode", Cvar::NONE, "GL_LINEAR_MIPMAP_LINEAR");
	cvar_t      *r_offsetFactor;
	cvar_t      *r_offsetUnits;

	cvar_t      *r_physicalMapping;
	cvar_t      *r_specularExponentMin;
	cvar_t      *r_specularExponentMax;
	cvar_t      *r_specularScale;
	cvar_t      *r_specularMapping;
	cvar_t      *r_deluxeMapping;
	cvar_t      *r_normalScale;
	cvar_t      *r_normalMapping;
	cvar_t      *r_liquidMapping;
	cvar_t      *r_highQualityNormalMapping;
	cvar_t      *r_reliefDepthScale;
	cvar_t      *r_reliefMapping;
	cvar_t      *r_glowMapping;
	Cvar::Cvar<bool> r_reflectionMapping( "r_reflectionMapping", "Use static reflections", Cvar::NONE, false );
	Cvar::Range<Cvar::Cvar<int>> r_autoBuildCubeMaps( "r_autoBuildCubeMaps",
		"Automatically build cube maps when a map is loaded: 0: off, 1: build and store on disk if not found, 2: always build", Cvar::NONE,
		Util::ordinal( cubeProbesAutoBuildMode::ALWAYS ),
		Util::ordinal( cubeProbesAutoBuildMode::DISABLED ),
		Util::ordinal( cubeProbesAutoBuildMode::ALWAYS ) );
	Cvar::Range<Cvar::Cvar<int>> r_cubeProbeSize( "r_cubeProbeSize", "Size of the static reflections cubemaps", Cvar::NONE,
		32, 1, 32768 );
	Cvar::Range<Cvar::Cvar<int>> r_cubeProbeSpacing( "r_cubeProbeSpacing", "Spacing between the static reflections cubemaps", Cvar::NONE,
		256, 64, 1024 );

	cvar_t      *r_halfLambertLighting;
	cvar_t      *r_rimLighting;
	cvar_t      *r_rimExponent;

	Cvar::Cvar<bool> r_highPrecisionRendering("r_highPrecisionRendering", "use high precision frame buffers for rendering and blending", Cvar::NONE, true);

	cvar_t      *r_gamma;

	Cvar::Cvar<bool> r_tonemap( "r_tonemap", "Use HDR->LDR tonemapping", Cvar::NONE, true );
	Cvar::Cvar<bool> r_tonemapAdaptiveExposure( "r_tonemapAdaptiveExposure",
		"Use adaptive exposure based on scene brightness", Cvar::NONE, false );
	Cvar::Cvar<float> r_tonemapExposure( "r_tonemapExposure", "Tonemap exposure", Cvar::NONE, 1.0f );
	Cvar::Range<Cvar::Cvar<float>> r_tonemapContrast( "r_tonemapContrast", "Makes dark areas light up faster",
		Cvar::NONE, 1.6f, 1.0f, 10.0f );
	Cvar::Range<Cvar::Cvar<float>> r_tonemapHighlightsCompressionSpeed( "r_tonemapHighlightsCompressionSpeed",
		"Highlights saturation",
		Cvar::NONE, 0.977f, 0.0f, 10.0f );
	Cvar::Range<Cvar::Cvar<float>> r_tonemapHDRMax( "r_tonemapHDRMax", "HDR white point",
		Cvar::NONE, 8.0f, 1.0f, 128.0f );
	Cvar::Range<Cvar::Cvar<float>> r_tonemapDarkAreaPointHDR( "r_tonemapDarkAreaPointHDR",
		"Cut-off for dark area light-up",
		Cvar::NONE, 0.18f, 0.0f, 1.0f );
	Cvar::Range<Cvar::Cvar<float>> r_tonemapDarkAreaPointLDR( "r_tonemapDarkAreaPointLDR",
		"Convert to this brightness at dark area cut-off",
		Cvar::NONE, 0.268f, 0.0f, 1.0f );

	cvar_t      *r_lockpvs;
	cvar_t      *r_noportals;

	cvar_t      *r_max_portal_levels;

	cvar_t      *r_subdivisions;
	cvar_t      *r_stitchCurves;

	Cvar::Modified<Cvar::Cvar<bool>> r_fullscreen(
		"r_fullscreen", "use full-screen window", CVAR_ARCHIVE, true );

	cvar_t      *r_customwidth;
	cvar_t      *r_customheight;

	cvar_t      *r_debugSurface;

	cvar_t      *r_showImages;

	cvar_t      *r_wolfFog;
	cvar_t      *r_noFog;

	Cvar::Range<Cvar::Cvar<float>> r_forceAmbient( "r_forceAmbient", "Minimal light amount in lightGrid", Cvar::NONE,
		0.125f, 0.0f, 0.3f );
	Cvar::Cvar<float> r_ambientScale( "r_ambientScale", "Scale lightGrid produced by ambientColor keyword by this much", Cvar::CHEAT, 1.0 );
	cvar_t      *r_lightScale;
	cvar_t      *r_debugSort;
	cvar_t      *r_printShaders;

	cvar_t      *r_maxPolys;
	cvar_t      *r_maxPolyVerts;

	cvar_t      *r_showTris;
	cvar_t      *r_showSky;
	cvar_t      *r_showShadowLod;
	cvar_t      *r_showShadowMaps;
	cvar_t      *r_showSkeleton;
	cvar_t      *r_showEntityTransforms;
	cvar_t      *r_showLightTransforms;
	cvar_t      *r_showLightInteractions;
	cvar_t      *r_showLightScissors;
	cvar_t      *r_showLightBatches;
	cvar_t      *r_showLightGrid;
	cvar_t      *r_showLightTiles;
	cvar_t      *r_showBatches;
	Cvar::Cvar<bool> r_showVertexColors("r_showVertexColors", "show vertex colors used for vertex lighting", Cvar::CHEAT, false);
	cvar_t      *r_showLightMaps;
	cvar_t      *r_showDeluxeMaps;
	cvar_t      *r_showNormalMaps;
	cvar_t      *r_showMaterialMaps;
	Cvar::Cvar<bool> r_showReflectionMaps( "r_showReflectionMaps", "Show only the static reflections on surfaces", Cvar::NONE, false );
	Cvar::Range<Cvar::Cvar<int>> r_showCubeProbes( "r_showCubeProbes", "Show static reflections cubemap placement: 0: off, 1: grid, "
		"2: unique only", Cvar::NONE,
		Util::ordinal( showCubeProbesMode::DISABLED ),
		Util::ordinal( showCubeProbesMode::DISABLED ),
		Util::ordinal( showCubeProbesMode::UNIQUE ) );
	Cvar::Cvar<int> r_showCubeProbeFace( "r_showCubeProbeFace", "Render from the perspective of a selected static reflection "
		"cubemap face, -1 to disable", Cvar::NONE, -1 );
	cvar_t      *r_showBspNodes;
	Cvar::Range<Cvar::Cvar<int>> r_showGlobalMaterials( "r_showGlobalMaterials", "Show material system materials: 0: off, 1: depth, "
		"2: opaque, 3: opaque + transparent", Cvar::NONE,
		Util::ordinal( MaterialDebugMode::NONE ),
		Util::ordinal( MaterialDebugMode::NONE ),
		Util::ordinal( MaterialDebugMode::OPAQUE_TRANSPARENT ) );
	Cvar::Cvar<bool> r_materialDebug( "r_materialDebug", "Enable material debug SSBO", Cvar::NONE, false );
	cvar_t      *r_showParallelShadowSplits;

	Cvar::Cvar<bool> r_profilerRenderSubGroups( "r_profilerRenderSubGroups", "Enable subgroup profiling in rendering shaders", Cvar::CHEAT, false );
	Cvar::Range<Cvar::Cvar<int>> r_profilerRenderSubGroupsMode( "r_profilerRenderSubGroupsMode", "Red: more wasted lanes, green: less wasted lanes; "
		"0: VS opaque, 1: VS transparent, 2: VS all, 3: FS opaque, 4: FS transparent, 5: FS all", Cvar::NONE,
		Util::ordinal( shaderProfilerRenderSubGroupsMode::VS_OPAQUE ),
		Util::ordinal( shaderProfilerRenderSubGroupsMode::VS_OPAQUE ),
		Util::ordinal( shaderProfilerRenderSubGroupsMode::FS_ALL ) );
	Cvar::Cvar<int> r_profilerRenderSubGroupsStage( "r_profilerRenderSubGroupsStage", "Select a specific"
		"stage/material ID (if material system is enabled) for subgroup profiling "
		"(-1 to profile all stages/materials, rendered in their usual order); "
		"for materials, material IDs start from opaque materials, depth pre-pass materials are ignored", Cvar::NONE, -1 );

	Cvar::Cvar<int> r_forceRendererTime( "r_forceRendererTime", "Set a specific time (in ms, since the start of the map) for time-based shader effects; -1 to disable", Cvar::CHEAT, -1 );

	cvar_t      *r_vboFaces;
	cvar_t      *r_vboCurves;
	cvar_t      *r_vboTriangles;
	Cvar::Cvar<bool> r_vboModels( "r_vboModels", "Use static GPU VBOs/IBOs for models", Cvar::NONE, true );
	cvar_t      *r_vboVertexSkinning;

	cvar_t      *r_mergeLeafSurfaces;
	
	Cvar::Cvar<bool> r_bloom( "r_bloom", "Use bloom", Cvar::ARCHIVE, false );
	Cvar::Cvar<float> r_bloomBlur( "r_bloomBlur", "Bloom strength", Cvar::NONE, 1.0 );
	Cvar::Cvar<int> r_bloomPasses( "r_bloomPasses", "Amount of bloom passes in each direction", Cvar::NONE, 2 );
	cvar_t      *r_FXAA;
	cvar_t      *r_ssao;

	cvar_t      *r_evsmPostProcess;

	void AssertCvarRange( cvar_t *cv, float minVal, float maxVal, bool shouldBeIntegral )
	{
		if ( shouldBeIntegral )
		{
			if ( cv->value != static_cast<float>(cv->integer) )
			{
				Log::Warn("cvar '%s' must be integral (%f)", cv->name, cv->value );
				Cvar_Set( cv->name, va( "%d", cv->integer ) );
			}
		}

		if ( cv->value < minVal )
		{
			Log::Warn("cvar '%s' out of range (%g < %g)", cv->name, cv->value, minVal );
			Cvar_Set( cv->name, va( "%f", minVal ) );
		}
		else if ( cv->value > maxVal )
		{
			Log::Warn("cvar '%s' out of range (%g > %g)", cv->name, cv->value, maxVal );
			Cvar_Set( cv->name, va( "%f", maxVal ) );
		}
	}

	/*
	** InitOpenGL
	**
	** This function is responsible for initializing a valid OpenGL subsystem.  This
	** is done by calling GLimp_Init (which gives us a working OGL subsystem) then
	** setting variables, checking GL constants, and reporting the gfx system config
	** to the user.
	*/
	static bool InitOpenGL()
	{
		char renderer_buffer[ 1024 ];

		//
		// initialize OS specific portions of the renderer
		//
		// GLimp_Init directly or indirectly references the following cvars:
		//      - r_fullscreen
		//      - r_mode
		//      - r_(color|depth|stencil)bits
		//

		if ( glConfig.vidWidth == 0 )
		{
			if ( !GLimp_Init() )
			{
				return false;
			}

			glState.tileStep[ 0 ] = TILE_SIZE * ( 1.0f / glConfig.vidWidth );
			glState.tileStep[ 1 ] = TILE_SIZE * ( 1.0f / glConfig.vidHeight );

			GL_CheckErrors();

			strcpy( renderer_buffer, glConfig.renderer_string );
			Q_strlwr( renderer_buffer );

			// handle any OpenGL/GLSL brokenness here...
			// nothing at present

			GLSL_InitGPUShaders();
			glConfig.smpActive = false;

			if ( r_smp->integer )
			{
				Log::Notice("Trying SMP acceleration..." );

				if ( GLimp_SpawnRenderThread( RB_RenderThread ) )
				{
					Log::Notice("...succeeded." );
					glConfig.smpActive = true;
				}
				else
				{
					Log::Notice("...failed." );
				}
			}
		}
		else
		{
			GLSL_ShutdownGPUShaders();
			GLSL_InitGPUShaders();
		}

		if ( glConfig2.glCoreProfile ) {
			glGenVertexArrays( 1, &backEnd.currentVAO );
			glBindVertexArray( backEnd.currentVAO );
		}

		GL_CheckErrors();

		// set default state
		GL_SetDefaultState();
		GL_CheckErrors();

		return true;
	}

	/*
	==================
	GL_CheckErrors

	Must not be called while the backend rendering thread is running
	==================
	*/
	void GL_CheckErrors_( const char *fileName, int line )
	{
		int  err;
		char s[ 128 ];

		if ( !checkGLErrors() )
		{
			return;
		}

		while ( ( err = glGetError() ) != GL_NO_ERROR )
		{
			switch ( err )
			{
				case GL_INVALID_ENUM:
					strcpy( s, "GL_INVALID_ENUM" );
					break;

				case GL_INVALID_VALUE:
					strcpy( s, "GL_INVALID_VALUE" );
					break;

				case GL_INVALID_OPERATION:
					strcpy( s, "GL_INVALID_OPERATION" );
					break;

				case GL_STACK_OVERFLOW:
					strcpy( s, "GL_STACK_OVERFLOW" );
					break;

				case GL_STACK_UNDERFLOW:
					strcpy( s, "GL_STACK_UNDERFLOW" );
					break;

				case GL_OUT_OF_MEMORY:
					strcpy( s, "GL_OUT_OF_MEMORY" );
					break;

				case GL_TABLE_TOO_LARGE:
					strcpy( s, "GL_TABLE_TOO_LARGE" );
					break;

				case GL_INVALID_FRAMEBUFFER_OPERATION:
					strcpy( s, "GL_INVALID_FRAMEBUFFER_OPERATION" );
					break;

				default:
					Com_sprintf( s, sizeof( s ), "0x%X", err );
					break;
			}
			// Pre-format the string so that each callsite counts separately for log suppression
			std::string error = Str::Format("OpenGL error %s detected at %s:%d", s, fileName, line);
			Log::Warn(error);
		}
	}

	/*
	** R_GetModeInfo
	*/
	struct vidmode_t
	{
		const char *description;
		int        width, height;
		float      pixelAspect; // pixel width / height
	};

	static const vidmode_t r_vidModes[] =
	{
		{ " 320x240",           320,  240, 1 },
		{ " 400x300",           400,  300, 1 },
		{ " 512x384",           512,  384, 1 },
		{ " 640x480",           640,  480, 1 },
		{ " 800x600",           800,  600, 1 },
		{ " 960x720",           960,  720, 1 },
		{ "1024x768",          1024,  768, 1 },
		{ "1152x864",          1152,  864, 1 },
		{ "1280x720  (16:9)",  1280,  720, 1 },
		{ "1280x768  (16:10)", 1280,  768, 1 },
		{ "1280x800  (16:10)", 1280,  800, 1 },
		{ "1280x1024",         1280, 1024, 1 },
		{ "1360x768  (16:9)",  1360,  768,  1 },
		{ "1440x900  (16:10)", 1440,  900, 1 },
		{ "1680x1050 (16:10)", 1680, 1050, 1 },
		{ "1600x1200",         1600, 1200, 1 },
		{ "1920x1080 (16:9)",  1920, 1080, 1 },
		{ "1920x1200 (16:10)", 1920, 1200, 1 },
		{ "2048x1536",         2048, 1536, 1 },
		{ "2560x1600 (16:10)", 2560, 1600, 1 },
	};
	static const int s_numVidModes = ARRAY_LEN( r_vidModes );

	bool R_GetModeInfo( int *width, int *height, int mode )
	{
		const vidmode_t *vm;

		if ( mode < -2 )
		{
			return false;
		}

		if ( mode >= s_numVidModes )
		{
			return false;
		}

		if( mode == -2)
		{
			// Must set width and height to display size before calling this function!
		}
		else if ( mode == -1 )
		{
			*width = r_customwidth->integer;
			*height = r_customheight->integer;
		}
		else
		{
			vm = &r_vidModes[ mode ];

			*width = vm->width;
			*height = vm->height;
		}

		return true;
	}

	class ListModesCmd : public Cmd::StaticCmd
	{
	public:
		ListModesCmd() : StaticCmd("listModes", Cmd::RENDERER, "list suggested screen/window dimensions") {}
		void Run( const Cmd::Args& ) const override
		{
			int i;

			for ( i = 0; i < s_numVidModes; i++ )
			{
				Print("Mode %-2d: %s", i, r_vidModes[ i ].description );
			}
		}
	};
	static ListModesCmd listModesCmdRegistration;

	/*
	==================
	RB_ReadPixels

	Reads an image but takes care of alignment issues for reading RGB images.
	Prepends the specified number of (uninitialized) bytes to the buffer.

	The returned buffer must be freed with ri.Hunk_FreeTempMemory().
	==================
	*/
	static byte *RB_ReadPixels( int x, int y, int width, int height, size_t offset )
	{
		GLint packAlign;
		int   lineLen, paddedLineLen;
		byte  *buffer, *pixels;
		int   i;

		glGetIntegerv( GL_PACK_ALIGNMENT, &packAlign );

		lineLen = width * 3;
		paddedLineLen = PAD( lineLen, packAlign );

		// Allocate a few more bytes so that we can choose an alignment we like
		buffer = ( byte * ) ri.Hunk_AllocateTempMemory( offset + paddedLineLen * height + packAlign - 1 );

		pixels = ( byte * ) PADP( buffer + offset, packAlign );
		glReadPixels( x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels );

		// Drop alignment and line padding bytes
		for ( i = 0; i < height; ++i )
		{
			memmove( buffer + offset + i * lineLen, pixels + i * paddedLineLen, lineLen );
		}

		return buffer;
	}

	/*
	==================
	RB_TakeScreenshot
	==================
	*/
	static void RB_TakeScreenshotTGA( int x, int y, int width, int height, const char *fileName )
	{
		byte *buffer;
		int  dataSize;
		byte *end, *p;

		// with 18 bytes for the TGA file header
		buffer = RB_ReadPixels( x, y, width, height, 18 );
		memset( buffer, 0, 18 );

		buffer[ 2 ] = 2; // uncompressed type
		buffer[ 12 ] = width & 255;
		buffer[ 13 ] = width >> 8;
		buffer[ 14 ] = height & 255;
		buffer[ 15 ] = height >> 8;
		buffer[ 16 ] = 24; // pixel size

		dataSize = 3 * width * height;

		// swap RGB to BGR
		end = buffer + 18 + dataSize;

		for ( p = buffer + 18; p < end; p += 3 )
		{
			byte temp = p[ 0 ];
			p[ 0 ] = p[ 2 ];
			p[ 2 ] = temp;
		}

		ri.FS_WriteFile( fileName, buffer, 18 + dataSize );

		ri.Hunk_FreeTempMemory( buffer );
	}

	/*
	==================
	RB_TakeScreenshotJPEG
	==================
	*/
	static void RB_TakeScreenshotJPEG( int x, int y, int width, int height, const char *fileName )
	{
		byte *buffer = RB_ReadPixels( x, y, width, height, 0 );

		SaveJPG( fileName, 90, width, height, buffer );
		ri.Hunk_FreeTempMemory( buffer );
	}

	/*
	==================
	RB_TakeScreenshotPNG
	==================
	*/
	static void RB_TakeScreenshotPNG( int x, int y, int width, int height, const char *fileName )
	{
		byte *buffer = RB_ReadPixels( x, y, width, height, 0 );

		SavePNG( fileName, buffer, width, height, 3, false );
		ri.Hunk_FreeTempMemory( buffer );
	}

	/*
	==================
	RB_TakeScreenshotCmd
	==================
	*/
	const RenderCommand *ScreenshotCommand::ExecuteSelf( ) const
	{
		switch ( format )
		{
			case ssFormat_t::SSF_TGA:
				RB_TakeScreenshotTGA( x, y, width, height, fileName );
				break;

			case ssFormat_t::SSF_JPEG:
				RB_TakeScreenshotJPEG( x, y, width, height, fileName );
				break;

			case ssFormat_t::SSF_PNG:
				RB_TakeScreenshotPNG( x, y, width, height, fileName );
				break;
		}

		return this + 1;
	}

	/*
	==================
	R_TakeScreenshot
	==================
	*/
	static bool R_TakeScreenshot( Str::StringRef path, ssFormat_t format )
	{
		ScreenshotCommand *cmd = R_GetRenderCommand<ScreenshotCommand>();

		if ( !cmd )
		{
			return false;
		}

		cmd->x = 0;
		cmd->y = 0;
		cmd->width = glConfig.vidWidth;
		cmd->height = glConfig.vidHeight;
		Q_strncpyz(cmd->fileName, path.c_str(), sizeof(cmd->fileName));
		cmd->format = format;

		return true;
	}

namespace {
class ScreenshotCmd : public Cmd::StaticCmd {
	const ssFormat_t format;
	const std::string fileExtension;
public:
	ScreenshotCmd(std::string cmdName, ssFormat_t format, std::string ext)
		: StaticCmd(cmdName, Cmd::RENDERER, Str::Format("take a screenshot in %s format", ext)),
		format(format), fileExtension(ext) {}

	void Run(const Cmd::Args& args) const override {
		if (!tr.registered) {
			Print("ScreenshotCmd: renderer not initialized");
			return;
		}
		if (args.Argc() > 2) {
			PrintUsage(args, "[name]");
			return;
		}

		std::string fileName;
		if ( args.Argc() == 2 )
		{
			fileName = Str::Format( "screenshots/" PRODUCT_NAME_LOWER "-%s.%s", args.Argv(1), fileExtension );
		}
		else
		{
			qtime_t t;
			ri.RealTime( &t );

			// scan for a free filename
			int lastNumber;
			for ( lastNumber = 0; lastNumber <= 999; lastNumber++ )
			{
				fileName = Str::Format( "screenshots/" PRODUCT_NAME_LOWER "_%04d-%02d-%02d_%02d%02d%02d_%03d.%s",
					                    1900 + t.tm_year, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, lastNumber, fileExtension );

				if ( !FS::HomePath::FileExists( fileName.c_str() ) )
				{
					break; // file doesn't exist
				}
			}

			if ( lastNumber == 1000 )
			{
				Print("ScreenshotCmd: Couldn't create a file" );
				return;
			}
		}

		if (R_TakeScreenshot(fileName, format))
		{
			Print("Wrote %s", fileName);
		}
	}
};
ScreenshotCmd screenshotRegistration("screenshot", ssFormat_t::SSF_JPEG, "jpg");
ScreenshotCmd screenshotTGARegistration("screenshotTGA", ssFormat_t::SSF_TGA, "tga");
ScreenshotCmd screenshotJPEGRegistration("screenshotJPEG", ssFormat_t::SSF_JPEG, "jpg");
ScreenshotCmd screenshotPNGRegistration("screenshotPNG", ssFormat_t::SSF_PNG, "png");
} // namespace

//============================================================================

	/*
	==================
	RB_TakeVideoFrameCmd
	==================
	*/
	const RenderCommand *VideoFrameCommand::ExecuteSelf( ) const
	{
		GLint                     packAlign;
		int                       lineLen, captureLineLen;
		byte                      *pixels;
		int                       i;
		int                       outputSize;
		int                       j;
		int                       aviLineLen;

		// RB: it is possible to we still have a videoFrameCommand_t but we already stopped
		// video recording
		if ( ri.CL_VideoRecording() )
		{
			// take care of alignment issues for reading RGB images..

			glGetIntegerv( GL_PACK_ALIGNMENT, &packAlign );

			lineLen = width * 3;
			captureLineLen = PAD( lineLen, packAlign );

			pixels = ( byte * ) PADP( captureBuffer, packAlign );
			glReadPixels( 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels );

			if ( motionJpeg )
			{
				// Drop alignment and line padding bytes
				for ( i = 0; i < height; ++i )
				{
					memmove( captureBuffer + i * lineLen, pixels + i * captureLineLen, lineLen );
				}

				outputSize = SaveJPGToBuffer( encodeBuffer, 3 * width * height, 90, width, height, captureBuffer );
				ri.CL_WriteAVIVideoFrame( encodeBuffer, outputSize );
			}
			else
			{
				aviLineLen = PAD( lineLen, AVI_LINE_PADDING );

				for ( i = 0; i < height; ++i )
				{
					for ( j = 0; j < lineLen; j += 3 )
					{
						encodeBuffer[ i * aviLineLen + j + 0 ] = pixels[ i * captureLineLen + j + 2 ];
						encodeBuffer[ i * aviLineLen + j + 1 ] = pixels[ i * captureLineLen + j + 1 ];
						encodeBuffer[ i * aviLineLen + j + 2 ] = pixels[ i * captureLineLen + j + 0 ];
					}

					while ( j < aviLineLen )
					{
						encodeBuffer[ i * aviLineLen + j++ ] = 0;
					}
				}

				ri.CL_WriteAVIVideoFrame( encodeBuffer, aviLineLen * height );
			}
		}

		return this + 1;
	}

//============================================================================

	/*
	** GL_SetDefaultState
	*/
	void GL_SetDefaultState()
	{
		GLimp_LogComment( "--- GL_SetDefaultState ---\n" );

		glCullFace( GL_FRONT );
		GL_Cull( CT_TWO_SIDED );

		GL_CheckErrors();

		glVertexAttrib4f( ATTR_INDEX_COLOR, 1, 1, 1, 1 );

		GL_CheckErrors();

		tr.currenttextures.resize( glConfig2.maxTextureUnits );

		GL_TextureMode( r_textureMode.Get().c_str() );
		r_textureMode.GetModifiedValue();

		GL_DepthFunc( GL_LEQUAL );

		// make sure our GL state vector is set correctly
		glState.glStateBits = GLS_DEPTHTEST_DISABLE | GLS_DEPTHMASK_TRUE;
		glState.vertexAttribsState = 0;
		glState.vertexAttribPointersSet = 0;

		GL_BindProgram( nullptr );

		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
		glState.currentVBO = nullptr;
		glState.currentIBO = nullptr;

		GL_CheckErrors();

		// the vertex array is always enabled, but the color and texture
		// arrays are enabled and disabled around the compiled vertex array call
		glEnableVertexAttribArray( ATTR_INDEX_POSITION );

		/*
		   OpenGL 3.0 spec: E.1. PROFILES AND DEPRECATED FEATURES OF OPENGL 3.0 405
		   Calling VertexAttribPointer when no buffer object or no
		   vertex array object is bound will generate an INVALID OPERATION error,
		   as will calling any array drawing command when no vertex array object is
		   bound.
		 */

		GL_fboShim.glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		GL_fboShim.glBindRenderbuffer( GL_RENDERBUFFER, 0 );
		glState.currentFBO = nullptr;

		GL_PolygonMode( GL_FRONT_AND_BACK, GL_FILL );
		GL_DepthMask( GL_TRUE );
		glDisable( GL_DEPTH_TEST );
		glEnable( GL_SCISSOR_TEST );
		glDisable( GL_BLEND );

		GL_ColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		GL_ClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
		GL_ClearDepth( 1.0 );
		GL_ClearStencil( 0 );

		GL_DrawBuffer( GL_BACK );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );

		GL_CheckErrors();

		glState.stackIndex = 0;

		for ( int i = 0; i < MAX_GLSTACK; i++ )
		{
			MatrixIdentity( glState.modelViewMatrix[ i ] );
			MatrixIdentity( glState.projectionMatrix[ i ] );
			MatrixIdentity( glState.modelViewProjectionMatrix[ i ] );
		}
	}

	/*
	================
	GfxInfo_f
	================
	*/
	static void GfxInfo_f()
	{
		static const char fsstrings[][16] =
		{
			"windowed",
			"fullscreen"
		};

		Log::Notice( "%sOpenGL hardware vendor: %s",
			Color::ToString( Util::ordinal(glConfig2.hardwareVendor) ? Color::Green : Color::Yellow ),
			GetGLHardwareVendorName( glConfig2.hardwareVendor ) );

		Log::Notice( "%sOpenGL driver vendor: %s",
			Color::ToString( Util::ordinal(glConfig2.driverVendor) ? Color::Green : Color::Yellow ),
			GetGLDriverVendorName( glConfig2.driverVendor ) );

		Log::Notice("GL_VENDOR: %s", glConfig.vendor_string );
		Log::Notice("GL_RENDERER: %s", glConfig.renderer_string );
		Log::Notice("GL_VERSION: %s", glConfig.version_string );
		Log::Debug("GL_EXTENSIONS: %s", glConfig2.glExtensionsString );

		Log::Notice("GL_MAX_TEXTURE_SIZE: %d", glConfig.maxTextureSize );
		Log::Notice("GL_MAX_3D_TEXTURE_SIZE: %d", glConfig2.max3DTextureSize );
		Log::Notice("GL_MAX_CUBE_MAP_TEXTURE_SIZE: %d", glConfig2.maxCubeMapTextureSize );
		Log::Notice("GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS: %d", glConfig2.maxTextureUnits );

		Log::Notice("GL_SHADING_LANGUAGE_VERSION: %s", glConfig2.shadingLanguageVersionString );

		Log::Notice("GL_MAX_VERTEX_UNIFORM_COMPONENTS %d", glConfig2.maxVertexUniforms );
		Log::Notice("GL_MAX_VERTEX_ATTRIBS %d", glConfig2.maxVertexAttribs );

		if ( !glConfig2.glCoreProfile )
		{
			Log::Notice( "GL_MAX_PROGRAM_NATIVE_ALU_INSTRUCTIONS_ARB: %d", glConfig2.maxAluInstructions );
			Log::Notice( "GL_MAX_PROGRAM_NATIVE_TEX_INDIRECTIONS_ARB: %d", glConfig2.maxTexIndirections );
		}

		if ( glConfig2.drawBuffersAvailable )
		{
			Log::Notice("GL_MAX_DRAW_BUFFERS: %d", glConfig2.maxDrawBuffers );
		}

		if ( glConfig2.textureAnisotropyAvailable )
		{
			Log::Notice("GL_TEXTURE_MAX_ANISOTROPY_EXT: %f", glConfig2.maxTextureAnisotropy );
		}

		Log::Notice("GL_MAX_RENDERBUFFER_SIZE: %d", glConfig2.maxRenderbufferSize );
		Log::Notice("GL_MAX_COLOR_ATTACHMENTS: %d", glConfig2.maxColorAttachments );

		Log::Notice("PIXELFORMAT: color(%d-bits)", glConfig.colorBits );

		Log::Notice("MODE: %d, %d x %d %s",
			r_mode->integer,
			glConfig.vidWidth, glConfig.vidHeight,
			fsstrings[ +r_fullscreen.Get() ] );

		if ( !!r_glExtendedValidation->integer )
		{
			Log::Notice("Using OpenGL version %d.%d, requested: %d.%d, highest: %d.%d",
				glConfig2.glMajor, glConfig2.glMinor, glConfig2.glRequestedMajor, glConfig2.glRequestedMinor,
				glConfig2.glHighestMajor, glConfig2.glHighestMinor );
		}
		else
		{
			Log::Notice("Using OpenGL version %d.%d, requested: %d.%d", glConfig2.glMajor, glConfig2.glMinor, glConfig2.glRequestedMajor, glConfig2.glRequestedMinor );
		}

		if ( glConfig.driverType == glDriverType_t::GLDRV_OPENGL3 )
		{
			Log::Notice("%sUsing OpenGL 3.x context.", Color::ToString( Color::Green ) );

			/* See https://www.khronos.org/opengl/wiki/OpenGL_Context
			for information about core, compatibility and forward context. */

			if ( glConfig2.glCoreProfile )
			{
				Log::Notice("%sUsing an OpenGL core profile.", Color::ToString( Color::Green ) );
			}
			else
			{
				Log::Notice("%sUsing an OpenGL compatibility profile.", Color::ToString( Color::Red ) );
			}

			if ( glConfig2.glForwardCompatibleContext )
			{
				Log::Notice("OpenGL 3.x context is forward compatible.");
			}
			else
			{
				Log::Notice("OpenGL 3.x context is not forward compatible.");
			}
		}
		else
		{
			Log::Notice("%sUsing OpenGL 2.x context.", Color::ToString( Color::Red ) );
		}

		if ( glConfig2.glEnabledExtensionsString.length() != 0 )
		{
			Log::Notice("%sUsing OpenGL extensions: %s", Color::ToString( Color::Green ), glConfig2.glEnabledExtensionsString );
		}

		if ( glConfig2.glMissingExtensionsString.length() != 0 )
		{
			Log::Notice("%sMissing OpenGL extensions: %s", Color::ToString( Color::Red ), glConfig2.glMissingExtensionsString );
		}

		if ( glConfig2.halfFloatVertexAvailable )
		{
			Log::Notice("%sUsing half-float vertex format.", Color::ToString( Color::Green ));
		}
		else
		{
			Log::Notice("%sMissing half-float vertex format.", Color::ToString( Color::Red ));
		}

		if ( glConfig.hardwareType == glHardwareType_t::GLHW_R300 )
		{
			Log::Notice("%sUsing ATI R300 approximations.", Color::ToString( Color::Red ));
		}

		if ( glConfig.textureCompression != textureCompression_t::TC_NONE )
		{
			Log::Notice("%sUsing S3TC (DXTC) texture compression.", Color::ToString( Color::Green ) );
		}

		if ( glConfig2.vboVertexSkinningAvailable )
		{
			/* Mesa drivers usually support 256 bones, Nvidia proprietary drivers
			usually support 233 bones, OpenGL 2.1 hardware usually supports no more
			than 41 bones which may not be enough to use hardware acceleration on
			models from games like Unvanquished. */
			if ( glConfig2.maxVertexSkinningBones < 233 )
			{
				Log::Notice("%sUsing GPU vertex skinning with max %i bones in a single pass, some models may not be hardware accelerated.", Color::ToString( Color::Red ), glConfig2.maxVertexSkinningBones );
			}
			else
			{
				Log::Notice("%sUsing GPU vertex skinning with max %i bones in a single pass, models are hardware accelerated.", Color::ToString( Color::Green ), glConfig2.maxVertexSkinningBones );
			}
		}
		else
		{
			Log::Notice("%sMissing GPU vertex skinning, models are not hardware-accelerated.", Color::ToString( Color::Red ) );
		}

		switch ( glConfig2.textureRGBA16BlendAvailable )
		{
			case 1:
				Log::Notice( "%sUsing GL_RGBA16 with GL_FRAMEBUFFER_BLEND.", Color::ToString( Color::Green ) );
				break;
			case -1:
				Log::Notice( "%sUsing GL_RGBA16 with GL_FRAMEBUFFER_BLEND (assumed to be available).", Color::ToString( Color::Yellow ) );
				break;
			default:
			case 0:
				Log::Notice( "%sMissing GL_RGBA16 with GL_FRAMEBUFFER_BLEND.", Color::ToString( Color::Red ) );
				break;
		}

		if ( glConfig.smpActive )
		{
			Log::Notice("Using dual processor acceleration." );
		}

		if ( r_finish->integer )
		{
			Log::Notice("Forcing glFinish." );
		}

		Log::Debug("texturemode: %s", r_textureMode.Get() );
		Log::Debug("picmip: %d", r_picMip->integer );
		Log::Debug("imageMaxDimension: %d", r_imageMaxDimension->integer );
		Log::Debug("ignoreMaterialMinDimension: %d", r_ignoreMaterialMinDimension->integer );
		Log::Debug("ignoreMaterialMaxDimension: %d", r_ignoreMaterialMaxDimension->integer );
		Log::Debug("replaceMaterialMinDimensionIfPresentWithMaxDimension: %d", r_replaceMaterialMinDimensionIfPresentWithMaxDimension->integer );
	}

	// FIXME: uses regular logging not Print()
	static Cmd::LambdaCmd gfxInfoCmd(
		"gfxinfo", Cmd::RENDERER, "dump graphics driver and configuration info",
		[]( const Cmd::Args & ) { GfxInfo_f(); });

	class GlslRestartCmd : public Cmd::StaticCmd
	{
	public:
		GlslRestartCmd() : StaticCmd(
			"glsl_restart", Cmd::RENDERER, "recompile GLSL shaders (useful when shaderpath is set)") {}

		void Run( const Cmd::Args & ) const override
		{
			// make sure the render thread is stopped
			R_SyncRenderThread();

			GLSL_ShutdownGPUShaders();
			GLSL_InitGPUShaders();

			for ( int i = 0; i < tr.numShaders; i++ )
			{
				shader_t &shader = *tr.shaders[ i ];
				if ( shader.stages == shader.lastStage || shader.stages[ 0 ].deformIndex == 0 )
				{
					continue;
				}

				int deformIndex =
					gl_shaderManager.getDeformShaderIndex( shader.deforms, shader.numDeforms );

				for ( shaderStage_t *stage = shader.stages; stage != shader.lastStage; stage++ )
				{
					stage->deformIndex = deformIndex;
				}
			}

			if ( glConfig2.usingMaterialSystem ) {
				/* GLSL shaders linked to materials will be invalidated by glsl_restart,
				so we need to reset them here */
				materialSystem.GLSLRestart();
			}
		}
	};
	static GlslRestartCmd glslRestartCmdRegistration;

	/*
	===============
	R_Register
	===============
	*/
	void R_Register()
	{
		// OpenGL context selection
		r_glMajorVersion = Cvar_Get( "r_glMajorVersion", "", CVAR_LATCH );
		r_glMinorVersion = Cvar_Get( "r_glMinorVersion", "", CVAR_LATCH );
		r_glProfile = Cvar_Get( "r_glProfile", "", CVAR_LATCH );
		Cvar::Latch( r_glDebugProfile );
		r_glAllowSoftware = Cvar_Get( "r_glAllowSoftware", "0", CVAR_LATCH );
		r_glExtendedValidation = Cvar_Get( "r_glExtendedValidation", "0", CVAR_LATCH );

		// latched and archived variables
		r_picMip = Cvar_Get( "r_picMip", "0",  CVAR_LATCH | CVAR_ARCHIVE );
		r_imageMaxDimension = Cvar_Get( "r_imageMaxDimension", "0",  CVAR_LATCH | CVAR_ARCHIVE );
		r_ignoreMaterialMinDimension = Cvar_Get( "r_ignoreMaterialMinDimension", "0",  CVAR_LATCH | CVAR_ARCHIVE );
		r_ignoreMaterialMaxDimension = Cvar_Get( "r_ignoreMaterialMaxDimension", "0",  CVAR_LATCH | CVAR_ARCHIVE );
		r_replaceMaterialMinDimensionIfPresentWithMaxDimension
			= Cvar_Get( "r_replaceMaterialMinDimensionIfPresentWithMaxDimension", "0",  CVAR_LATCH | CVAR_ARCHIVE );
		Cvar::Latch(r_imageFitScreen);
		r_colorbits = Cvar_Get( "r_colorbits", "0",  CVAR_LATCH );
		r_mode = Cvar_Get( "r_mode", "-2", CVAR_LATCH | CVAR_ARCHIVE );
		r_customwidth = Cvar_Get( "r_customwidth", "1600", CVAR_LATCH | CVAR_ARCHIVE );
		r_customheight = Cvar_Get( "r_customheight", "1024", CVAR_LATCH | CVAR_ARCHIVE );
		r_subdivisions = Cvar_Get( "r_subdivisions", "4", CVAR_LATCH );
		r_realtimeLightingCastShadows = Cvar_Get( "r_realtimeLightingCastShadows", "1", 0 );
		r_precomputedLighting = Cvar_Get( "r_precomputedLighting", "1", CVAR_CHEAT | CVAR_LATCH );
		Cvar::Latch( r_overbrightDefaultExponent );
		Cvar::Latch( r_overbrightDefaultClamp );
		Cvar::Latch( r_overbrightIgnoreMapSettings );
		Cvar::Latch( r_lightMode );
		Cvar::Latch( r_colorGrading );
		Cvar::Latch( r_drawSky );
		r_lightStyles = Cvar_Get( "r_lightStyles", "1", CVAR_LATCH | CVAR_ARCHIVE );
		r_exportTextures = Cvar_Get( "r_exportTextures", "0", 0 );
		r_heatHaze = Cvar_Get( "r_heatHaze", "1", CVAR_LATCH | CVAR_ARCHIVE );
		r_noMarksOnTrisurfs = Cvar_Get( "r_noMarksOnTrisurfs", "1", CVAR_CHEAT );

		r_wolfFog = Cvar_Get( "r_wolfFog", "1", CVAR_CHEAT );
		r_noFog = Cvar_Get( "r_noFog", "0", CVAR_CHEAT );

		Cvar::Latch( r_forceAmbient );

		r_smp = Cvar_Get( "r_smp", "0",  CVAR_LATCH );

		// temporary latched variables that can only change over a restart
		r_singleShader = Cvar_Get( "r_singleShader", "0", CVAR_CHEAT | CVAR_LATCH );
		r_stitchCurves = Cvar_Get( "r_stitchCurves", "1", CVAR_CHEAT | CVAR_LATCH );
		r_debugShadowMaps = Cvar_Get( "r_debugShadowMaps", "0", CVAR_CHEAT | CVAR_LATCH );
		r_shadowMapLinearFilter = Cvar_Get( "r_shadowMapLinearFilter", "1", CVAR_CHEAT | CVAR_LATCH );
		r_lightBleedReduction = Cvar_Get( "r_lightBleedReduction", "0", CVAR_CHEAT | CVAR_LATCH );
		r_overDarkeningFactor = Cvar_Get( "r_overDarkeningFactor", "30.0", CVAR_CHEAT | CVAR_LATCH );
		r_shadowMapDepthScale = Cvar_Get( "r_shadowMapDepthScale", "1.41", CVAR_CHEAT | CVAR_LATCH );

		r_parallelShadowSplitWeight = Cvar_Get( "r_parallelShadowSplitWeight", "0.9", CVAR_CHEAT );
		r_parallelShadowSplits = Cvar_Get( "r_parallelShadowSplits", "2", CVAR_CHEAT | CVAR_LATCH );
		AssertCvarRange( r_parallelShadowSplits, 0, MAX_SHADOWMAPS - 1, true );

		// archived variables that can change at any time
		r_lodBias = Cvar_Get( "r_lodBias", "0", 0 );
		r_znear = Cvar_Get( "r_znear", "3", CVAR_CHEAT );
		r_zfar = Cvar_Get( "r_zfar", "0", CVAR_CHEAT );
		r_checkGLErrors = Cvar_Get( "r_checkGLErrors", "-1", 0 );
		r_finish = Cvar_Get( "r_finish", "0", CVAR_CHEAT );
		r_gamma = Cvar_Get( "r_gamma", "1.0", CVAR_ARCHIVE );
		r_facePlaneCull = Cvar_Get( "r_facePlaneCull", "1", 0 );

		Cvar::Latch( r_ambientScale );
		r_lightScale = Cvar_Get( "r_lightScale", "2", CVAR_CHEAT );

		r_vboFaces = Cvar_Get( "r_vboFaces", "1", CVAR_CHEAT );
		r_vboCurves = Cvar_Get( "r_vboCurves", "1", CVAR_CHEAT );
		r_vboTriangles = Cvar_Get( "r_vboTriangles", "1", CVAR_CHEAT );
		Cvar::Latch( r_vboModels );
		r_vboVertexSkinning = Cvar_Get( "r_vboVertexSkinning", "1",  CVAR_LATCH );

		r_mergeLeafSurfaces = Cvar_Get( "r_mergeLeafSurfaces", "1",  CVAR_LATCH );

		r_evsmPostProcess = Cvar_Get( "r_evsmPostProcess", "0",  CVAR_LATCH );

		r_printShaders = Cvar_Get( "r_printShaders", "0", 0 );

		Cvar::Latch( r_bloom );
		r_FXAA = Cvar_Get( "r_FXAA", "0", CVAR_LATCH | CVAR_ARCHIVE );
		r_ssao = Cvar_Get( "r_ssao", "0", CVAR_LATCH | CVAR_ARCHIVE );

		// temporary variables that can change at any time
		r_showImages = Cvar_Get( "r_showImages", "0", CVAR_TEMP );

		r_debugSort = Cvar_Get( "r_debugSort", "0", CVAR_CHEAT );

		r_nocurves = Cvar_Get( "r_nocurves", "0", CVAR_CHEAT );
		r_lightScissors = Cvar_Get( "r_lightScissors", "1", CVAR_ARCHIVE );
		AssertCvarRange( r_lightScissors, 0, 2, true );

		r_noLightVisCull = Cvar_Get( "r_noLightVisCull", "0", CVAR_CHEAT );
		r_noInteractionSort = Cvar_Get( "r_noInteractionSort", "0", CVAR_CHEAT );

		Cvar::Latch( r_realtimeLightingRenderer );
		Cvar::Latch( r_realtimeLighting );
		Cvar::Latch( r_realtimeLightLayers );
		Cvar::Latch( r_preferBindlessTextures );
		Cvar::Latch( r_materialSystem );

		r_drawworld = Cvar_Get( "r_drawworld", "1", CVAR_CHEAT );
		r_max_portal_levels = Cvar_Get( "r_max_portal_levels", "5", 0 );

		r_showSmp = Cvar_Get( "r_showSmp", "0", CVAR_CHEAT );
		r_skipBackEnd = Cvar_Get( "r_skipBackEnd", "0", CVAR_CHEAT );

		r_lodScale = Cvar_Get( "r_lodScale", "5", CVAR_CHEAT );
		r_norefresh = Cvar_Get( "r_norefresh", "0", CVAR_CHEAT );
		r_drawentities = Cvar_Get( "r_drawentities", "1", CVAR_CHEAT );
		r_drawpolies = Cvar_Get( "r_drawpolies", "1", CVAR_CHEAT );
		r_ignore = Cvar_Get( "r_ignore", "1", CVAR_CHEAT );
		r_nocull = Cvar_Get( "r_nocull", "0", CVAR_CHEAT );
		r_novis = Cvar_Get( "r_novis", "0", CVAR_CHEAT );
		r_speeds = Cvar_Get( "r_speeds", "0", 0 );
		r_logFile = Cvar_Get( "r_logFile", "0", CVAR_CHEAT );
		r_debugSurface = Cvar_Get( "r_debugSurface", "0", CVAR_CHEAT );
		r_nobind = Cvar_Get( "r_nobind", "0", CVAR_CHEAT );
		r_offsetFactor = Cvar_Get( "r_offsetFactor", "-1", CVAR_CHEAT );
		r_offsetUnits = Cvar_Get( "r_offsetUnits", "-2", CVAR_CHEAT );

		r_physicalMapping = Cvar_Get( "r_physicalMapping", "1", CVAR_LATCH | CVAR_ARCHIVE );
		r_specularExponentMin = Cvar_Get( "r_specularExponentMin", "0.001", CVAR_CHEAT );
		r_specularExponentMax = Cvar_Get( "r_specularExponentMax", "16", CVAR_CHEAT );
		r_specularScale = Cvar_Get( "r_specularScale", "1.0", CVAR_CHEAT | CVAR_LATCH );
		r_specularMapping = Cvar_Get( "r_specularMapping", "1", CVAR_LATCH | CVAR_ARCHIVE );
		r_deluxeMapping = Cvar_Get( "r_deluxeMapping", "1", CVAR_LATCH | CVAR_ARCHIVE );
		r_normalScale = Cvar_Get( "r_normalScale", "1.0", CVAR_ARCHIVE );
		r_normalMapping = Cvar_Get( "r_normalMapping", "1", CVAR_LATCH | CVAR_ARCHIVE );
		r_highQualityNormalMapping = Cvar_Get( "r_highQualityNormalMapping", "0",  CVAR_LATCH );
		r_liquidMapping = Cvar_Get( "r_liquidMapping", "0", CVAR_LATCH | CVAR_ARCHIVE );
		r_reliefDepthScale = Cvar_Get( "r_reliefDepthScale", "0.02", CVAR_CHEAT );
		r_reliefMapping = Cvar_Get( "r_reliefMapping", "0", CVAR_LATCH | CVAR_ARCHIVE );
		r_glowMapping = Cvar_Get( "r_glowMapping", "1", CVAR_LATCH );
		Cvar::Latch( r_reflectionMapping );
		Cvar::Latch( r_autoBuildCubeMaps );
		Cvar::Latch( r_cubeProbeSize );
		Cvar::Latch( r_cubeProbeSpacing );

		r_halfLambertLighting = Cvar_Get( "r_halfLambertLighting", "1", CVAR_LATCH | CVAR_ARCHIVE );
		r_rimLighting = Cvar_Get( "r_rimLighting", "0",  CVAR_LATCH | CVAR_ARCHIVE );
		r_rimExponent = Cvar_Get( "r_rimExponent", "3", CVAR_CHEAT | CVAR_LATCH );
		AssertCvarRange( r_rimExponent, 0.5, 8.0, false );

		Cvar::Latch( r_highPrecisionRendering );

		r_drawBuffer = Cvar_Get( "r_drawBuffer", "GL_BACK", CVAR_CHEAT );
		r_lockpvs = Cvar_Get( "r_lockpvs", "0", CVAR_CHEAT );
		r_noportals = Cvar_Get( "r_noportals", "0", CVAR_CHEAT );

		Cvar::Latch( r_shadows );

		r_softShadows = Cvar_Get( "r_softShadows", "0",  CVAR_LATCH );
		AssertCvarRange( r_softShadows, 0, 6, true );

		r_softShadowsPP = Cvar_Get( "r_softShadowsPP", "0",  CVAR_LATCH );

		r_shadowBlur = Cvar_Get( "r_shadowBlur", "2",  CVAR_LATCH );

		r_shadowMapSizeUltra = Cvar_Get( "r_shadowMapSizeUltra", "1024",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeUltra, 32, 2048, true );

		r_shadowMapSizeVeryHigh = Cvar_Get( "r_shadowMapSizeVeryHigh", "512",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeVeryHigh, 32, 2048, true );

		r_shadowMapSizeHigh = Cvar_Get( "r_shadowMapSizeHigh", "256",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeHigh, 32, 2048, true );

		r_shadowMapSizeMedium = Cvar_Get( "r_shadowMapSizeMedium", "128",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeMedium, 32, 2048, true );

		r_shadowMapSizeLow = Cvar_Get( "r_shadowMapSizeLow", "64",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeLow, 32, 2048, true );

		shadowMapResolutions[ 0 ] = r_shadowMapSizeUltra->integer;
		shadowMapResolutions[ 1 ] = r_shadowMapSizeVeryHigh->integer;
		shadowMapResolutions[ 2 ] = r_shadowMapSizeHigh->integer;
		shadowMapResolutions[ 3 ] = r_shadowMapSizeMedium->integer;
		shadowMapResolutions[ 4 ] = r_shadowMapSizeLow->integer;

		r_shadowMapSizeSunUltra = Cvar_Get( "r_shadowMapSizeSunUltra", "1024",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeSunUltra, 32, 2048, true );

		r_shadowMapSizeSunVeryHigh = Cvar_Get( "r_shadowMapSizeSunVeryHigh", "1024",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeSunVeryHigh, 512, 2048, true );

		r_shadowMapSizeSunHigh = Cvar_Get( "r_shadowMapSizeSunHigh", "1024",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeSunHigh, 512, 2048, true );

		r_shadowMapSizeSunMedium = Cvar_Get( "r_shadowMapSizeSunMedium", "1024",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeSunMedium, 512, 2048, true );

		r_shadowMapSizeSunLow = Cvar_Get( "r_shadowMapSizeSunLow", "1024",  CVAR_LATCH );
		AssertCvarRange( r_shadowMapSizeSunLow, 512, 2048, true );

		sunShadowMapResolutions[ 0 ] = r_shadowMapSizeSunUltra->integer;
		sunShadowMapResolutions[ 1 ] = r_shadowMapSizeSunVeryHigh->integer;
		sunShadowMapResolutions[ 2 ] = r_shadowMapSizeSunHigh->integer;
		sunShadowMapResolutions[ 3 ] = r_shadowMapSizeSunMedium->integer;
		sunShadowMapResolutions[ 4 ] = r_shadowMapSizeSunLow->integer;

		r_shadowLodBias = Cvar_Get( "r_shadowLodBias", "0", CVAR_CHEAT );
		r_shadowLodScale = Cvar_Get( "r_shadowLodScale", "0.8", CVAR_CHEAT );
		r_noShadowPyramids = Cvar_Get( "r_noShadowPyramids", "0", CVAR_CHEAT );
		r_cullShadowPyramidFaces = Cvar_Get( "r_cullShadowPyramidFaces", "0", CVAR_CHEAT );
		r_noLightFrustums = Cvar_Get( "r_noLightFrustums", "1", CVAR_CHEAT );

		r_maxPolys = Cvar_Get( "r_maxpolys", "10000", CVAR_LATCH );  // 600 in vanilla Q3A
		AssertCvarRange( r_maxPolys, 600, 30000, true );

		r_maxPolyVerts = Cvar_Get( "r_maxpolyverts", "100000", CVAR_LATCH );  // 3000 in vanilla Q3A
		AssertCvarRange( r_maxPolyVerts, 3000, 200000, true );

		r_showTris = Cvar_Get( "r_showTris", "0", CVAR_CHEAT );
		r_showSky = Cvar_Get( "r_showSky", "0", CVAR_CHEAT );
		r_showShadowLod = Cvar_Get( "r_showShadowLod", "0", CVAR_CHEAT );
		r_showShadowMaps = Cvar_Get( "r_showShadowMaps", "0", CVAR_CHEAT );
		r_showSkeleton = Cvar_Get( "r_showSkeleton", "0", CVAR_CHEAT );
		r_showEntityTransforms = Cvar_Get( "r_showEntityTransforms", "0", CVAR_CHEAT );
		r_showLightTransforms = Cvar_Get( "r_showLightTransforms", "0", CVAR_CHEAT );
		r_showLightInteractions = Cvar_Get( "r_showLightInteractions", "0", CVAR_CHEAT );
		r_showLightScissors = Cvar_Get( "r_showLightScissors", "0", CVAR_CHEAT );
		r_showLightBatches = Cvar_Get( "r_showLightBatches", "0", CVAR_CHEAT );
		r_showLightGrid = Cvar_Get( "r_showLightGrid", "0", CVAR_CHEAT );
		r_showLightTiles = Cvar_Get("r_showLightTiles", "0", CVAR_CHEAT | CVAR_LATCH );
		r_showBatches = Cvar_Get( "r_showBatches", "0", CVAR_CHEAT );
		Cvar::Latch( r_showVertexColors );
		r_showLightMaps = Cvar_Get( "r_showLightMaps", "0", CVAR_CHEAT | CVAR_LATCH );
		r_showDeluxeMaps = Cvar_Get( "r_showDeluxeMaps", "0", CVAR_CHEAT | CVAR_LATCH );
		r_showNormalMaps = Cvar_Get( "r_showNormalMaps", "0", CVAR_CHEAT | CVAR_LATCH );
		r_showMaterialMaps = Cvar_Get( "r_showMaterialMaps", "0", CVAR_CHEAT | CVAR_LATCH );
		Cvar::Latch( r_showReflectionMaps );
		r_showBspNodes = Cvar_Get( "r_showBspNodes", "0", CVAR_CHEAT );
		Cvar::Latch( r_showGlobalMaterials );
		Cvar::Latch( r_materialDebug );
		r_showParallelShadowSplits = Cvar_Get( "r_showParallelShadowSplits", "0", CVAR_CHEAT | CVAR_LATCH );

		Cvar::Latch( r_profilerRenderSubGroups );
	}

	/*
	===============
	R_Init
	===============
	*/
	bool R_Init()
	{
		int i;

		Log::Debug("----- R_Init -----" );

		// clear all our internal state
		ResetStruct( tr );
		ResetStruct( backEnd );
		ResetStruct( tess );

		if ( ( intptr_t ) tess.verts & 15 )
		{
			Log::Warn( "tess.verts not 16 byte aligned" );
		}

		// init function tables
		for ( i = 0; i < FUNCTABLE_SIZE; i++ )
		{
			tr.sinTable[ i ] = sinf( DEG2RAD( i * 360.0f / ( ( float )( FUNCTABLE_SIZE - 1 ) ) ) );
			tr.squareTable[ i ] = ( i < FUNCTABLE_SIZE / 2 ) ? 1.0f : -1.0f;
			tr.sawToothTable[ i ] = ( float ) i / FUNCTABLE_SIZE;
			tr.inverseSawToothTable[ i ] = 1.0f - tr.sawToothTable[ i ];

			if ( i < FUNCTABLE_SIZE / 2 )
			{
				if ( i < FUNCTABLE_SIZE / 4 )
				{
					tr.triangleTable[ i ] = ( float ) i / ( FUNCTABLE_SIZE / 4 );
				}
				else
				{
					tr.triangleTable[ i ] = 1.0f - tr.triangleTable[ i - FUNCTABLE_SIZE / 4 ];
				}
			}
			else
			{
				tr.triangleTable[ i ] = -tr.triangleTable[ i - FUNCTABLE_SIZE / 2 ];
			}
		}

		R_InitFogTable();

		R_NoiseInit();

		R_Register();

		if ( !InitOpenGL() )
		{
			return false;
		}

		tr.lightMode = lightMode_t( r_lightMode.Get() );

		if ( !Com_AreCheatsAllowed() )
		{
			switch( tr.lightMode )
			{
				case lightMode_t::VERTEX:
				case lightMode_t::MAP:
					break;

				default:
					tr.lightMode = lightMode_t::MAP;
					r_lightMode.Set( Util::ordinal( tr.lightMode ) );
					Cvar::Latch( r_lightMode );
					break;
			}
		}

		if ( r_showNormalMaps->integer
			|| r_showMaterialMaps->integer )
		{
			tr.lightMode = lightMode_t::MAP;
		}
		else if ( r_showLightMaps->integer
			|| r_showDeluxeMaps->integer )
		{
			tr.lightMode = lightMode_t::MAP;
		}
		else if ( r_showVertexColors.Get() )
		{
			tr.lightMode = lightMode_t::VERTEX;
		}

		if ( r_reflectionMapping.Get() ) {
			glConfig2.reflectionMappingAvailable = true;

			if ( !r_normalMapping->integer ) {
				glConfig2.reflectionMappingAvailable = false;
				Log::Warn( "Unable to use static reflections without normal mapping, make sure you enable r_normalMapping" );
			}

			if ( !r_deluxeMapping->integer ) {
				glConfig2.reflectionMappingAvailable = false;
				Log::Warn( "Unable to use static reflections without deluxe mapping, make sure you enable r_deluxeMapping" );
			}

			if ( !r_specularMapping->integer ) {
				glConfig2.reflectionMappingAvailable = false;
				Log::Warn( "Unable to use static reflections without specular mapping, make sure you enable r_specularMapping" );
			}

			if ( r_physicalMapping->integer ) {
				glConfig2.reflectionMappingAvailable = false;
				Log::Warn( "Unable to use static reflections with physical mapping, make sure you disable r_physicalMapping" );
			}
		}

		backEndData[ 0 ] = ( backEndData_t * ) ri.Hunk_Alloc( sizeof( *backEndData[ 0 ] ), ha_pref::h_low );
		backEndData[ 0 ]->polys = ( srfPoly_t * ) ri.Hunk_Alloc( r_maxPolys->integer * sizeof( srfPoly_t ), ha_pref::h_low );
		backEndData[ 0 ]->polyVerts = ( polyVert_t * ) ri.Hunk_Alloc( r_maxPolyVerts->integer * sizeof( polyVert_t ), ha_pref::h_low );
		backEndData[ 0 ]->polyIndexes = ( int * ) ri.Hunk_Alloc( r_maxPolyVerts->integer * sizeof( int ), ha_pref::h_low );

		if ( r_smp->integer )
		{
			backEndData[ 1 ] = ( backEndData_t * ) ri.Hunk_Alloc( sizeof( *backEndData[ 1 ] ), ha_pref::h_low );
			backEndData[ 1 ]->polys = ( srfPoly_t * ) ri.Hunk_Alloc( r_maxPolys->integer * sizeof( srfPoly_t ), ha_pref::h_low );
			backEndData[ 1 ]->polyVerts = ( polyVert_t * ) ri.Hunk_Alloc( r_maxPolyVerts->integer * sizeof( polyVert_t ), ha_pref::h_low );
			backEndData[ 1 ]->polyIndexes = ( int * ) ri.Hunk_Alloc( r_maxPolyVerts->integer * sizeof( int ), ha_pref::h_low );
		}
		else
		{
			backEndData[ 1 ] = nullptr;
		}

		R_ToggleSmpFrame();

		R_InitImages();

		R_InitFBOs();

		R_InitVBOs();

		R_InitShaders();

		R_InitSkins();

		R_ModelInit();

		R_InitAnimations();

		R_InitFreeType();

		R_InitVisTests();

		GL_CheckErrors();

		// print info
		GfxInfo_f();

		Log::Debug("----- finished R_Init -----" );

		return true;
	}

	/*
	===============
	RE_Shutdown
	===============
	*/
	void RE_Shutdown( bool destroyWindow )
	{
		Log::Debug("RE_Shutdown( destroyWindow = %i )", destroyWindow );

		if ( tr.registered )
		{
			R_SyncRenderThread();

			CIN_CloseAllVideos();
			R_ShutdownBackend();
			R_ShutdownImages();
			R_ShutdownVBOs();
			R_ShutdownFBOs();
			R_ShutdownVisTests();
		}

		R_DoneFreeType();

		if ( glConfig2.usingMaterialSystem ) {
			materialSystem.Free();
		}

		// shut down platform specific OpenGL stuff
		if ( destroyWindow )
		{
			GLSL_ShutdownGPUShaders();
			if( glConfig2.glCoreProfile ) {
				glBindVertexArray( 0 );
				glDeleteVertexArrays( 1, &backEnd.currentVAO );
			}

			GLimp_Shutdown();
		}

		tr.registered = false;
	}

	/*
	=============
	RE_EndRegistration

	Touch all images to make sure they are resident
	=============
	*/
	void RE_EndRegistration()
	{
		R_SyncRenderThread();
		if ( r_lazyShaders.Get() == 1 )
		{
			GLSL_FinishGPUShaders();
		}

		/* TODO: Move this into a loading step and don't render it to the screen
		For now though do it here to avoid the ugly square rendering appearing on top of the loading screen */
		if ( glConfig2.reflectionMappingAvailable ) {
			switch ( r_autoBuildCubeMaps.Get() ) {
				case Util::ordinal( cubeProbesAutoBuildMode::CACHED ):
				case Util::ordinal( cubeProbesAutoBuildMode::ALWAYS ):
					R_BuildCubeMaps();
					break;
				case Util::ordinal( cubeProbesAutoBuildMode::DISABLED ):
				default:
					break;
			}
		} else if ( r_reflectionMapping.Get() && r_autoBuildCubeMaps.Get() != Util::ordinal( cubeProbesAutoBuildMode::DISABLED ) ) {
			/* TODO: Add some proper functionality to check the various cvar combinations required for different graphics settings,
			and move this check there */
			Log::Notice( "Unable to build reflection cubemaps due to incorrect graphics settings" );
		}
	}

	/*
	=====================
	GetRefAPI
	=====================
	*/
	refexport_t *GetRefAPI( int apiVersion, refimport_t *rimp )
	{
		static refexport_t re;

		ri = *rimp;

		Log::Debug("GetRefAPI()" );

		re = {};

		if ( apiVersion != REF_API_VERSION )
		{
			Log::Notice("Mismatched REF_API_VERSION: expected %i, got %i", REF_API_VERSION, apiVersion );
			return nullptr;
		}

		// the RE_ functions are Renderer Entry points

		// Q3A BEGIN
		re.Shutdown = RE_Shutdown;

		re.BeginRegistration = RE_BeginRegistration;
		re.RegisterModel = RE_RegisterModel;

		re.RegisterSkin = RE_RegisterSkin;
		re.RegisterShader = RE_RegisterShader;

		re.LoadWorld = RE_LoadWorldMap;
		re.SetWorldVisData = RE_SetWorldVisData;
		re.EndRegistration = RE_EndRegistration;

		re.BeginFrame = RE_BeginFrame;
		re.EndFrame = RE_EndFrame;

		re.MarkFragments = R_MarkFragments;

		re.LerpTag = RE_LerpTagET;

		re.ModelBounds = R_ModelBounds;

		re.ClearScene = RE_ClearScene;
		re.AddRefEntityToScene = RE_AddRefEntityToScene;

		re.AddPolyToScene = RE_AddPolyToSceneET;
		re.AddPolysToScene = RE_AddPolysToScene;
		re.LightForPoint = R_LightForPoint;

		re.AddLightToScene = RE_AddDynamicLightToSceneET;
		re.AddAdditiveLightToScene = RE_AddDynamicLightToSceneQ3A;

		re.RenderScene = RE_RenderScene;

		re.SetColor = RE_SetColor;
		re.SetClipRegion = RE_SetClipRegion;
		re.DrawStretchPic = RE_StretchPic;

		re.DrawRotatedPic = RE_RotatedPic;
		re.Add2dPolys = RE_2DPolyies;
		re.ScissorEnable = RE_ScissorEnable;
		re.ScissorSet = RE_ScissorSet;
		re.DrawStretchPicGradient = RE_StretchPicGradient;
		re.SetMatrixTransform = RE_SetMatrixTransform;
		re.ResetMatrixTransform = RE_ResetMatrixTransform;

		re.Glyph = RE_Glyph;
		re.GlyphChar = RE_GlyphChar;
		re.RegisterFont = RE_RegisterFont;
		re.UnregisterFont = RE_UnregisterFont;

		re.RemapShader = R_RemapShader;
		re.GetEntityToken = R_GetEntityToken;
		re.inPVS = R_inPVS;
		re.inPVVS = R_inPVVS;
		// Q3A END

		// ET BEGIN
		re.LoadDynamicShader = RE_LoadDynamicShader;
		re.Finish = RE_Finish;
		// ET END

		// XreaL BEGIN
		re.TakeVideoFrame = RE_TakeVideoFrame;

		re.RegisterAnimation = RE_RegisterAnimation;
		re.CheckSkeleton = RE_CheckSkeleton;
		re.BuildSkeleton = RE_BuildSkeleton;
		re.BlendSkeleton = RE_BlendSkeleton;
		re.BoneIndex = RE_BoneIndex;
		re.AnimNumFrames = RE_AnimNumFrames;
		re.AnimFrameRate = RE_AnimFrameRate;

		// XreaL END

		re.RegisterVisTest = RE_RegisterVisTest;
		re.AddVisTestToScene = RE_AddVisTestToScene;
		re.CheckVisibility = RE_CheckVisibility;
		re.UnregisterVisTest = RE_UnregisterVisTest;

		re.SetColorGrading = RE_SetColorGrading;

		re.SetAltShaderTokens = R_SetAltShaderTokens;

		re.GetTextureSize = RE_GetTextureSize;
		re.Add2dPolysIndexed = RE_2DPolyiesIndexed;
		re.GenerateTexture = RE_GenerateTexture;
		re.ShaderNameFromHandle = RE_GetShaderNameFromHandle;
		re.SendBotDebugDrawCommands = RE_SendBotDebugDrawCommands;

		return &re;
	}
