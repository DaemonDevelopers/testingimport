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

#include "CvarSystem.h"
#include "CommandSystem.h"

#include "qcommon/qcommon.h"
#include "qcommon/cvar.h"

//TODO: thread safety (not possible with the C API that doesn't care at all about this)

int cvar_modifiedFlags;

namespace Cvar {

    // A cvar is some info alongside a potential proxy for the cvar
    struct cvarRecord_t {
        std::string value;
        std::string resetValue;
        Util::optional<std::string> latchedValue;
        int flags;
        std::string description;
        CvarProxy* proxy;
        cvar_t ccvar; // The state of the cvar_t used to emulate the C API
        //DO: mutex?

        inline bool IsArchived() const {
            return (flags & USER_ARCHIVE) && !(flags & TEMPORARY);
        }
    };

    //Functions that emulate the C API
    void SetCStyleDescription(cvarRecord_t& cvar) {
        if (cvar.proxy) {
            return;
        }

        if (cvar.flags & CVAR_LATCH and cvar.ccvar.latchedString) {
            cvar.description = Str::Format("\"%s^*\" - latched value \"%s^*\"", cvar.ccvar.string, cvar.ccvar.latchedString);
        } else {
            cvar.description = Str::Format("\"%s^*\"", cvar.value);
        }

        Cmd::ChangeDescription(cvar.ccvar.name, cvar.description);
    }

    void SetCCvar(cvarRecord_t& cvar) {
        cvar_t& var = cvar.ccvar;
        bool modified = false;

        if (cvar.flags & CVAR_LATCH) {
            if (not var.string) {
                var.string = CopyString(cvar.value.c_str());
                modified = true;
            } else {
                if (Q_stricmp(var.string, cvar.value.c_str())) {
                    Log::Notice("The change to %s will take effect after restart.", var.name);
                    if (var.latchedString) Z_Free(var.latchedString);
                    var.latchedString = CopyString(cvar.value.c_str());
                    modified = true;
                } else if (var.latchedString) {
                    Z_Free(var.latchedString);
                    var.latchedString = nullptr;
                }
            }
        } else {
            if (var.string) {
                if (Q_stricmp(var.string, cvar.value.c_str())) {
                    modified = true;
                    Z_Free(var.string);
                    var.string = CopyString(cvar.value.c_str());
                }
            } else {
                var.string = CopyString(cvar.value.c_str());
                modified = true;
            }
        }

        var.value = atof(var.string);
        var.integer = atoi(var.string);

        if (modified) {
            cvar_modifiedFlags |= var.flags;
        }
        SetCStyleDescription(cvar);
    }

    void GetCCvar(const std::string& name, cvarRecord_t& cvar) {
        cvar_t& var = cvar.ccvar;
        bool modified = false;

        if (not var.name) {
            var.name = CopyString(name.c_str());
        }

        if (var.resetString) Z_Free(var.resetString);
        var.resetString = CopyString(cvar.resetValue.c_str());

        var.flags = cvar.flags;

        if (cvar.flags & CVAR_LATCH and var.latchedString) {
            if (var.string) Z_Free(var.string);
            var.string = var.latchedString;
            var.latchedString = nullptr;
            modified = true;
        } else {
            if (var.string) {
                if (Q_stricmp(var.string, cvar.value.c_str())) {
                    modified = true;
                    Z_Free(var.string);
                    var.string = CopyString(cvar.value.c_str());
                }
            } else {
                var.string = CopyString(cvar.value.c_str());
                modified = true;
            }
        }

        var.value = atof(var.string);
        var.integer = atoi(var.string);

        if (modified) {
            cvar_modifiedFlags |= var.flags;
        }
        SetCStyleDescription(cvar);
    }

    using CvarMap = std::unordered_map<std::string, cvarRecord_t*, Str::IHash, Str::IEqual>;
    static bool cheatsAllowed = true;

