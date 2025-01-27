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

/*
 * name:    sv_init.c
 *
 * desc:
 *
*/

#include "server.h"
#include "CryptoChallenge.h"
#include "common/Defs.h"
#include "framework/CvarSystem.h"
#include "framework/Network.h"
#include "qcommon/sys.h"

static Cvar::Cvar<int> cvar_protocol(
	"protocol", "network protocol version number", Cvar::SERVERINFO | Cvar::ROM, PROTOCOL_VERSION);
static Cvar::Cvar<std::string> cvar_pakname(
	"pakname", "pak containing current map", Cvar::SERVERINFO | Cvar::ROM, "");
static Cvar::Cvar<std::string> sv_paks(
	"sv_paks", "currently loaded paks", Cvar::SYSTEMINFO | Cvar::ROM, "");
static Cvar::Cvar<std::string> cvar_contact(
	"contact", "contact information to reach out to the server admin when needed: forum or chat nickname, mail address…",
	Cvar::SERVERINFO, "" );
static Cvar::Cvar<bool> sv_useBaseline(
	"sv_useBaseline", "send entity baseline for non-snapshot delta compression", Cvar::NONE, true);

/*
===============
SV_SetConfigstring
===============
*/
void SV_SetConfigstring( int index, const char *val )
{
	if ( index < 0 || index >= MAX_CONFIGSTRINGS )
	{
		Sys::Drop( "SV_SetConfigstring: bad index %i", index );
	}

	if ( !val )
	{
		val = "";
	}

	// don't bother broadcasting an update if no change
	if ( !strcmp( val, sv.configstrings[ index ] ) )
	{
		return;
	}

	// change the string in sv
	Z_Free( sv.configstrings[ index ] );
	sv.configstrings[ index ] = CopyString( val );
	sv.configstringsmodified[ index ] = true;
}

static void SendConfigStringToClient( int cs, client_t *cl )
{
	char buf[ 1024 ]; // escaped characters, in a quoted context
	// max command size for SV_SendServerCommand is 1022, leave a little overhead for the command
	char *limit = buf + 990;

	char *out = buf;
	bool first = true;
	
	for ( const char *in = sv.configstrings[ cs ]; ; )
	{
		char c = *in++;

		// '$' does not need to be escaped as it is not interpreted in the context of a server command
		if ( c == '\\' || c == '"' )
		{
			*out++ = '\\';
		}

		*out++ = c;

		if ( !*in )
		{
			break;
		}

		if ( out >= limit )
		{
			*out = '\0';
			SV_SendServerCommand( cl, "%s %d \"%s\"", first ? "bcs0" : "bcs1", cs, buf );
			first = false;
			out = buf;
		}
	}

	*out = '\0';
	SV_SendServerCommand( cl, "%s %d \"%s\"", first ? "cs" : "bcs2", cs, buf );
}

void SV_UpdateConfigStrings()
{
	int i, index;
	client_t *client;

	for ( index = 0; index < MAX_CONFIGSTRINGS; index++ )
	{
		if ( !sv.configstringsmodified[ index ] )
		{
			continue;
		}

		sv.configstringsmodified[ index ] = false;

		// send it to all the clients if we aren't
		// spawning a new server
		if ( sv.state == serverState_t::SS_GAME || sv.restarting )
		{
			// send the data to all relevent clients
			for ( i = 0, client = svs.clients; i < sv_maxClients.Get(); i++, client++ )
			{
				if ( client->state < clientState_t::CS_PRIMED )
				{
					continue;
				}

				// do not always send server info to all clients
				if ( index == CS_SERVERINFO && client->gentity && ( client->gentity->r.svFlags & SVF_NOSERVERINFO ) )
				{
					continue;
				}

				SendConfigStringToClient( index, client );
			}
		}
	}
}

/*
===============
SV_GetConfigstring

===============
*/
void SV_GetConfigstring( int index, char *buffer, int bufferSize )
{
	if ( bufferSize < 1 )
	{
		Sys::Drop( "SV_GetConfigstring: bufferSize == %i", bufferSize );
	}

	if ( index < 0 || index >= MAX_CONFIGSTRINGS )
	{
		Sys::Drop( "SV_GetConfigstring: bad index %i", index );
	}

	if ( !sv.configstrings[ index ] )
	{
		buffer[ 0 ] = 0;
		return;
	}

	Q_strncpyz( buffer, sv.configstrings[ index ], bufferSize );
}

