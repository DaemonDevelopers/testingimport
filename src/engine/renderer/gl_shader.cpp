/*
===========================================================================
Copyright (C) 2010-2011 Robert Beckebans <trebor_7@users.sourceforge.net>

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
// gl_shader.cpp -- GLSL shader handling

#include <common/FileSystem.h>
#include "gl_shader.h"
#include "Material.h"

// We currently write GLBinaryHeader to a file and memcpy all over it.
// Make sure it's a pod, so we don't put a std::string in it or something
// and try to memcpy over that or binary write an std::string to a file.
static_assert(std::is_pod<GLBinaryHeader>::value, "Value must be a pod while code in this cpp file reads and writes this object to file as binary.");

// set via command line args only since this allows arbitrary code execution
static Cvar::Cvar<std::string> shaderpath(
	"shaderpath", "path to load GLSL source files at runtime", Cvar::INIT | Cvar::TEMPORARY, "");

static Cvar::Cvar<bool> r_glslCache(
	"r_glslCache", "cache compiled GLSL shader binaries in the homepath", Cvar::NONE, true);

extern std::unordered_map<std::string, std::string> shadermap;
// shaderKind's value will be determined later based on command line setting or absence of.
ShaderKind shaderKind = ShaderKind::Unknown;

// *INDENT-OFF*

GLShader_generic                         *gl_genericShader = nullptr;
GLShader_genericMaterial                 *gl_genericShaderMaterial = nullptr;
GLShader_cull                            *gl_cullShader = nullptr;
GLShader_depthReduction                  *gl_depthReductionShader = nullptr;
GLShader_clearSurfaces                   *gl_clearSurfacesShader = nullptr;
GLShader_processSurfaces                 *gl_processSurfacesShader = nullptr;
GLShader_lightMapping                    *gl_lightMappingShader = nullptr;
GLShader_lightMappingMaterial            *gl_lightMappingShaderMaterial = nullptr;
GLShader_forwardLighting_omniXYZ         *gl_forwardLightingShader_omniXYZ = nullptr;
GLShader_forwardLighting_projXYZ         *gl_forwardLightingShader_projXYZ = nullptr;
GLShader_forwardLighting_directionalSun  *gl_forwardLightingShader_directionalSun = nullptr;
GLShader_shadowFill                      *gl_shadowFillShader = nullptr;
GLShader_reflection                      *gl_reflectionShader = nullptr;
GLShader_reflectionMaterial              *gl_reflectionShaderMaterial = nullptr;
GLShader_skybox                          *gl_skyboxShader = nullptr;
GLShader_skyboxMaterial                  *gl_skyboxShaderMaterial = nullptr;
GLShader_fogQuake3                       *gl_fogQuake3Shader = nullptr;
GLShader_fogQuake3Material               *gl_fogQuake3ShaderMaterial = nullptr;
GLShader_fogGlobal                       *gl_fogGlobalShader = nullptr;
GLShader_heatHaze                        *gl_heatHazeShader = nullptr;
GLShader_heatHazeMaterial                *gl_heatHazeShaderMaterial = nullptr;
GLShader_screen                          *gl_screenShader = nullptr;
GLShader_screenMaterial                  *gl_screenShaderMaterial = nullptr;
GLShader_portal                          *gl_portalShader = nullptr;
GLShader_contrast                        *gl_contrastShader = nullptr;
GLShader_cameraEffects                   *gl_cameraEffectsShader = nullptr;
GLShader_blur                           *gl_blurShader = nullptr;
GLShader_debugShadowMap                  *gl_debugShadowMapShader = nullptr;
GLShader_liquid                          *gl_liquidShader = nullptr;
GLShader_liquidMaterial                  *gl_liquidShaderMaterial = nullptr;
GLShader_motionblur                      *gl_motionblurShader = nullptr;
GLShader_ssao                            *gl_ssaoShader = nullptr;
GLShader_depthtile1                      *gl_depthtile1Shader = nullptr;
GLShader_depthtile2                      *gl_depthtile2Shader = nullptr;
GLShader_lighttile                       *gl_lighttileShader = nullptr;
GLShader_fxaa                            *gl_fxaaShader = nullptr;
GLShaderManager                           gl_shaderManager;

namespace // Implementation details
{
	NORETURN inline void ThrowShaderError(Str::StringRef msg)
	{
		throw ShaderException(msg.c_str());
	}

	const char* GetInternalShader(Str::StringRef filename)
	{
		auto it = shadermap.find(filename);
		if (it != shadermap.end())
			return it->second.c_str();
		return nullptr;
	}

	void CRLFToLF(std::string& source)
	{
		size_t sourcePos = 0;
		size_t keepPos = 0;

		auto keep = [&](size_t keepLength)
		{
			if (sourcePos > 0)
				std::copy(source.begin() + sourcePos, source.begin() + sourcePos + keepLength,
					source.begin() + keepPos);
			keepPos += keepLength;
		};

		for (;;)
		{
			size_t targetPos = source.find("\r\n", sourcePos);
			// If we don't find a line break, shuffle what's left
			// into place and we're done.
			if (targetPos == std::string::npos)
			{
				size_t remainingLength = source.length() - sourcePos;
				keep(remainingLength);
				break;
			}
			// If we do find a line break, shuffle what's before it into place
			// except for the '\r\n'. But then tack on a '\n',
			// resulting in effectively just losing the '\r' in the sequence.
			size_t keepLength = (targetPos - sourcePos);
			keep(keepLength);
			source[keepPos] = '\n';
			++keepPos;
			sourcePos = targetPos + 2;
		}
		source.resize(keepPos);
	}

	// CR/LF's can wind up in the raw files because of how
	// version control system works and how Windows works.
	// Remove them so we are always comparing apples with apples.
	void NormalizeShaderText( std::string& text )
	{
		// A windows user changing the shader file can put
		// Windows can put CRLF's in the file. Make them LF's.
		CRLFToLF(text);
	}

	std::string GetShaderFilename(Str::StringRef filename)
	{
		std::string shaderBase = GetShaderPath();
		if (shaderBase.empty())
			return shaderBase;
		std::string shaderFileName = FS::Path::Build(shaderBase, filename);
		return shaderFileName;
	}

	std::string GetShaderText(Str::StringRef filename)
	{
		// Shader type should be set during initialisation.
		if (shaderKind == ShaderKind::BuiltIn)
		{
			// Look for the shader internally. If not found, look for it externally.
			// If found neither internally or externally of if empty, then Error.
			auto text_ptr = GetInternalShader(filename);
			if (text_ptr == nullptr)
				ThrowShaderError(Str::Format("No shader found for shader: %s", filename));
			return text_ptr;
		}
		else if (shaderKind == ShaderKind::External)
		{
			std::string shaderText;
			std::string shaderFilename = GetShaderFilename(filename);

			Log::Notice("Loading shader '%s'", shaderFilename);

			std::error_code err;

			FS::File shaderFile = FS::RawPath::OpenRead(shaderFilename, err);
			if (err)
				ThrowShaderError(Str::Format("Cannot load shader from file %s: %s", shaderFilename, err.message()));

			shaderText = shaderFile.ReadAll(err);
			if (err)
				ThrowShaderError(Str::Format("Failed to read shader from file %s: %s", shaderFilename, err.message()));

			// Alert the user when a file does not match it's built-in version.
			// There should be no differences in normal conditions.
			// When testing shader file changes this is an expected message
			// and helps the tester track which files have changed and need
			// to be recommitted to git.
			// If one is not making shader files changes this message
			// indicates there is a mismatch between disk changes and builtins
			// which the application is out of sync with it's files
			// and he translation script needs to be run.
			auto textPtr = GetInternalShader(filename);
			std::string internalShaderText;
			if (textPtr != nullptr)
				internalShaderText = textPtr;

			// Note to the user any differences that might exist between
			// what's on disk and what's compiled into the program in shaders.cpp.
			// The developer should be aware of any differences why they exist but
			// they might be expected or unexpected.
			// If the developer made changes they might want to be reminded of what
			// they have changed while they are working.
			// But it also might be that the developer hasn't made any changes but
			// the compiled code is shaders.cpp is just out of sync with the shader
			// files and that buildshaders.sh might need to be run to re-sync.
			// This message alerts user to either situation and they can decide
			// what's going on from seeing that.
			// We normalize the text by removing CL/LF's so they aren't considered
			// a difference as Windows or the Version Control System can put them in
			// and another OS might read them back and consider that a difference
			// to what's in shader.cpp or vice vesa.
			NormalizeShaderText(internalShaderText);
			NormalizeShaderText(shaderText);
			if (internalShaderText != shaderText)
				Log::Warn("Note shader file differs from built-in shader: %s", shaderFilename);

			if (shaderText.empty())
				ThrowShaderError(Str::Format("Shader from file is empty: %s", shaderFilename));

			return shaderText;
		}
		ThrowShaderError("Internal error. ShaderKind not set.");
	}
}

std::string GetShaderPath()
{
	return shaderpath.Get();
}

GLShaderManager::~GLShaderManager()
= default;

void GLShaderManager::FreeAll() {
	_shaders.clear();

	deformShaderCount = 0;
	_deformShaderLookup.clear();

	for ( const ShaderPipelineDescriptor& pipeline : shaderPipelineDescriptors ) {
		if ( pipeline.id ) {
			glDeleteProgramPipelines( 1, &pipeline.id );
		}
	}

	shaderPipelineDescriptors.clear();

	for ( const ShaderProgramDescriptor& program : shaderProgramDescriptors ) {
		if ( program.id ) {
			glDeleteProgram( program.id );
		}

		if ( program.uniformLocations ) {
			Z_Free( program.uniformLocations );
		}

		if ( program.uniformBlockIndexes ) {
			Z_Free( program.uniformBlockIndexes );
		}

		if ( program.uniformFirewall ) {
			Z_Free( program.uniformFirewall );
		}
	}

	shaderProgramDescriptors.clear();

	for (  const ShaderDescriptor& shader : shaderDescriptors ) {
		if ( shader.id ) {
			glDeleteShader( shader.id );
		}
	}

	shaderDescriptors.clear();

	while ( !_shaderBuildQueue.empty() )
	{
		_shaderBuildQueue.pop();
	}
}

void GLShaderManager::UpdateShaderProgramUniformLocations( GLShader* shader, ShaderProgramDescriptor* shaderProgram ) const {
	size_t uniformSize = shader->_uniformStorageSize;
	size_t numUniforms = shader->_uniforms.size();
	size_t numUniformBlocks = shader->_uniformBlocks.size();

	// create buffer for storing uniform locations
	shaderProgram->uniformLocations = ( GLint* ) Z_Malloc( sizeof( GLint ) * numUniforms );

	// create buffer for uniform firewall
	shaderProgram->uniformFirewall = ( byte* ) Z_Malloc( uniformSize );

	// update uniforms
	for (GLUniform *uniform : shader->_uniforms)
	{
		uniform->UpdateShaderProgramUniformLocation( shaderProgram );
	}

	if( glConfig2.uniformBufferObjectAvailable ) {
		// create buffer for storing uniform block indexes
		shaderProgram->uniformBlockIndexes = ( GLuint* ) Z_Malloc( sizeof( GLuint ) * numUniformBlocks );

		// update uniform blocks
		for (GLUniformBlock *uniformBlock : shader->_uniformBlocks)
		{
			uniformBlock->UpdateShaderProgramUniformBlockIndex( shaderProgram );
		}
	}
}

static inline void AddDefine( std::string& defines, const std::string& define, int value )
{
	defines += Str::Format("#ifndef %s\n#define %s %d\n#endif\n", define, define, value);
}

// Epsilon for float is 5.96e-08, so exponential notation with 8 decimal places should give exact values.

static inline void AddDefine( std::string& defines, const std::string& define, float value )
{
	defines += Str::Format("#ifndef %s\n#define %s %.8e\n#endif\n", define, define, value);
}

static inline void AddDefine( std::string& defines, const std::string& define, float v1, float v2 )
{
	defines += Str::Format("#ifndef %s\n#define %s vec2(%.8e, %.8e)\n#endif\n", define, define, v1, v2);
}

static inline void AddDefine( std::string& defines, const std::string& define )
{
	defines += Str::Format("#ifndef %s\n#define %s\n#endif\n", define, define);
}

// Has to match enum genFunc_t in tr_local.h
static const char *const genFuncNames[] = {
	  "DSTEP_NONE",
	  "DSTEP_SIN",
	  "DSTEP_SQUARE",
	  "DSTEP_TRIANGLE",
	  "DSTEP_SAWTOOTH",
	  "DSTEP_INV_SAWTOOTH",
	  "DSTEP_NOISE"
};

static std::string BuildDeformSteps( deformStage_t *deforms, int numDeforms )
{
	std::string steps;

	steps.reserve(256); // Might help a little.

	steps += "#define DEFORM_STEPS ";
	for( int step = 0; step < numDeforms; step++ )
	{
		const deformStage_t &ds = deforms[ step ];

		switch ( ds.deformation )
		{
		case deform_t::DEFORM_WAVE:
			steps += "DSTEP_LOAD_POS(1.0, 1.0, 1.0) ";
			steps += Str::Format("%s(%f, %f, %f) ",
				    genFuncNames[ Util::ordinal(ds.deformationWave.func) ],
				    ds.deformationWave.phase,
				    ds.deformationSpread,
				    ds.deformationWave.frequency );
			steps += "DSTEP_LOAD_NORM(1.0, 1.0, 1.0) ";
			steps += Str::Format("DSTEP_MODIFY_POS(%f, %f, 1.0) ",
				    ds.deformationWave.base,
				    ds.deformationWave.amplitude );
			break;

		case deform_t::DEFORM_BULGE:
			steps += "DSTEP_LOAD_TC(1.0, 0.0, 0.0) ";
			steps += Str::Format("DSTEP_SIN(0.0, %f, %f) ",
				    ds.bulgeWidth,
				    ds.bulgeSpeed * 0.001f );
			steps += "DSTEP_LOAD_NORM(1.0, 1.0, 1.0) ";
			steps += Str::Format("DSTEP_MODIFY_POS(0.0, %f, 1.0) ",
				    ds.bulgeHeight );
			break;

		case deform_t::DEFORM_MOVE:
			steps += Str::Format("%s(%f, 0.0, %f) ",
				    genFuncNames[ Util::ordinal(ds.deformationWave.func) ],
				    ds.deformationWave.phase,
				    ds.deformationWave.frequency );
			steps += Str::Format("DSTEP_LOAD_VEC(%f, %f, %f) ",
				    ds.moveVector[ 0 ],
				    ds.moveVector[ 1 ],
				    ds.moveVector[ 2 ] );
			steps += Str::Format("DSTEP_MODIFY_POS(%f, %f, 1.0) ",
				    ds.deformationWave.base,
				    ds.deformationWave.amplitude );
			break;

		case deform_t::DEFORM_NORMALS:
			steps += "DSTEP_LOAD_POS(1.0, 1.0, 1.0) ";
			steps += Str::Format("DSTEP_NOISE(0.0, 0.0, %f) ",
				    ds.deformationWave.frequency );
			steps += Str::Format("DSTEP_MODIFY_NORM(0.0, %f, 1.0) ",
				    0.98f * ds.deformationWave.amplitude );
			break;

		case deform_t::DEFORM_ROTGROW:
			steps += "DSTEP_LOAD_POS(1.0, 1.0, 1.0) ";
			steps += Str::Format("DSTEP_ROTGROW(%f, %f, %f) ",
				    ds.moveVector[0],
				    ds.moveVector[1],
				    ds.moveVector[2] );
			steps += "DSTEP_LOAD_COLOR(1.0, 1.0, 1.0) ";
			steps += "DSTEP_MODIFY_COLOR(-1.0, 1.0, 0.0) ";
			break;

		default:
			break;
		}
	}

	return steps;
}

struct addedExtension_t {
	bool &available;
	int minGlslVersion;
	std::string name;
};

// Fragment and vertex version declaration.
static const std::vector<addedExtension_t> fragmentVertexAddedExtensions = {
	{ glConfig2.gpuShader4Available, 130, "EXT_gpu_shader4" },
	{ glConfig2.gpuShader5Available, 400, "ARB_gpu_shader5" },
	{ glConfig2.textureGatherAvailable, 400, "ARB_texture_gather" },
	{ glConfig2.textureIntegerAvailable, 0, "EXT_texture_integer" },
	{ glConfig2.textureRGAvailable, 0, "ARB_texture_rg" },
	{ glConfig2.uniformBufferObjectAvailable, 140, "ARB_uniform_buffer_object" },
	{ glConfig2.usingBindlessTextures, -1, "ARB_bindless_texture" },
	/* ARB_shader_draw_parameters set to -1, because we might get a 4.6 GL context,
	where the core variables have different names. */
	{ glConfig2.shaderDrawParametersAvailable, -1, "ARB_shader_draw_parameters" },
	{ glConfig2.SSBOAvailable, 430, "ARB_shader_storage_buffer_object" },
	/* Even though these are part of the GL_KHR_shader_subgroup extension, we need to enable
	the individual extensions for each feature.
	GL_KHR_shader_subgroup itself can't be used in the shader. */
	{ glConfig2.shaderSubgroupBasicAvailable, -1, "KHR_shader_subgroup_basic" },
	{ glConfig2.shaderSubgroupVoteAvailable, -1, "KHR_shader_subgroup_vote" },
	{ glConfig2.shaderSubgroupArithmeticAvailable, -1, "KHR_shader_subgroup_arithmetic" },
	{ glConfig2.shaderSubgroupBallotAvailable, -1, "KHR_shader_subgroup_ballot" },
	{ glConfig2.shaderSubgroupShuffleAvailable, -1, "KHR_shader_subgroup_shuffle" },
	{ glConfig2.shaderSubgroupShuffleRelativeAvailable, -1, "KHR_shader_subgroup_shuffle_relative" },
	{ glConfig2.shaderSubgroupQuadAvailable, -1, "KHR_shader_subgroup_quad" },
};

