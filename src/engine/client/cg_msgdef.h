/*
===========================================================================

Daemon GPL Source Code
Copyright (C) 2011  Dusan Jocic <dusanjocic@msn.com>

Daemon is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

Daemon is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

#ifndef CG_MSGDEF_H
#define CG_MSGDEF_H

#include "cg_api.h"
#include "engine/renderer/tr_types.h"
#include "common/IPC/CommonSyscalls.h"
#include "common/KeyIdentification.h"

namespace Util {
	template<> struct SerializeTraits<ipcSnapshot_t> {
		static void Write(Writer& stream, const ipcSnapshot_t& snap)
		{
			stream.Write<uint32_t>(snap.b.snapFlags);
			stream.Write<uint32_t>(snap.b.ping);
			stream.Write<uint32_t>(snap.b.serverTime);
			stream.WriteData(&snap.b.areamask, MAX_MAP_AREA_BYTES);
			stream.Write<OpaquePlayerState>(snap.ps);
			stream.Write<std::vector<entityState_t>>(snap.b.entities);
			stream.Write<std::vector<std::string>>(snap.b.serverCommands);
		}

		static ipcSnapshot_t Read(Reader& stream)
		{
			ipcSnapshot_t snap;
			snap.b.snapFlags = stream.Read<uint32_t>();
			snap.b.ping = stream.Read<uint32_t>();
			snap.b.serverTime = stream.Read<uint32_t>();
			stream.ReadData(&snap.b.areamask, MAX_MAP_AREA_BYTES);
			snap.ps = stream.Read<OpaquePlayerState>();
			snap.b.entities = stream.Read<std::vector<entityState_t>>();
			snap.b.serverCommands = stream.Read<std::vector<std::string>>();
			return snap;
		}
	};

	// For skeletons, only send the bones which are used
	template<> struct SerializeTraits<refSkeleton_t> {
		static void Write(Writer& stream, const refSkeleton_t& skel)
		{
			stream.Write<uint32_t>(Util::ordinal(skel.type));
			stream.WriteSize(skel.numBones);
			for (int i = 0; i < 2; i++) {
				for (int j = 0; j < 3; j++) {
					stream.Write<float>(skel.bounds[i][j]);
				}
			}
			stream.Write<float>(skel.scale);
			size_t length = sizeof(refBone_t) * skel.numBones;
			stream.WriteData(&skel.bones, length);
		}
		static refSkeleton_t Read(Reader& stream)
		{
			refSkeleton_t skel;
			skel.type = static_cast<refSkeletonType_t>(stream.Read<uint32_t>());
			skel.numBones = stream.ReadSize<refBone_t>();
			for (int i = 0; i < 2; i++) {
				for (int j = 0; j < 3; j++) {
					skel.bounds[i][j] = stream.Read<float>();
				}
			}
			skel.scale = stream.Read<float>();

			if (skel.numBones > sizeof(skel.bones) / sizeof(refBone_t)) {
				Sys::Drop("IPC: Too many bones for refSkeleton_t: %i", skel.numBones);
			}
			size_t length = sizeof(refBone_t) * skel.numBones;
			stream.ReadData(&skel.bones, length);
			return skel;
		}
	};

	// Use that bone optimization for refEntity_t
	template<> struct SerializeTraits<refEntity_t> {
		static void Write(Writer& stream, const refEntity_t& ent)
		{
			stream.WriteData(&ent, offsetof(refEntity_t, skeleton));
			stream.Write<refSkeleton_t>(ent.skeleton);
		}
		static refEntity_t Read(Reader& stream)
		{
			refEntity_t ent;
			stream.ReadData(&ent, offsetof(refEntity_t, skeleton));
			ent.skeleton = stream.Read<refSkeleton_t>();
			return ent;
		}
	};

	template<>
	struct SerializeTraits<Color::Color> {
		static void Write(Writer& stream, const Color::Color& value)
		{
			stream.WriteData(value.ToArray(), value.ArrayBytes());
		}
		static Color::Color Read(Reader& stream)
		{
			Color::Color value;
			stream.ReadData(value.ToArray(), value.ArrayBytes());
			return value;
		}
	};
}

enum cgameImport_t
{
  // Misc
  CG_SENDCLIENTCOMMAND,
  CG_UPDATESCREEN,
  CG_CM_BATCHMARKFRAGMENTS,
  CG_GETCURRENTSNAPSHOTNUMBER,
  CG_GETSNAPSHOT,
  CG_GETCURRENTCMDNUMBER,
  CG_GETUSERCMD,
  CG_SETUSERCMDVALUE,
  CG_REGISTER_BUTTON_COMMANDS,
  CG_NOTIFY_TEAMCHANGE,
  CG_PREPAREKEYUP,

  // Sound
  CG_S_STARTSOUND,
  CG_S_STARTLOCALSOUND,
  CG_S_CLEARLOOPINGSOUNDS,
  CG_S_ADDLOOPINGSOUND,
  CG_S_STOPLOOPINGSOUND,
  CG_S_UPDATEENTITYPOSITION,
  CG_S_RESPATIALIZE,
  CG_S_REGISTERSOUND,
  CG_S_STARTBACKGROUNDTRACK,
  CG_S_STOPBACKGROUNDTRACK,
  CG_S_UPDATEENTITYVELOCITY,
  CG_S_UPDATEENTITYPOSITIONVELOCITY,
  CG_S_SETREVERB,
  CG_S_BEGINREGISTRATION,
  CG_S_ENDREGISTRATION,

  // Renderer
  CG_R_SETALTSHADERTOKENS,
  CG_R_GETSHADERNAMEFROMHANDLE,
  CG_R_SCISSOR_ENABLE,
  CG_R_SCISSOR_SET,
  CG_R_LOADWORLDMAP,
  CG_R_REGISTERMODEL,
  CG_R_REGISTERSKIN,
  CG_R_REGISTERSHADER,
  CG_R_REGISTERFONT,
  CG_R_CLEARSCENE,
  CG_R_ADDREFENTITYTOSCENE,
  CG_R_ADDPOLYTOSCENE,
  CG_R_ADDPOLYSTOSCENE,
  CG_R_ADDLIGHTTOSCENE,
  CG_R_RENDERSCENE,
  CG_R_ADD2DPOLYSINDEXED,
  CG_R_SETMATRIXTRANSFORM,
  CG_R_RESETMATRIXTRANSFORM,
  CG_R_SETCOLOR,
  CG_R_SETCLIPREGION,
  CG_R_RESETCLIPREGION,
  CG_R_DRAWSTRETCHPIC,
  CG_R_DRAWROTATEDPIC,
  CG_R_MODELBOUNDS,
  CG_R_LERPTAG,
  CG_R_REMAP_SHADER,
  CG_R_BATCHINPVS,
  CG_R_LIGHTFORPOINT,
  CG_R_REGISTERANIMATION,
  CG_R_BUILDSKELETON,
  CG_R_GETANIMATION,
  CG_R_BATCHGETANIMATION,
  CG_R_BONEINDEX,
  CG_R_ANIMNUMFRAMES,
  CG_R_ANIMFRAMERATE,
  CG_REGISTERVISTEST,
  CG_ADDVISTESTTOSCENE,
  CG_CHECKVISIBILITY,
  CG_UNREGISTERVISTEST,
  CG_SETCOLORGRADING,
  CG_R_GETTEXTURESIZE,
  CG_R_GENERATETEXTURE,

  // Keys
  CG_KEY_GETCATCHER,
  CG_KEY_SETCATCHER,
  CG_KEY_GETKEYSFORBINDS,
  CG_KEY_GETCONSOLEKEYS,
  CG_KEY_SETCONSOLEKEYS,
  CG_KEY_GETCHARFORSCANCODE,
  CG_KEY_SETBINDING,
  CG_KEY_CLEARSTATES,
  CG_KEY_CLEARCMDBUTTONS,
  CG_KEY_KEYSDOWN,

  // Mouse
  CG_MOUSE_SETMOUSEMODE,

  // Lan
  CG_LAN_GETSERVERCOUNT,
  CG_LAN_GETSERVERINFO,
  CG_LAN_GETSERVERPING,
  CG_LAN_MARKSERVERVISIBLE,
  CG_LAN_SERVERISVISIBLE,
  CG_LAN_UPDATEVISIBLEPINGS,
  CG_LAN_RESETPINGS,
  CG_LAN_SERVERSTATUS,
  CG_LAN_RESETSERVERSTATUS,
};

// All Miscs

// TODO really sync?
using SendClientCommandMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_SENDCLIENTCOMMAND>, std::string>
>;
// TODO really sync?
using UpdateScreenMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_UPDATESCREEN>>
>;
// TODO can move to VM too?
using CMBatchMarkFragments = IPC::SyncMessage<
	IPC::Message<
		IPC::Id<VM::QVM, CG_CM_BATCHMARKFRAGMENTS>,
		unsigned,
		unsigned,
		std::vector<markMsgInput_t>>,
	IPC::Reply<
		std::vector<markMsgOutput_t>>
>;
// TODO send all snapshots at the beginning of the frame
using GetCurrentSnapshotNumberMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_GETCURRENTSNAPSHOTNUMBER>>,
	IPC::Reply<int, int>
>;
using GetSnapshotMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_GETSNAPSHOT>, int>,
	IPC::Reply<bool, ipcSnapshot_t>
>;
using GetCurrentCmdNumberMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_GETCURRENTCMDNUMBER>>,
	IPC::Reply<int>
>;
using GetUserCmdMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_GETUSERCMD>, int>,
	IPC::Reply<bool, usercmd_t>
>;
using SetUserCmdValueMsg = IPC::Message<IPC::Id<VM::QVM, CG_SETUSERCMDVALUE>, int, int, float>;
using RegisterButtonCommandsMsg = IPC::Message<IPC::Id<VM::QVM, CG_REGISTER_BUTTON_COMMANDS>, std::string>;
using NotifyTeamChangeMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_NOTIFY_TEAMCHANGE>, int>
>;
using PrepareKeyUpMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_PREPAREKEYUP>>
>;

// All Sounds

namespace Audio {
	using RegisterSoundMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_S_REGISTERSOUND>, std::string>,
		IPC::Reply<int>
	>;

    //Command buffer syscalls

	using StartSoundMsg = IPC::Message<IPC::Id<VM::QVM, CG_S_STARTSOUND>, bool, Vec3, int, int>;
	using StartLocalSoundMsg = IPC::Message<IPC::Id<VM::QVM, CG_S_STARTLOCALSOUND>, int>;
	using ClearLoopingSoundsMsg = IPC::Message<IPC::Id<VM::QVM, CG_S_CLEARLOOPINGSOUNDS>>;
	using AddLoopingSoundMsg = IPC::Message<IPC::Id<VM::QVM, CG_S_ADDLOOPINGSOUND>, int, int>;
	using StopLoopingSoundMsg = IPC::Message<IPC::Id<VM::QVM, CG_S_STOPLOOPINGSOUND>, int>;
	using UpdateEntityPositionMsg = IPC::Message<IPC::Id<VM::QVM, CG_S_UPDATEENTITYPOSITION>, int, Vec3>;
	using RespatializeMsg = IPC::Message<IPC::Id<VM::QVM, CG_S_RESPATIALIZE>, int, std::array<Vec3, 3>>;
	using StartBackgroundTrackMsg = IPC::Message<IPC::Id<VM::QVM, CG_S_STARTBACKGROUNDTRACK>, std::string, std::string>;
	using StopBackgroundTrackMsg = IPC::Message<IPC::Id<VM::QVM, CG_S_STOPBACKGROUNDTRACK>>;
	using UpdateEntityVelocityMsg = IPC::Message<IPC::Id<VM::QVM, CG_S_UPDATEENTITYVELOCITY>, int, Vec3>;
	using UpdateEntityPositionVelocityMsg = IPC::Message<IPC::Id<VM::QVM, CG_S_UPDATEENTITYPOSITIONVELOCITY>, int, Vec3, Vec3>;
	using SetReverbMsg = IPC::Message<IPC::Id<VM::QVM, CG_S_SETREVERB>, int, std::string, float>;
	using BeginRegistrationMsg = IPC::Message<IPC::Id<VM::QVM, CG_S_BEGINREGISTRATION>>;
	using EndRegistrationMsg = IPC::Message<IPC::Id<VM::QVM, CG_S_ENDREGISTRATION>>;
}

namespace Render {
	using SetAltShaderTokenMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_SETALTSHADERTOKENS>, std::string>;
	using GetShaderNameFromHandleMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_GETSHADERNAMEFROMHANDLE>, int>,
		IPC::Reply<std::string>
	>;
	// TODO is it really async?
	using LoadWorldMapMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_LOADWORLDMAP>, std::string>;
	using RegisterModelMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_REGISTERMODEL>, std::string>,
		IPC::Reply<int>
	>;
	using RegisterSkinMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_REGISTERSKIN>, std::string>,
		IPC::Reply<int>
	>;
	using RegisterShaderMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_REGISTERSHADER>, std::string, int>,
		IPC::Reply<int>
	>;
	using ModelBoundsMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_MODELBOUNDS>, int>,
		IPC::Reply<std::array<float, 3>, std::array<float, 3>>
	>;
	using LerpTagMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_LERPTAG>, refEntity_t, std::string, int>,
		IPC::Reply<orientation_t, int>
	>;
	using RemapShaderMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_REMAP_SHADER>, std::string, std::string, std::string>;
	// TODO not a renderer call, handle in CM in the VM?
	using BatchInPVSMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<
			VM::QVM, CG_R_BATCHINPVS>,
			std::array<float, 3>,
			std::vector<std::array<float, 3>>>,
		IPC::Reply<std::vector<bool>>
	>;
	using LightForPointMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_LIGHTFORPOINT>, std::array<float, 3>>,
		IPC::Reply<std::array<float, 3>, std::array<float, 3>, std::array<float, 3>, int>
	>;
	using RegisterAnimationMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_REGISTERANIMATION>, std::string>,
		IPC::Reply<int>
	>;
	using BuildSkeletonMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_BUILDSKELETON>, int, int, int, float, bool>,
		IPC::Reply<refSkeleton_t, int>
	>;
	using GetAnimationMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_GETANIMATION>, int>,
		IPC::Reply<skelAnimation_t>
	>;
	using BatchGetAnimationsMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_BATCHGETANIMATION>, std::vector<qhandle_t>>,
		IPC::Reply<std::vector<skelAnimation_t>>
	>;
	using BoneIndexMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_BONEINDEX>, int, std::string>,
		IPC::Reply<int>
	>;
	using AnimNumFramesMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_ANIMNUMFRAMES>, int>,
		IPC::Reply<int>
	>;
	using AnimFrameRateMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_ANIMFRAMERATE>, int>,
		IPC::Reply<int>
	>;
	using RegisterVisTestMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_REGISTERVISTEST>>,
		IPC::Reply<int>
	>;
	using CheckVisibilityMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_CHECKVISIBILITY>, int>,
		IPC::Reply<float>
	>;
	using GetTextureSizeMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_GETTEXTURESIZE>, qhandle_t>,
		IPC::Reply<int, int>
	>;
	using GenerateTextureMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_R_GENERATETEXTURE>, std::vector<byte>, int, int>,
		IPC::Reply<qhandle_t>
	>;

    // All command buffer syscalls

	using ScissorEnableMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_SCISSOR_ENABLE>, bool>;
	using ScissorSetMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_SCISSOR_SET>, int, int, int, int>;
	using ClearSceneMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_CLEARSCENE>>;
	using AddRefEntityToSceneMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_ADDREFENTITYTOSCENE>, refEntity_t>;
	using AddPolyToSceneMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_ADDPOLYTOSCENE>, int, std::vector<polyVert_t>>;
	using AddPolysToSceneMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_ADDPOLYSTOSCENE>, int, std::vector<polyVert_t>, int, int>;
	using AddLightToSceneMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_ADDLIGHTTOSCENE>, std::array<float, 3>, float, float, float, float, int>;
	using SetColorMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_SETCOLOR>, Color::Color>;
	using SetClipRegionMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_SETCLIPREGION>, std::array<float, 4>>;
	using ResetClipRegionMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_RESETCLIPREGION>>;
	using DrawStretchPicMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_DRAWSTRETCHPIC>, float, float, float, float, float, float, float, float, int>;
	using DrawRotatedPicMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_DRAWROTATEDPIC>, float, float, float, float, float, float, float, float, int, float>;
	using AddVisTestToSceneMsg = IPC::Message<IPC::Id<VM::QVM, CG_ADDVISTESTTOSCENE>, int, std::array<float, 3>, float, float>;
	using UnregisterVisTestMsg = IPC::Message<IPC::Id<VM::QVM, CG_UNREGISTERVISTEST>, int>;
	using SetColorGradingMsg = IPC::Message<IPC::Id<VM::QVM, CG_SETCOLORGRADING>, int, int>;
	using RenderSceneMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_RENDERSCENE>, refdef_t>;
	using Add2dPolysIndexedMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_ADD2DPOLYSINDEXED>, std::vector<polyVert_t>, int, std::vector<int>, int, int, int, qhandle_t>;
	using SetMatrixTransformMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_SETMATRIXTRANSFORM>, std::array<float, 16>>;
	using ResetMatrixTransformMsg = IPC::Message<IPC::Id<VM::QVM, CG_R_RESETMATRIXTRANSFORM>>;
}

namespace Keyboard {
	using GetCatcherMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_KEY_GETCATCHER>>,
		IPC::Reply<int>
	>;
	using SetCatcherMsg = IPC::Message<IPC::Id<VM::QVM, CG_KEY_SETCATCHER>, int>;
	using GetKeysForBindsMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_KEY_GETKEYSFORBINDS>, int, std::vector<std::string>>,
		IPC::Reply<std::vector<std::vector<Key>>>
	>;
	using GetConsoleKeysMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_KEY_GETCONSOLEKEYS>>,
		IPC::Reply<std::vector<Key>>
	>;
	using SetConsoleKeysMsg = IPC::Message<IPC::Id<VM::QVM, CG_KEY_SETCONSOLEKEYS>, std::vector<Key>>;
	using GetCharForScancodeMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_KEY_GETCHARFORSCANCODE>, int>,
		IPC::Reply<int>
	>;
	using SetBindingMsg = IPC::Message<IPC::Id<VM::QVM, CG_KEY_SETBINDING>, Key, int, std::string>;
	using ClearCmdButtonsMsg = IPC::Message<IPC::Id<VM::QVM, CG_KEY_CLEARCMDBUTTONS>>;
	using ClearStatesMsg = IPC::Message<IPC::Id<VM::QVM, CG_KEY_CLEARSTATES>>;
	using KeysDownMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_KEY_KEYSDOWN>, std::vector<Key>>,
		IPC::Reply<std::vector<bool>>
	>;
}

namespace Mouse {
	using SetMouseMode = IPC::Message<IPC::Id<VM::QVM, CG_MOUSE_SETMOUSEMODE>, MouseMode>;
}

namespace LAN {
	using GetServerCountMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_LAN_GETSERVERCOUNT>, int>,
		IPC::Reply<int>
	>;
	using GetServerInfoMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_LAN_GETSERVERINFO>, int, int>,
		IPC::Reply<trustedServerInfo_t, std::string>
	>;
	using GetServerPingMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_LAN_GETSERVERPING>, int, int>,
		IPC::Reply<int>
	>;
	using MarkServerVisibleMsg = IPC::Message<IPC::Id<VM::QVM, CG_LAN_MARKSERVERVISIBLE>, int, int, bool>;
	using ServerIsVisibleMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_LAN_SERVERISVISIBLE>, int, int>,
		IPC::Reply<bool>
	>;
	using UpdateVisiblePingsMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_LAN_UPDATEVISIBLEPINGS>, int>,
		IPC::Reply<bool>
	>;
	using ResetPingsMsg = IPC::Message<IPC::Id<VM::QVM, CG_LAN_RESETPINGS>, int>;
	using ServerStatusMsg = IPC::SyncMessage<
		IPC::Message<IPC::Id<VM::QVM, CG_LAN_SERVERSTATUS>, std::string, int>,
		IPC::Reply<std::string, int>
	>;
	using ResetServerStatusMsg = IPC::Message<IPC::Id<VM::QVM, CG_LAN_RESETSERVERSTATUS>>;
}


enum cgameExport_t
{
  CG_STATIC_INIT,

//  void CG_Init( int serverMessageNum, int serverCommandSequence )
  // called when the level loads or when the renderer is restarted
  // all media should be registered at this time
  // cgame will display loading status by calling SCR_Update, which
  // will call CG_DrawInformation during the loading process
  // reliableCommandSequence will be 0 on fresh loads, but higher for
  // demos or vid_restarts
  CG_INIT,

//  void (*CG_Shutdown)();
  // oportunity to flush and close any open files
  CG_SHUTDOWN,

//  void (*CG_DrawActiveFrame)( int serverTime, bool demoPlayback );
  // Generates and draws a game scene and status information at the given time.
  // If demoPlayback is set, local movement prediction will not be enabled
  CG_DRAW_ACTIVE_FRAME,

//  void    (*CG_KeyEvent)( Keyboard::Key key, bool down );
  CG_KEY_DOWN_EVENT,
  CG_KEY_UP_EVENT,

//  void    (*CG_MouseEvent)( int dx, int dy );
  CG_MOUSE_EVENT,

//  void    (*CG_MousePosEvent)( int x, int y );
  CG_MOUSE_POS_EVENT,

// pass in text input events from the engine
  CG_CHARACTER_INPUT_EVENT,

// Inits libRocket in the game.
  CG_ROCKET_VM_INIT,

// Rocket runs through a frame, including event processing, and rendering
  CG_ROCKET_FRAME,

  CG_CONSOLE_LINE,

// void (*CG_FocusEvent)( bool focus);
  CG_FOCUS_EVENT,
};

using CGameStaticInitMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_STATIC_INIT>, int>
>;
using CGameInitMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_INIT>, int, int, glconfig_t, GameStateCSs>
>;
using CGameShutdownMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_SHUTDOWN>>
>;
using CGameDrawActiveFrameMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_DRAW_ACTIVE_FRAME>, int, bool>
>;
using CGameKeyDownEventMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_KEY_DOWN_EVENT>, Keyboard::Key, bool>,
	IPC::Reply<bool>
>;
using CGameKeyUpEventMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_KEY_UP_EVENT>, Keyboard::Key>
>;
using CGameMouseEventMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_MOUSE_EVENT>, int, int>
>;
using CGameMousePosEventMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_MOUSE_POS_EVENT>, int, int>
>;
using CGameCharacterInputMsg = IPC::SyncMessage <
	IPC::Message<IPC::Id<VM::QVM, CG_CHARACTER_INPUT_EVENT>, int>
>;
using CGameFocusEventMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_FOCUS_EVENT>, bool>
>;

//TODO Check all rocket calls
using CGameRocketInitMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_ROCKET_VM_INIT>, glconfig_t>
>;
using CGameRocketFrameMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_ROCKET_FRAME>, cgClientState_t>
>;

using CGameConsoleLineMsg = IPC::SyncMessage<
	IPC::Message<IPC::Id<VM::QVM, CG_CONSOLE_LINE>, std::string>
>;

#endif