/*
===============
SV_SetUserinfo

===============
*/
void SV_SetUserinfo( int index, const char *val )
{
	if ( index < 0 || index >= sv_maxClients.Get() )
	{
		Sys::Drop( "SV_SetUserinfo: bad index %i", index );
	}

	if ( !val )
	{
		val = "";
	}

	Q_strncpyz( svs.clients[ index ].userinfo, val, sizeof( svs.clients[ index ].userinfo ) );
	Q_strncpyz( svs.clients[ index ].name, Info_ValueForKey( val, "name" ), sizeof( svs.clients[ index ].name ) );
}

/*
===============
SV_GetUserinfo

===============
*/
void SV_GetUserinfo( int index, char *buffer, int bufferSize )
{
	if ( bufferSize < 1 )
	{
		Sys::Drop( "SV_GetUserinfo: bufferSize == %i", bufferSize );
	}

	if ( index < 0 || index >= sv_maxClients.Get() )
	{
		Sys::Drop( "SV_GetUserinfo: bad index %i", index );
	}

	Q_strncpyz( buffer, svs.clients[ index ].userinfo, bufferSize );
}

/*
==================
SV_GetPlayerPubkey

==================
*/

void SV_GetPlayerPubkey( int clientNum, char *pubkey, int size )
{
	if ( size < 1 )
	{
		Sys::Drop( "SV_GetPlayerPubkey: size == %i", size );
	}

	if ( clientNum < 0 || clientNum >= sv_maxClients.Get() )
	{
		Sys::Drop( "SV_GetPlayerPubkey: bad clientNum %i", clientNum );
	}

	Q_strncpyz( pubkey, svs.clients[ clientNum ].pubkey, size );
}


/*
================
SV_CreateBaseline

Entity baselines are used to compress non-delta messages
to the clients -- only the fields that differ from the
baseline will be transmitted
================
*/
void SV_CreateBaseline()
{
	Cvar::Latch( sv_useBaseline );

	if ( !sv_useBaseline.Get() )
	{
		// make a baseline with no entities
		return;
	}

	for ( int entnum = MAX_CLIENTS; entnum < sv.num_entities; entnum++ )
	{
		sharedEntity_t *svent = SV_GentityNum( entnum );

		if ( !svent->r.linked )
		{
			continue;
		}

		svent->s.number = entnum;

		//
		// take current state as baseline
		//
		sv.svEntities[ entnum ].baseline = svent->s;
	}
}

/*
===============
SV_Startup

Called when a host starts a map when it wasn't running
one before.  Successive map or map_restart commands will
NOT cause this to be called, unless the game is exited to
the menu system first.
===============
*/
void SV_Startup()
{
	if ( svs.initialized )
	{
		Sys::Error( "SV_Startup: svs.initialized" );
	}

	Cvar::Latch( sv_maxClients );

	// RF, avoid trying to allocate large chunk on a fragmented zone
	svs.clients = ( client_t * ) Z_Calloc( sizeof( client_t ) * sv_maxClients.Get() );

	svs.numSnapshotEntities = sv_maxClients.Get() * PACKET_BACKUP * 64;

	svs.initialized = true;

	Cvar_Set( "sv_running", "1" );
#ifndef BUILD_SERVER
	// For clients, reconfigure to open server ports.
	NET_EnableNetworking( true );
#endif

	// Join the IPv6 multicast group now that a map is running, so clients can scan for us on the local network.
	NET_JoinMulticast6();
}

