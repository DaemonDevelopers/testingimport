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

#include "Common.h"

#include "engine/qcommon/qcommon.h"

namespace Cmd {

    std::string Escape(Str::StringRef text) {
        if (text.empty()) {
            return "\"\"";
        }

        bool escaped = false;
        bool maybeComment = false;
        std::string res = "\"";

        for (char c: text) {
            if (c == '\\') {
                res.append("\\\\");
            } else if (c == '\"') {
                res.append("\\\"");
            } else if (c == '$') {
                res.append("\\$");
            } else if (c == '\n') {
                // It's impossible to quote this
                Log::Warn("Truncating command with newline");
                break;
            } else {
                if (Str::cisspace(c) || c == ';') {
                    escaped = true;
                }
                res.push_back(c);
            }

            if (maybeComment && (c == '/' || c == '*')) {
                escaped = true;
                maybeComment = false;
            } else if (c == '/') {
                maybeComment = true;
            } else {
                maybeComment = false;
            }
        }

        if (escaped) {
            res += "\"";
        } else {
            res.erase(0, 1);
        }

        return res;
    }

    static bool SkipSpaces(const char*& in, const char* end)
    {
        bool foundSpace = false;
        while (in != end) {
            // Handle spaces
            if (Str::cisspace(in[0])) {
                foundSpace = true;
                in++;
            } else if (in + 1 != end) {
                // Handle // comments
                if (in[0] == '/' && in[1] == '/') {
                    in = end;
                    return true;
                }
                // Handle /* */ comments
                if (in[0] == '/' && in[1] == '*') {
                    foundSpace = true;
                    in += 2;
                    while (in != end) {
                        if (in + 1 != end && in[0] == '*' && in[1] == '/') {
                            in += 2;
                            break;
                        } else {
                            in++;
                        }
                    }
                } else {
                    // Non-space character
                    return foundSpace;
                }
            } else {
                // Non-space character
                return foundSpace;
            }
        }
        return true;
    }

    const char* SplitCommand(const char* start, const char* end) {
        bool inQuote = false;
        bool inComment = false;
        bool inInlineComment = false;

        while (start != end) {
            //End of comment
            if (inInlineComment && start + 1 != end && start[0] == '*' && start[1] == '/') {
                inInlineComment = false;
                start += 2;
                continue;
            }

            //Ignore everything else in an inline comment
            if (inInlineComment) {
                start++;
                continue;
            }

            //Start of comment
            if (not inQuote && not inComment && start + 1 != end && start[0] == '/' && start[1] == '*') {
                inInlineComment = true;
                start += 2;
                continue;
            }
            if (not inQuote && start + 1 != end && start[0] == '/' && start[1] == '/') {
                inComment = true;
                start += 2;
                continue;
            }

            //Escaped character
            if (start + 1 != end && start[0] == '\\') {
                start += 2;
                continue;
            }

            //Quote
            if (start[0] == '"') {
                inQuote = !inQuote;
                start++;
                continue;
            }

            //Semicolon
            if (start[0] == ';' && !inQuote && !inComment) {
                return start + 1;
            }

            //Newline
            if (start[0] == '\n') {
                return start + 1;
            }

            //Normal character
            start++;
        }

        return end;
    }

    // Special escape function for cvar values
    static void EscapeCvarValue(std::string& text, bool inQuote) {
        for (auto it = text.begin(); it != text.end(); ++it) {
            if (*it == '\\')
                it = text.insert(it, '\\') + 1;
            else if (*it == '\"')
                it = text.insert(it, '\\') + 1;
            else if (*it == '$')
                it = text.insert(it, '\\') + 1;
            else if (!inQuote && *it == '/')
                it = text.insert(it, '\\') + 1;
            else if (!inQuote && *it == ';')
                it = text.insert(it, '\\') + 1;
        }
    }

    std::string SubstituteCvars(Str::StringRef text) {
        std::string out = text;
        auto in = out.begin();
        auto end = out.end();
        bool inQuote = false;
        bool inInlineComment = false;

        while (in != end) {
            //End of comment
            if (inInlineComment && in + 1 != end && in[0] == '*' && in[1] == '/') {
                inInlineComment = false;
                in += 2;
                continue;
            }

            //Ignore everything else in an inline comment
            if (inInlineComment) {
                in++;
                continue;
            }

            //Start of comment
            if (not inQuote && in + 1 != end && in[0] == '/' && in[1] == '*') {
                inInlineComment = true;
                in += 2;
                continue;
            }

            //Escaped character
            if (in + 1 != end && in[0] == '\\') {
                in += 2;
                continue;
            }

            //Quote
            if (in[0] == '"') {
                inQuote = !inQuote;
                in++;
                continue;
            }

            // Start of cvar substitution block
            if (in[0] == '$') {
                // Find the end of the block, only allow valid cvar name characters
                auto cvarStart = ++in;
                while (in[0] == '_' || in[0] == '.' ||
                       (in[0] >= 'a' && in[0] <= 'z') ||
                       (in[0] >= 'A' && in[0] <= 'Z') ||
                       (in[0] >= '0' && in[0] <= '9')) {
                    in++;
                }

                // If the block is not terminated properly, ignore it
                if (in[0] == '$') {
                    std::string cvarName(cvarStart, in);
                    std::string cvarValue = Cvar::GetValue(cvarName);
                    EscapeCvarValue(cvarValue, inQuote);

                    // Iterators get invalidated by replace
                    size_t stringPos = cvarStart - 1 - out.begin() + cvarValue.size();
                    out.replace(cvarStart - 1, in + 1, cvarValue);
                    in = out.begin() + stringPos;
                    end = out.end();
                }
                continue;
            }

            //Normal character
            in++;
        }

        return out;
    }

