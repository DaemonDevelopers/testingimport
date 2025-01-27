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

#include "common/Common.h"

#include "server.h"
#include "CryptoChallenge.h"
#include "framework/Rcon.h"

#include "common/Defs.h"
#include "framework/CommandSystem.h"
#include "framework/CvarSystem.h"
#include "framework/Network.h"
#include "qcommon/sys.h"

// These two structs have the same lifetime... both are cleared when the sgame exits
serverStatic_t svs;
server_t       sv;

GameVM         gvm; // game virtual machine

// Controls the gamelogic simulation time slice size. The game time always jumps in increments
// of 1000/sv_fps ms. Multiple or (for clients hosting games) no gamelogic frames may be run
// per server frame. Since timescale affects the game clock's rate, if you have sv_fps 40 and
// timescale 5, the number of gamelogic frames per wall second will be 200.
// For the dedicated server this also controls the engine frame rate. The engine framerate
// is based on real time, disregarding timescale.
cvar_t         *sv_fps;

cvar_t         *sv_timeout; // seconds without any message
cvar_t         *sv_zombietime; // seconds to sink messages after disconnect
cvar_t         *sv_privatePassword; // password for the privateClient slots
cvar_t         *sv_allowDownload;
Cvar::Range<Cvar::Cvar<int>> sv_maxClients("sv_maxclients",
	"max number of players on the server", Cvar::SERVERINFO, 20, 1, MAX_CLIENTS);

Cvar::Range<Cvar::Cvar<int>> sv_privateClients("sv_privateClients",
    "number of password-protected client slots", Cvar::SERVERINFO, 0, 0, MAX_CLIENTS);
cvar_t         *sv_hostname;
cvar_t         *sv_statsURL;
cvar_t         *sv_reconnectlimit; // minimum seconds between connect messages
cvar_t         *sv_padPackets; // add nop bytes to messages
cvar_t         *sv_killserver; // menu system can set to 1 to shut server down
cvar_t         *sv_mapname;
cvar_t         *sv_serverid;
cvar_t         *sv_maxRate;

cvar_t         *sv_floodProtect;
cvar_t         *sv_lanForceRate; // TTimo - dedicated 1 (LAN) server forces local client rates to 99999 (bug #491)

cvar_t         *sv_dl_maxRate;

cvar_t *sv_showAverageBPS; // NERVE - SMF - net debugging

// fretn
cvar_t *sv_fullmsg;

Cvar::Range<Cvar::Cvar<int>> sv_networkScope(
	"sv_networkScope",
	"allowed source networks for incoming packets: 0 = loopback only, 1 = LAN, 2 = Internet",
	Cvar::NONE,
#ifdef BUILD_SERVER
	2,
#else
	1,
#endif
	0, 2);

// Network stuff other than communication with connected clients
Log::Logger netLog("server.net", "", Log::Level::NOTICE);

namespace Cvar {
template<>
std::string GetCvarTypeName<ServerPrivate>()
{
	return "server private";
}

} // namespace Cvar

bool ParseCvarValue(Str::StringRef value, ServerPrivate& result)
{
	int intermediate = 0;
	if ( Str::ParseInt(intermediate, value) &&
		intermediate >= int(ServerPrivate::Public) &&
		intermediate <= int(ServerPrivate::NoStatus) )
	{
		result = ServerPrivate(intermediate);
		return true;
	}
	return false;
}

std::string SerializeCvarValue(ServerPrivate value)
{
	return std::to_string(int(value));
}

Cvar::Cvar<ServerPrivate> isPrivate(
	"server.private",
	"Controls how much the server advertises: "
	"0 - Advertise everything, "
	"1 - Don't advertise but reply to status queries, "
	"2 - Only accept direct connections. ",
	Cvar::NONE,
#if BUILD_GRAPHICAL_CLIENT || BUILD_TTY_CLIENT
	ServerPrivate::NoAdvertise
#elif BUILD_SERVER
	ServerPrivate::Public
#else
	#error
#endif
);

bool SV_Private(ServerPrivate level)
{
	return isPrivate.Get() >= level;
}

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/

/*
======================
SV_AddServerCommand

The given command will be transmitted to the client, and is guaranteed to
not have future snapshot_t executed before it is executed
======================
*/
void SV_AddServerCommand( client_t *client, const char *cmd )
{
	int index, i;

	client->reliableSequence++;

	// if we would be losing an old command that hasn't been acknowledged,
	// we must drop the connection
	// we check == instead of >= so a broadcast print added by SV_DropClient()
	// doesn't cause a recursive drop client
	if ( client->reliableSequence - client->reliableAcknowledge == MAX_RELIABLE_COMMANDS + 1 )
	{
		Log::Debug("===== pending server commands =====");

		for ( i = client->reliableAcknowledge + 1; i <= client->reliableSequence; i++ )
		{
			Log::Debug( "cmd %5d: %s", i, client->reliableCommands[ i & ( MAX_RELIABLE_COMMANDS - 1 ) ] );
		}

		Log::Debug( "cmd %5d: %s", i, cmd );
		SV_DropClient( client, "Server command overflow" );
		return;
	}

	index = client->reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( client->reliableCommands[ index ], cmd, sizeof( client->reliableCommands[ index ] ) );
}

