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
// tr_backend.c

#include "tr_local.h"
#include "gl_shader.h"
#include "Material.h"
#if defined( REFBONE_NAMES )
	#include <client/client.h>
#endif

backEndData_t  *backEndData[ SMP_FRAMES ];
backEndState_t backEnd;

static Cvar::Cvar<bool> r_clear( "r_clear", "Clear screen before painting over it on every frame", Cvar::NONE, false );
Cvar::Cvar<bool> r_drawSky( "r_drawSky", "Draw the sky (clear the sky if disabled)", Cvar::NONE, true );

void GL_Bind( image_t *image )
{
	int texnum;

	if ( !image )
	{
		Log::Warn("GL_Bind: NULL image" );
		image = tr.defaultImage;
	}
	else
	{
		if ( r_logFile->integer )
		{
			// don't just call LogComment, or we will get a call to va() every frame!
			GLimp_LogComment( va( "--- GL_Bind( %s ) ---\n", image->name ) );
		}
	}

	texnum = image->texnum;

	if ( r_nobind->integer && tr.blackImage )
	{
		// performance evaluation option
		texnum = tr.blackImage->texnum;
	}

	if ( glConfig2.usingBindlessTextures ) {
		tr.textureManager.BindReservedTexture( image->type, texnum );
		return;
	}

	if ( tr.currenttextures[ glState.currenttmu ] != texnum )
	{
		image->frameUsed = tr.frameCount;
		tr.currenttextures[ glState.currenttmu ] = texnum;
		glBindTexture( image->type, texnum );
	}
}

void GL_Unbind( image_t *image )
{
	GLimp_LogComment( "--- GL_Unbind() ---\n" );

	tr.currenttextures[ glState.currenttmu ] = 0;
	glBindTexture( image->type, 0 );
}

GLuint64 BindAnimatedImage( int unit, const textureBundle_t *bundle )
{
	int index;

	if ( bundle->isVideoMap )
	{
		if ( bundle->videoMapHandle >= 0 && CIN_RunCinematic( bundle->videoMapHandle ) )
		{
			GL_SelectTexture( unit );
			CIN_UploadCinematic( bundle->videoMapHandle );
			return tr.cinematicImage[ bundle->videoMapHandle ]->texture->bindlessTextureHandle;
		}
		else
		{
			return GL_BindToTMU( unit, tr.defaultImage );
		}
	}

	if ( bundle->numImages <= 1 )
	{
		return GL_BindToTMU( unit, bundle->image[ 0 ] );
	}

	// it is necessary to do this messy calc to make sure animations line up
	// exactly with waveforms of the same frequency
	index = Q_ftol( backEnd.refdef.floatTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE );
	index >>= FUNCTABLE_SIZE2;

	if ( index < 0 )
	{
		index = 0; // may happen with shader time offsets
	}

	index %= bundle->numImages;

	return GL_BindToTMU( unit, bundle->image[ index ] );
}

void GL_BindProgram( ShaderProgramDescriptor* program )
{
	if ( !program )
	{
		GL_BindNullProgram();
		return;
	}

	if ( glState.currentProgram != program )
	{
		glUseProgram( program->id );
		glState.currentProgram = program;
	}
}

void GL_BindNullProgram()
{
	if ( r_logFile->integer )
	{
		GLimp_LogComment( "--- GL_BindNullProgram ---\n" );
	}

	if ( glState.currentProgram )
	{
		glUseProgram( 0 );
		glState.currentProgram = nullptr;
	}
}

void GL_SelectTexture( int unit )
{

	if ( glState.currenttmu == unit )
	{
		return;
	}

	if ( unit >= 0 && unit < glConfig2.maxTextureUnits )
	{
		glActiveTexture( GL_TEXTURE0 + unit );

		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "glActiveTexture( GL_TEXTURE%i )\n", unit ) );
		}
	}
	else
	{
		Sys::Drop( "GL_SelectTexture: unit = %i", unit );
	}

	glState.currenttmu = unit;
}

GLuint64 GL_BindToTMU( int unit, image_t *image )
{
	if ( !image )
	{
		Log::Warn("GL_BindToTMU: NULL image" );
		image = tr.defaultImage;
	}

	if ( glConfig2.usingBindlessTextures ) {
		if ( materialSystem.generatingWorldCommandBuffer ) {
			materialSystem.AddTexture( image->texture );
			return image->texture->bindlessTextureHandle;
		}

		return tr.textureManager.BindTexture( 0, image->texture );
	}

	int texnum = image->texnum;

	if ( unit < 0 || unit >= glConfig2.maxTextureUnits )
	{
		Sys::Drop( "GL_BindToTMU: unit %i is out of range\n", unit );
	}

	if ( tr.currenttextures[ unit ] == texnum )
	{
		return 0;
	}

	GL_SelectTexture( unit );
	GL_Bind( image );
	return 0;
}

void GL_BlendFunc( GLenum sfactor, GLenum dfactor )
{
	if ( glState.blendSrc != ( signed ) sfactor || glState.blendDst != ( signed ) dfactor )
	{
		glState.blendSrc = sfactor;
		glState.blendDst = dfactor;

		glBlendFunc( sfactor, dfactor );
	}
}

void GL_ClearColor( GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha )
{
	if ( glState.clearColorRed != red || glState.clearColorGreen != green || glState.clearColorBlue != blue || glState.clearColorAlpha != alpha )
	{
		glState.clearColorRed = red;
		glState.clearColorGreen = green;
		glState.clearColorBlue = blue;
		glState.clearColorAlpha = alpha;

		glClearColor( red, green, blue, alpha );
	}
}

void GL_ClearDepth( GLclampd depth )
{
	if ( glState.clearDepth != depth )
	{
		glState.clearDepth = depth;

		glClearDepth( depth );
	}
}

void GL_ClearStencil( GLint s )
{
	if ( glState.clearStencil != s )
	{
		glState.clearStencil = s;

		glClearStencil( s );
	}
}

void GL_ColorMask( GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha )
{
	if ( glState.colorMaskRed != red || glState.colorMaskGreen != green || glState.colorMaskBlue != blue || glState.colorMaskAlpha != alpha )
	{
		glState.colorMaskRed = red;
		glState.colorMaskGreen = green;
		glState.colorMaskBlue = blue;
		glState.colorMaskAlpha = alpha;

		glColorMask( red, green, blue, alpha );
	}
}

void GL_DepthFunc( GLenum func )
{
	if ( glState.depthFunc != ( signed ) func )
	{
		glState.depthFunc = func;

		glDepthFunc( func );
	}
}

void GL_DepthMask( GLboolean flag )
{
	if ( glState.depthMask != flag )
	{
		glState.depthMask = flag;

		glDepthMask( flag );
	}
}

void GL_DrawBuffer( GLenum mode )
{
	if ( glState.drawBuffer != ( signed ) mode )
	{
		glState.drawBuffer = mode;

		glDrawBuffer( mode );
	}
}

void GL_FrontFace( GLenum mode )
{
	if ( glState.frontFace != ( signed ) mode )
	{
		glState.frontFace = mode;

		glFrontFace( mode );
	}
}

void GL_LoadModelViewMatrix( const matrix_t m )
{
	if ( MatrixCompare( glState.modelViewMatrix[ glState.stackIndex ], m ) )
	{
		return;
	}

	MatrixCopy( m, glState.modelViewMatrix[ glState.stackIndex ] );
	MatrixMultiply( glState.projectionMatrix[ glState.stackIndex ], glState.modelViewMatrix[ glState.stackIndex ],
	                glState.modelViewProjectionMatrix[ glState.stackIndex ] );
}

void GL_LoadProjectionMatrix( const matrix_t m )
{
	if ( MatrixCompare( glState.projectionMatrix[ glState.stackIndex ], m ) )
	{
		return;
	}

	MatrixCopy( m, glState.projectionMatrix[ glState.stackIndex ] );
	MatrixMultiply( glState.projectionMatrix[ glState.stackIndex ], glState.modelViewMatrix[ glState.stackIndex ],
	                glState.modelViewProjectionMatrix[ glState.stackIndex ] );
}

void GL_PushMatrix()
{
	glState.stackIndex++;

	if ( glState.stackIndex >= MAX_GLSTACK )
	{
		glState.stackIndex = MAX_GLSTACK - 1;
		Sys::Drop( "GL_PushMatrix: stack overflow = %i", glState.stackIndex );
	}
}

void GL_PopMatrix()
{
	glState.stackIndex--;

	if ( glState.stackIndex < 0 )
	{
		glState.stackIndex = 0;
		Sys::Drop( "GL_PushMatrix: stack underflow" );
	}
}

void GL_PolygonMode( GLenum face, GLenum mode )
{
	if ( glState.polygonFace != ( signed ) face || glState.polygonMode != ( signed ) mode )
	{
		glState.polygonFace = face;
		glState.polygonMode = mode;

		glPolygonMode( face, mode );
	}
}

void GL_Scissor( GLint x, GLint y, GLsizei width, GLsizei height )
{
	if ( glState.scissorX != x || glState.scissorY != y || glState.scissorWidth != width || glState.scissorHeight != height )
	{
		glState.scissorX = x;
		glState.scissorY = y;
		glState.scissorWidth = width;
		glState.scissorHeight = height;

		glScissor( x, y, width, height );
	}
}

void GL_Viewport( GLint x, GLint y, GLsizei width, GLsizei height )
{
	if ( glState.viewportX != x || glState.viewportY != y || glState.viewportWidth != width || glState.viewportHeight != height )
	{
		glState.viewportX = x;
		glState.viewportY = y;
		glState.viewportWidth = width;
		glState.viewportHeight = height;

		glViewport( x, y, width, height );
	}
}

void GL_PolygonOffset( float factor, float units )
{
	if ( glState.polygonOffsetFactor != factor || glState.polygonOffsetUnits != units )
	{
		glState.polygonOffsetFactor = factor;
		glState.polygonOffsetUnits = units;

		glPolygonOffset( factor, units );
	}
}

void GL_Cull( cullType_t cullType )
{
	if ( backEnd.viewParms.mirrorLevel % 2 == 1 )
	{
		GL_FrontFace( GL_CW );
	}
	else
	{
		GL_FrontFace( GL_CCW );
	}

	if ( glState.faceCulling == cullType )
	{
		return;
	}

	if ( cullType == cullType_t::CT_TWO_SIDED )
	{
		glDisable( GL_CULL_FACE );
	}
	else
	{
		if( glState.faceCulling == cullType_t::CT_TWO_SIDED )
			glEnable( GL_CULL_FACE );

		if ( cullType == cullType_t::CT_BACK_SIDED )
		{
			glCullFace( GL_BACK );
		}
		else
		{
			glCullFace( GL_FRONT );
		}
	}
	glState.faceCulling = cullType;
}

/*
GL_State

This routine is responsible for setting the most commonly changed state
in Q3.
*/
void GL_State( uint32_t stateBits )
{
	uint32_t diff = stateBits ^ glState.glStateBits;
	diff &= ~glState.glStateBitsMask;

	if ( !diff )
	{
		return;
	}

	// check depthFunc bits
	if ( diff & GLS_DEPTHFUNC_BITS )
	{
		switch ( stateBits & GLS_DEPTHFUNC_BITS )
		{
			default:
				GL_DepthFunc( GL_LEQUAL );
				break;

			case GLS_DEPTHFUNC_ALWAYS:
				GL_DepthFunc( GL_ALWAYS );
				break;

			case GLS_DEPTHFUNC_LESS:
				GL_DepthFunc( GL_LESS );
				break;

			case GLS_DEPTHFUNC_EQUAL:
				GL_DepthFunc( GL_EQUAL );
				break;
		}
	}

	// check blend bits
	if ( diff & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) )
	{
		GLenum srcFactor, dstFactor;

		if ( stateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) )
		{
			switch ( stateBits & GLS_SRCBLEND_BITS )
			{
				case GLS_SRCBLEND_ZERO:
					srcFactor = GL_ZERO;
					break;

				case GLS_SRCBLEND_ONE:
					srcFactor = GL_ONE;
					break;

				case GLS_SRCBLEND_DST_COLOR:
					srcFactor = GL_DST_COLOR;
					break;

				case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
					srcFactor = GL_ONE_MINUS_DST_COLOR;
					break;

				case GLS_SRCBLEND_SRC_ALPHA:
					srcFactor = GL_SRC_ALPHA;
					break;

				case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
					srcFactor = GL_ONE_MINUS_SRC_ALPHA;
					break;

				case GLS_SRCBLEND_DST_ALPHA:
					srcFactor = GL_DST_ALPHA;
					break;

				case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
					srcFactor = GL_ONE_MINUS_DST_ALPHA;
					break;

				case GLS_SRCBLEND_ALPHA_SATURATE:
					srcFactor = GL_SRC_ALPHA_SATURATE;
					break;

				default:
					srcFactor = GL_ONE; // to get warning to shut up
					Sys::Drop( "GL_State: invalid src blend state bits" );
			}

			switch ( stateBits & GLS_DSTBLEND_BITS )
			{
				case GLS_DSTBLEND_ZERO:
					dstFactor = GL_ZERO;
					break;

				case GLS_DSTBLEND_ONE:
					dstFactor = GL_ONE;
					break;

				case GLS_DSTBLEND_SRC_COLOR:
					dstFactor = GL_SRC_COLOR;
					break;

				case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
					dstFactor = GL_ONE_MINUS_SRC_COLOR;
					break;

				case GLS_DSTBLEND_SRC_ALPHA:
					dstFactor = GL_SRC_ALPHA;
					break;

				case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
					dstFactor = GL_ONE_MINUS_SRC_ALPHA;
					break;

				case GLS_DSTBLEND_DST_ALPHA:
					dstFactor = GL_DST_ALPHA;
					break;

				case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
					dstFactor = GL_ONE_MINUS_DST_ALPHA;
					break;

				default:
					dstFactor = GL_ONE; // to get warning to shut up
					Sys::Drop( "GL_State: invalid dst blend state bits" );
			}

			glEnable( GL_BLEND );
			GL_BlendFunc( srcFactor, dstFactor );
		}
		else
		{
			glDisable( GL_BLEND );
		}
	}

	// check colormask
	if ( diff & GLS_COLORMASK_BITS )
	{
		GL_ColorMask( ( stateBits & GLS_REDMASK_FALSE ) ? GL_FALSE : GL_TRUE,
			      ( stateBits & GLS_GREENMASK_FALSE ) ? GL_FALSE : GL_TRUE,
			      ( stateBits & GLS_BLUEMASK_FALSE ) ? GL_FALSE : GL_TRUE,
			      ( stateBits & GLS_ALPHAMASK_FALSE ) ? GL_FALSE : GL_TRUE );
	}

	// check depthmask
	if ( diff & GLS_DEPTHMASK_TRUE )
	{
		if ( stateBits & GLS_DEPTHMASK_TRUE )
		{
			GL_DepthMask( GL_TRUE );
		}
		else
		{
			GL_DepthMask( GL_FALSE );
		}
	}

	// fill/line mode
	if ( diff & GLS_POLYMODE_LINE )
	{
		if ( stateBits & GLS_POLYMODE_LINE )
		{
			GL_PolygonMode( GL_FRONT_AND_BACK, GL_LINE );
		}
		else
		{
			GL_PolygonMode( GL_FRONT_AND_BACK, GL_FILL );
		}
	}

	// depthtest
	if ( diff & GLS_DEPTHTEST_DISABLE )
	{
		if ( stateBits & GLS_DEPTHTEST_DISABLE )
		{
			glDisable( GL_DEPTH_TEST );
		}
		else
		{
			glEnable( GL_DEPTH_TEST );
		}
	}

	glState.glStateBits ^= diff;
}

void GL_VertexAttribsState( uint32_t stateBits )
{
	uint32_t diff;
	uint32_t i;

	if ( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning )
	{
		stateBits |= ATTR_BONE_FACTORS;
	}

	GL_VertexAttribPointers( stateBits );

	diff = stateBits ^ glState.vertexAttribsState;

	if ( !diff )
	{
		return;
	}

	for ( i = 0; i < ATTR_INDEX_MAX; i++ )
	{
		uint32_t bit = BIT( i );

		if ( ( diff & bit ) )
		{
			if ( ( stateBits & bit ) )
			{
				if ( r_logFile->integer )
				{
					static char buf[ MAX_STRING_CHARS ];
					Q_snprintf( buf, sizeof( buf ), "glEnableVertexAttribArray( %s )\n", attributeNames[ i ] );

					GLimp_LogComment( buf );
				}

				glEnableVertexAttribArray( i );
			}
			else
			{
				if ( r_logFile->integer )
				{
					static char buf[ MAX_STRING_CHARS ];
					Q_snprintf( buf, sizeof( buf ), "glDisableVertexAttribArray( %s )\n", attributeNames[ i ] );

					GLimp_LogComment( buf );
				}

				glDisableVertexAttribArray( i );
			}
		}
	}

	glState.vertexAttribsState = stateBits;
}

void GL_VertexAttribPointers( uint32_t attribBits )
{
	uint32_t i;

	if ( !glState.currentVBO )
	{
		Sys::Error( "GL_VertexAttribPointers: no VBO bound" );
	}

	if ( r_logFile->integer )
	{
		// don't just call LogComment, or we will get a call to va() every frame!
		GLimp_LogComment( va( "--- GL_VertexAttribPointers( %s ) ---\n", glState.currentVBO->name ) );
	}

	if ( glConfig2.vboVertexSkinningAvailable && tess.vboVertexSkinning )
	{
		attribBits |= ATTR_BONE_FACTORS;
	}

	for ( i = 0; i < ATTR_INDEX_MAX; i++ )
	{
		uint32_t bit = BIT( i );
		uint32_t frame = 0;
		uintptr_t base = 0;

		if( glState.currentVBO == tess.vbo ) {
			base = tess.vertexBase * sizeof( shaderVertex_t );
		}

		if ( ( attribBits & bit ) != 0 &&
		     ( !( glState.vertexAttribPointersSet & bit ) ||
		       glState.vertexAttribsInterpolation >= 0 ||
		       glState.currentVBO == tess.vbo ) )
		{
			const vboAttributeLayout_t *layout = &glState.currentVBO->attribs[ i ];

			if ( r_logFile->integer )
			{
				static char buf[ MAX_STRING_CHARS ];
				Q_snprintf( buf, sizeof( buf ), "glVertexAttribPointer( %s )\n", attributeNames[ i ] );

				GLimp_LogComment( buf );
			}

			if ( ( ATTR_INTERP_BITS & bit ) && glState.vertexAttribsInterpolation > 0 )
			{
				frame = glState.vertexAttribsNewFrame;
			}
			else
			{
				frame = glState.vertexAttribsOldFrame;
			}

			if ( !( glState.currentVBO->attribBits & bit ) )
			{
				Log::Warn( "GL_VertexAttribPointers: %s does not have %s",
				           glState.currentVBO->name, attributeNames[ i ] );
			}

			glVertexAttribPointer( i, layout->numComponents, layout->componentType, layout->normalize, layout->stride, BUFFER_OFFSET( layout->ofs + ( frame * layout->frameOffset + base ) ) );
			glState.vertexAttribPointersSet |= bit;
		}
	}
}

/*
================
RB_Hyperspace

A player has predicted a teleport, but hasn't arrived yet
================
*/
static void RB_Hyperspace()
{
	float c;

	if ( !backEnd.isHyperspace )
	{
		// do initialization shit
	}

	c = ( backEnd.refdef.time & 255 ) / 255.0f;
	GL_ClearColor( c, c, c, 1 );
	glClear( GL_COLOR_BUFFER_BIT );

	backEnd.isHyperspace = true;
}

static void SetViewportAndScissor()
{
	float	mat[16], scale;
	vec4_t	q, c;

	MatrixCopy( backEnd.viewParms.projectionMatrix, mat );
	if( backEnd.viewParms.portalLevel > 0 )
	{
		VectorCopy(backEnd.viewParms.portalFrustum[FRUSTUM_NEAR].normal, c);
		c[3] = backEnd.viewParms.portalFrustum[FRUSTUM_NEAR].dist;

		// Lengyel, Eric. "Modifying the Projection Matrix to Perform Oblique Near-plane Clipping".
		// Terathon Software 3D Graphics Library, 2004. http://www.terathon.com/code/oblique.html
		q[0] = (c[0] < 0.0f ? -1.0f : 1.0f) / mat[0];
		q[1] = (c[1] < 0.0f ? -1.0f : 1.0f) / mat[5];
		q[2] = -1.0f;
		q[3] = (1.0f + mat[10]) / mat[14];

		scale = 2.0f / (DotProduct( c, q ) + c[3] * q[3]);
		mat[2]  = c[0] * scale;
		mat[6]  = c[1] * scale;
		mat[10] = c[2] * scale + 1.0f;
		mat[14] = c[3] * scale;
	}

	GL_LoadProjectionMatrix( mat );

	// set the window clipping
	GL_Viewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
	             backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );

	GL_Scissor( backEnd.viewParms.scissorX, backEnd.viewParms.scissorY,
	            backEnd.viewParms.scissorWidth, backEnd.viewParms.scissorHeight);
}

/*
================
RB_SetGL2D
================
*/
static void RB_SetGL2D()
{
	matrix_t proj;

	GLimp_LogComment( "--- RB_SetGL2D ---\n" );

	// disable offscreen rendering
	R_BindNullFBO();

	backEnd.projection2D = true;

	// set 2D virtual screen size
	GL_Viewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	GL_Scissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );

	MatrixOrthogonalProjection( proj, 0, glConfig.vidWidth, glConfig.vidHeight, 0, 0, 1 );
	// zero the z coordinate so it's never near/far clipped
	proj[ 2 ] = proj[ 6 ] = proj[ 10 ] = proj[ 14 ] = 0;

	GL_LoadProjectionMatrix( proj );
	GL_LoadModelViewMatrix( matrixIdentity );

	GL_Cull( cullType_t::CT_TWO_SIDED );

	// set time for 2D shaders
	backEnd.refdef.time = ri.Milliseconds();
	backEnd.refdef.floatTime =float(double(backEnd.refdef.time) * 0.001);
}

// used as bitfield
enum renderDrawSurfaces_e
{
  DRAWSURFACES_WORLD         = 1 << 0,
  DRAWSURFACES_FAR_ENTITIES  = 1 << 1,
  DRAWSURFACES_NEAR_ENTITIES = 1 << 2,
  DRAWSURFACES_ALL_FAR       = DRAWSURFACES_WORLD | DRAWSURFACES_FAR_ENTITIES,
  DRAWSURFACES_ALL_ENTITIES  = DRAWSURFACES_FAR_ENTITIES | DRAWSURFACES_NEAR_ENTITIES,
  DRAWSURFACES_ALL           = DRAWSURFACES_WORLD | DRAWSURFACES_ALL_ENTITIES
};

