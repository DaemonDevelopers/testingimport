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

#ifndef GL_SHADER_H
#define GL_SHADER_H

#include "tr_local.h"
#include <stdexcept>

#define LOG_GLSL_UNIFORMS 1
#define USE_UNIFORM_FIREWALL 1

// *INDENT-OFF*
static const unsigned int MAX_SHADER_MACROS = 10;
static const unsigned int GL_SHADER_VERSION = 5;

class ShaderException : public std::runtime_error
{
public:
	ShaderException(const char* msg) : std::runtime_error(msg) { }
};

enum class ShaderKind
{
	Unknown,
	BuiltIn,
	External
};

// Header for saved shader binaries
struct GLBinaryHeader
{
	unsigned int version;
	unsigned int checkSum; // checksum of shader source this was built from
	unsigned int driverVersionHash; // detect if the graphics driver was different

	unsigned int macros[ MAX_SHADER_MACROS ]; // macros the shader uses ( may or may not be enabled )
	unsigned int numMacros;

	GLenum binaryFormat; // argument to glProgramBinary
	uint32_t binaryLength; // argument to glProgramBinary
};

class GLUniform;
class GLShader;
class GLUniformBlock;
class GLCompileMacro;
class GLShaderManager;

// represents a piece of GLSL code that can be copied verbatim into
// GLShaders, like a .h file in C++
class GLHeader
{
private:

	std::string                    _name;
	std::string                    _text;
	uint32_t _lineCount;
	GLShaderManager               *_shaderManager;

public:
	GLHeader() : _name(), _text(), _lineCount(), _shaderManager( nullptr )
	{}

	GLHeader( const std::string &name, const std::string &text, GLShaderManager *manager ) :
		_name( name ),
		_text( text ),
		_lineCount( std::count( text.begin(), text.end(), '\n' ) ),
		_shaderManager( manager )
	{
	}

	const std::string &getName() const { return _name; }
	const std::string &getText() const { return _text; }
	uint32_t getLineCount() const { return _lineCount; }
	const GLShaderManager *getManager() const { return _shaderManager; }
};

class GLShader
{
	friend class GLShaderManager;
private:
	GLShader( const GLShader & ) = delete;
	GLShader &operator             = ( const GLShader & ) = delete;

	std::string                    _name;
	std::string                    _mainShaderName;
	const bool _useMaterialSystem;
	GLuint std430Size = 0;
	uint padding = 0;
	uint textureCount = 0;
protected:
	int                            _activeMacros;
	unsigned int                   _checkSum;
	shaderProgram_t                 *_currentProgram;
	const uint32_t                 _vertexAttribsRequired;
	uint32_t                       _vertexAttribs; // can be set by uniforms
	GLShaderManager                 *_shaderManager;

	bool                           _hasVertexShader;
	std::string                    _vertexShaderText;
	bool                           _hasFragmentShader;
	std::string                    _fragmentShaderText;
	bool                           _hasComputeShader;
	std::string                    _computeShaderText;
	std::vector< shaderProgram_t > _shaderPrograms;


	size_t                         _uniformStorageSize;
	std::vector< GLUniform * >      _uniforms;
	std::vector< GLUniformBlock * > _uniformBlocks;
	std::vector< GLCompileMacro * > _compileMacros;





	GLShader( const std::string &name, uint32_t vertexAttribsRequired, GLShaderManager *manager,
			  const bool hasVertexShader = true, const bool hasFragmentShader = true, const bool hasComputeShader = false ) :
		_name( name ),
		_mainShaderName( name ),
		_useMaterialSystem( false ),
		_activeMacros( 0 ),
		_checkSum( 0 ),
		_currentProgram( nullptr ),
		_vertexAttribsRequired( vertexAttribsRequired ),
		_vertexAttribs( 0 ),
		_shaderManager( manager ),
		_hasVertexShader( hasVertexShader ),
		_hasFragmentShader( hasFragmentShader ),
		_hasComputeShader( hasComputeShader ),
		_uniformStorageSize( 0 )
	{
	}

	GLShader( const std::string &name, const std::string &mainShaderName, uint32_t vertexAttribsRequired, GLShaderManager *manager,
			  const bool hasVertexShader = true, const bool hasFragmentShader = true, const bool hasComputeShader = false ) :
		_name( name ),
		_mainShaderName( mainShaderName ),
		_useMaterialSystem( false ),
		_activeMacros( 0 ),
		_checkSum( 0 ),
		_currentProgram( nullptr ),
		_vertexAttribsRequired( vertexAttribsRequired ),
		_vertexAttribs( 0 ),
		_shaderManager( manager ),
		_hasVertexShader( hasVertexShader ),
		_hasFragmentShader( hasFragmentShader ),
		_hasComputeShader( hasComputeShader ),
		_uniformStorageSize( 0 )
	{
	}

	GLShader( const std::string& name, const std::string& mainShaderName, const bool useMaterialSystem, uint32_t vertexAttribsRequired,
			  GLShaderManager* manager,
			  const bool hasVertexShader = true, const bool hasFragmentShader = true, const bool hasComputeShader = false ) :
		_name( name ),
		_mainShaderName( mainShaderName ),
		_useMaterialSystem( useMaterialSystem ),
		_activeMacros( 0 ),
		_checkSum( 0 ),
		_currentProgram( nullptr ),
		_vertexAttribsRequired( vertexAttribsRequired ),
		_vertexAttribs( 0 ),
		_shaderManager( manager ),
		_hasVertexShader( hasVertexShader ),
		_hasFragmentShader( hasFragmentShader ),
		_hasComputeShader( hasComputeShader ),
		_uniformStorageSize( 0 ) {
	}

public:
	virtual ~GLShader()
	{
		for ( std::size_t i = 0; i < _shaderPrograms.size(); i++ )
		{
			shaderProgram_t *p = &_shaderPrograms[ i ];

			if ( p->program )
			{
				glDeleteProgram( p->program );
			}

			if ( p->VS )
			{
				glDeleteShader( p->VS );
			}

			if ( p->FS )
			{
				glDeleteShader( p->FS );
			}

			if ( p->CS ) {
				glDeleteShader( p->CS );
			}

			if ( p->uniformFirewall )
			{
				Z_Free( p->uniformFirewall );
			}

			if ( p->uniformLocations )
			{
				Z_Free( p->uniformLocations );
			}

			if ( p->uniformBlockIndexes )
			{
				Z_Free( p->uniformBlockIndexes );
			}
		}
	}

	void RegisterUniform( GLUniform* uniform );

	void RegisterUniformBlock( GLUniformBlock *uniformBlock )
	{
		_uniformBlocks.push_back( uniformBlock );
	}

	void RegisterCompileMacro( GLCompileMacro *compileMacro )
	{
		if ( _compileMacros.size() >= MAX_SHADER_MACROS )
		{
			Sys::Drop( "Can't register more than %u compile macros for a single shader", MAX_SHADER_MACROS );
		}

		_compileMacros.push_back( compileMacro );
	}

	size_t GetNumOfCompiledMacros() const
	{
		return _compileMacros.size();
	}

	GLint GetUniformLocation( const GLchar *uniformName ) const;

	shaderProgram_t        *GetProgram() const
	{
		return _currentProgram;
	}

	const std::string      &GetName() const
	{
		return _name;
	}

	const std::string      &GetMainShaderName() const
	{
		return _mainShaderName;
	}

protected:
	void         PostProcessUniforms();
	bool         GetCompileMacrosString( size_t permutation, std::string &compileMacrosOut ) const;
	virtual void BuildShaderCompileMacros( std::string& /*vertexInlines*/ ) { };
	virtual void SetShaderProgramUniforms( shaderProgram_t* /*shaderProgram*/ ) { };
	int          SelectProgram();
public:
	GLuint GetProgram( int deformIndex );
	void BindProgram( int deformIndex );
	void DispatchCompute( const GLuint globalWorkgroupX, const GLuint globalWorkgroupY, const GLuint globalWorkgroupZ );
	void DispatchComputeIndirect( const GLintptr indirectBuffer );
	void SetRequiredVertexPointers();

	bool IsMacroSet( int bit )
	{
		return ( _activeMacros & bit ) != 0;
	}

	void AddMacroBit( int bit )
	{
		_activeMacros |= bit;
	}

	void DelMacroBit( int bit )
	{
		_activeMacros &= ~bit;
	}

	bool IsVertexAttribSet( int bit )
	{
		return ( _vertexAttribs & bit ) != 0;
	}

	void AddVertexAttribBit( int bit )
	{
		_vertexAttribs |= bit;
	}

	void DelVertexAttribBit( int bit )
	{
		_vertexAttribs &= ~bit;
	}

	GLuint GetSTD430Size() const {
		return std430Size;
	}

	uint GetPadding() const {
		return padding;
	}

	uint GetPaddedSize() const {
		return std430Size + padding;
	}

	uint GetTextureCount() const {
		return textureCount;
	}

	bool UseMaterialSystem() const {
		return _useMaterialSystem;
	}

	void WriteUniformsToBuffer( uint32_t* buffer );
};

class GLShaderManager
{
	std::queue< GLShader* > _shaderBuildQueue;
	std::vector< std::unique_ptr< GLShader > > _shaders;
	std::unordered_map< std::string, int > _deformShaderLookup;
	std::vector< GLint > _deformShaders;
	unsigned int _driverVersionHash; // For cache invalidation if hardware changes
	bool _shaderBinaryCacheInvalidated;

public:
	GLHeader GLVersionDeclaration;
	GLHeader GLComputeVersionDeclaration;
	GLHeader GLCompatHeader;
	GLHeader GLVertexHeader;
	GLHeader GLFragmentHeader;
	GLHeader GLComputeHeader;
	GLHeader GLWorldHeader;
	GLHeader GLEngineConstants;

	GLShaderManager() {}
	~GLShaderManager();

	void InitDriverInfo();

	void GenerateBuiltinHeaders();
	void GenerateWorldHeaders();

	template< class T >
	void load( T *& shader )
	{
		if( _deformShaders.size() == 0 ) {
			Q_UNUSED(getDeformShaderIndex( nullptr, 0 ));
		}

		shader = new T( this );
		InitShader( shader );
		_shaders.emplace_back( shader );
		_shaderBuildQueue.push( shader );
	}
	void freeAll();

	int getDeformShaderIndex( deformStage_t *deforms, int numDeforms );

	bool buildPermutation( GLShader *shader, int macroIndex, int deformIndex );
	void buildAll();
private:
	struct InfoLogEntry {
		int line;
		int character;
		std::string token;
		std::string error;
	};

	bool LoadShaderBinary( GLShader *shader, size_t permutation );
	void SaveShaderBinary( GLShader *shader, size_t permutation );
	GLuint CompileShader( Str::StringRef programName, Str::StringRef shaderText,
			      std::initializer_list<const GLHeader *> headers,
			      GLenum shaderType ) const;
	void CompileGPUShaders( GLShader *shader, shaderProgram_t *program,
				const std::string &compileMacros );
	void CompileAndLinkGPUShaderProgram( GLShader *shader, shaderProgram_t *program,
	                                     Str::StringRef compileMacros, int deformIndex );
	std::string ShaderPostProcess( GLShader *shader, const std::string& shaderText );
	std::string BuildDeformShaderText( const std::string& steps );
	std::string ProcessInserts( const std::string& shaderText, const uint32_t offset ) const;
	void LinkProgram( GLuint program ) const;
	void BindAttribLocations( GLuint program ) const;
	void PrintShaderSource( Str::StringRef programName, GLuint object, std::vector<InfoLogEntry>& infoLogLines ) const;
	std::vector<InfoLogEntry> ParseInfoLog( const std::string& infoLog ) const;
	std::string GetInfoLog( GLuint object ) const;
	void InitShader( GLShader *shader );
	void UpdateShaderProgramUniformLocations( GLShader *shader, shaderProgram_t *shaderProgram ) const;
};

class GLUniform
{
protected:
	GLShader   *_shader;
	std::string _name;
	const std::string _type;

	// In multiples of 4 bytes
	const GLuint _std430Size;
	const GLuint _std430Alignment;

	const bool _global; // This uniform won't go into materials SSBO if true
	const int _components;
	const bool _isTexture;

	size_t      _firewallIndex;
	size_t      _locationIndex;

	GLUniform( GLShader *shader, const char *name, const char* type, const GLuint std430Size, const GLuint std430Alignment,
	                             const bool global, const int components = 0,
	                             const bool isTexture = false ) :
		_shader( shader ),
		_name( name ),
		_type( type ),
		_std430Size( std430Size ),
		_std430Alignment( std430Alignment ),
		_global( global ),
		_components( components ),
		_isTexture( isTexture ),
		_firewallIndex( 0 ),
		_locationIndex( 0 )
	{
		_shader->RegisterUniform( this );
	}

public:
	virtual ~GLUniform() = default;

	void SetFirewallIndex( size_t offSetValue )
	{
		_firewallIndex = offSetValue;
	}

	void SetLocationIndex( size_t index )
	{
		_locationIndex = index;
	}

	const char *GetName()
	{
		return _name.c_str();
	}

	const std::string GetType() const {
		return _type;
	}

	GLuint GetSTD430Size() const;

	GLuint GetSTD430Alignment() const;

	int GetComponentSize() const {
		return _components;
	}

	bool IsGlobal() const {
		return _global;
	}

	bool IsTexture() const {
		return _isTexture;
	}

	// This should return a pointer to the memory right after the one this uniform wrote to
	virtual uint32_t* WriteToBuffer( uint32_t* buffer );

	void UpdateShaderProgramUniformLocation( shaderProgram_t *shaderProgram )
	{
		shaderProgram->uniformLocations[ _locationIndex ] = glGetUniformLocation( shaderProgram->program, GetName() );
	}

	virtual size_t GetSize()
	{
		return 0;
	}
};

class GLUniformSampler : protected GLUniform {
	protected:
	GLUniformSampler( GLShader* shader, const char* name, const char* type, const GLuint size, const bool global = false ) :
		GLUniform( shader, name, type, glConfig2.bindlessTexturesAvailable ? size * 2 : size,
									   glConfig2.bindlessTexturesAvailable ? size * 2 : size, global, 0, true ) {
	}