/*
=================
SV_SendServerCommand

Sends a reliable command string to be interpreted by
the client game module: "cp", "print", "chat", etc
A nullptr client will broadcast to all clients
=================
*/
void PRINTF_LIKE(2) SV_SendServerCommand( client_t *cl, const char *fmt, ... )
{
	va_list  argptr;
	byte     message[ MAX_MSGLEN ];
	client_t *client;
	int      j;

	va_start( argptr, fmt );
	Q_vsnprintf( ( char * ) message, sizeof( message ), fmt, argptr );
	va_end( argptr );

	// do not forward server command messages that would be too big to clients
	// ( q3infoboom / q3msgboom stuff )
	if ( strlen( ( char * ) message ) > 1022 )
	{
		Log::Warn( "^1Not sending reliable command because it is too long! [%.50s...]", message );
		return;
	}

	if ( cl != nullptr )
	{
		SV_AddServerCommand( cl, ( char * ) message );
		return;
	}

	if ( Com_IsDedicatedServer() )
	{
		if ( !strncmp( ( char * ) message, "print_tr_p ", 11 ) )
		{
			SV_PrintTranslatedText( ( const char * ) message, true, true );
		}
		else if ( !strncmp( ( char * ) message, "print_tr ", 9 ) )
		{
			SV_PrintTranslatedText( ( const char * ) message, true, false );
		}

		// hack to echo broadcast prints to console
		else if ( !strncmp( ( char * ) message, "print ", 6 ) )
		{
			// Don't use "Broadcast: %s" as this format string is too unspecific for log suppression
			Log::Notice( std::string("Broadcast: ") + Cmd_UnquoteString( ( const char * ) message + 6 ) );
		}
	}

	// send the data to all relevent clients
	for ( j = 0, client = svs.clients; j < sv_maxClients.Get(); j++, client++ )
	{
		if ( client->state < clientState_t::CS_PRIMED )
		{
			continue;
		}

		// Ridah, don't need to send messages to AI
		if ( SV_IsBot(client) )
		{
			continue;
		}

		// done.
		SV_AddServerCommand( client, ( char * ) message );
	}
}

/*
==============================================================================

MASTER SERVER FUNCTIONS

==============================================================================
*/
namespace {

struct MasterServer;
extern MasterServer masterServers[MAX_MASTER_SERVERS];

struct MasterServer
{
	Net::DNSQueryHandle query = 0;
	int nextHeartbeatTime = 0;
	std::string challenge;
	netadrtype_t challenge_address_type;
	Cvar::Modified<Cvar::Cvar<std::string>> addressCvar;

	MasterServer(const char* cvarName, const char* defaultHostname)
		: addressCvar(cvarName, "address of a master server", Cvar::NONE, defaultHostname )
	{}
};

MasterServer masterServers[MAX_MASTER_SERVERS] = {
	{"sv_master1", MASTER1_SERVER_NAME},
	{"sv_master2", MASTER2_SERVER_NAME},
	{"sv_master3", MASTER3_SERVER_NAME},
	{"sv_master4", MASTER4_SERVER_NAME},
	{"sv_master5", MASTER5_SERVER_NAME},
};
} // namespace

// Enqueue queries for the DNS resolution thread
static void SV_ResolveMasterServers( int netMask )
{
	static int oldNetMask = 0;

	for ( MasterServer& master : masterServers )
	{
		Util::optional<std::string> address = master.addressCvar.GetModifiedValue();
		if ( !address )
		{
			if ( netMask == oldNetMask )
			{
				continue; // Continue if nothing changed
			}
			address = master.addressCvar.Get();
		}

		// Something changed, reset the heartbeat timer
		master.nextHeartbeatTime = 0;

		if ( netMask != 0 && !address->empty() )
		{
			if ( master.query == 0 )
			{
				master.query = Net::AllocDNSQuery();
			}
			Net::SetDNSQuery( master.query, *address, netMask );
		}
		else
		{
			if ( master.query != 0 )
			{
				Net::SetDNSQuery( master.query, "", 0 );
			}
		}
	}

	oldNetMask = netMask;
}

/*
================
SV_NET_Config

Network connections being reconfigured. May need to redo some lookups.
================
*/
void SV_NET_Config()
{
	SV_ResolveMasterServers( 0 );
}

/*
================
SV_MasterHeartbeat

Send a message to the masters every few minutes to
let it know we are alive, and log information.
We will also have a heartbeat sent when a server
changes from empty to non-empty, and full to non-full,
but not on every player enter or exit.
================
*/
static const int HEARTBEAT_MSEC = (300 * 1000);
#define HEARTBEAT_GAME PRODUCT_NAME
#define HEARTBEAT_DEAD PRODUCT_NAME "-dead"