    bool IsValidCvarName(Str::StringRef text)
    {
        for (char c: text) {
            if (c >= 'a' && c <= 'z')
                continue;
            if (c >= 'A' && c <= 'Z')
                continue;
            if (c >= '0' && c <= '9')
                continue;
            if (c == '_' || c == '.')
                continue;
            return false;
        }
        return true;
    }

    bool IsValidCmdName(Str::StringRef text)
    {
        bool firstChar = true;
        for (char c: text) {
            // Allow command names starting with +/-
            if (firstChar && (c == '+' || c == '-')) {
                firstChar = false;
                continue;
            }
            firstChar = false;
            if (c >= 'a' && c <= 'z')
                continue;
            if (c >= 'A' && c <= 'Z')
                continue;
            if (c >= '0' && c <= '9')
                continue;
            if (c == '_' || c == '.')
                continue;
            return false;
        }
        return true;
    }

    bool IsSwitch(Str::StringRef arg, const char *name)
    {
        return Str::LongestPrefixSize(ToLower(arg), name) > 1;
    }

    /*
    ===============================================================================

    Cmd::CmdArgs

    ===============================================================================
    */

    Args::Args() = default;

    Args::Args(std::vector<std::string> args_)
	    : args(std::move(args_)) {
    }

    Args::Args(Str::StringRef cmd) {
        const char* in = cmd.begin();

        //Skip leading whitespace
        SkipSpaces(in, cmd.end());

        //Return if the cmd is empty
        if (in == cmd.end())
            return;

        //Build arg tokens
        std::string currentToken;
        while (true) {
            //Check for end of current token
            if (SkipSpaces(in, cmd.end())) {
                 args.push_back(std::move(currentToken));
                 if (in == cmd.end())
                    return;
                 currentToken.clear();
            }

            //Handle quoted strings
            if (in[0] == '\"') {
                in++;
                //Read all characters until quote end
                while (in != cmd.end()) {
                    if (in[0] == '\"') {
                        in++;
                        break;
                    } else if (in + 1 !=  cmd.end() && in[0] == '\\') {
                        currentToken.push_back(in[1]);
                        in += 2;
                    } else {
                        currentToken.push_back(in[0]);
                        in++;
                    }
                }
            } else {
                if (in + 1 !=  cmd.end() && in[0] == '\\') {
                    currentToken.push_back(in[1]);
                    in += 2;
                } else {
                    currentToken.push_back(in[0]);
                    in++;
                }
            }
        }
    }

    int Args::Argc() const {
        return args.size();
    }

    const std::string& Args::Argv(int argNum) const {
        // Cmd_Argv() returns an empty string for an out of bounds index.
        // Try to catch porting issues with an assert.
        ASSERT_LT(static_cast<size_t>(argNum), args.size());
        return args[argNum];
    }

    std::string Args::EscapedArgs(int start, int end) const {
        std::string res;

        if (end < 0) {
            end = args.size() - 1;
        }

        for (int i = start; i < end + 1; i++) {
            if (i != start) {
                res += " ";
            }
            res += Escape(args[i]);
        }

        return res;
    }

    std::string Args::ConcatArgs(int start, int end) const {
        std::string res;

        if (end < 0) {
            end = args.size() - 1;
        }

        for (int i = start; i < end + 1; i++) {
            if (i != start) {
                res += " ";
            }
            res += args[i];
        }

        return res;
    }

    const std::vector<std::string>& Args::ArgVector() const {
        return args;
    }

    int Args::size() const {
        return Argc();
    }

    const std::string& Args::operator[] (int argNum) const {
        return Argv(argNum);
    }

    std::vector<std::string>::const_iterator Args::begin() const {
        return args.cbegin();
    }

    std::vector<std::string>::const_iterator Args::end() const {
        return args.cend();
    }

    /*
    ===============================================================================

    Cmd::CmdBase

    ===============================================================================
    */

    CmdBase::CmdBase(const int flags): flags(flags) {
    }

    CompletionResult CmdBase::Complete(int argNum, const Args& args, Str::StringRef prefix) const {
        Q_UNUSED(argNum);
        Q_UNUSED(args);
        Q_UNUSED(prefix);
        return {};
    }

