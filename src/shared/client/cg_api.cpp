/*
===========================================================================
Daemon BSD Source Code
Copyright (c) 2013-2016, Daemon Developers
All rights reserved.

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

#include <engine/client/cg_msgdef.h>
#include <shared/VMMain.h>
#include <shared/CommandBufferClient.h>
#include "cg_api.h"

IPC::CommandBufferClient cmdBuffer("cgame");

// Definition of the VM->Engine calls

// All Miscs

void trap_SendClientCommand( const char *s )
{
	VM::SendMsg<SendClientCommandMsg>(s);
}

void trap_UpdateScreen()
{
	VM::SendMsg<UpdateScreenMsg>();
}

void trap_CM_BatchMarkFragments(
	unsigned maxPoints, //per mark
	unsigned maxFragments, //per mark
	const std::vector<markMsgInput_t> &markMsgInput,
	std::vector<markMsgOutput_t> &markMsgOutput )
{
	if (!markMsgInput.empty())
	{
		VM::SendMsg<CMBatchMarkFragments>(maxPoints, maxFragments, markMsgInput, markMsgOutput);
	}
}

void trap_GetCurrentSnapshotNumber( int *snapshotNumber, int *serverTime )
{
	VM::SendMsg<GetCurrentSnapshotNumberMsg>(*snapshotNumber, *serverTime);
}

bool trap_GetSnapshot( int snapshotNumber, ipcSnapshot_t *snapshot )
{
	bool res;
	VM::SendMsg<GetSnapshotMsg>(snapshotNumber, res, *snapshot);
	return res;
}

int trap_GetCurrentCmdNumber()
{
	int res;
	VM::SendMsg<GetCurrentCmdNumberMsg>(res);
	return res;
}

// Use a local cache to reduce IPC traffic. We assume that the user command number never decreases as
// the command number only resets on an svc_gamestate command, and this command is only sent once
// per cgame lifetime when the user enters the game (it's not even used for map_restart).
bool trap_GetUserCmd( int cmdNumber, usercmd_t *ucmd )
{
	static usercmd_t cache[ CMD_BACKUP ];
	static int latestInCache = -1;

	if ( cmdNumber <= latestInCache - CMD_BACKUP )
	{
		// Either the cgame is buggy and trying to request really old stuff, or there is some case
		// of additional calls to CL_ClearState that I didn't know about. Reset the cache in case
		// it's legit.
		Log::Warn( "Unexpectedly old command number requested in trap_GetUserCmd" );
		latestInCache = -1;
	}
	else if ( cmdNumber <= latestInCache )
	{
		*ucmd = cache[ cmdNumber & CMD_MASK ];
		return true;
	}

	bool res;
	VM::SendMsg<GetUserCmdMsg>(cmdNumber, res, *ucmd);
	if ( res )
	{
		latestInCache = cmdNumber;
		cache[ cmdNumber & CMD_MASK ] = *ucmd;
	}
	return res;
}

void trap_SetUserCmdValue( int stateValue, int flags, float sensitivityScale )
{
	VM::SendMsg<SetUserCmdValueMsg>(stateValue, flags, sensitivityScale);
}

void trap_RegisterButtonCommands( const char *cmds )
{
	VM::SendMsg<RegisterButtonCommandsMsg>(cmds);
}

void trap_notify_onTeamChange( int newTeam )
{
	VM::SendMsg<NotifyTeamChangeMsg>(newTeam);
}

void trap_PrepareKeyUp()
{
	VM::SendMsg<PrepareKeyUpMsg>();
}

// All Sounds

void trap_S_StartSound( vec3_t origin, int entityNum, soundChannel_t, sfxHandle_t sfx )
{
    Vec3 myorigin = Vec3(0.0f, 0.0f, 0.0f);
	if (origin) {
        myorigin = Vec3::Load(origin);
	}
	cmdBuffer.SendMsg<Audio::StartSoundMsg>(!!origin, myorigin, entityNum, sfx);
}

void trap_S_StartLocalSound( sfxHandle_t sfx, soundChannel_t )
{
	cmdBuffer.SendMsg<Audio::StartLocalSoundMsg>(sfx);
}

void trap_S_ClearLoopingSounds( bool )
{
	cmdBuffer.SendMsg<Audio::ClearLoopingSoundsMsg>();
}

void trap_S_AddLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx )
{
	if (origin) {
		trap_S_UpdateEntityPosition(entityNum, origin);
	}
	if (velocity) {
		trap_S_UpdateEntityVelocity(entityNum, velocity);
	}
	cmdBuffer.SendMsg<Audio::AddLoopingSoundMsg>(entityNum, sfx);
}

void trap_S_AddRealLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx )
{
	trap_S_AddLoopingSound(entityNum, origin, velocity, sfx);
}

void trap_S_StopLoopingSound( int entityNum )
{
	cmdBuffer.SendMsg<Audio::StopLoopingSoundMsg>(entityNum);
}

void trap_S_UpdateEntityPosition( int entityNum, const vec3_t origin )
{
	cmdBuffer.SendMsg<Audio::UpdateEntityPositionMsg>(entityNum, Vec3::Load(origin));
}

void trap_S_Respatialize( int entityNum, const vec3_t origin, vec3_t axis[ 3 ], int )
{
	if (origin) {
		trap_S_UpdateEntityPosition(entityNum, origin);
	}
	std::array<Vec3, 3> myaxis;
    myaxis[0] = Vec3::Load(axis[0]);
    myaxis[1] = Vec3::Load(axis[1]);
    myaxis[2] = Vec3::Load(axis[2]);
	cmdBuffer.SendMsg<Audio::RespatializeMsg>(entityNum, myaxis);
}

sfxHandle_t trap_S_RegisterSound( const char *sample, bool)
{
	int sfx;
	VM::SendMsg<Audio::RegisterSoundMsg>(sample, sfx);
	return sfx;
}

void trap_S_StartBackgroundTrack( const char *intro, const char *loop )
{
	cmdBuffer.SendMsg<Audio::StartBackgroundTrackMsg>(intro, loop);
}

void trap_S_StopBackgroundTrack()
{
	cmdBuffer.SendMsg<Audio::StopBackgroundTrackMsg>();
}

void trap_S_UpdateEntityVelocity( int entityNum, const vec3_t velocity )
{
	cmdBuffer.SendMsg<Audio::UpdateEntityVelocityMsg>(entityNum, Vec3::Load(velocity));
}

void trap_S_UpdateEntityPositionVelocity( int entityNum, const vec3_t origin, const vec3_t velocity )
{
	cmdBuffer.SendMsg<Audio::UpdateEntityPositionVelocityMsg>(entityNum, Vec3::Load(origin), Vec3::Load(velocity));
}

void trap_S_SetReverb( int slotNum, const char* name, float ratio )
{
	cmdBuffer.SendMsg<Audio::SetReverbMsg>(slotNum, name, ratio);
}

void trap_S_BeginRegistration()
{
	cmdBuffer.SendMsg<Audio::BeginRegistrationMsg>();
}

void trap_S_EndRegistration()
{
	cmdBuffer.SendMsg<Audio::EndRegistrationMsg>();
}

// All renderer

void trap_R_SetAltShaderTokens( const char *str )
{
	VM::SendMsg<Render::SetAltShaderTokenMsg>(str);
}

void trap_R_GetShaderNameFromHandle( const qhandle_t shader, char *out, int len )
{
	std::string result;
	VM::SendMsg<Render::GetShaderNameFromHandleMsg>(shader, result);
	Q_strncpyz(out, result.c_str(), len);
}

void trap_R_ScissorEnable( bool enable )
{
    cmdBuffer.SendMsg<Render::ScissorEnableMsg>(enable);
}

void trap_R_ScissorSet( int x, int y, int w, int h )
{
	cmdBuffer.SendMsg<Render::ScissorSetMsg>(x, y, w, h);
}

void trap_R_LoadWorldMap( const char *mapname )
{
	VM::SendMsg<Render::LoadWorldMapMsg>(mapname);
}

qhandle_t trap_R_RegisterModel( const char *name )
{
	int handle;
	VM::SendMsg<Render::RegisterModelMsg>(name, handle);
	return handle;
}

qhandle_t trap_R_RegisterSkin( const char *name )
{
	int handle;
	VM::SendMsg<Render::RegisterSkinMsg>(name, handle);
	return handle;
}

qhandle_t trap_R_RegisterShader( const char *name, int flags )
{
	int handle;
	VM::SendMsg<Render::RegisterShaderMsg>(name, flags, handle);
	return handle;
}

void trap_R_ClearScene()
{
	cmdBuffer.SendMsg<Render::ClearSceneMsg>();
}

void trap_R_AddRefEntityToScene( const refEntity_t *re )
{
	cmdBuffer.SendMsg<Render::AddRefEntityToSceneMsg>(*re);
}

void trap_R_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts )
{
	if (!numVerts)
	{
		return;
	}

	std::vector<polyVert_t> myverts(verts, verts + numVerts);
	cmdBuffer.SendMsg<Render::AddPolyToSceneMsg>(hShader, myverts);
}

void trap_R_AddPolysToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts, int numPolys )
{
	size_t size = numVerts * numPolys;

	if (!size)
	{
		return;
	}

	std::vector<polyVert_t> myverts(verts, verts + size);

	cmdBuffer.SendMsg<Render::AddPolysToSceneMsg>(hShader, myverts, numVerts, numPolys);
}

void trap_R_Add2dPolysIndexedToScene( const polyVert_t* polys, int numPolys, const int* indexes, int numIndexes, int trans_x, int trans_y, qhandle_t shader )
{
	if (!numIndexes)
	{
		return;
	}

	std::vector<polyVert_t> mypolys(polys, polys + numPolys);
	std::vector<int> myindices(indexes, indexes + numIndexes);
	cmdBuffer.SendMsg<Render::Add2dPolysIndexedMsg>(mypolys, numPolys, myindices, numIndexes, trans_x, trans_y, shader);
}

// Used exclusively for RmlUI's transformations. Other usecases might
// not work as expected.
void trap_R_SetMatrixTransform( const matrix_t matrix )
{
	std::array<float, 16> mymatrix;
	MatrixCopy(matrix, mymatrix.data());
	cmdBuffer.SendMsg<Render::SetMatrixTransformMsg>(mymatrix);
}

// Used exclusively for RmlUI's transformations. Other usecases might
// not work as expected.
void trap_R_ResetMatrixTransform()
{
	cmdBuffer.SendMsg<Render::ResetMatrixTransformMsg>();
}

void trap_R_AddLightToScene( const vec3_t origin, float radius, float intensity, float r, float g, float b, int flags )
{
	std::array<float, 3> myorg;
	VectorCopy( origin, myorg );
	cmdBuffer.SendMsg<Render::AddLightToSceneMsg>(myorg, radius, r * intensity, g * intensity, b * intensity, flags);
}

void trap_R_RenderScene( const refdef_t *fd )
{
	cmdBuffer.SendMsg<Render::RenderSceneMsg>(*fd);
}

void trap_R_ClearColor()
{
	cmdBuffer.SendMsg<Render::SetColorMsg>(Color::White);
}
void trap_R_SetColor( const Color::Color &rgba )
{
	cmdBuffer.SendMsg<Render::SetColorMsg>(rgba);
}
void trap_R_SetClipRegion( const float *region )
{
	std::array<float, 4> myregion;
	Vector4Copy(region, myregion);
	cmdBuffer.SendMsg<Render::SetClipRegionMsg>(myregion);
}

void trap_R_ResetClipRegion()
{
	cmdBuffer.SendMsg<Render::ResetClipRegionMsg>();
}

void trap_R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader )
{
	cmdBuffer.SendMsg<Render::DrawStretchPicMsg>(x, y, w, h, s1, t1, s2, t2, hShader);
}

void trap_R_DrawRotatedPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader, float angle )
{
	cmdBuffer.SendMsg<Render::DrawRotatedPicMsg>(x, y, w, h, s1, t1, s2, t2, hShader, angle);
}

void trap_R_ModelBounds( clipHandle_t model, vec3_t mins, vec3_t maxs )
{
	std::array<float, 3> mymins, mymaxs;
	VM::SendMsg<Render::ModelBoundsMsg>(model, mymins, mymaxs);
	VectorCopy(mymins, mins);
	VectorCopy(mymaxs, maxs);
}

int trap_R_LerpTag( orientation_t *tag, const refEntity_t *refent, const char *tagName, int startIndex )
{
	int result;
	VM::SendMsg<Render::LerpTagMsg>(*refent, tagName, startIndex, *tag, result);
	return result;
}

void trap_R_RemapShader( const char *oldShader, const char *newShader, const char *timeOffset )
{
	VM::SendMsg<Render::RemapShaderMsg>(oldShader, newShader, timeOffset);
}

std::vector<bool> trap_R_BatchInPVS(
	const vec3_t origin,
	const std::vector<std::array<float, 3>>& posEntities )
{
	std::array<float, 3> myOrigin;
	VectorCopy(origin, myOrigin);
	std::vector<bool> inPVS;
	VM::SendMsg<Render::BatchInPVSMsg>(myOrigin, posEntities, inPVS);
	return inPVS;
}

int trap_R_LightForPoint( vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir )
{
	int result;
	std::array<float, 3> mypoint, myambient, mydirected, mydir;
	VectorCopy(point, mypoint);
	VM::SendMsg<Render::LightForPointMsg>(mypoint, myambient, mydirected, mydir, result);
	VectorCopy(myambient, ambientLight);
	VectorCopy(mydirected, directedLight);
	VectorCopy(mydir, lightDir);
	return result;
}

qhandle_t trap_R_RegisterAnimation( const char *name )
{
	int handle;
	VM::SendMsg<Render::RegisterAnimationMsg>(name, handle);
	return handle;
}

skelAnimation_t trap_R_GetAnimation( qhandle_t anim ) {
	skelAnimation_t result;
	VM::SendMsg<Render::GetAnimationMsg>( anim, result );
	return result;
}

std::vector<skelAnimation_t> trap_R_BatchGetAnimations( const std::vector<qhandle_t>& anims ) {
	std::vector<skelAnimation_t> skelAnimations;
	VM::SendMsg<Render::BatchGetAnimationsMsg>( anims, skelAnimations );
	return skelAnimations;
}

static int IQMBuildSkeleton( refSkeleton_t* skel, skelAnimation_t* skelAnim,
	int startFrame, int endFrame, float frac ) {
	int            i;
	IQAnim_t* anim;
	transform_t* newPose, * oldPose;
	vec3_t         mins, maxs;

	anim = skelAnim->iqm;

	// Validate the frames so there is no chance of a crash.
	// This will write directly into the entity structure, so
	// when the surfaces are rendered, they don't need to be
	// range checked again.
	if ( anim->flags & IQM_LOOP ) {
		startFrame %= anim->num_frames;
		endFrame %= anim->num_frames;
	} else {
		startFrame = Math::Clamp( startFrame, 0, anim->num_frames - 1 );
		endFrame = Math::Clamp( endFrame, 0, anim->num_frames - 1 );
	}

	// compute frame pointers
	oldPose = &anim->poses[startFrame * anim->num_joints];
	newPose = &anim->poses[endFrame * anim->num_joints];

	// calculate a bounding box in the current coordinate system
	if ( anim->bounds ) {
		float* bounds = &anim->bounds[6 * startFrame];
		VectorCopy( bounds, mins );
		VectorCopy( bounds + 3, maxs );

		bounds = &anim->bounds[6 * endFrame];
		BoundsAdd( mins, maxs, bounds, bounds + 3 );
	}

#if defined( REFBONE_NAMES )
	const char* boneNames = anim->jointNames;
#endif
	for ( i = 0; i < anim->num_joints; i++ ) {
		TransStartLerp( &skel->bones[i].t );
		TransAddWeight( 1.0f - frac, &oldPose[i], &skel->bones[i].t );
		TransAddWeight( frac, &newPose[i], &skel->bones[i].t );
		TransEndLerp( &skel->bones[i].t );

#if defined( REFBONE_NAMES )
		Q_strncpyz( skel->bones[i].name, boneNames, sizeof( skel->bones[i].name ) );
		boneNames += strlen( boneNames ) + 1;
#endif

		skel->bones[i].parentIndex = anim->jointParents[i];
	}

	skel->numBones = anim->num_joints;
	skel->type = refSkeletonType_t::SK_RELATIVE;
	VectorCopy( mins, skel->bounds[0] );
	VectorCopy( maxs, skel->bounds[1] );
	return true;
}

static int BuildSkeleton( refSkeleton_t* skel, skelAnimation_t* skelAnim, int startFrame, int endFrame, float frac, bool clearOrigin ) {
	if ( skelAnim->type == animType_t::AT_IQM && skelAnim->iqm ) {
		return IQMBuildSkeleton( skel, skelAnim, startFrame, endFrame, frac );
	} else if ( skelAnim->type == animType_t::AT_MD5 && skelAnim->md5 ) {
		int            i;
		md5Animation_t* anim;
		md5Channel_t* channel;
		md5Frame_t* newFrame, * oldFrame;
		vec3_t         newOrigin, oldOrigin, lerpedOrigin;
		quat_t         newQuat, oldQuat, lerpedQuat;
		int            componentsApplied;

		anim = skelAnim->md5;

		// Validate the frames so there is no chance of a crash.
		// This will write directly into the entity structure, so
		// when the surfaces are rendered, they don't need to be
		// range checked again.

		/*
		   if((startFrame >= anim->numFrames) || (startFrame < 0) || (endFrame >= anim->numFrames) || (endFrame < 0))
		   {
		   Log::Debug("RE_BuildSkeleton: no such frame %d to %d for '%s'", startFrame, endFrame, anim->name);
		   //startFrame = 0;
		   //endFrame = 0;
		   }
		 */

		startFrame = Math::Clamp( startFrame, 0, anim->numFrames - 1 );
		endFrame = Math::Clamp( endFrame, 0, anim->numFrames - 1 );

		// compute frame pointers
		oldFrame = &anim->frames[startFrame];
		newFrame = &anim->frames[endFrame];

		// calculate a bounding box in the current coordinate system
		for ( i = 0; i < 3; i++ ) {
			skel->bounds[0][i] =
				oldFrame->bounds[0][i] < newFrame->bounds[0][i] ? oldFrame->bounds[0][i] : newFrame->bounds[0][i];
			skel->bounds[1][i] =
				oldFrame->bounds[1][i] > newFrame->bounds[1][i] ? oldFrame->bounds[1][i] : newFrame->bounds[1][i];
		}

		for ( i = 0, channel = anim->channels; i < anim->numChannels; i++, channel++ ) {
			// set baseframe values
			VectorCopy( channel->baseOrigin, newOrigin );
			VectorCopy( channel->baseOrigin, oldOrigin );

			QuatCopy( channel->baseQuat, newQuat );
			QuatCopy( channel->baseQuat, oldQuat );

			componentsApplied = 0;

			// update tranlation bits
			if ( channel->componentsBits & COMPONENT_BIT_TX ) {
				oldOrigin[0] = oldFrame->components[channel->componentsOffset + componentsApplied];
				newOrigin[0] = newFrame->components[channel->componentsOffset + componentsApplied];
				componentsApplied++;
			}

			if ( channel->componentsBits & COMPONENT_BIT_TY ) {
				oldOrigin[1] = oldFrame->components[channel->componentsOffset + componentsApplied];
				newOrigin[1] = newFrame->components[channel->componentsOffset + componentsApplied];
				componentsApplied++;
			}

			if ( channel->componentsBits & COMPONENT_BIT_TZ ) {
				oldOrigin[2] = oldFrame->components[channel->componentsOffset + componentsApplied];
				newOrigin[2] = newFrame->components[channel->componentsOffset + componentsApplied];
				componentsApplied++;
			}

			// update quaternion rotation bits
			if ( channel->componentsBits & COMPONENT_BIT_QX ) {
				( ( vec_t* ) oldQuat )[0] = oldFrame->components[channel->componentsOffset + componentsApplied];
				( ( vec_t* ) newQuat )[0] = newFrame->components[channel->componentsOffset + componentsApplied];
				componentsApplied++;
			}

			if ( channel->componentsBits & COMPONENT_BIT_QY ) {
				( ( vec_t* ) oldQuat )[1] = oldFrame->components[channel->componentsOffset + componentsApplied];
				( ( vec_t* ) newQuat )[1] = newFrame->components[channel->componentsOffset + componentsApplied];
				componentsApplied++;
			}

			if ( channel->componentsBits & COMPONENT_BIT_QZ ) {
				( ( vec_t* ) oldQuat )[2] = oldFrame->components[channel->componentsOffset + componentsApplied];
				( ( vec_t* ) newQuat )[2] = newFrame->components[channel->componentsOffset + componentsApplied];
			}

			QuatCalcW( oldQuat );
			QuatNormalize( oldQuat );

			QuatCalcW( newQuat );
			QuatNormalize( newQuat );

#if 1
			VectorLerp( oldOrigin, newOrigin, frac, lerpedOrigin );
			QuatSlerp( oldQuat, newQuat, frac, lerpedQuat );
#else
			VectorCopy( newOrigin, lerpedOrigin );
			QuatCopy( newQuat, lerpedQuat );
#endif

			// copy lerped information to the bone + extra data
			skel->bones[i].parentIndex = channel->parentIndex;

			if ( channel->parentIndex < 0 && clearOrigin ) {
				VectorClear( skel->bones[i].t.trans );
				QuatClear( skel->bones[i].t.rot );

				// move bounding box back
				VectorSubtract( skel->bounds[0], lerpedOrigin, skel->bounds[0] );
				VectorSubtract( skel->bounds[1], lerpedOrigin, skel->bounds[1] );
			} else {
				VectorCopy( lerpedOrigin, skel->bones[i].t.trans );
			}

			QuatCopy( lerpedQuat, skel->bones[i].t.rot );
			skel->bones[i].t.scale = 1.0f;

#if defined( REFBONE_NAMES )
			Q_strncpyz( skel->bones[i].name, channel->name, sizeof( skel->bones[i].name ) );
#endif
		}

		skel->numBones = anim->numChannels;
		skel->type = refSkeletonType_t::SK_RELATIVE;
		return true;
	}

	// FIXME: clear existing bones and bounds?
	return false;
}

