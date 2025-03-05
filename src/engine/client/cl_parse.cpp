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

// cl_parse.c  -- parse a message received from the server

#include "client.h"

static const char *const svc_strings[ 256 ] =
{
	"svc_bad",
	"svc_nop",
	"svc_gamestate",
	"svc_configstring",
	"svc_baseline",
	"svc_serverCommand",
	"svc_download",
	"svc_snapshot",
	"svc_EOF"
};

static void SHOWNET( msg_t *msg, const char *s )
{
	if ( cl_shownet->integer >= 2 )
	{
		Log::Notice( "%3i:%s", msg->readcount - 1, s );
	}
}

/*
=========================================================================

MESSAGE PARSING

=========================================================================
*/

// TODO(kangz) if we can make sure that the baseline entities have the correct entity
// number, then we could grab the entity number from old directly, simplifying code a bit.
void CL_DeltaEntity( msg_t *msg, clSnapshot_t *snapshot, int entityNum, const entityState_t &oldEntity)
{
    entityState_t entity;
    MSG_ReadDeltaEntity(msg, &oldEntity, &entity, entityNum);

    if (entity.number != MAX_GENTITIES - 1) {
        snapshot->entities.push_back(entity);
    }
}

/*
==================
CL_ParsePacketEntities

==================
*/
void CL_ParsePacketEntities( msg_t *msg, const clSnapshot_t *oldSnapshot, clSnapshot_t *newSnapshot )
{
    // The entity packet contains the delta between the two snapshots, with data only
    // for entities that were created, changed or removed. Entities entries are in
    // order of increasing entity number, as are entities in a snapshot. Using this we
    // have an efficient algorithm to create the new snapshot, that goes over the old
    // snapshot once from the beginning to the end.

    // If we don't have an old snapshot or it is empty, we'll recreate all entities
    // from the baseline entities as setting oldEntityNum to MAX_GENTITIES will force
    // us to only do step (3) below.
    unsigned int oldEntityNum = MAX_GENTITIES;
    if (oldSnapshot && oldSnapshot->entities.size() > 0){
        oldEntityNum = oldSnapshot->entities[0].number;
    }

    // Likewise when we don't have an old snapshot, oldEntities just has to be an empty
    // vector so that we skip step (4)
    std::vector<entityState_t> dummyEntities;
    auto& oldEntities = oldSnapshot? oldSnapshot->entities : dummyEntities;
    auto& newEntities = newSnapshot->entities;

    unsigned int numEntities = MSG_ReadShort(msg);
    newEntities.reserve(numEntities);

	unsigned oldIndex = 0;

    while (true) {
        unsigned int newEntityNum = MSG_ReadBits(msg, GENTITYNUM_BITS);

        if (msg->readcount > msg->cursize) {
            Sys::Drop("CL_ParsePacketEntities: Unexpected end of message");
        }

        if (newEntityNum == MAX_GENTITIES - 1) {
            break;
        }

        // (1) all entities that weren't specified between the previous newEntityNum and
        // the current one are unchanged and just copied over.
        while (oldEntityNum < newEntityNum) {
            newEntities.push_back(oldEntities[oldIndex]);

            oldIndex ++;
            if (oldIndex >= oldEntities.size()) {
                oldEntityNum = MAX_GENTITIES;
            } else {
                oldEntityNum = oldEntities[oldIndex].number;
            }
        }

        // (2) there is an entry for an entity in the old snapshot, apply the delta
        if (oldEntityNum == newEntityNum) {
            CL_DeltaEntity(msg, newSnapshot, newEntityNum, oldEntities[oldIndex]);

            oldIndex ++;
            if (oldIndex >= oldEntities.size()) {
                oldEntityNum = MAX_GENTITIES;
            } else {
                oldEntityNum = oldEntities[oldIndex].number;
            }
        } else {
            // (3) the entry isn't in the old snapshot, so the entity will be specified
            // from the baseline
            ASSERT_GT(oldEntityNum, newEntityNum);

            CL_DeltaEntity(msg, newSnapshot, newEntityNum, cl.entityBaselines[newEntityNum]);
        }
    }

    // (4) All remaining entities in the oldSnapshot are unchanged and copied over
    while (oldIndex < oldEntities.size()) {
        newEntities.push_back(oldEntities[oldIndex]);
        oldIndex ++;
    }

    ASSERT_EQ(numEntities, newEntities.size());
}