    // The order in which static global variables are initialized is undefined and cvar
    // can be registered before main. The first time this function is called the cvar map
    // is initialized so we are sure it is initialized as soon as we need it.
    CvarMap& GetCvarMap() {
        static CvarMap cvars;
        return cvars;
    }

	void Shutdown() {
		CvarMap &cvars = GetCvarMap();

		for (const auto &it: cvars) {
			cvarRecord_t *cvar = it.second;
			cvar_t &ccvar = cvar->ccvar;

			if (ccvar.name)
			{
				Z_Free(ccvar.name);
			}

			if (ccvar.resetString)
			{
				Z_Free(ccvar.resetString);
			}

			if (ccvar.latchedString)
			{
				Z_Free(ccvar.latchedString);
			}

			if (ccvar.string)
			{
				Z_Free(ccvar.string);
			}

			delete cvar;
		}

		cvars.clear();
	}

    // A command created for each cvar, used for /<cvar>
    class CvarCommand : public Cmd::CmdBase {
        public:
            CvarCommand() : Cmd::CmdBase(Cmd::CVAR) {
            }

            void Run(const Cmd::Args& args) const override {
                CvarMap& cvars = GetCvarMap();
                const std::string& name = args.Argv(0);
                cvarRecord_t* var = cvars[name];

                if (args.Argc() < 2) {
                    Print("\"%s\" - %s ^*- default: \"%s^*\"", name, var->description, var->resetValue);
                } else {
                    //TODO forward the print part of the environment
                    SetValue(name, args.Argv(1));
                }
            }
    };
    static CvarCommand cvarCommand;

    void ChangeCvarDescription(Str::StringRef cvarName, cvarRecord_t* cvar, Str::StringRef description) {
        std::string realDescription = Str::Format("\"%s^*\" - %s", cvar->value, description);
        Cmd::ChangeDescription(cvarName, "cvar - " + realDescription);
        cvar->description = std::move(realDescription);
    }

    void InternalSetValue(const std::string& cvarName, std::string value, int flags, bool rom, bool warnRom) {
        CvarMap& cvars = GetCvarMap();

        auto it = cvars.find(cvarName);
        //TODO: rom means the cvar should have been created before?
        if (it == cvars.end()) {
            if (!Cmd::IsValidCvarName(cvarName)) {
                Log::Notice("Invalid cvar name '%s'", cvarName.c_str());
                return;
            }

            //The user creates a new cvar through a command.
            cvars[cvarName] = new cvarRecord_t{value, value, Util::nullopt, flags | CVAR_USER_CREATED, "user created", nullptr, {}};
            Cmd::AddCommand(cvarName, cvarCommand, "cvar - user created");
            GetCCvar(cvarName, *cvars[cvarName]);

        } else {
            cvarRecord_t* cvar = it->second;

            if (not (cvar->flags & CVAR_USER_CREATED)) {
                if (cvar->flags & CVAR_ROM and not rom) {
                    Log::Notice("%s is read only.", cvarName.c_str());
                    return;
                }

                if (cvar->flags & INIT and not rom) {
                    Log::Notice("%s can only be set at program initalization.", cvarName);
                    return;
                }

                if (rom and warnRom and not (cvar->flags & (CVAR_ROM | INIT))) {
                    Log::Notice("SetValueForce called on non-ROM cvar '%s'", cvarName.c_str());
                }

                if (not cheatsAllowed && cvar->flags & CHEAT) {
                    Log::Notice("%s is cheat-protected.", cvarName.c_str());
                    return;
                }
            }

            cvar->flags |= flags;

            // mark for archival if flagged as archive-on-change
            if (cvar->flags & ARCHIVE) {
                cvar->flags |= USER_ARCHIVE;
            }

            if (cvar->proxy) {
                //Tell the cvar proxy about the new value
                OnValueChangedResult result = cvar->proxy->OnValueChanged(value);

                if (result.success) {
                    if (cvar->flags & INTERNAL_LATCH && value != cvar->value) {
                        ChangeCvarDescription(cvarName, cvar, Str::Format("%s - latched value \"%s^*\"", result.description, value));
                        OnValueChangedResult undo = cvar->proxy->OnValueChanged(cvar->value);
                        if (!undo.success) {
                            Sys::Error("error testing new value for latched cvar %s", cvarName);
                        }
                        Log::Notice("The change to %s will take effect after restart.", cvarName);
                        cvar->latchedValue = value;
                        return;
                    }
                    cvar->latchedValue = Util::nullopt;
                    cvar->value = std::move(value);
                    ChangeCvarDescription(cvarName, cvar, result.description);
                } else {
                    Log::Notice("Value '%s^*' is not valid for cvar %s: %s", value, cvarName, result.description);
                    return;
                }
            } else {
                cvar->value = std::move(value);
            }
            SetCCvar(*cvar);
        }

    }