    void CmdBase::PrintUsage(const Args& args, Str::StringRef syntax, Str::StringRef description) const {
        if(description.empty()) {
            Print("%s: %s %s", "usage", args.Argv(0).c_str(), syntax.c_str());
        } else {
            Print("%s: %s %s — %s", "usage", args.Argv(0).c_str(), syntax.c_str(), description.c_str());
        }
    }

    int CmdBase::GetFlags() const {
        return flags;
    }

    void CmdBase::ExecuteAfter(Str::StringRef text, bool parseCvars) const {
        GetEnv().ExecuteAfter(text, parseCvars);
    }

    CompletionResult FilterCompletion(Str::StringRef prefix, std::initializer_list<CompletionItem> list) {
        CompletionResult res;
        AddToCompletion(res, prefix, list);
        return res;
    }

    void AddToCompletion(CompletionResult& res, Str::StringRef prefix, std::initializer_list<CompletionItem> list) {
        for (const auto& item: list) {
            if (Str::IsIPrefix(prefix, item.first)) {
                res.push_back({item.first, item.second});
            }
        }
    }

    Environment& CmdBase::GetEnv() const {
        return *Cmd::GetEnv();
    }

#ifndef BUILD_ENGINE
    StaticCmd::StaticCmd(std::string name, std::string description)
    :CmdBase(0){
        //Register this command statically
        AddCommand(std::move(name), *this, std::move(description));
    }
#endif

    StaticCmd::StaticCmd(std::string name, const int flags, std::string description)
    :CmdBase(flags){
        //Register this command statically
        AddCommand(std::move(name), *this, std::move(description));
    }

    CompletionResult NoopComplete(int, const Args&, Str::StringRef) {
        return {};
    }

#ifndef BUILD_ENGINE
    LambdaCmd::LambdaCmd(std::string name, std::string description, RunFn run, CompleteFn complete)
    :StaticCmd(std::move(name), std::move(description)), run(run), complete(complete) {
    }
#endif
    LambdaCmd::LambdaCmd(std::string name, int flags, std::string description, RunFn run, CompleteFn complete)
    :StaticCmd(std::move(name), flags, std::move(description)), run(run), complete(complete) {
    }

    void LambdaCmd::Run(const Args& args) const {
        run(args);
    }
    CompletionResult LambdaCmd::Complete(int argNum, const Args& args, Str::StringRef prefix) const {
        return complete(argNum, args, prefix);
    }
}

class InjectFaultCmd : public Cmd::StaticCmd
{
public:
    InjectFaultCmd() : StaticCmd(
        VM_STRING_PREFIX "injectFault", Cmd::BASE, "make the program error and crash") {}

    void Run(const Cmd::Args& args) const override
    {
        bool useThread = Str::IsIEqual(args.ArgVector().back(), "thread");
        if (useThread) {
            std::thread(&DoFault, Cmd::Args(std::vector<std::string>(args.begin(), args.end() - 1)))
                .detach();
        } else {
            if (!DoFault(args)) {
                Print("Usage: %s (drop | error | segfault | intdiv | floatinvalid | floatdiv | floatoverflow"
                      " | unreachable | exception | throw | terminate | abort | freeze <seconds>) [thread]",
                      args.Argv(0));
            }
        }
    }

private:
    static bool DoFault(const Cmd::Args& args)
    {
        float freezeDuration;
        if (args.Argc() == 3 && Str::IsIEqual(args.Argv(1), "freeze") &&
                Cvar::ParseCvarValue(args.Argv(2), freezeDuration)) {
            int start = Sys::Milliseconds();
            while (Sys::Milliseconds() < start + freezeDuration * 1000.0f) {} // spin cpu
            return true;
        } else if (args.Argc() != 2) {
            return false;
        }

        std::string how = Str::ToLower(args.Argv(1));
        if (how == "drop") {
            Sys::Drop("drop from injectFault command");
        } else if (how == "error") {
            Sys::Error("error from injectFault command");
        } else if (how == "segfault") {
            volatile auto p = (volatile char*)1;
            *p = 'x';
        } else if (how == "intdiv") {
            volatile int n = 0;
            n = n / n;
		} else if (how == "floatinvalid") {
			volatile float f = -1.0f;
			f = sqrt(f);
        } else if (how == "floatdiv") {
            volatile float f = 0;
            f = 1 / f;
		} else if (how == "floatoverflow") {
			volatile float f = std::numeric_limits<float>::max();
			f = 2 * f;
        } else if (how == "unreachable") { // May print the usage string :)
            UNREACHABLE();
        } else if (how == "exception") { // std::exception
            std::vector<int> v;
            (void) v.at(0);
        } else if (how == "throw") { // other exception
            throw 2;
        } else if (how == "terminate") { // std::terminate without an exception
            std::thread t([]{});
        } else if (how == "abort") {
            std::abort();
        } else {
            return false;
        }
        return true;
    }
};
static InjectFaultCmd injectFaultRegistration;
