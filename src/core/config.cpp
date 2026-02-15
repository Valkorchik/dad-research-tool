#include "config.h"
#include <fstream>
#include <spdlog/spdlog.h>

Config::Config(const std::string& filepath) : m_filepath(filepath) {
    Load();
}

void Config::Load() {
    std::ifstream file(m_filepath);
    if (!file.is_open()) {
        spdlog::info("Config file not found, using defaults: {}", m_filepath);
        Save(); // Create default config
        return;
    }

    try {
        nlohmann::json j;
        file >> j;

        auto& f = m_config.filter;
        if (j.contains("filter")) {
            auto& jf = j["filter"];
            f.showPlayers = jf.value("showPlayers", true);
            f.showLoot = jf.value("showLoot", true);
            f.showNPCs = jf.value("showNPCs", true);
            f.showPortals = jf.value("showPortals", true);
            f.maxDistance = jf.value("maxDistance", 300.0f);
            f.maxPlayerDistance = jf.value("maxPlayerDistance", 1000.0f);
            f.minLootRarity = static_cast<ItemRarity>(jf.value("minLootRarity", 3)); // Default: Uncommon
            f.showHealthBars = jf.value("showHealthBars", true);
            f.showDistance = jf.value("showDistance", true);
            f.showSnapLines = jf.value("showSnapLines", false);
            f.minMonsterGrade = jf.value("minMonsterGrade", 0);
        }

        if (j.contains("visuals")) {
            auto& jv = j["visuals"];
            if (jv.contains("playerColor")) {
                auto c = jv["playerColor"];
                for (int i = 0; i < 4; i++) m_config.visuals.playerColor[i] = c[i];
            }
            if (jv.contains("teamColor")) {
                auto c = jv["teamColor"];
                for (int i = 0; i < 4; i++) m_config.visuals.teamColor[i] = c[i];
            }
            if (jv.contains("npcColor")) {
                auto c = jv["npcColor"];
                for (int i = 0; i < 4; i++) m_config.visuals.npcColor[i] = c[i];
            }
            m_config.visuals.fontSize = jv.value("fontSize", 15.0f);
            m_config.visuals.boxStyle = jv.value("boxStyle", 0);
        }

        m_config.updateRateFps = j.value("updateRateFps", 144);
        m_config.toggleMenuKey = j.value("toggleMenuKey", VK_INSERT);

        if (j.contains("patterns")) {
            auto& jp = j["patterns"];
            m_config.gworldPattern = jp.value("gworld", "");
            m_config.gnamesPattern = jp.value("gnames", "");
            m_config.gobjectsPattern = jp.value("gobjects", "");
        }

        if (j.contains("offsets")) {
            auto& jo = j["offsets"];
            auto parseHex = [](const std::string& s) -> uintptr_t {
                if (s.empty()) return 0;
                return std::stoull(s, nullptr, 16);
            };
            if (jo.contains("gworld")) m_config.gworldOffset = parseHex(jo["gworld"].get<std::string>());
            if (jo.contains("gnames")) m_config.gnamesOffset = parseHex(jo["gnames"].get<std::string>());
            if (jo.contains("gobjects")) m_config.gobjectsOffset = parseHex(jo["gobjects"].get<std::string>());
            if (jo.contains("ue_version")) m_config.ueVersion = jo["ue_version"].get<std::string>();
        }

        spdlog::info("Config loaded from {}", m_filepath);
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse config: {}", e.what());
    }
}

void Config::Save() const {
    nlohmann::json j;

    j["filter"] = {
        {"showPlayers", m_config.filter.showPlayers},
        {"showLoot", m_config.filter.showLoot},
        {"showNPCs", m_config.filter.showNPCs},
        {"showPortals", m_config.filter.showPortals},
        {"maxDistance", m_config.filter.maxDistance},
        {"maxPlayerDistance", m_config.filter.maxPlayerDistance},
        {"minLootRarity", static_cast<int>(m_config.filter.minLootRarity)},
        {"showHealthBars", m_config.filter.showHealthBars},
        {"showDistance", m_config.filter.showDistance},
        {"showSnapLines", m_config.filter.showSnapLines},
        {"minMonsterGrade", m_config.filter.minMonsterGrade}
    };

    j["visuals"] = {
        {"playerColor", {m_config.visuals.playerColor[0], m_config.visuals.playerColor[1],
                         m_config.visuals.playerColor[2], m_config.visuals.playerColor[3]}},
        {"teamColor", {m_config.visuals.teamColor[0], m_config.visuals.teamColor[1],
                       m_config.visuals.teamColor[2], m_config.visuals.teamColor[3]}},
        {"npcColor", {m_config.visuals.npcColor[0], m_config.visuals.npcColor[1],
                      m_config.visuals.npcColor[2], m_config.visuals.npcColor[3]}},
        {"fontSize", m_config.visuals.fontSize},
        {"boxStyle", m_config.visuals.boxStyle}
    };

    j["updateRateFps"] = m_config.updateRateFps;
    j["toggleMenuKey"] = m_config.toggleMenuKey;

    j["patterns"] = {
        {"gworld", m_config.gworldPattern},
        {"gnames", m_config.gnamesPattern},
        {"gobjects", m_config.gobjectsPattern}
    };

    // Format offsets as hex strings
    auto toHex = [](uintptr_t val) -> std::string {
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(val));
        return buf;
    };

    j["offsets"] = {
        {"gworld", toHex(m_config.gworldOffset)},
        {"gnames", toHex(m_config.gnamesOffset)},
        {"gobjects", toHex(m_config.gobjectsOffset)},
        {"ue_version", m_config.ueVersion}
    };

    std::ofstream file(m_filepath);
    if (file.is_open()) {
        file << j.dump(2);
        spdlog::info("Config saved to {}", m_filepath);
    } else {
        spdlog::error("Failed to save config to {}", m_filepath);
    }
}