    // Simple proxies for InternalSetValue
    void SetValue(const std::string& cvarName, const std::string& value) {
        InternalSetValue(cvarName, value, 0, false, true);
    }

    void SetValueForce(const std::string& cvarName, const std::string& value) {
        InternalSetValue(cvarName, value, 0, true, true);
    }

    std::string GetValue(const std::string& cvarName) {
        CvarMap& cvars = GetCvarMap();
        std::string result = "";

        auto iter = cvars.find(cvarName);
        if (iter != cvars.end()) {
            result = iter->second->value;
        }

        return result;
    }

    bool Register(CvarProxy* proxy, const std::string& name, std::string description, int flags, const std::string& defaultValue) {
        CvarMap& cvars = GetCvarMap();
        cvarRecord_t* cvar;

        auto it = cvars.find(name);
        if (it == cvars.end()) {
            if (!Cmd::IsValidCvarName(name)) {
                Log::Notice("Invalid cvar name '%s'", name.c_str());
                return false;
            }

            //Create the cvar and parse its default value
            description = Str::Format("\"%s^*\" - %s", defaultValue, description);
            cvar = new cvarRecord_t{defaultValue, defaultValue, Util::nullopt, flags, description, proxy, {}};
            cvars[name] = cvar;

            Cmd::AddCommand(name, cvarCommand, "cvar - " + description);

        } else {
            cvar = it->second;

            if (proxy && cvar->proxy) {
                Log::Warn("Cvar %s cannot be registered twice", name.c_str());
                return false;
            }

            // Register the cvar with the previous user_created value
            cvar->flags &= ~CVAR_USER_CREATED;
            cvar->flags |= flags;
            cvar->proxy = proxy;
            if ((flags & ROM) || (!cheatsAllowed && (flags & CHEAT))) {
                cvar->value = defaultValue;
            }

            cvar->resetValue = defaultValue;
            cvar->description.clear();

            if (proxy) { //TODO replace me with an assert once we do not need to support the C API
                OnValueChangedResult result = proxy->OnValueChanged(cvar->value);
                if (result.success) {
                    ChangeCvarDescription(name, cvar, result.description);
                } else {
                    OnValueChangedResult defaultValueResult = proxy->OnValueChanged(defaultValue);
                    if (defaultValueResult.success) {
                        ChangeCvarDescription(name, cvar, result.description);
                    } else {
                        Log::Notice("Default value '%s' is not correct for cvar '%s': %s",
                                defaultValue.c_str(), name.c_str(), defaultValueResult.description.c_str());
                    }
                }
            }
        }
        GetCCvar(name, *cvar);
        return true;
    }

    void Unregister(const std::string& cvarName) {
        CvarMap& cvars = GetCvarMap();

        auto it = cvars.find(cvarName);
        if (it != cvars.end()) {
            cvarRecord_t* cvar = it->second;

            cvar->proxy = nullptr;
            cvar->flags |= CVAR_USER_CREATED;
        } //TODO else what?
    }

    Cmd::CompletionResult Complete(Str::StringRef prefix) {
        CvarMap& cvars = GetCvarMap();

        Cmd::CompletionResult res;
        for (const auto& entry : cvars) {
            if (Str::IsIPrefix(prefix, entry.first)) {
                res.emplace_back(entry.first, entry.second->description);
            }
        }

        Cmd::NamespaceFold(res);
        return res;
    }

