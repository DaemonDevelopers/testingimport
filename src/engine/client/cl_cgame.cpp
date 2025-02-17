/*
===========================================================================

Daemon GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of the Daemon GPL Source Code (Daemon Source Code).

Daemon Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Daemon Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Daemon Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Daemon Source Code is also subject to certain additional terms.
You should have received a copy of these additional terms immediately following the
terms and conditions of the GNU General Public License which accompanied the Daemon
Source Code.  If not, please request a copy in writing from id Software at the address
below.

If you have questions concerning this license or the applicable additional terms, you
may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville,
Maryland 20850 USA.

===========================================================================
*/

// cl_cgame.c  -- client system interaction with client game

#include "client.h"
#include "cg_msgdef.h"

#include "key_identification.h"

#if defined(USE_MUMBLE)
#include "mumblelink/libmumblelink.h"
#endif

#include "qcommon/crypto.h"
#include "qcommon/sys.h"

#include "framework/CommonVMServices.h"
#include "framework/CommandSystem.h"
#include "framework/CvarSystem.h"
#include "framework/Network.h"

// Suppress warnings for unused [this] lambda captures.
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused-lambda-capture"
#endif

/**
 * Independently of the gamelogic, we can assume the game to have "teams" with an id,
 * as long as we don't assume any semantics on that
 * we can assume however that "0" is some form of "neutral" or "non" team,
 * most likely a non-playing client that e.g. observes the game or hasn't joined yet.
 * even in a deathmatch or singleplayer game, joining would start with team 1, even though there might not be another one
 * this allows several client logic (like team specific binds or configurations) to work no matter how the team is called or what its attributes are
 */
static Cvar::Cvar<int> p_team("p_team", "team number of your team", Cvar::ROM, 0);

/*
====================
CL_GetUserCmd
====================
*/
bool CL_GetUserCmd( int cmdNumber, usercmd_t *ucmd )
{
	// cmds[cmdNumber] is the last properly generated command

	// can't return anything that we haven't created yet
	if ( cmdNumber > cl.cmdNumber )
	{
		Sys::Drop( "CL_GetUserCmd: %i >= %i", cmdNumber, cl.cmdNumber );
	}

	// the usercmd has been overwritten in the wrapping
	// buffer because it is too far out of date
	if ( cmdNumber <= cl.cmdNumber - CMD_BACKUP )
	{
		return false;
	}

	*ucmd = cl.cmds[ cmdNumber & CMD_MASK ];

	return true;
}

int CL_GetCurrentCmdNumber()
{
	return cl.cmdNumber;
}

/*
=====================
CL_ConfigstringModified
=====================
*/
void CL_ConfigstringModified( Cmd::Args& csCmd )
{
	if (csCmd.Argc() < 3) {
		Sys::Drop( "CL_ConfigstringModified: wrong command received" );
	}

	int index = atoi( csCmd.Argv(1).c_str() );

	if ( index < 0 || index >= MAX_CONFIGSTRINGS )
	{
		Sys::Drop( "CL_ConfigstringModified: bad index %i", index );
	}

	if ( cl.gameState[index] == csCmd.Argv(2) )
	{
		return;
	}

	cl.gameState[index] = csCmd.Argv(2);

	if ( index == CS_SYSTEMINFO )
	{
		// parse serverId and other cvars
		CL_SystemInfoChanged();
	}
}

/*
===================
CL_HandleServerCommand
CL_GetServerCommand
===================
*/
bool CL_HandleServerCommand(Str::StringRef text, std::string& newText) {
	static char bigConfigString[ BIG_INFO_STRING ];
	Cmd::Args args(text);

	if (args.Argc() == 0) {
		return false;
	}

	auto cmd = args.Argv(0);
	int argc = args.Argc();

	if (cmd == "disconnect") {
		// NERVE - SMF - allow server to indicate why they were disconnected
		if (argc >= 2) {
			Sys::Drop("^3Server disconnected:\n^7%s", args.Argv(1).c_str());
		} else {
			Sys::Drop("^3Server disconnected:\n^7(reason unknown)");
		}
	}

	// bcs0 to bcs2 are used by the server to send info strings that are bigger than the size of a packet.
	// See also SV_UpdateConfigStrings
	// bcs0 starts a new big config string
	// bcs1 continues it
	// bcs2 finishes it and feeds it back as a new command sent by the server (bcs0 makes it a cs command)
	if (cmd == "bcs0") {
		if (argc >= 3) {
			Com_sprintf(bigConfigString, BIG_INFO_STRING, "cs %s %s", args.Argv(1).c_str(), args.EscapedArgs(2).c_str());
		}
		return false;
	}

	if (cmd == "bcs1") {
		if (argc >= 3) {
			const char* s = Cmd_QuoteString( args[2].c_str() );

			if (strlen(bigConfigString) + strlen(s) >= BIG_INFO_STRING) {
				Sys::Drop("bcs exceeded BIG_INFO_STRING");
			}

			Q_strcat(bigConfigString, sizeof(bigConfigString), s);
		}
		return false;
	}

	if (cmd == "bcs2") {
		if (argc >= 3) {
			const char* s = Cmd_QuoteString( args[2].c_str() );

			if (strlen(bigConfigString) + strlen(s) + 1 >= BIG_INFO_STRING) {
				Sys::Drop("bcs exceeded BIG_INFO_STRING");
			}

			Q_strcat(bigConfigString, sizeof(bigConfigString), s);
			Q_strcat(bigConfigString, sizeof(bigConfigString), "\"");
			newText = bigConfigString;
			return CL_HandleServerCommand(bigConfigString, newText);
		}
		return false;
	}

	if (cmd == "cs") {
		CL_ConfigstringModified(args);
		return true;
	}

	if (cmd == "map_restart") {
		// clear outgoing commands before passing
		// the restart to the cgame
		memset(cl.cmds, 0, sizeof(cl.cmds));
		return true;
	}

	if (cmd == "popup") {
		// direct server to client popup request, bypassing cgame
		if (cls.state == connstate_t::CA_ACTIVE && !clc.demoplaying && argc >=1) {
			// TODO: Pass to the cgame
		}
		return false;
	}

	if (cmd == "pubkey_decrypt") {
		char         buffer[ MAX_STRING_CHARS ] = "pubkey_identify ";
		NettleLength msg_len = MAX_STRING_CHARS - 16;
		mpz_t        message;

		if (argc == 1) {
			Log::Notice("^3Server sent a pubkey_decrypt command, but sent nothing to decrypt!");
			return false;
		}

		mpz_init_set_str(message, args.Argv(1).c_str(), 16);

		if (rsa_decrypt(&private_key, &msg_len, (unsigned char *) buffer + 16, message)) {
			nettle_mpz_set_str_256_u(message, msg_len, (unsigned char *) buffer + 16);
			mpz_get_str(buffer + 16, 16, message);
			CL_AddReliableCommand(buffer);
		}

		mpz_clear(message);
		return false;
	}

	return true;
}