void SV_MasterHeartbeat( const char *hbname )
{
	int netenabled = Cvar_VariableIntegerValue( "net_enabled" );

	if ( SV_Private(ServerPrivate::NoAdvertise) )
	{
		SV_ResolveMasterServers( 0 );
		return; // only dedicated servers send heartbeats
	}
	else if ( sv_networkScope.Get() < 2 )
	{
		if ( !svs.warnedNetworkScopeNotAdvertisable )
		{
			netLog.Warn( "Not sending master heartbeat because sv_networkScope is local" );
			svs.warnedNetworkScopeNotAdvertisable = true;
		}

		SV_ResolveMasterServers( 0 );
		return;
	}

	SV_ResolveMasterServers( netenabled );

	// send to group masters
	for ( MasterServer& master : masterServers )
	{
		if (svs.time < master.nextHeartbeatTime) {
			continue;
		}

		Net::DNSResult adrs = Net::GetAddresses(master.query);
		if (adrs.ipv4.type == netadrtype_t::NA_BAD && adrs.ipv6.type == netadrtype_t::NA_BAD)
		{
			continue;
		}
		master.nextHeartbeatTime = svs.time + HEARTBEAT_MSEC;

		if ( adrs.ipv4.type != netadrtype_t::NA_BAD )
		{
			if (adrs.ipv4.port == 0) {
				adrs.ipv4.port = UBigShort( PORT_MASTER );
			}
			netLog.Notice( "Sending heartbeat (IPv4) to %s", master.addressCvar.Get() );
			// this command should be changed if the server info / status format
			// ever incompatibly changes
			Net::OutOfBandPrint( netsrc_t::NS_SERVER, adrs.ipv4, "heartbeat %s\n", hbname );
		}

		if ( adrs.ipv6.type != netadrtype_t::NA_BAD )
		{
			if (adrs.ipv6.port == 0) {
				adrs.ipv6.port = UBigShort( PORT_MASTER );
			}
			netLog.Notice( "Sending heartbeat (IPv6) to %s", master.addressCvar.Get() );
			// this command should be changed if the server info / status format
			// ever incompatibly changes
			Net::OutOfBandPrint( netsrc_t::NS_SERVER, adrs.ipv6, "heartbeat %s\n", hbname );
		}
	}
}

// Also called by SV_DropClient, SV_DirectConnect, and SV_SpawnServer
void SV_Heartbeat_f()
{
	for (MasterServer& master : masterServers)
	{
		master.nextHeartbeatTime = 0;
	}
}

/*
=================
SV_MasterShutdown

Informs all masters that this server is going down
=================
*/
void SV_MasterShutdown()
{
	// send a heartbeat right now
	SV_Heartbeat_f();
	SV_MasterHeartbeat( HEARTBEAT_DEAD );  // NERVE - SMF - changed to flatline

	// send it again to minimize chance of drops
//  SV_Heartbeat_f();
//  SV_MasterHeartbeat( HEARTBEAT_DEAD );

	// when the master tries to poll the server, it won't respond, so
	// it will be removed from the list
}

/*
==============================================================================

CONNECTIONLESS COMMANDS

==============================================================================
*/

/*
================
SVC_Status

Responds with all the info that qplug or qspy can see about the server
and all connected players.  Used for getting detailed information after
the simple info query.
================
*/
static void SVC_Status( const netadr_t& from, const Cmd::Args& args )
{
	if ( SV_Private(ServerPrivate::NoStatus) )
	{
		return;
	}

	InfoMap info_map;
	Cvar::PopulateInfoMap(CVAR_SERVERINFO, info_map);

	if ( args.Argc() > 1 && InfoValidItem(args.Argv(1)) )
	{
		// echo back the parameter to status. so master servers can use it as a challenge
		// to prevent timed spoofed reply packets that add ghost servers
		info_map["challenge"] = args.Argv(1);
	}

	std::string status;
	for ( int i = 0; i < sv_maxClients.Get(); i++ )
	{
		client_t* cl = &svs.clients[ i ];

		if ( cl->state >= clientState_t::CS_CONNECTED )
		{
			OpaquePlayerState* ps = SV_GameClientNum( i );
			status +=  Str::Format( "%i %i \"%s\"\n", ps->persistant[ PERS_SCORE ], cl->ping, cl->name );
		}
	}

	Net::OutOfBandPrint( netsrc_t::NS_SERVER, from, "statusResponse\n%s\n%s",
		InfoMapToString( info_map ), status );
}

/*
================
SVC_Info

Responds with a short info message that should be enough to determine
if a user is interested in a server to do a full status
================
*/
static void SVC_Info( const netadr_t& from, const Cmd::Args& args )
{
	if ( SV_Private(ServerPrivate::NoStatus) || !com_sv_running || !com_sv_running->integer )
	{
		return;
	}

	int bots = 0; // Bots always use public slots.
	int publicSlotHumans = 0;
	int privateSlotHumans = 0;

	for ( int i = 0; i < sv_maxClients.Get(); i++ )
	{
		if ( svs.clients[ i ].state >= clientState_t::CS_CONNECTED )
		{
			if (i < sv_privateClients.Get())
			{
				++privateSlotHumans;
			}
			else if ( SV_IsBot(&svs.clients[ i ]) )
			{
				++bots;
			}
			else
			{
				++publicSlotHumans;
			}
		}
	}

	InfoMap info_map;

	if ( args.Argc() > 1 && InfoValidItem(args.Argv(1)) )
	{
		std::string  challenge = args.Argv(1);
		// echo back the parameter to status. so master servers can use it as a challenge
		// to prevent timed spoofed reply packets that add ghost servers
		info_map["challenge"] = challenge;

		// If the master server listens on IPv4 and IPv6, we want to send the
		// most recent challenge received from it over the OTHER protocol
		for ( MasterServer& master : masterServers )
		{
			// First, see if the challenge was sent by this master server
			Net::DNSResult adrs = Net::GetAddresses( master.query );
			if ( !NET_CompareBaseAdr( from, adrs.ipv4 ) && !NET_CompareBaseAdr( from, adrs.ipv6 ) )
			{
				continue;
			}

			// It was - if the saved challenge is for the other protocol, send it and record the current one
			if ( master.challenge_address_type == netadrtype_t::NA_IP ||
				 master.challenge_address_type == netadrtype_t::NA_IP6 )
			{
				if ( master.challenge_address_type != from.type )
				{
					info_map["challenge2"] = master.challenge;
					master.challenge_address_type = from.type;
					master.challenge = challenge;
					break;
				}
			}

			// Otherwise record the current one regardless and check the next server
			master.challenge_address_type = from.type;
			master.challenge = challenge;
		}
	}

	info_map["protocol"] = std::to_string( PROTOCOL_VERSION );
	info_map["hostname"] = sv_hostname->string;
	info_map["serverload"] = std::to_string( svs.serverLoad );
	info_map["mapname"] = sv_mapname->string;
	info_map["clients"] = std::to_string( publicSlotHumans + privateSlotHumans );
	info_map["bots"] = std::to_string( bots );
	// Satisfies (number of open public slots) = (displayed max clients) - (number of clients).
	info_map["sv_maxclients"] = std::to_string(
	    std::max( 0, sv_maxClients.Get() - sv_privateClients.Get() ) + privateSlotHumans );

	if ( sv_statsURL->string[0] )
	{
		info_map["stats"] = sv_statsURL->string;
	}

	info_map["gamename"] = GAMENAME_STRING;  // Arnout: to be able to filter out Quake servers

	Net::OutOfBandPrint( netsrc_t::NS_SERVER, from, "infoResponse\n%s", InfoMapToString( info_map ) );
}