// Compute version declaration, this has to be separate from other shader stages,
// because some things are unique to vertex/fragment or compute shaders.
static const std::vector<addedExtension_t> computeAddedExtensions = {
	{ glConfig2.computeShaderAvailable, 430, "ARB_compute_shader" },
	{ glConfig2.gpuShader4Available, 130, "EXT_gpu_shader4" },
	{ glConfig2.gpuShader5Available, 400, "ARB_gpu_shader5" },
	{ glConfig2.uniformBufferObjectAvailable, 140, "ARB_uniform_buffer_object" },
	{ glConfig2.SSBOAvailable, 430, "ARB_shader_storage_buffer_object" },
	{ glConfig2.shadingLanguage420PackAvailable, 420, "ARB_shading_language_420pack" },
	{ glConfig2.explicitUniformLocationAvailable, 430, "ARB_explicit_uniform_location" },
	{ glConfig2.shaderImageLoadStoreAvailable, 420, "ARB_shader_image_load_store" },
	{ glConfig2.shaderAtomicCountersAvailable, 420, "ARB_shader_atomic_counters" },
	/* ARB_shader_atomic_counter_ops set to -1,
	because we might get a 4.6 GL context, where the core functions have different names. */
	{ glConfig2.shaderAtomicCounterOpsAvailable, -1, "ARB_shader_atomic_counter_ops" },
	{ glConfig2.usingBindlessTextures, -1, "ARB_bindless_texture" },
	/* Even though these are part of the GL_KHR_shader_subgroup extension, we need to enable
	the individual extensions for each feature.
	GL_KHR_shader_subgroup itself can't be used in the shader. */
	{ glConfig2.shaderSubgroupBasicAvailable, -1, "KHR_shader_subgroup_basic" },
	{ glConfig2.shaderSubgroupVoteAvailable, -1, "KHR_shader_subgroup_vote" },
	{ glConfig2.shaderSubgroupArithmeticAvailable, -1, "KHR_shader_subgroup_arithmetic" },
	{ glConfig2.shaderSubgroupBallotAvailable, -1, "KHR_shader_subgroup_ballot" },
	{ glConfig2.shaderSubgroupShuffleAvailable, -1, "KHR_shader_subgroup_shuffle" },
	{ glConfig2.shaderSubgroupShuffleRelativeAvailable, -1, "KHR_shader_subgroup_shuffle_relative" },
	{ glConfig2.shaderSubgroupQuadAvailable, -1, "KHR_shader_subgroup_quad" },
};

static void addExtension( std::string& str, bool available, int minGlslVersion, const std::string& name ) {
	if ( !available ) {
		return;
	}

	if ( ( glConfig2.shadingLanguageVersion < minGlslVersion ) || ( minGlslVersion == -1 ) ) {
		str += Str::Format( "#extension GL_%s : require\n", name );
	}

	str += Str::Format( "#define HAVE_%s 1\n", name );
}

static void AddConst( std::string& str, const std::string& name, int value )
{
	str += Str::Format("const int %s = %d;\n", name, value);
}

static void AddConst( std::string& str, const std::string& name, float value )
{
	str += Str::Format("const float %s = %.8e;\n", name, value);
}

static void AddConst( std::string& str, const std::string& name, float v1, float v2 )
{
	str += Str::Format("const vec2 %s = vec2(%.8e, %.8e);\n", name, v1, v2);
}

static std::string GenVersionDeclaration( const std::vector<addedExtension_t> &addedExtensions ) {
	// Declare version.
	std::string str = Str::Format( "#version %d %s\n\n",
		glConfig2.shadingLanguageVersion,
		glConfig2.shadingLanguageVersion >= 150 ? ( glConfig2.glCoreProfile ? "core" : "compatibility" ) : "" );

	// Add supported GLSL extensions.
	for ( const auto& addedExtension : addedExtensions ) {
		addExtension( str, addedExtension.available, addedExtension.minGlslVersion, addedExtension.name );
	}

	return str;
}

static std::string GenFragmentVertexVersionDeclaration() {
	return GenVersionDeclaration( fragmentVertexAddedExtensions );
}

static std::string GenComputeVersionDeclaration() {
	return GenVersionDeclaration( computeAddedExtensions );
}

static std::string GenCompatHeader() {
	std::string str;

	// definition of functions missing in early GLSL
	if( glConfig2.shadingLanguageVersion <= 120 ) {
		str += "float smoothstep(float edge0, float edge1, float x) { float t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }\n";
	}

	if ( !glConfig2.gpuShader5Available ) {
		str += "#define unpackUnorm4x8( value ) ( ( vec4( value, value >> 8, value >> 16, value >> 24 ) & 0xFF ) / 255.0f )\n";
	}

	/* Driver bug: Adrenaline/OGLP drivers fail to recognise the ARB function versions when they return a 4.6 context
	and the shaders get #version 460 core, so add #define's for them here */
	if ( glConfig2.driverVendor == glDriverVendor_t::ATI && glConfig2.shaderAtomicCounterOpsAvailable ) {
		str += "#define atomicCounterAddARB atomicCounterAdd\n";
		str += "#define atomicCounterSubtractARB atomicCounterSubtract\n";
		str += "#define atomicCounterMinARB atomicCounterMin\n";
		str += "#define atomicCounterMaxARB atomicCounterMax\n";
		str += "#define atomicCounterAndARB atomicCounterAnd\n";
		str += "#define atomicCounterOrARB atomicCounterOr\n";
		str += "#define atomicCounterXorARB atomicCounterXor\n";
		str += "#define atomicCounterExchangeARB atomicCounterExchange\n";
		str += "#define atomicCounterAndARB atomicCounterAnd\n";
	}

	return str;
}

static std::string GenVertexHeader() {
	std::string str;

	// Vertex shader compatibility defines
	if( glConfig2.shadingLanguageVersion > 120 ) {
		str =   "#define IN in\n"
			"#define OUT(mode) mode out\n"
			"#define textureCube texture\n"
			"#define texture2D texture\n"
			"#define texture2DProj textureProj\n"
			"#define texture3D texture\n";
	} else {
		str =   "#define IN attribute\n"
			"#define OUT(mode) varying\n";
	}

	if ( glConfig2.shaderDrawParametersAvailable ) {
		str += "OUT(flat) int in_drawID;\n";
		str += "OUT(flat) int in_baseInstance;\n";
		str += "#define drawID gl_DrawIDARB\n";
		str += "#define baseInstance gl_BaseInstanceARB\n\n";
	}

	if ( glConfig2.usingMaterialSystem ) {
		AddDefine( str, "BIND_MATERIALS", Util::ordinal( BufferBind::MATERIALS ) );
		AddDefine( str, "BIND_TEX_DATA", Util::ordinal( BufferBind::TEX_DATA ) );
		AddDefine( str, "BIND_LIGHTMAP_DATA", Util::ordinal( BufferBind::LIGHTMAP_DATA ) );
	}

	return str;
}

static std::string GenFragmentHeader() {
	std::string str;

	// Fragment shader compatibility defines
	if( glConfig2.shadingLanguageVersion > 120 ) {
		str =   "#define IN(mode) mode in\n"
			"#define DECLARE_OUTPUT(type) out type outputColor;\n"
			"#define textureCube texture\n"
			"#define texture2D texture\n"
			"#define texture2DProj textureProj\n"
			"#define texture3D texture\n";
	} else if( glConfig2.gpuShader4Available) {
		str =   "#define IN(mode) varying\n"
			"#define DECLARE_OUTPUT(type) varying out type outputColor;\n";
	} else {
		str =   "#define IN(mode) varying\n"
			"#define outputColor gl_FragColor\n"
			"#define DECLARE_OUTPUT(type) /* empty*/\n";
	}

	if ( glConfig2.usingBindlessTextures ) {
		str += "layout(bindless_sampler) uniform;\n";
	}

	if ( glConfig2.shaderDrawParametersAvailable ) {
		str += "IN(flat) int in_drawID;\n";
		str += "IN(flat) int in_baseInstance;\n";
		str += "#define drawID in_drawID\n";
		str += "#define baseInstance in_baseInstance\n\n";
	}

	if ( glConfig2.usingMaterialSystem ) {
		AddDefine( str, "BIND_MATERIALS", Util::ordinal( BufferBind::MATERIALS ) );
		AddDefine( str, "BIND_TEX_DATA", Util::ordinal( BufferBind::TEX_DATA ) );
		AddDefine( str, "BIND_LIGHTMAP_DATA", Util::ordinal( BufferBind::LIGHTMAP_DATA ) );
	}

	return str;
}

static std::string GenComputeHeader() {
	std::string str;

	// Compute shader compatibility defines
	if ( glConfig2.usingMaterialSystem ) {
		AddDefine( str, "MAX_VIEWS", MAX_VIEWS );
		AddDefine( str, "MAX_FRAMES", MAX_FRAMES );
		AddDefine( str, "MAX_VIEWFRAMES", MAX_VIEWFRAMES );
		AddDefine( str, "MAX_SURFACE_COMMAND_BATCHES", MAX_SURFACE_COMMAND_BATCHES );
		AddDefine( str, "MAX_COMMAND_COUNTERS", MAX_COMMAND_COUNTERS );

		AddDefine( str, "BIND_SURFACE_DESCRIPTORS", Util::ordinal( BufferBind::SURFACE_DESCRIPTORS ) );
		AddDefine( str, "BIND_SURFACE_COMMANDS", Util::ordinal( BufferBind::SURFACE_COMMANDS ) );
		AddDefine( str, "BIND_CULLED_COMMANDS", Util::ordinal( BufferBind::CULLED_COMMANDS ) );
		AddDefine( str, "BIND_SURFACE_BATCHES", Util::ordinal( BufferBind::SURFACE_BATCHES ) );
		AddDefine( str, "BIND_COMMAND_COUNTERS_ATOMIC", Util::ordinal( BufferBind::COMMAND_COUNTERS_ATOMIC ) );
		AddDefine( str, "BIND_COMMAND_COUNTERS_STORAGE", Util::ordinal( BufferBind::COMMAND_COUNTERS_STORAGE ) );
		AddDefine( str, "BIND_PORTAL_SURFACES", Util::ordinal( BufferBind::PORTAL_SURFACES ) );

		AddDefine( str, "BIND_DEBUG", Util::ordinal( BufferBind::DEBUG ) );
	}

	if ( glConfig2.usingBindlessTextures ) {
		str += "layout(bindless_image) uniform;\n";
	}

	return str;
}

static std::string GenWorldHeader() {
	std::string str;

	// Shader compatibility defines that use map data for compile-time values
	AddDefine( str, "MAX_SURFACE_COMMANDS", materialSystem.maxStages );

	return str;
}