	inline GLint GetLocation() {
		shaderProgram_t* p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

		return p->uniformLocations[_locationIndex];
	}

	inline size_t GetFirewallIndex() const {
		return _firewallIndex;
	}

	public:
	size_t GetSize() override {
		return sizeof( GLuint64 );
	}

	void SetValue( GLuint value ) {
		currentValue = value;
	}

	void SetValueBindless( GLint64 value ) {
		currentValueBindless = value;

		if ( glConfig2.usingBindlessTextures && ( !_shader->UseMaterialSystem() || _global ) ) {
			glUniformHandleui64ARB( GetLocation(), currentValueBindless );
		}
	}

	uint32_t* WriteToBuffer( uint32_t* buffer ) override {
		uint32_t* bufferNext = buffer;
		if ( glConfig2.usingBindlessTextures ) {
			memcpy( buffer, &currentValueBindless, sizeof( GLuint64 ) );
			bufferNext += 2;
		} else {
			memcpy( buffer, &currentValue, sizeof( GLint ) );
			bufferNext += 1;
		}
		return bufferNext;
	}

	private:
	GLuint64 currentValueBindless = 0;
	GLuint currentValue = 0;
};

class GLUniformSampler1D : protected GLUniformSampler {
	protected:
	GLUniformSampler1D( GLShader* shader, const char* name ) :
		GLUniformSampler( shader, name, "sampler1D", 1 ) {
	}

	inline GLint GetLocation() {
		shaderProgram_t* p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

		return p->uniformLocations[_locationIndex];
	}

	public:
	size_t GetSize() override {
		return sizeof( GLuint64 );
	}
};

class GLUniformSampler2D : protected GLUniformSampler {
	protected:
	GLUniformSampler2D( GLShader* shader, const char* name, const bool global = false ) :
		GLUniformSampler( shader, name, "sampler2D", 1, global ) {
	}

	inline GLint GetLocation() {
		shaderProgram_t* p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

		return p->uniformLocations[_locationIndex];
	}

	public:
	size_t GetSize() override {
		return sizeof( GLuint64 );
	}
};

class GLUniformSampler3D : protected GLUniformSampler {
	protected:
	GLUniformSampler3D( GLShader* shader, const char* name, const bool global = false ) :
		GLUniformSampler( shader, name, "sampler3D", 1, global ) {
	}

	inline GLint GetLocation() {
		shaderProgram_t* p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

		return p->uniformLocations[_locationIndex];
	}

	public:
	size_t GetSize() override {
		return sizeof( GLuint64 );
	}
};

class GLUniformUSampler3D : protected GLUniformSampler {
	protected:
	GLUniformUSampler3D( GLShader* shader, const char* name, const bool global = false ) :
		GLUniformSampler( shader, name, "usampler3D", 1, global ) {
	}

	inline GLint GetLocation() {
		shaderProgram_t* p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

		return p->uniformLocations[_locationIndex];
	}

	public:
	size_t GetSize() override {
		return sizeof( GLuint64 );
	}
};

class GLUniformSamplerCube : protected GLUniformSampler {
	protected:
	GLUniformSamplerCube( GLShader* shader, const char* name, const bool global = false ) :
		GLUniformSampler( shader, name, "samplerCube", 1, global ) {
	}

	inline GLint GetLocation() {
		shaderProgram_t* p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

		return p->uniformLocations[_locationIndex];
	}

	public:
	size_t GetSize() override {
		return sizeof( GLuint64 );
	}
};

class GLUniform1i : protected GLUniform
{
protected:
	GLUniform1i( GLShader *shader, const char *name, const bool global = false ) :
	GLUniform( shader, name, "int", 1, 1, global )
	{
	}

	inline void SetValue( int value )
	{
		shaderProgram_t *p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniform1i( %s, shader: %s, value: %d ) ---\n",
				this->GetName(), _shader->GetName().c_str(), value ) );
		}
#endif

		if ( _shader->UseMaterialSystem() && !_global ) {
			currentValue = value;
			return;
		}

#if defined( USE_UNIFORM_FIREWALL )
		int *firewall = ( int * ) &p->uniformFirewall[ _firewallIndex ];

		if ( *firewall == value )
		{
			return;
		}

		*firewall = value;
#endif
		glUniform1i( p->uniformLocations[ _locationIndex ], value );
	}
public:
	size_t GetSize() override
	{
		return sizeof( int );
	}

	uint32_t* WriteToBuffer( uint32_t* buffer ) override {
		memcpy( buffer, &currentValue, sizeof( int ) );
		return buffer + 1;
	}

	private:
	int currentValue = 0;
};

class GLUniform1ui : protected GLUniform {
	protected:
	GLUniform1ui( GLShader* shader, const char* name, const bool global = false ) :
		GLUniform( shader, name, "uint", 1, 1, global ) {
	}

	inline void SetValue( uint value ) {
		shaderProgram_t* p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer ) {
			GLimp_LogComment( va( "GLSL_SetUniform1i( %s, shader: %s, value: %d ) ---\n",
				this->GetName(), _shader->GetName().c_str(), value ) );
		}
#endif

		if ( _shader->UseMaterialSystem() && !_global ) {
			currentValue = value;
			return;
		}

#if defined( USE_UNIFORM_FIREWALL )
		uint* firewall = ( uint* ) &p->uniformFirewall[_firewallIndex];

		if ( *firewall == value ) {
			return;
		}

		*firewall = value;
#endif
		glUniform1ui( p->uniformLocations[_locationIndex], value );
	}
	public:
	size_t GetSize() override {
		return sizeof( uint );
	}

	uint32_t* WriteToBuffer( uint32_t* buffer ) override {
		memcpy( buffer, &currentValue, sizeof( uint ) );
		return buffer + 1;
	}

	private:
	uint currentValue = 0;
};

class GLUniform1Bool : protected GLUniform {
	protected:
	// GLSL std430 bool is always 4 bytes, which might not correspond to C++ bool
	GLUniform1Bool( GLShader* shader, const char* name, const bool global ) :
		GLUniform( shader, name, "bool", 1, 1, global ) {
	}

	inline void SetValue( int value ) {
		shaderProgram_t* p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer ) {
			GLimp_LogComment( va( "GLSL_SetUniform1i( %s, shader: %s, value: %d ) ---\n",
				this->GetName(), _shader->GetName().c_str(), value ) );
		}
#endif

		if ( _shader->UseMaterialSystem() && !_global ) {
			currentValue = value;
			return;
		}

#if defined( USE_UNIFORM_FIREWALL )
		int* firewall = ( int* ) &p->uniformFirewall[_firewallIndex];

		if ( *firewall == value ) {
			return;
		}

		*firewall = value;
#endif
		glUniform1i( p->uniformLocations[_locationIndex], value );
	}

	public:
	size_t GetSize() override {
		return sizeof( int );
	}

	uint32_t* WriteToBuffer( uint32_t *buffer ) override {
		memcpy( buffer, &currentValue, sizeof( bool ) );
		return buffer + 1;
	}

	private:
	int currentValue = 0;
};

class GLUniform1f : protected GLUniform
{
protected:
	GLUniform1f( GLShader *shader, const char *name, const bool global = false ) :
	GLUniform( shader, name, "float", 1, 1, global )
	{
	}

	inline void SetValue( float value )
	{
		shaderProgram_t *p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniform1f( %s, shader: %s, value: %f ) ---\n",
				this->GetName(), _shader->GetName().c_str(), value ) );
		}
#endif

		if ( _shader->UseMaterialSystem() && !_global ) {
			currentValue = value;
			return;
		}

#if defined( USE_UNIFORM_FIREWALL )
		float *firewall = ( float * ) &p->uniformFirewall[ _firewallIndex ];

		if ( *firewall == value )
		{
			return;
		}

		*firewall = value;
#endif
		glUniform1f( p->uniformLocations[ _locationIndex ], value );
	}
public:
	size_t GetSize() override
	{
		return sizeof( float );
	}

	uint32_t* WriteToBuffer( uint32_t* buffer ) override {
		memcpy( buffer, &currentValue, sizeof( float ) );
		return buffer + 1;
	}
	
	private:
	float currentValue = 0;
};

class GLUniform1fv : protected GLUniform
{
protected:
	GLUniform1fv( GLShader *shader, const char *name, const int size ) :
	GLUniform( shader, name, "float", 1, 1, false, size )
	{
		currentValue.reserve( size );
	}

	inline void SetValue( int numFloats, float *f )
	{
		shaderProgram_t *p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniform1fv( %s, shader: %s, numFloats: %d ) ---\n",
				this->GetName(), _shader->GetName().c_str(), numFloats ) );
		}
#endif

		if ( _shader->UseMaterialSystem() && !_global ) {
			memcpy( currentValue.data(), f, numFloats * sizeof( float ) );
			return;
		}

		glUniform1fv( p->uniformLocations[ _locationIndex ], numFloats, f );
	}

	uint32_t* WriteToBuffer( uint32_t* buffer ) override {
		memcpy( buffer, currentValue.data(), currentValue.size() * sizeof( float ) );
		return buffer + _components;
	}

	private:
	std::vector<float> currentValue;
};

class GLUniform2f : protected GLUniform
{
protected:
	GLUniform2f( GLShader *shader, const char *name ) :
	GLUniform( shader, name, "vec2", 2, 2, false )
	{
		currentValue[0] = 0.0;
		currentValue[1] = 0.0;
	}

	inline void SetValue( const vec2_t v )
	{
		shaderProgram_t *p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniform2f( %s, shader: %s, value: [ %f, %f ] ) ---\n",
				this->GetName(), _shader->GetName().c_str(), v[ 0 ], v[ 1 ] ) );
		}
#endif

		if ( _shader->UseMaterialSystem() && !_global ) {
			Vector2Copy( v, currentValue );
			return;
		}

#if defined( USE_UNIFORM_FIREWALL )
		vec2_t *firewall = ( vec2_t * ) &p->uniformFirewall[ _firewallIndex ];

		if ( ( *firewall )[ 0 ] == v[ 0 ] && ( *firewall )[ 1 ] == v[ 1 ] )
		{
			return;
		}

		( *firewall )[ 0 ] = v[ 0 ];
		( *firewall )[ 1 ] = v[ 1 ];
#endif
		glUniform2f( p->uniformLocations[ _locationIndex ], v[ 0 ], v[ 1 ] );
	}

	size_t GetSize() override
	{
		return sizeof( vec2_t );
	}

	uint32_t* WriteToBuffer( uint32_t* buffer ) override {
		memcpy( buffer, &currentValue, sizeof( vec2_t ) );
		return buffer + 2;
	}

	private:
	vec2_t currentValue;
};

class GLUniform3f : protected GLUniform
{
protected:
	GLUniform3f( GLShader *shader, const char *name, const bool global = false ) :
	GLUniform( shader, name, "vec3", 3, 4, global )
	{
		currentValue[0] = 0.0;
		currentValue[1] = 0.0;
		currentValue[2] = 0.0;
	}

	inline void SetValue( const vec3_t v )
	{
		shaderProgram_t *p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniform3f( %s, shader: %s, value: [ %f, %f, %f ] ) ---\n",
				this->GetName(), _shader->GetName().c_str(), v[ 0 ], v[ 1 ], v[ 2 ] ) );
		}
#endif

		if ( _shader->UseMaterialSystem() && !_global ) {
			VectorCopy( v, currentValue );
			return;
		}

#if defined( USE_UNIFORM_FIREWALL )
		vec3_t *firewall = ( vec3_t * ) &p->uniformFirewall[ _firewallIndex ];

		if ( VectorCompare( *firewall, v ) )
		{
			return;
		}

		VectorCopy( v, *firewall );
#endif
		glUniform3f( p->uniformLocations[ _locationIndex ], v[ 0 ], v[ 1 ], v[ 2 ] );
	}
public:
	size_t GetSize() override
	{
		return sizeof( vec3_t );
	}

	uint32_t* WriteToBuffer( uint32_t* buffer ) override {
		memcpy( buffer, &currentValue, sizeof( vec3_t ) );
		return buffer + 4; // vec3 is aligned to 4 components
	}

	private:
	vec3_t currentValue;
};

class GLUniform4f : protected GLUniform
{
protected:
	GLUniform4f( GLShader *shader, const char *name, const bool global = false ) :
	GLUniform( shader, name, "vec4", 4, 4, global )
	{
		currentValue[0] = 0.0;
		currentValue[1] = 0.0;
		currentValue[2] = 0.0;
		currentValue[3] = 0.0;
	}

	inline void SetValue( const vec4_t v )
	{
		shaderProgram_t *p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniform4f( %s, shader: %s, value: [ %f, %f, %f, %f ] ) ---\n",
				this->GetName(), _shader->GetName().c_str(), v[ 0 ], v[ 1 ], v[ 2 ], v[ 3 ] ) );
		}
#endif

		if ( _shader->UseMaterialSystem() && !_global ) {
			Vector4Copy( v, currentValue );
			return;
		}

#if defined( USE_UNIFORM_FIREWALL )
		vec4_t *firewall = ( vec4_t * ) &p->uniformFirewall[ _firewallIndex ];

		if ( !memcmp( *firewall, v, sizeof( *firewall ) ) )
		{
			return;
		}

		Vector4Copy( v, *firewall );
#endif
		glUniform4f( p->uniformLocations[ _locationIndex ], v[ 0 ], v[ 1 ], v[ 2 ], v[ 3 ] );
	}
public:
	size_t GetSize() override
	{
		return sizeof( vec4_t );
	}

	uint32_t* WriteToBuffer( uint32_t* buffer ) override {
		memcpy( buffer, &currentValue, sizeof( vec4_t ) );
		return buffer + 4;
	}

	private:
	vec4_t currentValue;
};

class GLUniform4fv : protected GLUniform
{
protected:
	GLUniform4fv( GLShader *shader, const char *name, const int size ) :
	GLUniform( shader, name, "vec4", 4, 4, false, size )
	{
		currentValue.reserve( size );
	}

	inline void SetValue( int numV, vec4_t *v )
	{
		shaderProgram_t *p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniform4fv( %s, shader: %s, numV: %d ) ---\n",
				this->GetName(), _shader->GetName().c_str(), numV ) );
		}