// Get the server commands, does client-specific handling
// that may block the propagation of a command to cgame.
// If the propagation is not blocked then it puts the command
// in commands.
void CL_FillServerCommands(std::vector<std::string>& commands, int start, int end)
{
	// if we have irretrievably lost a reliable command, drop the connection
	if ( start <= clc.serverCommandSequence - MAX_RELIABLE_COMMANDS )
	{
		// when a demo record was started after the client got a whole bunch of
		// reliable commands then the client never got those first reliable commands
		if ( clc.demoplaying )
		{
			return;
		}

		Sys::Drop( "CL_FillServerCommand: a reliable command was cycled out" );
	}

	if ( end > clc.serverCommandSequence )
	{
		Sys::Drop( "CL_FillServerCommand: requested a command not received" );
	}

	for (int i = start; i <= end; i++) {
		const char* s = clc.serverCommands[ i & ( MAX_RELIABLE_COMMANDS - 1 ) ];

		std::string cmdText = s;
		if (CL_HandleServerCommand(s, cmdText)) {
			commands.push_back(std::move(cmdText));
		}
	}
}

/*
====================
CL_GetSnapshot
====================
*/
bool CL_GetSnapshot( int snapshotNumber, ipcSnapshot_t *snapshot )
{
	clSnapshot_t *clSnap;

	if ( snapshotNumber > cl.snap.messageNum )
	{
		Sys::Drop( "CL_GetSnapshot: snapshotNumber > cl.snapshot.messageNum" );
	}

	// if the frame has fallen out of the circular buffer, we can't return it
	if ( cl.snap.messageNum - snapshotNumber >= PACKET_BACKUP )
	{
		return false;
	}

	// if the frame is not valid, we can't return it
	clSnap = &cl.snapshots[ snapshotNumber & PACKET_MASK ];

	if ( !clSnap->valid )
	{
		return false;
	}

	// write the snapshot
	snapshot->b.snapFlags = clSnap->snapFlags;
	snapshot->b.ping = clSnap->ping;
	snapshot->b.serverTime = clSnap->serverTime;
	memcpy( snapshot->b.areamask, clSnap->areamask, sizeof( snapshot->b.areamask ) );
	snapshot->ps = clSnap->ps;
	snapshot->b.entities = clSnap->entities;

	CL_FillServerCommands(snapshot->b.serverCommands, clc.lastExecutedServerCommand + 1, clSnap->serverCommandNum);
	clc.lastExecutedServerCommand = clSnap->serverCommandNum;

	return true;
}

/*
====================
CL_ShutdownCGame
====================
*/
void CL_ShutdownCGame()
{
	cls.cgameStarted = false;

	if ( !cgvm.IsActive() )
	{
		return;
	}

	cgvm.CGameShutdown();
}

/*
 * ====================
 * LAN_ResetPings
 * ====================
 */
static void LAN_ResetPings( int source )
{
	int          count, i;
	serverInfo_t *servers = nullptr;

	count = 0;

	switch ( source )
	{
		case AS_LOCAL:
			servers = &cls.localServers[ 0 ];
			count = MAX_OTHER_SERVERS;
			break;

		case AS_GLOBAL:
			servers = &cls.globalServers[ 0 ];
			count = MAX_GLOBAL_SERVERS;
			break;
	}

	if ( servers )
	{
		for ( i = 0; i < count; i++ )
		{
			servers[ i ].pingStatus = pingStatus_t::WAITING;
			servers[ i ].pingAttempts = 0;
			servers[ i ].ping = -1;
		}
	}
}

/*
 * ====================
 * LAN_GetServerCount
 * ====================
 */
static int LAN_GetServerCount( int source )
{
	switch ( source )
	{
		case AS_LOCAL:
			return cls.numlocalservers;

		case AS_GLOBAL:
			return cls.numglobalservers;
	}

	return 0;
}

/*
 * ====================
 * LAN_GetServerInfo
 * ====================
 */
static void LAN_GetServerInfo( int source, int n, trustedServerInfo_t &trustedInfo, std::string &info )
{
	serverInfo_t *server = nullptr;

	switch ( source )
	{
		case AS_LOCAL:
			if ( n >= 0 && n < MAX_OTHER_SERVERS )
			{
				server = &cls.localServers[ n ];
			}

			break;

		case AS_GLOBAL:
			if ( n >= 0 && n < MAX_GLOBAL_SERVERS )
			{
				server = &cls.globalServers[ n ];
			}

			break;
	}

	if ( server )
	{
		trustedInfo.responseProto = server->responseProto;
		Q_strncpyz( trustedInfo.addr, Net::AddressToString( server->adr, true ).c_str(), sizeof( trustedInfo.addr ) );
		Q_strncpyz( trustedInfo.featuredLabel, server->label, sizeof( trustedInfo.featuredLabel ) );
		info = server->infoString;
	}
	else
	{
		trustedInfo = {};
		info.clear();
	}
}

/*
 * ====================
 * LAN_GetServerPing
 * ====================
 */
static int LAN_GetServerPing( int source, int n )
{
	serverInfo_t *server = nullptr;

	switch ( source )
	{
		case AS_LOCAL:
			if ( n >= 0 && n < MAX_OTHER_SERVERS )
			{
				server = &cls.localServers[ n ];
			}

			break;

		case AS_GLOBAL:
			if ( n >= 0 && n < MAX_GLOBAL_SERVERS )
			{
				server = &cls.globalServers[ n ];
			}

			break;
	}

	if ( server )
	{
		return server->ping;
	}

	return -1;
}

/*
 * ====================
 * LAN_MarkServerVisible
 * ====================
 */
static void LAN_MarkServerVisible( int source, int n, bool visible )
{
	if ( n == -1 )
	{
		int          count = MAX_OTHER_SERVERS;
		serverInfo_t *server = nullptr;

		switch ( source )
		{
			case AS_LOCAL:
				server = &cls.localServers[ 0 ];
				break;

			case AS_GLOBAL:
				server = &cls.globalServers[ 0 ];
				count = MAX_GLOBAL_SERVERS;
				break;
		}

		if ( server )
		{
			for ( n = 0; n < count; n++ )
			{
				server[ n ].visible = visible;
			}
		}
	}
	else
	{
		switch ( source )
		{
			case AS_LOCAL:
				if ( n >= 0 && n < MAX_OTHER_SERVERS )
				{
					cls.localServers[ n ].visible = visible;
				}

				break;

			case AS_GLOBAL:
				if ( n >= 0 && n < MAX_GLOBAL_SERVERS )
				{
					cls.globalServers[ n ].visible = visible;
				}

				break;
		}
	}
}

/*
 * =======================
 * LAN_ServerIsVisible
 * =======================
 */
static int LAN_ServerIsVisible( int source, int n )
{
	switch ( source )
	{
		case AS_LOCAL:
			if ( n >= 0 && n < MAX_OTHER_SERVERS )
			{
				return cls.localServers[ n ].visible;
			}

			break;

		case AS_GLOBAL:
			if ( n >= 0 && n < MAX_GLOBAL_SERVERS )
			{
				return cls.globalServers[ n ].visible;
			}

			break;
	}

	return false;
}