static std::string GenEngineConstants() {
	// Engine constants
	std::string str;

	AddDefine( str, "r_AmbientScale", r_ambientScale.Get() );
	AddDefine( str, "r_SpecularScale", r_specularScale->value );
	AddDefine( str, "r_zNear", r_znear->value );

	AddDefine( str, "M_PI", static_cast< float >( M_PI ) );
	AddDefine( str, "MAX_SHADOWMAPS", MAX_SHADOWMAPS );
	AddDefine( str, "MAX_REF_LIGHTS", MAX_REF_LIGHTS );
	AddDefine( str, "NUM_LIGHT_LAYERS", glConfig2.realtimeLightLayers );
	AddDefine( str, "TILE_SIZE", TILE_SIZE );

	AddDefine( str, "r_FBufSize", glConfig.vidWidth, glConfig.vidHeight );

	AddDefine( str, "r_tileStep", glState.tileStep[0], glState.tileStep[1] );

	if ( glConfig2.shadowMapping )
	{
		switch( glConfig2.shadowingMode )
		{
			case shadowingMode_t::SHADOWING_ESM16:
			case shadowingMode_t::SHADOWING_ESM32:
				AddDefine( str, "ESM", 1 );
				break;
			case shadowingMode_t::SHADOWING_VSM16:
			case shadowingMode_t::SHADOWING_VSM32:
				AddDefine( str, "VSM", 1 );

				if ( glConfig.hardwareType == glHardwareType_t::GLHW_R300 )
				{
					AddDefine( str, "VSM_CLAMP", 1 );
				}
				break;
			case shadowingMode_t::SHADOWING_EVSM32:
				AddDefine( str, "EVSM", 1 );

				// The exponents for the EVSM techniques should be less than ln(FLT_MAX/FILTER_SIZE)/2 {ln(FLT_MAX/1)/2 ~44.3}
				//         42.9 is the maximum possible value for FILTER_SIZE=15
				//         42.0 is the truncated value that we pass into the sample
				AddConst( str, "r_EVSMExponents", 42.0f, 42.0f );

				if ( r_evsmPostProcess->integer )
				{
					AddDefine( str, "r_EVSMPostProcess", 1 );
				}
				break;
			default:
				DAEMON_ASSERT( false );
				break;
		}

		switch( glConfig2.shadowingMode )
		{
			case shadowingMode_t::SHADOWING_ESM16:
			case shadowingMode_t::SHADOWING_ESM32:
				break;
			case shadowingMode_t::SHADOWING_VSM16:
				AddConst( str, "VSM_EPSILON", 0.0001f );
				break;
			case shadowingMode_t::SHADOWING_VSM32:
				// GLHW_R300 should not be GLDRV_OPENGL3 anyway.
				if ( glConfig.driverType != glDriverType_t::GLDRV_OPENGL3
					|| glConfig.hardwareType == glHardwareType_t::GLHW_R300 )
				{
					AddConst( str, "VSM_EPSILON", 0.0001f );
				}
				else
				{
					AddConst( str, "VSM_EPSILON", 0.000001f );
				}
				break;
			case shadowingMode_t::SHADOWING_EVSM32:
				// This may be wrong, but the code did that before it was rewritten.
				AddConst( str, "VSM_EPSILON", 0.0001f );
				break;
			default:
				DAEMON_ASSERT( false );
				break;
		}

		if ( r_lightBleedReduction->value )
			AddConst( str, "r_lightBleedReduction", r_lightBleedReduction->value );

		if ( r_overDarkeningFactor->value )
			AddConst( str, "r_overDarkeningFactor", r_overDarkeningFactor->value );

		if ( r_shadowMapDepthScale->value )
			AddConst( str, "r_shadowMapDepthScale", r_shadowMapDepthScale->value );

		if ( r_debugShadowMaps->integer )
			AddDefine( str, "r_debugShadowMaps", r_debugShadowMaps->integer );

		if ( r_softShadows->integer == 6 )
			AddDefine( str, "PCSS", 1 );
		else if ( r_softShadows->integer )
			AddConst( str, "r_PCFSamples", r_softShadows->value + 1.0f );

		if ( r_parallelShadowSplits->integer )
			AddDefine( str, Str::Format( "r_parallelShadowSplits_%d", r_parallelShadowSplits->integer ) );

		if ( r_showParallelShadowSplits->integer )
			AddDefine( str, "r_showParallelShadowSplits", 1 );
	}

	if ( glConfig2.realtimeLighting )
	{
		AddDefine( str, "r_realtimeLighting", 1 );
		AddDefine( str, "r_realtimeLightingRenderer", r_realtimeLightingRenderer.Get() );
	}

	if ( r_showNormalMaps->integer )
	{
		AddDefine( str, "r_showNormalMaps", 1 );
	}
	else if ( r_showMaterialMaps->integer )
	{
		AddDefine( str, "r_showMaterialMaps", 1 );
	}
	else if ( r_showLightMaps->integer )
	{
		AddDefine( str, "r_showLightMaps", 1 );
	}
	else if ( r_showDeluxeMaps->integer )
	{
		AddDefine( str, "r_showDeluxeMaps", 1 );
	}
	else if ( r_showReflectionMaps.Get() )
	{
		AddDefine( str, "r_showReflectionMaps", 1 );
	}
	else if ( r_showVertexColors.Get() )
	{
		AddDefine( str, "r_showVertexColors", 1 );
	}
	else if ( r_showGlobalMaterials.Get() != 0 )
	{
		AddDefine( str, "r_showGlobalMaterials", r_showGlobalMaterials.Get() );
	}

	if( r_showCubeProbes.Get() )
	{
		AddDefine( str, "r_showCubeProbes", 1 );
	}

	if ( r_materialDebug.Get() )
	{
		AddDefine( str, "r_materialDebug", 1 );
	}

	if ( r_profilerRenderSubGroups.Get() )
	{
		AddDefine( str, "r_profilerRenderSubGroups", 1 );
	}

	if ( glConfig2.vboVertexSkinningAvailable )
	{
		AddDefine( str, "r_vertexSkinning", 1 );
		AddConst( str, "MAX_GLSL_BONES", glConfig2.maxVertexSkinningBones );
	}
	else
	{
		AddConst( str, "MAX_GLSL_BONES", 4 );
	}

	if ( r_halfLambertLighting->integer )
		AddDefine( str, "r_halfLambertLighting", 1 );

	if ( r_rimLighting->integer )
	{
		AddDefine( str, "r_rimLighting", 1 );
		AddConst( str, "r_RimExponent", r_rimExponent->value );
	}

	if ( r_showLightTiles->integer )
	{
		AddDefine( str, "r_showLightTiles", 1 );
	}

	if ( glConfig2.normalMapping )
	{
		AddDefine( str, "r_normalMapping", 1 );
	}

	if ( r_liquidMapping->integer )
	{
		AddDefine( str, "r_liquidMapping", 1 );
	}

	if ( glConfig2.specularMapping )
	{
		AddDefine( str, "r_specularMapping", 1 );
	}

	if ( glConfig2.physicalMapping )
	{
		AddDefine( str, "r_physicalMapping", 1 );
	}

	if ( r_glowMapping->integer )
	{
		AddDefine( str, "r_glowMapping", 1 );
	}

	if ( glConfig2.colorGrading )
	{
		AddDefine( str, "r_colorGrading", 1 );
	}

	return str;
}

void GLShaderManager::InitDriverInfo()
{
	std::string driverInfo = std::string(glConfig.renderer_string) + glConfig.version_string;
	_driverVersionHash = Com_BlockChecksum(driverInfo.c_str(), static_cast<int>(driverInfo.size()));
	_shaderBinaryCacheInvalidated = false;
}

void GLShaderManager::GenerateBuiltinHeaders() {
	GLVersionDeclaration = GLHeader("GLVersionDeclaration", GenFragmentVertexVersionDeclaration(), this);
	GLComputeVersionDeclaration = GLHeader( "GLComputeVersionDeclaration", GenComputeVersionDeclaration(), this );
	GLCompatHeader = GLHeader("GLCompatHeader", GenCompatHeader(), this);
	GLVertexHeader = GLHeader("GLVertexHeader", GenVertexHeader(), this);
	GLFragmentHeader = GLHeader("GLFragmentHeader", GenFragmentHeader(), this);
	GLComputeHeader = GLHeader( "GLComputeHeader", GenComputeHeader(), this );
	GLWorldHeader = GLHeader( "GLWorldHeader", GenWorldHeader(), this );
	GLEngineConstants = GLHeader("GLEngineConstants", GenEngineConstants(), this);
}

void GLShaderManager::GenerateWorldHeaders() {
	GLWorldHeader = GLHeader( "GLWorldHeader", GenWorldHeader(), this );
}

std::string GLShaderManager::GetDeformShaderName( const int index ) {
	if ( ( !tr.world && !tr.loadingMap.size() ) || !index ) {
		return Str::Format( "deformVertexes_%i", index );
	}

	if ( !tr.world ) {
		return Str::Format( "deformVertexes_%s_%i", tr.loadingMap, index );
	}

	return Str::Format( "deformVertexes_%s_%i", tr.world->baseName, index );
}

std::string GLShaderManager::BuildDeformShaderText( const std::string& steps ) {
	std::string shaderText;

	shaderText = steps + "\n";
	shaderText += GetShaderText( "deformVertexes_vp.glsl" );

	return shaderText;
}

int GLShaderManager::GetDeformShaderIndex( deformStage_t *deforms, int numDeforms ) {
	std::string steps = BuildDeformSteps( deforms, numDeforms );
	uint32_t index = _deformShaderLookup[steps];

	if( !index ) {
		std::string shaderText = GLShaderManager::BuildDeformShaderText( steps );
		index = deformShaderCount;
		FindShader( GetDeformShaderName( index ), shaderText, GL_VERTEX_SHADER,
			std::vector<GLHeader*> { &GLVersionDeclaration, &GLVertexHeader } );

		deformShaderCount++;
		_deformShaderLookup[steps] = deformShaderCount;
	}

	return index;
}

static bool IsUnusedPermutation( const char *compileMacros )
{
	const char* token;
	while ( *( token = COM_ParseExt2( &compileMacros, false ) ) )
	{
		if ( strcmp( token, "USE_DELUXE_MAPPING" ) == 0 )
		{
			if ( !glConfig2.deluxeMapping ) return true;
		}
		else if ( strcmp( token, "USE_GRID_DELUXE_MAPPING" ) == 0 )
		{
			if ( !glConfig2.deluxeMapping ) return true;
		}
		else if ( strcmp( token, "USE_PHYSICAL_MAPPING" ) == 0 )
		{
			if ( !glConfig2.physicalMapping ) return true;
		}
		else if ( strcmp( token, "USE_REFLECTIVE_SPECULAR" ) == 0 )
		{
			/* FIXME: add to the following test: && r_physicalMapping->integer == 0
			when reflective specular is implemented for physical mapping too
			see https://github.com/DaemonEngine/Daemon/issues/355 */
			if ( !glConfig2.specularMapping ) return true;
		}
		else if ( strcmp( token, "USE_RELIEF_MAPPING" ) == 0 )
		{
			if ( !glConfig2.reliefMapping ) return true;
		}
		else if ( strcmp( token, "USE_HEIGHTMAP_IN_NORMALMAP" ) == 0 )
		{
			if ( !glConfig2.reliefMapping && !glConfig2.normalMapping ) return true;
		}
	}

	return false;
}

void GLShaderManager::BuildShader( ShaderDescriptor* descriptor ) {
	if ( descriptor->id ) {
		return;
	}

	const int start = Sys::Milliseconds();

	const GLchar* text[1] = { descriptor->shaderSource.data() };
	GLint length[1] = { ( GLint ) descriptor->shaderSource.size() };

	GLuint shader = glCreateShader( descriptor->type );
	GL_CheckErrors();

	glShaderSource( shader, 1, text, length );
	glCompileShader( shader );

	GL_CheckErrors();

	GLint compiled;
	glGetShaderiv( shader, GL_COMPILE_STATUS, &compiled );

	if ( !compiled ) {
		std::string log = GetInfoLog( shader );
		std::vector<InfoLogEntry> infoLog = ParseInfoLog( log );
		PrintShaderSource( descriptor->name, shader, infoLog );

		Log::Warn( "Compile log:\n%s", log );

		switch ( descriptor->type ) {
			case GL_VERTEX_SHADER:
				ThrowShaderError( Str::Format( "Couldn't compile vertex shader: %s", descriptor->name ) );
			case GL_FRAGMENT_SHADER:
				ThrowShaderError( Str::Format( "Couldn't compile fragment shader: %s", descriptor->name ) );
			case GL_COMPUTE_SHADER:
				ThrowShaderError( Str::Format( "Couldn't compile compute shader: %s", descriptor->name ) );
			default:
				break;
		}
	}

	descriptor->id = shader;

	const int time = Sys::Milliseconds() - start;
	compileTime += time;
	compileCount++;
	Log::Debug( "Compilation: %i", time );
}

void GLShaderManager::BuildShaderProgram( ShaderProgramDescriptor* descriptor ) {
	if ( descriptor->id ) {
		return;
	}

	const int start = Sys::Milliseconds();

	GLuint program = glCreateProgram();
	GL_CheckErrors();

	for ( const GLuint& shader : descriptor->shaders ) {
		if ( shader ) {
			glAttachShader( program, shader );
		} else {
			break;
		}
	}
	GL_CheckErrors();

	BindAttribLocations( program );

	if ( glConfig2.getProgramBinaryAvailable ) {
		glProgramParameteri( program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE );
	}

	if ( glConfig2.separateShaderObjectsAvailable ) {
		glProgramParameteri( program, GL_PROGRAM_SEPARABLE, GL_TRUE );
	}

	GLint linked;
	glLinkProgram( program );

	glGetProgramiv( program, GL_LINK_STATUS, &linked );

	if ( !linked ) {
		Log::Warn( "Link log:" );
		Log::Warn( GetInfoLog( program ) );
		ThrowShaderError( "Shader program failed to link!" );
	}

	descriptor->id = program;

	const int time = Sys::Milliseconds() - start;
	linkTime += time;
	linkCount++;
	Log::Debug( "Program creation + linking: %i", time );
}

ShaderProgramDescriptor* GLShaderManager::FindShaderProgram( std::vector<ShaderEntry>& shaders, GLShader* mainShader,
	GLShader::UniformData* uniformData ) {
	std::vector<ShaderProgramDescriptor>::iterator it = std::find_if( shaderProgramDescriptors.begin(), shaderProgramDescriptors.end(),
		[&]( const ShaderProgramDescriptor& program ) {
			for ( const ShaderEntry& shader : shaders ) {
				if ( std::find( program.shaderNames, program.shaderNames + program.shaderCount, shader )
					== program.shaderNames + program.shaderCount ) {
					return false;
				}
			}

			return true;
		}
	);

	if ( it == shaderProgramDescriptors.end() ) {
		std::sort( shaders.begin(), shaders.end(),
			[]( const ShaderEntry& lhs, const ShaderEntry& rhs ) {
				return lhs.name < rhs.name;
			}
		);

		ShaderProgramDescriptor desc;
		std::string combinedShaderText;
		std::vector<ShaderDescriptor*> buildQueue;
		buildQueue.reserve( shaders.size() );

		for ( const ShaderEntry& shader : shaders ) {
			std::vector<ShaderDescriptor>::iterator shaderIt = std::find_if( shaderDescriptors.begin(), shaderDescriptors.end(),
				[&]( const ShaderDescriptor& other ) {
					return shader.type == other.type && shader.macro == other.macro && shader.name == other.name;
				}
			);

			if ( shaderIt == shaderDescriptors.end() ) {
				ThrowShaderError( Str::Format( "Shader not found: %s %u", shader.name, shader.macro ) );
			}

			buildQueue.emplace_back( &*shaderIt );

			combinedShaderText += shaderIt->shaderSource;
		}

		desc.checkSum = Com_BlockChecksum( combinedShaderText.c_str(), combinedShaderText.length() );

		if ( !LoadShaderBinary( shaders, mainShader->_name, &desc ) ) {
			for ( ShaderDescriptor* shader : buildQueue ) {
				BuildShader( shader );
				desc.AttachShader( &*shader );
			}
			BuildShaderProgram( &desc );
			SaveShaderBinary( &desc );
		}

		UpdateShaderProgramUniformLocations( mainShader, &desc );
		uniformData->uniformLocations = desc.uniformLocations;
		uniformData->uniformBlockIndexes = desc.uniformBlockIndexes;
		uniformData->uniformFirewall = desc.uniformFirewall;
		GL_BindProgram( desc.id, true );
		mainShader->SetShaderProgramUniforms( &desc );
		GL_BindNullProgram( true );

		shaderProgramDescriptors.emplace_back( desc );

		return &shaderProgramDescriptors[shaderProgramDescriptors.size() - 1];
	}

	return &*it;
}