// When rendering a surface, the geometry generally goes through a three-stage pipeline:
// (1) Surface function (from rb_surfaceTable). This function generates the triangles, either by
//     explicitly writing them out, or by indicating a range from a static VBO/IBO.
// (2) Stage iterator function. Loops over the stages of a q3shader (if applicable), and sets up
//     some drawing parameters, in particular tess.svars, for each one. Responsible for updating
//     the non-static VBO (tess.verts) if it is used.
// (3) Render function. Feeds parameters to the GLSL shader and executes it with Tess_DrawElements.
//
// Function (1) is chosen based on the type of surface. (2) is chosen at the top level. (3) is
// chosen by (2).
//
// Batches of triangles from multiple calls to (1) may be merged together if everything is
// compatible between them. Draw surf sorting is an attempt to make this happen more often. But
// if there is not enough room in the buffers, an immediate call to (2) may be needed. Each batch
// of triangles may be rendered multiple times as (2) iterates shader stages.
//
// Note that portal recursion is done in the frontend when adding draw surfaces, not here.
static void RB_RenderDrawSurfaces( shaderSort_t fromSort, shaderSort_t toSort,
				   renderDrawSurfaces_e drawSurfFilter )
{
	trRefEntity_t *entity, *oldEntity;
	shader_t      *shader, *oldShader;
	int           lightmapNum, oldLightmapNum;
	int           fogNum, oldFogNum;
	bool          bspSurface;
	bool      depthRange, oldDepthRange;
	int           i;
	drawSurf_t    *drawSurf;
	int           lastSurf;

	GLimp_LogComment( "--- RB_RenderDrawSurfaces ---\n" );

	// draw everything
	oldEntity = nullptr;
	oldShader = nullptr;
	oldLightmapNum = -1;
	oldFogNum = -1;
	oldDepthRange = false;
	depthRange = false;
	backEnd.currentLight = nullptr;

	lastSurf = backEnd.viewParms.firstDrawSurf[ Util::ordinal(toSort) + 1 ];
	for ( i = backEnd.viewParms.firstDrawSurf[ Util::ordinal(fromSort) ]; i < lastSurf; i++ )
	{
		drawSurf = &backEnd.viewParms.drawSurfs[ i ];
		tess.currentDrawSurf = drawSurf;

		// FIXME: investigate why this happens.
		if( drawSurf->surface == nullptr )
		{
			continue;
		}

		// update locals
		entity = drawSurf->entity;
		shader = drawSurf->shader;
		lightmapNum = drawSurf->lightmapNum();
		fogNum = drawSurf->fog;
		bspSurface = drawSurf->bspSurface;

		if( entity == &tr.worldEntity ) {
			if( !( drawSurfFilter & DRAWSURFACES_WORLD ) )
				continue;
		} else if( !( entity->e.renderfx & RF_DEPTHHACK ) ) {
			if( !( drawSurfFilter & DRAWSURFACES_FAR_ENTITIES ) )
				continue;
		} else {
			if( !( drawSurfFilter & DRAWSURFACES_NEAR_ENTITIES ) )
				continue;
		}

		if ( entity == oldEntity && shader == oldShader && lightmapNum == oldLightmapNum && fogNum == oldFogNum )
		{
			// fast path, same as previous sort
			rb_surfaceTable[Util::ordinal(*drawSurf->surface)](drawSurf->surface );
			continue;
		}

		// change the tess parameters if needed
		// an "entityMergable" shader is a shader that can have surfaces from separate
		// entities merged into a single batch, like smoke and blood puff sprites
		if ( shader != oldShader || lightmapNum != oldLightmapNum || fogNum != oldFogNum || ( entity != oldEntity && !shader->entityMergable ) )
		{
			if ( oldShader != nullptr )
			{
				Tess_End();
			}

			Tess_Begin( Tess_StageIteratorColor, shader, nullptr, false, lightmapNum, fogNum, bspSurface );

			oldShader = shader;
			oldLightmapNum = lightmapNum;
			oldFogNum = fogNum;
		}

		// change the modelview matrix if needed
		if ( entity != oldEntity )
		{
			depthRange = false;

			if ( entity != &tr.worldEntity )
			{
				backEnd.currentEntity = entity;

				// set up the transformation matrix
				R_RotateEntityForViewParms( backEnd.currentEntity, &backEnd.viewParms, &backEnd.orientation );

				if ( backEnd.currentEntity->e.renderfx & RF_DEPTHHACK )
				{
					// hack the depth range to prevent view model from poking into walls
					depthRange = true;
				}
			}
			else
			{
				backEnd.currentEntity = &tr.worldEntity;
				backEnd.orientation = backEnd.viewParms.world;
			}

			GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );

			// change depthrange if needed
			if ( oldDepthRange != depthRange )
			{
				if ( depthRange )
				{
					glDepthRange( 0, 0.3 );
				}
				else
				{
					glDepthRange( 0, 1 );
				}

				oldDepthRange = depthRange;
			}

			oldEntity = entity;
		}

		// add the triangles for this surface
		rb_surfaceTable[Util::ordinal(*drawSurf->surface)](drawSurf->surface );
	}

	// draw the contents of the last shader batch
	if ( oldShader != nullptr )
	{
		Tess_End();
	}

	// go back to the world modelview matrix
	GL_LoadModelViewMatrix( backEnd.viewParms.world.modelViewMatrix );

	if ( depthRange )
	{
		glDepthRange( 0, 1 );
	}

	GL_CheckErrors();
}

/*
 * helper function for parallel split shadow mapping
 */
static int MergeInteractionBounds( const matrix_t lightViewProjectionMatrix, interaction_t *ia, int iaCount, vec3_t bounds[ 2 ], bool shadowCasters )
{
	int           i;
	int           j;
	surfaceType_t *surface;
	vec4_t        point;
	vec4_t        transf;
	vec3_t        worldBounds[ 2 ];
	int       numCasters;

	frustum_t frustum;
	cplane_t  *clipPlane;
	int       r;

	numCasters = 0;
	ClearBounds( bounds[ 0 ], bounds[ 1 ] );

	// calculate frustum planes using the modelview projection matrix
	R_SetupFrustum2( frustum, lightViewProjectionMatrix );

	while ( iaCount < backEnd.viewParms.numInteractions )
	{
		surface = ia->surface;

		if ( shadowCasters )
		{
			if ( !(ia->type & IA_SHADOW) )
			{
				goto skipInteraction;
			}
		}
		else
		{
			// we only merge shadow receivers
			if ( !(ia->type & IA_LIGHT) )
			{
				goto skipInteraction;
			}
		}

		if ( *surface == surfaceType_t::SF_FACE || *surface == surfaceType_t::SF_GRID || *surface == surfaceType_t::SF_TRIANGLES )
		{
			srfGeneric_t *gen = ( srfGeneric_t * ) surface;

			VectorCopy( gen->bounds[ 0 ], worldBounds[ 0 ] );
			VectorCopy( gen->bounds[ 1 ], worldBounds[ 1 ] );
		}
		else if ( *surface == surfaceType_t::SF_VBO_MESH )
		{
			srfVBOMesh_t *srf = ( srfVBOMesh_t * ) surface;

			VectorCopy( srf->bounds[ 0 ], worldBounds[ 0 ] );
			VectorCopy( srf->bounds[ 1 ], worldBounds[ 1 ] );
		}
		else if ( *surface == surfaceType_t::SF_MDV )
		{
			goto skipInteraction;
		}
		else
		{
			goto skipInteraction;
		}

		// use the frustum planes to cut off shadow casters beyond the split frustum
		for ( i = 0; i < 6; i++ )
		{
			clipPlane = &frustum[ i ];

			// we can have shadow casters outside the initial computed light view frustum
			if ( i == Util::ordinal(frustumBits_t::FRUSTUM_NEAR) && shadowCasters )
			{
				continue;
			}

			r = BoxOnPlaneSide( worldBounds[ 0 ], worldBounds[ 1 ], clipPlane );

			if ( r == 2 )
			{
				goto skipInteraction;
			}
		}

		if ( shadowCasters && (ia->type & IA_SHADOW) )
		{
			numCasters++;
		}

		for ( j = 0; j < 8; j++ )
		{
			point[ 0 ] = worldBounds[ j & 1 ][ 0 ];
			point[ 1 ] = worldBounds[( j >> 1 ) & 1 ][ 1 ];
			point[ 2 ] = worldBounds[( j >> 2 ) & 1 ][ 2 ];
			point[ 3 ] = 1;

			MatrixTransform4( lightViewProjectionMatrix, point, transf );
			transf[ 0 ] /= transf[ 3 ];
			transf[ 1 ] /= transf[ 3 ];
			transf[ 2 ] /= transf[ 3 ];

			AddPointToBounds( transf, bounds[ 0 ], bounds[ 1 ] );
		}

skipInteraction:

		if ( !ia->next )
		{
			// this is the last interaction of the current light
			break;
		}
		else
		{
			// just continue
			ia = ia->next;
			iaCount++;
		}
	}

	return numCasters;
}

static interaction_t *IterateLights( const interaction_t *prev )
{
	if ( !prev && backEnd.viewParms.numInteractions > 0 )
	{
		return backEnd.viewParms.interactions;
	}

	if ( backEnd.viewParms.numInteractions <= 0 )
	{
		return nullptr;
	}

	const interaction_t *next = prev;
	const interaction_t *last = &backEnd.viewParms.interactions[ backEnd.viewParms.numInteractions - 1 ];

	while ( next <= last && next->light == prev->light )
	{
		next++;
	}

	if ( next > last )
	{
		next = nullptr;
	}

	return ( interaction_t * ) next;
}

static void RB_SetupLightAttenuationForEntity( trRefLight_t *light, const trRefEntity_t *entity )
{
	matrix_t modelToLight;

	// transform light origin into model space for u_LightOrigin parameter
	if ( entity != &tr.worldEntity )
	{
		vec3_t tmp;
		VectorSubtract( light->origin, backEnd.orientation.origin, tmp );
		light->transformed[ 0 ] = DotProduct( tmp, backEnd.orientation.axis[ 0 ] );
		light->transformed[ 1 ] = DotProduct( tmp, backEnd.orientation.axis[ 1 ] );
		light->transformed[ 2 ] = DotProduct( tmp, backEnd.orientation.axis[ 2 ] );
	}
	else
	{
		VectorCopy( light->origin, light->transformed );
	}

	MatrixMultiply( light->viewMatrix, backEnd.orientation.transformMatrix, modelToLight );

	// build the attenuation matrix using the entity transform
	switch ( light->l.rlType )
	{
		case refLightType_t::RL_OMNI:
			{
				MatrixSetupTranslation( light->attenuationMatrix, 0.5, 0.5, 0.5 );  // bias
				MatrixMultiplyScale( light->attenuationMatrix, 0.5, 0.5, 0.5 );  // scale
				MatrixMultiply2( light->attenuationMatrix, light->projectionMatrix );
				MatrixMultiply2( light->attenuationMatrix, modelToLight );

				MatrixCopy( light->attenuationMatrix, light->shadowMatrices[ 0 ] );
				break;
			}

		case refLightType_t::RL_PROJ:
			{
				MatrixSetupTranslation( light->attenuationMatrix, 0.5, 0.5, 0.0 );  // bias
				MatrixMultiplyScale( light->attenuationMatrix, 0.5f, 0.5f, 1.0f / std::min( light->falloffLength, 1.0f ) );   // scale
				MatrixMultiply2( light->attenuationMatrix, light->projectionMatrix );
				MatrixMultiply2( light->attenuationMatrix, modelToLight );

				MatrixCopy( light->attenuationMatrix, light->shadowMatrices[ 0 ] );
				break;
			}

		case refLightType_t::RL_DIRECTIONAL:
			{
				MatrixSetupTranslation( light->attenuationMatrix, 0.5, 0.5, 0.5 );  // bias
				MatrixMultiplyScale( light->attenuationMatrix, 0.5, 0.5, 0.5 );  // scale
				MatrixMultiply2( light->attenuationMatrix, light->projectionMatrix );
				MatrixMultiply2( light->attenuationMatrix, modelToLight );
				break;
			}

		case refLightType_t::RL_MAX_REF_LIGHT_TYPE:
			{
				//Nothing for right now...
				break;
			}
	}
}

/*
=================
RB_RenderInteractions
=================
*/
static void RB_RenderInteractions()
{
	shader_t      *shader, *oldShader;
	trRefEntity_t *entity, *oldEntity;
	trRefLight_t  *light;
	const interaction_t *ia;
	const interaction_t *iaFirst;
	bool      depthRange, oldDepthRange;
	surfaceType_t *surface;
	int           startTime = 0, endTime = 0;

	GLimp_LogComment( "--- RB_RenderInteractions ---\n" );

	if ( r_speeds->integer == Util::ordinal(renderSpeeds_t::RSPEEDS_SHADING_TIMES))
	{
		glFinish();
		startTime = ri.Milliseconds();
	}

	// draw everything
	oldEntity = nullptr;
	oldShader = nullptr;
	oldDepthRange = false;
	depthRange = false;
	iaFirst = nullptr;

	// render interactions
	while ( ( iaFirst = IterateLights( iaFirst ) ) )
	{
		backEnd.currentLight = light = iaFirst->light;

		// set light scissor to reduce fillrate
		GL_Scissor( iaFirst->scissorX, iaFirst->scissorY, iaFirst->scissorWidth, iaFirst->scissorHeight );

		for ( ia = iaFirst; ia; ia = ia->next )
		{
			backEnd.currentEntity = entity = ia->entity;
			surface = ia->surface;
			shader = ia->shader;

			if ( !shader || !shader->interactLight )
			{
				// skip this interaction because the surface shader has no ability to interact with light
				// this will save texcoords and matrix calculations
				continue;
			}

			if ( !(ia->type & IA_LIGHT) )
			{
				// skip this interaction because the interaction is meant for shadowing only
				continue;
			}

			GLimp_LogComment( "----- Rendering new light -----\n" );

			// Tr3B: this should never happen in the first iteration
			if ( entity == oldEntity && shader == oldShader )
			{
				// fast path, same as previous
				rb_surfaceTable[Util::ordinal(*surface)](surface );
				continue;
			}

			// draw the contents of the last shader batch
			Tess_End();

			// begin a new batch
			Tess_Begin( Tess_StageIteratorLighting, shader, light->shader, false, -1, 0 );

			// change the modelview matrix if needed
			if ( entity != oldEntity )
			{
				depthRange = false;

				if ( entity != &tr.worldEntity )
				{
					// set up the transformation matrix
					R_RotateEntityForViewParms( backEnd.currentEntity, &backEnd.viewParms, &backEnd.orientation );

					if ( backEnd.currentEntity->e.renderfx & RF_DEPTHHACK )
					{
						// hack the depth range to prevent view model from poking into walls
						depthRange = true;
					}
				}
				else
				{
					backEnd.orientation = backEnd.viewParms.world;
				}

				GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );

				// change depthrange if needed
				if ( oldDepthRange != depthRange )
				{
					if ( depthRange )
					{
						glDepthRange( 0, 0.3 );
					}
					else
					{
						glDepthRange( 0, 1 );
					}

					oldDepthRange = depthRange;
				}

				RB_SetupLightAttenuationForEntity( light, entity );
			}

			// add the triangles for this surface
			rb_surfaceTable[Util::ordinal(*surface)](surface );
			oldEntity = entity;
			oldShader = shader;
		}

		// draw the contents of the last shader batch
		Tess_End();

		// force updates
		oldEntity = nullptr;
		oldShader = nullptr;
	}

	Tess_End();

	// go back to the world modelview matrix
	GL_LoadModelViewMatrix( backEnd.viewParms.world.modelViewMatrix );

	if ( depthRange )
	{
		glDepthRange( 0, 1 );
	}

	// reset scissor
	GL_Scissor( backEnd.viewParms.scissorX, backEnd.viewParms.scissorY,
	            backEnd.viewParms.scissorWidth, backEnd.viewParms.scissorHeight );

	GL_CheckErrors();

	if ( r_speeds->integer == Util::ordinal(renderSpeeds_t::RSPEEDS_SHADING_TIMES) )
	{
		glFinish();
		endTime = ri.Milliseconds();
		backEnd.pc.c_forwardLightingTime += endTime - startTime;
	}
}

static void RB_SetupLightForShadowing( trRefLight_t *light, int index,
				       bool shadowClip )
{
	// HACK: bring OpenGL into a safe state or strange FBO update problems will occur
	GL_BindNullProgram();
	GL_State( GLS_DEFAULT );

	GL_Bind( tr.whiteImage );
	int cubeSide = index;
	int splitFrustumIndex = index;
	interaction_t *ia = light->firstInteraction;
	int iaCount = ia - backEnd.viewParms.interactions;

	switch ( light->l.rlType )
	{
		case refLightType_t::RL_OMNI:
			{
				float    zNear, zFar;
				float    fovX, fovY;
				bool flipX, flipY;
				vec3_t   angles;
				matrix_t rotationMatrix, transformMatrix, viewMatrix;

				if ( r_logFile->integer )
				{
					// don't just call LogComment, or we will get
					// a call to va() every frame!
					GLimp_LogComment( va( "----- Rendering shadowCube side: %i -----\n", cubeSide ) );
				}

				R_BindFBO( tr.shadowMapFBO[ light->shadowLOD ] );
				if( shadowClip )
				{
					R_AttachFBOTexture2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X + cubeSide,
							      tr.shadowClipCubeFBOImage[ light->shadowLOD ]->texnum, 0 );
				}
				else
				{
					R_AttachFBOTexture2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X + cubeSide,
							      tr.shadowCubeFBOImage[ light->shadowLOD ]->texnum, 0 );
				}

				if ( checkGLErrors() )
				{
					R_CheckFBO( tr.shadowMapFBO[ light->shadowLOD ] );
				}

				// set the window clipping
				GL_Viewport( 0, 0, shadowMapResolutions[ light->shadowLOD ], shadowMapResolutions[ light->shadowLOD ] );
				GL_Scissor( 0, 0, shadowMapResolutions[ light->shadowLOD ], shadowMapResolutions[ light->shadowLOD ] );

				glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

				switch ( cubeSide )
				{
					case 0:
						{
							// view parameters
							VectorSet( angles, 0, 0, 90 );

							// projection parameters
							flipX = false;
							flipY = false;
							break;
						}

					case 1:
						{
							VectorSet( angles, 0, 180, 90 );
							flipX = true;
							flipY = true;
							break;
						}

					case 2:
						{
							VectorSet( angles, 0, 90, 0 );
							flipX = false;
							flipY = false;
							break;
						}

					case 3:
						{
							VectorSet( angles, 0, -90, 0 );
							flipX = true;
							flipY = true;
							break;
						}

					case 4:
						{
							VectorSet( angles, -90, 90, 0 );
							flipX = false;
							flipY = false;
							break;
						}

					case 5:
						{
							VectorSet( angles, 90, 90, 0 );
							flipX = true;
							flipY = true;
							break;
						}

					default:
						{
							// shut up compiler
							VectorSet( angles, 0, 0, 0 );
							flipX = false;
							flipY = false;
							break;
						}
				}

				// Quake -> OpenGL view matrix from light perspective
				MatrixFromAngles( rotationMatrix, angles[ PITCH ], angles[ YAW ], angles[ ROLL ] );
				MatrixSetupTransformFromRotation( transformMatrix, rotationMatrix, light->origin );
				MatrixAffineInverse( transformMatrix, viewMatrix );

				// convert from our coordinate system (looking down X)
				// to OpenGL's coordinate system (looking down -Z)
				MatrixMultiply( quakeToOpenGLMatrix, viewMatrix, light->viewMatrix );

				// OpenGL projection matrix
				fovX = 90;
				fovY = 90;

				zNear = 1.0;
				zFar = light->sphereRadius;

				if ( flipX )
				{
					fovX = -fovX;
				}

				if ( flipY )
				{
					fovY = -fovY;
				}

				MatrixPerspectiveProjectionFovXYRH( light->projectionMatrix, fovX, fovY, zNear, zFar );

				GL_LoadProjectionMatrix( light->projectionMatrix );
				break;
			}

		case refLightType_t::RL_PROJ:
			{
				GLimp_LogComment( "--- Rendering projective shadowMap ---\n" );

				R_BindFBO( tr.shadowMapFBO[ light->shadowLOD ] );
				if( shadowClip )
				{
					R_AttachFBOTexture2D( GL_TEXTURE_2D, tr.shadowClipMapFBOImage[ light->shadowLOD ]->texnum, 0 );
				}
				else
				{
					R_AttachFBOTexture2D( GL_TEXTURE_2D, tr.shadowMapFBOImage[ light->shadowLOD ]->texnum, 0 );
				}

				if ( checkGLErrors() )
				{
					R_CheckFBO( tr.shadowMapFBO[ light->shadowLOD ] );
				}

				// set the window clipping
				GL_Viewport( 0, 0, shadowMapResolutions[ light->shadowLOD ], shadowMapResolutions[ light->shadowLOD ] );
				GL_Scissor( 0, 0, shadowMapResolutions[ light->shadowLOD ], shadowMapResolutions[ light->shadowLOD ] );

				glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

				GL_LoadProjectionMatrix( light->projectionMatrix );
				break;
			}

		case refLightType_t::RL_DIRECTIONAL:
			{
				int      j;
				vec3_t   angles;
				vec4_t   forward, side, up;
				vec3_t   lightDirection;
				vec3_t   viewOrigin, viewDirection;
				matrix_t rotationMatrix, transformMatrix, viewMatrix, projectionMatrix, viewProjectionMatrix;
				matrix_t cropMatrix;
				vec3_t   splitFrustumNearCorners[ 4 ];
				vec3_t   splitFrustumFarCorners[ 4 ];
				vec3_t   splitFrustumBounds[ 2 ];
				vec3_t   splitFrustumClipBounds[ 2 ];
				int      numCasters;
				vec3_t   casterBounds[ 2 ];
				vec3_t   receiverBounds[ 2 ];
				vec3_t   cropBounds[ 2 ];
				vec4_t   point;
				vec4_t   transf;

				GLimp_LogComment( "--- Rendering directional shadowMap ---\n" );

				R_BindFBO( tr.sunShadowMapFBO[ splitFrustumIndex ] );

				if( shadowClip )
				{
					R_AttachFBOTexture2D( GL_TEXTURE_2D, tr.sunShadowClipMapFBOImage[ splitFrustumIndex ]->texnum, 0 );
				}
				else if ( !r_evsmPostProcess->integer )
				{
					R_AttachFBOTexture2D( GL_TEXTURE_2D, tr.sunShadowMapFBOImage[ splitFrustumIndex ]->texnum, 0 );
				}
				else
				{
					R_AttachFBOTextureDepth( tr.sunShadowMapFBOImage[ splitFrustumIndex ]->texnum );
				}

				if ( checkGLErrors() )
				{
					R_CheckFBO( tr.sunShadowMapFBO[ splitFrustumIndex ] );
				}

				// set the window clipping
				GL_Viewport( 0, 0, sunShadowMapResolutions[ splitFrustumIndex ], sunShadowMapResolutions[ splitFrustumIndex ] );
				GL_Scissor( 0, 0, sunShadowMapResolutions[ splitFrustumIndex ], sunShadowMapResolutions[ splitFrustumIndex ] );

				glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

				VectorCopy( tr.sunDirection, lightDirection );

				if ( r_parallelShadowSplits->integer )
				{
					// original light direction is from surface to light
					VectorInverse( lightDirection );
					VectorNormalize( lightDirection );

					VectorCopy( backEnd.viewParms.orientation.origin, viewOrigin );
					VectorCopy( backEnd.viewParms.orientation.axis[ 0 ], viewDirection );
					VectorNormalize( viewDirection );

					// calculate new up dir
					CrossProduct( lightDirection, viewDirection, side );
					VectorNormalize( side );

					CrossProduct( side, lightDirection, up );
					VectorNormalize( up );

					vectoangles( lightDirection, angles );
					MatrixFromAngles( rotationMatrix, angles[ PITCH ], angles[ YAW ], angles[ ROLL ] );
					AngleVectors( angles, forward, side, up );

					MatrixLookAtRH( light->viewMatrix, viewOrigin, lightDirection, up );

					plane_t splitFrustum[ 6 ];
					for ( j = 0; j < 6; j++ )
					{
						VectorCopy( backEnd.viewParms.frustums[ 1 + splitFrustumIndex ][ j ].normal, splitFrustum[ j ].normal );
						splitFrustum[ j ].dist = backEnd.viewParms.frustums[ 1 + splitFrustumIndex ][ j ].dist;
					}

					R_CalcFrustumNearCorners( splitFrustum, splitFrustumNearCorners );
					R_CalcFrustumFarCorners( splitFrustum, splitFrustumFarCorners );

					if ( r_logFile->integer )
					{
						vec3_t rayIntersectionNear, rayIntersectionFar;
						float  zNear, zFar;

						PlaneIntersectRay( viewOrigin, viewDirection, splitFrustum[ FRUSTUM_FAR ], rayIntersectionFar );
						zFar = Distance( viewOrigin, rayIntersectionFar );

						VectorInverse( viewDirection );

						PlaneIntersectRay( rayIntersectionFar, viewDirection, splitFrustum[ FRUSTUM_NEAR ], rayIntersectionNear );
						zNear = Distance( viewOrigin, rayIntersectionNear );

						VectorInverse( viewDirection );

						GLimp_LogComment( va( "split frustum %i: near = %5.3f, far = %5.3f\n", splitFrustumIndex, zNear, zFar ) );
						GLimp_LogComment( va( "pyramid nearCorners\n" ) );

						for ( auto nearCorner : splitFrustumNearCorners )
						{
							GLimp_LogComment( va( "(%5.3f, %5.3f, %5.3f)\n", nearCorner[ 0 ], nearCorner[ 1 ], nearCorner[ 2 ] ) );
						}

						GLimp_LogComment( va( "pyramid farCorners\n" ) );

						for ( auto farCorner : splitFrustumFarCorners )
						{
							GLimp_LogComment( va( "(%5.3f, %5.3f, %5.3f)\n", farCorner[ 0 ], farCorner[ 1 ], farCorner[ 2 ] ) );
						}
					}

					ClearBounds( splitFrustumBounds[ 0 ], splitFrustumBounds[ 1 ] );

					for ( auto nearCorner : splitFrustumNearCorners )
					{
						AddPointToBounds( nearCorner, splitFrustumBounds[ 0 ], splitFrustumBounds[ 1 ] );
					}

					for ( auto farCorner : splitFrustumFarCorners )
					{
						AddPointToBounds( farCorner, splitFrustumBounds[ 0 ], splitFrustumBounds[ 1 ] );
					}

					//
					// Scene-Dependent Projection
					//

					// find the bounding box of the current split in the light's view space
					ClearBounds( cropBounds[ 0 ], cropBounds[ 1 ] );

					const auto pointsToViewBounds = [&]( vec_t *c ) {
						VectorCopy( c, point );
						point[ 3 ] = 1;
						MatrixTransform4( light->viewMatrix, point, transf );
						transf[ 0 ] /= transf[ 3 ];
						transf[ 1 ] /= transf[ 3 ];
						transf[ 2 ] /= transf[ 3 ];

						AddPointToBounds( transf, cropBounds[ 0 ], cropBounds[ 1 ] );
					};

					for ( auto nearCorner : splitFrustumNearCorners )
					{
						pointsToViewBounds( nearCorner );
					}

					for ( auto farCorner : splitFrustumFarCorners )
					{
						pointsToViewBounds( farCorner );
					}

					MatrixOrthogonalProjectionRH( projectionMatrix, cropBounds[ 0 ][ 0 ], cropBounds[ 1 ][ 0 ], cropBounds[ 0 ][ 1 ], cropBounds[ 1 ][ 1 ], -cropBounds[ 1 ][ 2 ], -cropBounds[ 0 ][ 2 ] );

					MatrixMultiply( projectionMatrix, light->viewMatrix, viewProjectionMatrix );

					numCasters = MergeInteractionBounds( viewProjectionMatrix, ia, iaCount, casterBounds, true );
					MergeInteractionBounds( viewProjectionMatrix, ia, iaCount, receiverBounds, false );

					// find the bounding box of the current split in the light's clip space
					ClearBounds( splitFrustumClipBounds[ 0 ], splitFrustumClipBounds[ 1 ] );

					const auto pointsToViewProjectionBounds = [&]( vec_t* c ) {
						VectorCopy( c, point );
						point[ 3 ] = 1;

						MatrixTransform4( viewProjectionMatrix, point, transf );
						transf[ 0 ] /= transf[ 3 ];
						transf[ 1 ] /= transf[ 3 ];
						transf[ 2 ] /= transf[ 3 ];

						AddPointToBounds( transf, splitFrustumClipBounds[ 0 ], splitFrustumClipBounds[ 1 ] );
					};

					for ( auto nearCorner : splitFrustumNearCorners )
					{
						pointsToViewProjectionBounds( nearCorner );
					}

					for ( auto farCorner : splitFrustumFarCorners )
					{
						pointsToViewProjectionBounds( farCorner );
					}

					if ( r_logFile->integer )
					{
						GLimp_LogComment( va( "shadow casters = %i\n", numCasters ) );

						GLimp_LogComment( va( "split frustum light space clip bounds (%5.3f, %5.3f, %5.3f) (%5.3f, %5.3f, %5.3f)\n",
										        splitFrustumClipBounds[ 0 ][ 0 ], splitFrustumClipBounds[ 0 ][ 1 ], splitFrustumClipBounds[ 0 ][ 2 ],
										        splitFrustumClipBounds[ 1 ][ 0 ], splitFrustumClipBounds[ 1 ][ 1 ], splitFrustumClipBounds[ 1 ][ 2 ] ) );

						GLimp_LogComment( va( "shadow caster light space clip bounds (%5.3f, %5.3f, %5.3f) (%5.3f, %5.3f, %5.3f)\n",
										        casterBounds[ 0 ][ 0 ], casterBounds[ 0 ][ 1 ], casterBounds[ 0 ][ 2 ],
										        casterBounds[ 1 ][ 0 ], casterBounds[ 1 ][ 1 ], casterBounds[ 1 ][ 2 ] ) );

						GLimp_LogComment( va( "light receiver light space clip bounds (%5.3f, %5.3f, %5.3f) (%5.3f, %5.3f, %5.3f)\n",
										        receiverBounds[ 0 ][ 0 ], receiverBounds[ 0 ][ 1 ], receiverBounds[ 0 ][ 2 ],
										        receiverBounds[ 1 ][ 0 ], receiverBounds[ 1 ][ 1 ], receiverBounds[ 1 ][ 2 ] ) );
					}

					// scene-dependent bounding volume
					cropBounds[ 0 ][ 0 ] = std::max( std::max( casterBounds[ 0 ][ 0 ], receiverBounds[ 0 ][ 0 ] ), splitFrustumClipBounds[ 0 ][ 0 ] );
					cropBounds[ 0 ][ 1 ] = std::max( std::max( casterBounds[ 0 ][ 1 ], receiverBounds[ 0 ][ 1 ] ), splitFrustumClipBounds[ 0 ][ 1 ] );

					cropBounds[ 1 ][ 0 ] = std::min( std::min( casterBounds[ 1 ][ 0 ], receiverBounds[ 1 ][ 0 ] ), splitFrustumClipBounds[ 1 ][ 0 ] );
					cropBounds[ 1 ][ 1 ] = std::min( std::min( casterBounds[ 1 ][ 1 ], receiverBounds[ 1 ][ 1 ] ), splitFrustumClipBounds[ 1 ][ 1 ] );

					cropBounds[ 0 ][ 2 ] = std::min( casterBounds[ 0 ][ 2 ], splitFrustumClipBounds[ 0 ][ 2 ] );
					cropBounds[ 1 ][ 2 ] = std::min( receiverBounds[ 1 ][ 2 ], splitFrustumClipBounds[ 1 ][ 2 ] );

					if ( numCasters == 0 )
					{
						VectorCopy( splitFrustumClipBounds[ 0 ], cropBounds[ 0 ] );
						VectorCopy( splitFrustumClipBounds[ 1 ], cropBounds[ 1 ] );
					}

					MatrixCrop( cropMatrix, cropBounds[ 0 ], cropBounds[ 1 ] );

					MatrixMultiply( cropMatrix, projectionMatrix, light->projectionMatrix );

					GL_LoadProjectionMatrix( light->projectionMatrix );
				}
				else
				{
					// original light direction is from surface to light
					VectorInverse( lightDirection );

					// Quake -> OpenGL view matrix from light perspective
					vectoangles( lightDirection, angles );
					MatrixFromAngles( rotationMatrix, angles[ PITCH ], angles[ YAW ], angles[ ROLL ] );
					MatrixSetupTransformFromRotation( transformMatrix, rotationMatrix, backEnd.viewParms.orientation.origin );
					MatrixAffineInverse( transformMatrix, viewMatrix );
					MatrixMultiply( quakeToOpenGLMatrix, viewMatrix, light->viewMatrix );

					ClearBounds( splitFrustumBounds[ 0 ], splitFrustumBounds[ 1 ] );
					//BoundsAdd(splitFrustumBounds[0], splitFrustumBounds[1], backEnd.viewParms.visBounds[0], backEnd.viewParms.visBounds[1]);
					BoundsAdd( splitFrustumBounds[ 0 ], splitFrustumBounds[ 1 ], light->worldBounds[ 0 ], light->worldBounds[ 1 ] );

					ClearBounds( cropBounds[ 0 ], cropBounds[ 1 ] );

					for ( j = 0; j < 8; j++ )
					{
						point[ 0 ] = splitFrustumBounds[ j & 1 ][ 0 ];
						point[ 1 ] = splitFrustumBounds[( j >> 1 ) & 1 ][ 1 ];
						point[ 2 ] = splitFrustumBounds[( j >> 2 ) & 1 ][ 2 ];
						point[ 3 ] = 1;

						MatrixTransform4( light->viewMatrix, point, transf );
						transf[ 0 ] /= transf[ 3 ];
						transf[ 1 ] /= transf[ 3 ];
						transf[ 2 ] /= transf[ 3 ];

						AddPointToBounds( transf, cropBounds[ 0 ], cropBounds[ 1 ] );
					}

					MatrixOrthogonalProjectionRH( light->projectionMatrix, cropBounds[ 0 ][ 0 ], cropBounds[ 1 ][ 0 ], cropBounds[ 0 ][ 1 ], cropBounds[ 1 ][ 1 ], -cropBounds[ 1 ][ 2 ], -cropBounds[ 0 ][ 2 ] );
					GL_LoadProjectionMatrix( light->projectionMatrix );
				}

				break;
			}

		default:
			break;
	}

	if ( r_logFile->integer )
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va( "----- First Shadow Interaction: %i -----\n", (int)( light->firstInteraction - backEnd.viewParms.interactions ) ) );
	}
}