/*
 * ====================
 * Key_GetCatcher
 * ====================
 */
int Key_GetCatcher()
{
	return cls.keyCatchers;
}

/*
 * ====================
 * Key_SetCatcher
 * ====================
 */
void Key_SetCatcher( int catcher )
{
	// NERVE - SMF - console overrides everything
	if ( cls.keyCatchers & KEYCATCH_CONSOLE )
	{
		cls.keyCatchers = catcher | KEYCATCH_CONSOLE;
	}
	else
	{
		cls.keyCatchers = catcher;
	}
}

/*
====================
CL_InitCGame

Should only by called by CL_StartHunkUsers
====================
*/
void CL_InitCGame()
{
	const char *info;
	const char *mapname;
	int        t1, t2;

	t1 = Sys::Milliseconds();

	// put away the console
	Con_Close();

	// find the current mapname
	info = cl.gameState[ CS_SERVERINFO ].c_str();
	mapname = Info_ValueForKey( info, "mapname" );
	Com_sprintf( cl.mapname, sizeof( cl.mapname ), "maps/%s.bsp", mapname );


	cls.state = connstate_t::CA_LOADING;

	// init for this gamestate
	cgvm.CGameInit(clc.serverMessageSequence, clc.clientNum);

	// we will send a usercmd this frame, which
	// will cause the server to send us the first snapshot
	cls.state = connstate_t::CA_PRIMED;

	t2 = Sys::Milliseconds();

	Log::Debug( "CL_InitCGame: %5.2fs", ( t2 - t1 ) / 1000.0 );

	// have the renderer touch all its images, so they are present
	// on the card even if the driver does deferred loading
	re.EndRegistration();

	// Cause any input while loading to be dropped and forget what's pressed
	IN_DropInputsForFrame();
	CL_ClearKeys();
	Key_ClearStates();
}

/*
=====================
CL_CGameRendering
=====================
*/
void CL_CGameRendering()
{
	cgvm.CGameDrawActiveFrame(cl.serverTime, clc.demoplaying);
}

/*
=================
CL_AdjustTimeDelta

Adjust the clients view of server time.

We attempt to have cl.serverTime exactly equal the server's view
of time plus the timeNudge, but with variable latencies over
the Internet, it will often need to drift a bit to match conditions.

Our ideal time would be to have the adjusted time approach, but not pass,
the very latest snapshot.

Adjustments are only made when a new snapshot arrives with a rational
latency, which keeps the adjustment process framerate independent and
prevents massive overadjustment during times of significant packet loss
or bursted delayed packets.
=================
*/

static const int RESET_TIME = 500;

void CL_AdjustTimeDelta()
{
//	int             resetTime;
	int newDelta;
	int deltaDelta;

	cl.newSnapshots = false;

	// the delta never drifts when replaying a demo
	if ( clc.demoplaying )
	{
		return;
	}

	// if the current time is WAY off, just correct to the current value

	/*
	        if(com_sv_running->integer)
	        {
	                resetTime = 100;
	        }
	        else
	        {
	                resetTime = RESET_TIME;
	        }
	*/

	newDelta = cl.snap.serverTime - cls.realtime;
	deltaDelta = abs( newDelta - cl.serverTimeDelta );

	if ( deltaDelta > RESET_TIME )
	{
		cl.serverTimeDelta = newDelta;
		cl.oldServerTime = cl.snap.serverTime; // FIXME: is this a problem for cgame?
		cl.serverTime = cl.snap.serverTime;

		if ( cl_showTimeDelta->integer )
		{
			Log::Notice( "<RESET> " );
		}
	}
	else if ( deltaDelta > 100 )
	{
		// fast adjust, cut the difference in half
		if ( cl_showTimeDelta->integer )
		{
			Log::Notice( "<FAST> " );
		}

		cl.serverTimeDelta = ( cl.serverTimeDelta + newDelta ) >> 1;
	}
	else
	{
		// slow drift adjust, only move 1 or 2 msec

		// if any of the frames between this and the previous snapshot
		// had to be extrapolated, nudge our sense of time back a little
		// the granularity of +1 / -2 is too high for timescale modified frametimes
		if ( com_timescale->value == 0 || com_timescale->value == 1 )
		{
			if ( cl.extrapolatedSnapshot )
			{
				cl.extrapolatedSnapshot = false;
				cl.serverTimeDelta -= 2;
			}
			else
			{
				// otherwise, move our sense of time forward to minimize total latency
				cl.serverTimeDelta++;
			}
		}
	}

	if ( cl_showTimeDelta->integer )
	{
		Log::Notice("%i ", cl.serverTimeDelta );
	}
}

/*
==================
CL_FirstSnapshot
==================
*/
void CL_FirstSnapshot()
{
	// ignore snapshots that don't have entities
	if ( cl.snap.snapFlags & SNAPFLAG_NOT_ACTIVE )
	{
		return;
	}

	cls.state = connstate_t::CA_ACTIVE;

	// set the timedelta so we are exactly on this first frame
	cl.serverTimeDelta = cl.snap.serverTime - cls.realtime;
	cl.oldServerTime = cl.snap.serverTime;

	clc.timeDemoBaseTime = cl.snap.serverTime;

	// if this is the first frame of active play,
	// execute the contents of activeAction now
	// this is to allow scripting a timedemo to start right
	// after loading
	if ( cl_activeAction->string[ 0 ] )
	{
		Cmd::BufferCommandText(cl_activeAction->string);
		Cvar_Set( "activeAction", "" );
	}

#if defined(USE_MUMBLE)
	if ( ( cl_useMumble->integer ) && !mumble_islinked() )
	{
		int ret = mumble_link( CLIENT_WINDOW_TITLE );
		Log::Notice(ret == 0 ? "Mumble: Linking to Mumble application okay" : "Mumble: Linking to Mumble application failed" );
	}
#endif

	// resend userinfo upon entering the game, as some cvars may
    // not have had the CVAR_USERINFO flag set until loading cgame
	cvar_modifiedFlags |= CVAR_USERINFO;
}