ShaderPipelineDescriptor* GLShaderManager::FindShaderPipelines(
	std::vector<ShaderEntry>& vertexShaders, std::vector<ShaderEntry>& fragmentShaders,
	std::vector<ShaderEntry>& computeShaders,
	GLShader* mainShader, uint32_t permutation ) {
	std::vector<ShaderPipelineDescriptor>::iterator it = std::find_if( shaderPipelineDescriptors.begin(), shaderPipelineDescriptors.end(),
		[&]( const ShaderPipelineDescriptor& pipeline ) {
			for ( const ShaderEntry& shader : vertexShaders ) {
				if ( std::find( pipeline.shaderNames, pipeline.shaderNames + pipeline.shaderCount, shader )
					== pipeline.shaderNames + pipeline.shaderCount ) {
					return false;
				}
			}

			for ( const ShaderEntry& shader : fragmentShaders ) {
				if ( std::find( pipeline.shaderNames, pipeline.shaderNames + pipeline.shaderCount, shader )
					== pipeline.shaderNames + pipeline.shaderCount ) {
					return false;
				}
			}

			for ( const ShaderEntry& shader : computeShaders ) {
				if ( std::find( pipeline.shaderNames, pipeline.shaderNames + pipeline.shaderCount, shader )
					== pipeline.shaderNames + pipeline.shaderCount ) {
					return false;
				}
			}

			return true;
		}
	);

	if ( it == shaderPipelineDescriptors.end() ) {
		ShaderPipelineDescriptor descriptor;
		glGenProgramPipelines( 1, &descriptor.id );

		GL_CheckErrors();
		if ( vertexShaders.size() ) {
			ShaderProgramDescriptor* program =
				FindShaderProgram( vertexShaders, mainShader, &mainShader->uniformsData[permutation * 3] );
			mainShader->uniformsData[permutation * 3].program = program->id;
			mainShader->uniformsData[permutation * 3].uniformLocations = program->uniformLocations;
			mainShader->uniformsData[permutation * 3].uniformBlockIndexes = program->uniformBlockIndexes;
			mainShader->uniformsData[permutation * 3].uniformFirewall = program->uniformFirewall;
			descriptor.AttachProgram( program );
			descriptor.VSProgram = *program;

			GL_CheckErrors();

			glUseProgramStages( descriptor.id, GL_VERTEX_SHADER_BIT, program->id );
			GL_CheckErrors();
		}
		if ( fragmentShaders.size() ) {
			ShaderProgramDescriptor* program =
				FindShaderProgram( fragmentShaders, mainShader, &mainShader->uniformsData[permutation * 3 + 1] );
			mainShader->uniformsData[permutation * 3 + 1].program = program->id;
			mainShader->uniformsData[permutation * 3 + 1].uniformLocations = program->uniformLocations;
			mainShader->uniformsData[permutation * 3 + 1].uniformBlockIndexes = program->uniformBlockIndexes;
			mainShader->uniformsData[permutation * 3 + 1].uniformFirewall = program->uniformFirewall;
			descriptor.AttachProgram( program );
			descriptor.FSProgram = *program;

			GL_CheckErrors();

			glUseProgramStages( descriptor.id, GL_FRAGMENT_SHADER_BIT, program->id );
			GL_CheckErrors();
		}
		if ( computeShaders.size() ) {
			ShaderProgramDescriptor* program =
				FindShaderProgram( computeShaders, mainShader, &mainShader->uniformsData[permutation * 3 + 2] );
			mainShader->uniformsData[permutation * 3 + 2].program = program->id;
			mainShader->uniformsData[permutation * 3 + 2].uniformLocations = program->uniformLocations;
			mainShader->uniformsData[permutation * 3 + 2].uniformBlockIndexes = program->uniformBlockIndexes;
			mainShader->uniformsData[permutation * 3 + 2].uniformFirewall = program->uniformFirewall;
			descriptor.AttachProgram( program );
			descriptor.CSProgram = *program;

			GL_CheckErrors();

			glUseProgramStages( descriptor.id, GL_COMPUTE_SHADER_BIT, program->id );
			GL_CheckErrors();
		}

		shaderPipelineDescriptors.emplace_back( descriptor );
		return &shaderPipelineDescriptors.back();
	}

	if ( it->VSProgram.id ) {
		mainShader->uniformsData[permutation * 3].program = it->VSProgram.id;
		mainShader->uniformsData[permutation * 3].uniformLocations = it->VSProgram.uniformLocations;
		mainShader->uniformsData[permutation * 3].uniformBlockIndexes = it->VSProgram.uniformBlockIndexes;
		mainShader->uniformsData[permutation * 3].uniformFirewall = it->VSProgram.uniformFirewall;
	}
	if ( it->FSProgram.id ) {
		mainShader->uniformsData[permutation * 3 + 1].program = it->FSProgram.id;
		mainShader->uniformsData[permutation * 3 + 1].uniformLocations = it->FSProgram.uniformLocations;
		mainShader->uniformsData[permutation * 3 + 1].uniformBlockIndexes = it->FSProgram.uniformBlockIndexes;
		mainShader->uniformsData[permutation * 3 + 1].uniformFirewall = it->FSProgram.uniformFirewall;
	}
	if ( it->CSProgram.id ) {
		mainShader->uniformsData[permutation * 3 + 2].program = it->CSProgram.id;
		mainShader->uniformsData[permutation * 3 + 2].uniformLocations = it->CSProgram.uniformLocations;
		mainShader->uniformsData[permutation * 3 + 2].uniformBlockIndexes = it->CSProgram.uniformBlockIndexes;
		mainShader->uniformsData[permutation * 3 + 2].uniformFirewall = it->CSProgram.uniformFirewall;
	}
	return &*it;
}

bool GLShaderManager::BuildPermutation( GLShader* shader, int macroIndex, int deformIndex ) {
	size_t i = macroIndex + ( deformIndex << shader->_compileMacros.size() );

	std::string compileMacros;
	if ( !shader->GetCompileMacrosString( i, compileMacros, GLCompileMacro::VERTEX | GLCompileMacro::FRAGMENT ) ) {
		return false;
	}

	if ( IsUnusedPermutation( compileMacros.c_str() ) ) {
		return false;
	}

	// Program already exists
	if ( i < shader->shaderPipelines.size() &&
		shader->shaderPipelines[i] ) {
		return false;
	}

	Log::Debug( "Building %s shader permutation with macro: %s",
		shader->GetName(),
		compileMacros.empty() ? "none" : compileMacros );

	const int start = Sys::Milliseconds();

	if ( i >= shader->shaderPipelines.size() ) {
		shader->shaderPipelines.resize( ( deformIndex + 1 ) << shader->_compileMacros.size() );
		shader->uniformsData.resize( ( ( deformIndex + 1 ) << shader->_compileMacros.size() ) * 3 );
	}

	std::vector<ShaderEntry> vertexShaders;
	std::vector<ShaderEntry> fragmentShaders;
	std::vector<ShaderEntry> computeShaders;
	if ( shader->_hasVertexShader ) {
		const uint32_t macros = shader->GetUniqueCompileMacros( macroIndex, GLCompileMacro::VERTEX );
		vertexShaders.emplace_back( ShaderEntry{ shader->_name, macros, GL_VERTEX_SHADER } );
		vertexShaders.emplace_back( ShaderEntry{ GetDeformShaderName( deformIndex ), 0, GL_VERTEX_SHADER } );
	}
	if ( shader->_hasFragmentShader ) {
		const uint32_t macros = shader->GetUniqueCompileMacros( macroIndex, GLCompileMacro::FRAGMENT );
		fragmentShaders.emplace_back( ShaderEntry{ shader->_name, macros, GL_FRAGMENT_SHADER } );
	}
	if ( shader->_hasComputeShader ) {
		computeShaders.emplace_back( ShaderEntry{ shader->_name, 0, GL_COMPUTE_SHADER } );
	}

	GL_CheckErrors();
	if ( glConfig2.separateShaderObjectsAvailable ) {
		ShaderPipelineDescriptor* pipeline = FindShaderPipelines( vertexShaders, fragmentShaders, computeShaders, shader,
			i );

		shader->shaderPipelines[i] = pipeline->id;
	} else {
		std::vector<ShaderEntry> shaders;
		shaders.reserve( vertexShaders.size() + fragmentShaders.size() + computeShaders.size() );
		shaders.insert( shaders.end(), vertexShaders.begin(), vertexShaders.end() );
		shaders.insert( shaders.end(), fragmentShaders.begin(), fragmentShaders.end() );
		shaders.insert( shaders.end(), computeShaders.begin(), computeShaders.end() );

		ShaderProgramDescriptor* program = FindShaderProgram( shaders, shader, &shader->uniformsData[i * 3] );

		shader->shaderPipelines[i] = program->id;
	}

	GL_CheckErrors();

	Log::Debug( "Built in: %i ms", Sys::Milliseconds() - start );

	return true;
}

void GLShaderManager::BuildAll() {
	int startTime = Sys::Milliseconds();
	int count = 0;
	compileTime = 0;
	compileCount = 0;
	linkTime = 0;
	linkCount = 0;

	while ( !_shaderBuildQueue.empty() ) {
		GLShader* shader = _shaderBuildQueue.front();

		std::string shaderName = shader->GetMainShaderName();

		size_t numPermutations = static_cast<size_t>( 1 ) << shader->GetNumOfCompiledMacros();

		for( size_t i = 0; i < numPermutations; i++ ) {
			count += +BuildPermutation( shader, i, 0 );
		}

		_shaderBuildQueue.pop();
	}

	// doesn't include deform vertex shaders, those are built elsewhere!
	Log::Notice( "Built %u glsl shader programs in %i ms (compile: %u in %i ms, link: %u in %i ms, init: %u in %i ms;"
		" cache: loaded %u in %i ms, saved %u in %i ms)",
		count, Sys::Milliseconds() - startTime,
		compileCount, compileTime, linkCount, linkTime, initCount, initTime,
		cacheLoadCount, cacheLoadTime, cacheSaveCount, cacheSaveTime );
}

std::string GLShaderManager::ProcessInserts( const std::string& shaderText ) const {
	std::string out;
	std::istringstream shaderTextStream( shaderText );

	std::string line;
	int insertCount = 0;
	int lineCount = 0;

	while ( std::getline( shaderTextStream, line, '\n' ) ) {
		++lineCount;
		const std::string::size_type position = line.find( "#insert" );
		if ( position == std::string::npos || line.find_first_not_of( " \t" ) != position ) {
			out += line + "\n";
			continue;
		}

		std::string shaderInsertPath = line.substr( position + 8, std::string::npos );

		// Inserted shader lines will start at 10000, 20000 etc. to easily tell them apart from the main shader code
		// #insert recursion is not supported
		++insertCount;
		out += "#line " + std::to_string( insertCount * 10000 ) + " // " + shaderInsertPath + ".glsl\n";

		out += GetShaderText( shaderInsertPath + ".glsl" );
		out += "#line " + std::to_string( lineCount ) + "\n";
	}

	return out;
}

ShaderDescriptor* GLShaderManager::FindShader( const std::string& name, const std::string& mainText,
	const GLenum type, const std::vector<GLHeader*>& headers,
	const uint32_t macro, const std::string& compileMacros, const bool main ) {

	ShaderDescriptor desc{ name, compileMacros, macro, type, main };
	const std::vector<ShaderDescriptor>::iterator it = std::find_if( shaderDescriptors.begin(), shaderDescriptors.end(),
		[&]( const ShaderDescriptor& other ) {
			return desc.type == other.type && desc.macro == other.macro && desc.name == other.name;
		}
	);

	if ( it != shaderDescriptors.end() ) {
		return nullptr;
	}

	std::string combinedShaderText = BuildShaderText( mainText, headers, compileMacros );
	combinedShaderText = ProcessInserts( combinedShaderText );

	desc.shaderSource = combinedShaderText;

	shaderDescriptors.emplace_back( desc );

	return &shaderDescriptors.back();
}

std::string GLShaderManager::BuildShaderText( const std::string& mainShaderText, const std::vector<GLHeader*>& headers,
	const std::string& macros ) {
	std::string combinedText;

	uint32_t count = 0;
	for ( GLHeader* header : headers ) {
		count += header->getText().size();
	}

	combinedText.reserve( count );

	for ( GLHeader* header : headers ) {
		combinedText += header->getText();
	}

	const char* compileMacrosP = macros.c_str();
	while ( true ) {
		const char* token = COM_ParseExt2( &compileMacrosP, false );

		if ( !token[0] ) {
			break;
		}

		combinedText += Str::Format( "#ifndef %s\n#define %s 1\n#endif\n", token, token );
	}

	combinedText += mainShaderText;

	return combinedText;
}

void GLShaderManager::InitShader( GLShader* shader ) {
	const int start = Sys::Milliseconds();

	shader->PostProcessUniforms();

	shader->_uniformStorageSize = 0;
	for ( std::size_t i = 0; i < shader->_uniforms.size(); i++ ) {
		GLUniform* uniform = shader->_uniforms[i];
		uniform->SetLocationIndex( i );
		uniform->SetFirewallIndex( shader->_uniformStorageSize );
		shader->_uniformStorageSize += uniform->GetSize();
	}

	for ( std::size_t i = 0; i < shader->_uniformBlocks.size(); i++ ) {
		GLUniformBlock* uniformBlock = shader->_uniformBlocks[i];
		uniformBlock->SetLocationIndex( i );
	}

	struct ShaderType {
		bool enabled;
		int type;
		GLenum GLType;

		const char* postfix;
		uint32_t offset;
		std::vector<GLHeader*> headers;

		std::string path;
		std::string mainText;
	};

	ShaderType shaderTypes[] = {
		{ shader->_hasVertexShader, GLCompileMacro::VERTEX, GL_VERTEX_SHADER, "_vp",
			GLVersionDeclaration.getText().size(),
			{ &GLVersionDeclaration, &GLCompatHeader, &GLEngineConstants, &GLVertexHeader } },
		{ shader->_hasFragmentShader, GLCompileMacro::FRAGMENT, GL_FRAGMENT_SHADER, "_fp",
			GLVersionDeclaration.getText().size(),
			{ &GLVersionDeclaration, &GLCompatHeader, &GLEngineConstants, &GLFragmentHeader } },
		{ shader->_hasComputeShader, GLCompileMacro::COMPUTE, GL_COMPUTE_SHADER, "_cp",
			GLComputeVersionDeclaration.getText().size(),
			{ &GLComputeVersionDeclaration, &GLCompatHeader, &GLEngineConstants, &GLComputeHeader, &GLWorldHeader } }
	};

	char filename[MAX_QPATH];
	for ( ShaderType& shaderType : shaderTypes ) {
		if ( shaderType.enabled ) {
			Com_sprintf( filename, sizeof( filename ), "%s%s.glsl", shader->GetMainShaderName().c_str(), shaderType.postfix );

			shaderType.path = filename;
			shaderType.mainText = GetShaderText( filename );
		}
	}

	for ( uint32_t i = 0; i < BIT( shader->GetNumOfCompiledMacros() ); i++ ) {
		for ( ShaderType& shaderType : shaderTypes ) {
			if ( !shaderType.enabled ) {
				continue;
			}

			std::string compileMacros;
			if ( !shader->GetCompileMacrosString( i, compileMacros, shaderType.type ) ) {
				continue;
			}

			if ( IsUnusedPermutation( compileMacros.c_str() ) ) {
				continue;
			}

			shader->BuildShaderCompileMacros( compileMacros );

			const uint32_t uniqueMacros = shader->GetUniqueCompileMacros( i, shaderType.type );

			ShaderDescriptor* desc = FindShader( shader->_name, shaderType.mainText, shaderType.GLType, shaderType.headers,
				uniqueMacros, compileMacros, true );

			if ( desc && glConfig2.usingMaterialSystem && shader->_useMaterialSystem ) {
				desc->shaderSource = ShaderPostProcess( shader, desc->shaderSource, shaderType.offset );
			}

			initCount++;
		}
	}

	initTime += Sys::Milliseconds() - start;
}

bool GLShaderManager::LoadShaderBinary( const std::vector<ShaderEntry>& shaders, const std::string& mainShader,
	ShaderProgramDescriptor* descriptor ) {
	if ( !r_glslCache.Get() ) {
		return false;
	}

	if ( !GetShaderPath().empty() ) {
		return false;
	}

	// Don't even try if the necessary functions aren't available
	if ( !glConfig2.getProgramBinaryAvailable ) {
		return false;
	}

	if ( _shaderBinaryCacheInvalidated ) {
		return false;
	}

	const int start = Sys::Milliseconds();

	std::error_code err;

	std::string secondaryName;
	for ( const ShaderEntry& shader : shaders ) {
		if ( shader.name != mainShader ) {
			secondaryName += Str::Format( "%s_%u_%u", shader.name, shader.macro, shader.type );
		} else {
			secondaryName += Str::Format( "%u_%u_", shader.macro, shader.type );
		}
	}

	std::string shaderFilename = Str::Format( "glsl/%s/%s.bin", mainShader, secondaryName );
	FS::File shaderFile = FS::HomePath::OpenRead( shaderFilename, err );
	if ( err ) {
		return false;
	}

	GLint success;
	const byte *binaryptr;
	GLBinaryHeader shaderHeader;
	std::string shaderData = shaderFile.ReadAll( err );
	if ( err ) {
		return false;
	}

	if ( shaderData.size() < sizeof( shaderHeader ) ) {
		return false;
	}

	binaryptr = reinterpret_cast<const byte*>( shaderData.data() );

	// Get the shader header from the file
	memcpy( &shaderHeader, binaryptr, sizeof( shaderHeader ) );
	binaryptr += sizeof( shaderHeader );

	/* Check if the header struct is the correct format
	and the binary was produced by the same GL driver */
	if ( shaderHeader.version != GL_SHADER_VERSION /* || shaderHeader.driverVersionHash != _driverVersionHash */ ) {
		/* These two fields should be the same for all shaders. So if there is a mismatch,
		don't bother opening any of the remaining files.
		I've disabled the cache invalidation on driver version change, because we now also cache shader programs that use
		non-empty deformVertexes. This would mean that after updating the driver, any time you load a new map with
		deformVertexes, it would cause the rebuild of *all* shaders */
		Log::Notice( "Invalidating shader binary cache" );
		_shaderBinaryCacheInvalidated = true;
		return false;
	}

	// Make sure the checksums for the source code match
	if ( shaderHeader.checkSum != descriptor->checkSum ) {
		return false;
	}

	if ( glConfig2.separateShaderObjectsAvailable && !shaderHeader.separateProgram ) {
		return false;
	}

	if ( shaderHeader.binaryLength != shaderData.size() - sizeof( shaderHeader ) ) {
		Log::Warn( "Shader cache %s has wrong size", shaderFilename );
		return false;
	}

	// Load the shader program
	descriptor->id = glCreateProgram();
	glProgramBinary( descriptor->id, shaderHeader.binaryFormat, binaryptr, shaderHeader.binaryLength );
	glGetProgramiv( descriptor->id, GL_LINK_STATUS, &success );

	if ( !success ) {
		return false;
	}

	for ( const ShaderEntry& shader : shaders ) {
		descriptor->shaderNames[descriptor->shaderCount] = shader;
		descriptor->shaderCount++;
	}

	cacheLoadTime += Sys::Milliseconds() - start;
	cacheLoadCount++;

	return true;
}