static void RB_SetupLightForLighting( trRefLight_t *light )
{
	GLimp_LogComment( "--- Rendering lighting ---\n" );

	if ( r_logFile->integer )
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va( "----- First Light Interaction: %i -----\n", (int)( light->firstInteraction - backEnd.viewParms.interactions ) ) );
	}

	R_BindFBO( tr.mainFBO[ backEnd.currentMainFBO ] );

	// set the window clipping
	GL_Viewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
				    backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );

	interaction_t *iaFirst = light->firstInteraction;
	GL_Scissor( iaFirst->scissorX, iaFirst->scissorY,
				iaFirst->scissorWidth, iaFirst->scissorHeight );

	// restore camera matrices
	GL_LoadProjectionMatrix( backEnd.viewParms.projectionMatrix );
	GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );

	// reset light view and projection matrices
	switch ( light->l.rlType )
	{
		case refLightType_t::RL_OMNI:
			{
				MatrixAffineInverse( light->transformMatrix, light->viewMatrix );
				MatrixSetupScale( light->projectionMatrix, 1.0f / light->l.radius, 1.0f / light->l.radius,
							        1.0f / light->l.radius );
				break;
			}

		case refLightType_t::RL_DIRECTIONAL:
			{
				// draw split frustum shadow maps
				if ( r_showShadowMaps->integer )
				{
					int      frustumIndex;
					float    x, y, w, h;
					matrix_t ortho;
					vec4_t   quadVerts[ 4 ];

					// set 2D virtual screen size
					GL_PushMatrix();
					MatrixOrthogonalProjection( ortho, backEnd.viewParms.viewportX,
								                backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth,
								                backEnd.viewParms.viewportY,
								                backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight, -99999, 99999 );
					GL_LoadProjectionMatrix( ortho );

					for ( frustumIndex = 0; frustumIndex <= r_parallelShadowSplits->integer; frustumIndex++ )
					{
						GL_Cull( cullType_t::CT_TWO_SIDED );
						GL_State( GLS_DEPTHTEST_DISABLE );

						gl_debugShadowMapShader->BindProgram( 0 );

						gl_debugShadowMapShader->SetUniform_CurrentMapBindless(
							GL_BindToTMU( 0, tr.sunShadowMapFBOImage[frustumIndex] )
						);

						w = 200;
						h = 200;

						x = 205 * frustumIndex;
						y = 70;

						Tess_InstantQuad( *gl_debugShadowMapShader, x, y, w, h );

						{
							int    j;
							vec3_t farCorners[ 4 ];
							vec3_t nearCorners[ 4 ];

							GL_Viewport( x, y, w, h );
							GL_Scissor( x, y, w, h );

							GL_PushMatrix();

							gl_genericShader->SetVertexSkinning( false );
							gl_genericShader->SetVertexAnimation( false );
							gl_genericShader->SetTCGenEnvironment( false );
							gl_genericShader->SetTCGenLightmap( false );
							gl_genericShader->SetDepthFade( false );
							gl_genericShader->BindProgram( 0 );

							// set uniforms
							gl_genericShader->SetUniform_AlphaTest( GLS_ATEST_NONE );
							gl_genericShader->SetUniform_ColorModulateColorGen( colorGen_t::CGEN_VERTEX, alphaGen_t::AGEN_VERTEX );
							gl_genericShader->SetUniform_Color( Color::Black );

							GL_State( GLS_POLYMODE_LINE | GLS_DEPTHTEST_DISABLE );
							GL_Cull( cullType_t::CT_TWO_SIDED );

							// bind u_ColorMap
							gl_genericShader->SetUniform_ColorMapBindless(
								GL_BindToTMU( 0, tr.whiteImage )
							);
							gl_genericShader->SetUniform_TextureMatrix( matrixIdentity );

							gl_genericShader->SetUniform_ModelViewProjectionMatrix( light->shadowMatrices[ frustumIndex ] );

							tess.multiDrawPrimitives = 0;
							tess.numIndexes = 0;
							tess.numVertexes = 0;

							plane_t splitFrustum[ 6 ];
							for ( j = 0; j < 6; j++ )
							{
								VectorCopy( backEnd.viewParms.frustums[ 1 + frustumIndex ][ j ].normal, splitFrustum[ j ].normal );
								splitFrustum[ j ].dist = backEnd.viewParms.frustums[ 1 + frustumIndex ][ j ].dist;
							}

							R_CalcFrustumNearCorners( splitFrustum, nearCorners );
							R_CalcFrustumFarCorners( splitFrustum, farCorners );

							// draw outer surfaces
							for ( j = 0; j < 4; j++ )
							{
								Vector4Set( quadVerts[ 0 ], nearCorners[ j ][ 0 ], nearCorners[ j ][ 1 ], nearCorners[ j ][ 2 ], 1 );
								Vector4Set( quadVerts[ 1 ], farCorners[ j ][ 0 ], farCorners[ j ][ 1 ], farCorners[ j ][ 2 ], 1 );
								Vector4Set( quadVerts[ 2 ], farCorners[( j + 1 ) % 4 ][ 0 ], farCorners[( j + 1 ) % 4 ][ 1 ], farCorners[( j + 1 ) % 4 ][ 2 ], 1 );
								Vector4Set( quadVerts[ 3 ], nearCorners[( j + 1 ) % 4 ][ 0 ], nearCorners[( j + 1 ) % 4 ][ 1 ], nearCorners[( j + 1 ) % 4 ][ 2 ], 1 );
								Tess_AddQuadStamp2( quadVerts, Color::Cyan );
							}

							// draw far cap
							Vector4Set( quadVerts[ 0 ], farCorners[ 3 ][ 0 ], farCorners[ 3 ][ 1 ], farCorners[ 3 ][ 2 ], 1 );
							Vector4Set( quadVerts[ 1 ], farCorners[ 2 ][ 0 ], farCorners[ 2 ][ 1 ], farCorners[ 2 ][ 2 ], 1 );
							Vector4Set( quadVerts[ 2 ], farCorners[ 1 ][ 0 ], farCorners[ 1 ][ 1 ], farCorners[ 1 ][ 2 ], 1 );
							Vector4Set( quadVerts[ 3 ], farCorners[ 0 ][ 0 ], farCorners[ 0 ][ 1 ], farCorners[ 0 ][ 2 ], 1 );
							Tess_AddQuadStamp2( quadVerts, Color::Blue );

							// draw near cap
							Vector4Set( quadVerts[ 0 ], nearCorners[ 0 ][ 0 ], nearCorners[ 0 ][ 1 ], nearCorners[ 0 ][ 2 ], 1 );
							Vector4Set( quadVerts[ 1 ], nearCorners[ 1 ][ 0 ], nearCorners[ 1 ][ 1 ], nearCorners[ 1 ][ 2 ], 1 );
							Vector4Set( quadVerts[ 2 ], nearCorners[ 2 ][ 0 ], nearCorners[ 2 ][ 1 ], nearCorners[ 2 ][ 2 ], 1 );
							Vector4Set( quadVerts[ 3 ], nearCorners[ 3 ][ 0 ], nearCorners[ 3 ][ 1 ], nearCorners[ 3 ][ 2 ], 1 );
							Tess_AddQuadStamp2( quadVerts, Color::Green );

							Tess_UpdateVBOs( );
							GL_VertexAttribsState( ATTR_POSITION | ATTR_COLOR );
							Tess_DrawElements();

							tess.multiDrawPrimitives = 0;
							tess.numIndexes = 0;
							tess.numVertexes = 0;

							GL_PopMatrix();

							GL_Viewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
										    backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );

							GL_Scissor( iaFirst->scissorX, iaFirst->scissorY,
								iaFirst->scissorWidth, iaFirst->scissorHeight );
						}
					}

					GL_PopMatrix();
				}
			}
			break;

		default:
			break;
	}
}

static void RB_BlurShadowMap( const trRefLight_t *light, int i )
{
	if ( !glConfig2.shadowMapping )
	{
		return;
	}

	if ( light->l.inverseShadows )
	{
		return;
	}

	if ( !r_softShadowsPP->integer )
	{
		return;
	}

	if ( light->l.rlType == refLightType_t::RL_OMNI )
	{
		return;
	}

	int     index;
	image_t **images;
	FBO_t   **fbos;
	vec2_t  texScale;
	matrix_t ortho;

	fbos = ( light->l.rlType == refLightType_t::RL_DIRECTIONAL ) ? tr.sunShadowMapFBO : tr.shadowMapFBO;
	images = ( light->l.rlType == refLightType_t::RL_DIRECTIONAL ) ? tr.sunShadowMapFBOImage : tr.shadowMapFBOImage;
	index = ( light->l.rlType == refLightType_t::RL_DIRECTIONAL ) ? i : light->shadowLOD;

	texScale[ 0 ] = 1.0f / fbos[ index ]->width;
	texScale[ 1 ] = 1.0f / fbos[ index ]->height;

	R_BindFBO( fbos[ index ] );
	R_AttachFBOTexture2D( images[ index + MAX_SHADOWMAPS ]->type, images[ index + MAX_SHADOWMAPS ]->texnum, 0 );

	if ( checkGLErrors() )
	{
		R_CheckFBO( fbos[ index ] );
	}

	// set the window clipping
	GL_Viewport( 0, 0, fbos[index]->width, fbos[index]->height );
	GL_Scissor( 0, 0, fbos[index]->width, fbos[index]->height );

	glClear( GL_COLOR_BUFFER_BIT );

	GL_Cull( cullType_t::CT_TWO_SIDED );
	GL_State( GLS_DEPTHTEST_DISABLE );

	// GL_BindToTMU( 0, images[ index ] );

	GL_PushMatrix();

	MatrixOrthogonalProjection( ortho, 0, fbos[index]->width, 0, fbos[index]->height, -99999, 99999 );
	GL_LoadProjectionMatrix( ortho );

	gl_blurShader->BindProgram( 0 );
	gl_blurShader->SetUniform_DeformMagnitude( 1 );
	gl_blurShader->SetUniform_TexScale( texScale );
	gl_blurShader->SetUniform_Horizontal( true );

	gl_blurShader->SetUniform_ColorMapBindless(
		GL_BindToTMU( 0, images[index] )
	);

	Tess_InstantQuad( *gl_blurShader, 0, 0, fbos[ index ]->width, fbos[ index ]->height );

	R_AttachFBOTexture2D( images[ index ]->type, images[ index ]->texnum, 0 );

	glClear( GL_COLOR_BUFFER_BIT );

	gl_blurShader->BindProgram( 0 );
	gl_blurShader->SetUniform_DeformMagnitude( 1 );
	gl_blurShader->SetUniform_TexScale( texScale );
	gl_blurShader->SetUniform_Horizontal( false );

	gl_blurShader->SetUniform_ColorMapBindless(
		GL_BindToTMU( 0, images[index + MAX_SHADOWMAPS] )
	);

	Tess_InstantQuad( *gl_blurShader, 0, 0, fbos[index]->width, fbos[index]->height );

	GL_PopMatrix();
}

/*
=================
RB_RenderInteractionsShadowMapped
=================
*/