/*
==================
CL_SetCGameTime
==================
*/
void CL_SetCGameTime()
{
	// getting a valid frame message ends the connection process
	if ( cls.state != connstate_t::CA_ACTIVE )
	{
		if ( cls.state != connstate_t::CA_PRIMED )
		{
			return;
		}

		if ( clc.demoplaying )
		{
			// we shouldn't get the first snapshot on the same frame
			// as the gamestate, because it causes a bad time skip
			if ( !clc.firstDemoFrameSkipped )
			{
				clc.firstDemoFrameSkipped = true;
				return;
			}

			CL_ReadDemoMessage();
		}

		if ( cl.newSnapshots )
		{
			cl.newSnapshots = false;
			CL_FirstSnapshot();
		}

		if ( cls.state != connstate_t::CA_ACTIVE )
		{
			return;
		}
	}

	// if we have gotten to this point, cl.snap is guaranteed to be valid
	if ( !cl.snap.valid )
	{
		Sys::Drop( "CL_SetCGameTime: !cl.snap.valid" );
	}

	if ( cl.snap.serverTime < cl.oldFrameServerTime )
	{
		// Ridah, if this is a localhost, then we are probably loading a savegame
		if ( !Q_stricmp( cls.servername, "loopback" ) )
		{
			// do nothing?
			CL_FirstSnapshot();
		}
		else
		{
			Sys::Drop( "cl.snap.serverTime < cl.oldFrameServerTime" );
		}
	}

	cl.oldFrameServerTime = cl.snap.serverTime;

	// cl_timeNudge is a user adjustable cvar that allows more
	// or less latency to be added in the interest of better
	// smoothness or better responsiveness.
	int tn = Math::Clamp( cl_timeNudge->integer, -30, 30 );

	cl.serverTime = cls.realtime + cl.serverTimeDelta - tn;

	// guarantee that time will never flow backwards, even if
	// serverTimeDelta made an adjustment or cl_timeNudge was changed
	if ( cl.serverTime < cl.oldServerTime )
	{
		cl.serverTime = cl.oldServerTime;
	}

	cl.oldServerTime = cl.serverTime;

	// note if we are almost past the latest frame (without timeNudge),
	// so we will try and adjust back a bit when the next snapshot arrives
	if ( cls.realtime + cl.serverTimeDelta >= cl.snap.serverTime - 5 )
	{
		cl.extrapolatedSnapshot = true;
	}

	// if we have gotten new snapshots, drift serverTimeDelta
	// don't do this every frame, or a period of packet loss would
	// make a huge adjustment
	if ( cl.newSnapshots )
	{
		CL_AdjustTimeDelta();
	}

	if ( !clc.demoplaying )
	{
		return;
	}

	// if we are playing a demo back, we can just keep reading
	// messages from the demo file until the cgame definitely
	// has valid snapshots to interpolate between

	// a timedemo will always use a deterministic set of time samples
	// no matter what speed machine it is run on
	if ( cvar_demo_timedemo.Get() )
	{
		if ( !clc.timeDemoStart )
		{
			clc.timeDemoStart = Sys::Milliseconds();
		}

		clc.timeDemoFrames++;
		cl.serverTime = clc.timeDemoBaseTime + clc.timeDemoFrames * 50;
	}

	while ( cl.serverTime >= cl.snap.serverTime )
	{
		// feed another message, which should change
		// the contents of cl.snap
		CL_ReadDemoMessage();

		if ( cls.state != connstate_t::CA_ACTIVE )
		{
			Cvar_Set( "timescale", "1" );
			return; // end of demo
		}
	}
}

/**
 * is notified by teamchanges.
 * while most notifications will come from the cgame, due to game semantics,
 * other code may assume a change to a non-team "0", like e.g. /disconnect
 */
void  CL_OnTeamChanged( int newTeam )
{
	static bool first = true;
	if ( p_team.Get() == newTeam && !first )
	{
		return;
	}
	first = false;

	Cvar::SetValueForce( "p_team", std::to_string( newTeam ) );

	/* set all team specific teambindings */
	Keyboard::SetTeam( newTeam );

	/*
	 * execute a possibly team aware config each time the team was changed.
	 * the user can use the cvars p_team or p_teamname (if the cgame sets it) within that config
	 * to e.g. execute team specific configs, like cg_<team>Config did previously, but with less dependency on the cgame
	 */
	Cmd::BufferCommandText( "exec -f " TEAMCONFIG_NAME );
}

CGameVM::CGameVM(): VM::VMBase("cgame", Cvar::CHEAT), services(nullptr), cmdBuffer("client")
{
}

void CGameVM::Start()
{
	services = std::unique_ptr<VM::CommonVMServices>(new VM::CommonVMServices(*this, "CGame", FS::Owner::CGAME, Cmd::CGAME_VM));
	this->Create();
	this->CGameStaticInit();
}

void CGameVM::CGameStaticInit()
{
	this->SendMsg<CGameStaticInitMsg>(Sys::Milliseconds());
}

void CGameVM::CGameInit(int serverMessageNum, int clientNum)
{
	this->SendMsg<CGameInitMsg>(serverMessageNum, clientNum, cls.glconfig, cl.gameState);
	NetcodeTable psTable;
	size_t psSize;
	this->SendMsg<VM::GetNetcodeTablesMsg>(psTable, psSize);
	MSG_InitNetcodeTables(std::move(psTable), psSize);
}

void CGameVM::CGameShutdown()
{
	try {
		this->SendMsg<CGameShutdownMsg>();
	} catch (Sys::DropErr& err) {
		Log::Notice("Error during cgame shutdown: %s", err.what());
	}
	this->Free();
	services = nullptr;
}

void CGameVM::CGameDrawActiveFrame(int serverTime,  bool demoPlayback)
{
	this->SendMsg<CGameDrawActiveFrameMsg>(serverTime, demoPlayback);
}

bool CGameVM::CGameKeyDownEvent(Keyboard::Key key, bool repeat)
{
	if (!key.IsValid())
		return false;

	bool consumed;
	this->SendMsg<CGameKeyDownEventMsg>(key, repeat, consumed);
	return consumed;
}

void CGameVM::CGameKeyUpEvent(Keyboard::Key key)
{
	if (!key.IsValid())
		return;

	this->SendMsg<CGameKeyUpEventMsg>(key);
}

void CGameVM::CGameMouseEvent(int dx, int dy)
{
	this->SendMsg<CGameMouseEventMsg>(dx, dy);
}

void CGameVM::CGameMousePosEvent(int x, int y)
{
	this->SendMsg<CGameMousePosEventMsg>(x, y);
}

void CGameVM::CGameFocusEvent(bool focus)
{
	this->SendMsg<CGameFocusEventMsg>(focus);
}


void CGameVM::CGameTextInputEvent(int c)
{
	this->SendMsg<CGameCharacterInputMsg>(c);
}

void CGameVM::CGameRocketInit()
{
	this->SendMsg<CGameRocketInitMsg>(cls.glconfig);
}

void CGameVM::CGameRocketFrame()
{
	cgClientState_t state;
	state.connectPacketCount = clc.connectPacketCount;
	state.connState = cls.state;
	Q_strncpyz( state.servername, cls.servername, sizeof( state.servername ) );
	Q_strncpyz( state.updateInfoString, cls.updateInfoString, sizeof( state.updateInfoString ) );
	Q_strncpyz( state.messageString, clc.serverMessage, sizeof( state.messageString ) );
	state.clientNum = cl.snap.ps.clientNum;
	this->SendMsg<CGameRocketFrameMsg>(state);
}

void CGameVM::CGameConsoleLine(const std::string& str)
{
	this->SendMsg<CGameConsoleLineMsg>(str);
}