static int BuildSkeleton( refSkeleton_t* skel, qhandle_t anim, int startFrame, int endFrame, float frac, bool clearOrigin ) {
	skelAnimation_t skelAnimation = trap_R_GetAnimation( anim );
	BuildSkeleton( skel, &skelAnimation, startFrame, endFrame, frac, clearOrigin);
}

int trap_R_BuildSkeleton( refSkeleton_t *skel, qhandle_t anim, int startFrame, int endFrame, float frac, bool clearOrigin )
{
	int result;
	VM::SendMsg<Render::BuildSkeletonMsg>(anim, startFrame, endFrame, frac, clearOrigin, *skel, result);
	return result;
}

int trap_R_BuildSkeleton2( refSkeleton_t* skel, skelAnimation_t* anim, int startFrame, int endFrame, float frac, bool clearOrigin ) {
	int result;
	result = BuildSkeleton( skel, anim, startFrame, endFrame, frac, clearOrigin );
	return result;
}

// Shamelessly stolen from tr_animation.cpp
int trap_R_BlendSkeleton( refSkeleton_t *skel, const refSkeleton_t *blend, float frac )
{
    int    i;
    vec3_t bounds[ 2 ];

    if ( skel->numBones != blend->numBones )
    {
        Log::Warn("trap_R_BlendSkeleton: different number of bones %d != %d", skel->numBones, blend->numBones);
        return false;
    }

    // lerp between the 2 bone poses
    for ( i = 0; i < skel->numBones; i++ )
    {
        transform_t trans;

        TransStartLerp( &trans );
        TransAddWeight( 1.0f - frac, &skel->bones[ i ].t, &trans );
        TransAddWeight( frac, &blend->bones[ i ].t, &trans );
        TransEndLerp( &trans );

        skel->bones[ i ].t = trans;
    }

    // calculate a bounding box in the current coordinate system
    for ( i = 0; i < 3; i++ )
    {
        bounds[ 0 ][ i ] = skel->bounds[ 0 ][ i ] < blend->bounds[ 0 ][ i ] ? skel->bounds[ 0 ][ i ] : blend->bounds[ 0 ][ i ];
        bounds[ 1 ][ i ] = skel->bounds[ 1 ][ i ] > blend->bounds[ 1 ][ i ] ? skel->bounds[ 1 ][ i ] : blend->bounds[ 1 ][ i ];
    }

    VectorCopy( bounds[ 0 ], skel->bounds[ 0 ] );
    VectorCopy( bounds[ 1 ], skel->bounds[ 1 ] );

    return true;
}