/*
================
CL_ParseSnapshot

If the snapshot is parsed properly, it will be copied to
cl.snap and saved in cl.snapshots[].  If the snapshot is invalid
for any reason, no changes to the state will be made at all.
================
*/
void CL_ParseSnapshot( msg_t *msg )
{
	int          len;
	clSnapshot_t *old;
	int          deltaNum;
	int          oldMessageNum;
	int          i, packetNum;

	// get the reliable sequence acknowledge number
	// NOTE: now sent with all server to client messages
	//clc.reliableAcknowledge = MSG_ReadLong( msg );

	// read in the new snapshot to a temporary buffer
	// we will only copy to cl.snap if it is valid
	clSnapshot_t newSnap{};

	// we will have read any new server commands in this
	// message before we got to svc_snapshot
	newSnap.serverCommandNum = clc.serverCommandSequence;

	newSnap.serverTime = MSG_ReadLong( msg );

	newSnap.messageNum = clc.serverMessageSequence;

	deltaNum = MSG_ReadByte( msg );

	if ( !deltaNum )
	{
		newSnap.deltaNum = -1;
	}
	else
	{
		newSnap.deltaNum = newSnap.messageNum - deltaNum;
	}

	newSnap.snapFlags = MSG_ReadByte( msg );

	// If the frame is delta compressed from data that we
	// no longer have available, we must suck up the rest of
	// the frame, but not use it, then ask for a non-compressed
	// message
	if ( newSnap.deltaNum <= 0 )
	{
		newSnap.valid = true; // uncompressed frame
		old = nullptr;

		if ( clc.demorecording )
		{
			clc.demowaiting = false; // we can start recording now
		}
		else
		{
			if ( cl_autorecord->integer )
			{
				CL_Record("");
			}
		}
	}
	else
	{
		old = &cl.snapshots[ newSnap.deltaNum & PACKET_MASK ];

		if ( !old->valid )
		{
			// should never happen
			Log::Notice( "Delta from invalid frame (not supposed to happen!)." );
		}
		else if ( old->messageNum != newSnap.deltaNum )
		{
			// The frame that the server did the delta from
			// is too old, so we can't reconstruct it properly.
			Log::Debug( "Delta frame too old." );
		}
		else
		{
			newSnap.valid = true; // valid delta parse
		}
	}

	// read areamask
	len = MSG_ReadByte( msg );

	if ( len > (int) sizeof( newSnap.areamask ) )
	{
		Sys::Drop( "CL_ParseSnapshot: Invalid size %d for areamask.", len );
	}

	MSG_ReadData( msg, &newSnap.areamask, len );

	// read playerinfo
	SHOWNET( msg, "playerstate" );

	if ( old )
	{
		MSG_ReadDeltaPlayerstate( msg, &old->ps, &newSnap.ps );
	}
	else
	{
		MSG_ReadDeltaPlayerstate( msg, nullptr, &newSnap.ps );
	}

	// read packet entities
	SHOWNET( msg, "packet entities" );
	CL_ParsePacketEntities( msg, old, &newSnap );

	// if not valid, dump the entire thing now that it has
	// been properly read
	if ( !newSnap.valid )
	{
		return;
	}

	// clear the valid flags of any snapshots between the last
	// received and this one, so if there was a dropped packet
	// it won't look like something valid to delta from next
	// time we wrap around in the buffer
	oldMessageNum = cl.snap.messageNum + 1;

	if ( newSnap.messageNum - oldMessageNum >= PACKET_BACKUP )
	{
		oldMessageNum = newSnap.messageNum - ( PACKET_BACKUP - 1 );
	}

	for ( ; oldMessageNum < newSnap.messageNum; oldMessageNum++ )
	{
		cl.snapshots[ oldMessageNum & PACKET_MASK ].valid = false;
	}

	// copy to the current good spot
	cl.snap = newSnap;
	cl.snap.ping = 999;

	// calculate ping time
	for ( i = 0; i < PACKET_BACKUP; i++ )
	{
		packetNum = ( clc.netchan.outgoingSequence - 1 - i ) & PACKET_MASK;

		if ( cl.snap.ps.commandTime >= cl.outPackets[ packetNum ].p_serverTime )
		{
			cl.snap.ping = cls.realtime - cl.outPackets[ packetNum ].p_realtime;
			break;
		}
	}

	// save the frame off in the backup array for later delta comparisons
	cl.snapshots[ cl.snap.messageNum & PACKET_MASK ] = cl.snap;

	if ( cl_shownet->integer == 3 )
	{
		Log::Notice( "   snapshot:%i  delta:%i  ping:%i", cl.snap.messageNum, cl.snap.deltaNum, cl.snap.ping );
	}

	cl.newSnapshots = true;
}

//=====================================================================

/*
==================
CL_SystemInfoChanged

The systeminfo configstring has been changed, so parse
new information out of it.  This will happen at every
gamestate, and possibly during gameplay.
==================
*/
void CL_SystemInfoChanged()
{
	const char *systemInfo;
	const char *s;
	char       key[ BIG_INFO_KEY ];
	char       value[ BIG_INFO_VALUE ];

	systemInfo = cl.gameState[ CS_SYSTEMINFO ].c_str();
	// NOTE TTimo:
	// when the serverId changes, any further messages we send to the server will use this new serverId
	// show_bug.cgi?id=475
	// in some cases, outdated cp commands might get sent with this news serverId
	cl.serverId = atoi( Info_ValueForKey( systemInfo, "sv_serverid" ) );

	// load paks sent by the server, but not if we are running a local server
	if (!com_sv_running->integer) {
		FS::PakPath::ClearPaks();
		if (!FS_LoadServerPaks(Info_ValueForKey(systemInfo, "sv_paks"), clc.demoplaying)) {
			if (!cl_allowDownload->integer) {
				Sys::Drop("Client is missing paks but downloads are disabled");
			} else if (clc.demoplaying) {
				Sys::Drop("Client is missing paks needed by the demo");
			}
		}
	}

	// don't set any vars when playing a demo
	if ( clc.demoplaying )
	{
		return;
	}

	// scan through all the variables in the systeminfo and locally set cvars to match
	s = systemInfo;

	while ( s )
	{
		Info_NextPair( &s, key, value );

		if ( !key[ 0 ] )
		{
			break;
		}

		Cvar_Set( key, value );
	}
}