void GLShaderManager::SaveShaderBinary( ShaderProgramDescriptor* descriptor ) {
	if ( !r_glslCache.Get() ) {
		return;
	}

	if ( !GetShaderPath().empty() ) {
		return;
	}

	// Don't even try if the necessary functions aren't available
	if( !glConfig2.getProgramBinaryAvailable ) {
		return;
	}

	const int start = Sys::Milliseconds();

	// Find output size
	GLBinaryHeader shaderHeader{};
	GLuint binarySize = sizeof( shaderHeader );
	GLint binaryLength;
	glGetProgramiv( descriptor->id, GL_PROGRAM_BINARY_LENGTH, &binaryLength );

	// The binary length may be 0 if there is an error.
	if ( binaryLength <= 0 ) {
		return;
	}

	binarySize += binaryLength;

	byte* binary;
	byte* binaryptr;
	binaryptr = binary = ( byte* )ri.Hunk_AllocateTempMemory( binarySize );

	// Reserve space for the header
	binaryptr += sizeof( shaderHeader );

	// Get the program binary and write it to the buffer
	glGetProgramBinary( descriptor->id, binaryLength, nullptr, &shaderHeader.binaryFormat, binaryptr );

	// Set the header
	shaderHeader.version = GL_SHADER_VERSION;

	shaderHeader.binaryLength = binaryLength;
	shaderHeader.checkSum = descriptor->checkSum;
	shaderHeader.driverVersionHash = _driverVersionHash;

	shaderHeader.separateProgram = glConfig2.separateShaderObjectsAvailable;

	// Write the header to the buffer
	memcpy( binary, &shaderHeader, sizeof( shaderHeader ) );

	std::string secondaryName;
	for ( uint32_t i = 0; i < descriptor->shaderCount; i++ ) {
		const ShaderEntry& shader = descriptor->shaderNames[i];
		if ( shader.name != descriptor->mainShader || !descriptor->hasMain ) {
			secondaryName += Str::Format( "%s_%u_%u", shader.name, shader.macro, shader.type );
		} else {
			secondaryName += Str::Format( "%u_%u_", shader.macro, shader.type );
		}
	}

	std::string name = descriptor->hasMain ? descriptor->mainShader : "unknown";
	auto fileName = Str::Format( "glsl/%s/%s.bin", name, secondaryName );
	ri.FS_WriteFile( fileName.c_str(), binary, binarySize );

	ri.Hunk_FreeTempMemory( binary );

	cacheSaveTime += Sys::Milliseconds() - start;
	cacheSaveCount++;
}

// This will generate all the extra code for material system shaders
std::string GLShaderManager::ShaderPostProcess( GLShader *shader, const std::string& shaderText, const uint32_t offset ) {
	if ( !shader->std430Size ) {
		return shaderText;
	}

	std::string newShaderText;
	std::string materialStruct = "\nstruct Material {\n";
	// 6 kb for materials
	const uint32_t count = ( 4096 + 2048 ) / shader->GetPaddedSize();
	std::string materialBlock = "layout(std140, binding = "
		                        + std::to_string( Util::ordinal( BufferBind::MATERIALS ) )
		                        + ") uniform materialsUBO {\n"
	                            "	Material materials[" + std::to_string( count ) + "]; \n"
	                            "};\n\n";

	std::string texBuf = glConfig2.maxUniformBlockSize >= MIN_MATERIAL_UBO_SIZE ?
		"layout(std140, binding = "
		+ std::to_string( Util::ordinal( BufferBind::TEX_DATA ) )
		+ ") uniform texDataUBO {\n"
		"	TexData texData[" + std::to_string( MAX_TEX_BUNDLES ) + "]; \n"
		"};\n\n"
		: "layout(std430, binding = "
		+ std::to_string( Util::ordinal( BufferBind::TEX_DATA ) )
		+ ") restrict readonly buffer texDataSSBO {\n"
		"	TexData texData[];\n"
		"};\n\n";
	// We have to store u_TextureMatrix as vec4 + vec2 because otherwise it would be aligned to a vec4 under std140
	std::string texDataBlock = "struct TexData {\n"
		                       "	vec4 u_TextureMatrix;\n"
	                           "	vec2 u_TextureMatrix2;\n"
	                           "	uvec2 u_DiffuseMap;\n"
	                           "	uvec2 u_NormalMap;\n"
	                           "	uvec2 u_HeightMap;\n"
	                           "	uvec2 u_MaterialMap;\n"
	                           "	uvec2 u_GlowMap;\n"
	                           "};\n\n"
	                           + texBuf +
		                       "#define u_TextureMatrix mat3x2( texData[( baseInstance >> 12 ) & 0xFFF].u_TextureMatrix.xy, texData[( baseInstance >> 12 ) & 0xFFF].u_TextureMatrix.zw, texData[( baseInstance >> 12 ) & 0xFFF].u_TextureMatrix2 )\n"
		                       "#define u_DiffuseMap_initial texData[( baseInstance >> 12 ) & 0xFFF].u_DiffuseMap\n"
		                       "#define u_NormalMap_initial texData[( baseInstance >> 12 ) & 0xFFF].u_NormalMap\n"
		                       "#define u_HeightMap_initial texData[( baseInstance >> 12 ) & 0xFFF].u_HeightMap\n"
		                       "#define u_MaterialMap_initial texData[( baseInstance >> 12 ) & 0xFFF].u_MaterialMap\n"
		                       "#define u_GlowMap_initial texData[( baseInstance >> 12 ) & 0xFFF].u_GlowMap\n\n"
		                       "struct LightMapData {\n"
	                           "	uvec2 u_LightMap;\n"
	                           "	uvec2 u_DeluxeMap;\n"
	                           "};\n\n"
	                           "layout(std140, binding = "
		                       + std::to_string( Util::ordinal( BufferBind::LIGHTMAP_DATA ) )
		                       + ") uniform lightMapDataUBO {\n"
	                           "	LightMapData lightMapData[256];\n"
	                           "};\n\n"
		                       "#define u_LightMap_initial lightMapData[( baseInstance >> 24 ) & 0xFF].u_LightMap\n"
		                       "#define u_DeluxeMap_initial lightMapData[( baseInstance >> 24 ) & 0xFF].u_DeluxeMap\n\n";
	std::string materialDefines;

	/* Generate the struct and defines in the form of:
	* struct Material {
	*   type uniform0;
	*   type uniform1;
	*   ..
	*   type uniformn;
	* }
	* 
	* #define uniformx materials[baseInstance].uniformx
	*/

	for( GLUniform* uniform : shader->_uniforms ) {
		if ( uniform->IsGlobal() ) {
			continue;
		}

		if ( !uniform->IsTexture() ) {
			materialStruct += "	" + uniform->GetType() + " " + uniform->GetName();

			if ( uniform->GetComponentSize() ) {
				materialStruct += "[ " + std::to_string( uniform->GetComponentSize() ) + " ]";
			}
			materialStruct += ";\n";

			// vec3 is aligned to 4 components, so just pad it with int
			// TODO: Try to move 1 component uniforms here to avoid wasting memory
			if ( uniform->GetSTD430Size() == 3 ) {
				materialStruct += "	int ";
				materialStruct += uniform->GetName();
				materialStruct += "_padding;\n";
			}
		}

		if ( uniform->IsTexture() ) {
			continue;
		}

		materialDefines += "#define ";
		materialDefines += uniform->GetName();

		if ( uniform->IsTexture() ) { // Driver bug: AMD compiler crashes when using the SSBO value directly
			materialDefines += "_initial uvec2("; // We'll need this to create sampler objects later
		}

		materialDefines += " materials[baseInstance & 0xFFF].";
		materialDefines += uniform->GetName();
		
		if ( uniform->IsTexture() ) {
			materialDefines += " )";
		}

		materialDefines += "\n";
	}

	// Array of structs is aligned to the largest member of the struct
	for ( uint i = 0; i < shader->padding; i++ ) {
		materialStruct += "	int material_padding" + std::to_string( i );
		materialStruct += ";\n";
	}

	materialStruct += "};\n\n";
	materialDefines += "\n";

	std::istringstream shaderTextStream( shaderText );
	std::string shaderMain;

	std::string line;
	
	/* Remove local uniform declarations, but avoid removing uniform / storage blocks;
	*  their values will be sourced from a buffer instead
	*  Global uniforms (like u_ViewUp and u_ViewOrigin) will still be set as regular uniforms */
	while( std::getline( shaderTextStream, line, '\n' ) ) {
		bool skip = false;
		if ( line.find( "uniform" ) < line.find( "//" ) && line.find( ";" ) != std::string::npos ) {
			for ( GLUniform* uniform : shader->_uniforms ) {
				if ( !uniform->IsGlobal() ) {
					const size_t pos = line.find( uniform->GetName() );
					if ( pos != std::string::npos && !Str::cisalpha( line[pos + strlen( uniform->GetName() )] ) ) {
						skip = true;
						break;
					}
				}
			}
		}

		if ( skip ) {
			continue;
		}

		shaderMain += line + "\n";
	}

	materialDefines += "\n";

	newShaderText = "#define USE_MATERIAL_SYSTEM\n" + materialStruct + materialBlock + texDataBlock + materialDefines; // + shaderMain;
	shaderMain.insert( offset, newShaderText );
	return shaderMain;
}

void GLShaderManager::PrintShaderSource( Str::StringRef programName, GLuint object, std::vector<InfoLogEntry>& infoLog ) const
{
	char *dump;
	int maxLength = 0;

	glGetShaderiv( object, GL_SHADER_SOURCE_LENGTH, &maxLength );

	dump = ( char * ) ri.Hunk_AllocateTempMemory( maxLength );

	glGetShaderSource( object, maxLength, &maxLength, dump );

	std::string buffer;
	std::string delim( "\n" );
	std::string src( dump );

	ri.Hunk_FreeTempMemory( dump );

	int lineNumber = 0;
	size_t pos = 0;

	int infoLogID = -1;
	if ( infoLog.size() > 0 ) {
		infoLogID = 0;
	}

	while ( ( pos = src.find( delim ) ) != std::string::npos ) {
		std::string line = src.substr( 0, pos );
		if ( Str::IsPrefix( "#line ", line ) )
		{
			size_t lineNumEnd = line.find( ' ', 6 );
			Str::ParseInt( lineNumber, line.substr( 6, lineNumEnd - 6 ) );
		}

		std::string number = std::to_string( lineNumber );

		static const int numberWidth = 4;
		int p = numberWidth - number.length();
		p = p < 0 ? 0 : p;
		number.insert( number.begin(), p, ' ' );

		buffer.append( number );
		buffer.append( ": " );
		buffer.append( line );
		buffer.append( delim );

		while ( infoLogID != -1 && infoLog[infoLogID].line == lineNumber ) {
			if ( ( int( line.length() ) > infoLog[infoLogID].character ) && ( infoLog[infoLogID].character != -1 ) ) {
				buffer.append( numberWidth + 2, '-' );
				const size_t position = line.find_first_not_of( "\t" );

				if ( position != std::string::npos ) {
					buffer.append( position, '\t' );
					buffer.append( infoLog[infoLogID].character - position, '-' );
				} else {
					buffer.append( infoLog[infoLogID].character, '-' );
				}
				buffer.append( "^" );
				buffer.append( line.length() - infoLog[infoLogID].character - 1, '-' );

			} else if ( ( line.length() > 0 ) && ( infoLog[infoLogID].token.length() > 0 ) ) {
				size_t position = line.find_first_not_of( "\t" );
				size_t prevPosition = 0;

				buffer.append( numberWidth + 2, '-' );
				if ( position != std::string::npos ) {
					buffer.append( position, '\t' );
				} else {
					position = 0;
				}

				while ( ( position = line.find( infoLog[infoLogID].token, position ) ) && ( position != std::string::npos ) ) {
					buffer.append( position - prevPosition - 1, '-' );
					buffer.append( "^" );
					prevPosition = position;
					position++;
				}
				buffer.append( line.length() - position - 1, '-' );
			} else {
				buffer.append( numberWidth + 2 + line.length(), '^' );
			}

			buffer.append( delim );
			buffer.append( infoLog[infoLogID].error );
			buffer.append( delim );
			buffer.append( delim );

			infoLogID++;

			if ( infoLogID >= int( infoLog.size() ) ) {
				infoLogID = -1;
			}
		}

		src.erase( 0, pos + delim.length() );

		lineNumber++;
	}

	Log::Warn("Source for shader program %s:\n%s", programName, buffer.c_str());
}

std::vector<GLShaderManager::InfoLogEntry> GLShaderManager::ParseInfoLog( const std::string& infoLog ) const {
	std::vector<InfoLogEntry> out;

	std::istringstream infoLogTextStream( infoLog );
	std::string line;
	const char* digits = "0123456789";

	// Info-log is entirely implementation dependent, so different vendors will report it differently
	while ( std::getline( infoLogTextStream, line, '\n' ) ) {
		switch ( glConfig2.driverVendor ) {
			case glDriverVendor_t::NVIDIA:
			{
				// Format: <num of attached shader>(<line>): <error>
				size_t lineNum1 = line.find_first_of( '(' );
				if ( lineNum1 != std::string::npos ) {
					lineNum1++;
					const size_t lineNum2 = line.find_first_not_of( digits, lineNum1 );

					if ( lineNum2 != std::string::npos ) {
						int lineNum;
						if ( !Str::ParseInt( lineNum, line.substr( lineNum1, lineNum2 - lineNum1 ) ) ) {
							break;
						}
						int characterNum = -1;
						std::string token;

						// The token producing the error tends to be at the end in ""
						const size_t characterNum2 = line.find_last_of( "\"" );
						if ( characterNum2 != std::string::npos ) {
							const std::string subLine = line.substr( lineNum2, characterNum2 - lineNum2 );
							const size_t characterNum1 = subLine.find_last_of( "\"" );

							token = subLine.substr( characterNum1 + 1 );
						}

						InfoLogEntry entry;
						entry.line = lineNum;
						entry.character = characterNum;
						entry.token = token;
						entry.error = line;
						out.push_back( entry );
					}
				}
				break;
			}
			case glDriverVendor_t::MESA:
			{
				// Format: <num of attached shader>:<line>(<character>): <error>
				size_t num1 = line.find_first_of( ':' );
				if ( num1 != std::string::npos ) {
					num1++;
					size_t num2 = line.find_first_not_of( digits, num1 );
					int lineNum;
					if ( !Str::ParseInt( lineNum, line.substr( num1, num2 - num1 ) ) ) {
						break;
					}
					int characterNum = -1;

					num1 = line.find_first_of( '(' );
					if ( num1 != std::string::npos ) {
						num1++;
						num2 = line.find_first_not_of( digits, num1 );
						if ( !Str::ParseInt( characterNum, line.substr( num1, num2 - num1 ) ) ) {
							break;
						}
					}

					InfoLogEntry entry;
					entry.line = lineNum;
					entry.character = characterNum;
					entry.error = line;
					out.push_back( entry );
				}
				break;
			}
			case glDriverVendor_t::ATI:
				// Format: ERROR: <num of attached shader>:<line>: <error>
			case glDriverVendor_t::INTEL:
				// Format: ERROR: <wtf is this number?>:<line>: <error>
			{
				// Note: Intel sometimes reports incorrect line number for errors
				size_t num1 = line.find_first_not_of( "ERROR: " );
				if ( num1 != std::string::npos ) {
					num1 = line.find_first_not_of( digits, num1 );
					if ( num1 == std::string::npos ) {
						break;
					}
					num1 = line.find_first_of( digits, num1 );
					if ( num1 == std::string::npos ) {
						break;
					}

					const size_t num2 = line.find_first_not_of( digits, num1 );
					if ( num2 == std::string::npos ) {
						break;
					}
					int lineNum;
					if ( !Str::ParseInt( lineNum, line.substr( num1, num2 - num1 ) ) ) {
						break;
					}

					InfoLogEntry entry;
					entry.line = lineNum;
					entry.character = -1;
					entry.error = line;
					out.push_back( entry );
				}
				break;
			}
			case glDriverVendor_t::UNKNOWN:
			default:
				Log::Warn( "Unable to parse shader info log errors: unknown format" );
				return out;
		}
	}

	// Messages in the log can sometimes be out of order
	std::sort( out.begin(), out.end(),
		[]( const InfoLogEntry& lhs, const InfoLogEntry& rhs ) {
			return lhs.line < rhs.line;
		} );

	return out;
}