int trap_R_BoneIndex( qhandle_t hModel, const char *boneName )
{
	int index;
	VM::SendMsg<Render::BoneIndexMsg>(hModel, boneName, index);
	return index;
}

int trap_R_AnimNumFrames( qhandle_t hAnim )
{
	int n;
	VM::SendMsg<Render::AnimNumFramesMsg>(hAnim, n);
	return n;
}

int trap_R_AnimFrameRate( qhandle_t hAnim )
{
	int n;
	VM::SendMsg<Render::AnimFrameRateMsg>(hAnim, n);
	return n;
}

qhandle_t trap_RegisterVisTest()
{
	int handle;
	VM::SendMsg<Render::RegisterVisTestMsg>(handle);
	return handle;
}

void trap_AddVisTestToScene( qhandle_t hTest, const vec3_t pos, float depthAdjust, float area )
{
	std::array<float, 3> mypos;
	VectorCopy(pos, mypos);
	cmdBuffer.SendMsg<Render::AddVisTestToSceneMsg>(hTest, mypos, depthAdjust, area);
}

float trap_CheckVisibility( qhandle_t hTest )
{
	float result;
	VM::SendMsg<Render::CheckVisibilityMsg>(hTest, result);
	return result;
}

void trap_R_GetTextureSize( qhandle_t handle, int *x, int *y )
{
	VM::SendMsg<Render::GetTextureSizeMsg>(handle, *x, *y);
}

