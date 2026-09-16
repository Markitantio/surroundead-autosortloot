#include "AutoSortHooks.hpp"
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

namespace AutoSortHooks
{
    // ================== CONFIG ==================

    struct SortSettings {
        int hotkey = VK_F9;
        bool verboseLogs = false;
        StringType unknownItemAction = STR("Skip");
        StringType rootContainer = STR("LargeBlackMilitaryBackpack");
        std::map<StringType, std::vector<StringType>> mapping;
    };

    static SortSettings G_Settings;
    static StringType  G_ConfigPath;

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
        if (s == L"INSERT")                        return VK_INSERT;
        if (s == L"HOME")                          return VK_HOME;
        if (s == L"END")                           return VK_END;
        if (s == L"DELETE" || s == L"DEL")         return VK_DELETE;
        if (s == L"PAGEUP" || s == L"PGUP")        return VK_PRIOR;
        if (s == L"PAGEDOWN" || s == L"PGDN")      return VK_NEXT;
        if (s == L"SPACE")                         return VK_SPACE;
        if (s == L"TAB")                           return VK_TAB;
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
        if (!hMod) return StringType(L"AutoSortLoot.ini");

        wchar_t path[MAX_PATH] = {};
        DWORD n = GetModuleFileNameW(hMod, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return StringType(L"AutoSortLoot.ini");

        StringType p(path);
        size_t p1 = p.find_last_of(L"\\/");
        if (p1 == StringType::npos) return StringType(L"AutoSortLoot.ini");
        p = p.substr(0, p1);
        size_t p2 = p.find_last_of(L"\\/");
        if (p2 == StringType::npos) return StringType(L"AutoSortLoot.ini");
        p = p.substr(0, p2);
        p += L"\\AutoSortLoot.ini";
        return p;
    }