    bool AddFlags(const std::string& cvarName, int flags) {
        CvarMap& cvars = GetCvarMap();

        auto it = cvars.find(cvarName);
        if (it != cvars.end()) {
            cvarRecord_t* cvar = it->second;

            // User error. Possibly coder error too, but unlikely
            if ((cvar->flags & TEMPORARY) && (flags & (ARCHIVE | USER_ARCHIVE)))
            {
                Log::Notice("Cvar '%s' is temporary and will not be archived", cvarName.c_str());
                flags &= ~(ARCHIVE | USER_ARCHIVE);
            }

            cvar->flags |= flags;

            //TODO: remove it, overkill ?
            //Make sure to trigger the event as if this variable was changed
            cvar_modifiedFlags |= flags;
            return true; // success
        } //TODO else what?

        return false; // not found
    }

    bool GetFlags(const std::string& cvarName, int& flags) {
        CvarMap& cvars = GetCvarMap();

        auto it = cvars.find(cvarName);
        if (it == cvars.end()) {
            return false;
        }

        flags = it->second->flags;
        return true;
    }

    bool ClearFlags(const std::string& cvarName, int flags) {
        CvarMap& cvars = GetCvarMap();

        auto it = cvars.find(cvarName);
        if (it != cvars.end()) {
            cvarRecord_t* cvar = it->second;
            cvar->flags &= ~flags;

            //TODO: remove it, overkill ?
            //Make sure to trigger the event as if this variable was changed
            cvar_modifiedFlags |= flags;
            return true; // success
        } //TODO else what?

        return false; // not found
    }

    void SetCheatsAllowed(bool allowed) {
        cheatsAllowed = allowed;

        if (cheatsAllowed) {
            return;
        }

        CvarMap& cvars = GetCvarMap();

        // Reset all the CHEAT cvars to their default value
        for (auto& entry : cvars) {
            cvarRecord_t* cvar = entry.second;

            if (cvar->flags & CHEAT && cvar->value != cvar->resetValue) {
                cvar->value = cvar->resetValue;
                SetCCvar(*cvar);

                if (cvar->proxy) {
                    OnValueChangedResult result = cvar->proxy->OnValueChanged(cvar->resetValue);
                    if(result.success) {
                        ChangeCvarDescription(entry.first, cvar, result.description);
                    } else {
                        Log::Notice("Default value '%s' is not correct for cvar '%s': %s",
                                cvar->resetValue.c_str(), entry.first.c_str(), result.description.c_str());
                    }
                }
            }
        }
    }

    // Activates the new value of a latched cvar, like Cvar_Get for an old-style cvar with
    // CVAR_LATCH. By adding the latch flag here rather than in the cvar's constructor, we avoid
    // 'change will take effect after restart' messages during initialization. Also we avoid
    // the programming error of setting the latch flag but never reloading the value.
    void Latch(CvarProxy& cvar)
    {
        CvarMap& map = GetCvarMap();
        auto it = map.find(cvar.Name());

        if (it == map.end() || !it->second->proxy) {
            Sys::Error("cvar %s not registered", cvar.Name());
        }

        it->second->flags |= INTERNAL_LATCH;

        if (it->second->latchedValue) {
            cvar_modifiedFlags |= it->second->flags;
            it->second->value = std::move(*it->second->latchedValue);
            it->second->latchedValue = Util::nullopt;
            SetCCvar(*it->second);
            OnValueChangedResult result = it->second->proxy->OnValueChanged(it->second->value);
            if (result.success) {
                ChangeCvarDescription(it->first, it->second, result.description);
            } else {
                Log::Warn("BUG: failed setting cvar %s to latched value", cvar.Name());
            }
        }
    }

    // Used by the C API