/*
 * Sends back a simple reply
 * Used to check if the server is online without sending any other info
 */
static void SVC_Ping( const netadr_t& from, const Cmd::Args& )
{
	if ( SV_Private(ServerPrivate::NoStatus) )
	{
		return;
	}

	Net::OutOfBandPrint( netsrc_t::NS_SERVER, from, "ack\n" );
}

/*
=================
SV_CheckDRDoS

DRDoS stands for "Distributed Reflected Denial of Service".
See here: http://www.lemuria.org/security/application-drdos.html

Returns false if we're good.  true return value means we need to block.
If the address isn't NA_IP, it's automatically denied.
=================
*/
bool SV_CheckDRDoS( netadr_t from )
{
	int        i;
	int        globalCount;
	int        specificCount;
	receipt_t  *receipt;
	netadr_t   exactFrom;
	int        oldest;
	int        oldestTime;
	static int lastGlobalLogTime = 0;
	static int lastSpecificLogTime = 0;

	// Usually the network is smart enough to not allow incoming UDP packets
	// with a source address being a spoofed LAN address.  Even if that's not
	// the case, sending packets to other hosts in the LAN is not a big deal.
	// NA_LOOPBACK qualifies as a LAN address.
	if ( Sys_IsLANAddress( from ) ) { return false; }

	exactFrom = from;

	if ( from.type == netadrtype_t::NA_IP )
	{
		from.ip[ 3 ] = 0; // xx.xx.xx.0
	}
	else if ( from.type == netadrtype_t::NA_IP6 )
	{
		memset( from.ip6 + 7, 0, 9 ); // mask to /56
	}
	else
	{
		// So we got a connectionless packet but it's not IPv4, so
		// what is it?  I don't care, it doesn't matter, we'll just block it.
		// This probably won't even happen.
		return true;
	}

	// Count receipts in last 2 seconds.
	globalCount = 0;
	specificCount = 0;
	receipt = &svs.infoReceipts[ 0 ];
	oldest = 0;
	oldestTime = 0x7fffffff;

	for ( i = 0; i < MAX_INFO_RECEIPTS; i++, receipt++ )
	{
		if ( receipt->time + 2000 > svs.time )
		{
			if ( receipt->time )
			{
				// When the server starts, all receipt times are at zero.  Furthermore,
				// svs.time is close to zero.  We check that the receipt time is already
				// set so that during the first two seconds after server starts, queries
				// from the master servers don't get ignored.  As a consequence a potentially
				// unlimited number of getinfo+getstatus responses may be sent during the
				// first frame of a server's life.
				globalCount++;
			}

			if ( NET_CompareBaseAdr( from, receipt->adr ) )
			{
				specificCount++;
			}
		}

		if ( receipt->time < oldestTime )
		{
			oldestTime = receipt->time;
			oldest = i;
		}
	}

	if ( globalCount == MAX_INFO_RECEIPTS ) // All receipts happened in last 2 seconds.
	{
		if ( lastGlobalLogTime + 1000 <= svs.time ) // Limit one log every second.
		{
			netLog.Notice( "Detected flood of getinfo/getstatus connectionless packets" );
			lastGlobalLogTime = svs.time;
		}

		return true;
	}

	if ( specificCount >= 5 ) // Already sent 5 to this IP address in last 2 seconds.
	{
		if ( lastSpecificLogTime + 1000 <= svs.time ) // Limit one log every second.
		{
			netLog.Notice( "Possible DRDoS attack to address %i.%i.%i.%i, ignoring getinfo/getstatus connectionless packet",
			               exactFrom.ip[ 0 ], exactFrom.ip[ 1 ], exactFrom.ip[ 2 ], exactFrom.ip[ 3 ] );
			lastSpecificLogTime = svs.time;
		}

		return true;
	}

	receipt = &svs.infoReceipts[ oldest ];
	receipt->adr = from;
	receipt->time = svs.time;
	return false;
}

/*
===============
SVC_RemoteCommand

An rcon packet arrived from the network.
Shift down the remaining args
Redirect all printfs
===============
*/

class RconEnvironment: public Cmd::DefaultEnvironment {
public:
	RconEnvironment(const netadr_t& from)
		: from(from), bufferSize(MAX_MSGLEN - prefix.size() - 1)
	{}

