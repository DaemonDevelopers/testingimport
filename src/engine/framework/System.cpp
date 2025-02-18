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

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "Application.h"
#include "ConsoleHistory.h"
#include "CommandSystem.h"
#include "LogSystem.h"
#include "System.h"
#include "CrashDump.h"
#include "CvarSystem.h"
#include <common/FileSystem.h>
#ifdef _WIN32
#include <windows.h>
#include <SDL.h>
#else
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/file.h>
#endif

#if defined(DAEMON_USE_FLOAT_EXCEPTIONS)
	#if defined(__USE_GNU) || defined(__FreeBSD__) || defined(_WIN32)
		#if defined(DAEMON_ARCH_amd64) || defined(DAEMON_ARCH_i686)
			#define DAEMON_USE_FLOAT_EXCEPTIONS_AVAILABLE
		#endif
	#elif defined (__APPLE__)
		#if defined(DAEMON_ARCH_amd64) || defined(DAEMON_ARCH_arm64)
			#define DAEMON_USE_FLOAT_EXCEPTIONS_AVAILABLE
		#endif
	#endif

	#if defined(DAEMON_USE_FLOAT_EXCEPTIONS_AVAILABLE)
		#include <cfenv>
		#include <cfloat>

		static Cvar::Cvar<bool> common_floatExceptions_invalid("common.floatExceptions.invalid",
			"enable floating point exception for operation with NaN",
			Cvar::INIT, false);
		static Cvar::Cvar<bool> common_floatExceptions_divByZero("common.floatExceptions.divByZero",
			"enable floating point exception for division-by-zero operation",
			Cvar::INIT, false);
		static Cvar::Cvar<bool> common_floatExceptions_overflow("common.floatExceptions.overflow",
			"enable floating point exception for operation producing an overflow",
			Cvar::INIT, false);
	#else
		#warning Missing floating point exception implementation.
	#endif
#endif

namespace Sys {
static Cvar::Cvar<bool> cvar_common_shutdownOnDrop("common.shutdownOnDrop", "shut down engine on game drop", Cvar::TEMPORARY, false);

static std::string singletonSocketPath;
#ifdef _WIN32
static HANDLE singletonSocket;
#else
#define SINGLETON_SOCKET_BASENAME "socket"
static int singletonSocket;
static FS::File lockFile;
static bool haveSingletonLock = false;

static void FillSocketStruct(struct sockaddr_un &addr)
{
	addr.sun_family = AF_UNIX;
#ifdef __APPLE__
	Q_strncpyz(addr.sun_path, SINGLETON_SOCKET_BASENAME, sizeof(addr.sun_path));
#else
	if (singletonSocketPath.size() > sizeof(addr.sun_path)) {
		Sys::Error("Singleton socket name '%s' is too long. Try configuring a shorter $TMPDIR",
		           singletonSocketPath);
	}
	Q_strncpyz(addr.sun_path, singletonSocketPath.c_str(), sizeof(addr.sun_path));
#endif
}

#ifdef __APPLE__
// Secret Apple APIs. Chrome just declares them like this
extern "C" {
	int pthread_chdir_np(const char* path);
	int pthread_fchdir_np(int fd);
}

// These ChdirWrapper* functions return 0 on success or an errno on failure
static int ChdirWrapperSingletonSocketConnect()
{
	std::string dirName = FS::Path::DirName(singletonSocketPath);
	int error = 0;
	if (0 == pthread_chdir_np(dirName.c_str())) {
		struct sockaddr_un addr;
		FillSocketStruct(addr);
		if (0 != connect(singletonSocket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))) {
			error = errno;
		}
	} else {
		// Assume the directory didn't exist. If it does but it is not accessible, we should
		// hit another error soon when trying to create the singleton socket
		error = ENOENT;
	}
	pthread_fchdir_np(-1); // reset CWD
	return error;
}

static int ChdirWrapperSingletonSocketBind()
{
	std::string dirName = FS::Path::DirName(singletonSocketPath);
	bool chdirSuccess = 0 == pthread_chdir_np(dirName.c_str());
	int error = 0;
	if (chdirSuccess) {
		struct sockaddr_un addr;
		FillSocketStruct(addr);
		if (0 != bind(singletonSocket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))) {
			error = errno;
		}
	}
	pthread_fchdir_np(-1); // reset CWD
	if (!chdirSuccess) {
		Sys::Error("Failed to create or failed to access singleton socket directory '%s'", dirName);
	}
	return error;
}
#else // ! ifdef __APPLE__
// TODO: supposedly there is an API that can be used to make chdir thread safe
// on Linux too called "unshare"?
static int ChdirWrapperSingletonSocketConnect()
{
	struct sockaddr_un addr;
	FillSocketStruct(addr);
	return 0 == connect(singletonSocket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))
		? 0
		: errno;
}