static void RB_RenderInteractionsShadowMapped()
{
	shader_t       *shader, *oldShader;
	trRefEntity_t  *entity, *oldEntity;
	trRefLight_t   *light;
	const interaction_t  *ia;
	const interaction_t  *iaFirst;
	surfaceType_t  *surface;
	bool       depthRange, oldDepthRange;
	bool       alphaTest, oldAlphaTest;
	bool       shadowClipFound;

	int            startTime = 0, endTime = 0;
	static const matrix_t bias = { 0.5,     0.0, 0.0, 0.0,
	                               0.0,     0.5, 0.0, 0.0,
	                               0.0,     0.0, 0.5, 0.0,
	                               0.5,     0.5, 0.5, 1.0
	                      };

	DAEMON_ASSERT( glConfig2.shadowMapping );

	GLimp_LogComment( "--- RB_RenderInteractionsShadowMapped ---\n" );

	if ( r_speeds->integer == Util::ordinal(renderSpeeds_t::RSPEEDS_SHADING_TIMES) )
	{
		glFinish();
		startTime = ri.Milliseconds();
	}

	// draw everything
	oldEntity = nullptr;
	oldShader = nullptr;
	oldDepthRange = depthRange = false;
	oldAlphaTest = alphaTest = false;

	// if we need to clear the FBO color buffers then it should be white
	GL_ClearColor( 1.0f, 1.0f, 1.0f, 1.0f );

	// render each light
	iaFirst = nullptr;

	while ( ( iaFirst = IterateLights( iaFirst ) ) )
	{
		backEnd.currentLight = light = iaFirst->light;

		// begin shadowing
		int numMaps;
		switch( light->l.rlType )
		{
			case refLightType_t::RL_OMNI:
				numMaps = 6;
				break;
			case refLightType_t::RL_DIRECTIONAL:
				numMaps = std::max( r_parallelShadowSplits->integer + 1, 1 );
				break;
			default:
				numMaps = 1;
				break;
		}

		const interaction_t *iaLast = iaFirst;
		for ( int i = 0; i < numMaps; i++ )
		{
			entity = nullptr;
			shader = nullptr;
			oldEntity = nullptr;
			oldShader = nullptr;

			if ( light->l.noShadows || light->shadowLOD < 0 )
			{
				if ( r_logFile->integer )
				{
					// don't just call LogComment, or we will get
					// a call to va() every frame!
					GLimp_LogComment( va( "----- Skipping shadowCube side: %i -----\n", i ) );
				}
				continue;
			}

			RB_SetupLightForShadowing( light, i, false );

			shadowClipFound = false;
			for( ia = iaFirst; ia; ia = ia->next )
			{
				iaLast = ia;
				backEnd.currentEntity = entity = ia->entity;
				surface = ia->surface;
				shader = ia->shader;
				alphaTest = shader->alphaTest;

				if ( entity->e.renderfx & ( RF_NOSHADOW | RF_DEPTHHACK ) )
				{
					continue;
				}

				if ( shader->isSky )
				{
					continue;
				}

				if ( shader->sort > Util::ordinal(shaderSort_t::SS_OPAQUE) )
				{
					continue;
				}

				if ( shader->noShadows )
				{
					continue;
				}

				if ( (ia->type & IA_SHADOWCLIP) ) {
					shadowClipFound = true;
				}

				if ( !(ia->type & IA_SHADOW) )
				{
					continue;
				}

				if ( light->l.rlType == refLightType_t::RL_OMNI && !( ia->cubeSideBits & ( 1 << i ) ) )
				{
					continue;
				}

				switch ( light->l.rlType )
				{
					case refLightType_t::RL_OMNI:
					case refLightType_t::RL_PROJ:
					case refLightType_t::RL_DIRECTIONAL:
						{
							if ( entity == oldEntity && ( alphaTest ? shader == oldShader : alphaTest == oldAlphaTest ) )
							{
								if ( r_logFile->integer )
								{
									// don't just call LogComment, or we will get
									// a call to va() every frame!
									GLimp_LogComment( va( "----- Batching Shadow Interaction: %i -----\n", (int)( ia - backEnd.viewParms.interactions ) ) );
								}

								// fast path, same as previous
								rb_surfaceTable[ Util::ordinal(*surface) ]( surface );
								continue;
							}
							else
							{
								// draw the contents of the last shader batch
								Tess_End();

								if ( r_logFile->integer )
								{
									// don't just call LogComment, or we will get
									// a call to va() every frame!
									GLimp_LogComment( va( "----- Beginning Shadow Interaction: %i -----\n", (int)( ia - backEnd.viewParms.interactions ) ) );
								}

								// we don't need tangent space calculations here
								Tess_Begin( Tess_StageIteratorShadowFill, shader, light->shader, true, -1, 0 );
							}

							break;
						}

					default:
						break;
				}

				// change the modelview matrix if needed
				if ( entity != oldEntity )
				{
					depthRange = false;

					if ( entity != &tr.worldEntity )
					{
						// set up the transformation matrix
						R_RotateEntityForLight( entity, light, &backEnd.orientation );

						if ( entity->e.renderfx & RF_DEPTHHACK )
						{
							// hack the depth range to prevent view model from poking into walls
							depthRange = true;
						}
					}
					else
					{
						// set up the transformation matrix
						backEnd.orientation = {};

						backEnd.orientation.axis[ 0 ][ 0 ] = 1;
						backEnd.orientation.axis[ 1 ][ 1 ] = 1;
						backEnd.orientation.axis[ 2 ][ 2 ] = 1;
						VectorCopy( light->l.origin, backEnd.orientation.viewOrigin );

						MatrixIdentity( backEnd.orientation.transformMatrix );
						MatrixMultiply( light->viewMatrix, backEnd.orientation.transformMatrix, backEnd.orientation.viewMatrix );
						MatrixCopy( backEnd.orientation.viewMatrix, backEnd.orientation.modelViewMatrix );
					}

					GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );

					// change depthrange if needed
					if ( oldDepthRange != depthRange )
					{
						if ( depthRange )
						{
							glDepthRange( 0, 0.3 );
						}
						else
						{
							glDepthRange( 0, 1 );
						}

						oldDepthRange = depthRange;
					}

					RB_SetupLightAttenuationForEntity( light, entity );
				}

				switch ( light->l.rlType )
				{
					case refLightType_t::RL_OMNI:
					case refLightType_t::RL_PROJ:
					case refLightType_t::RL_DIRECTIONAL:
						{
							// add the triangles for this surface
							rb_surfaceTable[ Util::ordinal(*surface) ]( surface );
							break;
						}

					default:
						break;
				}
				oldEntity = entity;
				oldShader = shader;
				oldAlphaTest = alphaTest;
			}

			if ( r_logFile->integer )
			{
				// don't just call LogComment, or we will get
				// a call to va() every frame!
				GLimp_LogComment( va( "----- Last Interaction: %i -----\n", (int)( iaLast - backEnd.viewParms.interactions ) ) );
			}

			Tess_End();

			if( shadowClipFound )
			{
				entity = nullptr;
				shader = nullptr;
				oldEntity = nullptr;
				oldShader = nullptr;

				if ( light->l.noShadows || light->shadowLOD < 0 )
				{
					if ( r_logFile->integer )
					{
						// don't just call LogComment, or we will get
						// a call to va() every frame!
						GLimp_LogComment( va( "----- Skipping shadowCube side: %i -----\n", i ) );
					}
					continue;
				}

				RB_SetupLightForShadowing( light, i, true );

				for( ia = iaFirst; ia; ia = ia->next )
				{
					iaLast = ia;
					backEnd.currentEntity = entity = ia->entity;
					surface = ia->surface;
					shader = ia->shader;
					alphaTest = shader->alphaTest;

					if ( entity->e.renderfx & ( RF_NOSHADOW | RF_DEPTHHACK ) )
					{
						continue;
					}

					if ( shader->isSky )
					{
						continue;
					}

					if ( shader->sort > Util::ordinal(shaderSort_t::SS_OPAQUE) )
					{
						continue;
					}

					if ( shader->noShadows )
					{
						continue;
					}

					if ( !(ia->type & IA_SHADOWCLIP) )
					{
						continue;
					}

					if ( light->l.rlType == refLightType_t::RL_OMNI && !( ia->cubeSideBits & ( 1 << i ) ) )
					{
						continue;
					}

					switch ( light->l.rlType )
					{
						case refLightType_t::RL_OMNI:
						case refLightType_t::RL_PROJ:
						case refLightType_t::RL_DIRECTIONAL:
							{
								if ( entity == oldEntity && ( alphaTest ? shader == oldShader : alphaTest == oldAlphaTest ) )
								{
									if ( r_logFile->integer )
									{
										// don't just call LogComment, or we will get
										// a call to va() every frame!
										GLimp_LogComment( va( "----- Batching Shadow Interaction: %i -----\n", (int)( ia - backEnd.viewParms.interactions ) ) );
									}

									// fast path, same as previous
									rb_surfaceTable[ Util::ordinal(*surface) ]( surface );
									continue;
								}
								else
								{
									// draw the contents of the last shader batch
									Tess_End();

									if ( r_logFile->integer )
									{
										// don't just call LogComment, or we will get
										// a call to va() every frame!
										GLimp_LogComment( va( "----- Beginning Shadow Interaction: %i -----\n", (int)( ia - backEnd.viewParms.interactions ) ) );
									}

									// we don't need tangent space calculations here
									Tess_Begin( Tess_StageIteratorShadowFill, shader, light->shader, true, -1, 0 );
								}

								break;
							}

						default:
							break;
					}

					// change the modelview matrix if needed
					if ( entity != oldEntity )
					{
						depthRange = false;

						if ( entity != &tr.worldEntity )
						{
							// set up the transformation matrix
							R_RotateEntityForLight( entity, light, &backEnd.orientation );

							if ( entity->e.renderfx & RF_DEPTHHACK )
							{
								// hack the depth range to prevent view model from poking into walls
								depthRange = true;
							}
						}
						else
						{
							// set up the transformation matrix
							backEnd.orientation = {};

							backEnd.orientation.axis[ 0 ][ 0 ] = 1;
							backEnd.orientation.axis[ 1 ][ 1 ] = 1;
							backEnd.orientation.axis[ 2 ][ 2 ] = 1;
							VectorCopy( light->l.origin, backEnd.orientation.viewOrigin );

							MatrixIdentity( backEnd.orientation.transformMatrix );
							MatrixMultiply( light->viewMatrix, backEnd.orientation.transformMatrix, backEnd.orientation.viewMatrix );
							MatrixCopy( backEnd.orientation.viewMatrix, backEnd.orientation.modelViewMatrix );
						}

						GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );

						// change depthrange if needed
						if ( oldDepthRange != depthRange )
						{
							if ( depthRange )
							{
								glDepthRange( 0, 0.3 );
							}
							else
							{
								glDepthRange( 0, 1 );
							}

							oldDepthRange = depthRange;
						}

						RB_SetupLightAttenuationForEntity( light, entity );
					}

					switch ( light->l.rlType )
					{
						case refLightType_t::RL_OMNI:
						case refLightType_t::RL_PROJ:
						case refLightType_t::RL_DIRECTIONAL:
							{
								// add the triangles for this surface
								rb_surfaceTable[ Util::ordinal(*surface) ]( surface );
								break;
							}

						default:
							break;
					}
					oldEntity = entity;
					oldShader = shader;
					oldAlphaTest = alphaTest;
				}

				if ( r_logFile->integer )
				{
					// don't just call LogComment, or we will get
					// a call to va() every frame!
					GLimp_LogComment( va( "----- Last Interaction: %i -----\n", (int)( iaLast - backEnd.viewParms.interactions ) ) );
				}

				Tess_End();
			}

			// set shadow matrix including scale + offset
			if ( light->l.rlType == refLightType_t::RL_DIRECTIONAL )
			{
				MatrixCopy( bias, light->shadowMatricesBiased[ i ] );
				MatrixMultiply2( light->shadowMatricesBiased[ i ], light->projectionMatrix );
				MatrixMultiply2( light->shadowMatricesBiased[ i ], light->viewMatrix );

				MatrixMultiply( light->projectionMatrix, light->viewMatrix, light->shadowMatrices[ i ] );
			}

			RB_BlurShadowMap( light, i );
		}

		// begin lighting
		RB_SetupLightForLighting( light );
		entity = nullptr;
		shader = nullptr;
		oldEntity = nullptr;
		oldShader = nullptr;
		for ( ia = iaFirst; ia; ia = ia->next )
		{
			iaLast = ia;
			backEnd.currentEntity = entity = ia->entity;
			surface = ia->surface;
			shader = ia->shader;
			alphaTest = shader->alphaTest;

			if ( !shader->interactLight )
			{
				continue;
			}

			if ( !(ia->type & IA_LIGHT) )
			{
				continue;
			}

			if ( entity == oldEntity && shader == oldShader )
			{
				if ( r_logFile->integer )
				{
					// don't just call LogComment, or we will get
					// a call to va() every frame!
					GLimp_LogComment( va( "----- Batching Light Interaction: %i -----\n", (int)( ia - backEnd.viewParms.interactions ) ) );
				}

				// fast path, same as previous
				rb_surfaceTable[ Util::ordinal(*surface) ]( surface );
				continue;
			}
			else
			{
				// draw the contents of the last shader batch
				Tess_End();

				if ( r_logFile->integer )
				{
					// don't just call LogComment, or we will get
					// a call to va() every frame!
					GLimp_LogComment( va( "----- Beginning Light Interaction: %i -----\n", (int)( ia - backEnd.viewParms.interactions ) ) );
				}

				// begin a new batch
				Tess_Begin( Tess_StageIteratorLighting, shader, light->shader, light->l.inverseShadows, -1, 0 );
			}

			// change the modelview matrix if needed
			if ( entity != oldEntity )
			{
				depthRange = false;

				if ( entity != &tr.worldEntity )
				{
					// set up the transformation matrix
					R_RotateEntityForViewParms( entity, &backEnd.viewParms, &backEnd.orientation );

					if ( entity->e.renderfx & RF_DEPTHHACK )
					{
						// hack the depth range to prevent view model from poking into walls
						depthRange = true;
					}
				}
				else
				{
					// set up the transformation matrix
					// transform by the camera placement
					backEnd.orientation = backEnd.viewParms.world;
				}

				GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );

				// change depthrange if needed
				if ( oldDepthRange != depthRange )
				{
					if ( depthRange )
					{
						glDepthRange( 0, 0.3 );
					}
					else
					{
						glDepthRange( 0, 1 );
					}

					oldDepthRange = depthRange;
				}

				RB_SetupLightAttenuationForEntity( light, entity );
			}

			// add the triangles for this surface
			rb_surfaceTable[ Util::ordinal(*surface) ]( surface );
			oldEntity = entity;
			oldShader = shader;
			oldAlphaTest = alphaTest;
		}

		if ( r_logFile->integer )
		{
			// don't just call LogComment, or we will get
			// a call to va() every frame!
			GLimp_LogComment( va( "----- Last Interaction: %i -----\n", (int)( iaLast - backEnd.viewParms.interactions ) ) );
		}

		Tess_End();
	}

	// draw the contents of the last shader batch
	Tess_End();

	// go back to the world modelview matrix
	GL_LoadModelViewMatrix( backEnd.viewParms.world.modelViewMatrix );

	if ( depthRange )
	{
		glDepthRange( 0, 1 );
	}

	// reset scissor clamping
	GL_Scissor( backEnd.viewParms.scissorX, backEnd.viewParms.scissorY,
	            backEnd.viewParms.scissorWidth, backEnd.viewParms.scissorHeight );

	// reset clear color
	GL_ClearColor( 0.0f, 0.0f, 0.0f, 1.0f );

	GL_CheckErrors();

	if ( r_speeds->integer == Util::ordinal(renderSpeeds_t::RSPEEDS_SHADING_TIMES) )
	{
		glFinish();
		endTime = ri.Milliseconds();
		backEnd.pc.c_forwardLightingTime += endTime - startTime;
	}
}

/*
=============
RB_RunVisTests
=============
*/
void RB_RunVisTests( )
{
	int i;

	// finish any 2D drawing if needed
	Tess_End();

	for ( i = 0; i < backEnd.refdef.numVisTests; i++ )
	{
		vec3_t           diff;
		vec3_t           center, left, up;
		visTestResult_t  *test = &backEnd.refdef.visTests[ i ];
		visTestQueries_t *testState = &backEnd.visTestQueries[ test->visTestHandle - 1 ];

		if ( testState->running && !test->discardExisting )
		{
			GLint  available;
			GLuint result, resultRef;

			glGetQueryObjectiv( testState->hQuery,
					    GL_QUERY_RESULT_AVAILABLE,
					    &available );
			if( !available )
			{
				continue;
			}

			glGetQueryObjectiv( testState->hQueryRef,
					    GL_QUERY_RESULT_AVAILABLE,
					    &available );
			if ( !available )
			{
				continue;
			}

			glGetQueryObjectuiv( testState->hQueryRef, GL_QUERY_RESULT,
					     &resultRef );
			glGetQueryObjectuiv( testState->hQuery, GL_QUERY_RESULT,
					     &result );

			if ( resultRef > 0 )
			{
				test->lastResult = (float)result / (float)resultRef;
			}
			else
			{
				test->lastResult = 0.0f;
			}

			testState->running = false;
		}

		Tess_MapVBOs( false );
		VectorSubtract( backEnd.orientation.viewOrigin,
				test->position, diff );
		VectorNormalize( diff );
		VectorMA( test->position, test->depthAdjust, diff, center );

		VectorScale( backEnd.viewParms.orientation.axis[ 1 ],
			     test->area, left );
		VectorScale( backEnd.viewParms.orientation.axis[ 2 ],
			     test->area, up );

		tess.verts[ 0 ].xyz[ 0 ] = center[ 0 ] + left[ 0 ] + up[ 0 ];
		tess.verts[ 0 ].xyz[ 1 ] = center[ 1 ] + left[ 1 ] + up[ 1 ];
		tess.verts[ 0 ].xyz[ 2 ] = center[ 2 ] + left[ 2 ] + up[ 2 ];
		tess.verts[ 1 ].xyz[ 0 ] = center[ 0 ] - left[ 0 ] + up[ 0 ];
		tess.verts[ 1 ].xyz[ 1 ] = center[ 1 ] - left[ 1 ] + up[ 1 ];
		tess.verts[ 1 ].xyz[ 2 ] = center[ 2 ] - left[ 2 ] + up[ 2 ];
		tess.verts[ 2 ].xyz[ 0 ] = center[ 0 ] - left[ 0 ] - up[ 0 ];
		tess.verts[ 2 ].xyz[ 1 ] = center[ 1 ] - left[ 1 ] - up[ 1 ];
		tess.verts[ 2 ].xyz[ 2 ] = center[ 2 ] - left[ 2 ] - up[ 2 ];
		tess.verts[ 3 ].xyz[ 0 ] = center[ 0 ] + left[ 0 ] - up[ 0 ];
		tess.verts[ 3 ].xyz[ 1 ] = center[ 1 ] + left[ 1 ] - up[ 1 ];
		tess.verts[ 3 ].xyz[ 2 ] = center[ 2 ] + left[ 2 ] - up[ 2 ];
		tess.numVertexes = 4;

		tess.indexes[ 0 ] = 0;
		tess.indexes[ 1 ] = 1;
		tess.indexes[ 2 ] = 2;
		tess.indexes[ 3 ] = 0;
		tess.indexes[ 4 ] = 2;
		tess.indexes[ 5 ] = 3;
		tess.numIndexes = 6;

		Tess_UpdateVBOs( );
		GL_VertexAttribsState( ATTR_POSITION );

		gl_genericShader->SetVertexSkinning( false );
		gl_genericShader->SetVertexAnimation( false );
		gl_genericShader->SetTCGenEnvironment( false );
		gl_genericShader->SetTCGenLightmap( false );
		gl_genericShader->SetDepthFade( false );
		gl_genericShader->BindProgram( 0 );

		gl_genericShader->SetUniform_AlphaTest( GLS_ATEST_NONE );
		gl_genericShader->SetUniform_Color( Color::White );

		gl_genericShader->SetUniform_ColorModulateColorGen( colorGen_t::CGEN_CONST, alphaGen_t::AGEN_CONST );
		gl_genericShader->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
		gl_genericShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

		// bind u_ColorMap
		gl_genericShader->SetUniform_ColorMapBindless(
			GL_BindToTMU( 0, tr.whiteImage )
		);
		gl_genericShader->SetUniform_TextureMatrix( tess.svars.texMatrices[ TB_COLORMAP ] );

		GL_State( GLS_DEPTHTEST_DISABLE | GLS_COLORMASK_BITS );
		glBeginQuery( GL_SAMPLES_PASSED, testState->hQueryRef );
		Tess_DrawElements();
		glEndQuery( GL_SAMPLES_PASSED );

		GL_State( GLS_COLORMASK_BITS );
		glBeginQuery( GL_SAMPLES_PASSED, testState->hQuery );
		Tess_DrawElements();
		glEndQuery( GL_SAMPLES_PASSED );

		Tess_Clear();
		testState->running = true;
	}
}

void RB_RenderPostDepthLightTile()
{
	if ( !glConfig2.realtimeLighting )
	{
		return;
	}

	if ( r_realtimeLightingRenderer.Get() != Util::ordinal( realtimeLightingRenderer_t::TILED ) )
	{
		/* Do not run lightTile code when the tiled renderer is not used.

		This computation is part of the tiled dynamic lighting renderer,
		it's better to not run it and save CPU cycles when such effects
		are disabled.

		Disabling this code also make possible to not compile the related
		GLSL shaders at all when such effects are disabled.

		Not running the related GLSL shaders also helps older hardware to
		run the game, for example the Radeon R300 Arithmetic Logic Unit is
		too small to run the related GLSL code even if the shader itself
		can be compiled. Such GPU are so old and slow that	any kind of
		dynamic lighting including the tiled implementation is expected to
		be disabled anyway. Saving CPU cycles when a feature is not used is
		welcome in any case.

		See https://github.com/DaemonEngine/Daemon/issues/344 */

		return;
	}

	if ( !backEnd.refdef.numLights ) {
		return;
	}

	vec3_t zParams;
	int w, h;

	GLimp_LogComment( "--- RB_RenderPostDepthLightTile ---\n" );

	if ( ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) )
	{
		return;
	}

	// 1st step
	GL_State( GLS_DEPTHTEST_DISABLE );
	GL_Cull( CT_TWO_SIDED );

	R_BindFBO( tr.depthtile1FBO );
	w = (glConfig.vidWidth + TILE_SIZE_STEP1 - 1) >> TILE_SHIFT_STEP1;
	h = (glConfig.vidHeight + TILE_SIZE_STEP1 - 1) >> TILE_SHIFT_STEP1;
	GL_Viewport( 0, 0, w, h );
	GL_Scissor( 0, 0, w, h );
	gl_depthtile1Shader->BindProgram( 0 );

	zParams[ 0 ] = 2.0f * tanf( DEG2RAD( backEnd.refdef.fov_x * 0.5f) ) / glConfig.vidWidth;
	zParams[ 1 ] = 2.0f * tanf( DEG2RAD( backEnd.refdef.fov_y * 0.5f) ) / glConfig.vidHeight;
	zParams[ 2 ] = backEnd.viewParms.zFar;

	gl_depthtile1Shader->SetUniform_zFar( zParams );
	gl_depthtile1Shader->SetUniform_DepthMapBindless(
		GL_BindToTMU( 0, tr.currentDepthImage ) 
	);

	matrix_t ortho;
	GL_PushMatrix();
	MatrixOrthogonalProjection( ortho, backEnd.viewParms.viewportX,
		backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth,
		backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight, -99999, 99999 );
	GL_LoadProjectionMatrix( ortho );

	Tess_InstantQuad( *gl_depthtile1Shader,
	                  backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
	                  backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );

	// 2nd step
	R_BindFBO( tr.depthtile2FBO );

	w = (glConfig.vidWidth + TILE_SIZE - 1) >> TILE_SHIFT;
	h = (glConfig.vidHeight + TILE_SIZE - 1) >> TILE_SHIFT;
	GL_Viewport( 0, 0, w, h );
	GL_Scissor( 0, 0, w, h );
	gl_depthtile2Shader->BindProgram( 0 );

	gl_depthtile2Shader->SetUniform_DepthMapBindless(
		GL_BindToTMU( 0, tr.depthtile1RenderImage )
	);

	Tess_InstantQuad( *gl_depthtile2Shader,
	                  backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
	                  backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );

	GL_PopMatrix();

	vec3_t projToViewParams;
	projToViewParams[0] = tanf(DEG2RAD(backEnd.refdef.fov_x * 0.5f)) * backEnd.viewParms.zFar;
	projToViewParams[1] = tanf(DEG2RAD(backEnd.refdef.fov_y * 0.5f)) * backEnd.viewParms.zFar;
	projToViewParams[2] = backEnd.viewParms.zFar;

	// render lights
	R_BindFBO( tr.lighttileFBO );
	gl_lighttileShader->BindProgram( 0 );
	gl_lighttileShader->SetUniform_ModelMatrix( backEnd.viewParms.world.modelViewMatrix );
	gl_lighttileShader->SetUniform_numLights( backEnd.refdef.numLights );
	gl_lighttileShader->SetUniform_zFar( projToViewParams );

	gl_lighttileShader->SetUniformBlock_Lights( tr.dlightUBO );

	gl_lighttileShader->SetUniform_DepthMapBindless(
		GL_BindToTMU( 1, tr.depthtile2RenderImage ) 
	);

	R_BindVBO( tr.lighttileVBO );

	for( int layer = 0; layer < glConfig2.realtimeLightLayers; layer++ ) {
		R_AttachFBOTexture3D( tr.lighttileRenderImage->texnum, 0, layer );
		gl_lighttileShader->SetUniform_lightLayer( layer );

		GL_Viewport( 0, 0, tr.lighttileRenderImage->width, tr.lighttileRenderImage->height );
		tess.numIndexes = 0;
		tess.numVertexes = tr.lighttileVBO->vertexesNum;

		GL_VertexAttribsState( ATTR_POSITION | ATTR_TEXCOORD );
		if( !glConfig2.glCoreProfile )
			glEnable( GL_POINT_SPRITE );
		glEnable( GL_PROGRAM_POINT_SIZE );

		// Radeon R300 small ALU is known to fail on this.
		Tess_DrawArrays( GL_POINTS );

		glDisable( GL_PROGRAM_POINT_SIZE );
		if( !glConfig2.glCoreProfile )
			glDisable( GL_POINT_SPRITE );
	}

	Tess_Clear();

	// back to main image
	R_BindFBO( tr.mainFBO[ backEnd.currentMainFBO ] );
	GL_Viewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		     backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );
	GL_Scissor( backEnd.viewParms.scissorX, backEnd.viewParms.scissorY,
		    backEnd.viewParms.scissorWidth, backEnd.viewParms.scissorHeight );

	GL_CheckErrors();
}

void RB_RenderGlobalFog()
{
	vec3_t   local;
	vec4_t   fogDistanceVector;
	matrix_t ortho;

	GLimp_LogComment( "--- RB_RenderGlobalFog ---\n" );

	if ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL )
	{
		return;
	}

	if ( r_noFog->integer )
	{
		return;
	}

	if ( !tr.world || tr.world->globalFog < 0 )
	{
		return;
	}

	GL_Cull( cullType_t::CT_TWO_SIDED );

	gl_fogGlobalShader->BindProgram( 0 );

	// go back to the world modelview matrix
	backEnd.orientation = backEnd.viewParms.world;

	{
		fog_t *fog;

		fog = &tr.world->fogs[ tr.world->globalFog ];

		if ( r_logFile->integer )
		{
			GLimp_LogComment( va( "--- RB_RenderGlobalFog( fogNum = %i, originalBrushNumber = %i ) ---\n", tr.world->globalFog, fog->originalBrushNumber ) );
		}

		GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );

		// all fogging distance is based on world Z units
		VectorSubtract( backEnd.orientation.origin, backEnd.viewParms.orientation.origin, local );
		fogDistanceVector[ 0 ] = -backEnd.orientation.modelViewMatrix[ 2 ];
		fogDistanceVector[ 1 ] = -backEnd.orientation.modelViewMatrix[ 6 ];
		fogDistanceVector[ 2 ] = -backEnd.orientation.modelViewMatrix[ 10 ];
		fogDistanceVector[ 3 ] = DotProduct( local, backEnd.viewParms.orientation.axis[ 0 ] );

		// scale the fog vectors based on the fog's thickness
		fogDistanceVector[ 0 ] *= fog->tcScale;
		fogDistanceVector[ 1 ] *= fog->tcScale;
		fogDistanceVector[ 2 ] *= fog->tcScale;
		fogDistanceVector[ 3 ] *= fog->tcScale;

		gl_fogGlobalShader->SetUniform_FogDistanceVector( fogDistanceVector );
		gl_fogGlobalShader->SetUniform_Color( fog->color );
	}

	gl_fogGlobalShader->SetUniform_UnprojectMatrix( backEnd.viewParms.unprojectionMatrix );

	// bind u_ColorMap
	gl_fogGlobalShader->SetUniform_ColorMapBindless(
		GL_BindToTMU( 0, tr.fogImage ) 
	);

	// bind u_DepthMap
	gl_fogGlobalShader->SetUniform_DepthMapBindless(
		GL_BindToTMU( 1, tr.currentDepthImage )
	);

	// set 2D virtual screen size
	GL_PushMatrix();
	MatrixOrthogonalProjection( ortho, backEnd.viewParms.viewportX,
	                            backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth,
	                            backEnd.viewParms.viewportY, backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight,
	                            -99999, 99999 );
	GL_LoadProjectionMatrix( ortho );

	// draw viewport
	Tess_InstantQuad( *gl_fogGlobalShader,
	                  backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
	                  backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );

	// go back to 3D
	GL_PopMatrix();

	GL_CheckErrors();
}

