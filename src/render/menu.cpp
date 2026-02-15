#include "menu.h"
#include "drawing.h"
#include <imgui.h>
#include <cstdio>

static constexpr const char* VERSION = "v1.0.0";

void Menu::Render() {
    if (!m_visible) return;

    float S = Drawing::GetDpiScale();

    ImGui::SetNextWindowSize(ImVec2(400 * S, 540 * S), ImGuiCond_FirstUseEver);

    // Custom title with version
    char title[64];
    snprintf(title, sizeof(title), "DAD Research Tool %s###DRT", VERSION);

    if (ImGui::Begin(title, &m_visible, ImGuiWindowFlags_NoCollapse)) {

        // ---- Status bar ----
        {
            ImU32 statusColor = m_attached ? IM_COL32(80, 220, 80, 255) : IM_COL32(220, 80, 80, 255);
            ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
            ImGui::Text(m_attached ? "ATTACHED" : "NOT ATTACHED");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::Text("FPS: %.0f", m_fps);
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::Text("Entities: %d", m_entityCount);
        }
        ImGui::Separator();
        ImGui::Spacing();

        // ---- ESP Filters ----
        if (ImGui::CollapsingHeader("ESP Filters", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(4.0f * S);

            ImGui::Checkbox("Players", &m_config.filter.showPlayers);
            ImGui::SameLine(140 * S);
            ImGui::Checkbox("NPCs", &m_config.filter.showNPCs);

            ImGui::Checkbox("Loot", &m_config.filter.showLoot);
            ImGui::SameLine(140 * S);
            ImGui::Checkbox("Portals", &m_config.filter.showPortals);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::SliderFloat("Player Range (m)", &m_config.filter.maxPlayerDistance, 50.0f, 1000.0f, "%.0f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Player ESP range filter.\n"
                    "NOTE: The game server only sends player data within ~50m\n"
                    "(network relevancy). Players beyond that don't exist in memory.");

            ImGui::SliderFloat("Other Range (m)", &m_config.filter.maxDistance, 10.0f, 300.0f, "%.0f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Range for NPCs, loot, chests, portals");

            ImGui::Spacing();

            // Rarity filter starts at Uncommon — Junk/Poor/Common are never useful
            const char* rarityNames[] = {"Uncommon", "Rare", "Epic", "Legendary", "Unique"};
            const int RARITY_OFFSET = static_cast<int>(ItemRarity::UNCOMMON); // First selectable rarity
            int rarityIdx = static_cast<int>(m_config.filter.minLootRarity) - RARITY_OFFSET;
            if (rarityIdx < 0) rarityIdx = 0;
            if (rarityIdx >= IM_ARRAYSIZE(rarityNames)) rarityIdx = IM_ARRAYSIZE(rarityNames) - 1;
            if (ImGui::Combo("Min Loot Rarity", &rarityIdx, rarityNames, IM_ARRAYSIZE(rarityNames))) {
                m_config.filter.minLootRarity = static_cast<ItemRarity>(rarityIdx + RARITY_OFFSET);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Filters ground loot, chest contents, AND dead body equipment");

            const char* gradeNames[] = {"All", "Common+", "Elite+", "Nightmare"};
            ImGui::Combo("Min NPC Grade", &m_config.filter.minMonsterGrade, gradeNames, IM_ARRAYSIZE(gradeNames));

            ImGui::Unindent(4.0f * S);
        }

        ImGui::Spacing();

        // ---- Visual Settings ----
        if (ImGui::CollapsingHeader("Visuals", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(4.0f * S);

            const char* boxStyles[] = {"2D Box", "Filled Box", "Corner Box"};
            ImGui::Combo("Box Style", &m_config.visuals.boxStyle, boxStyles, IM_ARRAYSIZE(boxStyles));

            ImGui::Checkbox("Health Bars", &m_config.filter.showHealthBars);
            ImGui::SameLine(140 * S);
            ImGui::Checkbox("Distance", &m_config.filter.showDistance);

            ImGui::Checkbox("Snap Lines", &m_config.filter.showSnapLines);

            ImGui::Spacing();

            // Font size slider — allows user to adjust ESP text/UI size
            if (ImGui::SliderFloat("Font Size", &m_config.visuals.fontSize, 10.0f, 30.0f, "%.0f")) {
                m_fontSizeChanged = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Requires restart to take effect");
            if (m_fontSizeChanged) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(restart)");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::ColorEdit4("Player Color", m_config.visuals.playerColor,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            ImGui::SameLine();
            ImGui::Text("Player");
            ImGui::SameLine(200 * S);
            ImGui::ColorEdit4("NPC Color", m_config.visuals.npcColor,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            ImGui::SameLine();
            ImGui::Text("NPC");

            ImGui::Unindent(4.0f * S);
        }

        ImGui::Spacing();

        // ---- Performance ----
        if (ImGui::CollapsingHeader("Performance")) {
            ImGui::Indent(4.0f * S);
            ImGui::TextDisabled("Render");
            ImGui::SameLine(100 * S);
            ImGui::Text("%d FPS cap (no vsync)", m_config.updateRateFps);
            ImGui::TextDisabled("Scan");
            ImGui::SameLine(100 * S);
            ImGui::Text("~2 Hz background thread");
            ImGui::TextDisabled("Position");
            ImGui::SameLine(100 * S);
            ImGui::Text("Per-frame with extrapolation");
            ImGui::TextDisabled("Scale");
            ImGui::SameLine(100 * S);
            ImGui::Text("%.1fx (auto)", S);
            ImGui::Unindent(4.0f * S);
        }

        ImGui::Spacing();

        // ---- Debug ----
        if (ImGui::CollapsingHeader("Debug")) {
            ImGui::Indent(4.0f * S);
            ImGui::TextDisabled("GWorld");
            ImGui::SameLine(80 * S);
            ImGui::Text("0x%llX", m_gworldAddr);
            ImGui::TextDisabled("GNames");
            ImGui::SameLine(80 * S);
            ImGui::Text("0x%llX", m_gnamesAddr);
            ImGui::TextDisabled("Entities");
            ImGui::SameLine(80 * S);
            ImGui::Text("%d tracked", m_entityCount);
            ImGui::Unindent(4.0f * S);
        }

        // ---- Footer ----
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("INSERT - Toggle Menu  |  END - Exit");
    }
    ImGui::End();
}