#endif

		if ( _shader->UseMaterialSystem() && !_global ) {
			memcpy( currentValue.data(), v, numV * sizeof( vec4_t ) );
			return;
		}

		glUniform4fv( p->uniformLocations[ _locationIndex ], numV, &v[ 0 ][ 0 ] );
	}

	public:
	uint32_t* WriteToBuffer( uint32_t* buffer ) override {
		memcpy( buffer, currentValue.data(), currentValue.size() * sizeof( float ) );
		return buffer + 4 * _components;
	}

	private:
	std::vector<float> currentValue;
};

class GLUniformMatrix4f : protected GLUniform
{
protected:
	GLUniformMatrix4f( GLShader *shader, const char *name, const bool global = false ) :
	GLUniform( shader, name, "mat4", 16, 4, global )
	{
		MatrixIdentity( currentValue );
	}

	inline void SetValue( GLboolean transpose, const matrix_t m )
	{
		shaderProgram_t *p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniformMatrix4f( %s, shader: %s, transpose: %d, [ %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f, %f ] ) ---\n",
				this->GetName(), _shader->GetName().c_str(), transpose,
				m[ 0 ], m[ 1 ], m[ 2 ], m[ 3 ], m[ 4 ], m[ 5 ], m[ 6 ], m[ 7 ], m[ 8 ], m[ 9 ], m[ 10 ], m[ 11 ], m[ 12 ],
				m[ 13 ], m[ 14 ], m[ 15 ] ) );
		}
#endif

		if ( _shader->UseMaterialSystem() && !_global ) {
			MatrixCopy( m, currentValue );
			return;
		}

#if defined( USE_UNIFORM_FIREWALL )
		matrix_t *firewall = ( matrix_t * ) &p->uniformFirewall[ _firewallIndex ];

		if ( MatrixCompare( m, *firewall ) )
		{
			return;
		}

		MatrixCopy( m, *firewall );
#endif
		glUniformMatrix4fv( p->uniformLocations[ _locationIndex ], 1, transpose, m );
	}
public:
	size_t GetSize() override
	{
		return sizeof( matrix_t );
	}

	uint32_t* WriteToBuffer( uint32_t* buffer ) override {
		memcpy( buffer, &currentValue, sizeof( matrix_t ) );
		return buffer + 16;
	}

	private:
	matrix_t currentValue;
};

class GLUniformMatrix32f : protected GLUniform {
	protected:
	GLUniformMatrix32f( GLShader* shader, const char* name, const bool global = false ) :
		GLUniform( shader, name, "mat3x2", 6, 2, global ) {
	}

	inline void SetValue( GLboolean transpose, const vec_t* m ) {
		shaderProgram_t* p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer ) {
			GLimp_LogComment( va( "GLSL_SetUniformMatrix32f( %s, shader: %s, transpose: %d, [ %f, %f, %f, %f, %f, %f ] ) ---\n",
				this->GetName(), _shader->GetName().c_str(), transpose,
				m[0], m[1], m[2], m[3], m[4], m[5] ) );
		}
#endif

		if ( _shader->UseMaterialSystem() && !_global ) {
			memcpy( currentValue, m, 6 * sizeof( float ) );
			return;
		}

		glUniformMatrix3x2fv( p->uniformLocations[_locationIndex], 1, transpose, m );
	}
	public:
	size_t GetSize() override {
		return 6 * sizeof( float );
	}

	uint32_t* WriteToBuffer( uint32_t* buffer ) override {
		memcpy( buffer, currentValue, 6 * sizeof( float ) );
		return buffer + 6 * _components;
	}

	private:
	vec_t currentValue[6] {};
};

class GLUniformMatrix4fv : protected GLUniform
{
protected:
	GLUniformMatrix4fv( GLShader *shader, const char *name, const int size ) :
	GLUniform( shader, name, "mat4", 16, 4, false, size )
	{
		currentValue.reserve( size * 16 );
	}

	inline void SetValue( int numMatrices, GLboolean transpose, const matrix_t *m )
	{
		shaderProgram_t *p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniformMatrix4fv( %s, shader: %s, numMatrices: %d, transpose: %d ) ---\n",
				this->GetName(), _shader->GetName().c_str(), numMatrices, transpose ) );
		}
#endif

		if ( _shader->UseMaterialSystem() && !_global ) {
			memcpy( currentValue.data(), m, numMatrices * sizeof( matrix_t ) );
			return;
		}

		glUniformMatrix4fv( p->uniformLocations[ _locationIndex ], numMatrices, transpose, &m[ 0 ][ 0 ] );
	}

	public:
	uint32_t* WriteToBuffer( uint32_t* buffer ) override {
		memcpy( buffer, currentValue.data(), currentValue.size() * sizeof( float ) );
		return buffer + 16 * _components;
	}

	private:
	std::vector<float> currentValue;
};

class GLUniformMatrix34fv : protected GLUniform
{
protected:
	GLUniformMatrix34fv( GLShader *shader, const char *name, const int size ) :
	GLUniform( shader, name, "mat3x4", 12, 4, false, size )
	{
	}

	inline void SetValue( int numMatrices, GLboolean transpose, const float *m )
	{
		shaderProgram_t *p = _shader->GetProgram();

		if ( _global || !_shader->UseMaterialSystem() ) {
			ASSERT_EQ( p, glState.currentProgram );
		}

#if defined( LOG_GLSL_UNIFORMS )
		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "GLSL_SetUniformMatrix34fv( %s, shader: %s, numMatrices: %d, transpose: %d ) ---\n",
				this->GetName(), _shader->GetName().c_str(), numMatrices, transpose ) );
		}
#endif

		if ( _shader->UseMaterialSystem() && !_global ) {
			memcpy( currentValue.data(), m, numMatrices * sizeof( matrix_t ) );
			return;
		}

		glUniformMatrix3x4fv( p->uniformLocations[ _locationIndex ], numMatrices, transpose, m );
	}

	public:
	uint32_t* WriteToBuffer( uint32_t* buffer ) override {
		memcpy( buffer, currentValue.data(), currentValue.size() * sizeof( float ) );
		return buffer + 12 * _components;
	}

	private:
	std::vector<float> currentValue;
};

class GLUniformBlock
{
protected:
	GLShader   *_shader;
	std::string _name;
	size_t      _locationIndex;

	GLUniformBlock( GLShader *shader, const char *name ) :
		_shader( shader ),
		_name( name ),
		_locationIndex( 0 )
	{
		_shader->RegisterUniformBlock( this );
	}

public:
	void SetLocationIndex( size_t index )
	{
		_locationIndex = index;
	}

	const char *GetName()
	{
		return _name.c_str();
	}

	void UpdateShaderProgramUniformBlockIndex( shaderProgram_t *shaderProgram )
	{
		shaderProgram->uniformBlockIndexes[ _locationIndex ] = glGetUniformBlockIndex( shaderProgram->program, GetName() );
	}

	void SetBuffer( GLuint buffer ) {
		shaderProgram_t *p = _shader->GetProgram();
		GLuint blockIndex = p->uniformBlockIndexes[ _locationIndex ];

		ASSERT_EQ(p, glState.currentProgram);

		if( blockIndex != GL_INVALID_INDEX ) {
			glBindBufferBase( GL_UNIFORM_BUFFER, blockIndex, buffer );
		}
	}
};

class GLBuffer {
	public:
	friend class GLVAO;

	std::string name;
	const GLuint64 SYNC_TIMEOUT = 10000000000; // 10 seconds

	GLBuffer( const char* newName, const GLuint newBindingPoint, const GLbitfield newFlags, const GLbitfield newMapFlags ) :
		name( newName ),
		internalTarget( 0 ),
		internalBindingPoint( newBindingPoint ),
		flags( newFlags ),
		mapFlags( newMapFlags ) {
	}

	GLBuffer( const char* newName, const GLenum newTarget, const GLuint newBindingPoint,
		const GLbitfield newFlags, const GLbitfield newMapFlags ) :
		name( newName ),
		internalTarget( newTarget ),
		internalBindingPoint( newBindingPoint ),
		flags( newFlags ),
		mapFlags( newMapFlags ) {
	}

	void BindBufferBase( GLenum target = 0, GLuint bindingPoint = 0 ) {
		target = target ? target : internalTarget;
		bindingPoint = bindingPoint ? bindingPoint : internalBindingPoint;
		glBindBufferBase( target, bindingPoint, id );
	}

	void UnBindBufferBase( GLenum target = 0, GLuint bindingPoint = 0 ) {
		target = target ? target : internalTarget;
		bindingPoint = bindingPoint ? bindingPoint : internalBindingPoint;
		glBindBufferBase( target, bindingPoint, 0 );
	}

	void BindBuffer( GLenum target = 0 ) {
		target = target ? target : internalTarget;
		glBindBuffer( target, id );
	}

	void UnBindBuffer( GLenum target = 0 ) {
		target = target ? target : internalTarget;
		glBindBuffer( target, 0 );
	}

	void BufferData( const GLsizeiptr size, const void* data, const GLenum usageFlags ) {
		glNamedBufferData( id, size * sizeof( uint32_t ), data, usageFlags );
	}

	void BufferStorage( const GLsizeiptr newAreaSize, const GLsizeiptr areaCount, const void* data ) {
		areaSize = newAreaSize;
		maxAreas = areaCount;
		glNamedBufferStorage( id, areaSize * areaCount * sizeof( uint32_t ), data, flags );
		syncs.resize( areaCount );
	}

	void AreaIncr() {
		syncs[area] = glFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
		area++;
		if ( area >= maxAreas ) {
			area = 0;
		}
	}

	void MapAll() {
		if ( !mapped ) {
			mapped = true;
			data = ( uint32_t* ) glMapNamedBufferRange( id, 0, areaSize * maxAreas * sizeof( uint32_t ), flags | mapFlags );
		}
	}

	uint32_t* GetCurrentAreaData() {
		if ( syncs[area] != nullptr ) {
			if ( glClientWaitSync( syncs[area], GL_SYNC_FLUSH_COMMANDS_BIT, SYNC_TIMEOUT ) == GL_TIMEOUT_EXPIRED ) {
				Sys::Drop( "Failed buffer %s area %u sync", name, area );
			}
			glDeleteSync( syncs[area] );
		}

		return data + area * areaSize;
	}

	uint32_t* GetData() {
		return data;
	}

	void FlushCurrentArea() {
		glFlushMappedNamedBufferRange( id, area * areaSize * sizeof( uint32_t ), areaSize * sizeof( uint32_t ) );
	}

	void FlushAll() {
		glFlushMappedNamedBufferRange( id, 0, maxAreas * areaSize * sizeof( uint32_t ) );
	}

	uint32_t* MapBufferRange( const GLuint count ) {
		return MapBufferRange( 0, count );
	}

	uint32_t* MapBufferRange( const GLuint offset, const GLuint count ) {
		if ( !mapped ) {
			mapped = true;
			data = ( uint32_t* ) glMapNamedBufferRange( id,
				offset * sizeof( uint32_t ), count * sizeof( uint32_t ),
				flags | mapFlags );
		}

		return data;
	}

	void UnmapBuffer() {
		if ( mapped ) {
			mapped = false;
			glUnmapNamedBuffer( id );
		}
	}

	void GenBuffer() {
		glCreateBuffers( 1, &id );
	}

	void DelBuffer() {
		glDeleteBuffers( 1, &id );
	}

	private:
	const GLenum internalTarget;
	const GLuint internalBindingPoint;

	GLuint id;

	bool mapped = false;
	const GLbitfield flags;
	const GLbitfield mapFlags;

	std::vector<GLsync> syncs;
	GLsizeiptr area = 0;
	GLsizeiptr areaSize = 0;
	GLsizeiptr maxAreas = 0;
	uint32_t* data;
};

// Shorthands for buffers that are only bound to one specific target
class GLSSBO : public GLBuffer {
	public:
	GLSSBO( const char* name, const GLuint bindingPoint, const GLbitfield flags, const GLbitfield mapFlags ) :
		GLBuffer( name, GL_SHADER_STORAGE_BUFFER, bindingPoint, flags, mapFlags ) {
	}
};

class GLUBO : public GLBuffer {
	public:
	GLUBO( const char* name, const GLsizeiptr bindingPoint, const GLbitfield flags, const GLbitfield mapFlags ) :
		GLBuffer( name, GL_UNIFORM_BUFFER, bindingPoint, flags, mapFlags ) {
	}
};

class GLAtomicCounterBuffer : public GLBuffer {
	public:
	GLAtomicCounterBuffer( const char* name, const GLsizeiptr bindingPoint, const GLbitfield flags, const GLbitfield mapFlags ) :
		GLBuffer( name, GL_ATOMIC_COUNTER_BUFFER, bindingPoint, flags, mapFlags ) {
	}
};

class GLVAO {
	public:
	vboAttributeLayout_t attrs[ATTR_INDEX_MAX];
	uint32_t enabledAttrs;

	GLVAO( const GLuint newVBOBindingPoint ) :
		VBOBindingPoint( newVBOBindingPoint ) {
	}

	~GLVAO() = default;

	void Bind() {
		glBindVertexArray( id );
	}

	void SetAttrs( const vertexAttributeSpec_t* attrBegin, const vertexAttributeSpec_t* attrEnd ) {
		uint32_t ofs = 0;
		for ( const vertexAttributeSpec_t* spec = attrBegin; spec < attrEnd; spec++ ) {
			vboAttributeLayout_t& attr = attrs[spec->attrIndex];
			ASSERT_NQ( spec->numComponents, 0U );
			attr.componentType = spec->componentStorageType;
			if ( attr.componentType == GL_HALF_FLOAT && !glConfig2.halfFloatVertexAvailable ) {
				attr.componentType = GL_FLOAT;
			}
			attr.numComponents = spec->numComponents;
			attr.normalize = spec->attrOptions & ATTR_OPTION_NORMALIZE ? GL_TRUE : GL_FALSE;

			attr.ofs = ofs;
			ofs += attr.numComponents * R_ComponentSize( attr.componentType );
			ofs = ( ofs + 3 ) & ~3; // 4 is minimum alignment for any vertex attribute

			enabledAttrs |= 1 << spec->attrIndex;
		}

		stride = ofs;

		for ( const vertexAttributeSpec_t* spec = attrBegin; spec < attrEnd; spec++ ) {
			const int index = spec->attrIndex;
			vboAttributeLayout_t& attr = attrs[index];

			attr.stride = stride;

			glEnableVertexArrayAttrib( id, index );
			glVertexArrayAttribFormat( id, index, attr.numComponents, attr.componentType,
				attr.normalize, attr.ofs );
			glVertexArrayAttribBinding( id, index, VBOBindingPoint );
		}
	}