void RB_RenderBloom()
{
	GLimp_LogComment( "--- RB_RenderBloom ---\n" );

	if ( ( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_NOBLOOM ) )
		|| !glConfig2.bloom || backEnd.viewParms.portalLevel > 0 ) {
		return;
	}

	// set 2D virtual screen size
	GL_PushMatrix();
	matrix_t ortho;
	MatrixOrthogonalProjection( ortho, backEnd.viewParms.viewportX,
	                            backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth,
	                            backEnd.viewParms.viewportY, backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight,
	                            -99999, 99999 );
	GL_LoadProjectionMatrix( ortho );

	{
		GL_State( GLS_DEPTHTEST_DISABLE );
		GL_Cull( cullType_t::CT_TWO_SIDED );

		// render contrast downscaled to 1/4th of the screen
		gl_contrastShader->BindProgram( 0 );

		gl_contrastShader->SetUniform_ColorMapBindless(
			GL_BindToTMU( 0, tr.currentRenderImage[backEnd.currentMainFBO] )
		);

		R_BindFBO( tr.contrastRenderFBO );
		GL_ClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT );

		// draw viewport
		Tess_InstantQuad( *gl_contrastShader,
		                  backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		                  backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );

		// render bloom in multiple passes
		GL_ClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
		GL_State( GLS_DEPTHTEST_DISABLE );

		GL_PushMatrix();

		MatrixOrthogonalProjection( ortho, 0, tr.bloomRenderFBO[0]->width, 0, tr.bloomRenderFBO[0]->height, -99999, 99999 );
		GL_LoadProjectionMatrix( ortho );

		vec2_t texScale;
		texScale[0] = 1.0f / tr.bloomRenderFBO[0]->width;
		texScale[1] = 1.0f / tr.bloomRenderFBO[0]->height;

		gl_blurShader->BindProgram( 0 );

		gl_blurShader->SetUniform_DeformMagnitude( r_bloomBlur.Get() );
		gl_blurShader->SetUniform_TexScale( texScale );

		gl_blurShader->SetUniform_ColorMapBindless(
			GL_BindToTMU( 0, tr.contrastRenderFBOImage )
		);

		gl_blurShader->SetUniform_Horizontal( true );

		int flip = 0;
		for ( int i = 0; i < 2; i++ ) {
			for ( int j = 0; j < r_bloomPasses.Get(); j++ ) {
				R_BindFBO( tr.bloomRenderFBO[flip] );
				glClear( GL_COLOR_BUFFER_BIT );
				Tess_InstantQuad( *gl_blurShader,
					backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
					backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );

				gl_blurShader->SetUniform_ColorMapBindless(
					GL_BindToTMU( 0, tr.bloomRenderFBOImage[flip] )
				);

				flip ^= 1;
			}

			gl_blurShader->SetUniform_Horizontal( false );
		}

		GL_PopMatrix();

		R_BindFBO( tr.mainFBO[backEnd.currentMainFBO] );

		gl_screenShader->BindProgram( 0 );
		GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
		glVertexAttrib4fv( ATTR_INDEX_COLOR, Color::White.ToArray() );

		gl_screenShader->SetUniform_CurrentMapBindless( GL_BindToTMU( 0, tr.bloomRenderFBOImage[flip ^ 1] ) );
		Tess_InstantQuad( *gl_screenShader,
		                  backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
		                  backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );
	}

	// go back to 3D
	GL_PopMatrix();

	GL_CheckErrors();
}

void RB_RenderMotionBlur()
{

	GLimp_LogComment( "--- RB_RenderMotionBlur ---\n" );

	if ( !glConfig2.motionBlur || ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL )
		|| backEnd.viewParms.portalLevel > 0 )
	{
		return;
	}

	GL_State( GLS_DEPTHTEST_DISABLE );
	GL_Cull( cullType_t::CT_TWO_SIDED );

	gl_motionblurShader->BindProgram( 0 );

	// Swap main FBOs
	gl_motionblurShader->SetUniform_ColorMapBindless(
		GL_BindToTMU( 0, tr.currentRenderImage[backEnd.currentMainFBO] )
	);
	backEnd.currentMainFBO = 1 - backEnd.currentMainFBO;
	R_BindFBO( tr.mainFBO[ backEnd.currentMainFBO ] );

	gl_motionblurShader->SetUniform_blurVec(tr.refdef.blurVec);

	gl_motionblurShader->SetUniform_DepthMapBindless(
		GL_BindToTMU( 1, tr.currentDepthImage )
	);

	matrix_t ortho;
	GL_PushMatrix();
	MatrixOrthogonalProjection( ortho, backEnd.viewParms.viewportX,
		backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth,
		backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight, -99999, 99999 );
	GL_LoadProjectionMatrix( ortho );

	// draw quad
	Tess_InstantQuad( *gl_motionblurShader,
	                   backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
	                   backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );

	GL_PopMatrix();

	GL_CheckErrors();
}

void RB_RenderSSAO()
{
	GLimp_LogComment( "--- RB_RenderSSAO ---\n" );

	if ( !glConfig2.textureGatherAvailable ) {
		return;
	}

	if ( ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ||
	     backEnd.viewParms.portalLevel > 0 )
	{
		return;
	}

	GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO );
	GL_Cull( cullType_t::CT_TWO_SIDED );

	if ( r_ssao->integer < 0 ) {
		// clear the screen to show only SSAO
		GL_ClearColor( 1.0, 1.0, 1.0, 1.0 );
		glClear( GL_COLOR_BUFFER_BIT );
	}

	gl_ssaoShader->BindProgram( 0 );

	vec3_t zParams;
	zParams[ 0 ] = 2.0f * tanf( DEG2RAD( backEnd.refdef.fov_x * 0.5f ) ) / glConfig.vidWidth;
	zParams[ 1 ] = 2.0f * tanf( DEG2RAD( backEnd.refdef.fov_y * 0.5f ) ) / glConfig.vidHeight;
	zParams[ 2 ] = backEnd.viewParms.zFar;

	gl_ssaoShader->SetUniform_zFar( zParams );

	vec3_t unprojectionParams;
	unprojectionParams[ 0 ] = -r_znear->value * zParams[ 2 ];
	unprojectionParams[ 1 ] = 2.0 * ( zParams[ 2 ] - r_znear->value );
	unprojectionParams[ 2 ] = 2.0 * zParams[ 2 ] - r_znear->value;

	gl_ssaoShader->SetUniform_UnprojectionParams( unprojectionParams );

	gl_ssaoShader->SetUniform_DepthMapBindless(
		GL_BindToTMU( 0, tr.currentDepthImage )
	);

	matrix_t ortho;
	GL_PushMatrix();
	MatrixOrthogonalProjection( ortho, backEnd.viewParms.viewportX,
		backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth,
		backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight, -99999, 99999 );
	GL_LoadProjectionMatrix( ortho );

	// draw quad
	Tess_InstantQuad( *gl_ssaoShader,
	                  backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
	                  backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );

	GL_PopMatrix();

	GL_CheckErrors();
}

void RB_FXAA()
{

	GLimp_LogComment( "--- RB_FXAA ---\n" );

	if ( ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ||
	     backEnd.viewParms.portalLevel )
	{
		return;
	}

	if ( !r_FXAA->integer || !gl_fxaaShader )
	{
		return;
	}

	GL_State( GLS_DEPTHTEST_DISABLE );
	GL_Cull( cullType_t::CT_TWO_SIDED );

	// set the shader parameters
	gl_fxaaShader->BindProgram( 0 );

	// Swap main FBOs
	gl_fxaaShader->SetUniform_ColorMapBindless(
		GL_BindToTMU( 0, tr.currentRenderImage[backEnd.currentMainFBO] )
	);
	backEnd.currentMainFBO = 1 - backEnd.currentMainFBO;
	R_BindFBO( tr.mainFBO[ backEnd.currentMainFBO ] );

	matrix_t ortho;
	GL_PushMatrix();
	MatrixOrthogonalProjection( ortho, backEnd.viewParms.viewportX,
		backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth,
		backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight, -99999, 99999 );
	GL_LoadProjectionMatrix( ortho );

	Tess_InstantQuad( *gl_fxaaShader,
	                  backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
	                  backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );

	GL_PopMatrix();

	GL_CheckErrors();
}

static void ComputeTonemapParams( const float contrast, const float highlightsCompressionSpeed,
	const float HDRMax,
	const float darkAreaPointHDR, const float darkAreaPointLDR,
	float& shoulderClip, float& highlightsCompression ) {
	// Lottes 2016, "Advanced Techniques and Optimization of HDR Color Pipelines"
	/* a: contrast
	d: highlightsCompressionSpeed
	b: shoulderClip
	c: highlightsCompression
	hdrMax: HDRMax
	midIn: darkAreaPointHDR
	midOut: darkAreaPointLDR */

	shoulderClip =
		( -powf( darkAreaPointHDR, contrast ) + powf( HDRMax, contrast ) * darkAreaPointLDR )
		/
		( ( powf( HDRMax, contrast * highlightsCompressionSpeed )
			- powf( darkAreaPointHDR, contrast * highlightsCompressionSpeed )
		) * darkAreaPointLDR );
	highlightsCompression =
		( powf( HDRMax, contrast * highlightsCompressionSpeed ) * powf( darkAreaPointHDR, contrast )
			- powf( HDRMax, contrast ) * powf( darkAreaPointHDR, contrast * highlightsCompressionSpeed ) * darkAreaPointLDR
		)
		/
		( ( powf( HDRMax, contrast * highlightsCompressionSpeed )
			- powf( darkAreaPointHDR, contrast * highlightsCompressionSpeed )
		) * darkAreaPointLDR );
}

void RB_CameraPostFX()
{
	matrix_t ortho;

	GLimp_LogComment( "--- RB_CameraPostFX ---\n" );

	if ( ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) ||
	     backEnd.viewParms.portalLevel > 0 )
	{
		return;
	}

	// set 2D virtual screen size
	GL_PushMatrix();
	MatrixOrthogonalProjection( ortho, backEnd.viewParms.viewportX,
	                            backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth,
	                            backEnd.viewParms.viewportY, backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight,
	                            -99999, 99999 );
	GL_LoadProjectionMatrix( ortho );

	GL_State( GLS_DEPTHTEST_DISABLE );
	GL_Cull( cullType_t::CT_TWO_SIDED );

	// enable shader, set arrays
	gl_cameraEffectsShader->BindProgram( 0 );

	gl_cameraEffectsShader->SetUniform_ColorModulate( backEnd.viewParms.gradingWeights );

	gl_cameraEffectsShader->SetUniform_InverseGamma( 1.0 / r_gamma->value );

	const bool tonemap = r_tonemap.Get() && r_highPrecisionRendering.Get() && glConfig2.textureFloatAvailable;
	if ( tonemap ) {
		vec4_t tonemapParms { r_tonemapContrast.Get(), r_tonemapHighlightsCompressionSpeed.Get() };
		ComputeTonemapParams( tonemapParms[0], tonemapParms[1], r_tonemapHDRMax.Get(),
			r_tonemapDarkAreaPointHDR.Get(), r_tonemapDarkAreaPointLDR.Get(), tonemapParms[2], tonemapParms[3] );
		gl_cameraEffectsShader->SetUniform_TonemapParms( tonemapParms );
		gl_cameraEffectsShader->SetUniform_TonemapExposure( r_tonemapExposure.Get() );
	}
	gl_cameraEffectsShader->SetUniform_Tonemap( tonemap );

	// This shader is run last, so let it render to screen instead of
	// tr.mainFBO
	R_BindNullFBO();
	gl_cameraEffectsShader->SetUniform_CurrentMapBindless(
		GL_BindToTMU( 0, tr.currentRenderImage[backEnd.currentMainFBO] ) 
	);

	if ( glConfig2.colorGrading )
	{
		gl_cameraEffectsShader->SetUniform_ColorMap3DBindless( GL_BindToTMU( 3, tr.colorGradeImage ) );
	}

	// draw viewport
	Tess_InstantQuad( *gl_cameraEffectsShader,
	                  backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
	                  backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );

	// go back to 3D
	GL_PopMatrix();

	GL_CheckErrors();
}