	virtual void Print(Str::StringRef text) override
	{
		if (text.size() + buffer.size() > bufferSize - 1)
		{
			Flush();
		}

		buffer += text;
		buffer += '\n';
	}

	void Flush()
	{
		if ( !buffer.empty() )
		{
			Net::OutOfBandPrint(netsrc_t::NS_SERVER, from, "%s\n%s", prefix, buffer);
			buffer.clear();
		}
	}

	static void PrintError(const netadr_t& to, const std::string& message)
	{
		Net::OutOfBandPrint(netsrc_t::NS_SERVER, to, "error\n%s", message);
	}

private:
	const netadr_t from;
	std::string buffer;
	const std::string prefix = "print";
	const std::size_t bufferSize;
};

static Cvar::Cvar<std::string> cvar_rcon_server_password(
    "rcon.server.password",
    "Password used to protect the remote console",
    Cvar::NONE,
    ""
);

static Cvar::Range<Cvar::Cvar<int>> cvar_rcon_server_secure(
    "rcon.server.secure",
    "How secure the Rcon protocol should be: "
        "0: Allow unencrypted rcon, "
        "1: Require encryption, "
        "2: Require encryption and challenge check",
    Cvar::NONE,
    0,
    0,
    2
);

/*
 * Checks whether the message is acceptable by the server,
 * it must be valid and match the rcon settings and challenges.
 */
static bool RconAcceptable(const Rcon::Message& msg, std::string *invalid_reason = nullptr)
{
    auto invalid = [invalid_reason](const char* reason)
    {
        if ( invalid_reason )
            *invalid_reason = reason;
        return false;
    };

    if ( !msg.Valid(invalid_reason) )
    {
        return false;
    }

    if ( msg.secure < Rcon::Secure(cvar_rcon_server_secure.Get()) )
    {
        return invalid("Weak security");
    }

    if ( cvar_rcon_server_password.Get().empty() )
    {
        return invalid("No rcon.server.password set on the server.");
    }

    if ( msg.password != cvar_rcon_server_password.Get() )
    {
        return invalid("Bad password");
    }

    if ( msg.secure == Rcon::Secure::EncryptedChallenge )
    {
        if ( !ChallengeManager::MatchString(msg.remote, msg.challenge) )
        {
            return invalid("Mismatched challenge");
        }
    }

    return true;
}

/*
 * Decodes the arguments of an out of band message received by the server
 */
static Rcon::Message RconDecode(const netadr_t& remote, const Cmd::Args& args)
{
    if ( args.size() < 3 || (args[0] != "rcon" && args[0] != "srcon") )
    {
        return Rcon::Message("Invalid command");
    }

    if ( cvar_rcon_server_password.Get().empty() )
    {
        return Rcon::Message("rcon.server.password not set");
    }

    if ( args[0] == "rcon" )
    {
        return Rcon::Message(remote, args.EscapedArgs(2), Rcon::Secure::Unencrypted, args[1]);
    }

    auto authentication = args[1];
    Crypto::Data cyphertext = Crypto::FromString(args[2]);

    Crypto::Data data;
    if ( !Crypto::Encoding::Base64Decode( cyphertext, data ) )
    {
        return Rcon::Message("Invalid Base64 string");
    }

    Crypto::Data key = Crypto::Hash::Sha256( Crypto::FromString( cvar_rcon_server_password.Get() ) );

    if ( !Crypto::Aes256Decrypt( data, key, data ) )
    {
        return Rcon::Message("Error during decryption");
    }

    std::string command = Crypto::ToString( data );

    if ( authentication == "CHALLENGE" )
    {
        std::istringstream stream( command );
        std::string challenge_hex;
        stream >> challenge_hex;

        while ( Str::cisspace( stream.peek() ) )
        {
            stream.ignore();
        }

        std::getline( stream, command );

        return Rcon::Message(remote, command, Rcon::Secure::EncryptedChallenge,
            cvar_rcon_server_password.Get(), challenge_hex);
    }
    else if ( authentication == "PLAIN" )
    {
        return Rcon::Message(remote, command, Rcon::Secure::EncryptedPlain,
            cvar_rcon_server_password.Get());
    }
    else
    {
        return Rcon::Message(remote, command, Rcon::Secure::Invalid,
            cvar_rcon_server_password.Get());
    }
}

static int RemoteCommandThrottle()
{
    static int lasttime = 0;
    int time = Com_Milliseconds();
    int delta = time - lasttime;
    lasttime = time;

    return delta;
}

static void SVC_RemoteCommand( const netadr_t& from, const Cmd::Args& args )
{
	int throttle_delta = RemoteCommandThrottle();

	if ( throttle_delta < 180 )
	{
		return;
	}

	Rcon::Message message = RconDecode(from, args);

	std::string invalid_reason;

	if ( !RconAcceptable(message, &invalid_reason) )
	{
		// If the rconpassword is bad and one just happened recently, don't spam the log file, just die.
		if ( throttle_delta < 600 )
		{
			return;
		}

		netLog.Notice( "Bad rcon from %s:\n%s\n%s",
			Net::AddressToString( from ),
			invalid_reason.c_str(),
			args.ConcatArgs(2).c_str() );

		if ( !SV_Private(ServerPrivate::NoStatus) )
		{
			RconEnvironment::PrintError( from, invalid_reason );
		}
	}
	else
	{
		netLog.Notice( "Rcon from %s:\n%s", Net::AddressToString( from ), message.command.c_str() );

		// start redirecting all print outputs to the packet
		auto env = RconEnvironment(from);
		Cmd::ExecuteCommand(message.command, true, &env);
		Cmd::ExecuteCommandBuffer();
		env.Flush();
	}

}