static int ChdirWrapperSingletonSocketBind()
{
	struct sockaddr_un addr;
	FillSocketStruct(addr);
	return 0 == bind(singletonSocket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))
		? 0
		: errno;
}
#endif // ! ifdef __APPLE__
#endif // ! ifdef _WIN32

// Get the path of a singleton socket
std::string GetSingletonSocketPath()
{
    auto& homePathSuffix = Application::GetTraits().uniqueHomepathSuffix;
	// Use the hash of the homepath to identify instances sharing a homepath
	const std::string& homePath = FS::GetHomePath();
	char homePathHash[33] = "";
	Com_MD5Buffer(homePath.data(), homePath.size(), homePathHash, sizeof(homePathHash));
	std::string suffix = homePathSuffix + "-" + homePathHash;
#ifdef _WIN32
	return "\\\\.\\pipe\\" PRODUCT_NAME + suffix;
#else
	// We use a temporary directory rather that using the homepath because
	// socket paths are limited to about 100 characters. This also avoids issues
	// when the homepath is on a network filesystem.
	return FS::Path::Build(FS::Path::Build(
		FS::DefaultTempPath(), "." PRODUCT_NAME_LOWER + suffix), SINGLETON_SOCKET_BASENAME);
#endif
}

// Create a socket to listen for commands from other instances
static void CreateSingletonSocket()
{
#ifdef _WIN32
	singletonSocket = CreateNamedPipeA(singletonSocketPath.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 1000, nullptr);
	if (singletonSocket == INVALID_HANDLE_VALUE)
		Sys::Error("Could not create singleton socket: %s", Sys::Win32StrError(GetLastError()));
#else
	// Grab a lock to avoid race conditions. This lock is automatically released when
	// the process quits, but it may remain if the homepath is on a network filesystem.
	// We restrict the permissions to owner-only to prevent other users from grabbing
	// an exclusive lock (which only requires read access).
    auto& suffix = Application::GetTraits().uniqueHomepathSuffix;
	try {
		mode_t oldMask = umask(0077);
		lockFile = FS::HomePath::OpenWrite(std::string("lock") + suffix);
		umask(oldMask);
		if (flock(fileno(lockFile.GetHandle()), LOCK_EX | LOCK_NB) == -1)
			throw std::system_error(errno, std::generic_category());
	} catch (std::system_error& err) {
		Sys::Error("Could not acquire singleton lock: %s\n"
		           "If you are sure no other instance is running, delete %s", err.what(), FS::Path::Build(FS::GetHomePath(), std::string("lock") + suffix));
	}
	haveSingletonLock = true;

	// Delete any stale sockets
	std::string dirName = FS::Path::DirName(singletonSocketPath);
	unlink(singletonSocketPath.c_str());
	rmdir(dirName.c_str());

	singletonSocket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (singletonSocket == -1)
		Sys::Error("Could not create singleton socket: %s", strerror(errno));

	// Set socket permissions to only be accessible to the current user
	fchmod(singletonSocket, 0600);
	mkdir(dirName.c_str(), 0700);

	int bindErr = ChdirWrapperSingletonSocketBind();
	if (bindErr != 0)
		Sys::Error("Could not bind singleton socket at file: \"%s\", error: \"%s\"", singletonSocketPath, strerror(bindErr) );

	if (listen(singletonSocket, SOMAXCONN) == -1)
		Sys::Error("Could not listen on singleton socket file \"%s\", error: \"%s\"", singletonSocketPath, strerror(errno) );
#endif
}

