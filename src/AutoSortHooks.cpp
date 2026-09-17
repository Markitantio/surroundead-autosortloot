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
#include <thread>
#include <chrono>

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
        bool wakeClosedContainers = true;
        bool reservedFromEquipped = true;        // v14
        std::set<StringType> reservedItems;      // v14 (ручной список)
        std::map<StringType, std::vector<StringType>> mapping;
    };

    static SortSettings G_Settings;
    static StringType  G_ConfigPath;

    static std::string WToU8(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string out(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
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
        while (!s.empty() && (s.front() == 0xFEFF || s.front() == 0x200B || s.front() == 0x00A0))
            s.erase(s.begin());
        auto isSpace = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; };
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
        StringType s = raw; StripBomAndTrim(s);
        for (auto& c : s) c = std::towupper(c);
        if (s.empty()) return 0;
        if (s.size() >= 2 && s[0] == L'F') { int n = _wtoi(s.c_str() + 1); if (n >= 1 && n <= 24) return VK_F1 + (n - 1); }
        if (s.size() == 1) { wchar_t c = s[0]; if (c >= L'0' && c <= L'9') return (int)c; if (c >= L'A' && c <= L'Z') return (int)c; }
        if (s == L"INSERT") return VK_INSERT;
        if (s == L"HOME") return VK_HOME;
        if (s == L"END") return VK_END;
        if (s == L"DELETE" || s == L"DEL") return VK_DELETE;
        if (s == L"PAGEUP" || s == L"PGUP") return VK_PRIOR;
        if (s == L"PAGEDOWN" || s == L"PGDN") return VK_NEXT;
        if (s == L"SPACE") return VK_SPACE;
        if (s == L"TAB") return VK_TAB;
        return 0;
    }

    static std::vector<StringType> ParseCommaList(const StringType& raw)
    {
        std::vector<StringType> out;
        StringType cur;
        for (wchar_t c : raw) {
            if (c == L',') { StripBomAndTrim(cur); if (!cur.empty()) out.push_back(cur); cur.clear(); }
            else cur.push_back(c);
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
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&GetConfigPath), &hMod);
        if (!hMod) return StringType(L"AutoSortLoot.ini");
        wchar_t path[MAX_PATH] = {};
        DWORD n = GetModuleFileNameW(hMod, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return StringType(L"AutoSortLoot.ini");
        StringType p(path);
        size_t p1 = p.find_last_of(L"\\/"); if (p1 == StringType::npos) return StringType(L"AutoSortLoot.ini");
        p = p.substr(0, p1);
        size_t p2 = p.find_last_of(L"\\/"); if (p2 == StringType::npos) return StringType(L"AutoSortLoot.ini");
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
        content += L"RootContainer=LargeBlackMilitaryBackpack\r\n";
        content += L"WakeClosedContainers=true\r\n";
        content += L"; Автодетект: не сортировать патроны/магазины,\r\n";
        content += L"; подходящие к экипированному оружию (Primary/Secondary/Sidearm).\r\n";
        content += L"ReservedFromEquipped=true\r\n";
        content += L"; Ручной список ItemId, которые НЕ сортируются (патроны, магазины, обвесы).\r\n";
        content += L"; Разделитель: запятая. Пример: 556Ammo,762Mag\r\n";
        content += L"ReservedItems=\r\n\r\n";

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
            if (raw.size() >= 3 && (uint8_t)raw[0] == 0xEF && (uint8_t)raw[1] == 0xBB && (uint8_t)raw[2] == 0xBF)
                raw.erase(0, 3);
            if (raw.size() >= 2 && (uint8_t)raw[0] == 0xFF && (uint8_t)raw[1] == 0xFE) raw.erase(0, 2);

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
            StringType key = line.substr(0, eq), value = line.substr(eq + 1);
            StripBomAndTrim(key); StripBomAndTrim(value);

            if (curSection == L"Settings") {
                if (key == L"Hotkey") { int vk = ParseHotkey(value); if (vk) G_Settings.hotkey = vk; }
                else if (key == L"VerboseLogs") G_Settings.verboseLogs = ParseBool(value);
                else if (key == L"UnknownItemAction") G_Settings.unknownItemAction = value;
                else if (key == L"RootContainer") G_Settings.rootContainer = value;
                else if (key == L"WakeClosedContainers") G_Settings.wakeClosedContainers = ParseBool(value);
                else if (key == L"ReservedFromEquipped") G_Settings.reservedFromEquipped = ParseBool(value);
                else if (key == L"ReservedItems") {
                    auto list = ParseCommaList(value);
                    for (auto& x : list) G_Settings.reservedItems.insert(x);
                }
            } else if (curSection == L"Mapping") {
                StringType fullKey = key;
                if (fullKey.find(L'.') == StringType::npos) fullKey = StringType(L"Jig.ItemType.") + key;
                auto list = ParseCommaList(value);
                if (!list.empty()) G_Settings.mapping[fullKey] = list;
            }
        }
        return true;
    }

    static void InstallConfig()
    {
        G_ConfigPath = GetConfigPath();
        Output::send<LogLevel::Verbose>(STR("[AutoSortLoot] config path: {}\n"), G_ConfigPath);
        if (!FileExists(G_ConfigPath)) {
            Output::send<LogLevel::Verbose>(STR("[AutoSortLoot] config not found, creating default\n"));
            SaveDefaultConfig(G_ConfigPath);
        }
        LoadConfigFromFile(G_ConfigPath);
        Output::send<LogLevel::Verbose>(
            STR("[AutoSortLoot] config loaded: hotkey=0x{:X}, rules={}, verbose={}, wake={}, reservedEq={}, reservedManual={}\n"),
            G_Settings.hotkey, (int)G_Settings.mapping.size(),
            G_Settings.verboseLogs ? 1 : 0, G_Settings.wakeClosedContainers ? 1 : 0,
            G_Settings.reservedFromEquipped ? 1 : 0,
            (int)G_Settings.reservedItems.size());
    }

    // ================== UTILITIES ==================

    bool G_HookRegistered = false;
    bool G_ScannerRunning = false;
    bool G_HotkeyWasDown = false;

    #define SORT_V(...) do { if (G_Settings.verboseLogs) Output::send<LogLevel::Verbose>(__VA_ARGS__); } while(0)
    #define SORT_ALWAYS(...) Output::send<LogLevel::Verbose>(__VA_ARGS__)

    static StringType SafeName(UObject* o) { return o ? o->GetName() : StringType(STR("<null>")); }

    struct SimpleTArray { void* Data; int32_t Num; int32_t Max; };

    static int32_t ReadI32(void* b, int32_t o) { return *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(b) + o); }
    static uint32_t ReadU32(void* b, int32_t o) { return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(b) + o); }
    static double ReadDouble(void* b, int32_t o) { return *reinterpret_cast<double*>(reinterpret_cast<uint8_t*>(b) + o); }
    static void* ReadPtr(void* b, int32_t o) { return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(b) + o); }
    static StringType ReadFNameAt(void* b, int32_t o)
    {
        FName* f = reinterpret_cast<FName*>(reinterpret_cast<uint8_t*>(b) + o);
        return f->ToString();
    }

    UFunction* GetFunctionFromFrame(FFrame& Stack) { UFunction* F = Stack.Node(); return F ? F : Stack.CurrentNativeFunction(); }
    void PLSFPreHook(UObject* Context, FFrame& Stack, void* Result) { (void)Context; (void)Stack; (void)Result; }

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
        void* da = ReadPtr(itemWidget, 0x4F0); if (!da) return StringType(STR(""));
        return ReadFNameAt(da, 0x30);
    }
    StringType GetItemTypeOf(UObject* itemWidget)
    {
        if (!itemWidget) return StringType(STR(""));
        void* da = ReadPtr(itemWidget, 0x4F0); if (!da) return StringType(STR(""));
        return ReadFNameAt(da, 0x68);
    }
    StringType GetContainerItemId(UObject* jsi)
    {
        if (!jsi) return StringType(STR(""));
        void* sm = ReadPtr(jsi, 0x6A0); if (!sm) return StringType(STR(""));
        void* da = ReadPtr(sm, 0x4F0); if (!da) return StringType(STR(""));
        return ReadFNameAt(da, 0x30);
    }
    StringType GetContainerItemType(UObject* jsi)
    {
        if (!jsi) return StringType(STR(""));
        void* sm = ReadPtr(jsi, 0x6A0); if (!sm) return StringType(STR(""));
        void* da = ReadPtr(sm, 0x4F0); if (!da) return StringType(STR(""));
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

    static int32_t CountWSlots(UObject* jsi)
    {
        if (!jsi) return 0;
        SimpleTArray* arr = reinterpret_cast<SimpleTArray*>(reinterpret_cast<uint8_t*>(jsi) + 0x3A8);
        if (!arr || arr->Num <= 0 || !arr->Data) return 0;
        return arr->Num;
    }
    static int32_t RawWSlotsNum(UObject* jsi)
    {
        if (!jsi) return 0;
        SimpleTArray* arr = reinterpret_cast<SimpleTArray*>(reinterpret_cast<uint8_t*>(jsi) + 0x3A8);
        return (arr && arr->Data) ? arr->Num : 0;
    }
    static int32_t GetContainerNumCols(UObject* j) { return j ? ReadI32(j, 0x39C) : 0; }
    static int32_t GetContainerNumRows(UObject* j) { return j ? ReadI32(j, 0x3A0) : 0; }
    static bool GetContainerInitialized(UObject* j) { return j && *reinterpret_cast<bool*>(reinterpret_cast<uint8_t*>(j) + 0x378); }
    static double GetContainerSlotSizeX(UObject* j) { return j ? ReadDouble(j, 0x3C8) : 0.0; }
    static double GetContainerSlotSizeY(UObject* j) { return j ? ReadDouble(j, 0x3D0) : 0.0; }
    static double GetContainerMaxWeight(UObject* j) { return j ? ReadDouble(j, 0x5A8) : 0.0; }
    static bool GetContainerIsMain(UObject* j) { return j && *reinterpret_cast<bool*>(reinterpret_cast<uint8_t*>(j) + 0x800); }
    static bool GetContainerIsPartSpecial(UObject* j) { return j && *reinterpret_cast<bool*>(reinterpret_cast<uint8_t*>(j) + 0x711); }

    // =========================================================================
    // v15: SAFE READ HELPERS (POD-only, no C++ objects inside __try)
    // =========================================================================

    struct SafeArrView { void* data; int32_t num; int32_t max; int32_t status; }; // 0=ok,1=invalid,2=crash
    struct SafePtrView { void* ptr; int32_t status; };                          // 0=ok,1=null,2=crash

    __declspec(noinline) static SafeArrView SafeReadArrayHeader(void* base, int off)
    {
        SafeArrView r{nullptr, 0, 0, 1};
        __try {
            if (!base) return r;
            uint8_t* p = (uint8_t*)base + off;
            void* d = *(void**)p;
            int32_t n = *(int32_t*)(p + 8);
            int32_t m = *(int32_t*)(p + 12);
            r.data = d; r.num = n; r.max = m;
            if (!d || n <= 0 || n > 100000 || m < n || m > 1000000) { r.status = 1; return r; }
            r.status = 0;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            r.status = 2;
        }
        return r;
    }

    __declspec(noinline) static SafePtrView SafeReadPtr(void* base, int off)
    {
        SafePtrView r{nullptr, 1};
        __try {
            if (!base) return r;
            uint8_t* p = (uint8_t*)base + off;
            void* v = *(void**)p;
            r.ptr = v;
            r.status = v ? 0 : 1;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            r.status = 2;
        }
        return r;
    }

    __declspec(noinline) static int SafeLooksLikeUObject(void* p)
    {
        if (!p) return 0;
        uintptr_t u = (uintptr_t)p;
        if ((u & 7) != 0) return 0;
        if (u < 0x10000 || u > 0x7FFFFFFFFFFFULL) return 0;
        __try {
            void* vtable = *(void**)p;
            uintptr_t vt = (uintptr_t)vtable;
            if (vt < 0x10000 || vt > 0x7FFFFFFFFFFFULL) return 0;
            void* cls = *(void**)((uint8_t*)p + 0x10);
            uintptr_t cu = (uintptr_t)cls;
            if (cu < 0x10000 || cu > 0x7FFFFFFFFFFFULL) return 0;
            return 1;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // v12: WAKE PHASE (без изменений)
    struct ContainerDiag {
        UObject* jsi;
        StringType outerName, scName, itemId;
        int32_t cols, rows, wslotsNum;
        bool initialized;
        double slotSizeX, slotSizeY, maxWeight;
        bool isMain, isPartSpecial, needWake;
    };

    static UObject* FindOwningSpecialContainer(UObject* jsi)
    {
        if (!jsi) return nullptr;
        UObject* outer = jsi->GetOuterPrivate(); if (!outer) return nullptr;
        UObject* candidate = outer->GetOuterPrivate(); if (!candidate) return nullptr;
        UClass* cls = candidate->GetClassPrivate(); if (!cls) return nullptr;
        StringType cn = cls->GetName();
        if (cn.find(STR("SC_")) == 0) return candidate;
        return nullptr;
    }

    static ContainerDiag DiagnoseContainer(UObject* jsi)
    {
        ContainerDiag d{};
        d.jsi = jsi;
        d.outerName = SafeName(jsi ? jsi->GetOuterPrivate() : nullptr);
        d.itemId = GetContainerItemId(jsi);
        d.cols = GetContainerNumCols(jsi);
        d.rows = GetContainerNumRows(jsi);
        d.wslotsNum = RawWSlotsNum(jsi);
        d.initialized = GetContainerInitialized(jsi);
        d.slotSizeX = GetContainerSlotSizeX(jsi);
        d.slotSizeY = GetContainerSlotSizeY(jsi);
        d.maxWeight = GetContainerMaxWeight(jsi);
        d.isMain = GetContainerIsMain(jsi);
        d.isPartSpecial = GetContainerIsPartSpecial(jsi);
        UObject* sc = FindOwningSpecialContainer(jsi);
        if (sc) { UClass* cls = sc->GetClassPrivate(); d.scName = cls ? cls->GetName() : SafeName(sc); }
        d.needWake = false;
        if (d.wslotsNum == 0) d.needWake = true;
        else if (d.cols > 0 && d.rows > 0 && d.wslotsNum != d.cols * d.rows) d.needWake = true;
        else if (d.slotSizeX <= 0.0 || d.slotSizeY <= 0.0) d.needWake = true;
        return d;
    }

    static StringType DiagToString(const ContainerDiag& d)
    {
        StringType s;
        s += STR("cols="); s += std::to_wstring(d.cols);
        s += STR(" rows="); s += std::to_wstring(d.rows);
        s += STR(" WSlots="); s += std::to_wstring(d.wslotsNum);
        s += STR(" Init="); s += (d.initialized ? L"1" : L"0");
        s += STR(" SlotSize=(");
        s += std::to_wstring((int)d.slotSizeX); s += STR(",");
        s += std::to_wstring((int)d.slotSizeY); s += STR(")");
        s += STR(" MaxW="); s += std::to_wstring((int)d.maxWeight);
        if (d.isMain) s += STR(" Main");
        if (d.isPartSpecial) s += STR(" PartSpecial");
        return s;
    }

    static bool IsPendingKill(UObject* o);  // fwd

    static const wchar_t* WakeMethodName(int m)
    {
        switch (m) {
            case 1: return L"ForceInitJsi";
            case 2: return L"PreInitSC";
            case 3: return L"ForceInitSC";
            case 4: return L"Initialize";
            case 5: return L"ReInit";
            default: return L"nothing";
        }
    }

    static int TryWakeContainer(UObject* jsi, int32_t cols, int32_t rows)
    {
        if (!jsi || IsPendingKill(jsi)) return 0;
        UClass* jsiCls = jsi->GetClassPrivate();
        if (!jsiCls) return 0;
        UObject* sc = FindOwningSpecialContainer(jsi);

        if (UFunction* fn = jsiCls->GetFunctionByName(STR("ForceInitSpecialcontainer"))) {
            uint8_t buf[0x40] = {}; jsi->ProcessEvent(fn, buf); return 1;
        }
        if (sc) {
            UClass* scCls = sc->GetClassPrivate();
            if (scCls) {
                if (UFunction* fn = scCls->GetFunctionByName(STR("PreInitSpecialContainer"))) {
                    uint8_t buf[0x40] = {}; sc->ProcessEvent(fn, buf); return 2;
                }
            }
        }
        if (sc) {
            UClass* scCls = sc->GetClassPrivate();
            if (scCls) {
                if (UFunction* fn = scCls->GetFunctionByName(STR("ForceInitSpecialcontainer"))) {
                    uint8_t buf[0x40] = {}; sc->ProcessEvent(fn, buf); return 3;
                }
            }
        }
        if (UFunction* fn = jsiCls->GetFunctionByName(STR("Initialize"))) {
            uint8_t buf[0x40] = {}; buf[0] = 0; jsi->ProcessEvent(fn, buf); return 4;
        }
        if (cols > 0 && rows > 0) {
            if (UFunction* fn = jsiCls->GetFunctionByName(STR("Re-Init"))) {
                uint8_t buf[0x40] = {};
                *reinterpret_cast<int32_t*>(buf + 0x00) = cols;
                *reinterpret_cast<int32_t*>(buf + 0x04) = rows;
                jsi->ProcessEvent(fn, buf); return 5;
            }
        }
        return 0;
    }

    // =========================================================================
    // FILTERS
    // =========================================================================

    static bool IsPendingKill(UObject* o)
    {
        if (!o) return true;
        uint32_t flags = ReadU32(o, 0x08);
        return (flags & 0x42000000u) != 0;
    }

    static bool IsTrueContainerType(const StringType& itemType)
    {
        return itemType == STR("Jig.ItemType.Container")
            || itemType == STR("Jig.ItemType.Backpack");
    }

    // v16: Если сам контейнер — настоящий (Container/Backpack), то он НЕ под-контейнер.
    // Также не под-контейнер, если SlotMother держит Container/Backpack.
    static bool IsChildOfNonContainerItem(UObject* jsi)
    {
        if (!jsi) return false;

        // Сам контейнер — рюкзак/ящик → это валидный контейнер, не под-контейнер
        StringType cItemType = GetContainerItemType(jsi);
        if (IsTrueContainerType(cItemType)) return false;

        void* sm = ReadPtr(jsi, 0x6A0);
        if (!sm) return false;
        void* da = ReadPtr(sm, 0x4F0);
        if (!da) return false;
        StringType smType = ReadFNameAt(da, 0x68);
        if (smType.empty()) return false;

        // ItemType слота-хозяина — тоже рюкзак/ящик → не под-контейнер
        if (IsTrueContainerType(smType)) return false;

        StringType cmType;
        void* cm = ReadPtr(sm, 0x3F8);
        if (cm) cmType = ReadFNameAt(cm, 0x38C);
        if (cmType == STR("Jig.ContainerType.EquipTo")) return true;
        return false;
    }

    // v16: Рюкзак/ящик НЕ считается "экипировкой" — его содержимое надо сортировать,
    // даже если он физически лежит в слоте CBackpack с ContainerType=EquipTo.
    static bool IsContainerEquipped(UObject* jsi)
    {
        if (!jsi) return false;

        StringType cItemType = GetContainerItemType(jsi);
        if (IsTrueContainerType(cItemType)) return false;

        StringType ctype = GetContainerTypeTag(jsi);
        if (ctype == STR("Jig.ContainerType.EquipTo")) return true;
        void* sm = ReadPtr(jsi, 0x6A0);
        if (sm) {
            void* cm = ReadPtr(sm, 0x3F8);
            if (cm) {
                StringType cct = ReadFNameAt(cm, 0x38C);
                if (cct == STR("Jig.ContainerType.EquipTo")) return true;
            }
        }
        return false;
    }

    static std::set<std::string> GetActiveUidSet(UObject* jigComp)
    {
        std::set<std::string> uids;
        if (!jigComp) return uids;
        SimpleTArray* arr = reinterpret_cast<SimpleTArray*>(reinterpret_cast<uint8_t*>(jigComp) + 0xC0);
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
            chain += nm; chain += STR("/"); chain += cn;
        }
        return chain;
    }

    static bool ChainContainsAny(const StringType& chain, const wchar_t* const* keys)
    {
        for (size_t i = 0; keys[i]; ++i) if (chain.find(keys[i]) != StringType::npos) return true;
        return false;
    }

    static bool IsWorldContainer(UObject* jsi)
    {
        if (!jsi) return false;
        static const wchar_t* WORLD_KEYS[] = {
            L"W_LargeLootContainerUI", L"W_LootContainerUI", L"W_DeadPlayerLootUI",
            L"W_VicinityLootUI", L"Container_CompoundCrate", L"Container_POICrate",
            L"Container_DeadPlayerLoot", L"AirdropContainer_", L"Master_AirdropContainer",
            L"BP_LootContainer", L"BP_LootContainerWidget", nullptr
        };
        return ChainContainsAny(BuildOuterChain(jsi, 12), WORLD_KEYS);
    }

    static bool IsBaseValid(UObject* jsi, const std::set<std::string>& activeUids)
    {
        if (!jsi) return false;
        if (IsPendingKill(jsi)) return false;
        uint8_t uid[16];
        if (!GetContainerUid(jsi, uid)) return false;
        if (activeUids.find(std::string(reinterpret_cast<char*>(uid), 16)) == activeUids.end()) return false;
        if (IsChildOfNonContainerItem(jsi)) return false;
        if (IsWorldContainer(jsi)) return false;
        return true;
    }
    static bool IsValidSourceContainer(UObject* jsi, const std::set<std::string>& activeUids)
    {
        if (!IsBaseValid(jsi, activeUids)) return false;
        if (IsContainerEquipped(jsi)) return false;
        return true;
    }
    static bool IsValidTargetContainer(UObject* jsi, const std::set<std::string>& activeUids)
    {
        return IsBaseValid(jsi, activeUids);
    }

    static UObject* FindInventoryContainer(const std::vector<UObject*>& sources)
    {
        for (UObject* j : sources) {
            if (!j) continue;
            if (GetContainerTypeTag(j) == STR("Jig.ContainerType.Inventory")) return j;
        }
        return nullptr;
    }

    static bool ListContains(const std::vector<StringType>& lst, const StringType& v)
    {
        for (auto& x : lst) if (x == v) return true;
        return false;
    }

    UObject* FindItemWidgetByUID(UObject* jsi, const uint8_t* uid)
    {
        if (!jsi) return nullptr;
        SimpleTArray* arr = reinterpret_cast<SimpleTArray*>(reinterpret_cast<uint8_t*>(jsi) + 0x490);
        if (!arr || arr->Num <= 0 || !arr->Data) return nullptr;
        UObject** slots = reinterpret_cast<UObject**>(arr->Data);
        for (int32_t i = 0; i < arr->Num; ++i) {
            UObject* s = slots[i]; if (!s) continue;
            if (std::memcmp(reinterpret_cast<uint8_t*>(s) + 0x5A0, uid, 16) == 0) return s;
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

    // =========================================================================
    // v14/v15/v16: RESERVED FROM EQUIPPED WEAPONS
    // =========================================================================

    struct ReservedInfo {
        std::set<StringType> ammoIds;
        std::set<StringType> magIds;
    };

    static UObject* FindJSIContainerByUIDGlobal(const uint8_t* uid)
    {
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("JSIContainer_C"), all);
        for (UObject* c : all) {
            if (!c) continue;
            if (std::memcmp(reinterpret_cast<uint8_t*>(c) + 0x37C, uid, 16) == 0) return c;
        }
        return nullptr;
    }

    // v16: Способ 1 (ContainerSupportedItems @0x1E0) — дампаем сырые 32 байта для диагностики.
    //      Способ 2 (SubContainers @0xC8, stride 0x50) — детальное логирование.
    static ReservedInfo CollectReservedFromEquippedWeapons(UObject* jigComp)
    {
        ReservedInfo r;
        if (!jigComp) return r;

        SafeArrView mcArr = SafeReadArrayHeader(jigComp, 0xC0);
        if (mcArr.status != 0) {
            SORT_ALWAYS(STR("[Reserved] MainJigContainers unreadable (status={}, num={})\n"),
                        mcArr.status, mcArr.num);
            return r;
        }
        uint8_t* mcBase = (uint8_t*)mcArr.data;
        SORT_ALWAYS(STR("[Reserved] MainJigContainers: num={}\n"), mcArr.num);

        int weaponsSeen = 0;
        for (int32_t i = 0; i < mcArr.num; ++i) {
            uint8_t* mc = mcBase + i * 0x50;
            uint8_t mcUid[16]; std::memcpy(mcUid, mc, 16);

            UObject* jsi = FindJSIContainerByUIDGlobal(mcUid);
            if (!jsi) continue;
            StringType ctype = GetContainerTypeTag(jsi);
            if (ctype != STR("Jig.ContainerType.EquipTo")) continue;

            SafeArrView ciArr = SafeReadArrayHeader(mc, 0x40);
            if (ciArr.status != 0) continue;
            uint8_t* ciBase = (uint8_t*)ciArr.data;

            for (int32_t k = 0; k < ciArr.num; ++k) {
                uint8_t* item = ciBase + k * 0xD8;
                SafePtrView daP = SafeReadPtr(item, 0x28);
                if (daP.status != 0 || !SafeLooksLikeUObject(daP.ptr)) continue;
                UObject* da = (UObject*)daP.ptr;

                StringType itemType = ReadFNameAt(da, 0x68);
                if (itemType != STR("Jig.ItemType.MainFirearm") &&
                    itemType != STR("Jig.ItemType.Sidearm") &&
                    itemType != STR("Jig.ItemType.Melee")) continue;

                ++weaponsSeen;
                StringType weaponId = ReadFNameAt(da, 0x30);
                SORT_ALWAYS(STR("[Reserved] equipped weapon: {} ({})\n"), weaponId, itemType);

                // --- Способ 1: ContainerSupportedItems @0x1E0 — сырой дамп 32 байт
                SORT_ALWAYS(STR("[Reserved]   DA@0x1E0 raw: "));
                for (int bi = 0; bi < 8; ++bi) {
                    uint32_t v = *(uint32_t*)((uint8_t*)da + 0x1E0 + bi * 4);
                    SORT_ALWAYS(STR("{:08X} "), v);
                }
                SORT_ALWAYS(STR("\n"));

                SafeArrView supArr = SafeReadArrayHeader(da, 0x1E0);
                SORT_ALWAYS(STR("[Reserved]   sup items: status={} num={} max={} data=0x{:X}\n"),
                            supArr.status, supArr.num, supArr.max, (uintptr_t)supArr.data);
                if (supArr.status == 0) {
                    int okCnt = 0, badCnt = 0;
                    for (int32_t s = 0; s < supArr.num && s < 32; ++s) {
                        SafePtrView sp = SafeReadPtr(supArr.data, s * 8);
                        if (sp.status != 0 || !SafeLooksLikeUObject(sp.ptr)) {
                            ++badCnt;
                            SORT_ALWAYS(STR("[Reserved]     sup[{}] not-uobject (status={}, ptr=0x{:X})\n"),
                                        s, sp.status, (uintptr_t)sp.ptr);
                            continue;
                        }
                        UObject* sup = (UObject*)sp.ptr;
                        StringType supType = ReadFNameAt(sup, 0x68);
                        StringType supId   = ReadFNameAt(sup, 0x30);
                        SORT_ALWAYS(STR("[Reserved]     sup[{}] type='{}' id='{}'\n"), s, supType, supId);
                        if (supType == STR("Jig.ItemType.Ammunition"))            { r.ammoIds.insert(supId); ++okCnt; }
                        else if (supType == STR("Jig.ItemType.WeaponAttachment")) { r.magIds.insert(supId);  ++okCnt; }
                    }
                    SORT_ALWAYS(STR("[Reserved]   sup processed ok={} bad={}\n"), okCnt, badCnt);
                }

                // --- Способ 2: SubContainers @0xC8 (stride 0x50 = S_ReplicatedContainerInfo)
                SafeArrView subArr = SafeReadArrayHeader(item, 0xC8);
                SORT_ALWAYS(STR("[Reserved]   subcontainers: status={} num={}\n"),
                            subArr.status, subArr.num);
                if (subArr.status == 0) {
                    uint8_t* subBase = (uint8_t*)subArr.data;
                    for (int32_t m = 0; m < subArr.num && m < 32; ++m) {
                        uint8_t* sub = subBase + m * 0x50;
                        SafeArrView siArr = SafeReadArrayHeader(sub, 0x40);
                        SORT_ALWAYS(STR("[Reserved]     sub[{}]: siArr status={} num={}\n"),
                                    m, siArr.status, siArr.num);
                        if (siArr.status != 0) continue;
                        uint8_t* siBase = (uint8_t*)siArr.data;
                        int siOk = 0, siBad = 0;
                        for (int32_t n = 0; n < siArr.num && n < 64; ++n) {
                            uint8_t* si = siBase + n * 0xD8;
                            SafePtrView siDaP = SafeReadPtr(si, 0x28);
                            if (siDaP.status != 0 || !SafeLooksLikeUObject(siDaP.ptr)) {
                                ++siBad;
                                SORT_ALWAYS(STR("[Reserved]       item[{}] not-uobject (status={}, ptr=0x{:X})\n"),
                                            n, siDaP.status, (uintptr_t)siDaP.ptr);
                                continue;
                            }
                            ++siOk;
                            UObject* siDa = (UObject*)siDaP.ptr;
                            StringType siType = ReadFNameAt(siDa, 0x68);
                            StringType siId   = ReadFNameAt(siDa, 0x30);
                            SORT_ALWAYS(STR("[Reserved]       loaded type='{}' id='{}'\n"), siType, siId);
                            if (siType == STR("Jig.ItemType.Ammunition"))            r.ammoIds.insert(siId);
                            else if (siType == STR("Jig.ItemType.WeaponAttachment")) r.magIds.insert(siId);
                        }
                        SORT_ALWAYS(STR("[Reserved]     sub[{}] items ok={} bad={}\n"), m, siOk, siBad);
                    }
                }
            }
        }

        SORT_ALWAYS(STR("[Reserved] weapons seen: {}, ammo reserved: {}, mags reserved: {}\n"),
                    weaponsSeen, (int)r.ammoIds.size(), (int)r.magIds.size());
        return r;
    }

    struct PlannedMove {
        uint8_t  itemUid[16];
        UObject* srcJsi;
        StringType itemId;
        StringType itemType;
    };

    void SortLoot(UObject* jigComp)
    {
        SORT_ALWAYS(STR("[Sort] === START ===\n"));

        std::set<std::string> activeUids = GetActiveUidSet(jigComp);
        SORT_ALWAYS(STR("[Sort] active UIDs in MainJigContainers: {}\n"), (int)activeUids.size());

        // v14: собрать reserved
        ReservedInfo reserved;
        if (G_Settings.reservedFromEquipped) {
            reserved = CollectReservedFromEquippedWeapons(jigComp);
        }
        for (auto& x : G_Settings.reservedItems) {
            reserved.ammoIds.insert(x);
            reserved.magIds.insert(x);
        }

        std::vector<UObject*> allJsi;
        UObjectGlobals::FindAllOf(STR("JSIContainer_C"), allJsi);

        std::vector<UObject*> rawCandidates;
        std::vector<UObject*> sources;
        std::map<StringType, std::vector<UObject*>> byItemId;
        std::set<UObject*> equippedJsiSet;
        int skipNonContainerJsi = 0, skipStale = 0;
        // v16: подробные причины отсева для рюкзаков/ящиков
        int skipPk = 0, skipNotActive = 0, skipChildNonCont = 0, skipWorld = 0, skipEquipped = 0;

        for (UObject* j : allJsi) {
            if (!j) continue;
            uint8_t uid[16];
            if (!GetContainerUid(j, uid)) { ++skipNonContainerJsi; continue; }

            // v16: диагностика по каждому Backpack/Container
            if (G_Settings.verboseLogs) {
                StringType cItemType = GetContainerItemType(j);
                if (IsTrueContainerType(cItemType)) {
                    StringType cItemId = GetContainerItemId(j);
                    bool pk = IsPendingKill(j);
                    bool inActive = activeUids.find(std::string(reinterpret_cast<char*>(uid), 16)) != activeUids.end();
                    bool childNon = IsChildOfNonContainerItem(j);
                    bool world = IsWorldContainer(j);
                    bool equip = IsContainerEquipped(j);
                    SORT_V(STR("[Sort] DBG container '{}' name='{}' pk={} inActive={} childNonCont={} world={} equipped={}\n"),
                           cItemId, SafeName(j), pk?1:0, inActive?1:0, childNon?1:0, world?1:0, equip?1:0);
                }
            }

            bool okSource = IsValidSourceContainer(j, activeUids);
            bool okTarget = IsValidTargetContainer(j, activeUids);
            if (!okSource && !okTarget) {
                bool inActive = activeUids.find(std::string(reinterpret_cast<char*>(uid), 16)) != activeUids.end();
                if (!inActive) { ++skipStale; ++skipNotActive; }
                else {
                    ++skipNonContainerJsi;
                    if (IsPendingKill(j)) ++skipPk;
                    else if (IsChildOfNonContainerItem(j)) ++skipChildNonCont;
                    else if (IsWorldContainer(j)) ++skipWorld;
                    else if (IsContainerEquipped(j)) ++skipEquipped;
                }
                continue;
            }

            bool equipped = IsContainerEquipped(j);

            if (okSource) {
                rawCandidates.push_back(j);
                sources.push_back(j);
            }
            if (okTarget) {
                StringType itemId = GetContainerItemId(j);
                StringType itemType = GetContainerItemType(j);
                if (!itemId.empty() && IsTrueContainerType(itemType)) {
                    byItemId[itemId].push_back(j);
                    if (equipped) equippedJsiSet.insert(j);
                }
            }
        }

        SORT_ALWAYS(STR("[Sort] rawCandidates: {} (skip non-container: {}, stale: {}; details pk={} notActive={} childNonCont={} world={} equipped={})\n"),
                    (int)rawCandidates.size(), skipNonContainerJsi, skipStale,
                    skipPk, skipNotActive, skipChildNonCont, skipWorld, skipEquipped);

        // WAKE PHASE
        int wokeForceJsi = 0, wokePreInitSC = 0, wokeForceSC = 0, wokeInit = 0, wokeReInit = 0, wokeNothing = 0;
        int suspiciousCount = 0, fixedCount = 0, stillBrokenCount = 0;
        if (G_Settings.wakeClosedContainers) {
            SORT_ALWAYS(STR("[Sort] === WAKE PHASE ===\n"));
            std::vector<ContainerDiag> suspicious;
            for (UObject* j : rawCandidates) {
                ContainerDiag d = DiagnoseContainer(j);
                SORT_ALWAYS(STR("[Sort] pre '{}' sc='{}' outer='{}' {}\n"),
                            d.itemId, d.scName, d.outerName, DiagToString(d));
                if (d.needWake) { ++suspiciousCount; suspicious.push_back(d); }
            }
            SORT_ALWAYS(STR("[Sort] suspicious: {}\n"), suspiciousCount);
            for (auto& d : suspicious) {
                if (!d.jsi || IsPendingKill(d.jsi)) continue;
                int method = TryWakeContainer(d.jsi, d.cols, d.rows);
                switch (method) {
                    case 1: ++wokeForceJsi; break;
                    case 2: ++wokePreInitSC; break;
                    case 3: ++wokeForceSC; break;
                    case 4: ++wokeInit; break;
                    case 5: ++wokeReInit; break;
                    default: ++wokeNothing; break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                if (IsPendingKill(d.jsi)) {
                    SORT_ALWAYS(STR("[Sort] wake '{}' via={} -> pending kill\n"), d.itemId, WakeMethodName(method));
                    ++stillBrokenCount; continue;
                }
                ContainerDiag after = DiagnoseContainer(d.jsi);
                bool fixed = !after.needWake;
                if (fixed) ++fixedCount; else ++stillBrokenCount;
                SORT_ALWAYS(STR("[Sort] wake '{}' via={} -> {} status={}\n"),
                            after.itemId, WakeMethodName(method), DiagToString(after),
                            fixed ? L"FIXED" : L"STILL-BROKEN");
            }
            SORT_ALWAYS(STR("[Sort] wake summary: suspicious={} fixed={} still={} "
                            "(ForceInitJsi={} PreInitSC={} ForceInitSC={} Initialize={} ReInit={} nothing={})\n"),
                        suspiciousCount, fixedCount, stillBrokenCount,
                        wokeForceJsi, wokePreInitSC, wokeForceSC, wokeInit, wokeReInit, wokeNothing);
            SORT_ALWAYS(STR("[Sort] === WAKE PHASE END ===\n"));
        } else {
            SORT_ALWAYS(STR("[Sort] wake phase: disabled\n"));
        }

        // Финальный фильтр по WSlots
        std::vector<UObject*> sourcesFinal;
        std::map<StringType, std::vector<UObject*>> byItemIdFinal;
        int skipNoSlots = 0;

        for (UObject* j : sources) {
            if (CountWSlots(j) <= 0) { ++skipNoSlots; continue; }
            sourcesFinal.push_back(j);
        }
        for (auto& kv : byItemId) {
            auto& vec = kv.second;
            std::vector<UObject*> tmp;
            for (UObject* j : vec) if (CountWSlots(j) > 0) tmp.push_back(j);
            if (!tmp.empty()) {
                std::sort(tmp.begin(), tmp.end(), [&](UObject* a, UObject* b) {
                    auto canon = [](UObject* c) {
                        UObject* o = c ? c->GetOuterPrivate() : nullptr;
                        if (!o) return false;
                        UClass* cls = o->GetClassPrivate(); if (!cls) return false;
                        StringType cn = cls->GetName();
                        return cn.find(STR("SC_")) == 0;
                    };
                    bool ca = canon(a), cb = canon(b);
                    if (ca != cb) return ca;
                    if (equippedJsiSet.count(a) != equippedJsiSet.count(b))
                        return equippedJsiSet.count(a) > 0;
                    return CountWSlots(a) > CountWSlots(b);
                });
                byItemIdFinal[kv.first] = tmp;
            }
        }
        sources = sourcesFinal;
        byItemId = byItemIdFinal;

        SORT_ALWAYS(STR("[Sort] источников: {} (skip no-slots: {})\n"),
                    (int)sources.size(), skipNoSlots);
        for (auto& kv : byItemId) {
            int eq = 0;
            for (UObject* c : kv.second) if (equippedJsiSet.count(c)) ++eq;
            SORT_ALWAYS(STR("[Sort]   target {} x {} (equipped={}, max WSlots={})\n"),
                        kv.first, (int)kv.second.size(), eq,
                        kv.second.empty() ? 0 : CountWSlots(kv.second[0]));
        }

        UObject* defaultInventory = FindInventoryContainer(sources);
        bool rootContainerValid = (byItemId.find(G_Settings.rootContainer) != byItemId.end());

        SORT_ALWAYS(STR("[Sort] RootContainer='{}' valid={}, inventoryFallback={}\n"),
                    G_Settings.rootContainer, rootContainerValid ? 1 : 0,
                    defaultInventory ? 1 : 0);

        // План
        std::vector<PlannedMove> plan;
        int skipNoRule = 0, skipInPlace = 0, skipNoTarget = 0, skipContainer = 0;
        int skipReservedAmmo = 0, skipReservedMag = 0;

        for (UObject* srcJsi : sources) {
            StringType srcItemId = GetContainerItemId(srcJsi);
            SimpleTArray* items = reinterpret_cast<SimpleTArray*>(reinterpret_cast<uint8_t*>(srcJsi) + 0x490);
            if (!items || items->Num <= 0 || !items->Data) continue;
            UObject** arr = reinterpret_cast<UObject**>(items->Data);

            for (int32_t i = 0; i < items->Num; ++i) {
                UObject* iw = arr[i];
                if (!iw) continue;
                if (IsPendingKill(iw)) continue;

                StringType iType = GetItemTypeOf(iw);
                StringType iId   = GetItemIdOf(iw);
                if (iType.empty()) { ++skipNoRule; continue; }
                if (IsTrueContainerType(iType)) { ++skipContainer; continue; }

                // v14: reserved фильтр
                if (iType == STR("Jig.ItemType.Ammunition")
                    && reserved.ammoIds.count(iId)) {
                    ++skipReservedAmmo; continue;
                }
                if (iType == STR("Jig.ItemType.WeaponAttachment")
                    && reserved.magIds.count(iId)) {
                    ++skipReservedMag; continue;
                }

                std::vector<StringType> targets;
                auto it = G_Settings.mapping.find(iType);
                if (it != G_Settings.mapping.end()) targets = it->second;
                else {
                    if (G_Settings.unknownItemAction == STR("MoveToRoot")) {
                        if (rootContainerValid) targets.push_back(G_Settings.rootContainer);
                        else if (defaultInventory && defaultInventory != srcJsi) targets.push_back(STR("__INVENTORY__"));
                    }
                    if (targets.empty()) { ++skipNoRule; continue; }
                }
                if (!srcItemId.empty() && ListContains(targets, srcItemId)) { ++skipInPlace; continue; }

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
                m.itemId = iId;
                m.itemType = iType;
                plan.push_back(m);
            }
        }

        // dedup
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
        if (plan.size() != beforeDedup)
            SORT_V(STR("[Sort] plan dedup: {} -> {}\n"), (int)beforeDedup, (int)plan.size());

        SORT_ALWAYS(STR("[Sort] план: {} (skip: noRule={} inPlace={} noTarget={} container={} reservedAmmo={} reservedMag={})\n"),
                    (int)plan.size(), skipNoRule, skipInPlace, skipNoTarget, skipContainer,
                    skipReservedAmmo, skipReservedMag);

        // Выполнение
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
            if (it != G_Settings.mapping.end()) targets = it->second;
            else if (G_Settings.unknownItemAction == STR("MoveToRoot")) {
                if (rootContainerValid) targets.push_back(G_Settings.rootContainer);
                else if (defaultInventory && defaultInventory != m.srcJsi) targets.push_back(STR("__INVENTORY__"));
            }
            if (targets.empty()) { ++movedFail; continue; }

            UObject* chosenDst = nullptr;
            int32_t chosenSlot = -1;
            StringType chosenId;

            for (auto& t : targets) {
                if (t == STR("__INVENTORY__")) {
                    if (defaultInventory && defaultInventory != m.srcJsi) {
                        int32_t s = TryGetEmptySlotNative(defaultInventory, sx, sy);
                        if (s >= 0) { chosenDst = defaultInventory; chosenSlot = s; chosenId = STR("Inventory"); }
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
                    if (s >= 0) { chosenDst = cand; chosenSlot = s; chosenId = t; break; }
                }
                if (chosenDst) break;
            }

            if (!chosenDst) {
                SORT_ALWAYS(STR("[Sort] нет места для '{}' ({}) — кандидаты:\n"), m.itemId, m.itemType);
                for (auto& t : targets) {
                    auto jt = byItemId.find(t);
                    if (jt == byItemId.end()) { SORT_ALWAYS(STR("[Sort]   '{}' не найден\n"), t); continue; }
                    int shown = 0;
                    for (UObject* cand : jt->second) {
                        if (shown++ >= 3) break;
                        ContainerDiag d = DiagnoseContainer(cand);
                        SORT_ALWAYS(STR("[Sort]   cand '{}' sc='{}' outer='{}' equipped={} {}\n"),
                                    d.itemId, d.scName, d.outerName,
                                    equippedJsiSet.count(cand) ? 1 : 0, DiagToString(d));
                    }
                }
                ++movedFail;
                continue;
            }

            SORT_ALWAYS(STR("[Sort] MOVE '{}' ({}) from {} -> {} slot={}\n"),
                        m.itemId, m.itemType, SafeName(m.srcJsi), chosenId, chosenSlot);
            DoEventOnInventoryAction(jigComp, m.srcJsi, chosenDst, item, nullptr, chosenSlot, false);
            UObject* still = FindItemWidgetByUID(m.srcJsi, m.itemUid);
            if (still) { ++movedFail; SORT_ALWAYS(STR("[Sort]   FAIL\n")); }
            else       { ++movedOk;   SORT_ALWAYS(STR("[Sort]   OK\n"));   }
        }

        SORT_ALWAYS(STR("[Sort] === DONE ok={} fail={} ===\n"), movedOk, movedFail);
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
                [](UObject* Context, FFrame& Stack, void* Result) { PLSFPreHook(Context, Stack, Result); });
            G_HookRegistered = true;
            Output::send<LogLevel::Verbose>(STR("[AutoSortLoot] PLSF hook установлен.\n"));
        }
        if (G_Settings.hotkey == 0) { G_HotkeyWasDown = false; return; }
        bool down = (GetAsyncKeyState(G_Settings.hotkey) & 0x8000) != 0;
        if (down && !G_HotkeyWasDown && !G_ScannerRunning) {
            G_ScannerRunning = true;
            SORT_ALWAYS(STR("[Scanner] === SCAN START ===\n"));
            UObject* jigComp = FindPlayerJigComponent();
            if (!jigComp) SORT_ALWAYS(STR("[Scanner] jigComp не найден\n"));
            else SortLoot(jigComp);
            SORT_ALWAYS(STR("[Scanner] === SCAN END ===\n"));
            G_ScannerRunning = false;
        }
        G_HotkeyWasDown = down;
    }

    void Install() { Output::send<LogLevel::Verbose>(STR("[AutoSortLoot] Install()\n")); InstallConfig(); }
    void Uninstall() { Output::send<LogLevel::Verbose>(STR("[AutoSortLoot] Uninstall()\n")); }
}