void CGameVM::Syscall(uint32_t id, Util::Reader reader, IPC::Channel& channel)
{
	int major = id >> 16;
	int minor = id & 0xffff;
	if (major == VM::QVM) {
		this->QVMSyscall(minor, reader, channel);

	} else if (major == VM::COMMAND_BUFFER) {
		this->cmdBuffer.Syscall(minor, reader, channel);

	} else if (major < VM::LAST_COMMON_SYSCALL) {
		services->Syscall(major, minor, std::move(reader), channel);

	} else {
		Sys::Drop("Bad major game syscall number: %d", major);
	}
}

void CGameVM::QVMSyscall(int syscallNum, Util::Reader& reader, IPC::Channel& channel)
{
	switch (syscallNum) {
		case CG_SENDCLIENTCOMMAND:
			IPC::HandleMsg<SendClientCommandMsg>(channel, std::move(reader), [this] (const std::string& command) {
				CL_AddReliableCommand(command.c_str());
			});
			break;

		case CG_UPDATESCREEN:
			IPC::HandleMsg<UpdateScreenMsg>(channel, std::move(reader), [this]  {
				SCR_UpdateScreen();
			});
			break;

		case CG_CM_BATCHMARKFRAGMENTS:
			IPC::HandleMsg<CMBatchMarkFragments>(channel, std::move(reader), [this] (
				unsigned maxPoints,
				unsigned maxFragments,
				const std::vector<markMsgInput_t>& inputs,
				std::vector<markMsgOutput_t>& outputs)
			{
				outputs.reserve(inputs.size());
				std::vector<std::array<float, 3>> pointBuf(maxPoints);
				std::vector<markFragment_t> fragmentBuf(maxFragments);

				for (const markMsgInput_t& input : inputs)
				{
					auto& inputPoints = input.first;
					auto& projection = input.second;
					size_t numFragments = re.MarkFragments(
						inputPoints.size(),
						reinterpret_cast<const vec3_t*>(inputPoints.data()),
						projection.data(),
						maxPoints,
						reinterpret_cast<float*>(pointBuf.data()),
						maxFragments,
						fragmentBuf.data());
					size_t numPoints;
					if (numFragments == 0) {
						numPoints = 0;
					} else {
						// HACK: assume last fragment is last
						const markFragment_t& lastFragment = fragmentBuf[numFragments - 1];
						numPoints = lastFragment.firstPoint + lastFragment.numPoints;
					}
					outputs.emplace_back(
						std::vector<std::array<float, 3>>(pointBuf.data(), pointBuf.data() + numPoints),
						std::vector<markFragment_t>(fragmentBuf.data(), fragmentBuf.data() + numFragments)
					);
				}
			});
			break;

		case CG_GETCURRENTSNAPSHOTNUMBER:
			IPC::HandleMsg<GetCurrentSnapshotNumberMsg>(channel, std::move(reader), [this] (int& number, int& serverTime) {
				number = cl.snap.messageNum;
				serverTime = cl.snap.serverTime;
			});
			break;

		case CG_GETSNAPSHOT:
			IPC::HandleMsg<GetSnapshotMsg>(channel, std::move(reader), [this] (int number, bool& res, ipcSnapshot_t& snapshot) {
				res = CL_GetSnapshot(number, &snapshot);
			});
			break;

		case CG_GETCURRENTCMDNUMBER:
			IPC::HandleMsg<GetCurrentCmdNumberMsg>(channel, std::move(reader), [this] (int& number) {
				number = CL_GetCurrentCmdNumber();
			});
			break;

		case CG_GETUSERCMD:
			IPC::HandleMsg<GetUserCmdMsg>(channel, std::move(reader), [this] (int number, bool& res, usercmd_t& cmd) {
				res = CL_GetUserCmd(number, &cmd);
			});
			break;

		case CG_SETUSERCMDVALUE:
			IPC::HandleMsg<SetUserCmdValueMsg>(channel, std::move(reader), [this] (int stateValue, int flags, float scale) {
				cl.cgameUserCmdValue = stateValue;
				cl.cgameFlags = flags;
				cl.cgameSensitivity = scale;
			});
			break;

		case CG_REGISTER_BUTTON_COMMANDS:
			IPC::HandleMsg<RegisterButtonCommandsMsg>(channel, std::move(reader), [this] (const std::string& commands) {
				CL_RegisterButtonCommands(commands.c_str());
			});
			break;

		case CG_NOTIFY_TEAMCHANGE:
			IPC::HandleMsg<NotifyTeamChangeMsg>(channel, std::move(reader), [this] (int team) {
				CL_OnTeamChanged(team);
			});
			break;

		case CG_PREPAREKEYUP:
			IPC::HandleMsg<PrepareKeyUpMsg>(channel, std::move(reader), [this] {
				IN_PrepareKeyUp();
			});
			break;

		// All sounds

        case CG_S_REGISTERSOUND:
            IPC::HandleMsg<Audio::RegisterSoundMsg>(channel, std::move(reader), [this] (const std::string& sample, int& handle) {
                handle = Audio::RegisterSFX(sample.c_str());
            });
            break;

		// All renderer

		case CG_R_SETALTSHADERTOKENS:
			IPC::HandleMsg<Render::SetAltShaderTokenMsg>(channel, std::move(reader), [this] (const std::string& tokens) {
				re.SetAltShaderTokens(tokens.c_str());
			});
			break;

		case CG_R_GETSHADERNAMEFROMHANDLE:
			IPC::HandleMsg<Render::GetShaderNameFromHandleMsg>(channel, std::move(reader), [this] (int handle, std::string& name) {
			    name = re.ShaderNameFromHandle(handle);
			});
			break;

		case CG_R_LOADWORLDMAP:
			IPC::HandleMsg<Render::LoadWorldMapMsg>(channel, std::move(reader), [this] (const std::string& mapName) {
				re.SetWorldVisData(CM_ClusterPVS(-1));
				re.LoadWorld(mapName.c_str());
			});
			break;

		case CG_R_REGISTERMODEL:
			IPC::HandleMsg<Render::RegisterModelMsg>(channel, std::move(reader), [this] (const std::string& name, int& handle) {
				handle = re.RegisterModel(name.c_str());
			});
			break;

		case CG_R_REGISTERSKIN:
			IPC::HandleMsg<Render::RegisterSkinMsg>(channel, std::move(reader), [this] (const std::string& name, int& handle) {
				handle = re.RegisterSkin(name.c_str());
			});
			break;

		case CG_R_REGISTERSHADER:
			IPC::HandleMsg<Render::RegisterShaderMsg>(channel, std::move(reader), [this] (const std::string& name, int flags, int& handle) {
				handle = re.RegisterShader(name.c_str(), flags);
			});
			break;

		case CG_R_MODELBOUNDS:
			IPC::HandleMsg<Render::ModelBoundsMsg>(channel, std::move(reader), [this] (int handle, std::array<float, 3>& mins, std::array<float, 3>& maxs) {
				re.ModelBounds(handle, mins.data(), maxs.data());
			});
			break;

		case CG_R_LERPTAG:
			IPC::HandleMsg<Render::LerpTagMsg>(channel, std::move(reader), [this] (const refEntity_t& entity, const std::string& tagName, int startIndex, orientation_t& tag, int& res) {
				res = re.LerpTag(&tag, &entity, tagName.c_str(), startIndex);
			});
			break;

		case CG_R_REMAP_SHADER:
			IPC::HandleMsg<Render::RemapShaderMsg>(channel, std::move(reader), [this] (const std::string& oldShader, const std::string& newShader, const std::string& timeOffset) {
				re.RemapShader(oldShader.c_str(), newShader.c_str(), timeOffset.c_str());
			});
			break;

		case CG_R_BATCHINPVS:
			IPC::HandleMsg<Render::BatchInPVSMsg>(channel, std::move(reader), [this] (
				const std::array<float, 3>& origin,
				const std::vector<std::array<float, 3>>& posEntities,
				std::vector<bool>& inPVS)
			{
				inPVS.reserve(posEntities.size());

				for (const auto& posEntity : posEntities)
				{
					inPVS.push_back(re.inPVS(origin.data(), posEntity.data()));
				}
			});
			break;

		case CG_R_LIGHTFORPOINT:
			IPC::HandleMsg<Render::LightForPointMsg>(channel, std::move(reader), [this] (std::array<float, 3> point, std::array<float, 3>& ambient, std::array<float, 3>& directed, std::array<float, 3>& dir, int& res) {
				res = re.LightForPoint(point.data(), ambient.data(), directed.data(), dir.data());
			});
			break;

		case CG_R_REGISTERANIMATION:
			IPC::HandleMsg<Render::RegisterAnimationMsg>(channel, std::move(reader), [this] (const std::string& name, int& handle) {
				handle = re.RegisterAnimation(name.c_str());
			});
			break;

		case CG_R_BUILDSKELETON:
			IPC::HandleMsg<Render::BuildSkeletonMsg>(channel, std::move(reader), [this] (int anim, int startFrame, int endFrame, float frac, bool clearOrigin, refSkeleton_t& skel, int& res) {
				res = re.BuildSkeleton(&skel, anim, startFrame, endFrame, frac, clearOrigin);
			});
			break;

		case CG_R_GETANIMATION:
			IPC::HandleMsg<Render::GetAnimationMsg>( channel, std::move( reader ), [this]( int anim, skelAnimation_t& res ) {
				res = re.GetAnimation( anim );
			});
			break;

		case CG_R_BATCHGETANIMATION:
			IPC::HandleMsg<Render::BatchGetAnimationsMsg>( channel, std::move( reader ), [this](
				const std::vector<qhandle_t>& anims,
				std::vector<skelAnimation_t>& skelAnimations ) {
					skelAnimations.reserve( anims.size() );

					for ( const qhandle_t anim : anims ) {
						skelAnimations.push_back( re.GetAnimation( anim ) );
					}
			});
			break;

		case CG_R_BONEINDEX:
			IPC::HandleMsg<Render::BoneIndexMsg>(channel, std::move(reader), [this] (int model, const std::string& boneName, int& index) {
				index = re.BoneIndex(model, boneName.c_str());
			});
			break;

		case CG_R_ANIMNUMFRAMES:
			IPC::HandleMsg<Render::AnimNumFramesMsg>(channel, std::move(reader), [this] (int anim, int& res) {
				res = re.AnimNumFrames(anim);
			});
			break;

		case CG_R_ANIMFRAMERATE:
			IPC::HandleMsg<Render::AnimFrameRateMsg>(channel, std::move(reader), [this] (int anim, int& res) {
				res = re.AnimFrameRate(anim);
			});
			break;

		case CG_REGISTERVISTEST:
			IPC::HandleMsg<Render::RegisterVisTestMsg>(channel, std::move(reader), [this] (int& handle) {
				handle = re.RegisterVisTest();
			});
			break;

		case CG_CHECKVISIBILITY:
			IPC::HandleMsg<Render::CheckVisibilityMsg>(channel, std::move(reader), [this] (int handle, float& res) {
				res = re.CheckVisibility(handle);
			});
			break;

		case CG_R_GETTEXTURESIZE:
			IPC::HandleMsg<Render::GetTextureSizeMsg>(channel, std::move(reader), [this] (qhandle_t handle, int& x, int& y) {
				re.GetTextureSize(handle, &x, &y);
			});
			break;

		case CG_R_GENERATETEXTURE:
			IPC::HandleMsg<Render::GenerateTextureMsg>(channel, std::move(reader), [this] (std::vector<byte> data, int x, int y, qhandle_t& handle) {
				handle = re.GenerateTexture(data.data(), x, y);
			});
			break;

		// All keys

		case CG_KEY_GETCATCHER:
			IPC::HandleMsg<Keyboard::GetCatcherMsg>(channel, std::move(reader), [this] (int& catcher) {
				catcher = Key_GetCatcher();
			});
			break;

		case CG_KEY_SETCATCHER:
			IPC::HandleMsg<Keyboard::SetCatcherMsg>(channel, std::move(reader), [this] (int catcher) {
				Key_SetCatcher(catcher);
			});
			break;

		case CG_KEY_GETKEYSFORBINDS:
			IPC::HandleMsg<Keyboard::GetKeysForBindsMsg>(channel, std::move(reader), [this] (int team, const std::vector<std::string>& binds, std::vector<std::vector<Keyboard::Key>>& result) {
				for (const auto& bind : binds) {
					result.push_back(Keyboard::GetKeysBoundTo(team, bind));
				}
			});
			break;

		case CG_KEY_GETCHARFORSCANCODE:
			IPC::HandleMsg<Keyboard::GetCharForScancodeMsg>(channel, std::move(reader), [this] (int scancode, int& result) {
				result = Keyboard::GetCharForScancode(scancode);
				if (!result) {
					// Not sure if this fallback is ever useful. Usually SDL falls back on QWERTY itself.
					result = Keyboard::ScancodeToAscii(scancode);
				}
			});
			break;

		case CG_KEY_SETBINDING:
			IPC::HandleMsg<Keyboard::SetBindingMsg>(channel, std::move(reader), [this] (Keyboard::Key key, int team, std::string cmd) {
				if (key.IsBindable()) {
					Keyboard::SetBinding(key, team, std::move(cmd));
				} else {
					Log::Warn("Invalid key in SetBindingMsg");
				}
			});
			break;

		case CG_KEY_GETCONSOLEKEYS:
			IPC::HandleMsg<Keyboard::GetConsoleKeysMsg>(channel, std::move(reader), [this] (std::vector<Keyboard::Key>& keys) {
				keys = Keyboard::GetConsoleKeys();
			});
			break;

		case CG_KEY_SETCONSOLEKEYS:
			IPC::HandleMsg<Keyboard::SetConsoleKeysMsg>(channel, std::move(reader), [this] (const std::vector<Keyboard::Key>& keys) {
				Keyboard::SetConsoleKeys(keys);
			});
			break;

		case CG_KEY_CLEARCMDBUTTONS:
			IPC::HandleMsg<Keyboard::ClearCmdButtonsMsg>(channel, std::move(reader), [this] {
				CL_ClearCmdButtons();
			});
			break;

		case CG_KEY_CLEARSTATES:
			IPC::HandleMsg<Keyboard::ClearStatesMsg>(channel, std::move(reader), [this] {
				Key_ClearStates();
			});
			break;

		case CG_KEY_KEYSDOWN:
			IPC::HandleMsg<Keyboard::KeysDownMsg>(channel, std::move(reader), [this] (std::vector<Keyboard::Key> keys, std::vector<bool>& list) {
				list.reserve(keys.size());
				for (Keyboard::Key key : keys)
				{
					if (key == Keyboard::Key(keyNum_t::K_KP_NUMLOCK))
					{
						list.push_back(IN_IsNumLockOn());
					}
					else
					{
						list.push_back(Keyboard::IsDown(key));
					}
				}
			});
			break;

		// Mouse

		case CG_MOUSE_SETMOUSEMODE:
			IPC::HandleMsg<Mouse::SetMouseMode>(channel, std::move(reader), &IN_SetMouseMode);
			break;

		// All LAN

		case CG_LAN_GETSERVERCOUNT:
			IPC::HandleMsg<LAN::GetServerCountMsg>(channel, std::move(reader), [this] (int source, int& count) {
				count = LAN_GetServerCount(source);
			});
			break;

		case CG_LAN_GETSERVERINFO:
			IPC::HandleMsg<LAN::GetServerInfoMsg>(channel, std::move(reader), [this] (int source, int n, trustedServerInfo_t& trustedInfo, std::string& info) {
				LAN_GetServerInfo(source, n, trustedInfo, info);
			});
			break;

		case CG_LAN_GETSERVERPING:
			IPC::HandleMsg<LAN::GetServerPingMsg>(channel, std::move(reader), [this] (int source, int n, int& ping) {
				ping = LAN_GetServerPing(source, n);
			});
			break;

		case CG_LAN_MARKSERVERVISIBLE:
			IPC::HandleMsg<LAN::MarkServerVisibleMsg>(channel, std::move(reader), [this] (int source, int n, bool visible) {
				LAN_MarkServerVisible(source, n, visible);
			});
			break;

		case CG_LAN_SERVERISVISIBLE:
			IPC::HandleMsg<LAN::ServerIsVisibleMsg>(channel, std::move(reader), [this] (int source, int n, bool& visible) {
				visible = LAN_ServerIsVisible(source, n);
			});
			break;

		case CG_LAN_UPDATEVISIBLEPINGS:
			IPC::HandleMsg<LAN::UpdateVisiblePingsMsg>(channel, std::move(reader), [this] (int source, bool& res) {
				res = CL_UpdateVisiblePings_f(source);
			});
			break;

		case CG_LAN_RESETPINGS:
			IPC::HandleMsg<LAN::ResetPingsMsg>(channel, std::move(reader), [this] (int n) {
				LAN_ResetPings(n);
			});
			break;

		case CG_LAN_SERVERSTATUS:
			IPC::HandleMsg<LAN::ServerStatusMsg>(channel, std::move(reader), [this] (const std::string& serverAddress, int len, std::string& status, int& res) {
				std::unique_ptr<char[]> buffer(new char[len]);
				buffer[0] = '\0';
				res = CL_ServerStatus(serverAddress.c_str(), buffer.get(), len);
				status.assign(buffer.get());
			});
			break;

		case CG_LAN_RESETSERVERSTATUS:
			IPC::HandleMsg<LAN::ResetServerStatusMsg>(channel, std::move(reader), [this] {
				CL_ServerStatus(nullptr, nullptr, 0);
			});
			break;

	default:
		Sys::Drop("Bad CGame QVM syscall minor number: %d", syscallNum);
	}
}

