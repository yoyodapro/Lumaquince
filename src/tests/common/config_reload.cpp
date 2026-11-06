// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "common/settings_setting.h"
#include "frontend_common/config.h"

namespace {

class MemoryConfig final : public Config {
public:
    explicit MemoryConfig(ConfigType kind = ConfigType::GlobalConfig) : Config(kind) {
        config = std::make_unique<CSimpleIniA>();
        BeginGroup("Test");
    }

    void Put(const char* key, const char* value) {
        config->SetValue("Test", key, value);
    }
    std::string Get(const char* key) {
        return config->GetValue("Test", key, "missing");
    }
    using Config::ReadSettingGeneric;
    using Config::WriteSettingGeneric;

private:
    void ReloadAllValues() override {}
    void SaveAllValues() override {}
    void ReadHidbusValues() override {}
    void ReadDebugControlValues() override {}
    void ReadPathValues() override {}
    void ReadShortcutValues() override {}
    void ReadUIValues() override {}
    void ReadUIGamelistValues() override {}
    void ReadUILayoutValues() override {}
    void ReadMultiplayerValues() override {}
    void SaveHidbusValues() override {}
    void SaveDebugControlValues() override {}
    void SavePathValues() override {}
    void SaveShortcutValues() override {}
    void SaveUIValues() override {}
    void SaveUIGamelistValues() override {}
    void SaveUILayoutValues() override {}
    void SaveMultiplayerValues() override {}
    std::vector<Settings::BasicSetting*>& FindRelevantList(Settings::Category) override {
        return empty;
    }
    std::vector<Settings::BasicSetting*> empty;
};

} // namespace

TEST_CASE("Global config reads update the global slot while preserving an active custom slot",
          "[config_reload]") {
    Settings::Linkage linkage;
    Settings::SwitchableSetting<s32> setting(linkage, 5, "test_value", Settings::Category::Core);
    setting.SetValue(10);
    setting.SetGlobal(false);
    setting.SetValue(20);
    MemoryConfig config;
    config.Put("test_value", "30");
    config.Put("test_value\\default", "false");

    config.ReadSettingGeneric(&setting);
    CHECK_FALSE(setting.UsingGlobal());
    CHECK(setting.GetValue() == 20);
    CHECK(setting.GetValue(true) == 30);

    config.WriteSettingGeneric(&setting);
    CHECK(config.Get("test_value") == "30");
    CHECK_FALSE(setting.UsingGlobal());

    setting.SetGlobal(true);
    config.Put("test_value", "40");
    config.ReadSettingGeneric(&setting);
    CHECK(setting.UsingGlobal());
    CHECK(setting.GetValue() == 40);
    setting.SetGlobal(false);
    CHECK(setting.GetValue() == 20);
}

TEST_CASE("Global default and invalid numeric reads leave custom values intact", "[config_reload]") {
    const bool default_value = GENERATE(false, true);
    Settings::Linkage linkage;
    Settings::SwitchableSetting<s32> setting(linkage, 5, "test_value", Settings::Category::Core);
    setting.SetValue(10);
    setting.SetGlobal(false);
    setting.SetValue(20);
    MemoryConfig config;
    config.Put("test_value", "not-a-number");
    config.Put("test_value\\default", default_value ? "true" : "false");
    config.ReadSettingGeneric(&setting);
    CHECK_FALSE(setting.UsingGlobal());
    CHECK(setting.GetValue() == 20);
    CHECK(setting.GetValue(true) == 5);
}

TEST_CASE("Per-game config still selects global or custom values", "[config_reload]") {
    Settings::Linkage linkage;
    Settings::SwitchableSetting<s32> setting(linkage, 5, "test_value", Settings::Category::Core);
    setting.SetValue(10);
    MemoryConfig config(Config::ConfigType::PerGameConfig);
    config.Put("test_value", "30");
    config.Put("test_value\\default", "false");
    config.Put("test_value\\use_global", "false");
    config.ReadSettingGeneric(&setting);
    CHECK_FALSE(setting.UsingGlobal());
    CHECK(setting.GetValue() == 30);
    CHECK(setting.GetValue(true) == 10);

    config.Put("test_value\\use_global", "true");
    config.ReadSettingGeneric(&setting);
    CHECK(setting.UsingGlobal());
    CHECK(setting.GetValue() == 10);
    setting.SetGlobal(false);
    CHECK(setting.GetValue() == 30);
}
