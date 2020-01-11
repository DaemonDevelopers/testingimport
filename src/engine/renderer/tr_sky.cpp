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
// tr_sky.c
#include "tr_local.h"
#include "gl_shader.h"

//======================================================================================

/*
================
Tess_StageIteratorSky

All of the visible sky triangles are in tess

Other things could be stuck in here, like birds in the sky, etc
================
*/
void Tess_StageIteratorSky()
{
	// log this call
	if ( r_logFile->integer )
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va
		                  ( "--- Tess_StageIteratorSky( %s, %i vertices, %i triangles ) ---\n", tess.surfaceShader->name,
		                    tess.numVertexes, tess.numIndexes / 3 ) );
	}

	tr.drawingSky = false;

	if ( !r_drawSky.Get() )
	{
		return;
	}

	// Store the current projection, modelView and modelViewProjection matrices if we're drawing the skybox in a portal
	// since portals set up the projection matrix differently
	matrix_t currentProjectionMatrix;
	matrix_t currentModelViewMatrix;
	matrix_t currentModelViewProjectionMatrix;
	if ( backEnd.viewParms.portalLevel > 0 ) {
		MatrixCopy( glState.projectionMatrix[glState.stackIndex], currentProjectionMatrix );
		MatrixCopy( glState.modelViewMatrix[glState.stackIndex], currentModelViewMatrix );
		MatrixCopy( glState.modelViewProjectionMatrix[glState.stackIndex], currentModelViewProjectionMatrix );
		GL_LoadProjectionMatrix( backEnd.viewParms.projectionMatrixNonPortal );
	}
	
	// Compute the skyboxes orientation to center it on view origin
	orientationr_t skyboxOrientation;
	VectorCopy( backEnd.viewParms.orientation.origin, skyboxOrientation.origin );
	AxisCopy( backEnd.viewParms.world.axis, skyboxOrientation.axis );

	MatrixSetupTransformFromVectorsFLU( skyboxOrientation.transformMatrix, skyboxOrientation.axis[0], skyboxOrientation.axis[1],
										skyboxOrientation.axis[2], skyboxOrientation.origin );
	MatrixAffineInverse( skyboxOrientation.transformMatrix, skyboxOrientation.viewMatrix );
	MatrixMultiply( backEnd.viewParms.world.viewMatrix, skyboxOrientation.transformMatrix, skyboxOrientation.modelViewMatrix );

	GL_LoadModelViewMatrix( skyboxOrientation.modelViewMatrix );

	GL_Cull(cullType_t::CT_BACK_SIDED);

	// r_showSky will draw the whole skybox in front of everything else
	if ( r_showSky->integer )
	{
		glDepthRange( 0.0, 0.0 );
	}
	else
	{
		glDepthRange( 1.0, 1.0 );
	}

	// Setup tess for rendering
	tess.multiDrawPrimitives = 0;
	tess.numIndexes = 0;
	tess.numVertexes = 0;

	rb_surfaceTable[Util::ordinal( *( tr.skybox->surface ) )]( tr.skybox->surface );
	GL_VertexAttribsState( ATTR_POSITION );

	gl_skyboxShader->BindProgram( 0 );

	gl_skyboxShader->SetUniform_ModelViewProjectionMatrix( glState.modelViewProjectionMatrix[glState.stackIndex] );

	// u_LinearizeTexture
	gl_skyboxShader->SetUniform_LinearizeTexture( tr.worldLinearizeTexture );

	gl_skyboxShader->SetRequiredVertexPointers();

	// draw the outer skybox
	if ( tess.surfaceShader->sky.outerbox )
	{
		GL_State( GLS_DEFAULT );

		// bind u_ColorMap
		gl_skyboxShader->SetUniform_ColorMapCubeBindless(
			GL_BindToTMU( 0, tess.surfaceShader->sky.outerbox )
		);

		// Only render the outer skybox at this stage
		gl_skyboxShader->SetUniform_UseCloudMap( false );

		// u_AlphaThreshold
		gl_skyboxShader->SetUniform_AlphaTest( GLS_ATEST_NONE );

		Tess_DrawElements();
	}

	if ( r_showTris->integer ) {
		GL_State( GLS_POLYMODE_LINE | GLS_DEPTHFUNC_ALWAYS );

		// bind u_ColorMap
		gl_skyboxShader->SetUniform_ColorMapCubeBindless(
			GL_BindToTMU( 0, tr.whiteCubeImage )
		);

		// Only render the outer skybox outline at this stage
		gl_skyboxShader->SetUniform_UseCloudMap( false );

		// u_AlphaThreshold
		gl_skyboxShader->SetUniform_AlphaTest( GLS_ATEST_NONE );

		Tess_DrawElements();
	}

	// Only render clouds at these stages
	gl_skyboxShader->SetUniform_UseCloudMap( true );
	gl_skyboxShader->SetUniform_CloudHeight( tess.surfaceShader->sky.cloudHeight );

	for ( shaderStage_t *pStage = tess.surfaceStages; pStage < tess.surfaceLastStage; pStage++ )
	{
		if ( !RB_EvalExpression( &pStage->ifExp, 1.0 ) ) {
			continue;
		}

		Tess_ComputeTexMatrices( pStage );

		gl_skyboxShader->SetUniform_TextureMatrix( tess.svars.texMatrices[TB_COLORMAP] );

		gl_skyboxShader->SetUniform_CloudMapBindless(
			GL_BindToTMU( 1, pStage->bundle[TB_COLORMAP].image[0] )
		);

		// u_AlphaThreshold
		gl_skyboxShader->SetUniform_AlphaTest( pStage->stateBits );

		// u_LinearizeTexture
		gl_skyboxShader->SetUniform_LinearizeTexture( tr.worldLinearizeTexture );

		GL_State( pStage->stateBits );

		switch ( pStage->type ) {
			case stageType_t::ST_COLORMAP:
				Tess_DrawElements();
				break;

			default:
				break;
		}
	}

	// back to standard depth range
	glDepthRange( 0.0, 1.0 );

	// note that sky was drawn so we will draw a sun later
	backEnd.skyRenderedThisView = true;

	// Restore matrices if we're rendering in a portal
	if ( backEnd.viewParms.portalLevel > 0 ) {
		MatrixCopy( currentProjectionMatrix, glState.projectionMatrix[glState.stackIndex] );
		MatrixCopy( currentModelViewMatrix, glState.modelViewMatrix[glState.stackIndex] );
		MatrixCopy( currentModelViewProjectionMatrix, glState.modelViewProjectionMatrix[glState.stackIndex] );
		return;
	}

	GL_LoadModelViewMatrix( backEnd.orientation.modelViewMatrix );
}