//TODO move somewhere else
template<typename Func, typename Id, typename... MsgArgs> void HandleMsg(IPC::Message<Id, MsgArgs...>, Util::Reader reader, Func&& func)
{
    using Message = IPC::Message<Id, MsgArgs...>;

    typename IPC::detail::MapTuple<typename Message::Inputs>::type inputs;
    reader.FillTuple<0>(Util::TypeListFromTuple<typename Message::Inputs>(), inputs);

    Util::apply(std::forward<Func>(func), std::move(inputs));
}

template<typename Msg, typename Func> void HandleMsg(Util::Reader reader, Func&& func)
{
    HandleMsg(Msg(), std::move(reader), std::forward<Func>(func));
}

CGameVM::CmdBuffer::CmdBuffer(std::string name): IPC::CommandBufferHost(name) {
}

void CGameVM::CmdBuffer::HandleCommandBufferSyscall(int major, int minor, Util::Reader& reader) {
	if (major == VM::QVM) {
		switch (minor) {

			// All sounds

			case CG_S_STARTSOUND:
				HandleMsg<Audio::StartSoundMsg>(std::move(reader), [this] (bool isPositional, Vec3 origin, int entityNum, int sfx) {
					Audio::StartSound((isPositional ? -1 : entityNum), origin, sfx);
				});
				break;

			case CG_S_STARTLOCALSOUND:
				HandleMsg<Audio::StartLocalSoundMsg>(std::move(reader), [this] (int sfx) {
					Audio::StartLocalSound(sfx);
				});
				break;

			case CG_S_CLEARLOOPINGSOUNDS:
				HandleMsg<Audio::ClearLoopingSoundsMsg>(std::move(reader), [this] {
					Audio::ClearAllLoopingSounds();
				});
				break;

			case CG_S_ADDLOOPINGSOUND:
				HandleMsg<Audio::AddLoopingSoundMsg>(std::move(reader), [this] (int entityNum, int sfx) {
					Audio::AddEntityLoopingSound(entityNum, sfx);
				});
				break;

			case CG_S_STOPLOOPINGSOUND:
				HandleMsg<Audio::StopLoopingSoundMsg>(std::move(reader), [this] (int entityNum) {
					Audio::ClearLoopingSoundsForEntity(entityNum);
				});
				break;

			case CG_S_UPDATEENTITYPOSITION:
				HandleMsg<Audio::UpdateEntityPositionMsg>(std::move(reader), [this] (int entityNum, Vec3 position) {
					Audio::UpdateEntityPosition(entityNum, position);
				});
				break;

			case CG_S_RESPATIALIZE:
				HandleMsg<Audio::RespatializeMsg>(std::move(reader), [this] (int entityNum, const std::array<Vec3, 3>& axis) {
					Audio::UpdateListener(entityNum, axis.data());
				});
				break;

			case CG_S_STARTBACKGROUNDTRACK:
				HandleMsg<Audio::StartBackgroundTrackMsg>(std::move(reader), [this] (const std::string& intro, const std::string& loop) {
					Audio::StartMusic(intro.c_str(), loop.c_str());
				});
				break;

			case CG_S_STOPBACKGROUNDTRACK:
				HandleMsg<Audio::StopBackgroundTrackMsg>(std::move(reader), [this] {
					Audio::StopMusic();
				});
				break;

			case CG_S_UPDATEENTITYVELOCITY:
				HandleMsg<Audio::UpdateEntityVelocityMsg>(std::move(reader), [this] (int entityNum, Vec3 velocity) {
					Audio::UpdateEntityVelocity(entityNum, velocity);
				});
				break;

			case CG_S_UPDATEENTITYPOSITIONVELOCITY:
				HandleMsg<Audio::UpdateEntityPositionVelocityMsg>(std::move(reader), [this] (int entityNum, Vec3 position, Vec3 velocity) {
					Audio::UpdateEntityPosition(entityNum, position);
					Audio::UpdateEntityVelocity(entityNum, velocity);
				});
				break;

			case CG_S_SETREVERB:
				HandleMsg<Audio::SetReverbMsg>(std::move(reader), [this] (int slotNum, const std::string& name, float ratio) {
					Audio::SetReverb(slotNum, name, ratio);
				});
				break;

			case CG_S_BEGINREGISTRATION:
				HandleMsg<Audio::BeginRegistrationMsg>(std::move(reader), [this] {
					Audio::BeginRegistration();
				});
				break;

			case CG_S_ENDREGISTRATION:
				HandleMsg<Audio::EndRegistrationMsg>(std::move(reader), [this] {
					Audio::EndRegistration();
				});
				break;

            // All renderer

            case CG_R_SCISSOR_ENABLE:
                HandleMsg<Render::ScissorEnableMsg>(std::move(reader), [this] (bool enable) {
                    re.ScissorEnable(enable);
                });
                break;

            case CG_R_SCISSOR_SET:
                HandleMsg<Render::ScissorSetMsg>(std::move(reader), [this] (int x, int y, int w, int h) {
                    re.ScissorSet(x, y, w, h);
                });
                break;

            case CG_R_CLEARSCENE:
                HandleMsg<Render::ClearSceneMsg>(std::move(reader), [this] {
                    re.ClearScene();
                });
                break;

            case CG_R_ADDREFENTITYTOSCENE:
                HandleMsg<Render::AddRefEntityToSceneMsg>(std::move(reader), [this] (refEntity_t&& entity) {
                    re.AddRefEntityToScene(&entity);
                });
                break;

            case CG_R_ADDPOLYTOSCENE:
                HandleMsg<Render::AddPolyToSceneMsg>(std::move(reader), [this] (int shader, const std::vector<polyVert_t>& verts) {
                    re.AddPolyToScene(shader, verts.size(), verts.data());
                });
                break;

            case CG_R_ADDPOLYSTOSCENE:
                HandleMsg<Render::AddPolysToSceneMsg>(std::move(reader), [this] (int shader, const std::vector<polyVert_t>& verts, int numVerts, int numPolys) {
                    re.AddPolysToScene(shader, numVerts, verts.data(), numPolys);
                });
                break;

            case CG_R_ADDLIGHTTOSCENE:
                HandleMsg<Render::AddLightToSceneMsg>(std::move(reader), [this] (const std::array<float, 3>& point, float radius, float r, float g, float b, int flags) {
                    re.AddLightToScene(point.data(), radius, r, g, b, flags);
                });
                break;

            case CG_R_SETCOLOR:
                HandleMsg<Render::SetColorMsg>(std::move(reader), [this] (const Color::Color& color) {
                    re.SetColor(color);
                });
                break;

            case CG_R_SETCLIPREGION:
                HandleMsg<Render::SetClipRegionMsg>(std::move(reader), [this] (const std::array<float, 4>& region) {
                    re.SetClipRegion(region.data());
                });
                break;

            case CG_R_RESETCLIPREGION:
                HandleMsg<Render::ResetClipRegionMsg>(std::move(reader), [this] {
                    re.SetClipRegion(nullptr);
                });
                break;

            case CG_R_DRAWSTRETCHPIC:
                HandleMsg<Render::DrawStretchPicMsg>(std::move(reader), [this] (float x, float y, float w, float h, float s1, float t1, float s2, float t2, int shader) {
                    re.DrawStretchPic(x, y, w, h, s1, t1, s2, t2, shader);
                });
                break;

            case CG_R_DRAWROTATEDPIC:
                HandleMsg<Render::DrawRotatedPicMsg>(std::move(reader), [this] (float x, float y, float w, float h, float s1, float t1, float s2, float t2, int shader, float angle) {
                    re.DrawRotatedPic(x, y, w, h, s1, t1, s2, t2, shader, angle);
                });
                break;

            case CG_ADDVISTESTTOSCENE:
                HandleMsg<Render::AddVisTestToSceneMsg>(std::move(reader), [this] (int handle, const std::array<float, 3>& pos, float depthAdjust, float area) {
                    re.AddVisTestToScene(handle, pos.data(), depthAdjust, area);
                });
                break;

            case CG_UNREGISTERVISTEST:
                HandleMsg<Render::UnregisterVisTestMsg>(std::move(reader), [this] (int handle) {
                    re.UnregisterVisTest(handle);
                });
                break;

            case CG_SETCOLORGRADING:
                HandleMsg<Render::SetColorGradingMsg>(std::move(reader), [this] (int slot, int shader) {
                    re.SetColorGrading(slot, shader);
                });
                break;

            case CG_R_RENDERSCENE:
                HandleMsg<Render::RenderSceneMsg>(std::move(reader), [this] (refdef_t rd) {
                    re.RenderScene(&rd);
                });
				break;

			case CG_R_ADD2DPOLYSINDEXED:
				HandleMsg<Render::Add2dPolysIndexedMsg>(std::move(reader), [this] (std::vector<polyVert_t> polys, int numPolys, std::vector<int> indicies, int numIndicies, int trans_x, int trans_y, qhandle_t shader) {
					re.Add2dPolysIndexed(polys.data(), numPolys, indicies.data(), numIndicies, trans_x, trans_y, shader);
				});
                break;
			case CG_R_SETMATRIXTRANSFORM:
				HandleMsg<Render::SetMatrixTransformMsg>(std::move(reader), [this] (const std::array<float, 16>& matrix) {
					re.SetMatrixTransform(matrix.data());
				});
				break;
			case CG_R_RESETMATRIXTRANSFORM:
				HandleMsg<Render::ResetMatrixTransformMsg>(std::move(reader), [this] {
					re.ResetMatrixTransform();
				});
				break;

		default:
			Sys::Drop("Bad minor CGame QVM Command Buffer number: %d", minor);
		}

	} else {
		Sys::Drop("Bad major CGame Command Buffer number: %d", major);
	}
}