    static void SaveDefaultConfig(const StringType& path)
    {
        std::wstring content;
        content += L"; ============================================================\r\n";
        content += L";  AutoSortLoot - config\r\n";
        content += L"; ============================================================\r\n";
        content += L"; После изменения файла перезапустите игру.\r\n";
        content += L";\r\n\r\n";

        content += L"[Settings]\r\n";
        content += L"Hotkey=F9\r\n";
        content += L"VerboseLogs=false\r\n";
        content += L"UnknownItemAction=Skip\r\n";
        content += L"RootContainer=LargeBlackMilitaryBackpack\r\n\r\n";

        content += L"[Mapping]\r\n";
        content += L"MedicalConsumable=MedBag\r\n";
        content += L"FoodConsumable=LunchBox\r\n";
        content += L"DrinkConsumable=LunchBox\r\n";
        content += L"MainFirearm=WeaponsCase\r\n";
        content += L"Sidearm=WeaponsCase\r\n";
        content += L"Melee=WeaponsCase\r\n";
        content += L"Ammunition=AmmoTin,WeaponsCase\r\n";
        content += L"WeaponAttachment=AmmoTin,WeaponsCase\r\n";
        content += L"EquipmentAttachment=AmmoTin,WeaponsCase\r\n";
        content += L"Throwable=AmmoTin,WeaponsCase\r\n";
        content += L"Material=LargeToolbox,SmallToolbox\r\n";
        content += L"Tool=LargeToolbox,SmallToolbox\r\n";
        content += L"Currency=Wallet,Safe,Briefcase\r\n";
        content += L"Keycard=Wallet,Safe,Briefcase\r\n";

        std::string utf8 = WToU8(content);
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
                } else if (key == L"UnknownItemAction") {
                    G_Settings.unknownItemAction = value;
                } else if (key == L"RootContainer") {
                    G_Settings.rootContainer = value;
                }
            } else if (curSection == L"Mapping") {
                StringType fullKey = key;
                if (fullKey.find(L'.') == StringType::npos) {
                    fullKey = StringType(L"Jig.ItemType.") + key;
                }
                auto list = ParseCommaList(value);
                if (!list.empty()) G_Settings.mapping[fullKey] = list;
            }
        }
        return true;
    }

    static void InstallConfig()
    {
        G_ConfigPath = GetConfigPath();
        Output::send<LogLevel::Verbose>(
            STR("[AutoSortLoot] config path: {}\n"), G_ConfigPath);

        if (!FileExists(G_ConfigPath)) {
            Output::send<LogLevel::Verbose>(
                STR("[AutoSortLoot] config not found, creating default\n"));
            SaveDefaultConfig(G_ConfigPath);
        }
        LoadConfigFromFile(G_ConfigPath);
        Output::send<LogLevel::Verbose>(
            STR("[AutoSortLoot] config loaded: hotkey=0x{:X}, rules={}, verbose={}\n"),
            G_Settings.hotkey, (int)G_Settings.mapping.size(),
            G_Settings.verboseLogs ? 1 : 0);
    }

    // ================== UTILITIES ==================

    bool G_HookRegistered = false;
    bool G_ScannerRunning = false;
    bool G_HotkeyWasDown = false;

    #define SORT_V(...) do { if (G_Settings.verboseLogs) \
        Output::send<LogLevel::Verbose>(__VA_ARGS__); } while(0)

    static StringType SafeName(UObject* o)
    { return o ? o->GetName() : StringType(STR("<null>")); }

    struct SimpleTArray { void* Data; int32_t Num; int32_t Max; };

    static int32_t ReadI32(void* b, int32_t o)
    { return *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(b) + o); }
    static uint32_t ReadU32(void* b, int32_t o)
    { return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(b) + o); }
    static double ReadDouble(void* b, int32_t o)
    { return *reinterpret_cast<double*>(reinterpret_cast<uint8_t*>(b) + o); }
    static void* ReadPtr(void* b, int32_t o)
    { return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(b) + o); }
    static StringType ReadFNameAt(void* b, int32_t o)
    {
        FName* f = reinterpret_cast<FName*>(reinterpret_cast<uint8_t*>(b) + o);
        return f->ToString();
    }

    UFunction* GetFunctionFromFrame(FFrame& Stack)
    { UFunction* F = Stack.Node(); return F ? F : Stack.CurrentNativeFunction(); }

    void PLSFPreHook(UObject* Context, FFrame& Stack, void* Result)
    { (void)Context; (void)Stack; (void)Result; }

    bool CallFunc(UObject* obj, const wchar_t* fname, uint8_t* params, int32_t bufSize)
    {
        if (!obj) return false;
        UClass* cls = obj->GetClassPrivate();
        if (!cls) return false;
        UFunction* fn = cls->GetFunctionByName(fname);
        if (!fn) return false;
        obj->ProcessEvent(fn, params);
        (void)bufSize;
        return true;
    }

    StringType GetItemIdOf(UObject* itemWidget)
    {
        if (!itemWidget) return StringType(STR(""));
        void* da = ReadPtr(itemWidget, 0x4F0);
        if (!da) return StringType(STR(""));
        return ReadFNameAt(da, 0x30);
    }

    StringType GetItemTypeOf(UObject* itemWidget)
    {
        if (!itemWidget) return StringType(STR(""));
        void* da = ReadPtr(itemWidget, 0x4F0);
        if (!da) return StringType(STR(""));
        return ReadFNameAt(da, 0x68);
    }

    StringType GetContainerItemId(UObject* jsi)
    {
        if (!jsi) return StringType(STR(""));
        void* sm = ReadPtr(jsi, 0x6A0);
        if (!sm) return StringType(STR(""));
        void* da = ReadPtr(sm, 0x4F0);
        if (!da) return StringType(STR(""));
        return ReadFNameAt(da, 0x30);
    }

    StringType GetContainerItemType(UObject* jsi)
    {
        if (!jsi) return StringType(STR(""));
        void* sm = ReadPtr(jsi, 0x6A0);
        if (!sm) return StringType(STR(""));
        void* da = ReadPtr(sm, 0x4F0);
        if (!da) return StringType(STR(""));
        return ReadFNameAt(da, 0x68);
    }

    StringType GetContainerTypeTag(UObject* jsi)
    {
        if (!jsi) return StringType(STR(""));
        return ReadFNameAt(jsi, 0x38C);
    }

    bool GetContainerUid(UObject* jsi, uint8_t outUid[16])
    {
        if (!jsi) return false;
        std::memcpy(outUid, reinterpret_cast<uint8_t*>(jsi) + 0x37C, 16);
        for (int i = 0; i < 16; ++i) if (outUid[i]) return true;
        return false;
    }

    // Считаем «живые» слоты контейнера
    static int32_t CountWSlots(UObject* jsi)
    {
        if (!jsi) return 0;
        SimpleTArray* arr = reinterpret_cast<SimpleTArray*>(
            reinterpret_cast<uint8_t*>(jsi) + 0x3A8);
        if (!arr || arr->Num <= 0 || !arr->Data) return 0;
        // проверим, что первые слоты не null
        UObject** slots = reinterpret_cast<UObject**>(arr->Data);
        if (!slots || !slots[0]) return 0;
        return arr->Num;
    }

    UObject* FindItemWidgetByUID(UObject* jsi, const uint8_t* uid)
    {
        if (!jsi) return nullptr;
        SimpleTArray* arr = reinterpret_cast<SimpleTArray*>(
            reinterpret_cast<uint8_t*>(jsi) + 0x490);
        if (!arr || arr->Num <= 0 || !arr->Data) return nullptr;
        UObject** slots = reinterpret_cast<UObject**>(arr->Data);
        for (int32_t i = 0; i < arr->Num; ++i) {
            UObject* s = slots[i];
            if (!s) continue;
            if (std::memcmp(reinterpret_cast<uint8_t*>(s) + 0x5A0, uid, 16) == 0)
                return s;
        }
        return nullptr;
    }

    int32_t TryGetEmptySlotNative(UObject* jsi, double dimX, double dimY)
    {
        if (!jsi) return -1;
        if (CountWSlots(jsi) <= 0) return -1;

        uint8_t buf[0x40] = {};
        *reinterpret_cast<double*>(buf + 0x00) = dimX;
        *reinterpret_cast<double*>(buf + 0x08) = dimY;
        *reinterpret_cast<int32_t*>(buf + 0x10) = -1;
        buf[0x14] = 0;
        if (!CallFunc(jsi, STR("GetEmptySlot"), buf, sizeof(buf))) return -1;
        int32_t idx = *reinterpret_cast<int32_t*>(buf + 0x10);
        bool found = buf[0x14] != 0;
        return found ? idx : -1;
    }

    bool DoEventOnInventoryAction(UObject* jigComp, UObject* from, UObject* to,
                                  UObject* dropped, UObject* receiver,
                                  int32_t toSlot, bool rotated)
    {
        uint8_t buf[0x40] = {};
        *reinterpret_cast<UObject**>(buf + 0x00) = from;
        *reinterpret_cast<UObject**>(buf + 0x08) = to;
        *reinterpret_cast<UObject**>(buf + 0x10) = dropped;
        *reinterpret_cast<UObject**>(buf + 0x18) = receiver;
        *reinterpret_cast<int32_t*>(buf + 0x20) = toSlot;
        buf[0x24] = rotated ? 1 : 0;
        return CallFunc(jigComp, STR("EventOnInventoryAction"), buf, sizeof(buf));
    }

    // ===== Фильтры =====

    static bool IsPendingKill(UObject* o)
    {
        if (!o) return true;
        uint32_t flags = ReadU32(o, 0x08);
        return (flags & 0x42000000u) != 0;
    }

    static std::set<std::string> GetActiveUidSet(UObject* jigComp)
    {
        std::set<std::string> uids;
        if (!jigComp) return uids;
        SimpleTArray* arr = reinterpret_cast<SimpleTArray*>(
            reinterpret_cast<uint8_t*>(jigComp) + 0xC0);
        if (!arr || arr->Num <= 0 || !arr->Data) return uids;
        uint8_t* items = reinterpret_cast<uint8_t*>(arr->Data);
        for (int32_t i = 0; i < arr->Num; ++i) {
            uint8_t* mc = items + (i * 0x50);
            bool zero = true;
            for (int k = 0; k < 16; ++k) if (mc[k]) { zero = false; break; }
            if (zero) continue;
            uids.insert(std::string(reinterpret_cast<char*>(mc), 16));
        }
        return uids;
    }

    static StringType BuildOuterChain(UObject* obj, int maxDepth)
    {
        StringType chain;
        UObject* cur = obj;
        for (int i = 0; i < maxDepth && cur; ++i) {
            cur = cur->GetOuterPrivate();
            if (!cur) break;
            StringType nm = cur->GetName();
            UClass* cls = cur->GetClassPrivate();
            StringType cn = cls ? cls->GetName() : StringType(STR("?"));
            if (!chain.empty()) chain += STR("|");
            chain += nm;
            chain += STR("/");
            chain += cn;
        }
        return chain;
    }

    static bool ChainContainsAny(const StringType& chain, const wchar_t* const* keys)
    {
        for (size_t i = 0; keys[i]; ++i) {
            if (chain.find(keys[i]) != StringType::npos) return true;
        }
        return false;
    }

    static bool IsWorldContainer(UObject* jsi)
    {
        if (!jsi) return false;
        static const wchar_t* WORLD_KEYS[] = {
            L"W_LargeLootContainerUI",
            L"W_LootContainerUI",
            L"W_DeadPlayerLootUI",
            L"W_VicinityLootUI",
            L"Container_CompoundCrate",
            L"Container_POICrate",
            L"Container_DeadPlayerLoot",
            L"AirdropContainer_",
            L"Master_AirdropContainer",
            L"BP_LootContainer",
            L"BP_LootContainerWidget",
            nullptr
        };
        StringType chain = BuildOuterChain(jsi, 12);
        return ChainContainsAny(chain, WORLD_KEYS);
    }

    static bool IsTrueContainerType(const StringType& itemType)
    {
        return itemType == STR("Jig.ItemType.Container")
            || itemType == STR("Jig.ItemType.Backpack");
    }

    static bool IsChildOfWeapon(UObject* jsi)
    {
        if (!jsi) return false;
        void* sm = ReadPtr(jsi, 0x6A0);
        if (!sm) return false;
        void* da = ReadPtr(sm, 0x4F0);
        if (!da) return false;
        StringType smType = ReadFNameAt(da, 0x68);
        return smType == STR("Jig.ItemType.MainFirearm")
            || smType == STR("Jig.ItemType.Sidearm")
            || smType == STR("Jig.ItemType.Melee");
    }

    static bool IsValidSortContainer(UObject* jsi, const std::set<std::string>& activeUids)
    {
        if (!jsi) return false;
        if (IsPendingKill(jsi)) return false;

        uint8_t uid[16];
        if (!GetContainerUid(jsi, uid)) return false;
        if (activeUids.find(std::string(reinterpret_cast<char*>(uid), 16))
                == activeUids.end())
            return false;

        StringType ctype = GetContainerTypeTag(jsi);
        if (ctype == STR("Jig.ContainerType.EquipTo")) return false;
        if (IsChildOfWeapon(jsi)) return false;
        if (IsWorldContainer(jsi)) return false;
        return true;
    }

    static UObject* FindInventoryContainer(const std::vector<UObject*>& sources)
    {
        for (UObject* j : sources) {
            if (!j) continue;
            StringType ctype = GetContainerTypeTag(j);
            if (ctype == STR("Jig.ContainerType.Inventory")) return j;
        }
        return nullptr;
    }

    static bool ListContains(const std::vector<StringType>& lst, const StringType& v)
    {
        for (auto& x : lst) if (x == v) return true;
        return false;
    }

    struct PlannedMove {
        uint8_t  itemUid[16];
        UObject* srcJsi;
        StringType itemId;
        StringType itemType;
    };

    void SortLoot(UObject* jigComp)
    {
        Output::send<LogLevel::Verbose>(STR("[Sort] === START ===\n"));

        std::set<std::string> activeUids = GetActiveUidSet(jigComp);
        SORT_V(STR("[Sort] active UIDs in MainJigContainers: {}\n"),
               (int)activeUids.size());

        std::vector<UObject*> allJsi;
        UObjectGlobals::FindAllOf(STR("JSIContainer_C"), allJsi);

        std::vector<UObject*> sources;
        std::map<StringType, std::vector<UObject*>> byItemId;
        int skipNonContainerJsi = 0;
        int skipStale = 0;
        int skipNoSlots = 0;

        for (UObject* j : allJsi) {
            if (!j) continue;
            uint8_t uid[16];
            if (!GetContainerUid(j, uid)) { ++skipNonContainerJsi; continue; }
            if (!IsValidSortContainer(j, activeUids)) {
                if (activeUids.find(std::string(reinterpret_cast<char*>(uid), 16))
                        == activeUids.end())
                    ++skipStale;
                else
                    ++skipNonContainerJsi;
                continue;
            }

            // Фильтр по наличию слотов
            int32_t ws = CountWSlots(j);
            if (ws <= 0) { ++skipNoSlots; continue; }

            sources.push_back(j);

            StringType itemId = GetContainerItemId(j);
            StringType itemType = GetContainerItemType(j);
            if (!itemId.empty() && IsTrueContainerType(itemType)) {
                byItemId[itemId].push_back(j);
            }
        }

        // Сортируем кандидатов по убыванию WSlots.Num
        for (auto& kv : byItemId) {
            std::sort(kv.second.begin(), kv.second.end(),
                [](UObject* a, UObject* b) {
                    return CountWSlots(a) > CountWSlots(b);
                });
        }

        SORT_V(STR("[Sort] источников: {} (skip non-container: {}, stale: {}, no-slots: {})\n"),
               (int)sources.size(), skipNonContainerJsi, skipStale, skipNoSlots);
        for (auto& kv : byItemId) {
            SORT_V(STR("[Sort]   target {} x {} (max WSlots={})\n"),
                   kv.first, (int)kv.second.size(),
                   kv.second.empty() ? 0 : CountWSlots(kv.second[0]));
        }

        UObject* defaultInventory = FindInventoryContainer(sources);
        bool rootContainerValid = (byItemId.find(G_Settings.rootContainer) != byItemId.end());

        SORT_V(STR("[Sort] RootContainer='{}' valid={}, inventoryFallback={}\n"),
               G_Settings.rootContainer, rootContainerValid ? 1 : 0,
               defaultInventory ? 1 : 0);

        // ===== План =====
        std::vector<PlannedMove> plan;
        int skipNoRule = 0, skipInPlace = 0, skipNoTarget = 0, skipContainer = 0;

        for (UObject* srcJsi : sources) {
            StringType srcItemId = GetContainerItemId(srcJsi);

            SimpleTArray* items = reinterpret_cast<SimpleTArray*>(
                reinterpret_cast<uint8_t*>(srcJsi) + 0x490);
            if (!items || items->Num <= 0 || !items->Data) continue;
            UObject** arr = reinterpret_cast<UObject**>(items->Data);

            for (int32_t i = 0; i < items->Num; ++i) {
                UObject* iw = arr[i];
                if (!iw) continue;
                if (IsPendingKill(iw)) continue;

                StringType iType = GetItemTypeOf(iw);
                if (iType.empty()) { ++skipNoRule; continue; }
                if (IsTrueContainerType(iType)) { ++skipContainer; continue; }

                std::vector<StringType> targets;
                auto it = G_Settings.mapping.find(iType);
                if (it != G_Settings.mapping.end()) {
                    targets = it->second;
                } else {
                    if (G_Settings.unknownItemAction == STR("MoveToRoot")) {
                        if (rootContainerValid) {
                            targets.push_back(G_Settings.rootContainer);
                        } else if (defaultInventory && defaultInventory != srcJsi) {
                            targets.push_back(STR("__INVENTORY__"));
                        }
                    }
                    if (targets.empty()) { ++skipNoRule; continue; }
                }

                if (!srcItemId.empty() && ListContains(targets, srcItemId)) {
                    ++skipInPlace; continue;
                }

                bool hasAny = false;
                for (auto& t : targets) {
                    if (t == STR("__INVENTORY__")) {
                        if (defaultInventory && defaultInventory != srcJsi) { hasAny = true; break; }
                        continue;
                    }
                    auto jt = byItemId.find(t);
                    if (jt != byItemId.end() && !jt->second.empty()) { hasAny = true; break; }
                }
                if (!hasAny) { ++skipNoTarget; continue; }

                PlannedMove m{};
                std::memcpy(m.itemUid, reinterpret_cast<uint8_t*>(iw) + 0x5A0, 16);
                m.srcJsi = srcJsi;
                m.itemId = GetItemIdOf(iw);
                m.itemType = iType;
                plan.push_back(m);
            }
        }

        size_t beforeDedup = plan.size();
        {
            std::vector<PlannedMove> deduped;
            std::set<std::string> seen;
            for (auto& m : plan) {
                std::string key(reinterpret_cast<const char*>(m.itemUid), 16);
                if (seen.insert(key).second) deduped.push_back(m);
            }
            plan.swap(deduped);
        }
        if (plan.size() != beforeDedup) {
            SORT_V(STR("[Sort] plan dedup: {} -> {}\n"),
                   (int)beforeDedup, (int)plan.size());
        }

        SORT_V(STR("[Sort] план: {} (skip: noRule={} inPlace={} noTarget={} container={})\n"),
               (int)plan.size(), skipNoRule, skipInPlace, skipNoTarget, skipContainer);

        // ===== Выполнение =====
        int movedOk = 0, movedFail = 0;
        for (auto& m : plan) {
            UObject* item = FindItemWidgetByUID(m.srcJsi, m.itemUid);
            if (!item) continue;

            double sx = ReadDouble(item, 0x518);
            double sy = ReadDouble(item, 0x520);
            if (sx <= 0.0) sx = 1.0;
            if (sy <= 0.0) sy = 1.0;

            std::vector<StringType> targets;
            auto it = G_Settings.mapping.find(m.itemType);
            if (it != G_Settings.mapping.end()) {
                targets = it->second;
            } else if (G_Settings.unknownItemAction == STR("MoveToRoot")) {
                if (rootContainerValid) {
                    targets.push_back(G_Settings.rootContainer);
                } else if (defaultInventory && defaultInventory != m.srcJsi) {
                    targets.push_back(STR("__INVENTORY__"));
                }
            }
            if (targets.empty()) { ++movedFail; continue; }

            UObject* chosenDst = nullptr;
            int32_t chosenSlot = -1;
            StringType chosenId;

            for (auto& t : targets) {
                if (t == STR("__INVENTORY__")) {
                    if (defaultInventory && defaultInventory != m.srcJsi) {
                        int32_t s = TryGetEmptySlotNative(defaultInventory, sx, sy);
                        if (s >= 0) {
                            chosenDst = defaultInventory;
                            chosenSlot = s;
                            chosenId = STR("Inventory");
                        }
                    }
                    continue;
                }

                auto jt = byItemId.find(t);
                if (jt == byItemId.end()) continue;
                int checked = 0;
                for (UObject* cand : jt->second) {
                    if (checked++ >= 5) break;
                    if (cand == m.srcJsi) continue;
                    int32_t s = TryGetEmptySlotNative(cand, sx, sy);
                    if (s >= 0) {
                        chosenDst = cand;
                        chosenSlot = s;
                        chosenId = t;
                        break;
                    }
                }
                if (chosenDst) break;
            }

            if (!chosenDst) {
                Output::send<LogLevel::Verbose>(
                    STR("[Sort] нет места для '{}' ({}) — кандидаты:\n"),
                    m.itemId, m.itemType);
                for (auto& t : targets) {
                    auto jt = byItemId.find(t);
                    if (jt == byItemId.end()) {
                        Output::send<LogLevel::Verbose>(
                            STR("[Sort]   '{}' не найден\n"), t);
                        continue;
                    }
                    int shown = 0;
                    for (UObject* cand : jt->second) {
                        if (shown++ >= 3) break;
                        Output::send<LogLevel::Verbose>(
                            STR("[Sort]   cand {} outer='{}' WSlots={} items={}\n"),
                            SafeName(cand),
                            SafeName(cand->GetOuterPrivate()),
                            CountWSlots(cand),
                            [&]{ SimpleTArray* ia = reinterpret_cast<SimpleTArray*>(
                                    reinterpret_cast<uint8_t*>(cand) + 0x490);
                                 return ia ? ia->Num : -1; }());
                    }
                }
                ++movedFail;
                continue;
            }

            Output::send<LogLevel::Verbose>(
                STR("[Sort] MOVE '{}' ({}) from {} -> {} slot={}\n"),
                m.itemId, m.itemType, SafeName(m.srcJsi), chosenId, chosenSlot);

            DoEventOnInventoryAction(jigComp, m.srcJsi, chosenDst,
                                     item, nullptr, chosenSlot, false);

            UObject* still = FindItemWidgetByUID(m.srcJsi, m.itemUid);
            if (still) { ++movedFail; Output::send<LogLevel::Verbose>(STR("[Sort]   FAIL\n")); }
            else       { ++movedOk;   Output::send<LogLevel::Verbose>(STR("[Sort]   OK\n"));   }
        }

        Output::send<LogLevel::Verbose>(
            STR("[Sort] === DONE ok={} fail={} ===\n"), movedOk, movedFail);
    }

    UObject* FindPlayerJigComponent()
    {
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("BP_JigComponent_C"), all);
        for (UObject* c : all) {
            if (!c) continue;
            if (IsPendingKill(c)) continue;
            StringType on = SafeName(c->GetOuterPrivate());
            if (on.find(STR("BP_PlayerCharacter")) != std::wstring::npos) return c;
        }
        return nullptr;
    }

    void TryInstall()
    {
        if (!G_HookRegistered) {
            Hook::RegisterProcessLocalScriptFunctionPreCallback(
                [](UObject* Context, FFrame& Stack, void* Result) {
                    PLSFPreHook(Context, Stack, Result);
                });
            G_HookRegistered = true;
            Output::send<LogLevel::Verbose>(STR("[AutoSortLoot] PLSF hook установлен.\n"));
        }
        if (G_Settings.hotkey == 0) { G_HotkeyWasDown = false; return; }
        bool down = (GetAsyncKeyState(G_Settings.hotkey) & 0x8000) != 0;
        if (down && !G_HotkeyWasDown && !G_ScannerRunning) {
            G_ScannerRunning = true;
            Output::send<LogLevel::Verbose>(STR("[Scanner] === SCAN START ===\n"));
            UObject* jigComp = FindPlayerJigComponent();
            if (!jigComp) {
                Output::send<LogLevel::Verbose>(STR("[Scanner] jigComp не найден\n"));
            } else {
                SortLoot(jigComp);
            }
            Output::send<LogLevel::Verbose>(STR("[Scanner] === SCAN END ===\n"));
            G_ScannerRunning = false;
        }
        G_HotkeyWasDown = down;
    }

    void Install()
    {
        Output::send<LogLevel::Verbose>(STR("[AutoSortLoot] Install()\n"));
        InstallConfig();
    }

    void Uninstall()
    {
        Output::send<LogLevel::Verbose>(STR("[AutoSortLoot] Uninstall()\n"));
    }
}