/*
==================
CL_ParseGamestate

The server normally sends this for a new map or when a download operation completes.
==================
*/
void CL_ParseGamestate( msg_t *msg )
{
	int           i;
	entityState_t *es;
	int           newnum;
	int           cmd;

	Con_Close();

	clc.connectPacketCount = 0;

	if ( !cl.reading ) {
		// wipe local client state
		CL_ClearState();

		// a gamestate always marks a server command sequence
		clc.serverCommandSequence = MSG_ReadLong( msg );
	}

	// parse all the configstrings and baselines
	while (true)
	{
		cmd = MSG_ReadByte( msg );

		if ( cmd == svc_EOF )
		{
			break;
		}

		if ( cmd == svc_configstring )
		{
			i = MSG_ReadShort( msg );

			if ( i < 0 || i >= MAX_CONFIGSTRINGS )
			{
				Sys::Drop( "configstring > MAX_CONFIGSTRINGS" );
			}

			const char* str = MSG_ReadBigString( msg );
			cl.gameState[i] = str;
		}
		else if ( cmd == svc_baseline )
		{
			newnum = MSG_ReadBits( msg, GENTITYNUM_BITS );

			if ( newnum < 0 || newnum >= MAX_GENTITIES )
			{
				Sys::Drop( "Baseline number out of range: %i", newnum );
			}

			entityState_t nullstate{};
			es = &cl.entityBaselines[ newnum ];
			MSG_ReadDeltaEntity( msg, &nullstate, es, newnum );

			cl.reading = false;
		}
		else if ( cmd == svc_gamestatePartial )
		{
			cl.reading = true;
			break;
		}
		else
		{
			Sys::Drop( "CL_ParseGamestate: bad command byte" );
		}
	}

	if ( !cl.reading ) {
		clc.clientNum = MSG_ReadLong( msg );

		// parse serverId and other cvars
		CL_SystemInfoChanged();

		// This used to call CL_StartHunkUsers, but now we enter the download state before loading the
		// cgame
		CL_InitDownloads();
	}
}

//=====================================================================

/*
=====================
CL_ParseCommandString

Command strings are just saved off until cgame asks for them
when it transitions a snapshot
=====================
*/
void CL_ParseCommandString( msg_t *msg )
{
	char *s;
	int  seq;
	int  index;

	seq = MSG_ReadLong( msg );
	s = MSG_ReadString( msg );

	// see if we have already executed stored it off
	if ( clc.serverCommandSequence >= seq )
	{
		return;
	}

	clc.serverCommandSequence = seq;

	index = seq & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( clc.serverCommands[ index ], s, sizeof( clc.serverCommands[ index ] ) );
}

/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage( msg_t *msg )
{
	int cmd;
//	msg_t           msgback;

//	msgback = *msg;

	if ( cl_shownet->integer == 1 )
	{
		Log::Notice("%i ", msg->cursize );
	}
	else if ( cl_shownet->integer >= 2 )
	{
		Log::Notice( "------------------" );
	}

	MSG_Bitstream( msg );

	// get the reliable sequence acknowledge number
	clc.reliableAcknowledge = MSG_ReadLong( msg );

	//
	if ( clc.reliableAcknowledge < clc.reliableSequence - MAX_RELIABLE_COMMANDS )
	{
		clc.reliableAcknowledge = clc.reliableSequence;
	}

	//
	// parse the message
	//
	while (true)
	{
		if ( msg->readcount > msg->cursize )
		{
			Sys::Drop( "CL_ParseServerMessage: read past end of server message" );
		}

		cmd = MSG_ReadByte( msg );

		if ( cmd < 0 || cmd == svc_EOF )
		{
			SHOWNET( msg, "END OF MESSAGE" );
			break;
		}

		if ( cl_shownet->integer >= 2 )
		{
			if ( !svc_strings[ cmd ] )
			{
				Log::Notice( "%3i:BAD CMD %i", msg->readcount - 1, cmd );
			}
			else
			{
				SHOWNET( msg, svc_strings[ cmd ] );
			}
		}

		// other commands
		switch ( cmd )
		{
			default:
				Sys::Drop( "CL_ParseServerMessage: Illegible server message %d", cmd );

			case svc_nop:
				break;

			case svc_serverCommand:
				CL_ParseCommandString( msg );
				break;

			case svc_gamestate:
				CL_ParseGamestate( msg );
				break;

			case svc_snapshot:
				CL_ParseSnapshot( msg );
				break;

			case svc_download:
				CL_ParseDownload( msg );
				break;
		}
	}
}