qhandle_t trap_R_GenerateTexture( const byte *data, int x, int y )
{
	ASSERT( x && y );
	qhandle_t handle;
	std::vector<byte> mydata(data, data + 4 * x * y);
	VM::SendMsg<Render::GenerateTextureMsg>(mydata, x, y, handle);
	return handle;
}


void trap_UnregisterVisTest( qhandle_t hTest )
{
	cmdBuffer.SendMsg<Render::UnregisterVisTestMsg>(hTest);
}

void trap_SetColorGrading( int slot, qhandle_t hShader )
{
	cmdBuffer.SendMsg<Render::SetColorGradingMsg>(slot, hShader);
}

// All keys

int trap_Key_GetCatcher()
{
	int result;
	VM::SendMsg<Keyboard::GetCatcherMsg>(result);
	return result;
}

void trap_Key_SetCatcher( int catcher )
{
	VM::SendMsg<Keyboard::SetCatcherMsg>(catcher);
}

std::vector<std::vector<Keyboard::Key>> trap_Key_GetKeysForBinds(int team, const std::vector<std::string>& binds) {
	std::vector<std::vector<Keyboard::Key>> result;
	VM::SendMsg<Keyboard::GetKeysForBindsMsg>(team, binds, result);
	return result;
}

int trap_Key_GetCharForScancode( int scancode )
{
	int result;
	VM::SendMsg<Keyboard::GetCharForScancodeMsg>(scancode, result);
	return result;
}