	void SetVertexBuffer( const GLBuffer buffer, const GLuint offset ) {
		glVertexArrayVertexBuffer( id, VBOBindingPoint, buffer.id, offset, stride );
	}

	void SetIndexBuffer( const GLBuffer buffer ) {
		glVertexArrayElementBuffer( id, buffer.id );
	}

	void GenVAO() {
		glGenVertexArrays( 1, &id );
	}

	void DelVAO() {
		glDeleteVertexArrays( 1, &id );
	}

	private:
	GLuint id;
	const GLuint VBOBindingPoint;
	GLuint stride;
};

class GLCompileMacro
{
private:
	int     _bit;

protected:
	GLShader *_shader;

	GLCompileMacro( GLShader *shader ) :
		_shader( shader )
	{
		_bit = BIT( _shader->GetNumOfCompiledMacros() );
		_shader->RegisterCompileMacro( this );
	}

// RB: This is not good oo design, but it can be a workaround and its cost is more or less only a virtual function call.
// It also works regardless of RTTI is enabled or not.
	enum EGLCompileMacro : unsigned
	{
	  USE_BSP_SURFACE,
	  USE_VERTEX_SKINNING,
	  USE_VERTEX_ANIMATION,
	  USE_TCGEN_ENVIRONMENT,
	  USE_TCGEN_LIGHTMAP,
	  USE_DELUXE_MAPPING,
	  USE_GRID_LIGHTING,
	  USE_GRID_DELUXE_MAPPING,
	  USE_HEIGHTMAP_IN_NORMALMAP,
	  USE_RELIEF_MAPPING,
	  USE_REFLECTIVE_SPECULAR,
	  USE_SHADOWING,
	  LIGHT_DIRECTIONAL,
	  USE_DEPTH_FADE,
	  USE_PHYSICAL_MAPPING,
	};

public:
	virtual const char       *GetName() const = 0;
	virtual EGLCompileMacro GetType() const = 0;

	virtual bool            HasConflictingMacros( size_t, const std::vector<GLCompileMacro*>& ) const
	{
		return false;
	}

	virtual bool            MissesRequiredMacros( size_t, const std::vector<GLCompileMacro*>& ) const
	{
		return false;
	}

	virtual uint32_t        GetRequiredVertexAttributes() const
	{
		return 0;
	}

	void SetMacro( bool enable )
	{
		int bit = GetBit();

		if ( enable && !_shader->IsMacroSet( bit ) )
		{
			_shader->AddMacroBit( bit );
		}
		else if ( !enable && _shader->IsMacroSet( bit ) )
		{
			_shader->DelMacroBit( bit );
		}
		// else do nothing because already enabled/disabled
	}

public:
	int GetBit() const
	{
		return _bit;
	}

	virtual ~GLCompileMacro() = default;
};