/*
==================
SV_ChangeMaxClients
==================
*/
void SV_ChangeMaxClients()
{
	int oldMaxClients = sv_maxClients.Get();
	Cvar::Latch( sv_maxClients );
	int desiredMaxClients = sv_maxClients.Get();

	// get the highest client number in use
	int count = 0;

	for ( int i = 0; i < oldMaxClients; i++ )
	{
		if ( svs.clients[ i ].state >= clientState_t::CS_CONNECTED )
		{
			count = i;
		}
	}

	count++;

	// never go below the highest client number in use
	int newMaxClients = std::max( count, desiredMaxClients );

	if ( newMaxClients != desiredMaxClients )
	{
		// keep trying to set the user-requested value until the high-numbered clients leave
		sv_maxClients.Set( newMaxClients );
		Cvar::Latch( sv_maxClients );
		sv_maxClients.Set( desiredMaxClients );
	}

	ASSERT_EQ( newMaxClients, sv_maxClients.Get() );

	// if still the same
	if ( newMaxClients == oldMaxClients )
	{
		return;
	}

	client_t* oldClients = svs.clients;

	// allocate new clients
	svs.clients = ( client_t * ) Z_Calloc( newMaxClients * sizeof( client_t ) );

	// copy the clients over
	for ( int i = 0; i < count; i++ )
	{
		if ( oldClients[ i ].state >= clientState_t::CS_CONNECTED )
		{
			svs.clients[ i ] = oldClients[ i ];
		}
	}

	// free the old clients
	Z_Free( oldClients );

	svs.numSnapshotEntities = newMaxClients * PACKET_BACKUP * 64;
}

/*
================
SV_ClearServer
================
*/
void SV_ClearServer()
{
	int i;

	for ( i = 0; i < MAX_CONFIGSTRINGS; i++ )
	{
		if ( sv.configstrings[ i ] )
		{
			Z_Free( sv.configstrings[ i ] );
		}
	}

	ResetStruct( sv );
}