// Try to connect to an existing socket to send our commands to an existing instance
static bool ConnectSingletonSocket()
{
#ifdef _WIN32
	while (true) {
		singletonSocket = CreateFileA(singletonSocketPath.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (singletonSocket != INVALID_HANDLE_VALUE)
			break;
		const DWORD rc = GetLastError();
		if (rc != ERROR_PIPE_BUSY) {
			if (rc != ERROR_FILE_NOT_FOUND)
				Log::Warn("Could not connect to existing instance: %s", Sys::Win32StrError(rc));
			return false;
		}
		WaitNamedPipeA(singletonSocketPath.c_str(), NMPWAIT_WAIT_FOREVER);
	}

	return true;
#else
	singletonSocket = socket(AF_UNIX, SOCK_STREAM, 0);
	if (singletonSocket == -1)
		Sys::Error("Could not create socket: %s", strerror(errno));

	int connectErr = ChdirWrapperSingletonSocketConnect();
	if (connectErr != 0) {
		if (connectErr != ENOENT)
			Log::Warn("Could not connect to existing instance: %s", strerror(connectErr));
		close(singletonSocket);
		return false;
	}

	return true;
#endif
}

// Send commands to the existing instance
static void WriteSingletonSocket(Str::StringRef commands)
{
#ifdef _WIN32
	DWORD result = 0;
	if (!WriteFile(singletonSocket, commands.data(), commands.size(), &result, nullptr))
		Log::Warn("Could not send commands through socket: %s", Sys::Win32StrError(GetLastError()));
	else if (result != commands.size())
		Log::Warn("Could not send commands through socket: Short write");
#else
	ssize_t result = write(singletonSocket, commands.data(), commands.size());
	if (result == -1 || static_cast<size_t>(result) != commands.size())
		Log::Warn("Could not send commands through socket: %s", result == -1 ? strerror(errno) : "Short write");
#endif
}

// Handle any commands sent by other instances
#ifdef _WIN32
static void ReadSingletonSocketCommands()
{
	std::string commands;
	char buffer[4096];
	while (true) {
		DWORD result = 0;
		if (!ReadFile(singletonSocket, buffer, sizeof(buffer), &result, nullptr)) {
			const DWORD rc = GetLastError();
			if (rc != ERROR_NO_DATA && rc != ERROR_BROKEN_PIPE) {
				Log::Warn("Singleton socket ReadFile() failed: %s", Sys::Win32StrError(rc));
				return;
			} else
				break;
		}
		if (result == 0)
			break;
		commands.append(buffer, result);
	}

	Cmd::BufferCommandText(commands);
}
#else
static void ReadSingletonSocketCommands(int sock)
{
	std::string commands;
	char buffer[4096];
	while (true) {
		ssize_t result = read(sock, buffer, sizeof(buffer));
		if (result == -1) {
			Log::Warn("Singleton socket read() failed: %s", strerror(errno));
			return;
		}
		if (result == 0)
			break;
		commands.append(buffer, result);
	}

	Cmd::BufferCommandText(commands);
}
#endif
void ReadSingletonSocket()
{
#ifdef _WIN32
	while (true) {
		if (!ConnectNamedPipe(singletonSocket, nullptr)) {
			Log::Warn("Singleton socket ConnectNamedPipe() failed: %s", Sys::Win32StrError(GetLastError()));

			// Stop handling incoming commands if an error occured
			CloseHandle(singletonSocket);
			return;
		}

		ReadSingletonSocketCommands();
		DisconnectNamedPipe(singletonSocket);
	}
#else
	while (true) {
		int sock = accept(singletonSocket, nullptr, nullptr);
		if (sock == -1) {
			Log::Warn("Singleton socket accept() failed: %s", strerror(errno));

			// Stop handling incoming commands if an error occured
			close(singletonSocket);
			return;
		}

		ReadSingletonSocketCommands(sock);
		close(sock);
	}
#endif
}

static void CloseSingletonSocket()
{
#ifdef _WIN32
	// We do not close the singleton socket on Windows because another thread is
	// currently busy waiting for message. This would cause CloseHandle to block
	// indefinitely. Instead we rely on process shutdown to close the handle.
#else
	if (!haveSingletonLock)
		return;
	std::string dirName = FS::Path::DirName(singletonSocketPath);
	unlink(singletonSocketPath.c_str());
	rmdir(dirName.c_str());
	try {
		FS::HomePath::DeleteFile(std::string("lock") + Application::GetTraits().uniqueHomepathSuffix);
	} catch (std::system_error&) {}
#endif
}

static void SetFloatingPointExceptions()
{
#if defined(DAEMON_USE_FLOAT_EXCEPTIONS_AVAILABLE)
	#if defined(DAEMON_ARCH_amd64) || defined(DAEMON_ARCH_i686)
		#if defined(__USE_GNU) || defined(__FreeBSD__) || defined(__APPLE__)
			int exceptions = 0;
		#elif defined(_WIN32)
			unsigned int exceptions = 0;
		#endif

		#if defined(DAEMON_USE_ARCH_INTRINSICS_i686_sse)
			int mxcsr_exceptions = 0;
		#endif
	#elif defined(DAEMON_ARCH_arm64)
		#if defined(__APPLE__)
			unsigned long long fpcr_exceptions = 0;
		#endif
	#endif

	// Operations with NaN.
	if (common_floatExceptions_invalid.Get())
	{
		#if defined(DAEMON_ARCH_amd64) || defined(DAEMON_ARCH_i686)
			#if defined(__USE_GNU) || defined(__FreeBSD__) || defined(__APPLE__)
				exceptions |= FE_INVALID;
			#elif defined(_WIN32)
				exceptions |= _EM_INVALID;
			#endif

			#if defined(DAEMON_USE_ARCH_INTRINSICS_i686_sse)
				mxcsr_exceptions |= _MM_MASK_INVALID;
			#endif
		#elif defined(DAEMON_ARCH_arm64)
			#if defined(__APPLE__)
				fpcr_exceptions |= __fpcr_trap_invalid;
			#endif
		#endif
	}

	// Division by zero.
	if (common_floatExceptions_divByZero.Get())
	{
		#if defined(DAEMON_ARCH_amd64) || defined(DAEMON_ARCH_i686)
			#if defined(__USE_GNU) || defined(__FreeBSD__) || defined(__APPLE__)
				exceptions |= FE_DIVBYZERO;
			#elif defined(_WIN32)
				exceptions |= _EM_ZERODIVIDE;
			#endif

			#if defined(DAEMON_USE_ARCH_INTRINSICS_i686_sse)
				mxcsr_exceptions |= _MM_MASK_DIV_ZERO;
			#endif
		#elif defined(DAEMON_ARCH_arm64)
			#if defined(__APPLE__)
				fpcr_exceptions |= __fpcr_trap_divbyzero;
			#endif
		#endif
	}

	// Operations producing an overflow.
	if (common_floatExceptions_overflow.Get())
	{
		#if defined(DAEMON_ARCH_amd64) || defined(DAEMON_ARCH_i686)
			#if defined(__USE_GNU) || defined(__FreeBSD__) || defined(__APPLE__)
				exceptions |= FE_OVERFLOW;
			#elif defined(_WIN32)
				exceptions |= _EM_OVERFLOW;
			#endif

			#if defined(DAEMON_USE_ARCH_INTRINSICS_i686_sse)
				mxcsr_exceptions |= _MM_MASK_OVERFLOW;
			#endif
		#elif defined(DAEMON_ARCH_arm64)
			#if defined(__APPLE__)
				fpcr_exceptions |= __fpcr_trap_overflow;
			#endif
		#endif
	}

	#if defined(DAEMON_ARCH_amd64) || defined(DAEMON_ARCH_i686)
		#if defined(__USE_GNU) || defined(__FreeBSD__)
			// https://www.gnu.org/savannah-checkouts/gnu/libc/manual/html_node/Control-Functions.html
			feenableexcept(exceptions);
		#elif defined(_WIN32)
			// https://learn.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-2012/c9676k6h(v=vs.110)
			unsigned int current = 0;
			_controlfp_s(&current, ~exceptions, _MCW_EM);
		#endif
	#endif

	#if defined(__APPLE__)
		// https://stackoverflow.com/a/15302624
		// https://stackoverflow.com/a/71792418
		fenv_t env;
		fegetenv( &env );

		#if defined(DAEMON_ARCH_amd64)
			env.__control &= ~exceptions;
			env.__mxcsr &= ~mxcsr_exceptions;
		#elif defined(DAEMON_ARCH_arm64)
			env.__fpcr |= fpcr_exceptions;
		#endif

		fesetenv( &env );

		/* The signal is on SIGFPE on amd64 and on SIGILL on arm64.
		We already have some sigaction() in src/common/System.cpp. */
	#endif

	// Superfluous on some systems, but always safe to do.
	#if defined(DAEMON_USE_ARCH_INTRINSICS_i686_sse)
		_MM_SET_EXCEPTION_MASK(_MM_GET_EXCEPTION_MASK() & ~mxcsr_exceptions);
	#endif
#endif
}

// Common code for fatal and non-fatal exit
// TODO: Handle shutdown requests coming from multiple threads (could happen from the *nix signal thread)
static void Shutdown(bool error, Str::StringRef message)
{
	FS::FlushAll();

	// Stop accepting commands from other instances
	CloseSingletonSocket();

	Application::Shutdown(error, message);

	if (PedanticShutdown()) {
		// could be interesting to see if there are some we forgot to close
		FS_CloseAllForOwner(FS::Owner::ENGINE);

		Cvar::Shutdown();
	}

	// Always run CON_Shutdown, because it restores the terminal to a usable state.
	CON_Shutdown();

	// Flush the logs one last time. Logs are turned off when OSExit is called.
	Log::FlushLogFile();
}

void Quit(Str::StringRef message)
{
	if (message.empty()) {
		Log::Notice("Quitting");
	} else {
		Log::Notice("Quitting: %s", message);
	}
	Shutdown(false, message);

	OSExit(0);
}

class QuitCmd : public Cmd::StaticCmd {
    public:
        QuitCmd(): StaticCmd("quit", Cmd::BASE, "quits the program") {
        }

        void Run(const Cmd::Args& args) const override {
            Quit(args.ConcatArgs(1));
        }
};
static QuitCmd QuitCmdRegistration;

void Error(Str::StringRef message)
{
	// Crash immediately in case of a recursive error
	static std::atomic_flag errorEntered;
	if (errorEntered.test_and_set())
		_exit(-1);

	Log::Warn(message);
	Shutdown(true, message);

	OSExit(1);
}

// Translate non-fatal signals into a quit command
#ifndef _WIN32
static void SignalHandler(int sig)
{
	// Abort if we still haven't exited 1 second after shutdown started
	static Util::optional<Sys::SteadyClock::time_point> abort_time;
	if (abort_time) {
		if (Sys::SteadyClock::now() < abort_time)
			return;
		_exit(255);
	}

	// Queue a quit command to be executed next frame
	Cmd::BufferCommandText("quit");

	// Sleep a bit, and wait for a signal again. If we still haven't shut down
	// by then, trigger an error.
	Sys::SleepFor(std::chrono::seconds(2));

	sigset_t sigset;
	sigemptyset(&sigset);
	for (int signal: {SIGTERM, SIGINT, SIGQUIT, SIGPIPE, SIGHUP})
		sigaddset(&sigset, signal);
	sigwait(&sigset, &sig);

	// Allow aborting shutdown if it gets stuck
	abort_time = Sys::SteadyClock::now() + std::chrono::seconds(1);
	pthread_sigmask(SIG_UNBLOCK, &sigset, nullptr);

	Sys::Error("Forcing shutdown from signal: %s", strsignal(sig));
}
static void SignalThread()
{
	// Unblock the signals we are interested in and handle them in this thread
	sigset_t sigset;
	sigemptyset(&sigset);
	struct sigaction sa;
	sa.sa_flags = 0;
	sa.sa_handler = SignalHandler;
	sigfillset(&sa.sa_mask);
	for (int sig: {SIGTERM, SIGINT, SIGQUIT, SIGPIPE, SIGHUP}) {
		sigaddset(&sigset, sig);
		sigaction(sig, &sa, nullptr);
	}
	pthread_sigmask(SIG_UNBLOCK, &sigset, nullptr);

	// Sleep indefinitely, all the work is done in the signal handler. We don't
	// use sigwait here because it interferes with gdb when debugging.
	while (true)
		sleep(1000000);
}
static void StartSignalThread()
{
	// Block all signals we are interested in. This will cause the signals to be
	// blocked in all threads since they inherit the signal mask.
	sigset_t sigset;
	sigemptyset(&sigset);
	for (int sig: {SIGTERM, SIGINT, SIGQUIT, SIGPIPE, SIGHUP})
		sigaddset(&sigset, sig);
	pthread_sigmask(SIG_BLOCK, &sigset, nullptr);

	// Start the signal thread
	try {
		std::thread(SignalThread).detach();
	} catch (std::system_error& err) {
		Sys::Error("Could not create signal handling thread: %s", err.what());
	}
}
#endif

// Command line arguments
struct cmdlineArgs_t {
	cmdlineArgs_t()
		: homePath(Application::GetTraits().defaultHomepath), libPath(FS::DefaultLibPath()),
		  reset_config(false), use_crash_handlers(true),
		  use_curses(Application::GetTraits().useCurses),
		  allowStartNewInstance(true), allowForwardToExistingInstance(true) {}

	std::string homePath;
	std::string libPath;
	std::vector<std::string> pakPaths;

	bool reset_config;
	bool use_crash_handlers;
	bool use_curses;
	bool allowStartNewInstance;
	bool allowForwardToExistingInstance;

	std::unordered_map<std::string, std::string> cvars;
	std::string commands;
};

// Parse the command line arguments
static void ParseCmdline(int argc, char** argv, cmdlineArgs_t& cmdlineArgs)
{
#ifdef __APPLE__
	// Ignore the -psn parameter added by OSX
	if (!strncmp(argv[argc - 1], "-psn", 4))
		argc--;
#endif

	bool foundCommands = false;
	for (int i = 1; i < argc; i++) {
		// A + indicate the start of a command that should be run on startup
		if (argv[i][0] == '+') {
			foundCommands = true;
			if (!cmdlineArgs.commands.empty())
				cmdlineArgs.commands.push_back('\n');
			cmdlineArgs.commands.append(argv[i] + 1);
			continue;
		}

		// Anything after a + is a parameter for that command
		if (foundCommands) {
			cmdlineArgs.commands.push_back(' ');
			cmdlineArgs.commands.append(Cmd::Escape(argv[i]));
			continue;
		}

		// If a URI is given, save it so it can later be transformed into a
		// /connect command. This only applies if no +commands have been given.
		// Any arguments after the URI are discarded.
		if (Str::IsIEqual("-connect", argv[i])) {
			if (argc < i + 2) {
				Log::Notice("Ignoring -connect parameter as no server was provided");
				break;
			}

			// Make it forwardable.
			cmdlineArgs.commands = "connect " + Cmd::Escape(argv[i + 1]);

			if (argc > i + 2) {
				// It is necessary to ignore following arguments because the command line may have
				// arbitrary unescaped text when the program is used as a protocol handler on Windows.
				Log::Warn("Ignoring extraneous arguments after -connect URI");
			}
			break;
		}

		// Variant that does not discard subsequent arguments. This can be used if the argument is sent
		// by our updater which can validate the inputs
		if (Str::IsIEqual("-connect-trusted", argv[i])) {
			if (argc < i + 2) {
				Log::Warn("Missing command line parameter for -connect-trusted");
				break;
			}

			cmdlineArgs.commands = "connect " + Cmd::Escape(argv[i + 1]);
			i++;
		}

		if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-help") || !strcmp(argv[i], "-h")) {
			printf("Usage: %s [-OPTION]... [%s+COMMAND...]\n", argv[0], Application::GetTraits().supportsUri ? "-connect <uri> | " : "");
			printf("\n"
				"Possible options are:\n"
				"  -h, -help                print this help and exit\n"
				"  -v, -version             print version and exit\n"
				"  -homepath <path>         set the path used for user-specific configuration files and downloaded dpk files\n"
				"  -libpath <path>          set the path containing additional executables and libraries\n"
				"  -pakpath <path>          add another path from which dpk files are loaded\n"
				"  -resetconfig             reset all cvars and keybindings to their default value\n"
				"  -noforward               do not forward commands to an existing existance; instead exit with error\n"
				"  -forward-only            just forward commands; exit with error if no existing instance\n"
#ifdef USE_CURSES
				"  -curses                  activate the curses interface\n"
#endif
				"  -nocrashhandler          disable catching SIGSEGV etc. (enable core dumps)\n"
				"  -set <variable> <value>  set the value of a cvar\n");
			printf("%s", Application::GetTraits().supportsUri ?
				"  -connect " URI_SCHEME "<address>[:<port>]>\n"
				"                           connect to server at startup\n" : "");
			printf(
				"  +<command> <args>        execute an ingame command after startup\n"
				"\n"
				"Order is important, -options must be set before +commands.\n"
				"Nothing is read and executed after -connect option and the following URI.\n"
				"If another instance is already running, commands will be forwarded to it.\n"
			);
			OSExit(0);
		} else if (!strcmp(argv[i], "--version") || !strcmp(argv[i], "-version") || !strcmp(argv[i], "-v")) {
			printf(PRODUCT_NAME " " PRODUCT_VERSION "\n");
			OSExit(0);
		} else if (!strcmp(argv[i], "-set")) {
			if (i >= argc - 2) {
				Log::Warn("Missing argument for -set");
				continue;
			}
			cmdlineArgs.cvars[argv[i + 1]] = argv[i + 2];
			i += 2;
		} else if (!strcmp(argv[i], "-pakpath")) {
			if (i == argc - 1) {
				Log::Warn("Missing argument for -pakpath");
				continue;
			}
			cmdlineArgs.pakPaths.push_back(argv[i + 1]);
			i++;
		} else if (!strcmp(argv[i], "-libpath")) {
			if (i == argc - 1) {
				Log::Warn("Missing argument for -libpath");
				continue;
			}
			cmdlineArgs.libPath = argv[i + 1];
			i++;
		} else if (!strcmp(argv[i], "-homepath")) {
			if (i == argc - 1) {
				Log::Warn("Missing argument for -homepath");
				continue;
			}
			cmdlineArgs.homePath = argv[i + 1];
			i++;
		} else if (!strcmp(argv[i], "-resetconfig")) {
			cmdlineArgs.reset_config = true;
		} else if (!strcmp(argv[i], "-noforward")) {
			cmdlineArgs.allowForwardToExistingInstance = false;
			cmdlineArgs.allowStartNewInstance = true;
		} else if (!strcmp(argv[i], "-forward-only")) {
			cmdlineArgs.allowForwardToExistingInstance = true;
			cmdlineArgs.allowStartNewInstance = false;
		}
		else if (!strcmp(argv[i], "-nocrashhandler")) {
			cmdlineArgs.use_crash_handlers = false;
		}
#ifdef USE_CURSES
		else if (!strcmp(argv[i], "-curses")) {
			cmdlineArgs.use_curses = true;
		}
#endif
		else {
			Log::Warn("Ignoring unrecognized parameter \"%s\"", argv[i]);
			continue;
		}
	}
}