std::string GLShaderManager::GetInfoLog( GLuint object ) const
{
	char *msg;
	int maxLength = 0;

	if ( glIsShader( object ) )
	{
		glGetShaderiv( object, GL_INFO_LOG_LENGTH, &maxLength );
	}
	else if ( glIsProgram( object ) )
	{
		glGetProgramiv( object, GL_INFO_LOG_LENGTH, &maxLength );
	}
	else
	{
		Log::Warn( "object is not a shader or program" );
		return "";
	}

	msg = ( char * ) ri.Hunk_AllocateTempMemory( maxLength );

	if ( glIsShader( object ) )
	{
		glGetShaderInfoLog( object, maxLength, &maxLength, msg );
	}
	else if ( glIsProgram( object ) )
	{
		glGetProgramInfoLog( object, maxLength, &maxLength, msg );
	}

	std::string out = msg;
	ri.Hunk_FreeTempMemory( msg );

	return out;
}

void GLShaderManager::BindAttribLocations( GLuint program ) const {
	for ( uint32_t i = 0; i < ATTR_INDEX_MAX; i++ ) {
		glBindAttribLocation( program, i, attributeNames[ i ] );
	}
}

// reflective specular not implemented for PBR yet
bool GLCompileMacro_USE_REFLECTIVE_SPECULAR::HasConflictingMacros( size_t permutation, const std::vector< GLCompileMacro * > &macros ) const
{
	for (const GLCompileMacro* macro : macros)
	{
		if ( ( permutation & macro->GetBit() ) != 0 && macro->GetType() == USE_PHYSICAL_MAPPING )
		{
			//Log::Notice("conflicting macro! canceling '%s' vs. '%s'", GetName(), macro->GetName());
			return true;
		}
	}

	return false;
}

bool GLCompileMacro_USE_VERTEX_SKINNING::HasConflictingMacros( size_t permutation, const std::vector< GLCompileMacro * > &macros ) const
{
	for (const GLCompileMacro* macro : macros)
	{
		//if(GLCompileMacro_USE_VERTEX_ANIMATION* m = dynamic_cast<GLCompileMacro_USE_VERTEX_ANIMATION*>(macro))
		if ( ( permutation & macro->GetBit() ) != 0 && macro->GetType() == USE_VERTEX_ANIMATION )
		{
			//Log::Notice("conflicting macro! canceling '%s' vs. '%s'", GetName(), macro->GetName());
			return true;
		}
	}

	return false;
}

bool GLCompileMacro_USE_VERTEX_SKINNING::MissesRequiredMacros( size_t /*permutation*/, const std::vector< GLCompileMacro * > &/*macros*/ ) const
{
	return !glConfig2.vboVertexSkinningAvailable;
}

bool GLCompileMacro_USE_VERTEX_ANIMATION::HasConflictingMacros( size_t permutation, const std::vector< GLCompileMacro * > &macros ) const
{
	for (const GLCompileMacro* macro : macros)
	{
		if ( ( permutation & macro->GetBit() ) != 0 && macro->GetType() == USE_VERTEX_SKINNING )
		{
			//Log::Notice("conflicting macro! canceling '%s' vs. '%s'", GetName(), macro->GetName());
			return true;
		}
	}

	return false;
}

uint32_t GLCompileMacro_USE_VERTEX_ANIMATION::GetRequiredVertexAttributes() const
{
	uint32_t attribs = ATTR_POSITION2 | ATTR_QTANGENT2;

	return attribs;
}

bool GLCompileMacro_USE_TCGEN_ENVIRONMENT::HasConflictingMacros( size_t permutation, const std::vector<GLCompileMacro*> &macros) const
{
	for (const GLCompileMacro* macro : macros)
	{
		if ((permutation & macro->GetBit()) != 0 && (macro->GetType() == USE_TCGEN_LIGHTMAP))
		{
			//Log::Notice("conflicting macro! canceling '%s' vs. '%s'", GetName(), macro->GetName());
			return true;
		}
	}

	return false;
}

bool GLCompileMacro_USE_TCGEN_LIGHTMAP::HasConflictingMacros(size_t permutation, const std::vector<GLCompileMacro*> &macros) const
{
	for (const GLCompileMacro* macro : macros)
	{
		if ((permutation & macro->GetBit()) != 0 && (macro->GetType() == USE_TCGEN_ENVIRONMENT))
		{
			//Log::Notice("conflicting macro! canceling '%s' vs. '%s'", GetName(), macro->GetName());
			return true;
		}
	}

	return false;
}

bool GLCompileMacro_USE_DELUXE_MAPPING::HasConflictingMacros(size_t permutation, const std::vector<GLCompileMacro*> &macros) const
{
	for (const GLCompileMacro* macro : macros)
	{
		if ((permutation & macro->GetBit()) != 0 && (macro->GetType() == USE_GRID_DELUXE_MAPPING || macro->GetType() == USE_GRID_LIGHTING))
		{
			return true;
		}
	}

	return false;
}

bool GLCompileMacro_USE_GRID_DELUXE_MAPPING::HasConflictingMacros(size_t permutation, const std::vector<GLCompileMacro*> &macros) const
{
	for (const GLCompileMacro* macro : macros)
	{
		if ((permutation & macro->GetBit()) != 0 && (macro->GetType() == USE_DELUXE_MAPPING || macro->GetType() == USE_BSP_SURFACE))
		{
			return true;
		}
	}

	return false;
}

bool GLCompileMacro_USE_GRID_LIGHTING::HasConflictingMacros(size_t permutation, const std::vector<GLCompileMacro*> &macros) const
{
	for (const GLCompileMacro* macro : macros)
	{
		if ((permutation & macro->GetBit()) != 0 && (macro->GetType() == USE_DELUXE_MAPPING))
		{
			return true;
		}
	}

	return false;
}

bool GLCompileMacro_USE_BSP_SURFACE::HasConflictingMacros(size_t permutation, const std::vector<GLCompileMacro*> &macros) const
{
	for (const GLCompileMacro* macro : macros)
	{
		if ((permutation & macro->GetBit()) != 0 && (macro->GetType() == USE_GRID_DELUXE_MAPPING))
		{
			return true;
		}
	}

	return false;
}

GLuint GLUniform::GetSTD430Size() const {
	return _std430Size;
}

GLuint GLUniform::GetSTD430Alignment() const {
	return _std430Alignment;
}

uint32_t* GLUniform::WriteToBuffer( uint32_t* buffer ) {
	return buffer;
}

void GLShader::RegisterUniform( GLUniform* uniform ) {
	_uniforms.push_back( uniform );
	textureCount += uniform->IsTexture();
}

GLint GLShader::GetUniformLocation( const GLchar *uniformName ) const {
	GLuint p = GetProgram();
	return glGetUniformLocation( p, uniformName );
}

// Compute std430 size/alignment and sort uniforms from highest to lowest alignment
void GLShader::PostProcessUniforms() {
	if ( !_useMaterialSystem ) {
		return;
	}

	std::vector<GLUniform*> tmp;

	std::vector<GLUniform*> globalUniforms;
	for ( GLUniform* uniform : _uniforms ) {
		if ( uniform->IsGlobal() || uniform->IsTexture() ) {
			globalUniforms.emplace_back( uniform );
		}
	}
	for ( GLUniform* uniform : globalUniforms ) {
		_uniforms.erase( std::remove( _uniforms.begin(), _uniforms.end(), uniform ), _uniforms.end() );
	}

	// Sort uniforms from highest to lowest alignment so we don't need to pad uniforms (other than vec3s)
	const uint numUniforms = _uniforms.size();
	GLuint structSize = 0;
	while ( tmp.size() < numUniforms ) {
		// Higher-alignment uniforms first to avoid wasting memory
		GLuint highestAlignment = 0;
		int highestUniform = 0;
		for( uint i = 0; i < _uniforms.size(); i++ ) {
			if ( _uniforms[i]->GetSTD430Alignment() > highestAlignment ) {
				highestAlignment = _uniforms[i]->GetSTD430Alignment();
				highestUniform = i;
			}

			if ( highestAlignment == 4 ) {
				break; // 4-component is the highest alignment in std430
			}
		}
			
		const GLuint size = _uniforms[highestUniform]->GetSTD430Size();
		if ( _uniforms[highestUniform]->GetComponentSize() != 0 ) {
			structSize += ( size == 3 ) ? 4 * _uniforms[highestUniform]->GetComponentSize() :
												size * _uniforms[highestUniform]->GetComponentSize();
		} else {
			structSize += ( size == 3 ) ? 4 : size;
		}

		tmp.emplace_back( _uniforms[highestUniform] );
		_uniforms.erase( _uniforms.begin() + highestUniform );
	}
	_uniforms = tmp;

	const GLuint structAlignment = 4; // Material buffer is now a UBO, so it uses std140 layout, which is aligned to vec4
	if ( structSize > 0 ) {
		padding = ( structAlignment - ( structSize % structAlignment ) ) % structAlignment;
	}
	std430Size = structSize;
	for ( GLUniform* uniform : globalUniforms ) {
		_uniforms.emplace_back( uniform );
	}
}

uint32_t GLShader::GetUniqueCompileMacros( size_t permutation, const int type ) const {
	uint32_t macroOut = 0;
	for ( const GLCompileMacro* macro : _compileMacros ) {
		if ( permutation & macro->GetBit() ) {
			if ( macro->HasConflictingMacros( permutation, _compileMacros ) ) {
				continue;
			}

			if ( macro->MissesRequiredMacros( permutation, _compileMacros ) ) {
				continue;
			}

			if ( !( macro->GetShaderTypes() & type ) ) {
				continue;
			}

			macroOut |= BIT( macro->GetType() );
		}
	}

	return macroOut;
}

bool GLShader::GetCompileMacrosString( size_t permutation, std::string &compileMacrosOut, const int type ) const {
	compileMacrosOut.clear();

	for ( const GLCompileMacro* macro : _compileMacros ) {
		if ( permutation & macro->GetBit() ) {
			if ( macro->HasConflictingMacros( permutation, _compileMacros ) ) {
				return false;
			}

			if ( macro->MissesRequiredMacros( permutation, _compileMacros ) ) {
				return false;
			}

			if ( !( macro->GetShaderTypes() & type ) ) {
				return false;
			}

			compileMacrosOut += macro->GetName();
			compileMacrosOut += " ";
		}
	}

	return true;
}

int GLShader::SelectProgram()
{
	int    index = 0;

	size_t numMacros = _compileMacros.size();

	for ( size_t i = 0; i < numMacros; i++ )
	{
		if ( _activeMacros & BIT( i ) )
		{
			index += BIT( i );
		}
	}

	return index;
}

GLuint GLShader::GetProgram( int deformIndex ) {
	int macroIndex = SelectProgram();
	size_t index = macroIndex + ( size_t( deformIndex ) << _compileMacros.size() );

	// program may not be loaded yet because the shader manager hasn't yet gotten to it
	// so try to load it now
	if ( index >= shaderPipelines.size() || !shaderPipelines[index] ) {
		_shaderManager->BuildPermutation( this, macroIndex, deformIndex );
	}

	// program is still not loaded
	if ( index >= shaderPipelines.size() || !shaderPipelines[index] ) {
		std::string activeMacros;
		size_t numMacros = _compileMacros.size();

		for ( size_t j = 0; j < numMacros; j++ ) {
			GLCompileMacro* macro = _compileMacros[j];

			int bit = macro->GetBit();

			if ( IsMacroSet( bit ) ) {
				activeMacros += macro->GetName();
				activeMacros += " ";
			}
		}

		ThrowShaderError( Str::Format( "Invalid shader configuration: shader = '%s', macros = '%s'", _name, activeMacros ) );
	}

	return shaderPipelines[index];
}

void GLShader::BindProgram( int deformIndex ) {
	int macroIndex = SelectProgram();
	size_t index = macroIndex + ( size_t(deformIndex) << _compileMacros.size() );

	// program may not be loaded yet because the shader manager hasn't yet gotten to it
	// so try to load it now
	if ( index >= shaderPipelines.size() || !shaderPipelines[index] )
	{
		_shaderManager->BuildPermutation( this, macroIndex, deformIndex );
	}

	// program is still not loaded
	if ( index >= shaderPipelines.size() || !shaderPipelines[index] )
	{
		std::string activeMacros;

		for ( const auto& macro : _compileMacros )
		{
			if ( IsMacroSet( macro->GetBit() ) )
			{
				activeMacros += macro->GetName();
				activeMacros += " ";
			}
		}

		ThrowShaderError(Str::Format("Invalid shader configuration: shader = '%s', macros = '%s'", _name, activeMacros ));
	}

	currentPipeline = shaderPipelines[index];
	currentIndex = index;

	if ( r_logFile->integer ) {
		std::string macros;

		GetCompileMacrosString( index, macros, GLCompileMacro::VERTEX | GLCompileMacro::FRAGMENT );

		auto msg = Str::Format( "--- GL_BindProgram( name = '%s', macros = '%s' ) ---\n", this->GetName(), macros );
		GLimp_LogComment( msg.c_str() );
	}

	GL_BindProgram( currentPipeline );
}

void GLShader::DispatchCompute( const GLuint globalWorkgroupX, const GLuint globalWorkgroupY, const GLuint globalWorkgroupZ ) {
	ASSERT_EQ( currentPipeline, glState.currentPipeline );
	ASSERT( _hasComputeShader );
	glDispatchCompute( globalWorkgroupX, globalWorkgroupY, globalWorkgroupZ );
}

void GLShader::DispatchComputeIndirect( const GLintptr indirectBuffer ) {
	ASSERT_EQ( currentPipeline, glState.currentPipeline );
	ASSERT( _hasComputeShader );
	glDispatchComputeIndirect( indirectBuffer );
}

void GLShader::SetRequiredVertexPointers()
{
	uint32_t macroVertexAttribs = 0;

	for ( const auto& macro : _compileMacros )
	{
		if ( IsMacroSet( macro->GetBit() ) )
		{
			macroVertexAttribs |= macro->GetRequiredVertexAttributes();
		}
	}

	uint32_t attribs = _vertexAttribsRequired | _vertexAttribs | macroVertexAttribs; // & ~_vertexAttribsUnsupported);
	GL_VertexAttribsState( attribs );
}