class GLCompileMacro_USE_BSP_SURFACE :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_BSP_SURFACE( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_BSP_SURFACE";
	}

	bool HasConflictingMacros(size_t permutation, const std::vector< GLCompileMacro * > &macros) const override;

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_BSP_SURFACE;
	}

	void SetBspSurface( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_VERTEX_SKINNING :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_VERTEX_SKINNING( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_VERTEX_SKINNING";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_VERTEX_SKINNING;
	}

	bool HasConflictingMacros( size_t permutation, const std::vector< GLCompileMacro * > &macros ) const override;
	bool MissesRequiredMacros( size_t permutation, const std::vector< GLCompileMacro * > &macros ) const override;

	uint32_t        GetRequiredVertexAttributes() const override
	{
		return ATTR_BONE_FACTORS;
	}

	void SetVertexSkinning( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_VERTEX_ANIMATION :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_VERTEX_ANIMATION( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_VERTEX_ANIMATION";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_VERTEX_ANIMATION;
	}

	bool     HasConflictingMacros( size_t permutation, const std::vector< GLCompileMacro * > &macros ) const override;
	uint32_t GetRequiredVertexAttributes() const override;

	void SetVertexAnimation( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_TCGEN_ENVIRONMENT :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_TCGEN_ENVIRONMENT( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_TCGEN_ENVIRONMENT";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_TCGEN_ENVIRONMENT;
	}

	bool     HasConflictingMacros(size_t permutation, const std::vector< GLCompileMacro * > &macros) const override;
	uint32_t        GetRequiredVertexAttributes() const override
	{
		return ATTR_QTANGENT;
	}

	void SetTCGenEnvironment( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_TCGEN_LIGHTMAP :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_TCGEN_LIGHTMAP( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_TCGEN_LIGHTMAP";
	}

	bool     HasConflictingMacros(size_t permutation, const std::vector< GLCompileMacro * > &macros) const override;
	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_TCGEN_LIGHTMAP;
	}

	void SetTCGenLightmap( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_GRID_LIGHTING :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_GRID_LIGHTING( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_GRID_LIGHTING";
	}

	bool HasConflictingMacros(size_t permutation, const std::vector< GLCompileMacro * > &macros) const override;

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_GRID_LIGHTING;
	}

	void SetGridLighting( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_DELUXE_MAPPING :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_DELUXE_MAPPING( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_DELUXE_MAPPING";
	}

	bool HasConflictingMacros(size_t permutation, const std::vector< GLCompileMacro * > &macros) const override;

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_DELUXE_MAPPING;
	}

	void SetDeluxeMapping( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_GRID_DELUXE_MAPPING :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_GRID_DELUXE_MAPPING( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_GRID_DELUXE_MAPPING";
	}

	bool HasConflictingMacros(size_t permutation, const std::vector< GLCompileMacro * > &macros) const override;

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_GRID_DELUXE_MAPPING;
	}

	void SetGridDeluxeMapping( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_HEIGHTMAP_IN_NORMALMAP";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_HEIGHTMAP_IN_NORMALMAP;
	}

	void SetHeightMapInNormalMap( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_RELIEF_MAPPING :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_RELIEF_MAPPING( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_RELIEF_MAPPING";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_RELIEF_MAPPING;
	}

	void SetReliefMapping( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_REFLECTIVE_SPECULAR :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_REFLECTIVE_SPECULAR( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_REFLECTIVE_SPECULAR";
	}

	bool HasConflictingMacros(size_t permutation, const std::vector< GLCompileMacro * > &macros) const override;

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_REFLECTIVE_SPECULAR;
	}

	void SetReflectiveSpecular( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_LIGHT_DIRECTIONAL :
	GLCompileMacro
{
public:
	GLCompileMacro_LIGHT_DIRECTIONAL( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "LIGHT_DIRECTIONAL";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::LIGHT_DIRECTIONAL;
	}

	void SetMacro_LIGHT_DIRECTIONAL( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_SHADOWING :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_SHADOWING( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_SHADOWING";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_SHADOWING;
	}

	void SetShadowing( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_DEPTH_FADE :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_DEPTH_FADE( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_DEPTH_FADE";
	}

	EGLCompileMacro GetType() const override
	{
		return EGLCompileMacro::USE_DEPTH_FADE;
	}

	void SetDepthFade( bool enable )
	{
		SetMacro( enable );
	}
};

class GLCompileMacro_USE_PHYSICAL_MAPPING :
	GLCompileMacro
{
public:
	GLCompileMacro_USE_PHYSICAL_MAPPING( GLShader *shader ) :
		GLCompileMacro( shader )
	{
	}

	const char *GetName() const override
	{
		return "USE_PHYSICAL_MAPPING";
	}

	EGLCompileMacro GetType() const override
	{
		return USE_PHYSICAL_MAPPING;
	}

	void SetPhysicalShading( bool enable )
	{
		SetMacro( enable );
	}
};

class u_ColorMap :
	GLUniformSampler2D {
	public:
	u_ColorMap( GLShader* shader ) :
		// While u_ColorMap is used for some screen-space shaders, it's never global in material system shaders
		GLUniformSampler2D( shader, "u_ColorMap" ) {
	}

	void SetUniform_ColorMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_ColorMap() {
		return this->GetLocation();
	}
};

class u_ColorMap3D :
	GLUniformSampler3D {
	public:
	u_ColorMap3D( GLShader* shader ) :
		GLUniformSampler3D( shader, "u_ColorMap3D" ) {
	}

	void SetUniform_ColorMap3DBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_ColorMap3D() {
		return this->GetLocation();
	}
};

class u_ColorMapCube :
	GLUniformSamplerCube {
	public:
	u_ColorMapCube( GLShader* shader ) :
		GLUniformSamplerCube( shader, "u_ColorMapCube" ) {
	}

	void SetUniform_ColorMapCubeBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_ColorMapCube() {
		return this->GetLocation();
	}
};

class u_DepthMap :
	GLUniformSampler2D {
	public:
	u_DepthMap( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_DepthMap", true ) {
	}

	void SetUniform_DepthMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_DepthMap() {
		return this->GetLocation();
	}
};

class u_DiffuseMap :
	GLUniformSampler2D {
	public:
	u_DiffuseMap( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_DiffuseMap" ) {
	}

	void SetUniform_DiffuseMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_DiffuseMap() {
		return this->GetLocation();
	}
};

class u_HeightMap :
	GLUniformSampler2D {
	public:
	u_HeightMap( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_HeightMap" ) {
	}

	void SetUniform_HeightMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_HeightMap() {
		return this->GetLocation();
	}
};

class u_NormalMap :
	GLUniformSampler2D {
	public:
	u_NormalMap( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_NormalMap" ) {
	}

	void SetUniform_NormalMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_NormalMap() {
		return this->GetLocation();
	}
};

class u_MaterialMap :
	GLUniformSampler2D {
	public:
	u_MaterialMap( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_MaterialMap" ) {
	}

	void SetUniform_MaterialMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_MaterialMap() {
		return this->GetLocation();
	}
};

class u_LightMap :
	GLUniformSampler {
	public:
	u_LightMap( GLShader* shader ) :
		GLUniformSampler( shader, "u_LightMap", "sampler2D", 1, true ) {
	}

	void SetUniform_LightMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_LightMap() {
		return this->GetLocation();
	}
};

class u_DeluxeMap :
	GLUniformSampler {
	public:
	u_DeluxeMap( GLShader* shader ) :
		GLUniformSampler( shader, "u_DeluxeMap", "sampler2D", 1, true ) {
	}

	void SetUniform_DeluxeMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_DeluxeMap() {
		return this->GetLocation();
	}
};

class u_GlowMap :
	GLUniformSampler2D {
	public:
	u_GlowMap( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_GlowMap" ) {
	}

	void SetUniform_GlowMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_GlowMap() {
		return this->GetLocation();
	}
};

class u_RandomMap :
	GLUniformSampler2D {
	public:
	u_RandomMap( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_RandomMap" ) {
	}

	void SetUniform_RandomMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_RandomMap() {
		return this->GetLocation();
	}
};

class u_PortalMap :
	GLUniformSampler2D {
	public:
	u_PortalMap( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_PortalMap", true ) {
	}

	void SetUniform_PortalMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_PortalMap() {
		return this->GetLocation();
	}
};

class u_CloudMap :
	GLUniformSampler2D {
	public:
	u_CloudMap( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_CloudMap" ) {
	}

	void SetUniform_CloudMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_CloudMap() {
		return this->GetLocation();
	}
};

class u_FogMap :
	GLUniformSampler2D {
	public:
	u_FogMap( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_FogMap", true ) {
	}

	void SetUniform_FogMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_FogMap() {
		return this->GetLocation();
	}
};

class u_LightTiles :
	GLUniformSampler3D {
	public:
	u_LightTiles( GLShader* shader ) :
		GLUniformSampler3D( shader, "u_LightTiles", true ) {
	}

	void SetUniform_LightTilesBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_LightTiles() {
		return this->GetLocation();
	}
};

class u_LightGrid1 :
	GLUniformSampler3D {
	public:
	u_LightGrid1( GLShader* shader ) :
		GLUniformSampler3D( shader, "u_LightGrid1", true ) {
	}

	void SetUniform_LightGrid1Bindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_LightGrid1() {
		return this->GetLocation();
	}
};

class u_LightGrid2 :
	GLUniformSampler3D {
	public:
	u_LightGrid2( GLShader* shader ) :
		GLUniformSampler3D( shader, "u_LightGrid2", true ) {
	}

	void SetUniform_LightGrid2Bindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_LightGrid2() {
		return this->GetLocation();
	}
};

class u_EnvironmentMap0 :
	GLUniformSamplerCube {
	public:
	u_EnvironmentMap0( GLShader* shader ) :
		GLUniformSamplerCube( shader, "u_EnvironmentMap0", true ) {
	}

	void SetUniform_EnvironmentMap0Bindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_EnvironmentMap0() {
		return this->GetLocation();
	}
};

class u_EnvironmentMap1 :
	GLUniformSamplerCube {
	public:
	u_EnvironmentMap1( GLShader* shader ) :
		GLUniformSamplerCube( shader, "u_EnvironmentMap1", true ) {
	}

	void SetUniform_EnvironmentMap1Bindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_EnvironmentMap1() {
		return this->GetLocation();
	}
};

class u_CurrentMap :
	GLUniformSampler2D {
	public:
	u_CurrentMap( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_CurrentMap", true ) {
	}

	void SetUniform_CurrentMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_CurrentMap() {
		return this->GetLocation();
	}
};

class u_AttenuationMapXY :
	GLUniformSampler2D {
	public:
	u_AttenuationMapXY( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_AttenuationMapXY" ) {
	}

	void SetUniform_AttenuationMapXYBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_AttenuationMapXY() {
		return this->GetLocation();
	}
};

class u_AttenuationMapZ :
	GLUniformSampler2D {
	public:
	u_AttenuationMapZ( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_AttenuationMapZ" ) {
	}

	void SetUniform_AttenuationMapZBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_AttenuationMapZ() {
		return this->GetLocation();
	}
};

class u_ShadowMap :
	GLUniformSampler2D {
	public:
	u_ShadowMap( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_ShadowMap" ) {
	}

	void SetUniform_ShadowMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_ShadowMap() {
		return this->GetLocation();
	}
};

class u_ShadowMap0 :
	GLUniformSampler2D {
	public:
	u_ShadowMap0( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_ShadowMap0" ) {
	}

	void SetUniform_ShadowMap0Bindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_ShadowMap0() {
		return this->GetLocation();
	}
};

class u_ShadowMap1 :
	GLUniformSampler2D {
	public:
	u_ShadowMap1( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_ShadowMap1" ) {
	}

	void SetUniform_ShadowMap1Bindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_ShadowMap1() {
		return this->GetLocation();
	}
};

class u_ShadowMap2 :
	GLUniformSampler2D {
	public:
	u_ShadowMap2( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_ShadowMap2" ) {
	}

	void SetUniform_ShadowMap2Bindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_ShadowMap2() {
		return this->GetLocation();
	}
};

class u_ShadowMap3 :
	GLUniformSampler2D {
	public:
	u_ShadowMap3( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_ShadowMap3" ) {
	}

	void SetUniform_ShadowMap3Bindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_ShadowMap3() {
		return this->GetLocation();
	}
};

class u_ShadowMap4 :
	GLUniformSampler2D {
	public:
	u_ShadowMap4( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_ShadowMap4" ) {
	}

	void SetUniform_ShadowMap4Bindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_ShadowMap4() {
		return this->GetLocation();
	}
};

class u_ShadowClipMap :
	GLUniformSampler2D {
	public:
	u_ShadowClipMap( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_ShadowClipMap" ) {
	}

	void SetUniform_ShadowClipMapBindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_ShadowClipMap() {
		return this->GetLocation();
	}
};

class u_ShadowClipMap0 :
	GLUniformSampler2D {
	public:
	u_ShadowClipMap0( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_ShadowClipMap0" ) {
	}

	void SetUniform_ShadowClipMap0Bindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_ShadowClipMap0() {
		return this->GetLocation();
	}
};

class u_ShadowClipMap1 :
	GLUniformSampler2D {
	public:
	u_ShadowClipMap1( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_ShadowClipMap1" ) {
	}

	void SetUniform_ShadowClipMap1Bindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_ShadowClipMap1() {
		return this->GetLocation();
	}
};

class u_ShadowClipMap2 :
	GLUniformSampler2D {
	public:
	u_ShadowClipMap2( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_ShadowClipMap2" ) {
	}

	void SetUniform_ShadowClipMap2Bindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_ShadowClipMap2() {
		return this->GetLocation();
	}
};

class u_ShadowClipMap3 :
	GLUniformSampler2D {
	public:
	u_ShadowClipMap3( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_ShadowClipMap3" ) {
	}

	void SetUniform_ShadowClipMap3Bindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_ShadowClipMap3() {
		return this->GetLocation();
	}
};

class u_ShadowClipMap4 :
	GLUniformSampler2D {
	public:
	u_ShadowClipMap4( GLShader* shader ) :
		GLUniformSampler2D( shader, "u_ShadowClipMap4" ) {
	}

	void SetUniform_ShadowClipMap4Bindless( GLuint64 bindlessHandle ) {
		this->SetValueBindless( bindlessHandle );
	}

	GLint GetUniformLocation_ShadowClipMap4() {
		return this->GetLocation();
	}
};

class u_TextureMatrix :
	GLUniformMatrix32f
{
public:
	u_TextureMatrix( GLShader *shader ) :
		GLUniformMatrix32f( shader, "u_TextureMatrix", true )
	{
	}

	void SetUniform_TextureMatrix( const matrix_t m )
	{
		/* We only actually need these 6 components to get the correct texture transformation,
		the other ones are unused */
		vec_t m2[6];
		m2[0] = m[0];
		m2[1] = m[1];
		m2[2] = m[4];
		m2[3] = m[5];
		m2[4] = m[12];
		m2[5] = m[13];
		this->SetValue( GL_FALSE, m2 );
	}
};

class u_AlphaThreshold :
	GLUniform1f
{
public:
	u_AlphaThreshold( GLShader *shader ) :
		GLUniform1f( shader, "u_AlphaThreshold" )
	{
	}

	void SetUniform_AlphaTest( uint32_t stateBits )
	{
		float value;

		switch( stateBits & GLS_ATEST_BITS ) {
			case GLS_ATEST_GT_0:
				if ( r_dpBlend.Get() )
				{
					// DarkPlaces only supports one alphaFunc operation:
					//   https://gitlab.com/xonotic/darkplaces/blob/324a5329d33ef90df59e6488abce6433d90ac04c/model_shared.c#L1875-1876
					// Which is GE128:
					//   https://gitlab.com/xonotic/darkplaces/blob/0ea8f691e05ea968bb8940942197fa627966ff99/render.h#L95
					// Because of that, people may silently introduce regressions in their textures
					// designed for GT0 by compressing them using a lossy picture format like Jpg.
					// Xonotic texture known to trigger this bug:
					//   models/desertfactory/textures/shaders/grass01
					// Using GE128 instead would hide Jpeg artifacts while not breaking that much
					// non-DarkPlaces GT0.
					// No one operation other than GT0 an GE128 was found in whole Xonotic corpus,
					// so if there is other operations used in third party maps, they were broken
					// on DarkPlaces and will work there.
					value = 0.5f;
				}
				else
				{
					value = 1.0f;
				}
				break;
			case GLS_ATEST_LT_128:
				value = -1.5f;
				break;
			case GLS_ATEST_GE_128:
				value = 0.5f;
				break;
			case GLS_ATEST_GT_ENT:
				value = 1.0f - backEnd.currentEntity->e.shaderRGBA.Alpha() * ( 1.0f / 255.0f );
				break;
			case GLS_ATEST_LT_ENT:
				value = -2.0f + backEnd.currentEntity->e.shaderRGBA.Alpha() * ( 1.0f / 255.0f );
				break;
			default:
				value = 1.5f;
				break;
		}

		this->SetValue( value );
	}
};

class u_ViewOrigin :
	GLUniform3f
{
public:
	u_ViewOrigin( GLShader *shader ) :
		GLUniform3f( shader, "u_ViewOrigin", true )
	{
	}

	void SetUniform_ViewOrigin( const vec3_t v )
	{
		this->SetValue( v );
	}
};

class u_ViewUp :
	GLUniform3f
{
public:
	u_ViewUp( GLShader *shader ) :
		GLUniform3f( shader, "u_ViewUp", true )
	{
	}

	void SetUniform_ViewUp( const vec3_t v )
	{
		this->SetValue( v );
	}
};

class u_LightDir :
	GLUniform3f
{
public:
	u_LightDir( GLShader *shader ) :
		GLUniform3f( shader, "u_LightDir" )
	{
	}

	void SetUniform_LightDir( const vec3_t v )
	{
		this->SetValue( v );
	}
};

class u_LightOrigin :
	GLUniform3f
{
public:
	u_LightOrigin( GLShader *shader ) :
		GLUniform3f( shader, "u_LightOrigin" )
	{
	}

	void SetUniform_LightOrigin( const vec3_t v )
	{
		this->SetValue( v );
	}
};

class u_LightColor :
	GLUniform3f
{
public:
	u_LightColor( GLShader *shader ) :
		GLUniform3f( shader, "u_LightColor" )
	{
	}

	void SetUniform_LightColor( const vec3_t v )
	{
		this->SetValue( v );
	}
};

class u_LightRadius :
	GLUniform1f
{
public:
	u_LightRadius( GLShader *shader ) :
		GLUniform1f( shader, "u_LightRadius" )
	{
	}

	void SetUniform_LightRadius( float value )
	{
		this->SetValue( value );
	}
};

class u_LightScale :
	GLUniform1f
{
public:
	u_LightScale( GLShader *shader ) :
		GLUniform1f( shader, "u_LightScale" )
	{
	}

	void SetUniform_LightScale( float value )
	{
		this->SetValue( value );
	}
};

class u_LightAttenuationMatrix :
	GLUniformMatrix4f
{
public:
	u_LightAttenuationMatrix( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_LightAttenuationMatrix" )
	{
	}

	void SetUniform_LightAttenuationMatrix( const matrix_t m )
	{
		this->SetValue( GL_FALSE, m );
	}
};

class u_LightFrustum :
	GLUniform4fv
{
public:
	u_LightFrustum( GLShader *shader ) :
		GLUniform4fv( shader, "u_LightFrustum", 6 )
	{
	}

	void SetUniform_LightFrustum( vec4_t lightFrustum[ 6 ] )
	{
		this->SetValue( 6, lightFrustum );
	}
};

class u_ShadowTexelSize :
	GLUniform1f
{
public:
	u_ShadowTexelSize( GLShader *shader ) :
		GLUniform1f( shader, "u_ShadowTexelSize" )
	{
	}

	void SetUniform_ShadowTexelSize( float value )
	{
		this->SetValue( value );
	}
};

class u_ShadowBlur :
	GLUniform1f
{
public:
	u_ShadowBlur( GLShader *shader ) :
		GLUniform1f( shader, "u_ShadowBlur" )
	{
	}

	void SetUniform_ShadowBlur( float value )
	{
		this->SetValue( value );
	}
};

class u_ShadowMatrix :
	GLUniformMatrix4fv
{
public:
	u_ShadowMatrix( GLShader *shader ) :
		GLUniformMatrix4fv( shader, "u_ShadowMatrix", MAX_SHADOWMAPS )
	{
	}

	void SetUniform_ShadowMatrix( matrix_t m[ MAX_SHADOWMAPS ] )
	{
		this->SetValue( MAX_SHADOWMAPS, GL_FALSE, m );
	}
};

class u_ShadowParallelSplitDistances :
	GLUniform4f
{
public:
	u_ShadowParallelSplitDistances( GLShader *shader ) :
		GLUniform4f( shader, "u_ShadowParallelSplitDistances" )
	{
	}

	void SetUniform_ShadowParallelSplitDistances( const vec4_t v )
	{
		this->SetValue( v );
	}
};

class u_RefractionIndex :
	GLUniform1f
{
public:
	u_RefractionIndex( GLShader *shader ) :
		GLUniform1f( shader, "u_RefractionIndex" )
	{
	}

	void SetUniform_RefractionIndex( float value )
	{
		this->SetValue( value );
	}
};

class u_FresnelPower :
	GLUniform1f
{
public:
	u_FresnelPower( GLShader *shader ) :
		GLUniform1f( shader, "u_FresnelPower" )
	{
	}

	void SetUniform_FresnelPower( float value )
	{
		this->SetValue( value );
	}
};

class u_FresnelScale :
	GLUniform1f
{
public:
	u_FresnelScale( GLShader *shader ) :
		GLUniform1f( shader, "u_FresnelScale" )
	{
	}

	void SetUniform_FresnelScale( float value )
	{
		this->SetValue( value );
	}
};

class u_FresnelBias :
	GLUniform1f
{
public:
	u_FresnelBias( GLShader *shader ) :
		GLUniform1f( shader, "u_FresnelBias" )
	{
	}

	void SetUniform_FresnelBias( float value )
	{
		this->SetValue( value );
	}
};

class u_NormalScale :
	GLUniform3f
{
public:
	u_NormalScale( GLShader *shader ) :
		GLUniform3f( shader, "u_NormalScale" )
	{
	}

	void SetUniform_NormalScale( const vec3_t value )
	{
		this->SetValue( value );
	}
};


class u_FogDensity :
	GLUniform1f
{
public:
	u_FogDensity( GLShader *shader ) :
		GLUniform1f( shader, "u_FogDensity" )
	{
	}

	void SetUniform_FogDensity( float value )
	{
		this->SetValue( value );
	}
};

class u_FogColor :
	GLUniform3f
{
public:
	u_FogColor( GLShader *shader ) :
		GLUniform3f( shader, "u_FogColor" )
	{
	}

	void SetUniform_FogColor( const vec3_t v )
	{
		this->SetValue( v );
	}
};


class u_CloudHeight :
	GLUniform1f {
	public:
	u_CloudHeight( GLShader* shader ) :
		GLUniform1f( shader, "u_CloudHeight" ) {
	}

	void SetUniform_CloudHeight( const float cloudHeight ) {
		this->SetValue( cloudHeight );
	}
};

class u_Color :
	GLUniform1ui
{
public:
	u_Color( GLShader *shader ) :
		GLUniform1ui( shader, "u_Color" )
	{
	}

	void SetUniform_Color( const Color::Color& color )
	{
		this->SetValue( packUnorm4x8( color.ToArray() ) );
	}
};

class u_ColorGlobal :
	GLUniform1ui {
	public:
	u_ColorGlobal( GLShader* shader ) :
		GLUniform1ui( shader, "u_ColorGlobal", true ) {
	}

	void SetUniform_ColorGlobal( const Color::Color& color ) {
		this->SetValue( packUnorm4x8( color.ToArray() ) );
	}
};

class u_Frame :
	GLUniform1ui {
	public:
	u_Frame( GLShader* shader ) :
		GLUniform1ui( shader, "u_Frame" ) {
	}

	void SetUniform_Frame( const uint frame ) {
		this->SetValue( frame );
	}
};

class u_ViewID :
	GLUniform1ui {
	public:
	u_ViewID( GLShader* shader ) :
		GLUniform1ui( shader, "u_ViewID" ) {
	}

	void SetUniform_ViewID( const uint viewID ) {
		this->SetValue( viewID );
	}
};

class u_FirstPortalGroup :
	GLUniform1ui {
	public:
	u_FirstPortalGroup( GLShader* shader ) :
		GLUniform1ui( shader, "u_FirstPortalGroup" ) {
	}

	void SetUniform_FirstPortalGroup( const uint firstPortalGroup ) {
		this->SetValue( firstPortalGroup );
	}
};

class u_TotalPortals :
	GLUniform1ui {
	public:
	u_TotalPortals( GLShader* shader ) :
		GLUniform1ui( shader, "u_TotalPortals" ) {
	}

	void SetUniform_TotalPortals( const uint totalPortals ) {
		this->SetValue( totalPortals );
	}
};

class u_ViewWidth :
	GLUniform1ui {
	public:
	u_ViewWidth( GLShader* shader ) :
		GLUniform1ui( shader, "u_ViewWidth" ) {
	}

	void SetUniform_ViewWidth( const uint viewWidth ) {
		this->SetValue( viewWidth );
	}
};

class u_ViewHeight :
	GLUniform1ui {
	public:
	u_ViewHeight( GLShader* shader ) :
		GLUniform1ui( shader, "u_ViewHeight" ) {
	}

	void SetUniform_ViewHeight( const uint viewHeight ) {
		this->SetValue( viewHeight );
	}
};

class u_InitialDepthLevel :
	GLUniform1Bool {
	public:
	u_InitialDepthLevel( GLShader* shader ) :
		GLUniform1Bool( shader, "u_InitialDepthLevel", true ) {
	}

	void SetUniform_InitialDepthLevel( const int initialDepthLevel ) {
		this->SetValue( initialDepthLevel );
	}
};

class u_P00 :
	GLUniform1f {
	public:
	u_P00( GLShader* shader ) :
		GLUniform1f( shader, "u_P00" ) {
	}

	void SetUniform_P00( const float P00 ) {
		this->SetValue( P00 );
	}
};

class u_P11 :
	GLUniform1f {
	public:
	u_P11( GLShader* shader ) :
		GLUniform1f( shader, "u_P11" ) {
	}

	void SetUniform_P11( const float P11 ) {
		this->SetValue( P11 );
	}
};

class u_TotalDrawSurfs :
	GLUniform1ui {
	public:
	u_TotalDrawSurfs( GLShader* shader ) :
		GLUniform1ui( shader, "u_TotalDrawSurfs", true ) {
	}

	void SetUniform_TotalDrawSurfs( const uint totalDrawSurfs ) {
		this->SetValue( totalDrawSurfs );
	}
};

class u_UseFrustumCulling :
	GLUniform1Bool {
	public:
	u_UseFrustumCulling( GLShader* shader ) :
		GLUniform1Bool( shader, "u_UseFrustumCulling", true ) {
	}

	void SetUniform_UseFrustumCulling( const int useFrustumCulling ) {
		this->SetValue( useFrustumCulling );
	}
};

class u_UseOcclusionCulling :
	GLUniform1Bool {
	public:
	u_UseOcclusionCulling( GLShader* shader ) :
		GLUniform1Bool( shader, "u_UseOcclusionCulling", true ) {
	}

	void SetUniform_UseOcclusionCulling( const int useOcclusionCulling ) {
		this->SetValue( useOcclusionCulling );
	}
};

class u_ShowTris :
	GLUniform1Bool {
	public:
	u_ShowTris( GLShader* shader ) :
		GLUniform1Bool( shader, "u_ShowTris", true ) {
	}

	void SetUniform_ShowTris( const int showTris ) {
		this->SetValue( showTris );
	}
};

class u_CameraPosition :
	GLUniform3f {
	public:
	u_CameraPosition( GLShader* shader ) :
		GLUniform3f( shader, "u_CameraPosition" ) {
	}

	void SetUniform_CameraPosition( const vec3_t cameraPosition ) {
		this->SetValue( cameraPosition );
	}
};

class u_Frustum :
	GLUniform4fv {
	public:
	u_Frustum( GLShader* shader ) :
		GLUniform4fv( shader, "u_Frustum", 6 ) {
	}

	void SetUniform_Frustum( vec4_t frustum[6] ) {
		this->SetValue( 6, &frustum[0] );
	}
};

class u_SurfaceCommandsOffset :
	GLUniform1ui {
	public:
	u_SurfaceCommandsOffset( GLShader* shader ) :
		GLUniform1ui( shader, "u_SurfaceCommandsOffset" ) {
	}

	void SetUniform_SurfaceCommandsOffset( const uint surfaceCommandsOffset ) {
		this->SetValue( surfaceCommandsOffset );
	}
};

class u_MaterialColour :
	GLUniform3f {
	public:
	u_MaterialColour( GLShader* shader ) :
		GLUniform3f( shader, "u_MaterialColour", true ) {
	}

	void SetUniform_MaterialColour( const vec3_t materialColour ) {
		this->SetValue( materialColour );
	}
};

// u_Profiler* uniforms are all used for shader profiling, with the corresponding r_profiler* cvars
// u_ProfilerZero is used to reset the colour in a shader without the shader compiler optimising the rest of the shader out
class u_ProfilerZero :
	GLUniform1f {
	public:
	u_ProfilerZero( GLShader* shader ) :
		GLUniform1f( shader, "u_ProfilerZero", true ) {
	}

	void SetUniform_ProfilerZero() {
		this->SetValue( 0.0 );
	}
};

class u_ProfilerRenderSubGroups :
	GLUniform1ui {
	public:
	u_ProfilerRenderSubGroups( GLShader* shader ) :
		GLUniform1ui( shader, "u_ProfilerRenderSubGroups", true ) {
	}

	void SetUniform_ProfilerRenderSubGroups( const uint renderSubGroups ) {
		this->SetValue( renderSubGroups );
	}
};

class u_ModelMatrix :
	GLUniformMatrix4f
{
public:
	u_ModelMatrix( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_ModelMatrix", true )
	{
	}

	void SetUniform_ModelMatrix( const matrix_t m )
	{
		this->SetValue( GL_FALSE, m );
	}
};

class u_ViewMatrix :
	GLUniformMatrix4f
{
public:
	u_ViewMatrix( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_ViewMatrix" )
	{
	}

	void SetUniform_ViewMatrix( const matrix_t m )
	{
		this->SetValue( GL_FALSE, m );
	}
};

class u_ModelViewMatrix :
	GLUniformMatrix4f
{
public:
	u_ModelViewMatrix( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_ModelViewMatrix" )
	{
	}

	void SetUniform_ModelViewMatrix( const matrix_t m )
	{
		this->SetValue( GL_FALSE, m );
	}
};

class u_ModelViewMatrixTranspose :
	GLUniformMatrix4f
{
public:
	u_ModelViewMatrixTranspose( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_ModelViewMatrixTranspose", true )
	{
	}

	void SetUniform_ModelViewMatrixTranspose( const matrix_t m )
	{
		this->SetValue( GL_TRUE, m );
	}
};

class u_ProjectionMatrixTranspose :
	GLUniformMatrix4f
{
public:
	u_ProjectionMatrixTranspose( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_ProjectionMatrixTranspose", true )
	{
	}

	void SetUniform_ProjectionMatrixTranspose( const matrix_t m )
	{
		this->SetValue( GL_TRUE, m );
	}
};

class u_ModelViewProjectionMatrix :
	GLUniformMatrix4f
{
public:
	u_ModelViewProjectionMatrix( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_ModelViewProjectionMatrix", true )
	{
	}

	void SetUniform_ModelViewProjectionMatrix( const matrix_t m )
	{
		this->SetValue( GL_FALSE, m );
	}
};

class u_UnprojectMatrix :
	GLUniformMatrix4f
{
public:
	u_UnprojectMatrix( GLShader *shader ) :
		GLUniformMatrix4f( shader, "u_UnprojectMatrix", true )
	{
	}

	void SetUniform_UnprojectMatrix( const matrix_t m )
	{
		this->SetValue( GL_FALSE, m );
	}
};

class u_UseCloudMap :
	GLUniform1Bool {
	public:
	u_UseCloudMap( GLShader* shader ) :
		GLUniform1Bool( shader, "u_UseCloudMap", true ) {
	}

	void SetUniform_UseCloudMap( const bool useCloudMap ) {
		this->SetValue( useCloudMap );
	}
};

class u_Bones :
	GLUniform4fv
{
public:
	u_Bones( GLShader *shader ) :
		GLUniform4fv( shader, "u_Bones", MAX_BONES )
	{
	}

	void SetUniform_Bones( int numBones, transform_t bones[ MAX_BONES ] )
	{
		this->SetValue( 2 * numBones, &bones[ 0 ].rot );
	}
};

class u_VertexInterpolation :
	GLUniform1f
{
public:
	u_VertexInterpolation( GLShader *shader ) :
		GLUniform1f( shader, "u_VertexInterpolation" )
	{
	}

	void SetUniform_VertexInterpolation( float value )
	{
		this->SetValue( value );
	}
};

class u_InversePortalRange :
	GLUniform1f
{
public:
	u_InversePortalRange( GLShader *shader ) :
		GLUniform1f( shader, "u_InversePortalRange" )
	{
	}

	void SetUniform_InversePortalRange( float value )
	{
		this->SetValue( value );
	}
};

class u_DepthScale :
	GLUniform1f
{
public:
	u_DepthScale( GLShader *shader ) :
		GLUniform1f( shader, "u_DepthScale" )
	{
	}

	void SetUniform_DepthScale( float value )
	{
		this->SetValue( value );
	}
};

class u_ReliefDepthScale :
	GLUniform1f
{
public:
	u_ReliefDepthScale( GLShader *shader ) :
		GLUniform1f( shader, "u_ReliefDepthScale" )
	{
	}

	void SetUniform_ReliefDepthScale( float value )
	{
		this->SetValue( value );
	}
};

class u_ReliefOffsetBias :
	GLUniform1f
{
public:
	u_ReliefOffsetBias( GLShader *shader ) :
		GLUniform1f( shader, "u_ReliefOffsetBias" )
	{
	}

	void SetUniform_ReliefOffsetBias( float value )
	{
		this->SetValue( value );
	}
};

class u_EnvironmentInterpolation :
	GLUniform1f
{
public:
	u_EnvironmentInterpolation( GLShader *shader ) :
		GLUniform1f( shader, "u_EnvironmentInterpolation", true )
	{
	}

	void SetUniform_EnvironmentInterpolation( float value )
	{
		this->SetValue( value );
	}
};

class u_Time :
	GLUniform1f
{
public:
	u_Time( GLShader *shader ) :
		GLUniform1f( shader, "u_Time", true ) // Source this from a buffer when entity support is added to the material system
	{
	}

	void SetUniform_Time( float value )
	{
		this->SetValue( value );
	}
};

class u_GlobalLightFactor :
	GLUniform1f
{
public:
	u_GlobalLightFactor( GLShader *shader ) :
		GLUniform1f( shader, "u_GlobalLightFactor" )
	{
	}

	void SetUniform_GlobalLightFactor( float value )
	{
		this->SetValue( value );
	}
};

class GLDeformStage :
	public u_Time
{
public:
	GLDeformStage( GLShader *shader ) :
		u_Time( shader )
	{
	}
};

class u_ColorModulate :
	GLUniform4f
{
public:
	u_ColorModulate( GLShader *shader ) :
		GLUniform4f( shader, "u_ColorModulate" )
	{
	}

	void SetUniform_ColorModulate( vec4_t v )
	{
		this->SetValue( v );
	}
};

enum class ColorModulate {
	COLOR_ONE = BIT( 0 ),
	COLOR_MINUS_ONE = BIT( 1 ),
	COLOR_LIGHTFACTOR = BIT( 2 ),
	ALPHA_ONE = BIT( 3 ),
	ALPHA_MINUS_ONE = BIT( 4 ),
	ALPHA_ADD_ONE = BIT( 5 )
};

class u_ColorModulateColorGen :
	GLUniform1ui {
	public:
	u_ColorModulateColorGen( GLShader* shader ) :
		GLUniform1ui( shader, "u_ColorModulateColorGen" ) {
	}

	void SetUniform_ColorModulateColorGen( colorGen_t colorGen, alphaGen_t alphaGen, bool vertexOverbright = false,
		const bool useMapLightFactor = false ) {
		uint32_t colorModulate = 0;
		bool needAttrib = false;

		if ( r_logFile->integer ) {
			GLimp_LogComment(
				va( "--- u_ColorModulate::SetUniform_ColorModulateColorGen( program = %s, colorGen = %s, alphaGen = %s ) ---\n",
					_shader->GetName().c_str(), Util::enum_str( colorGen ), Util::enum_str( alphaGen ) )
			);
		}

		uint32_t lightFactor = 0;
		switch ( colorGen ) {
			case colorGen_t::CGEN_VERTEX:
				needAttrib = true;
				if ( vertexOverbright ) {
					// vertexOverbright is only needed for non-lightmapped cases. When there is a
					// lightmap, this is done by multiplying with the overbright-scaled white image
					colorModulate |= Util::ordinal( ColorModulate::COLOR_LIGHTFACTOR );
					lightFactor = uint32_t( tr.mapLightFactor ) << 6;
				} else {
					colorModulate |= Util::ordinal( ColorModulate::COLOR_ONE );
				}
				break;

			case colorGen_t::CGEN_ONE_MINUS_VERTEX:
				needAttrib = true;
				colorModulate |= Util::ordinal( ColorModulate::COLOR_MINUS_ONE );
				break;

			default:
				break;
		}

		if ( useMapLightFactor ) {
			ASSERT_EQ( vertexOverbright, false );
			lightFactor = uint32_t( tr.mapLightFactor ) << 6;
		}

		colorModulate |= lightFactor ? lightFactor : 1 << 6;

		switch ( alphaGen ) {
			case alphaGen_t::AGEN_VERTEX:
				needAttrib = true;
				colorModulate |= Util::ordinal( ColorModulate::ALPHA_ONE );
				break;

			case alphaGen_t::AGEN_ONE_MINUS_VERTEX:
				needAttrib = true;
				colorModulate |= Util::ordinal( ColorModulate::ALPHA_MINUS_ONE );
				break;

			default:
				break;
		}

		if ( !needAttrib ) {
			/* Originally, this controlled whether the vertex color array was used,
			now it does the equivalent by setting the color in the shader in such way as if it was using
			the default OpenGL values for the disabled arrays (0.0, 0.0, 0.0, 1.0)
			This allows to skip the vertex format change */
			colorModulate |= Util::ordinal( ColorModulate::ALPHA_ADD_ONE );
		}
		this->SetValue( colorModulate );
	}
};

class u_FogDistanceVector :
	GLUniform4f
{
public:
	u_FogDistanceVector( GLShader *shader ) :
		GLUniform4f( shader, "u_FogDistanceVector", true )
	{
	}

	void SetUniform_FogDistanceVector( const vec4_t v )
	{
		this->SetValue( v );
	}
};

class u_FogDepthVector :
	GLUniform4f
{
public:
	u_FogDepthVector( GLShader *shader ) :
		GLUniform4f( shader, "u_FogDepthVector", true )
	{
	}

	void SetUniform_FogDepthVector( const vec4_t v )
	{
		this->SetValue( v );
	}
};

class u_FogEyeT :
	GLUniform1f
{
public:
	u_FogEyeT( GLShader *shader ) :
		GLUniform1f( shader, "u_FogEyeT", true )
	{
	}

	void SetUniform_FogEyeT( float value )
	{
		this->SetValue( value );
	}
};

class u_DeformEnable :
	GLUniform1f {
	public:
	u_DeformEnable( GLShader* shader ) :
		GLUniform1f( shader, "u_DeformEnable", true ) {
	}

	void SetUniform_DeformEnable( const bool value ) {
		this->SetValue( value );
	}
};

class u_DeformMagnitude :
	GLUniform1f
{
public:
	u_DeformMagnitude( GLShader *shader ) :
		GLUniform1f( shader, "u_DeformMagnitude" )
	{
	}

	void SetUniform_DeformMagnitude( float value )
	{
		this->SetValue( value );
	}
};

class u_blurVec :
	GLUniform3f
{
public:
	u_blurVec( GLShader *shader ) :
		GLUniform3f( shader, "u_blurVec" )
	{
	}

	void SetUniform_blurVec( vec3_t value )
	{
		this->SetValue( value );
	}
};

class u_Horizontal :
	GLUniform1Bool {
	public:
	u_Horizontal( GLShader* shader ) :
		GLUniform1Bool( shader, "u_Horizontal", true ) {
	}

	void SetUniform_Horizontal( bool horizontal ) {
		this->SetValue( horizontal );
	}
};

class u_TexScale :
	GLUniform2f
{
public:
	u_TexScale( GLShader *shader ) :
		GLUniform2f( shader, "u_TexScale" )
	{
	}

	void SetUniform_TexScale( vec2_t value )
	{
		this->SetValue( value );
	}
};

class u_SpecularExponent :
	GLUniform2f
{
public:
	u_SpecularExponent( GLShader *shader ) :
		GLUniform2f( shader, "u_SpecularExponent" )
	{
	}

	void SetUniform_SpecularExponent( float min, float max )
	{
		// in the fragment shader, the exponent must be computed as
		// exp = ( max - min ) * gloss + min
		// to expand the [0...1] range of gloss to [min...max]
		// we precompute ( max - min ) before sending the uniform to the fragment shader
		vec2_t v = { max - min, min };
		this->SetValue( v );
	}
};

class u_InverseGamma :
	GLUniform1f
{
public:
	u_InverseGamma( GLShader *shader ) :
		GLUniform1f( shader, "u_InverseGamma" )
	{
	}

	void SetUniform_InverseGamma( float value )
	{
		this->SetValue( value );
	}
};

class u_Tonemap :
	GLUniform1Bool {
	public:
	u_Tonemap( GLShader* shader ) :
		GLUniform1Bool( shader, "u_Tonemap", true ) {
	}

	void SetUniform_Tonemap( bool tonemap ) {
		this->SetValue( tonemap );
	}
};

class u_TonemapParms :
	GLUniform4f {
	public:
	u_TonemapParms( GLShader* shader ) :
		GLUniform4f( shader, "u_TonemapParms", true ) {
	}

	void SetUniform_TonemapParms( vec4_t tonemapParms ) {
		this->SetValue( tonemapParms );
	}
};

class u_TonemapExposure :
	GLUniform1f {
	public:
	u_TonemapExposure( GLShader* shader ) :
		GLUniform1f( shader, "u_TonemapExposure", true ) {
	}

	void SetUniform_TonemapExposure( float tonemapExposure ) {
		this->SetValue( tonemapExposure );
	}
};

class u_LightGridOrigin :
	GLUniform3f
{
public:
	u_LightGridOrigin( GLShader *shader ) :
		GLUniform3f( shader, "u_LightGridOrigin", true )
	{
	}

	void SetUniform_LightGridOrigin( vec3_t origin )
	{
		this->SetValue( origin );
	}
};

class u_LightGridScale :
	GLUniform3f
{
public:
	u_LightGridScale( GLShader *shader ) :
		GLUniform3f( shader, "u_LightGridScale", true )
	{
	}

	void SetUniform_LightGridScale( vec3_t scale )
	{
		this->SetValue( scale );
	}
};

class u_zFar :
	GLUniform3f
{
public:
	u_zFar( GLShader *shader ) :
		GLUniform3f( shader, "u_zFar" )
	{
	}

	void SetUniform_zFar( const vec3_t value )
	{
		this->SetValue( value );
	}
};

class u_UnprojectionParams :
	GLUniform3f {
	public:
	u_UnprojectionParams( GLShader* shader ) :
		GLUniform3f( shader, "u_UnprojectionParams" ) {
	}

	void SetUniform_UnprojectionParams( const vec3_t value ) {
		this->SetValue( value );
	}
};

class u_numLights :
	GLUniform1i
{
public:
	u_numLights( GLShader *shader ) :
		GLUniform1i( shader, "u_numLights", true )
	{
	}

	void SetUniform_numLights( int value )
	{
		this->SetValue( value );
	}
};

class u_lightLayer :
	GLUniform1i
{
public:
	u_lightLayer( GLShader *shader ) :
		GLUniform1i( shader, "u_lightLayer" )
	{
	}

	void SetUniform_lightLayer( int value )
	{
		this->SetValue( value );
	}
};

class u_Lights :
	GLUniformBlock
{
 public:
	u_Lights( GLShader *shader ) :
		GLUniformBlock( shader, "u_Lights" )
	{
	}

	void SetUniformBlock_Lights( GLuint buffer )
	{
		this->SetBuffer( buffer );
	}
};

class GLShader_generic :
	public GLShader,
	public u_ColorMap,
	public u_DepthMap,
	public u_TextureMatrix,
	public u_ViewOrigin,
	public u_ViewUp,
	public u_AlphaThreshold,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_ColorModulateColorGen,
	public u_Color,
	public u_Bones,
	public u_VertexInterpolation,
	public u_DepthScale,
	public u_ProfilerZero,
	public u_ProfilerRenderSubGroups,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION,
	public GLCompileMacro_USE_TCGEN_ENVIRONMENT,
	public GLCompileMacro_USE_TCGEN_LIGHTMAP,
	public GLCompileMacro_USE_DEPTH_FADE
{
public:
	GLShader_generic( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_genericMaterial :
	public GLShader,
	public u_ColorMap,
	public u_DepthMap,
	public u_TextureMatrix,
	public u_ViewOrigin,
	public u_ViewUp,
	public u_AlphaThreshold,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_ColorModulateColorGen,
	public u_Color,
	public u_DepthScale,
	public u_ShowTris,
	public u_MaterialColour,
	public u_ProfilerZero,
	public u_ProfilerRenderSubGroups,
	public GLDeformStage,
	public GLCompileMacro_USE_TCGEN_ENVIRONMENT,
	public GLCompileMacro_USE_TCGEN_LIGHTMAP,
	public GLCompileMacro_USE_DEPTH_FADE {
	public:
	GLShader_genericMaterial( GLShaderManager* manager );
	void SetShaderProgramUniforms( shaderProgram_t* shaderProgram ) override;
};

class GLShader_lightMapping :
	public GLShader,
	public u_DiffuseMap,
	public u_NormalMap,
	public u_HeightMap,
	public u_MaterialMap,
	public u_LightMap,
	public u_DeluxeMap,
	public u_GlowMap,
	public u_EnvironmentMap0,
	public u_EnvironmentMap1,
	public u_LightGrid1,
	public u_LightGrid2,
	public u_LightTiles,
	public u_TextureMatrix,
	public u_SpecularExponent,
	public u_ColorModulateColorGen,
	public u_Color,
	public u_AlphaThreshold,
	public u_ViewOrigin,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_Bones,
	public u_VertexInterpolation,
	public u_ReliefDepthScale,
	public u_ReliefOffsetBias,
	public u_NormalScale,
	public u_EnvironmentInterpolation,
	public u_LightGridOrigin,
	public u_LightGridScale,
	public u_numLights,
	public u_Lights,
	public u_ProfilerZero,
	public u_ProfilerRenderSubGroups,
	public GLDeformStage,
	public GLCompileMacro_USE_BSP_SURFACE,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION,
	public GLCompileMacro_USE_DELUXE_MAPPING,
	public GLCompileMacro_USE_GRID_LIGHTING,
	public GLCompileMacro_USE_GRID_DELUXE_MAPPING,
	public GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP,
	public GLCompileMacro_USE_RELIEF_MAPPING,
	public GLCompileMacro_USE_REFLECTIVE_SPECULAR,
	public GLCompileMacro_USE_PHYSICAL_MAPPING
{
public:
	GLShader_lightMapping( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_lightMappingMaterial :
	public GLShader,
	public u_DiffuseMap,
	public u_NormalMap,
	public u_HeightMap,
	public u_MaterialMap,
	public u_LightMap,
	public u_DeluxeMap,
	public u_GlowMap,
	public u_EnvironmentMap0,
	public u_EnvironmentMap1,
	public u_LightGrid1,
	public u_LightGrid2,
	public u_LightTiles,
	public u_TextureMatrix,
	public u_SpecularExponent,
	public u_ColorModulateColorGen,
	public u_Color,
	public u_AlphaThreshold,
	public u_ViewOrigin,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_ReliefDepthScale,
	public u_ReliefOffsetBias,
	public u_NormalScale,
	public u_EnvironmentInterpolation,
	public u_LightGridOrigin,
	public u_LightGridScale,
	public u_numLights,
	public u_Lights,
	public u_ShowTris,
	public u_MaterialColour,
	public u_ProfilerZero,
	public u_ProfilerRenderSubGroups,
	public GLDeformStage,
	public GLCompileMacro_USE_BSP_SURFACE,
	public GLCompileMacro_USE_DELUXE_MAPPING,
	public GLCompileMacro_USE_GRID_LIGHTING,
	public GLCompileMacro_USE_GRID_DELUXE_MAPPING,
	public GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP,
	public GLCompileMacro_USE_RELIEF_MAPPING,
	public GLCompileMacro_USE_REFLECTIVE_SPECULAR,
	public GLCompileMacro_USE_PHYSICAL_MAPPING {
	public:
	GLShader_lightMappingMaterial( GLShaderManager* manager );
	void SetShaderProgramUniforms( shaderProgram_t* shaderProgram ) override;
};

class GLShader_forwardLighting_omniXYZ :
	public GLShader,
	public u_DiffuseMap,
	public u_NormalMap,
	public u_MaterialMap,
	public u_AttenuationMapXY,
	public u_AttenuationMapZ,
	public u_ShadowMap,
	public u_ShadowClipMap,
	public u_RandomMap,
	public u_HeightMap,
	public u_TextureMatrix,
	public u_SpecularExponent,
	public u_AlphaThreshold,
	public u_ColorModulateColorGen,
	public u_Color,
	public u_ViewOrigin,
	public u_LightOrigin,
	public u_LightColor,
	public u_LightRadius,
	public u_LightScale,
	public u_LightAttenuationMatrix,
	public u_ShadowTexelSize,
	public u_ShadowBlur,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_Bones,
	public u_VertexInterpolation,
	public u_ReliefDepthScale,
	public u_ReliefOffsetBias,
	public u_NormalScale,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION,
	public GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP,
	public GLCompileMacro_USE_RELIEF_MAPPING,
	public GLCompileMacro_USE_SHADOWING //,
{
public:
	GLShader_forwardLighting_omniXYZ( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_forwardLighting_projXYZ :
	public GLShader,
	public u_DiffuseMap,
	public u_NormalMap,
	public u_MaterialMap,
	public u_AttenuationMapXY,
	public u_AttenuationMapZ,
	public u_ShadowMap0,
	public u_ShadowClipMap0,
	public u_RandomMap,
	public u_HeightMap,
	public u_TextureMatrix,
	public u_SpecularExponent,
	public u_AlphaThreshold,
	public u_ColorModulateColorGen,
	public u_Color,
	public u_ViewOrigin,
	public u_LightOrigin,
	public u_LightColor,
	public u_LightRadius,
	public u_LightScale,
	public u_LightAttenuationMatrix,
	public u_ShadowTexelSize,
	public u_ShadowBlur,
	public u_ShadowMatrix,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_Bones,
	public u_VertexInterpolation,
	public u_ReliefDepthScale,
	public u_ReliefOffsetBias,
	public u_NormalScale,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION,
	public GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP,
	public GLCompileMacro_USE_RELIEF_MAPPING,
	public GLCompileMacro_USE_SHADOWING //,
{
public:
	GLShader_forwardLighting_projXYZ( GLShaderManager *manager );
	void BuildShaderCompileMacros( std::string& compileMacros ) override;
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_forwardLighting_directionalSun :
	public GLShader,
	public u_DiffuseMap,
	public u_NormalMap,
	public u_MaterialMap,
	public u_ShadowMap0,
	public u_ShadowMap1,
	public u_ShadowMap2,
	public u_ShadowMap3,
	public u_ShadowMap4,
	public u_ShadowClipMap0,
	public u_ShadowClipMap1,
	public u_ShadowClipMap2,
	public u_ShadowClipMap3,
	public u_ShadowClipMap4,
	public u_HeightMap,
	public u_TextureMatrix,
	public u_SpecularExponent,
	public u_AlphaThreshold,
	public u_ColorModulateColorGen,
	public u_Color,
	public u_ViewOrigin,
	public u_LightDir,
	public u_LightColor,
	public u_LightRadius,
	public u_LightScale,
	public u_LightAttenuationMatrix,
	public u_ShadowTexelSize,
	public u_ShadowBlur,
	public u_ShadowMatrix,
	public u_ShadowParallelSplitDistances,
	public u_ModelMatrix,
	public u_ViewMatrix,
	public u_ModelViewProjectionMatrix,
	public u_Bones,
	public u_VertexInterpolation,
	public u_ReliefDepthScale,
	public u_ReliefOffsetBias,
	public u_NormalScale,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION,
	public GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP,
	public GLCompileMacro_USE_RELIEF_MAPPING,
	public GLCompileMacro_USE_SHADOWING //,
{
public:
	GLShader_forwardLighting_directionalSun( GLShaderManager *manager );
	void BuildShaderCompileMacros( std::string& compileMacros ) override;
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_shadowFill :
	public GLShader,
	public u_ColorMap,
	public u_TextureMatrix,
	public u_AlphaThreshold,
	public u_LightOrigin,
	public u_LightRadius,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_Color,
	public u_Bones,
	public u_VertexInterpolation,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION,
	public GLCompileMacro_LIGHT_DIRECTIONAL
{
public:
	GLShader_shadowFill( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_reflection :
	public GLShader,
	public u_ColorMapCube,
	public u_NormalMap,
	public u_HeightMap,
	public u_TextureMatrix,
	public u_ViewOrigin,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_Bones,
	public u_ReliefDepthScale,
	public u_ReliefOffsetBias,
	public u_NormalScale,
	public u_VertexInterpolation,
	public u_CameraPosition,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION,
	public GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP,
	public GLCompileMacro_USE_RELIEF_MAPPING
{
public:
	GLShader_reflection( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_reflectionMaterial :
	public GLShader,
	public u_ColorMapCube,
	public u_NormalMap,
	public u_HeightMap,
	public u_TextureMatrix,
	public u_ViewOrigin,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_ReliefDepthScale,
	public u_ReliefOffsetBias,
	public u_NormalScale,
	public u_CameraPosition,
	public GLDeformStage,
	public GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP,
	public GLCompileMacro_USE_RELIEF_MAPPING {
	public:
	GLShader_reflectionMaterial( GLShaderManager* manager );
	void SetShaderProgramUniforms( shaderProgram_t* shaderProgram ) override;
};

class GLShader_skybox :
	public GLShader,
	public u_ColorMapCube,
	public u_CloudMap,
	public u_TextureMatrix,
	public u_CloudHeight,
	public u_UseCloudMap,
	public u_AlphaThreshold,
	public u_ModelViewProjectionMatrix
{
public:
	GLShader_skybox( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_skyboxMaterial :
	public GLShader,
	public u_ColorMapCube,
	public u_CloudMap,
	public u_TextureMatrix,
	public u_CloudHeight,
	public u_UseCloudMap,
	public u_AlphaThreshold,
	public u_ModelViewProjectionMatrix {
	public:
	GLShader_skyboxMaterial( GLShaderManager* manager );
	void SetShaderProgramUniforms( shaderProgram_t* shaderProgram ) override;
};

class GLShader_fogQuake3 :
	public GLShader,
	public u_FogMap,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_ColorGlobal,
	public u_Bones,
	public u_VertexInterpolation,
	public u_FogDistanceVector,
	public u_FogDepthVector,
	public u_FogEyeT,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION
{
public:
	GLShader_fogQuake3( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_fogQuake3Material :
	public GLShader,
	public u_FogMap,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_ColorGlobal,
	public u_FogDistanceVector,
	public u_FogDepthVector,
	public u_FogEyeT,
	public GLDeformStage {
	public:
	GLShader_fogQuake3Material( GLShaderManager* manager );
	void SetShaderProgramUniforms( shaderProgram_t* shaderProgram ) override;
};

class GLShader_fogGlobal :
	public GLShader,
	public u_ColorMap,
	public u_DepthMap,
	public u_ModelViewProjectionMatrix,
	public u_UnprojectMatrix,
	public u_Color,
	public u_FogDistanceVector
{
public:
	GLShader_fogGlobal( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_heatHaze :
	public GLShader,
	public u_CurrentMap,
	public u_NormalMap,
	public u_HeightMap,
	public u_TextureMatrix,
	public u_DeformMagnitude,
	public u_ModelViewProjectionMatrix,
	public u_ModelViewMatrixTranspose,
	public u_ProjectionMatrixTranspose,
	public u_Bones,
	public u_NormalScale,
	public u_VertexInterpolation,
	public GLDeformStage,
	public GLCompileMacro_USE_VERTEX_SKINNING,
	public GLCompileMacro_USE_VERTEX_ANIMATION
{
public:
	GLShader_heatHaze( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_heatHazeMaterial :
	public GLShader,
	public u_CurrentMap,
	public u_NormalMap,
	public u_HeightMap,
	public u_TextureMatrix,
	public u_DeformEnable,
	public u_DeformMagnitude,
	public u_ModelViewProjectionMatrix,
	public u_ModelViewMatrixTranspose,
	public u_ProjectionMatrixTranspose,
	public u_NormalScale,
	public GLDeformStage
{
public:
	GLShader_heatHazeMaterial( GLShaderManager* manager );
	void SetShaderProgramUniforms( shaderProgram_t* shaderProgram ) override;
};

class GLShader_screen :
	public GLShader,
	public u_CurrentMap,
	public u_ModelViewProjectionMatrix
{
public:
	GLShader_screen( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_screenMaterial :
	public GLShader,
	public u_CurrentMap,
	public u_ModelViewProjectionMatrix {
	public:
	GLShader_screenMaterial( GLShaderManager* manager );
	void SetShaderProgramUniforms( shaderProgram_t* shaderProgram ) override;
};

class GLShader_portal :
	public GLShader,
	public u_CurrentMap,
	public u_ModelViewMatrix,
	public u_ModelViewProjectionMatrix,
	public u_InversePortalRange
{
public:
	GLShader_portal( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_contrast :
	public GLShader,
	public u_ColorMap,
	public u_ModelViewProjectionMatrix
{
public:
	GLShader_contrast( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_cameraEffects :
	public GLShader,
	public u_ColorMap3D,
	public u_CurrentMap,
	public u_GlobalLightFactor,
	public u_ColorModulate,
	public u_TextureMatrix,
	public u_ModelViewProjectionMatrix,
	public u_Tonemap,
	public u_TonemapParms,
	public u_TonemapExposure,
	public u_InverseGamma
{
public:
	GLShader_cameraEffects( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_blur :
	public GLShader,
	public u_ColorMap,
	public u_ModelViewProjectionMatrix,
	public u_DeformMagnitude,
	public u_TexScale,
	public u_Horizontal
{
public:
	GLShader_blur( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_debugShadowMap :
	public GLShader,
	public u_CurrentMap,
	public u_ModelViewProjectionMatrix
{
public:
	GLShader_debugShadowMap( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_liquid :
	public GLShader,
	public u_CurrentMap,
	public u_DepthMap,
	public u_NormalMap,
	public u_PortalMap,
	public u_LightGrid1,
	public u_LightGrid2,
	public u_HeightMap,
	public u_TextureMatrix,
	public u_ViewOrigin,
	public u_RefractionIndex,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_UnprojectMatrix,
	public u_FresnelPower,
	public u_FresnelScale,
	public u_FresnelBias,
	public u_ReliefDepthScale,
	public u_ReliefOffsetBias,
	public u_NormalScale,
	public u_FogDensity,
	public u_FogColor,
	public u_SpecularExponent,
	public u_LightGridOrigin,
	public u_LightGridScale,
	public GLCompileMacro_USE_GRID_DELUXE_MAPPING,
	public GLCompileMacro_USE_GRID_LIGHTING,
	public GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP,
	public GLCompileMacro_USE_RELIEF_MAPPING
{
public:
	GLShader_liquid( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_liquidMaterial :
	public GLShader,
	public u_CurrentMap,
	public u_DepthMap,
	public u_NormalMap,
	public u_PortalMap,
	public u_LightGrid1,
	public u_LightGrid2,
	public u_HeightMap,
	public u_TextureMatrix,
	public u_ViewOrigin,
	public u_RefractionIndex,
	public u_ModelMatrix,
	public u_ModelViewProjectionMatrix,
	public u_UnprojectMatrix,
	public u_FresnelPower,
	public u_FresnelScale,
	public u_FresnelBias,
	public u_ReliefDepthScale,
	public u_ReliefOffsetBias,
	public u_NormalScale,
	public u_FogDensity,
	public u_FogColor,
	public u_LightTiles,
	public u_SpecularExponent,
	public u_LightGridOrigin,
	public u_LightGridScale,
	public GLCompileMacro_USE_GRID_DELUXE_MAPPING,
	public GLCompileMacro_USE_GRID_LIGHTING,
	public GLCompileMacro_USE_HEIGHTMAP_IN_NORMALMAP,
	public GLCompileMacro_USE_RELIEF_MAPPING {
	public:
	GLShader_liquidMaterial( GLShaderManager* manager );
	void SetShaderProgramUniforms( shaderProgram_t* shaderProgram ) override;
};

class GLShader_motionblur :
	public GLShader,
	public u_ColorMap,
	public u_DepthMap,
	public u_ModelViewProjectionMatrix,
	public u_blurVec
{
public:
	GLShader_motionblur( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_ssao :
	public GLShader,
	public u_DepthMap,
	public u_ModelViewProjectionMatrix,
	public u_UnprojectionParams,
	public u_zFar
{
public:
	GLShader_ssao( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_depthtile1 :
	public GLShader,
	public u_DepthMap,
	public u_ModelViewProjectionMatrix,
	public u_zFar
{
public:
	GLShader_depthtile1( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_depthtile2 :
	public GLShader,
	public u_DepthMap,
	public u_ModelViewProjectionMatrix
{
public:
	GLShader_depthtile2( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_lighttile :
	public GLShader,
	public u_DepthMap,
	public u_Lights,
	public u_numLights,
	public u_lightLayer,
	public u_ModelMatrix,
	public u_zFar
{
public:
	GLShader_lighttile( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_fxaa :
	public GLShader,
	public u_ColorMap,
	public u_ModelViewProjectionMatrix
{
public:
	GLShader_fxaa( GLShaderManager *manager );
	void SetShaderProgramUniforms( shaderProgram_t *shaderProgram ) override;
};

class GLShader_cull :
	public GLShader,
	public u_Frame,
	public u_ViewID,
	public u_TotalDrawSurfs,
	public u_SurfaceCommandsOffset,
	public u_Frustum,
	public u_UseFrustumCulling,
	public u_UseOcclusionCulling,
	public u_CameraPosition,
	public u_ModelViewMatrix,
	public u_FirstPortalGroup,
	public u_TotalPortals,
	public u_ViewWidth,
	public u_ViewHeight,
	public u_P00,
	public u_P11 {
	public:
	GLShader_cull( GLShaderManager* manager );
};

class GLShader_depthReduction :
	public GLShader,
	public u_ViewWidth,
	public u_ViewHeight,
	public u_InitialDepthLevel {
	public:
	GLShader_depthReduction( GLShaderManager* manager );
	void SetShaderProgramUniforms( shaderProgram_t* shaderProgram ) override;
};

class GLShader_clearSurfaces :
	public GLShader,
	public u_Frame {
	public:
	GLShader_clearSurfaces( GLShaderManager* manager );
};

class GLShader_processSurfaces :
	public GLShader,
	public u_Frame,
	public u_ViewID,
	public u_SurfaceCommandsOffset {
	public:
	GLShader_processSurfaces( GLShaderManager* manager );
};


std::string GetShaderPath();

extern ShaderKind shaderKind;

extern GLShader_generic                         *gl_genericShader;
extern GLShader_genericMaterial                 *gl_genericShaderMaterial;
extern GLShader_cull                            *gl_cullShader;
extern GLShader_depthReduction                  *gl_depthReductionShader;
extern GLShader_clearSurfaces                   *gl_clearSurfacesShader;
extern GLShader_processSurfaces                 *gl_processSurfacesShader;
extern GLShader_lightMapping                    *gl_lightMappingShader;
extern GLShader_lightMappingMaterial            *gl_lightMappingShaderMaterial;
extern GLShader_forwardLighting_omniXYZ         *gl_forwardLightingShader_omniXYZ;
extern GLShader_forwardLighting_projXYZ         *gl_forwardLightingShader_projXYZ;
extern GLShader_forwardLighting_directionalSun  *gl_forwardLightingShader_directionalSun;
extern GLShader_shadowFill                      *gl_shadowFillShader;
extern GLShader_reflection                      *gl_reflectionShader;
extern GLShader_reflectionMaterial              *gl_reflectionShaderMaterial;
extern GLShader_skybox                          *gl_skyboxShader;
extern GLShader_skyboxMaterial                  *gl_skyboxShaderMaterial;
extern GLShader_fogQuake3                       *gl_fogQuake3Shader;
extern GLShader_fogQuake3Material               *gl_fogQuake3ShaderMaterial;
extern GLShader_fogGlobal                       *gl_fogGlobalShader;
extern GLShader_heatHaze                        *gl_heatHazeShader;
extern GLShader_heatHazeMaterial                *gl_heatHazeShaderMaterial;
extern GLShader_screen                          *gl_screenShader;
extern GLShader_screenMaterial                  *gl_screenShaderMaterial;
extern GLShader_portal                          *gl_portalShader;
extern GLShader_contrast                        *gl_contrastShader;
extern GLShader_cameraEffects                   *gl_cameraEffectsShader;
extern GLShader_blur                            *gl_blurShader;
extern GLShader_debugShadowMap                  *gl_debugShadowMapShader;
extern GLShader_liquid                          *gl_liquidShader;
extern GLShader_liquidMaterial                  *gl_liquidShaderMaterial;
extern GLShader_motionblur                      *gl_motionblurShader;
extern GLShader_ssao                            *gl_ssaoShader;
extern GLShader_depthtile1                      *gl_depthtile1Shader;
extern GLShader_depthtile2                      *gl_depthtile2Shader;
extern GLShader_lighttile                       *gl_lighttileShader;
extern GLShader_fxaa                            *gl_fxaaShader;
extern GLShaderManager                           gl_shaderManager;

#endif // GL_SHADER_H