    cvar_t* FindCCvar(const std::string& cvarName) {
        CvarMap& cvars = GetCvarMap();

        auto iter = cvars.find(cvarName);
        if (iter != cvars.end()) {
            return &iter->second->ccvar;
        }

        return nullptr;
    }

    std::string GetCvarConfigText() {
        CvarMap& cvars = GetCvarMap();
        std::ostringstream result;

        for (auto& entry : cvars) {
            cvarRecord_t* cvar = entry.second;

            if (cvar->IsArchived()) {
                const char* value;
                if (cvar->ccvar.latchedString) {
                    value = cvar->ccvar.latchedString;
                } else {
                    value = cvar->value.c_str();
                }
                result << Str::Format("seta %s %s\n", entry.first.c_str(), Cmd::Escape(value).c_str());
            }
        }
        return result.str();
    }

    char* InfoString(int flag, bool big) {
        static char info[BIG_INFO_STRING];
        info[0] = 0;

        CvarMap& cvars = GetCvarMap();

        for (auto& entry : cvars) {
            cvarRecord_t* cvar = entry.second;

            if (cvar->flags & flag) {
                Info_SetValueForKey(info, entry.first.c_str(), cvar->value.c_str(), big);
            }
        }

        return info;
    }

    void PopulateInfoMap(int flag, InfoMap& map) {

        for (auto& entry : GetCvarMap()) {
            cvarRecord_t* cvar = entry.second;

            if (cvar->flags & flag) {
                map[entry.first] = cvar->value;
            }
        }
    }

    void SetValueCProxy(const std::string& cvarName, const std::string& value) {
        InternalSetValue(cvarName, value, 0, true, false);
    }

    /*
    ===============================================================================

    Cvar:: Cvar-related commands

    ===============================================================================
    */

    class SetCmd: public Cmd::StaticCmd {
        public:
            SetCmd(const std::string& name, const std::string& help, int flags): Cmd::StaticCmd(name, Cmd::BASE, help), flags(flags) {
            }

            void Run(const Cmd::Args& args) const override {
                if (args.Argc() < 2) {
                    PrintUsage(args, "<variable> <value>","");
                    return;
                }

                const std::string& name = args.Argv(1);
                const std::string& value = args.ConcatArgs(2);

                ::Cvar::SetValue(name, value);
                ::Cvar::AddFlags(name, flags);
            }

            Cmd::CompletionResult Complete(int argNum, const Cmd::Args&, Str::StringRef prefix) const override {
                if (argNum == 1) {
                    return ::Cvar::Complete(prefix);
                }

                return {};
            }

        private:
            int flags;
    };
    static SetCmd SetCmdRegistration("set", "sets the value of a cvar", 0);
    static SetCmd SetuCmdRegistration("setu", "sets the value of a cvar", USERINFO);
    static SetCmd SetsCmdRegistration("sets", "sets the value of a cvar", SERVERINFO);
    static SetCmd SetaCmdRegistration("seta", "sets the value of a cvar and marks the cvar as archived", USER_ARCHIVE);

    class ResetCmd: public Cmd::StaticCmd {
        public:
            ResetCmd(): Cmd::StaticCmd("reset", Cmd::BASE, "resets the named variables") {
            }

            void Run(const Cmd::Args& args) const override {
                int argc = args.Argc();
                bool clearArchive = true;

                if (argc < 2) {
                    PrintUsage(args, "<variable>…", "");
                    return;
                }

                CvarMap& cvars = GetCvarMap();

                for (int i = 1; i < argc; ++i)
                {
                    const std::string& name = args[i];

                    auto iter = cvars.find(name);
                    if (iter != cvars.end()) {
                        cvarRecord_t* cvar = iter->second;
                        ::Cvar::SetValue(name, cvar->resetValue);
                        if (clearArchive) {
                            ::Cvar::ClearFlags(name, USER_ARCHIVE);
                        }
                    } else {
                        Print("Cvar '%s' doesn't exist", name.c_str());
                    }
                }
            }