static void RB_RenderDebugUtils()
{
	GLimp_LogComment( "--- RB_RenderDebugUtils ---\n" );

	if ( r_showLightTransforms->integer || r_showShadowLod->integer )
	{
		const interaction_t *ia;
		trRefLight_t  *light;
		vec3_t        forward, left, up;

		static const vec3_t minSize = { -2, -2, -2 };
		static const vec3_t maxSize = { 2,  2,  2 };

		gl_genericShader->SetVertexSkinning( false );
		gl_genericShader->SetVertexAnimation( false );
		gl_genericShader->SetTCGenEnvironment( false );
		gl_genericShader->SetTCGenLightmap( false );
		gl_genericShader->SetDepthFade( false );
		gl_genericShader->BindProgram( 0 );

		GL_State( GLS_POLYMODE_LINE | GLS_DEPTHTEST_DISABLE );
		GL_Cull( cullType_t::CT_TWO_SIDED );

		// set uniforms
		gl_genericShader->SetUniform_AlphaTest( GLS_ATEST_NONE );
		gl_genericShader->SetUniform_ColorModulateColorGen( colorGen_t::CGEN_CUSTOM_RGB, alphaGen_t::AGEN_CUSTOM );

		// bind u_ColorMap
		gl_genericShader->SetUniform_ColorMapBindless(
			GL_BindToTMU( 0, tr.whiteImage )
		);
		gl_genericShader->SetUniform_TextureMatrix( matrixIdentity );

		ia = nullptr;
		Color::Color lightColor;
		while ( ( ia = IterateLights( ia ) ) )
		{
			backEnd.currentLight = light = ia->light;

			if ( r_showShadowLod->integer )
			{
				if ( light->shadowLOD == 0 )
				{
					lightColor = Color::Red;
				}
				else if ( light->shadowLOD == 1 )
				{
					lightColor = Color::Green;
				}
				else if ( light->shadowLOD == 2 )
				{
					lightColor = Color::Blue;
				}
				else if ( light->shadowLOD == 3 )
				{
					lightColor = Color::Yellow;
				}
				else if ( light->shadowLOD == 4 )
				{
					lightColor = Color::Magenta;
				}
				else if ( light->shadowLOD == 5 )
				{
					lightColor = Color::Cyan;
				}
				else
				{
					lightColor = Color::MdGrey;
				}
			}
			else
			{
				lightColor = Color::Blue;
			}

			lightColor.SetAlpha( 0.2 );

			gl_genericShader->SetUniform_Color( lightColor );

			MatrixToVectorsFLU( matrixIdentity, forward, left, up );
			VectorMA( vec3_origin, 16, forward, forward );
			VectorMA( vec3_origin, 16, left, left );
			VectorMA( vec3_origin, 16, up, up );

			Tess_Begin( Tess_StageIteratorDebug, nullptr, nullptr, true, -1, 0 );

			{
				// set up the transformation matrix
				R_RotateLightForViewParms( light, &backEnd.viewParms, &backEnd.orientation );

				GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );
				gl_genericShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );
				gl_genericShader->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );

				R_TessLight( light, lightColor );

				switch ( light->l.rlType )
				{
					case refLightType_t::RL_OMNI:
					case refLightType_t::RL_DIRECTIONAL:
						{
							if ( !VectorCompare( light->l.center, vec3_origin ) )
							{
								Tess_AddCube( light->l.center, minSize, maxSize, Color::Yellow );
							}
							break;
						}

					case refLightType_t::RL_PROJ:
						{
							// draw light_target
							Tess_AddCube( light->l.projTarget, minSize, maxSize, Color::Red );
							Tess_AddCube( light->l.projRight, minSize, maxSize, Color::Green );
							Tess_AddCube( light->l.projUp, minSize, maxSize, Color::Blue );

							if ( !VectorCompare( light->l.projStart, vec3_origin ) )
							{
								Tess_AddCube( light->l.projStart, minSize, maxSize, Color::Yellow );
							}

							if ( !VectorCompare( light->l.projEnd, vec3_origin ) )
							{
								Tess_AddCube( light->l.projEnd, minSize, maxSize, Color::Magenta );
							}
							break;
						}

					default:
						break;
				}
			}

			Tess_End();
		}

		// go back to the world modelview matrix
		backEnd.orientation = backEnd.viewParms.world;
		GL_LoadModelViewMatrix( backEnd.viewParms.world.modelViewMatrix );
	}

	if ( r_showLightInteractions->integer )
	{
		int           i;
		int           cubeSides;
		interaction_t *ia;
		int           iaCount;
		trRefLight_t  *light;
		trRefEntity_t *entity;
		surfaceType_t *surface;
		Color::Color lightColor;

		static const vec3_t mins = { -1, -1, -1 };
		static const vec3_t maxs = { 1, 1, 1 };

		gl_genericShader->SetVertexSkinning( false );
		gl_genericShader->SetVertexAnimation( false );
		gl_genericShader->SetTCGenEnvironment( false );
		gl_genericShader->SetTCGenLightmap( false );
		gl_genericShader->SetDepthFade( false );
		gl_genericShader->BindProgram( 0 );

		GL_State( GLS_POLYMODE_LINE | GLS_DEPTHTEST_DISABLE );
		GL_Cull( cullType_t::CT_TWO_SIDED );

		// set uniforms
		gl_genericShader->SetUniform_AlphaTest( GLS_ATEST_NONE );
		gl_genericShader->SetUniform_ColorModulateColorGen( colorGen_t::CGEN_VERTEX, alphaGen_t::AGEN_VERTEX );
		gl_genericShader->SetUniform_Color( Color::Black );

		// bind u_ColorMap
		gl_genericShader->SetUniform_ColorMapBindless(
			GL_BindToTMU( 0, tr.whiteImage )
		);
		gl_genericShader->SetUniform_TextureMatrix( matrixIdentity );

		for ( iaCount = 0, ia = &backEnd.viewParms.interactions[ 0 ]; iaCount < backEnd.viewParms.numInteractions; ia++, iaCount++ )
		{
			backEnd.currentEntity = entity = ia->entity;
			light = ia->light;
			surface = ia->surface;

			if ( entity != &tr.worldEntity )
			{
				// set up the transformation matrix
				R_RotateEntityForViewParms( backEnd.currentEntity, &backEnd.viewParms, &backEnd.orientation );
			}
			else
			{
				backEnd.orientation = backEnd.viewParms.world;
			}

			GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );
			gl_genericShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

			if ( glConfig2.shadowMapping && light->l.rlType == refLightType_t::RL_OMNI )
			{
				// count how many cube sides are in use for this interaction
				cubeSides = 0;

				for ( i = 0; i < 6; i++ )
				{
					if ( ia->cubeSideBits & ( 1 << i ) )
					{
						cubeSides++;
					}
				}
				lightColor = Color::Color::Indexed( cubeSides );
			}
			else
			{
				lightColor = Color::MdGrey;
			}

			lightColor = Color::White;

			Tess_Begin( Tess_StageIteratorDebug, nullptr, nullptr, true, -1, 0 );

			if ( *surface == surfaceType_t::SF_FACE || *surface == surfaceType_t::SF_GRID || *surface == surfaceType_t::SF_TRIANGLES )
			{
				srfGeneric_t *gen;

				gen = ( srfGeneric_t * ) surface;

				if ( *surface == surfaceType_t::SF_FACE )
				{
					lightColor = Color::MdGrey;
				}
				else if ( *surface == surfaceType_t::SF_GRID )
				{
					lightColor = Color::Cyan;
				}
				else if ( *surface == surfaceType_t::SF_TRIANGLES )
				{
					lightColor = Color::Magenta;
				}
				else
				{
					lightColor = Color::MdGrey;
				}

				Tess_AddCube( vec3_origin, gen->bounds[ 0 ], gen->bounds[ 1 ], lightColor );

				Tess_AddCube( gen->origin, mins, maxs, Color::White );
			}
			else if ( *surface == surfaceType_t::SF_VBO_MESH )
			{
				srfVBOMesh_t *srf = ( srfVBOMesh_t * ) surface;
				Tess_AddCube( vec3_origin, srf->bounds[ 0 ], srf->bounds[ 1 ], lightColor );
			}
			else if ( *surface == surfaceType_t::SF_MDV )
			{
				Tess_AddCube( vec3_origin, entity->localBounds[ 0 ], entity->localBounds[ 1 ], lightColor );
			}

			Tess_End();
		}

		// go back to the world modelview matrix
		backEnd.orientation = backEnd.viewParms.world;
		GL_LoadModelViewMatrix( backEnd.viewParms.world.modelViewMatrix );
	}

	if ( r_showEntityTransforms->integer )
	{
		trRefEntity_t *ent;
		int           i;
		static const vec3_t mins = { -1, -1, -1 };
		static const vec3_t maxs = { 1, 1, 1 };

		gl_genericShader->SetVertexSkinning( false );
		gl_genericShader->SetVertexAnimation( false );
		gl_genericShader->SetTCGenEnvironment( false );
		gl_genericShader->SetTCGenLightmap( false );
		gl_genericShader->SetDepthFade( false );
		gl_genericShader->BindProgram( 0 );

		GL_State( GLS_POLYMODE_LINE | GLS_DEPTHTEST_DISABLE );
		GL_Cull( cullType_t::CT_TWO_SIDED );

		// set uniforms
		gl_genericShader->SetUniform_AlphaTest( GLS_ATEST_NONE );
		gl_genericShader->SetUniform_ColorModulateColorGen( colorGen_t::CGEN_VERTEX, alphaGen_t::AGEN_VERTEX );
		gl_genericShader->SetUniform_Color( Color::Black );

		// bind u_ColorMap
		gl_genericShader->SetUniform_ColorMapBindless(
			GL_BindToTMU( 0, tr.whiteImage )
		);
		gl_genericShader->SetUniform_TextureMatrix( matrixIdentity );

		ent = backEnd.refdef.entities;

		for ( i = 0; i < backEnd.refdef.numEntities; i++, ent++ )
		{
			if ( ( ent->e.renderfx & RF_THIRD_PERSON ) &&
			     backEnd.viewParms.portalLevel == 0 )
			{
				continue;
			}

			if ( ent->cull == cullResult_t::CULL_OUT )
			{
				continue;
			}

			// set up the transformation matrix
			R_RotateEntityForViewParms( ent, &backEnd.viewParms, &backEnd.orientation );
			GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );
			gl_genericShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

			Tess_Begin( Tess_StageIteratorDebug, nullptr, nullptr, true, -1, 0 );

			Tess_AddCube( vec3_origin, ent->localBounds[ 0 ], ent->localBounds[ 1 ], Color::Blue );

			Tess_AddCube( vec3_origin, mins, maxs,Color::White );

			Tess_End();
		}

		// go back to the world modelview matrix
		backEnd.orientation = backEnd.viewParms.world;
		GL_LoadModelViewMatrix( backEnd.viewParms.world.modelViewMatrix );
	}

	if ( r_showSkeleton->integer )
	{
		int                  i, j, k, parentIndex;
		trRefEntity_t        *ent;
		vec3_t               offset;
		vec3_t               diff, tmp, tmp2, tmp3;
		vec_t                length;
		vec4_t               tetraVerts[ 4 ];
		static refSkeleton_t skeleton;
		refSkeleton_t        *skel;

		gl_genericShader->SetVertexSkinning( false );
		gl_genericShader->SetVertexAnimation( false );
		gl_genericShader->SetTCGenEnvironment( false );
		gl_genericShader->SetTCGenLightmap( false );
		gl_genericShader->SetDepthFade( false );
		gl_genericShader->BindProgram( 0 );

		GL_Cull( cullType_t::CT_TWO_SIDED );

		// set uniforms
		gl_genericShader->SetUniform_AlphaTest( GLS_ATEST_NONE );
		gl_genericShader->SetUniform_ColorModulateColorGen( colorGen_t::CGEN_VERTEX, alphaGen_t::AGEN_VERTEX );
		gl_genericShader->SetUniform_Color( Color::Black );

		// bind u_ColorMap
		gl_genericShader->SetUniform_ColorMapBindless(
			GL_BindToTMU( 0, tr.whiteImage )
		);

		gl_genericShader->SetUniform_TextureMatrix( matrixIdentity );

		ent = backEnd.refdef.entities;

		for ( i = 0; i < backEnd.refdef.numEntities; i++, ent++ )
		{
			if ( ( ent->e.renderfx & RF_THIRD_PERSON && backEnd.viewParms.portalLevel == 0 ) ||
			     ( ent->e.renderfx & RF_FIRST_PERSON && backEnd.viewParms.portalLevel != 0 ) )
			{
				continue;
			}

			// set up the transformation matrix
			R_RotateEntityForViewParms( ent, &backEnd.viewParms, &backEnd.orientation );
			GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );
			gl_genericShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

			skel = nullptr;

			if ( ent->e.skeleton.type == refSkeletonType_t::SK_ABSOLUTE )
			{
				skel = &ent->e.skeleton;
			}
			else
			{
				model_t   *model;
				refBone_t *bone;

				model = R_GetModelByHandle( ent->e.hModel );

				if ( model )
				{
					switch ( model->type )
					{
						case modtype_t::MOD_MD5:
							{
								// copy absolute bones
								skeleton.numBones = model->md5->numBones;

								for ( j = 0, bone = &skeleton.bones[ 0 ]; j < skeleton.numBones; j++, bone++ )
								{
#if defined( REFBONE_NAMES )
									Q_strncpyz( bone->name, model->md5->bones[ j ].name, sizeof( bone->name ) );
#endif

									bone->parentIndex = model->md5->bones[ j ].parentIndex;
									TransInitRotationQuat( model->md5->bones[ j ].rotation, &bone->t );
									TransAddTranslation( model->md5->bones[ j ].origin, &bone->t );
								}

								skel = &skeleton;
								break;
							}

						default:
							break;
					}
				}
			}

			if ( skel )
			{
				static vec3_t worldOrigins[ MAX_BONES ];

				GL_State( GLS_POLYMODE_LINE | GLS_DEPTHTEST_DISABLE );
				Tess_Begin( Tess_StageIteratorDebug, nullptr, nullptr, true, -1, 0 );

				for ( j = 0; j < skel->numBones; j++ )
				{
					parentIndex = skel->bones[ j ].parentIndex;
					vec3_t origin;

					if ( parentIndex < 0 )
					{
						VectorClear( origin );
					}
					else
					{
						VectorCopy( skel->bones[ parentIndex ].t.trans, origin );
					}

					VectorCopy( skel->bones[ j ].t.trans, offset );
					vec3_t forward, right, up;
					QuatToVectorsFRU( skel->bones[ j ].t.rot, forward, right, up );

					VectorSubtract( offset, origin, diff );

					if ( ( length = VectorNormalize( diff ) ) )
					{
						PerpendicularVector( tmp, diff );
						//VectorCopy(up, tmp);

						VectorScale( tmp, length * 0.1, tmp2 );
						VectorMA( tmp2, length * 0.2, diff, tmp2 );

						for ( k = 0; k < 3; k++ )
						{
							RotatePointAroundVector( tmp3, diff, tmp2, k * 120 );
							VectorAdd( tmp3, origin, tmp3 );
							VectorScale( tmp3, skel->scale, tetraVerts[ k ] );
							tetraVerts[ k ][ 3 ] = 1;
						}

						VectorScale( origin, skel->scale, tetraVerts[ 3 ] );
						tetraVerts[ 3 ][ 3 ] = 1;

						Color::Color color = Color::Color::Indexed( j );
						Tess_AddTetrahedron( tetraVerts, color );

						VectorScale( offset, skel->scale, tetraVerts[ 3 ] );
						tetraVerts[ 3 ][ 3 ] = 1;
						Tess_AddTetrahedron( tetraVerts, color );
					}

					MatrixTransformPoint( backEnd.orientation.transformMatrix, skel->bones[ j ].t.trans, worldOrigins[ j ] );
				}

				Tess_End();

#if defined( REFBONE_NAMES )
				if ( cls.consoleFont != nullptr )
				{
					GL_State( GLS_DEPTHTEST_DISABLE | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );

					// go back to the world modelview matrix
					backEnd.orientation = backEnd.viewParms.world;
					GL_LoadModelViewMatrix( backEnd.viewParms.world.modelViewMatrix );
					gl_genericShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

					// draw names
					for ( j = 0; j < skel->numBones; j++ )
					{
						vec3_t left, up;
						float  radius;
						vec3_t origin;

						// calculate the xyz locations for the four corners
						radius = 0.4;
						VectorScale( backEnd.viewParms.orientation.axis[ 1 ], radius, left );
						VectorScale( backEnd.viewParms.orientation.axis[ 2 ], radius, up );

						if ( backEnd.viewParms.isMirror )
						{
							VectorSubtract( vec3_origin, left, left );
						}

						for ( k = 0; ( unsigned ) k < strlen( skel->bones[ j ].name ); k++ )
						{
							int   ch;

							ch = skel->bones[ j ].name[ k ];
							ch &= 255;

							glyphInfo_t *glyph = &cls.consoleFont->glyphBlock[ 0 ][ ch ];
							re.GlyphChar( cls.consoleFont, ch, glyph );

							shader_t *shader = R_GetShaderByHandle( glyph->glyph );
							if ( shader != tess.surfaceShader )
							{
								// Try to grab an image rather than running the whole q3shader since we
								// want to disable the depth test
								if ( shader->lastStage == shader->stages ||
									!shader->stages[ 0 ].bundle[ TB_COLORMAP ].image[ 0 ] )
								{
									Log::Warn( "can't render bone name '%s'", skel->bones[ j ].name );
									break;
								}

								Tess_End();
								Tess_Begin( Tess_StageIteratorDebug, shader, nullptr, true, -1, 0 );
								gl_genericShader->SetUniform_ColorMapBindless(
									GL_BindToTMU( 0, shader->stages[ 0 ].bundle[ TB_COLORMAP ].image[ 0 ] )
								);
							}

							VectorMA( worldOrigins[ j ], - ( k*1.8f + 2.0f ), left, origin );
							Tess_AddQuadStampExt( origin, left, up, Color::White, glyph->s,
							                      glyph->t, glyph->s2, glyph->t2 );
						}
					}

					Tess_End();
				}
#endif // REFBONE_NAMES
			}
		}
	}

	if ( r_showLightScissors->integer )
	{
		interaction_t *ia;
		int           iaCount;
		matrix_t      ortho;

		gl_genericShader->SetVertexSkinning( false );
		gl_genericShader->SetVertexAnimation( false );
		gl_genericShader->SetTCGenEnvironment( false );
		gl_genericShader->SetTCGenLightmap( false );
		gl_genericShader->SetDepthFade( false );
		gl_genericShader->BindProgram( 0 );

		GL_State( GLS_POLYMODE_LINE | GLS_DEPTHTEST_DISABLE );
		GL_Cull( cullType_t::CT_TWO_SIDED );

		// set uniforms
		gl_genericShader->SetUniform_AlphaTest( GLS_ATEST_NONE );
		gl_genericShader->SetUniform_ColorModulateColorGen( colorGen_t::CGEN_CUSTOM_RGB, alphaGen_t::AGEN_CUSTOM );

		// bind u_ColorMap
		gl_genericShader->SetUniform_ColorMapBindless(
			GL_BindToTMU( 0, tr.whiteImage )
		);
		gl_genericShader->SetUniform_TextureMatrix( matrixIdentity );

		// set 2D virtual screen size
		GL_PushMatrix();
		MatrixOrthogonalProjection( ortho, backEnd.viewParms.viewportX,
		                            backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth,
		                            backEnd.viewParms.viewportY,
		                            backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight, -99999, 99999 );
		GL_LoadProjectionMatrix( ortho );

		for ( iaCount = 0, ia = &backEnd.viewParms.interactions[ 0 ]; iaCount < backEnd.viewParms.numInteractions; )
		{
			gl_genericShader->SetUniform_Color( Color::White );

			Tess_InstantQuad( *gl_genericShader,
			                  ia->scissorX, ia->scissorY, ia->scissorWidth - 1.0f, ia->scissorHeight - 1.0f );

			if ( !ia->next )
			{
				if ( iaCount < ( backEnd.viewParms.numInteractions - 1 ) )
				{
					// jump to next interaction and continue
					ia++;
					iaCount++;
				}
				else
				{
					// increase last time to leave for loop
					iaCount++;
				}
			}
			else
			{
				// just continue
				ia = ia->next;
				iaCount++;
			}
		}

		GL_PopMatrix();
	}

	// GLSL shader isn't built when reflection mapping is disabled.
	if ( r_showCubeProbes.Get() && glConfig2.reflectionMapping &&
	     !( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_NOCUBEMAP ) ) )
	{
		static const vec3_t mins = { -8, -8, -8 };
		static const vec3_t maxs = { 8,  8,  8 };

		static const vec3_t outlineMins = { -9, -9, -9 };
		static const vec3_t outlineMaxs = { 9,  9,  9 };

		// choose right shader program ----------------------------------
		gl_reflectionShader->SetVertexSkinning( false );
		gl_reflectionShader->SetVertexAnimation( false );

		gl_reflectionShader->BindProgram( 0 );

		// end choose right shader program ------------------------------

		gl_reflectionShader->SetUniform_ViewOrigin( backEnd.viewParms.orientation.origin );  // in world space

		GL_State( 0 );
		GL_Cull( cullType_t::CT_FRONT_SIDED );

		// set up the transformation matrix
		backEnd.orientation = backEnd.viewParms.world;
		GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );

		gl_reflectionShader->SetUniform_ModelMatrix( backEnd.orientation.transformMatrix );
		gl_reflectionShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

		if ( r_showCubeProbes.Get() == Util::ordinal( showCubeProbesMode::GRID ) ) {
			// Debug rendering can be really slow here
			for ( auto it = tr.cubeProbeGrid.begin(); it != tr.cubeProbeGrid.end(); it++ ) {
				uint32_t x;
				uint32_t y;
				uint32_t z;
				tr.cubeProbeGrid.IteratorToCoords( it, &x, &y, &z );
				vec3_t position{ ( float ) x * tr.cubeProbeSpacing, ( float ) y * tr.cubeProbeSpacing, ( float ) z * tr.cubeProbeSpacing };

				// Match the map's start coords
				VectorAdd( position, tr.world->nodes[0].mins, position );

				cubemapProbe_t* cubeProbe = &tr.cubeProbes[tr.cubeProbeGrid( x, y, z )];

				Tess_Begin( Tess_StageIteratorDebug, nullptr, nullptr, true, -1, 0 );

				gl_reflectionShader->SetUniform_CameraPosition( position );

				// bind u_ColorMap
				gl_reflectionShader->SetUniform_ColorMapCubeBindless(
					GL_BindToTMU( 0, cubeProbe->cubemap )
				);

				Tess_AddCubeWithNormals( position, mins, maxs, Color::White );

				Tess_End();
			}
		} else {
			for ( const cubemapProbe_t& cubeProbe : tr.cubeProbes ) {

				Tess_Begin( Tess_StageIteratorDebug, nullptr, nullptr, true, -1, 0 );

				gl_reflectionShader->SetUniform_CameraPosition( cubeProbe.origin );

				// bind u_ColorMap
				gl_reflectionShader->SetUniform_ColorMapCubeBindless(
					GL_BindToTMU( 0, cubeProbe.cubemap )
				);

				Tess_AddCubeWithNormals( cubeProbe.origin, mins, maxs, Color::White );

				Tess_End();
			}
		}

		{
			gl_genericShader->SetVertexSkinning( false );
			gl_genericShader->SetVertexAnimation( false );
			gl_genericShader->SetTCGenEnvironment( false );
			gl_genericShader->SetTCGenLightmap( false );
			gl_genericShader->SetDepthFade( false );
			gl_genericShader->BindProgram( 0 );

			// set uniforms
			gl_genericShader->SetUniform_AlphaTest( GLS_ATEST_NONE );
			gl_genericShader->SetUniform_ColorModulateColorGen( colorGen_t::CGEN_VERTEX, alphaGen_t::AGEN_VERTEX );
			gl_genericShader->SetUniform_Color( Color::Black );

			GL_State( GLS_DEFAULT );
			GL_Cull( cullType_t::CT_TWO_SIDED );

			// set uniforms

			// set up the transformation matrix
			backEnd.orientation = backEnd.viewParms.world;
			GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );
			gl_genericShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

			// bind u_ColorMap
			gl_genericShader->SetUniform_ColorMapBindless(
				GL_BindToTMU( 0, tr.whiteImage )
			);
			gl_genericShader->SetUniform_TextureMatrix( matrixIdentity );

			GL_State( GLS_POLYMODE_LINE | GLS_DEPTHFUNC_ALWAYS );

			GL_CheckErrors();

			cubemapProbe_t* probes[2];
			vec4_t trilerp;
			vec3_t gridPoints[2];
			R_GetNearestCubeMaps( backEnd.viewParms.orientation.origin, probes, trilerp, 2, gridPoints );

			Tess_Begin( Tess_StageIteratorDebug, nullptr, nullptr, true, -1, 0 );

			vec3_t position;
			if ( r_showCubeProbes.Get() == Util::ordinal( showCubeProbesMode::GRID ) ) {
				VectorSet( position, gridPoints[0][0] * tr.cubeProbeSpacing, gridPoints[0][1] * tr.cubeProbeSpacing,
					gridPoints[0][2] * tr.cubeProbeSpacing );

				// Match the map's start coords
				VectorAdd( position, tr.world->nodes[0].mins, position );
			} else {
				VectorCopy( probes[0]->origin, position );
			}

			Tess_AddCubeWithNormals( position, outlineMins, outlineMaxs, Color::Green );

			if ( r_showCubeProbes.Get() == Util::ordinal( showCubeProbesMode::GRID ) ) {
				VectorSet( position, gridPoints[1][0] * tr.cubeProbeSpacing, gridPoints[1][1] * tr.cubeProbeSpacing,
					gridPoints[1][2] * tr.cubeProbeSpacing );

				// Match the map's start coords
				VectorAdd( position, tr.world->nodes[0].mins, position );
			} else {
				VectorCopy( probes[1]->origin, position );
			}

			Tess_AddCubeWithNormals( position, outlineMins, outlineMaxs, Color::Red );

			Tess_End();
		}

		// go back to the world modelview matrix
		backEnd.orientation = backEnd.viewParms.world;
		GL_LoadModelViewMatrix( backEnd.viewParms.world.modelViewMatrix );
	}

	if ( r_showLightGrid->integer && !( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) )
	{
		int             x, y, z, k;
		vec3_t          offset;
		vec3_t          tmp, tmp2, tmp3;
		vec_t           length;
		vec4_t          tetraVerts[ 4 ];

		GLimp_LogComment( "--- r_showLightGrid > 0: Rendering light grid\n" );

		gl_genericShader->SetVertexSkinning( false );
		gl_genericShader->SetVertexAnimation( false );
		gl_genericShader->SetTCGenEnvironment( false );
		gl_genericShader->SetTCGenLightmap( false );
		gl_genericShader->SetDepthFade( false );
		gl_genericShader->BindProgram( 0 );

		// set uniforms
		gl_genericShader->SetUniform_AlphaTest( GLS_ATEST_NONE );
		gl_genericShader->SetUniform_ColorModulateColorGen( colorGen_t::CGEN_VERTEX, alphaGen_t::AGEN_VERTEX );
		gl_genericShader->SetUniform_Color( Color::Black );

		GL_State( GLS_DEFAULT );
		GL_Cull( cullType_t::CT_TWO_SIDED );

		// set uniforms

		// set up the transformation matrix
		backEnd.orientation = backEnd.viewParms.world;
		GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );
		gl_genericShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

		// bind u_ColorMap
		gl_genericShader->SetUniform_ColorMapBindless(
			GL_BindToTMU( 0, tr.whiteImage )
		);
		gl_genericShader->SetUniform_TextureMatrix( matrixIdentity );

		Tess_Begin( Tess_StageIteratorDebug, nullptr, nullptr, true, -1, 0 );
		GL_CheckErrors();

		for ( z = 0; z < tr.world->lightGridBounds[ 2 ]; z++ ) {
			for ( y = 0; y < tr.world->lightGridBounds[ 1 ]; y++ ) {
				for ( x = 0; x < tr.world->lightGridBounds[ 0 ]; x++ ) {
					vec3_t origin;
					Color::Color ambientColor;
					Color::Color directedColor;
					vec3_t lightDir;

					VectorCopy( tr.world->lightGridOrigin, origin );
					origin[ 0 ] += x * tr.world->lightGridSize[ 0 ];
					origin[ 1 ] += y * tr.world->lightGridSize[ 1 ];
					origin[ 2 ] += z * tr.world->lightGridSize[ 2 ];

					if ( VectorDistanceSquared( origin, backEnd.viewParms.orientation.origin ) > Square( 1024 ) )
					{
						continue;
					}

					R_LightForPoint( origin, ambientColor.ToArray(),
							 directedColor.ToArray(), lightDir );
					VectorNegate( lightDir, lightDir );

					length = 8;
					VectorMA( origin, 8, lightDir, offset );

					PerpendicularVector( tmp, lightDir );
					//VectorCopy(up, tmp);

					VectorScale( tmp, length * 0.1, tmp2 );
					VectorMA( tmp2, length * 0.2, lightDir, tmp2 );

					for ( k = 0; k < 3; k++ )
					{
						RotatePointAroundVector( tmp3, lightDir, tmp2, k * 120 );
						VectorAdd( tmp3, origin, tmp3 );
						VectorCopy( tmp3, tetraVerts[ k ] );
						tetraVerts[ k ][ 3 ] = 1;
					}

					VectorCopy( origin, tetraVerts[ 3 ] );
					tetraVerts[ 3 ][ 3 ] = 1;
					Tess_AddTetrahedron( tetraVerts, ambientColor );

					VectorCopy( offset, tetraVerts[ 3 ] );
					tetraVerts[ 3 ][ 3 ] = 1;
					Tess_AddTetrahedron( tetraVerts, directedColor );
				}
			}
		}

		Tess_End();
	}

	if ( r_showBspNodes->integer )
	{
		if ( ( backEnd.refdef.rdflags & ( RDF_NOWORLDMODEL ) ) || !tr.world )
		{
			return;
		}

		gl_genericShader->SetVertexSkinning( false );
		gl_genericShader->SetVertexAnimation( false );
		gl_genericShader->SetTCGenEnvironment( false );
		gl_genericShader->SetTCGenLightmap( false );
		gl_genericShader->SetDepthFade( false );
		gl_genericShader->BindProgram( 0 );

		// set uniforms
		gl_genericShader->SetUniform_AlphaTest( GLS_ATEST_NONE );
		gl_genericShader->SetUniform_ColorModulateColorGen( colorGen_t::CGEN_CUSTOM_RGB, alphaGen_t::AGEN_CUSTOM );

		// bind u_ColorMap
		gl_genericShader->SetUniform_ColorMapBindless(
			GL_BindToTMU( 0, tr.whiteImage )
		);
		gl_genericShader->SetUniform_TextureMatrix( matrixIdentity );

		GL_CheckErrors();

		for ( int i = 0; i < 2; i++ )
		{
			float    x, y, w, h;
			matrix_t ortho;
			vec4_t   quadVerts[ 4 ];

			if ( i == 1 )
			{
				// set 2D virtual screen size
				GL_PushMatrix();
				MatrixOrthogonalProjection( ortho, backEnd.viewParms.viewportX,
				                            backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth,
				                            backEnd.viewParms.viewportY,
				                            backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight, -99999, 99999 );
				GL_LoadProjectionMatrix( ortho );

				GL_Cull( cullType_t::CT_TWO_SIDED );
				GL_State( GLS_DEPTHTEST_DISABLE );

				gl_genericShader->SetUniform_Color( Color::Black );

				w = 300;
				h = 300;

				x = 20;
				y = 90;

				Vector4Set( quadVerts[ 0 ], x, y, 0, 1 );
				Vector4Set( quadVerts[ 1 ], x + w, y, 0, 1 );
				Vector4Set( quadVerts[ 2 ], x + w, y + h, 0, 1 );
				Vector4Set( quadVerts[ 3 ], x, y + h, 0, 1 );

				Tess_InstantQuad( *gl_genericShader, x, y, w, h );

				{
					int    j;
					vec3_t farCorners[ 4 ];
					vec3_t nearCorners[ 4 ];
					vec3_t cropBounds[ 2 ];
					vec4_t point, transf;

					GL_Viewport( x, y, w, h );
					GL_Scissor( x, y, w, h );

					GL_PushMatrix();

					// calculate top down view projection matrix
					{
						vec3_t                                                       forward = { 0, 0, -1 };
						vec3_t                                                       up = { 1, 0, 0 };

						matrix_t /*rotationMatrix, transformMatrix,*/ viewMatrix, projectionMatrix;

						// Quake -> OpenGL view matrix from light perspective
						MatrixLookAtRH( viewMatrix, backEnd.viewParms.orientation.origin, forward, up );

						ClearBounds( cropBounds[ 0 ], cropBounds[ 1 ] );

						for ( j = 0; j < 8; j++ )
						{
							point[ 0 ] = tr.world->models[ 0 ].bounds[ j & 1 ][ 0 ];
							point[ 1 ] = tr.world->models[ 0 ].bounds[( j >> 1 ) & 1 ][ 1 ];
							point[ 2 ] = tr.world->models[ 0 ].bounds[( j >> 2 ) & 1 ][ 2 ];
							point[ 3 ] = 1;

							MatrixTransform4( viewMatrix, point, transf );
							transf[ 0 ] /= transf[ 3 ];
							transf[ 1 ] /= transf[ 3 ];
							transf[ 2 ] /= transf[ 3 ];

							AddPointToBounds( transf, cropBounds[ 0 ], cropBounds[ 1 ] );
						}

						MatrixOrthogonalProjectionRH( projectionMatrix, cropBounds[ 0 ][ 0 ], cropBounds[ 1 ][ 0 ], cropBounds[ 0 ][ 1 ], cropBounds[ 1 ][ 1 ], -cropBounds[ 1 ][ 2 ], -cropBounds[ 0 ][ 2 ] );

						GL_LoadModelViewMatrix( viewMatrix );
						GL_LoadProjectionMatrix( projectionMatrix );
					}

					// set uniforms
					gl_genericShader->SetUniform_ColorModulateColorGen( colorGen_t::CGEN_VERTEX, alphaGen_t::AGEN_VERTEX );
					gl_genericShader->SetUniform_Color( Color::Black );

					GL_State( GLS_POLYMODE_LINE | GLS_DEPTHTEST_DISABLE );
					GL_Cull( cullType_t::CT_TWO_SIDED );

					// bind u_ColorMap
					gl_genericShader->SetUniform_ColorMapBindless(
						GL_BindToTMU( 0, tr.whiteImage )
					);
					gl_genericShader->SetUniform_TextureMatrix( matrixIdentity );

					gl_genericShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

					plane_t splitFrustum[ 6 ];
					for ( j = 0; j < 6; j++ )
					{
						VectorCopy( backEnd.viewParms.frustums[ 0 ][ j ].normal, splitFrustum[ j ].normal );
						splitFrustum[ j ].dist = backEnd.viewParms.frustums[ 0 ][ j ].dist;
					}

					// calculate split frustum corner points
					R_CalcFrustumNearCorners( splitFrustum, nearCorners );
					R_CalcFrustumFarCorners( splitFrustum, farCorners );

					Tess_Begin( Tess_StageIteratorDebug, nullptr, nullptr, true, -1, 0 );

					// draw outer surfaces
					for ( j = 0; j < 4; j++ )
					{
						Vector4Set( quadVerts[ 0 ], nearCorners[ j ][ 0 ], nearCorners[ j ][ 1 ], nearCorners[ j ][ 2 ], 1 );
						Vector4Set( quadVerts[ 1 ], farCorners[ j ][ 0 ], farCorners[ j ][ 1 ], farCorners[ j ][ 2 ], 1 );
						Vector4Set( quadVerts[ 2 ], farCorners[( j + 1 ) % 4 ][ 0 ], farCorners[( j + 1 ) % 4 ][ 1 ], farCorners[( j + 1 ) % 4 ][ 2 ], 1 );
						Vector4Set( quadVerts[ 3 ], nearCorners[( j + 1 ) % 4 ][ 0 ], nearCorners[( j + 1 ) % 4 ][ 1 ], nearCorners[( j + 1 ) % 4 ][ 2 ], 1 );
						Tess_AddQuadStamp2( quadVerts, Color::Cyan );
					}

					// draw far cap
					Vector4Set( quadVerts[ 0 ], farCorners[ 3 ][ 0 ], farCorners[ 3 ][ 1 ], farCorners[ 3 ][ 2 ], 1 );
					Vector4Set( quadVerts[ 1 ], farCorners[ 2 ][ 0 ], farCorners[ 2 ][ 1 ], farCorners[ 2 ][ 2 ], 1 );
					Vector4Set( quadVerts[ 2 ], farCorners[ 1 ][ 0 ], farCorners[ 1 ][ 1 ], farCorners[ 1 ][ 2 ], 1 );
					Vector4Set( quadVerts[ 3 ], farCorners[ 0 ][ 0 ], farCorners[ 0 ][ 1 ], farCorners[ 0 ][ 2 ], 1 );
					Tess_AddQuadStamp2( quadVerts, Color::Blue );

					// draw near cap
					Vector4Set( quadVerts[ 0 ], nearCorners[ 0 ][ 0 ], nearCorners[ 0 ][ 1 ], nearCorners[ 0 ][ 2 ], 1 );
					Vector4Set( quadVerts[ 1 ], nearCorners[ 1 ][ 0 ], nearCorners[ 1 ][ 1 ], nearCorners[ 1 ][ 2 ], 1 );
					Vector4Set( quadVerts[ 2 ], nearCorners[ 2 ][ 0 ], nearCorners[ 2 ][ 1 ], nearCorners[ 2 ][ 2 ], 1 );
					Vector4Set( quadVerts[ 3 ], nearCorners[ 3 ][ 0 ], nearCorners[ 3 ][ 1 ], nearCorners[ 3 ][ 2 ], 1 );
					Tess_AddQuadStamp2( quadVerts, Color::Green );

					Tess_End();

					gl_genericShader->SetUniform_ColorModulateColorGen( colorGen_t::CGEN_CUSTOM_RGB, alphaGen_t::AGEN_CUSTOM );
				}
			} // i == 1
			else
			{
				GL_State( GLS_POLYMODE_LINE | GLS_DEPTHTEST_DISABLE );
				GL_Cull( cullType_t::CT_TWO_SIDED );

				// render in world space
				backEnd.orientation = backEnd.viewParms.world;

				GL_LoadProjectionMatrix( backEnd.viewParms.projectionMatrix );
				GL_LoadModelViewMatrix( backEnd.viewParms.world.modelViewMatrix );

				gl_genericShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );
			}

			// draw BSP nodes
			for ( int j = 0; j < backEndData[ backEnd.smpFrame ]->traversalLength; j++ )
			{
				bspNode_t *node = backEndData[ backEnd.smpFrame ]->traversalList[ j ];

				if ( node->contents != -1 )
				{
					if ( r_showBspNodes->integer == 3 )
					{
						continue;
					}

					if ( node->numMarkSurfaces <= 0 )
					{
						continue;
					}

					//if(node->shrinkedAABB)
					//  gl_genericShader->SetUniform_Color(colorBlue);
					//else
					if ( node->visCounts[ tr.visIndex ] == tr.visCounts[ tr.visIndex ] )
					{
						gl_genericShader->SetUniform_Color( Color::Green );
					}
					else
					{
						gl_genericShader->SetUniform_Color( Color::Red );
					}
				}
				else
				{
					if ( r_showBspNodes->integer == 2 )
					{
						continue;
					}

					if ( node->visCounts[ tr.visIndex ] == tr.visCounts[ tr.visIndex ] )
					{
						gl_genericShader->SetUniform_Color( Color::Yellow );
					}
					else
					{
						gl_genericShader->SetUniform_Color( Color::Blue );
					}
				}

				if ( node->contents != -1 )
				{
					glEnable( GL_POLYGON_OFFSET_FILL );
					GL_PolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
				}

				Tess_Begin( Tess_StageIteratorDebug, nullptr, nullptr, true, -1, 0 );
				Tess_AddCube( vec3_origin, node->mins, node->maxs, Color::White );
				Tess_End();

				if ( node->contents != -1 )
				{
					glDisable( GL_POLYGON_OFFSET_FILL );
				}
			}

			if ( i == 1 )
			{
				GL_PopMatrix();

				GL_Viewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
				             backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );

				GL_Scissor( backEnd.viewParms.scissorX, backEnd.viewParms.scissorY,
				            backEnd.viewParms.scissorWidth, backEnd.viewParms.scissorHeight );

				GL_PopMatrix();
			}
		}

		// go back to the world modelview matrix
		backEnd.orientation = backEnd.viewParms.world;

		GL_LoadProjectionMatrix( backEnd.viewParms.projectionMatrix );
		GL_LoadModelViewMatrix( backEnd.viewParms.world.modelViewMatrix );
	}

	GL_CheckErrors();
}