void GLShader::WriteUniformsToBuffer( uint32_t* buffer ) {
	uint32_t* bufPtr = buffer;
	for ( GLUniform* uniform : _uniforms ) {
		if ( !uniform->IsGlobal() && !uniform->IsTexture() ) {
			bufPtr = uniform->WriteToBuffer( bufPtr );
		}
	}
}

void TextureUniform( GLuint program, GLint location, GLint value ) {
	if ( glConfig2.separateShaderObjectsAvailable ) {
		glProgramUniform1i( program, location, value );
	} else {
		glUniform1i( location, value );
	}
}

GLShader_generic::GLShader_generic( GLShaderManager *manager ) :
	GLShader( "generic", ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT | ATTR_COLOR, manager ),
	u_ColorMap( this ),
	u_DepthMap( this ),
	u_TextureMatrix( this ),
	u_ViewOrigin( this ),
	u_ViewUp( this ),
	u_AlphaThreshold( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_ColorModulateColorGen( this ),
	u_Color( this ),
	u_Bones( this ),
	u_VertexInterpolation( this ),
	u_DepthScale( this ),
	u_ProfilerZero( this ),
	u_ProfilerRenderSubGroups( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_VERTEX_SKINNING( this ),
	GLCompileMacro_USE_VERTEX_ANIMATION( this ),
	GLCompileMacro_USE_TCGEN_ENVIRONMENT( this ),
	GLCompileMacro_USE_TCGEN_LIGHTMAP( this ),
	GLCompileMacro_USE_DEPTH_FADE( this )
{
}

void GLShader_generic::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_ColorMap" ), 0 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_DepthMap" ), 1 );
}

GLShader_genericMaterial::GLShader_genericMaterial( GLShaderManager* manager ) :
	GLShader( "genericMaterial", "generic", true, ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT | ATTR_COLOR, manager ),
	u_ColorMap( this ),
	u_DepthMap( this ),
	u_TextureMatrix( this ),
	u_ViewOrigin( this ),
	u_ViewUp( this ),
	u_AlphaThreshold( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_ColorModulateColorGen( this ),
	u_Color( this ),
	u_DepthScale( this ),
	u_ShowTris( this ),
	u_MaterialColour( this ),
	u_ProfilerZero( this ),
	u_ProfilerRenderSubGroups( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_TCGEN_ENVIRONMENT( this ),
	GLCompileMacro_USE_TCGEN_LIGHTMAP( this ),
	GLCompileMacro_USE_DEPTH_FADE( this ) {
}

void GLShader_genericMaterial::SetShaderProgramUniforms( ShaderProgramDescriptor* shaderProgram ) {
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_ColorMap" ), 0 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_DepthMap" ), 1 );
}

GLShader_lightMapping::GLShader_lightMapping( GLShaderManager *manager ) :
	GLShader( "lightMapping",
	ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT | ATTR_COLOR, manager ),
	u_DiffuseMap( this ),
	u_NormalMap( this ),
	u_HeightMap( this ),
	u_MaterialMap( this ),
	u_LightMap( this ),
	u_DeluxeMap( this ),
	u_GlowMap( this ),
	u_EnvironmentMap0( this ),
	u_EnvironmentMap1( this ),
	u_LightGrid1( this ),
	u_LightGrid2( this ),
	u_LightTiles( this ),
	u_TextureMatrix( this ),
	u_SpecularExponent( this ),
	u_ColorModulateColorGen( this ),
	u_Color( this ),
	u_AlphaThreshold( this ),
	u_ViewOrigin( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_Bones( this ),
	u_VertexInterpolation( this ),
	u_ReliefDepthScale( this ),
	u_ReliefOffsetBias( this ),
	u_NormalScale( this ),
	u_EnvironmentInterpolation( this ),
	u_LightGridOrigin( this ),
	u_LightGridScale( this ),
	u_numLights( this ),
	u_Lights( this ),
	u_ProfilerZero( this ),
	u_ProfilerRenderSubGroups( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_BSP_SURFACE( this ),
	GLCompileMacro_USE_VERTEX_SKINNING( this ),
	GLCompileMacro_USE_VERTEX_ANIMATION( this ),
	GLCompileMacro_USE_DELUXE_MAPPING( this ),
	GLCompileMacro_USE_GRID_LIGHTING( this ),
	GLCompileMacro_USE_GRID_DELUXE_MAPPING( this ),
	GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP( this ),
	GLCompileMacro_USE_RELIEF_MAPPING( this ),
	GLCompileMacro_USE_REFLECTIVE_SPECULAR( this ),
	GLCompileMacro_USE_PHYSICAL_MAPPING( this )
{
}

void GLShader_lightMapping::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_DiffuseMap" ), BIND_DIFFUSEMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_NormalMap" ), BIND_NORMALMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_HeightMap" ), BIND_HEIGHTMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_MaterialMap" ), BIND_MATERIALMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_LightMap" ), BIND_LIGHTMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_LightGrid1" ), BIND_LIGHTMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_DeluxeMap" ), BIND_DELUXEMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_LightGrid2" ), BIND_DELUXEMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_GlowMap" ), BIND_GLOWMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_EnvironmentMap0" ), BIND_ENVIRONMENTMAP0 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_EnvironmentMap1" ), BIND_ENVIRONMENTMAP1 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_LightTiles" ), BIND_LIGHTTILES );
	if( !glConfig2.uniformBufferObjectAvailable ) {
		glUniform1i( glGetUniformLocation( shaderProgram->id, "u_Lights" ), BIND_LIGHTS );
	}
}

GLShader_lightMappingMaterial::GLShader_lightMappingMaterial( GLShaderManager* manager ) :
	GLShader( "lightMappingMaterial", "lightMapping", true,
		ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT | ATTR_COLOR, manager ),
	u_DiffuseMap( this ),
	u_NormalMap( this ),
	u_HeightMap( this ),
	u_MaterialMap( this ),
	u_LightMap( this ),
	u_DeluxeMap( this ),
	u_GlowMap( this ),
	u_EnvironmentMap0( this ),
	u_EnvironmentMap1( this ),
	u_LightGrid1( this ),
	u_LightGrid2( this ),
	u_LightTiles( this ),
	u_TextureMatrix( this ),
	u_SpecularExponent( this ),
	u_ColorModulateColorGen( this ),
	u_Color( this ),
	u_AlphaThreshold( this ),
	u_ViewOrigin( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_ReliefDepthScale( this ),
	u_ReliefOffsetBias( this ),
	u_NormalScale( this ),
	u_EnvironmentInterpolation( this ),
	u_LightGridOrigin( this ),
	u_LightGridScale( this ),
	u_numLights( this ),
	u_Lights( this ),
	u_ShowTris( this ),
	u_MaterialColour( this ),
	u_ProfilerZero( this ),
	u_ProfilerRenderSubGroups( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_BSP_SURFACE( this ),
	GLCompileMacro_USE_DELUXE_MAPPING( this ),
	GLCompileMacro_USE_GRID_LIGHTING( this ),
	GLCompileMacro_USE_GRID_DELUXE_MAPPING( this ),
	GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP( this ),
	GLCompileMacro_USE_RELIEF_MAPPING( this ),
	GLCompileMacro_USE_REFLECTIVE_SPECULAR( this ),
	GLCompileMacro_USE_PHYSICAL_MAPPING( this ) {
}

void GLShader_lightMappingMaterial::SetShaderProgramUniforms( ShaderProgramDescriptor* shaderProgram ) {
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_DiffuseMap" ), BIND_DIFFUSEMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_NormalMap" ), BIND_NORMALMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_HeightMap" ), BIND_HEIGHTMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_MaterialMap" ), BIND_MATERIALMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_LightMap" ), BIND_LIGHTMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_DeluxeMap" ), BIND_DELUXEMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_GlowMap" ), BIND_GLOWMAP );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_EnvironmentMap0" ), BIND_ENVIRONMENTMAP0 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_EnvironmentMap1" ), BIND_ENVIRONMENTMAP1 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_LightTiles" ), BIND_LIGHTTILES );
	if ( !glConfig2.uniformBufferObjectAvailable ) {
		glUniform1i( glGetUniformLocation( shaderProgram->id, "u_Lights" ), BIND_LIGHTS );
	}
}

GLShader_forwardLighting_omniXYZ::GLShader_forwardLighting_omniXYZ( GLShaderManager *manager ):
	GLShader("forwardLighting_omniXYZ", "forwardLighting", ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager),
	u_DiffuseMap( this ),
	u_NormalMap( this ),
	u_MaterialMap( this ),
	u_AttenuationMapXY( this ),
	u_AttenuationMapZ( this ),
	u_ShadowMap( this ),
	u_ShadowClipMap( this ),
	u_RandomMap( this ),
	u_HeightMap( this ),
	u_TextureMatrix( this ),
	u_SpecularExponent( this ),
	u_AlphaThreshold( this ),
	u_ColorModulateColorGen( this ),
	u_Color( this ),
	u_ViewOrigin( this ),
	u_LightOrigin( this ),
	u_LightColor( this ),
	u_LightRadius( this ),
	u_LightScale( this ),
	u_LightAttenuationMatrix( this ),
	u_ShadowTexelSize( this ),
	u_ShadowBlur( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_Bones( this ),
	u_VertexInterpolation( this ),
	u_ReliefDepthScale( this ),
	u_ReliefOffsetBias( this ),
	u_NormalScale( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_VERTEX_SKINNING( this ),
	GLCompileMacro_USE_VERTEX_ANIMATION( this ),
	GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP( this ),
	GLCompileMacro_USE_RELIEF_MAPPING( this ),
	GLCompileMacro_USE_SHADOWING( this )
{
}

void GLShader_forwardLighting_omniXYZ::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_DiffuseMap" ), 0 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_NormalMap" ), 1 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_MaterialMap" ), 2 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_AttenuationMapXY" ), 3 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_AttenuationMapZ" ), 4 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_ShadowMap" ), 5 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_RandomMap" ), 6 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_ShadowClipMap" ), 7 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_HeightMap" ), 15 );
}

GLShader_forwardLighting_projXYZ::GLShader_forwardLighting_projXYZ( GLShaderManager *manager ):
	GLShader("forwardLighting_projXYZ", "forwardLighting", ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager),
	u_DiffuseMap( this ),
	u_NormalMap( this ),
	u_MaterialMap( this ),
	u_AttenuationMapXY( this ),
	u_AttenuationMapZ( this ),
	u_ShadowMap0( this ),
	u_ShadowClipMap0( this ),
	u_RandomMap( this ),
	u_HeightMap( this ),
	u_TextureMatrix( this ),
	u_SpecularExponent( this ),
	u_AlphaThreshold( this ),
	u_ColorModulateColorGen( this ),
	u_Color( this ),
	u_ViewOrigin( this ),
	u_LightOrigin( this ),
	u_LightColor( this ),
	u_LightRadius( this ),
	u_LightScale( this ),
	u_LightAttenuationMatrix( this ),
	u_ShadowTexelSize( this ),
	u_ShadowBlur( this ),
	u_ShadowMatrix( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_Bones( this ),
	u_VertexInterpolation( this ),
	u_ReliefDepthScale( this ),
	u_ReliefOffsetBias( this ),
	u_NormalScale( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_VERTEX_SKINNING( this ),
	GLCompileMacro_USE_VERTEX_ANIMATION( this ),
	GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP( this ),
	GLCompileMacro_USE_RELIEF_MAPPING( this ),
	GLCompileMacro_USE_SHADOWING( this )
{
}

void GLShader_forwardLighting_projXYZ::BuildShaderCompileMacros( std::string& compileMacros )
{
	compileMacros += "LIGHT_PROJ ";
}

void GLShader_forwardLighting_projXYZ::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_DiffuseMap" ), 0 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_NormalMap" ), 1 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_MaterialMap" ), 2 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_AttenuationMapXY" ), 3 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_AttenuationMapZ" ), 4 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_ShadowMap0" ), 5 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_RandomMap" ), 6 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_ShadowClipMap0" ), 7 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_HeightMap" ), 15 );
}

GLShader_forwardLighting_directionalSun::GLShader_forwardLighting_directionalSun( GLShaderManager *manager ):
	GLShader("forwardLighting_directionalSun", "forwardLighting", ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager),
	u_DiffuseMap( this ),
	u_NormalMap( this ),
	u_MaterialMap( this ),
	u_ShadowMap0( this ),
	u_ShadowMap1( this ),
	u_ShadowMap2( this ),
	u_ShadowMap3( this ),
	u_ShadowMap4( this ),
	u_ShadowClipMap0( this ),
	u_ShadowClipMap1( this ),
	u_ShadowClipMap2( this ),
	u_ShadowClipMap3( this ),
	u_ShadowClipMap4( this ),
	u_HeightMap( this ),
	u_TextureMatrix( this ),
	u_SpecularExponent( this ),
	u_AlphaThreshold( this ),
	u_ColorModulateColorGen( this ),
	u_Color( this ),
	u_ViewOrigin( this ),
	u_LightDir( this ),
	u_LightColor( this ),
	u_LightRadius( this ),
	u_LightScale( this ),
	u_LightAttenuationMatrix( this ),
	u_ShadowTexelSize( this ),
	u_ShadowBlur( this ),
	u_ShadowMatrix( this ),
	u_ShadowParallelSplitDistances( this ),
	u_ModelMatrix( this ),
	u_ViewMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_Bones( this ),
	u_VertexInterpolation( this ),
	u_ReliefDepthScale( this ),
	u_ReliefOffsetBias( this ),
	u_NormalScale( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_VERTEX_SKINNING( this ),
	GLCompileMacro_USE_VERTEX_ANIMATION( this ),
	GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP( this ),
	GLCompileMacro_USE_RELIEF_MAPPING( this ),
	GLCompileMacro_USE_SHADOWING( this )
{
}

void GLShader_forwardLighting_directionalSun::BuildShaderCompileMacros( std::string& compileMacros )
{
	compileMacros += "LIGHT_DIRECTIONAL ";
}

void GLShader_forwardLighting_directionalSun::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_DiffuseMap" ), 0 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_NormalMap" ), 1 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_MaterialMap" ), 2 );
	//glUniform1i(glGetUniformLocation( shaderProgram->id, "u_AttenuationMapXY" ), 3);
	//glUniform1i(glGetUniformLocation( shaderProgram->id, "u_AttenuationMapZ" ), 4);
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_ShadowMap0" ), 5 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_ShadowMap1" ), 6 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_ShadowMap2" ), 7 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_ShadowMap3" ), 8 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_ShadowMap4" ), 9 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_ShadowClipMap0" ), 10 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_ShadowClipMap1" ), 11 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_ShadowClipMap2" ), 12 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_ShadowClipMap3" ), 13 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_ShadowClipMap4" ), 14 );
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_HeightMap" ), 15 );
}