static void SVC_RconInfo( const netadr_t& from, const Cmd::Args& )
{
	if ( SV_Private(ServerPrivate::NoStatus) )
	{
		return;
	}

	int duration_seconds = std::chrono::duration_cast<std::chrono::seconds>(Challenge::Timeout()).count();
	std::string rcon_info_string = InfoMapToString({
		{"secure",     std::to_string(cvar_rcon_server_secure.Get())},
		{"encryption", "AES256"},
		{"key",        "SHA256"},
		{"challenge",  std::to_string(cvar_rcon_server_secure.Get() >= 2)},
		{"timeout",    std::to_string(duration_seconds)},
	});
	Net::OutOfBandPrint( netsrc_t::NS_SERVER, from, "rconInfoResponse\n%s\n", rcon_info_string );
}


/*
=================
SV_ConnectionlessPacket

A connectionless packet has four leading 0xff
characters to distinguish it from a game channel.
Clients that are in the game can still send
connectionless packets.
=================
*/
static void SV_ConnectionlessPacket( const netadr_t& from, msg_t *msg )
{
	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );  // skip the -1 marker

	if ( !Q_strncmp( "connect", ( char * ) &msg->data[ 4 ], 7 ) )
	{
		Huff_Decompress( msg, 12 );
	}

	Cmd::Args args(MSG_ReadStringLine( msg ));

	if ( args.Argc() <= 0 )
	{
		return;
	}

	netLog.Debug( "SV packet %s : %s", Net::AddressToString( from ), args.Argv(0) );

	if ( args.Argv(0) == "getstatus" )
	{
		if ( SV_CheckDRDoS( from ) ) { return; }

		SVC_Status( from, args );
	}
	else if ( args.Argv(0) == "getinfo" )
	{
		if ( SV_CheckDRDoS( from ) ) { return; }

		SVC_Info( from, args );
	}
	else if ( args.Argv(0) == "getchallenge" )
	{
		SV_GetChallenge( from );
	}
	else if ( args.Argv(0) == "connect" )
	{
		SV_DirectConnect( from, args );
	}
	else if ( args.Argv(0) == "rcon" || args.Argv(0) == "srcon" )
	{
		SVC_RemoteCommand( from, args );
	}
	else if ( args.Argv(0) == "rconinfo" )
	{
		SVC_RconInfo( from, args );
	}
	else if ( args.Argv(0) == "disconnect" )
	{
		// if a client starts up a local server, we may see some spurious
		// server disconnect messages when their new server sees our final
		// sequenced messages to the old client
	}
	else if ( args.Argv(0) == "ping" )
	{
		SVC_Ping( from, args );
	}
	else
	{
		netLog.Verbose( "bad connectionless packet from %s: %s", Net::AddressToString( from ), args.ConcatArgs(0) );
	}
}

//============================================================================

static bool SV_IsAllowedNetwork( const netadr_t& address )
{
	switch ( sv_networkScope.Get() )
	{
	case 0:
		return address.type == netadrtype_t::NA_LOOPBACK;
	case 1:
		return Sys_IsLANAddress( address );
	case 2:
		return true;
	}

	ASSERT_UNREACHABLE();
}

/*
=================
SV_PacketEvent
=================
*/
void SV_PacketEvent( const netadr_t& from, msg_t *msg )
{
	int      i;
	client_t *cl;
	int      qport;

	if ( !SV_IsAllowedNetwork( from ) )
	{
		return;
	}

	// check for connectionless packet (0xffffffff) first
	if ( msg->cursize >= 4 && * ( int * ) msg->data == -1 )
	{
		SV_ConnectionlessPacket( from, msg );
		return;
	}

	// read the qport out of the message so we can fix up
	// stupid address translating routers
	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );  // sequence number
	qport = MSG_ReadShort( msg ) & 0xffff;

	// find which client the message is from
	for ( i = 0, cl = svs.clients; i < sv_maxClients.Get(); i++, cl++ )
	{
		if ( cl->state == clientState_t::CS_FREE )
		{
			continue;
		}

		if ( !NET_CompareBaseAdr( from, cl->netchan.remoteAddress ) )
		{
			continue;
		}

		// it is possible to have multiple clients from a single IP
		// address, so they are differentiated by the qport variable
		if ( cl->netchan.qport != qport )
		{
			continue;
		}

		// the IP port can't be used to differentiate them, because
		// some address translating routers periodically change UDP
		// port assignments
		if ( cl->netchan.remoteAddress.port != from.port )
		{
			netLog.Notice( "SV_PacketEvent: fixing up a translated port" );
			cl->netchan.remoteAddress.port = from.port;
		}

		// make sure it is a valid, in sequence packet
		if ( Netchan_Process( &cl->netchan, msg ) )
		{
			// zombie clients still need to do the Netchan_Process
			// to make sure they don't need to retransmit the final
			// reliable message, but they don't do any other processing
			if ( cl->state != clientState_t::CS_ZOMBIE )
			{
				cl->lastPacketTime = svs.time; // don't timeout
				SV_ExecuteClientMessage( cl, msg );
			}
		}

		return;
	}

	// if we received a sequenced packet from an address we don't recognize,
	// send an out of band disconnect packet to it
	Net::OutOfBandPrint( netsrc_t::NS_SERVER, from, "disconnect" );
}