static unsigned int drawMode;
static debugDrawMode_t currentDebugDrawMode;
static unsigned maxDebugVerts;
static float currentDebugSize;

void DebugDrawBegin( debugDrawMode_t mode, float size ) {

	if ( tess.numVertexes )
	{
		Tess_End();
	}

	Tess_MapVBOs( false );

	Color::Color colorClear = { 0, 0, 0, 0 };
	currentDebugDrawMode = mode;
	currentDebugSize = size;
	switch(mode) {
		case debugDrawMode_t::D_DRAW_POINTS:
			glPointSize( size );
			drawMode = GL_POINTS;
			maxDebugVerts = SHADER_MAX_VERTEXES - 1;
			break;
		case debugDrawMode_t::D_DRAW_LINES:
			glLineWidth( size );
			drawMode = GL_LINES;
			maxDebugVerts = ( SHADER_MAX_VERTEXES - 1 )/2*2;
			break;
		case debugDrawMode_t::D_DRAW_TRIS:
			drawMode = GL_TRIANGLES;
			maxDebugVerts = ( SHADER_MAX_VERTEXES - 1 )/3*3;
			break;
		case debugDrawMode_t::D_DRAW_QUADS:
			drawMode = GL_QUADS;
			maxDebugVerts = ( SHADER_MAX_VERTEXES - 1 )/4*4;
			break;
	}

	gl_genericShader->SetVertexSkinning( false );
	gl_genericShader->SetVertexAnimation( false );
	gl_genericShader->SetTCGenEnvironment( false );
	gl_genericShader->SetTCGenLightmap( false );
	gl_genericShader->SetDepthFade( false );
	gl_genericShader->BindProgram( 0 );

	GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	GL_Cull( cullType_t::CT_FRONT_SIDED );

	GL_VertexAttribsState( ATTR_POSITION | ATTR_COLOR | ATTR_TEXCOORD );

	// set uniforms
	gl_genericShader->SetUniform_AlphaTest( GLS_ATEST_NONE );
	gl_genericShader->SetUniform_ColorModulateColorGen( colorGen_t::CGEN_VERTEX, alphaGen_t::AGEN_VERTEX );
	gl_genericShader->SetUniform_Color( colorClear );

	// bind u_ColorMap
	gl_genericShader->SetUniform_ColorMapBindless(
		GL_BindToTMU( 0, tr.whiteImage )
	);
	gl_genericShader->SetUniform_TextureMatrix( matrixIdentity );

	// render in world space
	backEnd.orientation = backEnd.viewParms.world;
	GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );
	gl_genericShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[ glState.stackIndex ] );

	GL_CheckErrors();
}

void DebugDrawDepthMask(bool state)
{
	GL_DepthMask( state ? GL_TRUE : GL_FALSE );
}

void DebugDrawVertex(const vec3_t pos, unsigned int color, const vec2_t uv) {
	Color::Color32Bit colors = {
		static_cast<byte>(color & 0xFF),
		static_cast<byte>((color >> 8) & 0xFF),
		static_cast<byte>((color >> 16) & 0xFF),
		static_cast<byte>((color >> 24) & 0xFF)
	};

	//we have reached the maximum number of verts we can batch
	if( tess.numVertexes == maxDebugVerts ) {
		//draw the geometry we already have
		DebugDrawEnd();
		//start drawing again
		DebugDrawBegin(currentDebugDrawMode, currentDebugSize);
	}

	tess.verts[ tess.numVertexes ].xyz[ 0 ] = pos[ 0 ];
	tess.verts[ tess.numVertexes ].xyz[ 1 ] = pos[ 1 ];
	tess.verts[ tess.numVertexes ].xyz[ 2 ] = pos[ 2 ];
	tess.verts[ tess.numVertexes ].color = colors;
	if( uv ) {
		tess.verts[ tess.numVertexes ].texCoords[ 0 ] = uv[ 0 ];
		tess.verts[ tess.numVertexes ].texCoords[ 1 ] = uv[ 1 ];
	}
	tess.indexes[ tess.numIndexes ] = tess.numVertexes;
	tess.numVertexes++;
	tess.numIndexes++;
}

void DebugDrawEnd() {

	Tess_UpdateVBOs( );
	GL_VertexAttribsState( ATTR_POSITION | ATTR_TEXCOORD | ATTR_COLOR );

	if ( glState.currentVBO && glState.currentIBO )
	{
		uintptr_t base = 0;
		if ( glState.currentIBO == tess.ibo )
		{
			base = tess.indexBase * sizeof(glIndex_t);
		}

		glDrawElements( drawMode, tess.numIndexes, GL_INDEX_TYPE, BUFFER_OFFSET( base ) );
		GL_CheckErrors();

		backEnd.pc.c_drawElements++;

		backEnd.pc.c_vboVertexes += tess.numVertexes;
		backEnd.pc.c_vboIndexes += tess.numIndexes;

		backEnd.pc.c_indexes += tess.numIndexes;
		backEnd.pc.c_vertexes += tess.numVertexes;
	}

	Tess_Clear();
	glLineWidth( 1.0f );
	glPointSize( 1.0f );
}

/*
==================
RB_RenderView
==================
*/
static void RB_RenderView( bool depthPass )
{
	int startTime = 0, endTime = 0;

	if ( r_logFile->integer )
	{
		// don't just call LogComment, or we will get a call to va() every frame!
		GLimp_LogComment( va
		                  ( "--- RB_RenderView( %i surfaces, %i interactions ) ---\n", backEnd.viewParms.numDrawSurfs,
		                    backEnd.viewParms.numInteractions ) );
	}

	GL_CheckErrors();

	backEnd.pc.c_surfaces += backEnd.viewParms.numDrawSurfs;

	// disable offscreen rendering
	R_BindFBO( tr.mainFBO[ backEnd.currentMainFBO ] );

	// we will need to change the projection matrix before drawing
	// 2D images again
	backEnd.projection2D = false;

	// set the modelview matrix for the viewer
	SetViewportAndScissor();

	// ensures that depth writes are enabled for the depth clear
	GL_State( GLS_DEFAULT );

	if ( ( backEnd.refdef.rdflags & RDF_HYPERSPACE ) )
	{
		RB_Hyperspace();

		return;
	}
	else
	{
		backEnd.isHyperspace = false;
	}

	// we will only draw a sun if there was sky rendered in this view
	backEnd.skyRenderedThisView = false;

	GL_CheckErrors();

	if ( r_speeds->integer == Util::ordinal(renderSpeeds_t::RSPEEDS_SHADING_TIMES) )
	{
		glFinish();
		startTime = ri.Milliseconds();
	}

	if( depthPass ) {
		if ( glConfig2.usingMaterialSystem ) {
			materialSystem.RenderMaterials( shaderSort_t::SS_DEPTH, shaderSort_t::SS_DEPTH, backEnd.viewParms.viewID );
		}
		RB_RenderDrawSurfaces( shaderSort_t::SS_DEPTH, shaderSort_t::SS_DEPTH, DRAWSURFACES_ALL );
		RB_RunVisTests();
		if ( !backEnd.postDepthLightTileRendered && !backEnd.viewParms.hasNestedViews ) {
			RB_RenderPostDepthLightTile();
			backEnd.postDepthLightTileRendered = true;
		}
		return;
	}

	if( tr.refdef.blurVec[0] != 0.0f ||
			tr.refdef.blurVec[1] != 0.0f ||
			tr.refdef.blurVec[2] != 0.0f )
	{
		// draw everything that is not the gun
		if ( glConfig2.usingMaterialSystem ) {
			materialSystem.RenderMaterials( shaderSort_t::SS_ENVIRONMENT_FOG, shaderSort_t::SS_OPAQUE, backEnd.viewParms.viewID );
		}
		RB_RenderDrawSurfaces( shaderSort_t::SS_ENVIRONMENT_FOG, shaderSort_t::SS_OPAQUE, DRAWSURFACES_ALL_FAR );

		RB_RenderMotionBlur();

		// draw the gun and other "near" stuff
		RB_RenderDrawSurfaces( shaderSort_t::SS_ENVIRONMENT_FOG, shaderSort_t::SS_OPAQUE, DRAWSURFACES_NEAR_ENTITIES );
	}
	else
	{
		// draw everything that is opaque
		if ( glConfig2.usingMaterialSystem ) {
			materialSystem.RenderMaterials( shaderSort_t::SS_ENVIRONMENT_FOG, shaderSort_t::SS_OPAQUE, backEnd.viewParms.viewID );
		}
		RB_RenderDrawSurfaces( shaderSort_t::SS_ENVIRONMENT_FOG, shaderSort_t::SS_OPAQUE, DRAWSURFACES_ALL );
	}

	if ( r_ssao->integer ) {
		RB_RenderSSAO();
	}

	if ( r_speeds->integer == Util::ordinal(renderSpeeds_t::RSPEEDS_SHADING_TIMES) )
	{
		glFinish();
		endTime = ri.Milliseconds();
		backEnd.pc.c_forwardAmbientTime += endTime - startTime;
	}

	if ( glConfig2.shadowMapping )
	{
		// render dynamic shadowing and lighting using shadow mapping
		RB_RenderInteractionsShadowMapped();
	}
	else
	{
		// render dynamic lighting
		RB_RenderInteractions();
	}

	// render global fog post process effect
	RB_RenderGlobalFog();

	// draw everything that is translucent
	if ( glConfig2.usingMaterialSystem ) {
		materialSystem.RenderMaterials( shaderSort_t::SS_ENVIRONMENT_NOFOG, shaderSort_t::SS_POST_PROCESS, backEnd.viewParms.viewID );
	}
	RB_RenderDrawSurfaces( shaderSort_t::SS_ENVIRONMENT_NOFOG, shaderSort_t::SS_POST_PROCESS, DRAWSURFACES_ALL );

	GL_CheckErrors();

	// render bloom post process effect
	RB_RenderBloom();

	// render debug information
	RB_RenderDebugUtils();

	if ( backEnd.viewParms.portalLevel > 0 )
	{
		if ( r_liquidMapping->integer )
		{
			// capture current color buffer
			// liquid shader will then bind tr.portalRenderImage
			// as u_PortalMap to be read by liquid glsl
			// FIXME: it does not work
			GL_SelectTexture( 0 );
			GL_Bind( tr.portalRenderImage );
			glCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, tr.portalRenderImage->uploadWidth, tr.portalRenderImage->uploadHeight );
		}

		backEnd.pc.c_portals++;
	}

	backEnd.pc.c_views++;
}

/*
==================
RB_RenderPostProcess

Present FBO to screen, and render any effects that must go after the main view and its sub views have been rendered
This is done so various debugging facilities will work properly
==================
*/
static void RB_RenderPostProcess()
{
	if ( glConfig2.usingMaterialSystem && !r_materialSystemSkip.Get() ) {
		// Dispatch the cull compute shaders for queued once we're done with post-processing
		// We'll only use the results from those shaders in the next frame so we don't block the pipeline
		materialSystem.CullSurfaces();
		materialSystem.EndFrame();
	}

	RB_FXAA();

	// render chromatic aberration
	RB_CameraPostFX();

	// copy to given byte buffer that is NOT a FBO
	if ( tr.refdef.pixelTarget != nullptr ) {
		glReadPixels( 0, 0, tr.refdef.pixelTargetWidth, tr.refdef.pixelTargetHeight, GL_RGBA,
			GL_UNSIGNED_BYTE, tr.refdef.pixelTarget );

		for ( int i = 0; i < tr.refdef.pixelTargetWidth * tr.refdef.pixelTargetHeight; i++ ) {
			tr.refdef.pixelTarget[( i * 4 ) + 3] = 255; // Set the alpha to 1.0
		}
	}

	GL_CheckErrors();
}

/*
============================================================================

RENDER BACK END THREAD FUNCTIONS

============================================================================
*/

void RE_UploadCinematic( int cols, int rows, const byte *data, int client, bool dirty )
{
	GL_Bind( tr.cinematicImage[ client ] );

	// if the scratchImage isn't in the format we want, specify it as a new texture
	if ( cols != tr.cinematicImage[ client ]->width || rows != tr.cinematicImage[ client ]->height )
	{
		tr.cinematicImage[ client ]->width = tr.cinematicImage[ client ]->uploadWidth = cols;
		tr.cinematicImage[ client ]->height = tr.cinematicImage[ client ]->uploadHeight = rows;

		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB8, cols, rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, data );

		glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

		glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );
		glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );
		glTexParameterfv( GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, Color::Black.ToArray() );

		// Getting bindless handle makes the texture immutable, so generate it again because we used glTexParameter*
		if ( glConfig2.usingBindlessTextures ) {
			tr.cinematicImage[ client ]->texture->GenBindlessHandle();
		}
	}
	else
	{
		if ( dirty )
		{
			// otherwise, just subimage upload it so that drivers can tell we are going to be changing
			// it and don't try and do a texture compression
			glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, cols, rows, GL_RGBA, GL_UNSIGNED_BYTE, data );
		}
	}

	GL_CheckErrors();
}

/*
=============
RB_SetColor
=============
*/
const RenderCommand *SetColorCommand::ExecuteSelf() const
{
	GLimp_LogComment( "--- SetColorCommand::ExecuteSelf ---\n" );

	backEnd.color2D = color;

	return this + 1;
}

/*
=============
RB_SetColorGrading
=============
*/
const RenderCommand *SetColorGradingCommand::ExecuteSelf( ) const
{
	GLimp_LogComment( "--- SetColorGradingCommand::ExecuteSelf ---\n" );

	if( slot < 0 || slot >= REF_COLORGRADE_SLOTS ) {
		return this + 1;
	}

	if( glState.colorgradeSlots[ slot ] == image ) {
		return this + 1;
	}

	GL_Bind( image );

	glBindBuffer( GL_PIXEL_PACK_BUFFER, tr.colorGradePBO );

	glGetTexImage( image->type, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glBindBuffer( GL_PIXEL_PACK_BUFFER, 0 );

	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, tr.colorGradePBO );

	GL_Bind( tr.colorGradeImage );

	if ( image->width == REF_COLORGRADEMAP_SIZE )
	{
		glTexSubImage3D( GL_TEXTURE_3D, 0, 0, 0, slot * REF_COLORGRADEMAP_SIZE,
		                 REF_COLORGRADEMAP_SIZE, REF_COLORGRADEMAP_SIZE, REF_COLORGRADEMAP_SIZE,
		                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	}
	else
	{
		int i;

		glPixelStorei( GL_UNPACK_ROW_LENGTH, REF_COLORGRADEMAP_SIZE * REF_COLORGRADEMAP_SIZE );

		for ( i = 0; i < 16; i++ )
		{
			glTexSubImage3D( GL_TEXTURE_3D, 0, 0, 0, i + slot * REF_COLORGRADEMAP_SIZE,
			                 REF_COLORGRADEMAP_SIZE, REF_COLORGRADEMAP_SIZE, 1,
			                 GL_RGBA, GL_UNSIGNED_BYTE, BUFFER_OFFSET( sizeof(u8vec4_t) * REF_COLORGRADEMAP_SIZE ) );
		}

		glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
	}

	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );

	glState.colorgradeSlots[ slot ] = image;

	return this + 1;
}

/*
=============
RB_StretchPic
=============
*/
const RenderCommand *StretchPicCommand::ExecuteSelf( ) const
{
	int                       numVerts, numIndexes;

	GLimp_LogComment( "--- StretchPicCommand::ExecuteSelf ---\n" );

	if ( !backEnd.projection2D )
	{
		RB_SetGL2D();
	}

	if ( shader != tess.surfaceShader )
	{
		if ( tess.numIndexes )
		{
			Tess_End();
		}

		backEnd.currentEntity = &backEnd.entity2D;
		Tess_Begin( Tess_StageIteratorColor, shader, nullptr, false, -1, 0 );
	}

	if( !tess.indexes ) {
		Tess_Begin( Tess_StageIteratorColor, shader, nullptr, false, -1, 0 );
	}

	Tess_CheckOverflow( 4, 6 );
	numVerts = tess.numVertexes;
	numIndexes = tess.numIndexes;

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[ numIndexes ] = numVerts + 3;
	tess.indexes[ numIndexes + 1 ] = numVerts + 0;
	tess.indexes[ numIndexes + 2 ] = numVerts + 2;
	tess.indexes[ numIndexes + 3 ] = numVerts + 2;
	tess.indexes[ numIndexes + 4 ] = numVerts + 0;
	tess.indexes[ numIndexes + 5 ] = numVerts + 1;

	tess.verts[ numVerts ].xyz[ 0 ] = x;
	tess.verts[ numVerts ].xyz[ 1 ] = y;
	tess.verts[ numVerts ].xyz[ 2 ] = 0.0f;
	tess.verts[ numVerts + 0 ].color = backEnd.color2D;

	tess.verts[ numVerts ].texCoords[ 0 ] = s1;
	tess.verts[ numVerts ].texCoords[ 1 ] = t1;

	tess.verts[ numVerts + 1 ].xyz[ 0 ] = x + w;
	tess.verts[ numVerts + 1 ].xyz[ 1 ] = y;
	tess.verts[ numVerts + 1 ].xyz[ 2 ] = 0.0f;
	tess.verts[ numVerts + 1 ].color = backEnd.color2D;

	tess.verts[ numVerts + 1 ].texCoords[ 0 ] = s2;
	tess.verts[ numVerts + 1 ].texCoords[ 1 ] = t1;

	tess.verts[ numVerts + 2 ].xyz[ 0 ] = x + w;
	tess.verts[ numVerts + 2 ].xyz[ 1 ] = y + h;
	tess.verts[ numVerts + 2 ].xyz[ 2 ] = 0.0f;
	tess.verts[ numVerts + 2 ].color = backEnd.color2D;

	tess.verts[ numVerts + 2 ].texCoords[ 0 ] = s2;
	tess.verts[ numVerts + 2 ].texCoords[ 1 ] = t2;

	tess.verts[ numVerts + 3 ].xyz[ 0 ] = x;
	tess.verts[ numVerts + 3 ].xyz[ 1 ] = y + h;
	tess.verts[ numVerts + 3 ].xyz[ 2 ] = 0.0f;
	tess.verts[ numVerts + 3 ].color = backEnd.color2D;

	tess.verts[ numVerts + 3 ].texCoords[ 0 ] = s1;
	tess.verts[ numVerts + 3 ].texCoords[ 1 ] = t2;

	return this + 1;
}

const RenderCommand *ScissorSetCommand::ExecuteSelf( ) const {
	tr.scissor.x = x;
	tr.scissor.y = y;
	tr.scissor.w = w;
	tr.scissor.h = h;

	Tess_End();
	GL_Scissor( x, y, w, h );
	tess.surfaceShader = nullptr;

	return this + 1;
}

const RenderCommand *Poly2dCommand::ExecuteSelf( ) const
{
	int                   i;

	if ( !backEnd.projection2D )
	{
		RB_SetGL2D();
	}

	if ( shader != tess.surfaceShader )
	{
		if ( tess.numIndexes )
		{
			Tess_End();
		}

		backEnd.currentEntity = &backEnd.entity2D;
		Tess_Begin( Tess_StageIteratorColor, shader, nullptr, false, -1, 0 );
	}

	Tess_CheckOverflow( numverts, ( numverts - 2 ) * 3 );

	for ( i = 0; i < numverts - 2; i++ )
	{
		tess.indexes[ tess.numIndexes + 0 ] = tess.numVertexes;
		tess.indexes[ tess.numIndexes + 1 ] = tess.numVertexes + i + 1;
		tess.indexes[ tess.numIndexes + 2 ] = tess.numVertexes + i + 2;
		tess.numIndexes += 3;
	}

	for ( i = 0; i < numverts; i++ )
	{
		tess.verts[ tess.numVertexes ].xyz[ 0 ] = verts[ i ].xyz[ 0 ];
		tess.verts[ tess.numVertexes ].xyz[ 1 ] = verts[ i ].xyz[ 1 ];
		tess.verts[ tess.numVertexes ].xyz[ 2 ] = 0.0f;

		tess.verts[ tess.numVertexes ].texCoords[ 0 ] = verts[ i ].st[ 0 ];
		tess.verts[ tess.numVertexes ].texCoords[ 1 ] = verts[ i ].st[ 1 ];

		tess.verts[ tess.numVertexes ].color = Color::Adapt( verts[ i ].modulate );
		tess.numVertexes++;
	}

	return this + 1;
}

