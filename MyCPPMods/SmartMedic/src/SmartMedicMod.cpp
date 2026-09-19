#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include "SmartMedicHooks.hpp"

class SmartMedic : public RC::CppUserModBase {
public:
    SmartMedic() : CppUserModBase() {
        ModName        = STR("SmartMedic");
        ModVersion     = STR("0.1.0");
        ModDescription = STR("Press F10 to auto-use the best medical item");
        ModAuthors     = STR("you");
        RC::Output::send<RC::LogLevel::Normal>(STR("[SmartMedic]: Init.\n"));
    }

    auto on_unreal_init() -> void override {
        RC::Output::send<RC::LogLevel::Normal>(STR("[SmartMedic]: on_unreal_init\n"));
        SmartMedic::Install();
    }

    // UE4SS вызывает on_update каждый кадр для каждого активного C++ мода
    auto on_update() -> void override {
        SmartMedic::TryInstall();
    }
};

#define MOD_EXPORT __declspec(dllexport)
extern "C" {
    MOD_EXPORT RC::CppUserModBase* start_mod() { return new SmartMedic(); }
    MOD_EXPORT void uninstall_mod(RC::CppUserModBase* mod) { delete mod; }
}