/*
===================
SV_CalcPings

Updates the cl->ping variables
===================
*/
void SV_CalcPings()
{
	int           i, j;
	client_t      *cl;
	int           total, count;
	int           delta;

	for ( i = 0; i < sv_maxClients.Get(); i++ )
	{
		cl = &svs.clients[ i ];

		if ( cl->state != clientState_t::CS_ACTIVE )
		{
			cl->ping = 999;
			continue;
		}

		if ( !cl->gentity )
		{
			cl->ping = 999;
			continue;
		}

		if ( SV_IsBot(cl) )
		{
			cl->ping = 0;
			continue;
		}

		total = 0;
		count = 0;

		for ( j = 0; j < PACKET_BACKUP; j++ )
		{
			if ( cl->frames[ j ].messageAcked <= 0 )
			{
				continue;
			}

			delta = cl->frames[ j ].messageAcked - cl->frames[ j ].messageSent;
			count++;
			total += delta;
		}

		if ( !count )
		{
			cl->ping = 999;
		}
		else
		{
			cl->ping = total / count;

			if ( cl->ping > 999 )
			{
				cl->ping = 999;
			}
		}

		// let the game module know about the ping
		OpaquePlayerState* ps = SV_GameClientNum( i );
		ps->ping = cl->ping;
	}
}

/*
==================
SV_CheckTimeouts

If a packet has not been received from a client for timeout->integer
seconds, drop the connection.  Server time is used instead of
realtime to avoid dropping the local client while debugging.

When a client is dropped, the client_t goes into a zombie state
for a few seconds to make sure any final reliable message gets resent
if necessary
==================
*/
void SV_CheckTimeouts()
{
	int      i;
	client_t *cl;
	int      droppoint;
	int      zombiepoint;

	droppoint = svs.time - 1000 * sv_timeout->integer;
	zombiepoint = svs.time - 1000 * sv_zombietime->integer;

	for ( i = 0, cl = svs.clients; i < sv_maxClients.Get(); i++, cl++ )
	{
		// message times may be wrong across a changelevel
		if ( cl->lastPacketTime > svs.time )
		{
			cl->lastPacketTime = svs.time;
		}

		if ( SV_IsBot( cl ) )
		{
			continue;
		}

		if ( cl->state == clientState_t::CS_ZOMBIE && cl->lastPacketTime < zombiepoint )
		{
			// using the client id cause the cl->name is empty at this point
			Log::Debug( "Going from CS_ZOMBIE to CS_FREE for client %d", i );
			cl->state = clientState_t::CS_FREE; // can now be reused

			continue;
		}

		if ( cl->state >= clientState_t::CS_CONNECTED && cl->lastPacketTime < droppoint )
		{
			// wait several frames so a debugger session doesn't
			// cause a timeout
			if ( ++cl->timeoutCount > 5 )
			{
				SV_DropClient( cl, "timed out" );
				cl->state = clientState_t::CS_FREE; // don't bother with zombie state
			}
		}
		else
		{
			cl->timeoutCount = 0;
		}
	}
}

/*
==================
SV_FrameMsec
Return time in milliseconds until processing of the next server frame.
==================
*/
int SV_FrameMsec()
{
	if( sv_fps )
	{
		const int frameMsec = static_cast<int>(1000.0f / sv_fps->value);
		int scaledResidual = static_cast<int>( sv.timeResidual / com_timescale->value );

		if ( frameMsec < scaledResidual )
		{
			return 0;
		}
		else
		{
			return frameMsec - scaledResidual;
		}
	}
	else
	{
		return 1;
	}
}


