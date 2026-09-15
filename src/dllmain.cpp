#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include "AutoSortHooks.hpp"

class AutoSortLoot : public RC::CppUserModBase {
public:
    AutoSortLoot() : CppUserModBase() {
        ModName        = STR("AutoSortLoot");
        ModVersion     = STR("0.1.0");
        ModDescription = STR("Auto-sort loot into specialized containers");
        ModAuthors     = STR("you");
        RC::Output::send<RC::LogLevel::Normal>(STR("[AutoSortLoot]: Init.\n"));
    }

    auto on_unreal_init() -> void override {
        RC::Output::send<RC::LogLevel::Normal>(STR("[AutoSortLoot]: on_unreal_init\n"));
        AutoSortHooks::Install();
    }

    // UE4SS вызывает on_update каждый кадр для каждого активного C++ мода
    auto on_update() -> void override {
        AutoSortHooks::TryInstall();
    }
};

#define MOD_EXPORT __declspec(dllexport)
extern "C" {
    MOD_EXPORT RC::CppUserModBase* start_mod() { return new AutoSortLoot(); }
    MOD_EXPORT void uninstall_mod(RC::CppUserModBase* mod) { delete mod; }
}