GLShader_shadowFill::GLShader_shadowFill( GLShaderManager *manager ) :
	GLShader( "shadowFill", ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager ),
	u_ColorMap( this ),
	u_TextureMatrix( this ),
	u_AlphaThreshold( this ),
	u_LightOrigin( this ),
	u_LightRadius( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_Color( this ),
	u_Bones( this ),
	u_VertexInterpolation( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_VERTEX_SKINNING( this ),
	GLCompileMacro_USE_VERTEX_ANIMATION( this ),
	GLCompileMacro_LIGHT_DIRECTIONAL( this )
{
}

void GLShader_shadowFill::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	glUniform1i( glGetUniformLocation( shaderProgram->id, "u_ColorMap" ), 0 );
}

GLShader_reflection::GLShader_reflection( GLShaderManager *manager ):
	GLShader("reflection", "reflection_CB", ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager ),
	u_ColorMapCube( this ),
	u_NormalMap( this ),
	u_HeightMap( this ),
	u_TextureMatrix( this ),
	u_ViewOrigin( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_Bones( this ),
	u_ReliefDepthScale( this ),
	u_ReliefOffsetBias( this ),
	u_NormalScale( this ),
	u_VertexInterpolation( this ),
	u_CameraPosition( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_VERTEX_SKINNING( this ),
	GLCompileMacro_USE_VERTEX_ANIMATION( this ),
	GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP( this ),
	GLCompileMacro_USE_RELIEF_MAPPING( this )
{
}

void GLShader_reflection::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_ColorMap" ), 0 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_NormalMap" ), 1 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_HeightMap" ), 15 );
}

GLShader_reflectionMaterial::GLShader_reflectionMaterial( GLShaderManager* manager ) :
	GLShader( "reflectionMaterial", "reflection_CB", true, ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager ),
	u_ColorMapCube( this ),
	u_NormalMap( this ),
	u_HeightMap( this ),
	u_TextureMatrix( this ),
	u_ViewOrigin( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_ReliefDepthScale( this ),
	u_ReliefOffsetBias( this ),
	u_NormalScale( this ),
	u_CameraPosition( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP( this ),
	GLCompileMacro_USE_RELIEF_MAPPING( this ) {
}

void GLShader_reflectionMaterial::SetShaderProgramUniforms( ShaderProgramDescriptor* shaderProgram ) {
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_ColorMap" ), 0 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_NormalMap" ), 1 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_HeightMap" ), 15 );
}

GLShader_skybox::GLShader_skybox( GLShaderManager *manager ) :
	GLShader( "skybox", ATTR_POSITION, manager ),
	u_ColorMapCube( this ),
	u_CloudMap( this ),
	u_TextureMatrix( this ),
	u_CloudHeight( this ),
	u_UseCloudMap( this ),
	u_AlphaThreshold( this ),
	u_ModelViewProjectionMatrix( this )
{
}

void GLShader_skybox::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_ColorMapCube" ), 0 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_CloudMap" ), 1 );
}

GLShader_skyboxMaterial::GLShader_skyboxMaterial( GLShaderManager* manager ) :
	GLShader( "skyboxMaterial", "skybox", true, ATTR_POSITION, manager ),
	u_ColorMapCube( this ),
	u_CloudMap( this ),
	u_TextureMatrix( this ),
	u_CloudHeight( this ),
	u_UseCloudMap( this ),
	u_AlphaThreshold( this ),
	u_ModelViewProjectionMatrix( this )
{}

void GLShader_skyboxMaterial::SetShaderProgramUniforms( ShaderProgramDescriptor* shaderProgram ) {
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_ColorMap" ), 0 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_CloudMap" ), 1 );
}

GLShader_fogQuake3::GLShader_fogQuake3( GLShaderManager *manager ) :
	GLShader( "fogQuake3", ATTR_POSITION | ATTR_QTANGENT, manager ),
	u_FogMap( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_ColorGlobal( this ),
	u_Bones( this ),
	u_VertexInterpolation( this ),
	u_FogDistanceVector( this ),
	u_FogDepthVector( this ),
	u_FogEyeT( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_VERTEX_SKINNING( this ),
	GLCompileMacro_USE_VERTEX_ANIMATION( this )
{
}

void GLShader_fogQuake3::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_FogMap" ), 0 );
}

GLShader_fogQuake3Material::GLShader_fogQuake3Material( GLShaderManager* manager ) :
	GLShader( "fogQuake3Material", "fogQuake3", true, ATTR_POSITION | ATTR_QTANGENT, manager ),
	u_FogMap( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_ColorGlobal( this ),
	u_FogDistanceVector( this ),
	u_FogDepthVector( this ),
	u_FogEyeT( this ),
	GLDeformStage( this ) {
}

void GLShader_fogQuake3Material::SetShaderProgramUniforms( ShaderProgramDescriptor* shaderProgram ) {
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_FogMap" ), 0 );
}

GLShader_fogGlobal::GLShader_fogGlobal( GLShaderManager *manager ) :
	GLShader( "fogGlobal", ATTR_POSITION, manager ),
	u_ColorMap( this ),
	u_DepthMap( this ),
	u_ModelViewProjectionMatrix( this ),
	u_UnprojectMatrix( this ),
	u_Color( this ),
	u_FogDistanceVector( this )
{
}

void GLShader_fogGlobal::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_ColorMap" ), 0 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_DepthMap" ), 1 );
}

GLShader_heatHaze::GLShader_heatHaze( GLShaderManager *manager ) :
	GLShader( "heatHaze", ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager ),
	u_CurrentMap( this ),
	u_NormalMap( this ),
	u_HeightMap( this ),
	u_TextureMatrix( this ),
	u_DeformMagnitude( this ),
	u_ModelViewProjectionMatrix( this ),
	u_ModelViewMatrixTranspose( this ),
	u_ProjectionMatrixTranspose( this ),
	u_Bones( this ),
	u_NormalScale( this ),
	u_VertexInterpolation( this ),
	GLDeformStage( this ),
	GLCompileMacro_USE_VERTEX_SKINNING( this ),
	GLCompileMacro_USE_VERTEX_ANIMATION( this )
{
}

void GLShader_heatHaze::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_NormalMap" ), 0 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_CurrentMap" ), 1 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_HeightMap" ), 15 );
}

GLShader_heatHazeMaterial::GLShader_heatHazeMaterial( GLShaderManager* manager ) :
	GLShader( "heatHazeMaterial", "heatHaze", true, ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager ),
	u_CurrentMap( this ),
	u_NormalMap( this ),
	u_HeightMap( this ),
	u_TextureMatrix( this ),
	u_DeformEnable( this ),
	u_DeformMagnitude( this ),
	u_ModelViewProjectionMatrix( this ),
	u_ModelViewMatrixTranspose( this ),
	u_ProjectionMatrixTranspose( this ),
	u_NormalScale( this ),
	GLDeformStage( this )
{
}

void GLShader_heatHazeMaterial::SetShaderProgramUniforms( ShaderProgramDescriptor* shaderProgram ) {
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_NormalMap" ), 0 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_CurrentMap" ), 1 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_HeightMap" ), 15 );
}

GLShader_screen::GLShader_screen( GLShaderManager *manager ) :
	GLShader( "screen", ATTR_POSITION, manager ),
	u_CurrentMap( this ),
	u_ModelViewProjectionMatrix( this )
{
}

void GLShader_screen::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_CurrentMap" ), 0 );
}

GLShader_screenMaterial::GLShader_screenMaterial( GLShaderManager* manager ) :
	GLShader( "screenMaterial", "screen", true, ATTR_POSITION, manager ),
	u_CurrentMap( this ),
	u_ModelViewProjectionMatrix( this ) {
}

void GLShader_screenMaterial::SetShaderProgramUniforms( ShaderProgramDescriptor* shaderProgram ) {
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_CurrentMap" ), 0 );
}

GLShader_portal::GLShader_portal( GLShaderManager *manager ) :
	GLShader( "portal", ATTR_POSITION, manager ),
	u_CurrentMap( this ),
	u_ModelViewMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_InversePortalRange( this )
{
}

void GLShader_portal::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_CurrentMap" ), 0 );
}

GLShader_contrast::GLShader_contrast( GLShaderManager *manager ) :
	GLShader( "contrast", ATTR_POSITION, manager ),
	u_ColorMap( this ),
	u_ModelViewProjectionMatrix( this )
{
}

void GLShader_contrast::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_ColorMap" ), 0 );
}

GLShader_cameraEffects::GLShader_cameraEffects( GLShaderManager *manager ) :
	GLShader( "cameraEffects", ATTR_POSITION | ATTR_TEXCOORD, manager ),
	u_ColorMap3D( this ),
	u_CurrentMap( this ),
	u_ColorModulate( this ),
	u_TextureMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_Tonemap( this ),
	u_TonemapParms( this ),
	u_TonemapExposure( this ),
	u_InverseGamma( this )
{
}

void GLShader_cameraEffects::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_CurrentMap" ), 0 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_ColorMap3D" ), 3 );
}

GLShader_blur::GLShader_blur( GLShaderManager *manager ) :
	GLShader( "blur", ATTR_POSITION, manager ),
	u_ColorMap( this ),
	u_ModelViewProjectionMatrix( this ),
	u_DeformMagnitude( this ),
	u_TexScale( this ),
	u_Horizontal( this )
{
}

void GLShader_blur::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_ColorMap" ), 0 );
}

GLShader_debugShadowMap::GLShader_debugShadowMap( GLShaderManager *manager ) :
	GLShader( "debugShadowMap", ATTR_POSITION, manager ),
	u_CurrentMap( this ),
	u_ModelViewProjectionMatrix( this )
{
}

void GLShader_debugShadowMap::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_CurrentMap" ), 0 );
}

GLShader_liquid::GLShader_liquid( GLShaderManager *manager ) :
	GLShader( "liquid", ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager ),
	u_CurrentMap( this ),
	u_DepthMap( this ),
	u_NormalMap( this ),
	u_PortalMap( this ),
	u_LightGrid1( this ),
	u_LightGrid2( this ),
	u_HeightMap( this ),
	u_TextureMatrix( this ),
	u_ViewOrigin( this ),
	u_RefractionIndex( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_UnprojectMatrix( this ),
	u_FresnelPower( this ),
	u_FresnelScale( this ),
	u_FresnelBias( this ),
	u_ReliefDepthScale( this ),
	u_ReliefOffsetBias( this ),
	u_NormalScale( this ),
	u_FogDensity( this ),
	u_FogColor( this ),
	u_SpecularExponent( this ),
	u_LightGridOrigin( this ),
	u_LightGridScale( this ),
	GLCompileMacro_USE_GRID_DELUXE_MAPPING( this ),
	GLCompileMacro_USE_GRID_LIGHTING( this ),
	GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP( this ),
	GLCompileMacro_USE_RELIEF_MAPPING( this )
{
}

void GLShader_liquid::SetShaderProgramUniforms( ShaderProgramDescriptor* shaderProgram ) {
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_CurrentMap" ), 0 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_PortalMap" ), 1 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_DepthMap" ), 2 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_NormalMap" ), 3 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_LightGrid1" ), 6 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_LightGrid2" ), 7 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_HeightMap" ), 15 );
}

GLShader_liquidMaterial::GLShader_liquidMaterial( GLShaderManager* manager ) :
	GLShader( "liquidMaterial", "liquid", true, ATTR_POSITION | ATTR_TEXCOORD | ATTR_QTANGENT, manager ),
	u_CurrentMap( this ),
	u_DepthMap( this ),
	u_NormalMap( this ),
	u_PortalMap( this ),
	u_LightGrid1( this ),
	u_LightGrid2( this ),
	u_HeightMap( this ),
	u_TextureMatrix( this ),
	u_ViewOrigin( this ),
	u_RefractionIndex( this ),
	u_ModelMatrix( this ),
	u_ModelViewProjectionMatrix( this ),
	u_UnprojectMatrix( this ),
	u_FresnelPower( this ),
	u_FresnelScale( this ),
	u_FresnelBias( this ),
	u_ReliefDepthScale( this ),
	u_ReliefOffsetBias( this ),
	u_NormalScale( this ),
	u_FogDensity( this ),
	u_FogColor( this ),
	u_LightTiles( this ),
	u_SpecularExponent( this ),
	u_LightGridOrigin( this ),
	u_LightGridScale( this ),
	GLCompileMacro_USE_GRID_DELUXE_MAPPING( this ),
	GLCompileMacro_USE_GRID_LIGHTING( this ),
	GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP( this ),
	GLCompileMacro_USE_RELIEF_MAPPING( this ) {
}

void GLShader_liquidMaterial::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_CurrentMap" ), 0 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_PortalMap" ), 1 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_DepthMap" ), 2 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_NormalMap" ), 3 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_LightGrid1" ), 6 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_LightGrid2" ), 7 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_HeightMap" ), 15 );
}

GLShader_motionblur::GLShader_motionblur( GLShaderManager *manager ) :
	GLShader( "motionblur", ATTR_POSITION, manager ),
	u_ColorMap( this ),
	u_DepthMap( this ),
	u_ModelViewProjectionMatrix( this ),
	u_blurVec( this )
{
}

void GLShader_motionblur::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_ColorMap" ), 0 );
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_DepthMap" ), 1 );
}

GLShader_ssao::GLShader_ssao( GLShaderManager *manager ) :
	GLShader( "ssao", ATTR_POSITION, manager ),
	u_DepthMap( this ),
	u_ModelViewProjectionMatrix( this ),
	u_UnprojectionParams( this ),
	u_zFar( this )
{
}

void GLShader_ssao::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_DepthMap" ), 0 );
}

GLShader_depthtile1::GLShader_depthtile1( GLShaderManager *manager ) :
	GLShader( "depthtile1", ATTR_POSITION, manager ),
	u_DepthMap( this ),
	u_ModelViewProjectionMatrix( this ),
	u_zFar( this )
{
}

void GLShader_depthtile1::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_DepthMap" ), 0 );
}

GLShader_depthtile2::GLShader_depthtile2( GLShaderManager *manager ) :
	GLShader( "depthtile2", ATTR_POSITION, manager ),
	u_DepthMap( this ),
	u_ModelViewProjectionMatrix( this )
{
}

void GLShader_depthtile2::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_DepthMap" ), 0 );
}

GLShader_lighttile::GLShader_lighttile( GLShaderManager *manager ) :
	GLShader( "lighttile", ATTR_POSITION | ATTR_TEXCOORD, manager ),
	u_DepthMap( this ),
	u_Lights( this ),
	u_numLights( this ),
	u_lightLayer( this ),
	u_ModelMatrix( this ),
	u_zFar( this )
{
}

void GLShader_lighttile::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_DepthMap" ), 0 );

	if( !glConfig2.uniformBufferObjectAvailable ) {
		glUniform1i( glGetUniformLocation( shaderProgram->id, "u_Lights" ), 1 );
	}
}

GLShader_fxaa::GLShader_fxaa( GLShaderManager *manager ) :
	GLShader( "fxaa", ATTR_POSITION, manager ),
	u_ColorMap( this ),
	u_ModelViewProjectionMatrix( this )
{
}

void GLShader_fxaa::SetShaderProgramUniforms( ShaderProgramDescriptor *shaderProgram )
{
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "u_ColorMap" ), 0 );
}

GLShader_cull::GLShader_cull( GLShaderManager* manager ) :
	GLShader( "cull", ATTR_POSITION, manager, false, false, true ),
	u_Frame( this ),
	u_ViewID( this ),
	u_TotalDrawSurfs( this ),
	u_SurfaceCommandsOffset( this ),
	u_Frustum( this ),
	u_UseFrustumCulling( this ),
	u_UseOcclusionCulling( this ),
	u_CameraPosition( this ),
	u_ModelViewMatrix( this ),
	u_FirstPortalGroup( this ),
	u_TotalPortals( this ),
	u_ViewWidth( this ),
	u_ViewHeight( this ),
	u_P00( this ),
	u_P11( this ) {
}

GLShader_depthReduction::GLShader_depthReduction( GLShaderManager* manager ) :
	GLShader( "depthReduction", ATTR_POSITION, manager, false, false, true ),
	u_ViewWidth( this ),
	u_ViewHeight( this ),
	u_InitialDepthLevel( this ) {
}

void GLShader_depthReduction::SetShaderProgramUniforms( ShaderProgramDescriptor* shaderProgram ) {
	TextureUniform( shaderProgram->id, glGetUniformLocation( shaderProgram->id, "depthTextureInitial" ), 0 );
}

GLShader_clearSurfaces::GLShader_clearSurfaces( GLShaderManager* manager ) :
	GLShader( "clearSurfaces", ATTR_POSITION, manager, false, false, true ),
	u_Frame( this ) {
}

GLShader_processSurfaces::GLShader_processSurfaces( GLShaderManager* manager ) :
	GLShader( "processSurfaces", ATTR_POSITION, manager, false, false, true ),
	u_Frame( this ),
	u_ViewID( this ),
	u_SurfaceCommandsOffset( this ) {
}