            Cmd::CompletionResult Complete(int argNum, const Cmd::Args&, Str::StringRef prefix) const override {
                if (argNum) {
                    return ::Cvar::Complete(prefix);
                }

                return {};
            }
    };
    static ResetCmd ResetCmdRegistration;

    std::string Raw(const std::string& src) {
        std::string out;
        size_t length = src.length();

        for (size_t i = 0; i < length; ++i)
        {
            if (src[i] == '^')
                out += '^';
            out += src[i];
        }
        return out;
    }

    class ListCvars: public Cmd::StaticCmd {
        public:
            ListCvars(): Cmd::StaticCmd("listCvars", Cmd::BASE, "lists variables") {
            }

            void Run(const Cmd::Args& args) const override {
                CvarMap& cvars = GetCvarMap();

                bool raw = false;
                std::string match = "";

                //Read parameters
                if (args.Argc() > 1) {
                    match = args.Argv(1);
                    if (Cmd::IsSwitch(match, "-raw")) {
                        raw = true;
                        match = (args.Argc() > 2) ? args.Argv(2) : "";
                    }
                }

                std::vector<cvarRecord_t*> matches;

                std::vector<std::string> matchesNames;
                size_t maxNameLength = 0;

                std::vector<std::string> matchesValues;

                //Find all the matching cvars
                for (auto& entry : cvars) {
                    if (Com_Filter(match.c_str(), entry.first.c_str(), false)) {
                        matchesNames.push_back(entry.first);

                        matches.push_back(entry.second);
                        matchesValues.push_back(entry.second->value);

                        //TODO: the raw parameter is not handled, need a function to escape carets
                        maxNameLength = std::max(maxNameLength, entry.first.length());
                    }
                }

                //Print the matches, keeping the flags and descriptions aligned
                for (size_t i = 0; i < matches.size(); i++) {
                    const std::string& name = matchesNames[i];
                    cvarRecord_t* var = matches[i];

                    std::string cvarFlags = "";
                    cvarFlags += (var->flags & SERVERINFO) ? "S" : "_";
                    cvarFlags += (var->flags & SYSTEMINFO) ? "s" : "_";
                    cvarFlags += (var->flags & USERINFO) ? "U" : "_";
                    cvarFlags += (var->flags & ROM) ? "R" : "_";
                    cvarFlags += (var->flags & INIT) ? "I" : "_";
                    cvarFlags += (var->flags & TEMPORARY) ? "T" : (var->flags & USER_ARCHIVE) ? "A" : "_";
                    cvarFlags += (var->flags & (CVAR_LATCH | INTERNAL_LATCH)) ? "L" : "_";
                    cvarFlags += (var->flags & CHEAT) ? "C" : "_";
                    cvarFlags += (var->flags & CVAR_USER_CREATED) ? "?" : "_";

                    // this is going to 'break' (wrong output) if the description contains any ^s other than in the variable value(s)
                    if (raw) {
                        Print("  %-*s %s %s", maxNameLength, name, cvarFlags, Raw(var->description));
                    }
                    else {
                        Print("  %-*s %s %s", maxNameLength, name, cvarFlags, var->description);
                    }
                }

                Print("%zu cvars", matches.size());
            }

            Cmd::CompletionResult Complete(int argNum, const Cmd::Args& args, Str::StringRef prefix) const override {
                int nameIndex = 1;
                int argc = args.Argc();

                // FIXME: translation
                static const std::initializer_list<Cmd::CompletionItem> cvarFlags = {
                    {"-raw", "display colour controls"},
                };

                // command only allows one switch
                if (argc > 1 && args[1][0] == '-') {
                    nameIndex = 2;
                }

                if (argNum == 1) {
                    auto completion = ::Cvar::Complete(prefix);
                    Cmd::AddToCompletion(completion, prefix, cvarFlags);
                    return completion;
                }

                if (argNum == nameIndex) {
                    return ::Cvar::Complete(prefix);
                }

                return {};
            }
    };
    static ListCvars ListCvarsRegistration;

}
