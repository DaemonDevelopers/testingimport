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

// APIs for use by the cgame VM
#ifndef SHARED_CLIENT_API_H_
#define SHARED_CLIENT_API_H_

#include "engine/qcommon/q_shared.h"
#include "engine/renderer/tr_types.h"
#include "engine/client/cg_api.h"
#include "common/KeyIdentification.h"
#include "shared/CommonProxies.h"
#include <shared/CommandBufferClient.h>

extern IPC::CommandBufferClient cmdBuffer;

void            trap_SendClientCommand( const char *s );
void            trap_UpdateScreen();

void trap_CM_BatchMarkFragments(
	unsigned maxPoints,
	unsigned maxFragments,
	const std::vector<markMsgInput_t> &markMsgInput,
	std::vector<markMsgOutput_t> &markMsgOutput );

void            trap_S_StartSound( vec3_t origin, int entityNum, soundChannel_t entchannel, sfxHandle_t sfx );
void            trap_S_StartLocalSound( sfxHandle_t sfx, soundChannel_t channelNum );
void            trap_S_ClearLoopingSounds( bool killall );
void            trap_S_AddLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx );
void            trap_S_AddRealLoopingSound( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx );
void            trap_S_StopLoopingSound( int entityNum );
void            trap_S_UpdateEntityPosition( int entityNum, const vec3_t origin );
void            trap_S_Respatialize( int entityNum, const vec3_t origin, vec3_t axis[ 3 ], int inwater );
sfxHandle_t     trap_S_RegisterSound( const char *sample, bool compressed );
void            trap_S_StartBackgroundTrack( const char *intro, const char *loop );
void            trap_R_LoadWorldMap( const char *mapname );
qhandle_t       trap_R_RegisterModel( const char *name );
qhandle_t       trap_R_RegisterSkin( const char *name );
qhandle_t       trap_R_RegisterShader( const char *name, int flags );
void            trap_R_ClearScene();
void            trap_R_AddRefEntityToScene( const refEntity_t *re );
void            trap_R_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts );
void            trap_R_AddPolysToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts, int numPolys );
void            trap_R_AddLightToScene( const vec3_t origin, float radius, float intensity, float r, float g, float b, int flags );
void            trap_R_Add2dPolysIndexedToScene( const polyVert_t *polys, int numVerts, const int *indexes, int numIndexes, int trans_x, int trans_y, qhandle_t shader );
void            trap_R_SetMatrixTransform( const matrix_t matrix );
void            trap_R_ResetMatrixTransform();
void            trap_R_RenderScene( const refdef_t *fd );
void            trap_R_ClearColor();
void            trap_R_SetColor( const Color::Color &rgba );
void            trap_R_SetClipRegion( const float *region );
void            trap_R_ResetClipRegion();
void            trap_R_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader );
void            trap_R_DrawRotatedPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader, float angle );
void            trap_R_ModelBounds( clipHandle_t model, vec3_t mins, vec3_t maxs );
int             trap_R_LerpTag( orientation_t *tag, const refEntity_t *refent, const char *tagName, int startIndex );
void            trap_R_GetTextureSize( qhandle_t handle, int *x, int *y );
qhandle_t       trap_R_GenerateTexture( const byte *data, int x, int y );
void            trap_GetCurrentSnapshotNumber( int *snapshotNumber, int *serverTime );
bool        trap_GetSnapshot( int snapshotNumber, ipcSnapshot_t *snapshot );
int             trap_GetCurrentCmdNumber();
bool        trap_GetUserCmd( int cmdNumber, usercmd_t *ucmd );
void            trap_SetUserCmdValue( int stateValue, int flags, float sensitivityScale );
int             trap_Key_GetCatcher();
void            trap_Key_SetCatcher( int catcher );
void            trap_Key_SetBinding( Keyboard::Key key, int team, const char *cmd );
std::vector<Keyboard::Key> trap_Key_GetConsoleKeys();
void trap_Key_SetConsoleKeys(const std::vector<Keyboard::Key>& keys);
void            trap_Key_ClearCmdButtons();
void            trap_Key_ClearStates();
std::vector<bool> trap_Key_KeysDown( const std::vector<Keyboard::Key>& keys );
void            trap_SetMouseMode( MouseMode mode );
void            trap_S_StopBackgroundTrack();
void            trap_R_RemapShader( const char *oldShader, const char *newShader, const char *timeOffset );
std::vector<std::vector<Keyboard::Key>> trap_Key_GetKeysForBinds(int team, const std::vector<std::string>& binds);
int             trap_Key_GetCharForScancode( int scancode );

std::vector<bool> trap_R_BatchInPVS(
	const vec3_t origin,
	const std::vector<std::array<float, 3>>& posEntities );

int             trap_R_LightForPoint( vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir );
qhandle_t       trap_R_RegisterAnimation( const char *name );
std::vector<skelAnimation_t> trap_R_BatchGetAnimations( const std::vector<qhandle_t>& anims );
int             trap_R_BuildSkeleton2( refSkeleton_t* skel, skelAnimation_t* anim, int startFrame, int endFrame, float frac, bool clearOrigin );
int             trap_R_BuildSkeleton( refSkeleton_t *skel, qhandle_t anim, int startFrame, int endFrame, float frac, bool clearOrigin );
int             trap_R_BlendSkeleton( refSkeleton_t *skel, const refSkeleton_t *blend, float frac );
int             trap_R_BoneIndex( qhandle_t hModel, const char *boneName );
int             trap_R_AnimNumFrames( qhandle_t hAnim );
int             trap_R_AnimFrameRate( qhandle_t hAnim );
void            trap_RegisterButtonCommands( const char *cmds );
void            trap_notify_onTeamChange( int newTeam );
qhandle_t       trap_RegisterVisTest();
void            trap_AddVisTestToScene( qhandle_t hTest, const vec3_t pos,
    float depthAdjust, float area );
float           trap_CheckVisibility( qhandle_t hTest );
void            trap_UnregisterVisTest( qhandle_t hTest );
void            trap_SetColorGrading( int slot, qhandle_t hShader );
void            trap_R_ScissorEnable( bool enable );
void            trap_R_ScissorSet( int x, int y, int w, int h );
int             trap_LAN_GetServerCount( int source );
void            trap_LAN_GetServerInfo( int source, int n, trustedServerInfo_t &trustedInfo, std::string &info );
int             trap_LAN_GetServerPing( int source, int n );
void            trap_LAN_MarkServerVisible( int source, int n, bool visible );
int             trap_LAN_ServerIsVisible( int source, int n );
bool        trap_LAN_UpdateVisiblePings( int source );
void            trap_LAN_ResetPings( int n );
int             trap_LAN_ServerStatus( const char *serverAddress, char *serverStatus, int maxLen );
void            trap_LAN_ResetServerStatus();
void            trap_R_GetShaderNameFromHandle( const qhandle_t shader, char *out, int len );
void            trap_PrepareKeyUp();
void            trap_R_SetAltShaderTokens( const char * );
void            trap_S_UpdateEntityVelocity( int entityNum, const vec3_t velocity );
void trap_S_UpdateEntityPositionVelocity( int entityNum, const vec3_t position, const vec3_t velocity );
void            trap_S_SetReverb( int slotNum, const char* presetName, float ratio );
void            trap_S_BeginRegistration();
void            trap_S_EndRegistration();

#endif
