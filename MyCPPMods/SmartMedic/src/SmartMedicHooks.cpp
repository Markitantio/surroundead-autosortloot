#include "SmartMedicHooks.hpp"
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <fstream>

using namespace RC;
using namespace RC::Unreal;

namespace SmartMedic
{
    // ============================================================
    //  Settings
    // ============================================================

    struct MedSettings {
        int    hotkey = VK_F10;
        bool   verboseLogs = false;
        double healthThreshold = 0.5;

        std::vector<StringType> prioHeavyBleed;
        std::vector<StringType> prioBleed;
        std::vector<StringType> prioBrokenBone;
        std::vector<StringType> prioInfection;
        std::vector<StringType> prioRadiation;
        std::vector<StringType> prioLowHealth;
    };

    static MedSettings G_Settings;
    static StringType  G_ConfigPath;

    // ============================================================
    //  CONFIG  (adapted from AutoSortLoot)
    // ============================================================

    static std::string WToU8(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                                    nullptr, 0, nullptr, nullptr);
        std::string out(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                            out.data(), n, nullptr, nullptr);
        return out;
    }
    static std::wstring U8ToW(const std::string& s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring out(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
        return out;
    }

    static void StripBomAndTrim(StringType& s)
    {
        while (!s.empty() && (s.front() == 0xFEFF || s.front() == 0x200B
                              || s.front() == 0x00A0))
            s.erase(s.begin());
        auto isSpace = [](wchar_t c) {
            return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
        };
        while (!s.empty() && isSpace(s.front())) s.erase(s.begin());
        while (!s.empty() && isSpace(s.back()))  s.pop_back();
    }

    static bool ParseBool(const StringType& v)
    {
        if (v.empty()) return false;
        StringType s = v;
        for (auto& c : s) c = std::towlower(c);
        StripBomAndTrim(s);
        return s == L"true" || s == L"1" || s == L"yes" || s == L"on";
    }

    static int ParseHotkey(const StringType& raw)
    {
        StringType s = raw;
        StripBomAndTrim(s);
        for (auto& c : s) c = std::towupper(c);
        if (s.empty()) return 0;
        if (s.size() >= 2 && s[0] == L'F') {
            int n = _wtoi(s.c_str() + 1);
            if (n >= 1 && n <= 24) return VK_F1 + (n - 1);
        }
        if (s.size() == 1) {
            wchar_t c = s[0];
            if (c >= L'0' && c <= L'9') return (int)c;
            if (c >= L'A' && c <= L'Z') return (int)c;
        }
        if (s == L"INSERT")                     return VK_INSERT;
        if (s == L"HOME")                       return VK_HOME;
        if (s == L"END")                        return VK_END;
        if (s == L"DELETE" || s == L"DEL")      return VK_DELETE;
        if (s == L"PAGEUP" || s == L"PGUP")     return VK_PRIOR;
        if (s == L"PAGEDOWN" || s == L"PGDN")   return VK_NEXT;
        if (s == L"SPACE")                      return VK_SPACE;
        if (s == L"TAB")                        return VK_TAB;
        return 0;
    }

    static std::vector<StringType> ParseCommaList(const StringType& raw)
    {
        std::vector<StringType> out;
        StringType cur;
        for (wchar_t c : raw) {
            if (c == L',') {
                StripBomAndTrim(cur);
                if (!cur.empty()) out.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        StripBomAndTrim(cur);
        if (!cur.empty()) out.push_back(cur);
        return out;
    }

    static bool FileExists(const StringType& path)
    {
        DWORD attr = GetFileAttributesW(path.c_str());
        return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
    }

    static StringType GetConfigPath()
    {
        HMODULE hMod = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetConfigPath),
            &hMod);
        if (!hMod) return StringType(L"SmartMedic.ini");

        wchar_t path[MAX_PATH] = {};
        DWORD n = GetModuleFileNameW(hMod, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return StringType(L"SmartMedic.ini");

        StringType p(path);
        size_t p1 = p.find_last_of(L"\\/");
        if (p1 == StringType::npos) return StringType(L"SmartMedic.ini");
        p = p.substr(0, p1);
        size_t p2 = p.find_last_of(L"\\/");
        if (p2 == StringType::npos) return StringType(L"SmartMedic.ini");
        p = p.substr(0, p2);
        p += L"\\SmartMedic.ini";
        return p;
    }

    static void SaveDefaultConfig(const StringType& path)
    {
        std::wstring c;
        c += L"; ============================================================\r\n";
        c += L";  SmartMedic - config\r\n";
        c += L"; ============================================================\r\n";
        c += L"; После изменения файла перезапустите игру.\r\n";
        c += L";\r\n";
        c += L"; Hotkey  — по этой клавише мод выберет и применит\r\n";
        c += L";           наиболее подходящий медицинский предмет.\r\n";
        c += L";\r\n";
        c += L"; HealthThreshold — доля HP (0..1), ниже которой мод\r\n";
        c += L";                   попытается вылечить (если нет других\r\n";
        c += L";                   приоритетных состояний).\r\n";
        c += L";\r\n";
        c += L"; Приоритеты: список ItemId через запятую. Мод берёт\r\n";
        c += L"; первый, который найдётся в инвентаре.\r\n";
        c += L";\r\n\r\n";

        c += L"[Settings]\r\n";
        c += L"Hotkey=F10\r\n";
        c += L"VerboseLogs=false\r\n";
        c += L"HealthThreshold=0.5\r\n\r\n";

        c += L"[Priorities]\r\n";
        c += L"; HeavyBleed и Bleed взаимоисключающие — если есть HeavyBleed,\r\n";
        c += L"; используется HeavyBleed-список (Bandages лечат и то, и другое).\r\n";
        c += L"HeavyBleed=Bandages,SmallMedkit,LargeMedkit\r\n";
        c += L"Bleed=Rags,Bandages,SmallMedkit,LargeMedkit\r\n";
        c += L"BrokenBone=MakeshiftSplint,Splint,LargeMedkit\r\n";
        c += L"Infection=InfectionCure,LargeMedkit,SmallMedkit\r\n";
        c += L"Radiation=RadiationPills,LargeMedkit\r\n";
        c += L"LowHealth=SmallMedkit,LargeMedkit,Bandages\r\n";

        std::string utf8 = WToU8(c);
        std::ofstream f(WToU8(path).c_str(), std::ios::binary);
        if (!f.is_open()) return;
        f.write(utf8.data(), (std::streamsize)utf8.size());
    }

    static bool LoadConfigFromFile(const StringType& path)
    {
        std::ifstream f(WToU8(path).c_str(), std::ios::binary);
        if (!f.is_open()) return false;

        std::string raw;
        StringType curSection;

        while (std::getline(f, raw)) {
            if (!raw.empty() && raw.back() == '\r') raw.pop_back();

            if (raw.size() >= 3
                && (uint8_t)raw[0] == 0xEF
                && (uint8_t)raw[1] == 0xBB
                && (uint8_t)raw[2] == 0xBF)
                raw.erase(0, 3);
            if (raw.size() >= 2
                && (uint8_t)raw[0] == 0xFF
                && (uint8_t)raw[1] == 0xFE)
                raw.erase(0, 2);

            StringType line = U8ToW(raw);
            StripBomAndTrim(line);
            if (line.empty()) continue;
            if (line[0] == L';' || line[0] == L'#') continue;

            if (line.front() == L'[' && line.back() == L']') {
                curSection = line.substr(1, line.size() - 2);
                StripBomAndTrim(curSection);
                continue;
            }

            size_t eq = line.find(L'=');
            if (eq == StringType::npos) continue;

            StringType key   = line.substr(0, eq);
            StringType value = line.substr(eq + 1);
            StripBomAndTrim(key);
            StripBomAndTrim(value);

            if (curSection == L"Settings") {
                if (key == L"Hotkey") {
                    int vk = ParseHotkey(value);
                    if (vk) G_Settings.hotkey = vk;
                } else if (key == L"VerboseLogs") {
                    G_Settings.verboseLogs = ParseBool(value);
                } else if (key == L"HealthThreshold") {
                    try {
                        double v = std::stod(value);
                        if (v > 0.0 && v <= 1.0) G_Settings.healthThreshold = v;
                    } catch (...) {}
                }
            } else if (curSection == L"Priorities") {
                auto list = ParseCommaList(value);
                if (list.empty()) continue;
                if      (key == L"HeavyBleed")  G_Settings.prioHeavyBleed  = list;
                else if (key == L"Bleed")       G_Settings.prioBleed       = list;
                else if (key == L"BrokenBone")  G_Settings.prioBrokenBone  = list;
                else if (key == L"Infection")   G_Settings.prioInfection   = list;
                else if (key == L"Radiation")   G_Settings.prioRadiation   = list;
                else if (key == L"LowHealth")   G_Settings.prioLowHealth   = list;
            }
        }
        return true;
    }

    static void InstallConfig()
    {
        G_ConfigPath = GetConfigPath();
        Output::send<LogLevel::Verbose>(STR("[SmartMedic] config path: {}\n"), G_ConfigPath);

        if (!FileExists(G_ConfigPath)) {
            Output::send<LogLevel::Verbose>(STR("[SmartMedic] creating default config\n"));
            SaveDefaultConfig(G_ConfigPath);
        }
        LoadConfigFromFile(G_ConfigPath);

        Output::send<LogLevel::Verbose>(
            STR("[SmartMedic] config: hotkey=0x{:X} verbose={} hpThr={:.2f}\n"),
            G_Settings.hotkey,
            G_Settings.verboseLogs ? 1 : 0,
            G_Settings.healthThreshold);

        auto dump = [](const wchar_t* name, const std::vector<StringType>& v) {
            Output::send<LogLevel::Verbose>(STR("[SmartMedic]   {} ({}): "),
                                            StringType(name), (int)v.size());
            for (auto& s : v) Output::send<LogLevel::Verbose>(STR("'{}' "), s);
            Output::send<LogLevel::Verbose>(STR("\n"));
        };
        dump(L"HeavyBleed",  G_Settings.prioHeavyBleed);
        dump(L"Bleed",       G_Settings.prioBleed);
        dump(L"BrokenBone",  G_Settings.prioBrokenBone);
        dump(L"Infection",   G_Settings.prioInfection);
        dump(L"Radiation",   G_Settings.prioRadiation);
        dump(L"LowHealth",   G_Settings.prioLowHealth);
    }

    // ============================================================
    //  HELPERS
    // ============================================================

    #define SM_V(...) do { if (G_Settings.verboseLogs) \
        Output::send<LogLevel::Verbose>(__VA_ARGS__); } while(0)

    #define SM_LOG(...) \
        Output::send<LogLevel::Verbose>(__VA_ARGS__)

    static StringType SafeName(UObject* o)
    { return o ? o->GetName() : StringType(STR("<null>")); }

    static uint32_t ReadU32(void* b, int32_t o)
    { return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(b) + o); }
    static bool ReadBool(void* b, int32_t o)
    { return *reinterpret_cast<bool*>(reinterpret_cast<uint8_t*>(b) + o); }
    static double ReadDouble(void* b, int32_t o)
    { return *reinterpret_cast<double*>(reinterpret_cast<uint8_t*>(b) + o); }
    static void* ReadPtr(void* b, int32_t o)
    { return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(b) + o); }
    static StringType ReadFNameAt(void* b, int32_t o)
    {
        FName* f = reinterpret_cast<FName*>(reinterpret_cast<uint8_t*>(b) + o);
        return f->ToString();
    }

    static bool IsPendingKill(UObject* o)
    {
        if (!o) return true;
        uint32_t flags = ReadU32(o, 0x08);
        return (flags & 0x42000000u) != 0;
    }

    // ============================================================
    //  OFFSETS
    // ============================================================

    namespace PlayerOffset {
        constexpr int32_t MedicalComponent   = 0x07A8;
        constexpr int32_t JigContextMenuComp = 0x07E8;
        constexpr int32_t BP_JigMultiplayer  = 0x07F0;
        constexpr int32_t PlayerController   = 0x13C8;
    }

    namespace MedOffset {
        constexpr int32_t Bleed             = 0x00D8;
        constexpr int32_t HeavyBleed        = 0x00D9;
        constexpr int32_t BrokenBone        = 0x00DA;
        constexpr int32_t Health            = 0x00E0;
        constexpr int32_t MaxHealth         = 0x00E8;
        constexpr int32_t RadiationSickness = 0x0118;
        constexpr int32_t Infection         = 0x0119;
    }

    namespace JsiOffset {
        constexpr int32_t ArrayOfItems = 0x0490;
    }

    namespace SlotOffset {
        constexpr int32_t JigDataAsset = 0x04F0;
    }

    namespace DaOffset {
        constexpr int32_t ItemId   = 0x0030;
        constexpr int32_t ItemType = 0x0068;
    }

    // ============================================================
    //  FIND LOCAL PLAYER
    // ============================================================

    static UObject* FindLocalPlayer()
    {
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("BP_PlayerCharacter_C"), all);
        for (UObject* p : all) {
            if (!p) continue;
            if (IsPendingKill(p)) continue;
            UObject* pc = reinterpret_cast<UObject*>(ReadPtr(p, PlayerOffset::PlayerController));
            if (pc) return p;
        }
        // fallback: any non-pending-kill player character
        for (UObject* p : all) {
            if (p && !IsPendingKill(p)) return p;
        }
        return nullptr;
    }

    // ============================================================
    //  READ MEDICAL STATE
    // ============================================================

    struct MedState {
        bool   bleed = false;
        bool   heavyBleed = false;
        bool   brokenBone = false;
        bool   infection = false;
        bool   radiation = false;
        double health = 0.0;
        double maxHealth = 100.0;
        double healthPct = 1.0;
    };

    static bool ReadMedicalState(UObject* player, MedState& out)
    {
        if (!player) return false;
        UObject* med = reinterpret_cast<UObject*>(ReadPtr(player, PlayerOffset::MedicalComponent));
        if (!med) return false;

        out.bleed      = ReadBool(med, MedOffset::Bleed);
        out.heavyBleed = ReadBool(med, MedOffset::HeavyBleed);
        out.brokenBone = ReadBool(med, MedOffset::BrokenBone);
        out.radiation  = ReadBool(med, MedOffset::RadiationSickness);
        out.infection  = ReadBool(med, MedOffset::Infection);
        out.health     = ReadDouble(med, MedOffset::Health);
        out.maxHealth  = ReadDouble(med, MedOffset::MaxHealth);
        out.healthPct  = (out.maxHealth > 0.0)
                         ? (out.health / out.maxHealth)
                         : 1.0;
        return true;
    }

    // ============================================================
    //  FIND ITEM IN INVENTORY
    // ============================================================

    struct SimpleTArray { void* Data; int32_t Num; int32_t Max; };

    static void CollectItemsById(const StringType& wantId,
                                 std::vector<UObject*>& out,
                                 int32_t maxItems = 4)
    {
        if ((int32_t)out.size() >= maxItems) return;

        std::vector<UObject*> containers;
        UObjectGlobals::FindAllOf(STR("JSIContainer_C"), containers);

        for (UObject* c : containers) {
            if (!c || IsPendingKill(c)) continue;

            auto* arr = reinterpret_cast<SimpleTArray*>(
                reinterpret_cast<uint8_t*>(c) + JsiOffset::ArrayOfItems);
            if (!arr || arr->Num <= 0 || !arr->Data) continue;

            UObject** items = reinterpret_cast<UObject**>(arr->Data);
            for (int32_t i = 0; i < arr->Num; ++i) {
                UObject* slot = items[i];
                if (!slot || IsPendingKill(slot)) continue;

                UObject* da = reinterpret_cast<UObject*>(ReadPtr(slot, SlotOffset::JigDataAsset));
                if (!da) continue;

                StringType id = ReadFNameAt(da, DaOffset::ItemId);
                if (id == wantId) {
                    out.push_back(slot);
                    if ((int32_t)out.size() >= maxItems) return;
                }
            }
        }
    }

    // ============================================================
    //  USE ITEM (Player.HotBarConsume)
    // ============================================================

    static bool CallHotBarConsume(UObject* player, UObject* itemRef)
    {
        if (!player || !itemRef) return false;

        UClass* cls = player->GetClassPrivate();
        if (!cls) return false;

        UFunction* fn = cls->GetFunctionByName(STR("HotBarConsume"));
        if (!fn) {
            SM_LOG(STR("[SmartMedic] HotBarConsume NOT FOUND\n"));
            return false;
        }

        // Params: (UJSI_Slot_C* ItemRef)
        uint8_t buf[0x40] = {};
        *reinterpret_cast<UObject**>(buf + 0x00) = itemRef;

        player->ProcessEvent(fn, buf);
        return true;
    }

    // ============================================================
    //  PRIORITY LIST
    // ============================================================

    static void AppendDedup(std::vector<StringType>& dst, const std::vector<StringType>& src)
    {
        for (const auto& s : src) {
            bool exists = false;
            for (const auto& d : dst) if (d == s) { exists = true; break; }
            if (!exists) dst.push_back(s);
        }
    }

    static std::vector<StringType> BuildPriorityList(const MedState& st)
    {
        std::vector<StringType> list;
        if (st.heavyBleed)   AppendDedup(list, G_Settings.prioHeavyBleed);
        else if (st.bleed)   AppendDedup(list, G_Settings.prioBleed);
        if (st.brokenBone)   AppendDedup(list, G_Settings.prioBrokenBone);
        if (st.infection)    AppendDedup(list, G_Settings.prioInfection);
        if (st.radiation)    AppendDedup(list, G_Settings.prioRadiation);
        if (st.healthPct < G_Settings.healthThreshold)
                             AppendDedup(list, G_Settings.prioLowHealth);
        return list;
    }

    // ============================================================
    //  MAIN
    // ============================================================

    static void TrySmartConsume()
    {
        SM_LOG(STR("[SmartMedic] === TRY ===\n"));

        UObject* player = FindLocalPlayer();
        if (!player) { SM_LOG(STR("[SmartMedic] no player\n")); return; }
        SM_LOG(STR("[SmartMedic] player='{}'\n"), SafeName(player));

        MedState st{};
        if (!ReadMedicalState(player, st)) {
            SM_LOG(STR("[SmartMedic] no MedicalComponent\n"));
            return;
        }

        SM_LOG(STR("[SmartMedic] HP={:.1f}/{:.1f} ({:.0f}%) "
                   "Bleed={} HeavyBleed={} BrokenBone={} Infection={} Radiation={}\n"),
               st.health, st.maxHealth, st.healthPct * 100.0,
               st.bleed ? 1 : 0, st.heavyBleed ? 1 : 0,
               st.brokenBone ? 1 : 0, st.infection ? 1 : 0,
               st.radiation ? 1 : 0);

        std::vector<StringType> priority = BuildPriorityList(st);

        if (priority.empty()) {
            SM_LOG(STR("[SmartMedic] nothing to treat\n"));
            return;
        }

        SM_LOG(STR("[SmartMedic] priority ({}): "), (int)priority.size());
        for (auto& s : priority) SM_LOG(STR("'{}' "), s);
        SM_LOG(STR("\n"));

        for (const auto& itemId : priority) {
            std::vector<UObject*> found;
            CollectItemsById(itemId, found, 4);
            if (found.empty()) {
                SM_V(STR("[SmartMedic]   '{}' — not found\n"), itemId);
                continue;
            }

            UObject* slot = found[0];
            SM_LOG(STR("[SmartMedic] USING '{}' slot={}\n"),
                   itemId, SafeName(slot));

            if (CallHotBarConsume(player, slot)) {
                SM_LOG(STR("[SmartMedic] consume dispatched\n"));
                return;
            } else {
                SM_LOG(STR("[SmartMedic] consume FAILED\n"));
                return;
            }
        }

        SM_LOG(STR("[SmartMedic] no matching item in inventory\n"));
    }

    // ============================================================
    //  HOTKEY
    // ============================================================

    static bool G_HookRegistered = false;
    static bool G_KeyWasDown = false;
    static bool G_Busy = false;

    void TryInstall()
    {
        if (!G_HookRegistered) {
            Hook::RegisterProcessLocalScriptFunctionPreCallback(
                [](UObject*, FFrame&, void*) {});
            G_HookRegistered = true;
            SM_LOG(STR("[SmartMedic] hook installed\n"));
        }

        if (G_Settings.hotkey == 0) { G_KeyWasDown = false; return; }

        bool down = (GetAsyncKeyState(G_Settings.hotkey) & 0x8000) != 0;
        if (down && !G_KeyWasDown && !G_Busy) {
            G_Busy = true;
            TrySmartConsume();
            G_Busy = false;
        }
        G_KeyWasDown = down;
    }

    void Install()
    {
        Output::send<LogLevel::Verbose>(STR("[SmartMedic] Install()\n"));
        InstallConfig();
    }

    void Uninstall()
    {
        Output::send<LogLevel::Verbose>(STR("[SmartMedic] Uninstall()\n"));
    }
}