// Apply a -set argument early, before the configuration files are loaded
static void EarlyCvar(Str::StringRef name, cmdlineArgs_t& cmdlineArgs)
{
	auto it = cmdlineArgs.cvars.find(name);
	if (it != cmdlineArgs.cvars.end())
		Cvar::SetValue(it->first, it->second);
}

static void SetCvarsWithInitFlag(cmdlineArgs_t& cmdlineArgs)
{
	for (auto it = cmdlineArgs.cvars.begin(); it != cmdlineArgs.cvars.end(); )
	{
		int flags;
		if (Cvar::GetFlags(it->first, flags) && flags & Cvar::INIT) {
			Cvar::SetValueForce(it->first, it->second);
			// Remove so that trying to set the cvar won't trigger a warning later. It doesn't
			// need to be set after running config files because config files can't change it.
			it = cmdlineArgs.cvars.erase(it);
		} else {
			++it;
		}
	}
}

// Initialize the engine
static void Init(int argc, char** argv)
{
	cmdlineArgs_t cmdlineArgs;

#ifdef _WIN32
	// Detect MSYS2 terminal. The AttachConsole code makes output not appear
	const char* msystem = getenv("MSYSTEM");
	if (!msystem || !Str::IsPrefix("MINGW", msystem)) {
		// If we were launched from a console, make our output visible on it
		if (AttachConsole(ATTACH_PARENT_PROCESS)) {
			(void)freopen("CONOUT$", "w", stdout);
			(void)freopen("CONOUT$", "w", stderr);
		}
	}
#endif

	// Print a banner and a copy of the command-line arguments
	Log::Notice(Q3_VERSION " " PLATFORM_STRING " " XSTRING(ARCH_STRING) " (" XSTRING(DAEMON_CXX_COMPILER_STRING) ") " __DATE__);

	std::string argsString = "cmdline:";
	for (int i = 1; i < argc; i++) {
		argsString.push_back(' ');
		argsString.append(argv[i]);
	}
	Log::Notice(argsString);

	Sys::ParseCmdline(argc, argv, cmdlineArgs);

	if (cmdlineArgs.use_crash_handlers) {
		Sys::SetupCrashHandler(); // If Breakpad is enabled, this handler will soon be replaced.
	}

	// Platform-specific initialization
#ifdef _WIN32
	// Don't let SDL set the timer resolution. We do that manually in Sys::Sleep.
	SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "0");