const RenderCommand *Poly2dIndexedCommand::ExecuteSelf( ) const
{
	cullType_t            oldCullType;
	int                   i;

	if ( !backEnd.projection2D )
	{
		RB_SetGL2D();
	}

	// HACK: Our shader system likes to cull things that we'd like shown
	oldCullType = shader->cullType;
	shader->cullType = CT_TWO_SIDED;

	if ( shader != tess.surfaceShader )
	{
		if ( tess.numIndexes )
		{
			Tess_End();
		}

		backEnd.currentEntity = &backEnd.entity2D;
		Tess_Begin( Tess_StageIteratorColor, shader, nullptr, false, -1, 0 );
	}

	if( !tess.verts ) {
		Tess_Begin( Tess_StageIteratorColor, shader, nullptr, false, -1, 0 );
	}

	Tess_CheckOverflow( numverts, numIndexes );

	for ( i = 0; i < numIndexes; i++ )
	{
		tess.indexes[ tess.numIndexes + i ] = tess.numVertexes + indexes[ i ];
	}
	tess.numIndexes += numIndexes;
	if ( tr.scissor.status )
	{
		GL_Scissor( tr.scissor.x, tr.scissor.y, tr.scissor.w, tr.scissor.h );
	}

	for ( i = 0; i < numverts; i++ )
	{
		tess.verts[ tess.numVertexes ].xyz[ 0 ] = verts[ i ].xyz[ 0 ] + translation[ 0 ];
		tess.verts[ tess.numVertexes ].xyz[ 1 ] = verts[ i ].xyz[ 1 ] + translation[ 1 ];
		tess.verts[ tess.numVertexes ].xyz[ 2 ] = 0.0f;

		tess.verts[ tess.numVertexes ].texCoords[ 0 ] = verts[ i ].st[ 0 ];
		tess.verts[ tess.numVertexes ].texCoords[ 1 ] = verts[ i ].st[ 1 ];

		tess.verts[ tess.numVertexes ].color = Color::Adapt( verts[ i ].modulate );
		tess.numVertexes++;
	}

	shader->cullType = oldCullType;

	return this + 1;
}

static bool pushed = false;
const RenderCommand *SetMatrixTransformCommand::ExecuteSelf() const {
	Tess_End();
	// HACK: Currently, this is only used for RmlUI. We have a maximum
	// of MAX_GLSTACK matrices we can have on our matrix stack. RmlUI can
	// sometimes push more than 4 causing a crash. However, RmlUI doesn't
	// need the push/pop nature, since it sends premultiplied matrices.
	// Therefore, we only need to push one matrix, and overwrite it every
	// new call, until we get a call to reset, then we pop it.
	if (!pushed)
	{
		GL_PushMatrix();
		pushed = true;
	}
	// Copy the previous project matrix to the current stack.
	MatrixCopy( glState.projectionMatrix[ glState.stackIndex - 1 ],
	            glState.projectionMatrix[ glState.stackIndex ] );
	// Load our new transformation matrix.
	GL_LoadModelViewMatrix( this->matrix );
	tess.surfaceShader = nullptr;
	return this + 1;
}

const RenderCommand *ResetMatrixTransformCommand::ExecuteSelf() const {
	if (!pushed) return this + 1;
	Tess_End();
	GL_PopMatrix();
	pushed = false;
	return this + 1;
}

// NERVE - SMF

/*
=============
RB_RotatedPic
=============
*/
const RenderCommand *RotatedPicCommand::ExecuteSelf( ) const
{
	int                       numVerts, numIndexes;
	float                     mx, my, cosA, sinA, cw, ch, sw, sh;

	if ( !backEnd.projection2D )
	{
		RB_SetGL2D();
	}

	if ( shader != tess.surfaceShader )
	{
		if ( tess.numIndexes )
		{
			Tess_End();
		}

		backEnd.currentEntity = &backEnd.entity2D;
		Tess_Begin( Tess_StageIteratorColor, shader, nullptr, false, -1, 0 );
	}

	if( !tess.indexes ) {
		Tess_Begin( Tess_StageIteratorColor, shader, nullptr, false, -1, 0 );
	}

	Tess_CheckOverflow( 4, 6 );
	numVerts = tess.numVertexes;
	numIndexes = tess.numIndexes;

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[ numIndexes ] = numVerts + 3;
	tess.indexes[ numIndexes + 1 ] = numVerts + 0;
	tess.indexes[ numIndexes + 2 ] = numVerts + 2;
	tess.indexes[ numIndexes + 3 ] = numVerts + 2;
	tess.indexes[ numIndexes + 4 ] = numVerts + 0;
	tess.indexes[ numIndexes + 5 ] = numVerts + 1;

	mx = x + ( w / 2 );
	my = y + ( h / 2 );
	cosA = cosf( DEG2RAD( angle ) );
	sinA = sinf( DEG2RAD( angle ) );
	cw = cosA * ( w / 2 );
	ch = cosA * ( h / 2 );
	sw = sinA * ( w / 2 );
	sh = sinA * ( h / 2 );

	tess.verts[ numVerts ].xyz[ 0 ] = mx - cw - sh;
	tess.verts[ numVerts ].xyz[ 1 ] = my + sw - ch;
	tess.verts[ numVerts ].xyz[ 2 ] = 0.0f;
	tess.verts[ numVerts + 0 ].color = backEnd.color2D;

	tess.verts[ numVerts ].texCoords[ 0 ] = s1;
	tess.verts[ numVerts ].texCoords[ 1 ] = t1;

	tess.verts[ numVerts + 1 ].xyz[ 0 ] = mx + cw - sh;
	tess.verts[ numVerts + 1 ].xyz[ 1 ] = my - sw - ch;
	tess.verts[ numVerts + 1 ].xyz[ 2 ] = 0.0f;
	tess.verts[ numVerts + 1 ].color = backEnd.color2D;

	tess.verts[ numVerts + 1 ].texCoords[ 0 ] = s2;
	tess.verts[ numVerts + 1 ].texCoords[ 1 ] = t1;

	tess.verts[ numVerts + 2 ].xyz[ 0 ] = mx + cw + sh;
	tess.verts[ numVerts + 2 ].xyz[ 1 ] = my - sw + ch;
	tess.verts[ numVerts + 2 ].xyz[ 2 ] = 0.0f;
	tess.verts[ numVerts + 2 ].color = backEnd.color2D;

	tess.verts[ numVerts + 2 ].texCoords[ 0 ] = s2;
	tess.verts[ numVerts + 2 ].texCoords[ 1 ] = t2;

	tess.verts[ numVerts + 3 ].xyz[ 0 ] = mx - cw + sh;
	tess.verts[ numVerts + 3 ].xyz[ 1 ] = my + sw + ch;
	tess.verts[ numVerts + 3 ].xyz[ 2 ] = 0.0f;
	tess.verts[ numVerts + 3 ].color = backEnd.color2D;

	tess.verts[ numVerts + 3 ].texCoords[ 0 ] = s1;
	tess.verts[ numVerts + 3 ].texCoords[ 1 ] = t2;

	return this + 1;
}

// -NERVE - SMF

/*
==============
RB_StretchPicGradient
==============
*/
const RenderCommand *GradientPicCommand::ExecuteSelf( ) const
{
	int                       numVerts, numIndexes;

	if ( !backEnd.projection2D )
	{
		RB_SetGL2D();
	}

	if ( shader != tess.surfaceShader )
	{
		if ( tess.numIndexes )
		{
			Tess_End();
		}

		backEnd.currentEntity = &backEnd.entity2D;
		Tess_Begin( Tess_StageIteratorColor, shader, nullptr, false, -1, 0 );
	}

	if( !tess.indexes ) {
		Tess_Begin( Tess_StageIteratorColor, shader, nullptr, false, -1, 0 );
	}

	Tess_CheckOverflow( 4, 6 );
	numVerts = tess.numVertexes;
	numIndexes = tess.numIndexes;

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[ numIndexes ] = numVerts + 3;
	tess.indexes[ numIndexes + 1 ] = numVerts + 0;
	tess.indexes[ numIndexes + 2 ] = numVerts + 2;
	tess.indexes[ numIndexes + 3 ] = numVerts + 2;
	tess.indexes[ numIndexes + 4 ] = numVerts + 0;
	tess.indexes[ numIndexes + 5 ] = numVerts + 1;

	tess.verts[ numVerts + 0 ].color = backEnd.color2D;
	tess.verts[ numVerts + 1 ].color = backEnd.color2D;
	tess.verts[ numVerts + 2 ].color = gradientColor;
	tess.verts[ numVerts + 3 ].color = gradientColor;

	tess.verts[ numVerts ].xyz[ 0 ] = x;
	tess.verts[ numVerts ].xyz[ 1 ] = y;
	tess.verts[ numVerts ].xyz[ 2 ] = 0.0f;

	tess.verts[ numVerts ].texCoords[ 0 ] = s1;
	tess.verts[ numVerts ].texCoords[ 1 ] = t1;

	tess.verts[ numVerts + 1 ].xyz[ 0 ] = x + w;
	tess.verts[ numVerts + 1 ].xyz[ 1 ] = y;
	tess.verts[ numVerts + 1 ].xyz[ 2 ] = 0.0f;

	tess.verts[ numVerts + 1 ].texCoords[ 0 ] = s2;
	tess.verts[ numVerts + 1 ].texCoords[ 1 ] = t1;

	tess.verts[ numVerts + 2 ].xyz[ 0 ] = x + w;
	tess.verts[ numVerts + 2 ].xyz[ 1 ] = y + h;
	tess.verts[ numVerts + 2 ].xyz[ 2 ] = 0.0f;

	tess.verts[ numVerts + 2 ].texCoords[ 0 ] = s2;
	tess.verts[ numVerts + 2 ].texCoords[ 1 ] = t2;

	tess.verts[ numVerts + 3 ].xyz[ 0 ] = x;
	tess.verts[ numVerts + 3 ].xyz[ 1 ] = y + h;
	tess.verts[ numVerts + 3 ].xyz[ 2 ] = 0.0f;

	tess.verts[ numVerts + 3 ].texCoords[ 0 ] = s1;
	tess.verts[ numVerts + 3 ].texCoords[ 1 ] = t2;

	return this + 1;
}

/*
=============
RB_SetupLights
=============
*/
const RenderCommand *SetupLightsCommand::ExecuteSelf( ) const
{
	int numLights;
	GLenum bufferTarget = glConfig2.uniformBufferObjectAvailable ? GL_UNIFORM_BUFFER : GL_PIXEL_UNPACK_BUFFER;

	GLimp_LogComment( "--- SetupLightsCommand::ExecuteSelf ---\n" );

	if( (numLights = refdef.numLights) > 0 ) {
		shaderLight_t *buffer;

		glBindBuffer( bufferTarget, tr.dlightUBO );
		buffer = (shaderLight_t *)glMapBufferRange( bufferTarget,
							    0, numLights * sizeof( shaderLight_t ),
							    GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT );

		for( int i = 0, j = 0; i < numLights; i++, j++ ) {
			trRefLight_t *light = &refdef.lights[j];

			while( light->l.inverseShadows ) {
				light = &refdef.lights[++j];
			}

			VectorCopy( light->l.origin, buffer[i].center );
			buffer[i].radius = light->l.radius;
			VectorScale( light->l.color, 4.0f * light->l.scale, buffer[i].color );
			buffer[i].type = Util::ordinal( light->l.rlType );
			switch( light->l.rlType ) {
			case refLightType_t::RL_PROJ:
				VectorCopy( light->l.projTarget,
					    buffer[i].direction );
				buffer[i].angle = cosf( atan2f( VectorLength( light->l.projUp), VectorLength( light->l.projTarget ) ) );
				break;
			case refLightType_t::RL_DIRECTIONAL:
				VectorCopy( light->l.projTarget,
					    buffer[i].direction );
				break;
			default:
				break;
			}
		}

		glUnmapBuffer( bufferTarget );
		glBindBuffer( bufferTarget, 0 );
	}

	return this + 1;
}

/*
=============
RB_ClearBuffer
=============
*/
const RenderCommand *ClearBufferCommand::ExecuteSelf( ) const
{
	int clearBits = GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;

	GLimp_LogComment( "--- ClearBufferCommand::ExecuteSelf ---\n" );

	// finish any 2D drawing if needed
	Tess_End();

	backEnd.refdef = refdef;
	backEnd.viewParms = viewParms;
	backEnd.postDepthLightTileRendered = false;

	GL_CheckErrors();

	// sync with gl if needed
	if ( r_finish->integer == 1 && !glState.finishCalled )
	{
		glFinish();
		glState.finishCalled = true;
	}

	if ( r_finish->integer == 0 )
	{
		glState.finishCalled = true;
	}

	// disable offscreen rendering
	R_BindFBO( tr.mainFBO[ backEnd.currentMainFBO ] );

	// we will need to change the projection matrix before drawing
	// 2D images again
	backEnd.projection2D = false;

	// set the modelview matrix for the viewer
	SetViewportAndScissor();

	// ensures that depth writes are enabled for the depth clear
	GL_State( GLS_DEFAULT );

	// Clear relevant buffers, not drawing the sky always require clearing.
	if ( r_clear.Get() || !r_drawSky.Get() ) {
		clearBits |= GL_COLOR_BUFFER_BIT;
	}

	glClear( clearBits );

	return this + 1;
}

/*
=============
RB_PreparePortal
=============
*/
const RenderCommand *PreparePortalCommand::ExecuteSelf( ) const
{
	GLimp_LogComment( "--- PreparePortalCommand::ExecuteSelf ---\n" );

	backEnd.refdef = refdef;
	backEnd.viewParms = viewParms;
	shader_t *shader = surface->shader;

	// set the modelview matrix for the viewer
	SetViewportAndScissor();

	if ( surface->entity != &tr.worldEntity )
	{
		backEnd.currentEntity = surface->entity;

		// set up the transformation matrix
		R_RotateEntityForViewParms( backEnd.currentEntity, &backEnd.viewParms, &backEnd.orientation );
	}
	else
	{
		backEnd.currentEntity = &tr.worldEntity;
		backEnd.orientation = backEnd.viewParms.world;
	}

	GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );

	if ( backEnd.viewParms.portalLevel == 0 ) {
		glEnable( GL_STENCIL_TEST );
		glStencilMask( 0xff );
	}

	glStencilFunc( GL_EQUAL, backEnd.viewParms.portalLevel, 0xff );
	glStencilOp( GL_KEEP, GL_KEEP, GL_INCR );

	GL_State( GLS_COLORMASK_BITS );
	glState.glStateBitsMask = GLS_COLORMASK_BITS;

	Tess_Begin( Tess_StageIteratorPortal, shader, nullptr, false, -1, 0 );
	rb_surfaceTable[Util::ordinal(*(surface->surface))](surface->surface );
	Tess_End();

	glState.glStateBitsMask = 0;

	// set depth to max on portal area
	glDepthRange( 1.0f, 1.0f );

	glStencilFunc( GL_EQUAL, backEnd.viewParms.portalLevel + 1, 0xff );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );

	GL_State( GLS_DEPTHMASK_TRUE | GLS_COLORMASK_BITS | GLS_DEPTHFUNC_ALWAYS);
	glState.glStateBitsMask = GLS_DEPTHMASK_TRUE | GLS_COLORMASK_BITS | GLS_DEPTHFUNC_ALWAYS;

	Tess_Begin( Tess_StageIteratorPortal, shader, nullptr, false, -1, 0 );
	rb_surfaceTable[Util::ordinal(*(surface->surface))](surface->surface );
	Tess_End();

	glState.glStateBitsMask = 0;

	glDepthRange( 0.0f, 1.0f );

	// keep stencil test enabled !

	return this + 1;
}

/*
=============
RB_FinalisePortal
=============
*/
const RenderCommand *FinalisePortalCommand::ExecuteSelf( ) const
{
	GLimp_LogComment( "--- FinalisePortalCommand::ExecuteSelf ---\n" );

	backEnd.refdef = refdef;
	backEnd.viewParms = viewParms;
	shader_t *shader = surface->shader;

	// set the modelview matrix for the viewer
	SetViewportAndScissor();

	if ( surface->entity != &tr.worldEntity )
	{
		backEnd.currentEntity = surface->entity;

		// set up the transformation matrix
		R_RotateEntityForViewParms( backEnd.currentEntity, &backEnd.viewParms, &backEnd.orientation );
	}
	else
	{
		backEnd.currentEntity = &tr.worldEntity;
		backEnd.orientation = backEnd.viewParms.world;
	}

	GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );

	GL_State( GLS_DEPTHMASK_TRUE | GLS_DEPTHFUNC_ALWAYS );
	glState.glStateBitsMask = GLS_DEPTHMASK_TRUE | GLS_DEPTHFUNC_ALWAYS;

	glStencilFunc( GL_EQUAL, backEnd.viewParms.portalLevel + 1, 0xff );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );

	Tess_Begin( Tess_StageIteratorColor, shader,
		nullptr, false, surface->lightmapNum(), surface->fogNum(), surface->bspSurface );
	rb_surfaceTable[Util::ordinal( *( surface->surface ) )]( surface->surface );
	Tess_End();

	glStencilOp( GL_KEEP, GL_KEEP, GL_DECR );

	GL_State( GLS_COLORMASK_BITS | GLS_DEPTHFUNC_ALWAYS);
	glState.glStateBitsMask = GLS_COLORMASK_BITS | GLS_DEPTHFUNC_ALWAYS;

	Tess_Begin( Tess_StageIteratorPortal, shader, nullptr, false, -1, 0 );
	rb_surfaceTable[Util::ordinal(*(surface->surface))](surface->surface );
	Tess_End();

	glStencilFunc( GL_EQUAL, backEnd.viewParms.portalLevel, 0xff );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );

	glState.glStateBitsMask = 0;

	if( backEnd.viewParms.portalLevel == 0 ) {
		glDisable( GL_STENCIL_TEST );
	}

	return this + 1;
}

/*
=============
RB_DrawView
=============
*/
const RenderCommand *DrawViewCommand::ExecuteSelf( ) const
{
	GLimp_LogComment( "--- DrawViewCommand::ExecuteSelf ---\n" );

	// finish any 2D drawing if needed
	Tess_End();

	backEnd.refdef = refdef;
	backEnd.viewParms = viewParms;

	RB_RenderView( depthPass );

	return this + 1;
}

/*
=============
RB_DrawPostProcess
=============
*/
const RenderCommand *RenderPostProcessCommand::ExecuteSelf( ) const
{
	GLimp_LogComment("--- RenderPostProcessCommand::ExecuteSelf ---\n");

	// finish any 3D drawing if needed
	if (tess.numIndexes)
	{
		Tess_End();
	}

	backEnd.refdef = refdef;
	backEnd.viewParms = viewParms;

	RB_RenderPostProcess();

	return this + 1;
}

/*
=============
RB_DrawBuffer
=============
*/
const RenderCommand *DrawBufferCommand::ExecuteSelf( ) const
{
	GLimp_LogComment( "--- DrawBufferCommand::ExecuteSelf ---\n" );

	GL_DrawBuffer( buffer );

	glState.finishCalled = false;
	return this + 1;
}

/*
===============
RB_ShowImages

Draw all the images to the screen, on top of whatever
was there.  This is used to test for texture thrashing.

Also called by RE_EndRegistration
===============
*/
void RB_ShowImages()
{
	image_t *image;
	float   x, y, w, h;
	int     start, end;

	GLimp_LogComment( "--- RB_ShowImages ---\n" );

	if ( !backEnd.projection2D )
	{
		RB_SetGL2D();
	}

	glClear( GL_COLOR_BUFFER_BIT );

	glFinish();

	gl_genericShader->SetVertexSkinning( false );
	gl_genericShader->SetVertexAnimation( false );
	gl_genericShader->SetTCGenEnvironment( false );
	gl_genericShader->SetTCGenLightmap( false );
	gl_genericShader->SetDepthFade( false );
	gl_genericShader->BindProgram( 0 );

	GL_Cull( cullType_t::CT_TWO_SIDED );

	// set uniforms
	gl_genericShader->SetUniform_AlphaTest( GLS_ATEST_NONE );
	gl_genericShader->SetUniform_ColorModulateColorGen( colorGen_t::CGEN_VERTEX, alphaGen_t::AGEN_VERTEX );
	gl_genericShader->SetUniform_TextureMatrix( matrixIdentity );

	GL_SelectTexture( 0 );

	start = ri.Milliseconds();

	matrix_t ortho;
	GL_PushMatrix();
	MatrixOrthogonalProjection( ortho, backEnd.viewParms.viewportX,
		backEnd.viewParms.viewportX + backEnd.viewParms.viewportWidth,
		backEnd.viewParms.viewportY,
		backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight, -99999, 99999 );
	GL_LoadProjectionMatrix( ortho );

	for ( size_t i = 0; i < tr.images.size(); i++ )
	{
		image = tr.images[ i ];

		/*
		   if(image->bits & (IF_RGBA16F | IF_RGBA32F | IF_LA16F | IF_LA32F))
		   {
		   // don't render float textures using FFP
		   continue;
		   }
		 */

		w = glConfig.vidWidth / 20;
		h = glConfig.vidHeight / 15;
		x = i % 20 * w;
		y = i / 20 * h;

		// show in proportional size in mode 2
		if ( r_showImages->integer == 2 )
		{
			w *= image->uploadWidth / 512.0f;
			h *= image->uploadHeight / 512.0f;
		}

		// bind u_ColorMap
		gl_genericShader->SetUniform_ColorMapBindless(
			GL_BindToTMU( 0, image )
		);

		Tess_InstantQuad( *gl_genericShader, x, y, w, h );
	}

	GL_PopMatrix();

	glFinish();

	end = ri.Milliseconds();
	Log::Debug("%i msec to draw all images", end - start );

	GL_CheckErrors();
}

/*
=============
RB_SwapBuffers
=============
*/
const RenderCommand *SwapBuffersCommand::ExecuteSelf( ) const
{
	// finish any 2D drawing if needed
	Tess_End();

	// texture swapping test
	if ( r_showImages->integer )
	{
		RB_ShowImages();
	}

	GLimp_LogComment( "***************** RB_SwapBuffers *****************\n\n\n" );

	GLimp_EndFrame();

	backEnd.projection2D = false;

	return this + 1;
}

/*
=============
R_ShutdownBackend
=============
*/
void R_ShutdownBackend()
{
	int i;

	for ( i = 0; i < ATTR_INDEX_MAX; i++ )
	{
		glDisableVertexAttribArray( i );
	}
	glState.vertexAttribsState = 0;
}

const RenderCommand *EndOfListCommand::ExecuteSelf( ) const
{
	return nullptr;
}

/*
====================
RB_ExecuteRenderCommands

This function will be called synchronously if running without
smp extensions, or asynchronously by another thread.
====================
*/
void RB_ExecuteRenderCommands( const void *data )
{
	const RenderCommand *cmd = (const RenderCommand *)data;
	int t1, t2;

	GLimp_LogComment( "--- RB_ExecuteRenderCommands ---\n" );

	t1 = ri.Milliseconds();

	if ( !r_smp->integer || data == backEndData[ 0 ]->commands.cmds )
	{
		backEnd.smpFrame = 0;
	}
	else
	{
		backEnd.smpFrame = 1;
	}


	materialSystem.frameStart = true;
	while ( cmd != nullptr )
	{
		cmd = cmd->ExecuteSelf();
	}

	// stop rendering on this thread
	t2 = ri.Milliseconds();
	backEnd.pc.msec = t2 - t1;
	return;
}

/*
================
RB_RenderThread
================
*/
void RB_RenderThread()
{
	const void *data;

	// wait for either a rendering command or a quit command
	while ( true )
	{
		// sleep until we have work to do
		data = GLimp_RendererSleep();

		if ( !data )
		{
			return; // all done, renderer is shutting down
		}

		renderThreadActive = true;

		RB_ExecuteRenderCommands( data );

		renderThreadActive = false;
	}
}