void trap_Key_SetBinding( Keyboard::Key key, int team, const char *cmd )
{
	VM::SendMsg<Keyboard::SetBindingMsg>(key, team, cmd);
}

std::vector<Keyboard::Key> trap_Key_GetConsoleKeys()
{
	std::vector<Keyboard::Key> result;
	VM::SendMsg<Keyboard::GetConsoleKeysMsg>(result);
	return result;
}

void trap_Key_SetConsoleKeys(const std::vector<Keyboard::Key>& keys)
{
	VM::SendMsg<Keyboard::SetConsoleKeysMsg>(keys);
}

void trap_Key_ClearCmdButtons()
{
	VM::SendMsg<Keyboard::ClearCmdButtonsMsg>();
}

void trap_Key_ClearStates()
{
	VM::SendMsg<Keyboard::ClearStatesMsg>();
}

std::vector<bool> trap_Key_KeysDown( const std::vector<Keyboard::Key>& keys )
{
	std::vector<bool> list;
	VM::SendMsg<Keyboard::KeysDownMsg>( keys, list );
	return list;
}

// Mouse

void trap_SetMouseMode( MouseMode mode )
{
	VM::SendMsg<Mouse::SetMouseMode>( mode );
}

// All LAN

int trap_LAN_GetServerCount( int source )
{
	int count;
	VM::SendMsg<LAN::GetServerCountMsg>(source, count);
	return count;
}