#else
	// Translate non-fatal signals to a quit command
	Sys::StartSignalThread();

	// Force a UTF-8 locale for LC_CTYPE so that terminals can output unicode
	// characters. We keep all other locale facets as "C".
	if (!setlocale(LC_CTYPE, "C.UTF-8") || !setlocale(LC_CTYPE, "UTF-8") || !setlocale(LC_CTYPE, "en_US.UTF-8")) {
		// Try using the user's locale with UTF-8
		const char* locale = setlocale(LC_CTYPE, "");
		if (!locale) {
			setlocale(LC_CTYPE, "C");
			Log::Warn("Could not set locale to UTF-8, unicode characters may not display correctly");
		} else {
			std::string locale2 = locale;
			if (!Str::IsSuffix(".UTF-8", locale2)) {
				size_t dot = locale2.rfind('.');
				if (dot != std::string::npos)
					locale2.replace(dot, locale2.size() - dot, ".UTF-8");
				if (!setlocale(LC_CTYPE, locale2.c_str())) {
					setlocale(LC_CTYPE, "C");
					Log::Warn("Could not set locale to UTF-8, unicode characters may not display correctly");
				}
			}
		}
	}
#endif

	// Initialize the console
	if (cmdlineArgs.use_curses)
		CON_Init();
	else
		CON_Init_TTY();

	// Set cvars set from the command line having the Cvar::INIT flag
	SetCvarsWithInitFlag(cmdlineArgs);

	SetFloatingPointExceptions();

	// Initialize the filesystem. For pakpaths, the libpath is added first and has the
	// lowest priority, while the homepath is added last and has the highest.

	/* The default pakpath is <libpath>/pkg, but we load <libpath>/../pkg instead
	if <libpath>/pkg doesn't exist but <libpath>/../pkg exists.
	This is to make sure extracting the engine archive in a subfolder works.
	Many file browsers create a parent directory when there is no directory in an archive. */
	std::string defaultPakPath = FS::Path::Build(cmdlineArgs.libPath, "pkg");

	std::error_code missing;
	FS::RawPath::ListFiles(defaultPakPath, missing);
	if (missing) {
		std::string parentPakPath = FS::Path::Build(cmdlineArgs.libPath, FS::Path::Build("..", "pkg"));
		FS::RawPath::ListFiles(parentPakPath, missing);
		if (!missing) {
			defaultPakPath = parentPakPath;
		}
	}

	cmdlineArgs.pakPaths.insert(cmdlineArgs.pakPaths.begin(), defaultPakPath);

	cmdlineArgs.pakPaths.push_back(FS::Path::Build(cmdlineArgs.homePath, "pkg"));
	EarlyCvar("fs_legacypaks", cmdlineArgs);
	FS::Initialize(cmdlineArgs.homePath, cmdlineArgs.libPath, cmdlineArgs.pakPaths);

	// Look for an existing instance of the engine running on the same homepath.
	// If there is one, forward any +commands to it and exit.
	singletonSocketPath = GetSingletonSocketPath();
	if (ConnectSingletonSocket()) {
		Log::Notice("Existing instance found");
		if (cmdlineArgs.allowForwardToExistingInstance) {
			if (!cmdlineArgs.commands.empty()) {
				Log::Notice("Forwarding commands to existing instance");
				WriteSingletonSocket(cmdlineArgs.commands);
			} else {
				Log::Notice("No commands given, exiting...");
			}
		}
#ifdef _WIN32
		CloseHandle(singletonSocket);
#else
		close(singletonSocket);
#endif
		CON_Shutdown();
		OSExit(cmdlineArgs.allowForwardToExistingInstance ? 0 : 1);
	} else if (!cmdlineArgs.allowStartNewInstance) {
		Log::Notice("Command forwarding requested, but no existing instance found");
		CON_Shutdown();
		OSExit(1);
	}

	// Create the singleton socket
	CreateSingletonSocket();

	// At this point we can safely open the log file since there are no existing
	// instances running on this homepath.
	EarlyCvar("logs.logFile.active", cmdlineArgs);
	Log::OpenLogFile();

	if (CreateCrashDumpPath() && cmdlineArgs.use_crash_handlers) {
		// This may fork(), and then exec() *in the parent process*,
		// so threads must not be created before this point.
		BreakpadInit();
	}

	// Start a thread which reads commands from the singleton socket
	try {
		std::thread(ReadSingletonSocket).detach();
	} catch (std::system_error& err) {
		Sys::Error("Could not create singleton socket thread: %s", err.what());
	}

	// Load the base paks
	// TODO: cvar names and FS_* stuff needs to be properly integrated
	EarlyCvar("fs_basepak", cmdlineArgs);
	EarlyCvar("fs_extrapaks", cmdlineArgs);
	EarlyCvar("fs_pakprefixes", cmdlineArgs);

    Application::LoadInitialConfig(cmdlineArgs.reset_config);
	Cmd::ExecuteCommandBuffer();

	// Override any cvars set in configuration files with those on the command-line
	for (auto& cvar: cmdlineArgs.cvars)
		Cvar::SetValue(cvar.first, cvar.second);

	// Load the console history
	Console::History::Load();

	// Legacy initialization code, needs to be replaced
	// TODO: eventually move all of Com_Init into here

	Application::Initialize();

	// Buffer the commands that were specified on the command line so they are
	// executed in the first frame.
	Cmd::BufferCommandText(cmdlineArgs.commands);
}

} // namespace Sys