/*
================
SV_SpawnServer

Change the server to a new map, taking all connected
clients along with it.
This is NOT called for map_restart, UNLESS the number of client slots changed
================
*/
void SV_SpawnServer(std::string pakname, std::string mapname)
{
	int        i;
	bool   isBot;

	// shut down the existing game if it is running
	SV_ShutdownGameProgs();

	PrintBanner( "Server Initialization" )

	if ( !SV_Private(ServerPrivate::NoAdvertise)
		&& sv_networkScope.Get() >= 2
		&& cvar_contact.Get().empty() )
	{
		Log::Warn( "The contact information isn't set, it is requested for public servers." );
	}

	Log::Notice( "Map: %s", mapname );

	// if not running a dedicated server CL_MapLoading will connect the client to the server
	// also print some status stuff
	CL_MapLoading();

	// clear collision map data
	CM_ClearMap();

	// wipe the entire per-level structure
	SV_ClearServer();

	// allocate empty config strings
	for ( i = 0; i < MAX_CONFIGSTRINGS; i++ )
	{
		sv.configstrings[ i ] = CopyString( "" );
		sv.configstringsmodified[ i ] = false;
	}

	// init client structures and svs.numSnapshotEntities
	if ( !Cvar_VariableValue( "sv_running" ) )
	{
		SV_Startup();
	}
	else
	{
		// check for maxclients change
		SV_ChangeMaxClients();
	}

	// allocate the snapshot entities
	svs.snapshotEntities.reset(new entityState_t[svs.numSnapshotEntities]);
	svs.nextSnapshotEntities = 0;

	// toggle the server bit so clients can detect that a
	// server has changed
	svs.snapFlagServerBit ^= SNAPFLAG_SERVERCOUNT;

	// Seed the RNG
	srand( Sys::Milliseconds() );

	FS::PakPath::ClearPaks();
	FS_LoadBasePak();
	if (!FS_LoadPak(pakname))
		Sys::Drop("Could not load map pak '%s'\n", pakname);

	CM_LoadMap(mapname);

	// set serverinfo visible name
	Cvar_Set( "mapname", mapname.c_str() );
	Cvar::SetValueForce( cvar_pakname.Name(), pakname );

	// serverid should be different each time
	sv.serverId = com_frameTime;
	sv.restartedServerId = sv.serverId;
	Cvar_Set( "sv_serverid", va( "%i", sv.serverId ) );

	// media configstring setting should be done during
	// the loading stage, so connected clients don't have
	// to load during actual gameplay
	sv.state = serverState_t::SS_LOADING;

	// load and spawn all other entities
	SV_InitGameProgs();

	// run a few frames to allow everything to settle
	for ( i = 0; i < GAME_INIT_FRAMES; i++ )
	{
		gvm.GameRunFrame( sv.time );
		svs.time += FRAMETIME;
		sv.time += FRAMETIME;
	}

	// create a baseline for more efficient communications
	SV_CreateBaseline();

	for ( i = 0; i < sv_maxClients.Get(); i++ )
	{
		// send the new gamestate to all connected clients
		if ( svs.clients[ i ].state >= clientState_t::CS_CONNECTED )
		{
			bool denied;
			char reason[ MAX_STRING_CHARS ];

			isBot = SV_IsBot(&svs.clients[i]);

			// connect the client again
			denied = gvm.GameClientConnect( reason, sizeof( reason ), i, false, isBot );   // firstTime = false

			if ( denied )
			{
				// this generally shouldn't happen, because the client
				// was connected before the level change
				SV_DropClient( &svs.clients[ i ], reason );
			}
			else
			{
				if ( !isBot )
				{
					// when we get the next packet from a connected client,
					// the new gamestate will be sent
					svs.clients[ i ].state = clientState_t::CS_CONNECTED;
				}
				else
				{
					client_t       *client;
					sharedEntity_t *ent;

					client = &svs.clients[ i ];
					client->state = clientState_t::CS_ACTIVE;
					ent = SV_GentityNum( i );
					ent->s.number = i;
					client->gentity = ent;

					client->deltaMessage = -1;
					client->nextSnapshotTime = svs.time; // generate a snapshot immediately

					gvm.GameClientBegin( i );
				}
			}
		}
	}

	// run another frame to allow things to look at all the players
	gvm.GameRunFrame( sv.time );
	svs.time += FRAMETIME;
	sv.time += FRAMETIME;

	// the server sends these to the clients so they can figure
	// out which dpk/pk3s should be auto-downloaded

	Cvar::SetValueForce( sv_paks.Name(), FS_LoadedPaks() );

	// save systeminfo and serverinfo strings
	cvar_modifiedFlags &= ~CVAR_SYSTEMINFO;
	SV_SetConfigstring( CS_SYSTEMINFO, Cvar_InfoString( CVAR_SYSTEMINFO, true ) );

	SV_SetConfigstring( CS_SERVERINFO, Cvar_InfoString( CVAR_SERVERINFO, false ) );
	cvar_modifiedFlags &= ~CVAR_SERVERINFO;

	// any media configstring setting now should issue a warning
	// and any configstring changes should be reliably transmitted
	// to all clients
	sv.state = serverState_t::SS_GAME;

	// send a heartbeat now so the master will get up to date info
	SV_Heartbeat_f();

	SV_UpdateConfigStrings();

	SV_AddOperatorCommands();

	Log::Notice( "-----------------------------------" );
}

/*
===============
SV_Init

Only called at main exe startup, not for each game
===============
*/
void SV_Init()
{
	SV_AddOperatorCommands();

	// serverinfo vars
	sv_mapname = Cvar_Get( "mapname", "nomap", CVAR_SERVERINFO | CVAR_ROM );
	sv_hostname = Cvar_Get( "sv_hostname", UNNAMED_SERVER, CVAR_SERVERINFO  );
	Cvar::Latch( sv_maxClients );
	sv_maxRate = Cvar_Get( "sv_maxRate", "0",  CVAR_SERVERINFO );
	sv_floodProtect = Cvar_Get( "sv_floodProtect", "0",  CVAR_SERVERINFO );
	Cvar::SetValue( "layout", "" ); // TODO: declare in sgame
	Cvar::AddFlags( "layout", Cvar::SERVERINFO );

	sv_statsURL = Cvar_Get( "sv_statsURL", "", CVAR_SERVERINFO  );

	// systeminfo
	sv_serverid = Cvar_Get( "sv_serverid", "0", CVAR_SYSTEMINFO | CVAR_ROM );

	// server vars
	sv_privatePassword = Cvar_Get( "sv_privatePassword", "", CVAR_TEMP );
	sv_fps = Cvar_Get( "sv_fps", "40", CVAR_TEMP );
	sv_timeout = Cvar_Get( "sv_timeout", "240", CVAR_TEMP );
	sv_zombietime = Cvar_Get( "sv_zombietime", "2", CVAR_TEMP );

	sv_allowDownload = Cvar_Get( "sv_allowDownload", "1", 0 );
	sv_reconnectlimit = Cvar_Get( "sv_reconnectlimit", "3", 0 );
	sv_padPackets = Cvar_Get( "sv_padPackets", "0", 0 );
	sv_killserver = Cvar_Get( "sv_killserver", "0", 0 );

	sv_lanForceRate = Cvar_Get( "sv_lanForceRate", "1", 0 );

	sv_showAverageBPS = Cvar_Get( "sv_showAverageBPS", "0", 0 );  // NERVE - SMF - net debugging

	// the download netcode tops at 18/20 kb/s, no need to make you think you can go above
	sv_dl_maxRate = Cvar_Get( "sv_dl_maxRate", "42000", 0 );

	// fretn - note: redirecting of clients to other servers relies on this,
	// ET://someserver.com
	sv_fullmsg = Cvar_Get( "sv_fullmsg", "Server is full.", 0 );

	svs.serverLoad = -1;
}