// See SVC_Info() for the keys that are supposed to be available in `info`
void trap_LAN_GetServerInfo( int source, int n, trustedServerInfo_t &trustedInfo, std::string &info )
{
	VM::SendMsg<LAN::GetServerInfoMsg>(source, n, trustedInfo, info);
}

int trap_LAN_GetServerPing( int source, int n )
{
	int ping;
	VM::SendMsg<LAN::GetServerPingMsg>(source, n, ping);
	return ping;
}

void trap_LAN_MarkServerVisible( int source, int n, bool visible )
{
	VM::SendMsg<LAN::MarkServerVisibleMsg>(source, n, visible);
}

int trap_LAN_ServerIsVisible( int source, int n )
{
	bool visible;
	VM::SendMsg<LAN::ServerIsVisibleMsg>(source, n, visible);
	return visible;
}

bool trap_LAN_UpdateVisiblePings( int source )
{
	bool res;
	VM::SendMsg<LAN::UpdateVisiblePingsMsg>(source, res);
	return res;
}

void trap_LAN_ResetPings( int n )
{
	VM::SendMsg<LAN::ResetPingsMsg>(n);
}

int trap_LAN_ServerStatus( const char *serverAddress, char *serverStatus, int maxLen )
{
	std::string status;
	int res;
	VM::SendMsg<LAN::ServerStatusMsg>(serverAddress, maxLen, status, res);
	Q_strncpyz(serverStatus, status.c_str(), maxLen);
	return res;
}

void trap_LAN_ResetServerStatus()
{
	VM::SendMsg<LAN::ResetServerStatusMsg>();
}