#ifdef __MINGW64__
// https://www.kb.cert.org/vuls/id/307144
__declspec(dllexport) void DummyPreventingLinkerFromBreakingASLR() {}
#endif

// Program entry point. On Windows, main is #defined to SDL_main which is invoked by SDLmain.
// This is why ALIGN_STACK_FOR_MINGW is needed (normally gcc would generate alignment code in main()).
ALIGN_STACK_FOR_MINGW int main(int argc, char** argv)
{
	// Initialize the engine. Any errors here are fatal.
	try {
		Sys::Init(argc, argv);
	} catch (Sys::DropErr& err) {
		Sys::Error("Error during initialization: %s", err.what());
	} catch (std::exception& err) {
		Sys::Error("Unhandled exception (%s): %s", typeid(err).name(), err.what());
	} catch (...) {
		Sys::Error("Unhandled exception of unknown type");
	}

	// Run the engine frame in a loop. First try to handle an error by returning
	// to the menu, but make the error fatal if the shutdown code fails.
	try {
		while (true) {
			try {
				Application::Frame();
			} catch (Sys::DropErr& err) {
				if (err.is_error()) {
					Log::Warn(err.what());
				} else {
					Log::Notice(err.what());
				}
				Application::OnDrop(err.is_error(), err.what());

				if (Sys::cvar_common_shutdownOnDrop.Get()) {
					/* Sys::Quit and Sys::Error are not reused because here,
					quitting is never an error even if an error happened and
					even if we still want to return an error code.

					Also, the error message is already displayed as an error
					message and the message we pass to Shutdown is not that
					error message. */

					/* False because quitting is not an error, do not display
					the error window requiring manual action to complete the
					shutdown. */
					Sys::Shutdown(false, "Quitting: shut down engine on game drop");

					/* Return an error code if there was an error anyway, this
					can be used for scripting where a calling script runs the
					engine with a game with explicit option to shut down engine
					on game drop but wants to know if game dropped with an error
					or not. */
					Sys::OSExit(err.is_error() ? 1 : 0);
				}
			}
		}
	} catch (Sys::DropErr& err) {
		Sys::Error("Error during error handling: %s", err.what());
	} catch (std::exception& err) {
		Sys::Error("Unhandled exception (%s): %s", typeid(err).name(), err.what());
	} catch (...) {
		Sys::Error("Unhandled exception of unknown type");
	}

	ASSERT_UNREACHABLE();
}