/*
==================
SV_Frame

Player movement occurs as a result of packet events, which
happen before SV_Frame is called
==================
*/
void SV_Frame( int msec )
{
	int        frameMsec;
	int        startTime;
	int        frameStartTime = 0, frameEndTime;
	static int start, end;

	start = Sys::Milliseconds();
	svs.stats.idle += ( double )( start - end ) / 1000;

	// the menu kills the server with this cvar
	if ( sv_killserver->integer )
	{
		SV_Shutdown( "Server was killed." );
		Cvar_Set( "sv_killserver", "0" );
		return;
	}

	if ( !com_sv_running->integer )
	{
		return;
	}

	frameStartTime = Sys::Milliseconds();

	// if it isn't time for the next frame, do nothing
	if ( sv_fps->integer < 1 )
	{
		Cvar_Set( "sv_fps", "10" );
	}

	frameMsec = 1000 / sv_fps->integer;

	sv.timeResidual += msec;

	if ( Com_IsDedicatedServer() && sv.timeResidual < frameMsec )
	{
		// NET_Sleep will give the OS time slices until either get a packet
		// or time enough for a server frame has gone by
		NET_Sleep( frameMsec - sv.timeResidual );
		return;
	}

	// if time is about to hit the 32nd bit, kick all clients
	// and clear sv.time, rather
	// than checking for negative time wraparound everywhere.
	// This generally won't happen because the engine will exit earlier to prevent
	// Sys::Milliseconds overflow, but maybe with timescale > 1 or something
	if ( svs.time > 0x78000000 )
	{
		// TTimo
		// show_bug.cgi?id=388
		// there won't be a map_restart if you have shut down the server
		// since it doesn't restart a non-running server
		// instead, re-run the current map
		Cmd::BufferCommandText(Str::Format("map %s", Cmd::Escape(sv_mapname->string)));

		Sys::Drop( "Restarting server due to time wrapping" );
	}

	// this can happen considerably earlier when lots of clients play and the map doesn't change
	if ( svs.nextSnapshotEntities >= 0x7FFFFFFE - svs.numSnapshotEntities )
	{
		// TTimo see above
		Cmd::BufferCommandText(Str::Format("map %s", Cmd::Escape(sv_mapname->string)));
		Sys::Drop( "Restarting server due to numSnapshotEntities wrapping" );
	}

	if ( sv.restartTime && sv.time >= sv.restartTime )
	{
		sv.restartTime = 0;
		Cmd::BufferCommandText("map_restart 0");
		return;
	}

	// update infostrings if anything has been changed
	if ( cvar_modifiedFlags & CVAR_SERVERINFO )
	{
		SV_SetConfigstring( CS_SERVERINFO, Cvar_InfoString( CVAR_SERVERINFO, false ) );
		cvar_modifiedFlags &= ~CVAR_SERVERINFO;
	}

	if ( cvar_modifiedFlags & CVAR_SYSTEMINFO )
	{
		SV_SetConfigstring( CS_SYSTEMINFO, Cvar_InfoString( CVAR_SYSTEMINFO, true ) );
		cvar_modifiedFlags &= ~CVAR_SYSTEMINFO;
	}

	if ( com_speeds->integer )
	{
		startTime = Sys::Milliseconds();
	}
	else
	{
		startTime = 0; // quite a compiler warning
	}

	// update ping based on the all received frames
	SV_CalcPings();

	// run the game simulation in chunks
	while ( sv.timeResidual >= frameMsec )
	{
		sv.timeResidual -= frameMsec;
		svs.time += frameMsec;
		sv.time += frameMsec;

		// let everything in the world think and move
		gvm.GameRunFrame( sv.time );
	}

	if ( com_speeds->integer )
	{
		time_game = Sys::Milliseconds() - startTime;
	}

	// check timeouts
	SV_CheckTimeouts();

	// send messages back to the clients
	SV_SendClientMessages();

	// send a heartbeat to the master if needed
	SV_MasterHeartbeat( HEARTBEAT_GAME );

	frameEndTime = Sys::Milliseconds();

	svs.totalFrameTime += ( frameEndTime - frameStartTime );
	svs.currentFrameIndex++;

	//if( svs.currentFrameIndex % 50 == 0 )
	//  Log::Notice( "currentFrameIndex: %i", svs.currentFrameIndex );

	if ( svs.currentFrameIndex == SERVER_PERFORMANCECOUNTER_FRAMES )
	{
		int averageFrameTime;

		averageFrameTime = svs.totalFrameTime / SERVER_PERFORMANCECOUNTER_FRAMES;

		svs.sampleTimes[ svs.currentSampleIndex % SERVER_PERFORMANCECOUNTER_SAMPLES ] = averageFrameTime;
		svs.currentSampleIndex++;

		if ( svs.currentSampleIndex > SERVER_PERFORMANCECOUNTER_SAMPLES )
		{
			int totalTime, i;

			totalTime = 0;

			for ( i = 0; i < SERVER_PERFORMANCECOUNTER_SAMPLES; i++ )
			{
				totalTime += svs.sampleTimes[ i ];
			}

			if ( !totalTime )
			{
				totalTime = 1;
			}

			averageFrameTime = totalTime / SERVER_PERFORMANCECOUNTER_SAMPLES;

			svs.serverLoad = static_cast<int>(( averageFrameTime / static_cast<float>(frameMsec) ) * 100.0F);
		}

		//Log::Notice( "serverload: %i (%i/%i)", svs.serverLoad, averageFrameTime, frameMsec );

		svs.totalFrameTime = 0;
		svs.currentFrameIndex = 0;
	}

	// collect timing statistics
	end = Sys::Milliseconds();
	svs.stats.active += ( ( double )( end - start ) ) / 1000;

	if ( ++svs.stats.count == STATFRAMES )
	{
		svs.stats.latched_active = svs.stats.active;
		svs.stats.latched_idle = svs.stats.idle;
		svs.stats.latched_packets = svs.stats.packets;
		svs.stats.active = 0;
		svs.stats.idle = 0;
		svs.stats.packets = 0;
		svs.stats.count = 0;
	}
}

/*
========================
 SV_PrintTranslatedText

 Translation for print_tr and friends is currently disabled for the *dedicated server console*.
 But we need TranslateText_Internal to interpret the format substitutions like $1$.

 TODO: move this substitution stuff into the sgame since print_tr etc. is really
 a gamelogic-internal concept.
========================
 */
#define TRANSLATE_FUNC(msg) (msg)
#define PLURAL_TRANSLATE_FUNC(msg, msg2, num) (Q_UNUSED(msg2), Q_UNUSED(num), msg)
#include "qcommon/print_translated.h"

void SV_PrintTranslatedText( const char *text, bool broadcast, bool plural )
{
	Cmd_SaveCmdContext();
	Cmd_TokenizeString( text );
	if ( broadcast )
	{
		// Don't use "Broadcast: %s" as this format string is too unspecific for log suppression
		Log::Notice( std::string("Broadcast: ") + TranslateText_Internal( plural, 1 ) );
	}
	else
	{
		Log::CommandInteractionMessage( TranslateText_Internal( plural, 1 ) );
	}
	Cmd_RestoreCmdContext();
}


//============================================================================