/*
==================
SV_FinalCommand

Used by SV_Shutdown to send a final message to all
connected clients before the server goes down.  The messages are sent immediately,
not just stuck on the outgoing message list, because the server is going
to totally exit after returning from this function.
==================
*/
void SV_FinalCommand( char *cmd, bool disconnect )
{
	int      i, j;
	client_t *cl;

	// send it twice, ignoring rate
	for ( j = 0; j < 2; j++ )
	{
		for ( i = 0, cl = svs.clients; i < sv_maxClients.Get(); i++, cl++ )
		{
			if ( cl->state >= clientState_t::CS_CONNECTED )
			{
				// don't send a disconnect to a local client
				if ( cl->netchan.remoteAddress.type != netadrtype_t::NA_LOOPBACK )
				{
					//% SV_SendServerCommand( cl, "print \"%s\"", message );
					SV_SendServerCommand( cl, "%s", cmd );

					// ydnar: added this so map changes can use this functionality
					if ( disconnect )
					{
						SV_SendServerCommand( cl, "disconnect" );
					}
				}

				if (sv.gameClients != nullptr)
				{
					// force a snapshot to be sent
					cl->nextSnapshotTime = -1;
					SV_SendClientSnapshot( cl );
				}
			}
		}
	}
}

// Used instead of SV_Shutdown when Daemon is exiting
void SV_QuickShutdown( const char *finalmsg )
{
	if ( !com_sv_running || !com_sv_running->integer )
	{
		return;
	}

	PrintBanner( "Server Shutdown" )

	if ( svs.clients )
	{
		SV_FinalCommand( va( "print %s", Cmd_QuoteString( finalmsg ) ), true );
	}

	SV_ShutdownGameProgs();
	SV_MasterShutdown();
}

/*
================
SV_Shutdown

Called to shut down the sgame VM, or to clean up after the VM shut down on its own
================
*/
void SV_Shutdown( const char *finalmsg )
{
	if ( !com_sv_running || !com_sv_running->integer )
	{
		return;
	}

	SV_QuickShutdown( finalmsg );

	NET_LeaveMulticast6();

	SV_RemoveOperatorCommands();

	// free current level
	SV_ClearServer();

	// clear collision map data
	CM_ClearMap();

	// free server static data
	if ( svs.clients )
	{
		int index;

		for ( index = 0; index < sv_maxClients.Get(); index++ )
		{
			SV_FreeClient( &svs.clients[ index ] );
		}

		Z_Free( svs.clients );
	}

	ResetStruct( svs );

	svs.serverLoad = -1;
	ChallengeManager::Clear();

	Cvar_Set( "sv_running", "0" );
#ifndef BUILD_SERVER
	// For clients, reconfigure to close server ports.
	NET_EnableNetworking( false );
#endif

	SV_NET_Config(); // clear master server DNS queries
	Net::ShutDownDNS();

	Log::Notice( "---------------------------" );
}
