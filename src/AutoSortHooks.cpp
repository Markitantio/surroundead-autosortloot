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
#include <fstream>

using namespace RC;
using namespace RC::Unreal;

namespace AutoSortHooks
{
    // =================================================================
    // ============================ CONFIG =============================
    // =================================================================

    struct SortSettings {
        int hotkey = VK_F9;
        bool verboseLogs = false;
        StringType unknownItemAction = STR("Skip"); // "Skip" | "MoveToRoot"
        StringType rootContainer = STR("LargeBlackMilitaryBackpack");
        std::map<StringType, std::vector<StringType>> mapping;
    };

    static SortSettings G_Settings;
    static StringType  G_ConfigPath;

    // --- UTF-8 helpers (для корректной работы с русским в комментариях)
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
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(),
                                    nullptr, 0);
        std::wstring out(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(),
                            out.data(), n);
        return out;
    }

    static void TrimString(StringType& s)
    {
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
        return s == L"true" || s == L"1" || s == L"yes" || s == L"on";
    }

    static int ParseHotkey(const StringType& raw)
    {
        StringType s = raw;
        TrimString(s);
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
                TrimString(cur);
                if (!cur.empty()) out.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        TrimString(cur);
        if (!cur.empty()) out.push_back(cur);
        return out;
    }

    static bool FileExists(const StringType& path)
    {
        DWORD attr = GetFileAttributesW(path.c_str());
        return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
    }

    // Конфиг лежит рядом с папкой dlls/ у мода:
    //   .../ue4ss/Mods/AutoSortLoot/dlls/main.dll
    //   .../ue4ss/Mods/AutoSortLoot/AutoSortLoot.ini
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
        p = p.substr(0, p1);                       // .../dlls/
        size_t p2 = p.find_last_of(L"\\/");
        if (p2 == StringType::npos) return StringType(L"AutoSortLoot.ini");
        p = p.substr(0, p2);                       // .../AutoSortLoot/
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
        content += L";\r\n";
        content += L"; [Settings]  - основные настройки\r\n";
        content += L"; [Mapping]   - правила распределения предметов\r\n";
        content += L";\r\n\r\n";

        content += L"[Settings]\r\n";
        content += L"; Клавиша сортировки.\r\n";
        content += L"; Поддерживается: F1..F12, 0..9, A..Z,\r\n";
        content += L";   INSERT, HOME, END, DELETE, PAGEUP, PAGEDOWN, SPACE, TAB\r\n";
        content += L"Hotkey=F9\r\n\r\n";

        content += L"; Подробные логи в UE4SS.log (true/false).\r\n";
        content += L"; false = только итог и результат по каждому предмету.\r\n";
        content += L"VerboseLogs=false\r\n\r\n";

        content += L"; Что делать с предметами, для которых нет правила в [Mapping]:\r\n";
        content += L";   Skip       = не трогать (по умолчанию)\r\n";
        content += L";   MoveToRoot = переместить в RootContainer\r\n";
        content += L"UnknownItemAction=Skip\r\n\r\n";

        content += L"; Имя корневого контейнера для UnknownItemAction=MoveToRoot\r\n";
        content += L"RootContainer=LargeBlackMilitaryBackpack\r\n\r\n";

        content += L"[Mapping]\r\n";
        content += L"; Формат: <ItemType>=<Container1>,<Container2>,...\r\n";
        content += L"; Мод перебирает контейнеры по порядку и кладёт предмет в первый,\r\n";
        content += L"; у которого есть свободный слот. Если ни одного нет - предмет остаётся.\r\n";
        content += L";\r\n";
        content += L"; Префикс \"Jig.ItemType.\" можно опускать.\r\n";
        content += L"; Полный список типов см. в JSON-дампе игры (поле ItemType).\r\n\r\n";

        content += L"; --- Медицина ---\r\n";
        content += L"MedicalConsumable=MedBag\r\n\r\n";

        content += L"; --- Еда и питьё ---\r\n";
        content += L"FoodConsumable=LunchBox\r\n";
        content += L"DrinkConsumable=LunchBox\r\n\r\n";

        content += L"; --- Оружие ---\r\n";
        content += L"MainFirearm=WeaponsCase\r\n";
        content += L"Sidearm=WeaponsCase\r\n";
        content += L"Melee=WeaponsCase\r\n\r\n";

        content += L"; --- Боеприпасы и обвесы ---\r\n";
        content += L"Ammunition=AmmoTin,WeaponsCase\r\n";
        content += L"WeaponAttachment=AmmoTin,WeaponsCase\r\n";
        content += L"EquipmentAttachment=AmmoTin,WeaponsCase\r\n";
        content += L"Throwable=AmmoTin,WeaponsCase\r\n\r\n";

        content += L"; --- Материалы и инструменты ---\r\n";
        content += L"Material=LargeToolbox,SmallToolbox\r\n";
        content += L"Tool=LargeToolbox,SmallToolbox\r\n\r\n";

        content += L"; --- Ценности ---\r\n";
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
            StringType line = U8ToW(raw);
            TrimString(line);
            if (line.empty()) continue;
            if (line[0] == L';' || line[0] == L'#') continue;

            if (line.front() == L'[' && line.back() == L']') {
                curSection = line.substr(1, line.size() - 2);
                TrimString(curSection);
                continue;
            }

            size_t eq = line.find(L'=');
            if (eq == StringType::npos) continue;

            StringType key   = line.substr(0, eq);
            StringType value = line.substr(eq + 1);
            TrimString(key);
            TrimString(value);

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

    // =================================================================
    // ========================== UTILITIES ============================
    // =================================================================

    bool G_HookRegistered = false;
    bool G_ScannerRunning = false;
    bool G_HotkeyWasDown = false;

    // verbose-логи только когда включён флаг
    #define SORT_V(...) do { if (G_Settings.verboseLogs) \
        Output::send<LogLevel::Verbose>(__VA_ARGS__); } while(0)

    static StringType SafeName(UObject* o)
    { return o ? o->GetName() : StringType(STR("<null>")); }

    struct SimpleTArray { void* Data; int32_t Num; int32_t Max; };

    static int32_t ReadI32(void* b, int32_t o)
    { return *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(b) + o); }
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

    bool GetContainerUid(UObject* jsi, uint8_t outUid[16])
    {
        if (!jsi) return false;
        std::memcpy(outUid, reinterpret_cast<uint8_t*>(jsi) + 0x37C, 16);
        for (int i = 0; i < 16; ++i) if (outUid[i]) return true;
        return false;
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

    static bool IsTrueContainerType(const StringType& itemType)
    {
        return itemType == STR("Jig.ItemType.Container")
            || itemType == STR("Jig.ItemType.Backpack");
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

        std::vector<UObject*> allJsi;
        UObjectGlobals::FindAllOf(STR("JSIContainer_C"), allJsi);

        std::map<StringType, std::vector<UObject*>> byItemId;
        int skipNonContainerJsi = 0;

        for (UObject* j : allJsi) {
            if (!j) continue;
            uint8_t uid[16];
            if (!GetContainerUid(j, uid)) { ++skipNonContainerJsi; continue; }
            StringType cType = GetContainerItemType(j);
            if (!IsTrueContainerType(cType)) { ++skipNonContainerJsi; continue; }
            StringType itemId = GetContainerItemId(j);
            if (itemId.empty()) { ++skipNonContainerJsi; continue; }
            byItemId[itemId].push_back(j);
        }

        SORT_V(STR("[Sort] контейнеров: {} (skip non-container: {})\n"),
               (int)byItemId.size(), skipNonContainerJsi);
        for (auto& kv : byItemId) {
            SORT_V(STR("[Sort]   {} x {}\n"), kv.first, (int)kv.second.size());
        }

        // ===== План =====
        std::vector<PlannedMove> plan;
        int skipNoRule = 0, skipInPlace = 0, skipNoTarget = 0, skipContainer = 0;

        for (auto& kv : byItemId) {
            StringType srcItemId = kv.first;
            for (UObject* srcJsi : kv.second) {
                if (!srcJsi) continue;
                SimpleTArray* items = reinterpret_cast<SimpleTArray*>(
                    reinterpret_cast<uint8_t*>(srcJsi) + 0x490);
                if (!items || items->Num <= 0 || !items->Data) continue;
                UObject** arr = reinterpret_cast<UObject**>(items->Data);
                for (int32_t i = 0; i < items->Num; ++i) {
                    UObject* iw = arr[i];
                    if (!iw) continue;

                    StringType iType = GetItemTypeOf(iw);

                    // Пропускаем только сами контейнеры-предметы
                    if (!iType.empty() && IsTrueContainerType(iType)) {
                        ++skipContainer; continue;
                    }

                    // Определяем список целей
                    std::vector<StringType> targets;
                    auto it = G_Settings.mapping.find(iType);
                    if (it != G_Settings.mapping.end()) {
                        targets = it->second;
                    } else {
                        // Правила нет
                        if (G_Settings.unknownItemAction == STR("MoveToRoot")
                            && !G_Settings.rootContainer.empty()
                            && srcItemId != G_Settings.rootContainer)
                        {
                            targets.push_back(G_Settings.rootContainer);
                        } else {
                            ++skipNoRule; continue;
                        }
                    }
                    if (targets.empty()) { ++skipNoRule; continue; }

                    // Уже в целевом?
                    if (ListContains(targets, srcItemId)) { ++skipInPlace; continue; }

                    // Есть ли хоть один подходящий контейнер у игрока?
                    bool hasAny = false;
                    for (auto& t : targets) {
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
            } else if (G_Settings.unknownItemAction == STR("MoveToRoot")
                       && !G_Settings.rootContainer.empty()) {
                targets.push_back(G_Settings.rootContainer);
            }
            if (targets.empty()) { ++movedFail; continue; }

            UObject* chosenDst = nullptr;
            int32_t chosenSlot = -1;
            StringType chosenId;

            for (auto& t : targets) {
                auto jt = byItemId.find(t);
                if (jt == byItemId.end()) continue;
                for (UObject* cand : jt->second) {
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
                    STR("[Sort] нет места для '{}' ({})\n"), m.itemId, m.itemType);
                ++movedFail;
                continue;
            }

            Output::send<LogLevel::Verbose>(
                STR("[Sort] MOVE '{}' ({}) -> {} slot={}\n"),
                m.itemId, m.itemType, chosenId, chosenSlot);

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