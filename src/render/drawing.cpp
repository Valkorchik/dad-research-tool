#include "drawing.h"
#include <cstdio>
#include <cmath>

namespace Drawing {

// ============================================================================
// DPI scale factor — set once at startup from screen resolution.
// S = 1.0 at 1080p, 2.0 at 4K. All pixel constants multiply by S.
// ============================================================================
static float g_dpiScale = 1.0f;

void SetDpiScale(float scale) { g_dpiScale = scale; }
float GetDpiScale() { return g_dpiScale; }

// Shorthand for scaling a pixel value
static inline float S(float px) { return px * g_dpiScale; }

// ============================================================================
// Text shadow offset for all ESP labels (scaled outline for readability)
// ============================================================================
static constexpr ImU32 SHADOW_COLOR = IM_COL32(0, 0, 0, 200);

// Helper: draw text with a dark drop-shadow for readability (2 draws instead of 5)
static void DrawTextShadowed(ImDrawList* draw, const ImVec2& pos, ImU32 color, const char* text) {
    float off = S(1.0f);
    draw->AddText(ImVec2(pos.x + off, pos.y + off), SHADOW_COLOR, text);
    draw->AddText(pos, color, text);
}

// ============================================================================
// Deduplicated rarity color from equipment string (used in dead body + chest lists)
// ============================================================================
static ImU32 GetEquipmentItemColor(const std::string& item) {
    if (item.find("Artifact") != std::string::npos)
        return IM_COL32(255, 0, 0, 255);      // Red
    if (item.find("Unique") != std::string::npos)
        return IM_COL32(255, 212, 0, 255);     // Gold
    if (item.find("Legendary") != std::string::npos || item.find("Legend") != std::string::npos)
        return IM_COL32(255, 170, 0, 255);     // Orange
    if (item.find("Epic") != std::string::npos)
        return IM_COL32(255, 0, 255, 255);     // Purple
    if (item.find("Rare") != std::string::npos)
        return IM_COL32(0, 136, 255, 255);     // Blue
    if (item.find("Uncommon") != std::string::npos)
        return IM_COL32(50, 200, 50, 255);     // Green
    return IM_COL32(255, 200, 50, 255);        // Default gold
}

// ============================================================================
// Basic primitives
// ============================================================================

void DrawBox2D(ImDrawList* draw, const ImVec2& topLeft, const ImVec2& bottomRight,
               ImU32 color, float thickness) {
    float t = thickness * g_dpiScale;
    // Dark outline behind the colored box for contrast
    draw->AddRect(topLeft, bottomRight, IM_COL32(0, 0, 0, 160), 0.0f, 0, t + S(1.0f));
    draw->AddRect(topLeft, bottomRight, color, 0.0f, 0, t);
}

void DrawCornerBox(ImDrawList* draw, const ImVec2& topLeft, const ImVec2& bottomRight,
                   ImU32 color, float cornerLength, float thickness) {
    float w = bottomRight.x - topLeft.x;
    float h = bottomRight.y - topLeft.y;
    float cl = (std::min)(cornerLength * g_dpiScale, (std::min)(w, h) / 3.0f);
    float t = thickness * g_dpiScale;

    ImU32 shadow = IM_COL32(0, 0, 0, 160);
    float shadowThick = t + S(1.0f);

    // Top-left (shadow then color)
    draw->AddLine(topLeft, ImVec2(topLeft.x + cl, topLeft.y), shadow, shadowThick);
    draw->AddLine(topLeft, ImVec2(topLeft.x, topLeft.y + cl), shadow, shadowThick);
    draw->AddLine(topLeft, ImVec2(topLeft.x + cl, topLeft.y), color, t);
    draw->AddLine(topLeft, ImVec2(topLeft.x, topLeft.y + cl), color, t);

    // Top-right
    draw->AddLine(ImVec2(bottomRight.x, topLeft.y), ImVec2(bottomRight.x - cl, topLeft.y), shadow, shadowThick);
    draw->AddLine(ImVec2(bottomRight.x, topLeft.y), ImVec2(bottomRight.x, topLeft.y + cl), shadow, shadowThick);
    draw->AddLine(ImVec2(bottomRight.x, topLeft.y), ImVec2(bottomRight.x - cl, topLeft.y), color, t);
    draw->AddLine(ImVec2(bottomRight.x, topLeft.y), ImVec2(bottomRight.x, topLeft.y + cl), color, t);

    // Bottom-left
    draw->AddLine(ImVec2(topLeft.x, bottomRight.y), ImVec2(topLeft.x + cl, bottomRight.y), shadow, shadowThick);
    draw->AddLine(ImVec2(topLeft.x, bottomRight.y), ImVec2(topLeft.x, bottomRight.y - cl), shadow, shadowThick);
    draw->AddLine(ImVec2(topLeft.x, bottomRight.y), ImVec2(topLeft.x + cl, bottomRight.y), color, t);
    draw->AddLine(ImVec2(topLeft.x, bottomRight.y), ImVec2(topLeft.x, bottomRight.y - cl), color, t);

    // Bottom-right
    draw->AddLine(bottomRight, ImVec2(bottomRight.x - cl, bottomRight.y), shadow, shadowThick);
    draw->AddLine(bottomRight, ImVec2(bottomRight.x, bottomRight.y - cl), shadow, shadowThick);
    draw->AddLine(bottomRight, ImVec2(bottomRight.x - cl, bottomRight.y), color, t);
    draw->AddLine(bottomRight, ImVec2(bottomRight.x, bottomRight.y - cl), color, t);
}

void DrawLabel(ImDrawList* draw, const ImVec2& pos, const char* text,
               ImU32 textColor, ImU32 bgColor) {
    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImVec2 bgMin(pos.x - S(3), pos.y - S(1));
    ImVec2 bgMax(pos.x + textSize.x + S(3), pos.y + textSize.y + S(1));

    draw->AddRectFilled(bgMin, bgMax, bgColor, S(2.0f));
    draw->AddText(pos, textColor, text);
}

void DrawLabelCentered(ImDrawList* draw, const ImVec2& pos, const char* text,
                       ImU32 textColor, ImU32 bgColor) {
    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImVec2 textPos(pos.x - textSize.x / 2.0f, pos.y - textSize.y / 2.0f);

    ImVec2 bgMin(textPos.x - S(3), textPos.y - S(1));
    ImVec2 bgMax(textPos.x + textSize.x + S(3), textPos.y + textSize.y + S(1));

    draw->AddRectFilled(bgMin, bgMax, bgColor, S(2.0f));
    draw->AddText(textPos, textColor, text);
}

// ============================================================================
// Health bar — smooth gradient from green -> yellow -> red
// ============================================================================
void DrawHealthBar(ImDrawList* draw, const ImVec2& pos, float width,
                   float healthPct, float height) {
    healthPct = (std::max)(0.0f, (std::min)(1.0f, healthPct));

    // Background (dark with subtle border)
    draw->AddRectFilled(
        ImVec2(pos.x - S(1), pos.y - S(1)),
        ImVec2(pos.x + width + S(1), pos.y + height + S(1)),
        IM_COL32(0, 0, 0, 220), S(1.0f)
    );

    // Smooth gradient: 100% = green, 50% = yellow, 0% = red
    int r, g;
    if (healthPct > 0.5f) {
        float t = (healthPct - 0.5f) * 2.0f;
        r = static_cast<int>((1.0f - t) * 255);
        g = 255;
    } else {
        float t = healthPct * 2.0f;
        r = 255;
        g = static_cast<int>(t * 255);
    }
    ImU32 healthColor = IM_COL32(r, g, 0, 255);

    // Health fill
    float fillWidth = width * healthPct;
    if (fillWidth > S(1.0f)) {
        draw->AddRectFilled(
            ImVec2(pos.x, pos.y),
            ImVec2(pos.x + fillWidth, pos.y + height),
            healthColor, S(1.0f)
        );
    }
}

void DrawSnapLine(ImDrawList* draw, const ImVec2& entityPos,
                  int screenWidth, int screenHeight, ImU32 color) {
    ImVec2 screenBottom(screenWidth / 2.0f, static_cast<float>(screenHeight));
    draw->AddLine(screenBottom, entityPos, IM_COL32(0, 0, 0, 100), S(2.0f));
    draw->AddLine(screenBottom, entityPos, color, S(1.0f));
}

void DrawCircleFilled(ImDrawList* draw, const ImVec2& center,
                      float radius, ImU32 color) {
    draw->AddCircleFilled(center, radius, color);
}

void DrawDistance(ImDrawList* draw, const ImVec2& pos, float distanceMeters) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0fm", distanceMeters);
    DrawLabelCentered(draw, pos, buf, IM_COL32(200, 200, 200, 255), IM_COL32(0, 0, 0, 140));
}

// ============================================================================
// Full player/monster/chest ESP
// ============================================================================
void DrawPlayerESP(ImDrawList* draw, const GameEntity& entity,
                   ImU32 color, int boxStyle) {
    if (!entity.isOnScreen) return;

    float sx = entity.screenPos.X;
    float sy = entity.screenPos.Y;

    // ---- Dead entity: simple X marker + name + equipment ----
    if (!entity.isAlive) {
        bool hasLoot = !entity.equipment.empty();
        ImU32 deadColor = hasLoot
            ? IM_COL32(255, 200, 50, 220)
            : ((color & 0x00FFFFFF) | 0x80000000);

        float xSize = hasLoot ? S(10.0f) : S(8.0f);
        if (entity.distanceMeters > 10.0f) xSize *= (10.0f / entity.distanceMeters);
        xSize = (std::max)(xSize, S(4.0f));

        // X shadow + color
        draw->AddLine(ImVec2(sx - xSize, sy - xSize), ImVec2(sx + xSize, sy + xSize), SHADOW_COLOR, S(3.0f));
        draw->AddLine(ImVec2(sx + xSize, sy - xSize), ImVec2(sx - xSize, sy + xSize), SHADOW_COLOR, S(3.0f));
        draw->AddLine(ImVec2(sx - xSize, sy - xSize), ImVec2(sx + xSize, sy + xSize), deadColor, S(2.0f));
        draw->AddLine(ImVec2(sx + xSize, sy - xSize), ImVec2(sx - xSize, sy + xSize), deadColor, S(2.0f));

        // Name label
        char label[128];
        if (entity.type == EntityType::PLAYER && !entity.playerClass.empty()) {
            if (hasLoot) {
                snprintf(label, sizeof(label), "%s - %s (LOOT)", entity.displayName.c_str(), entity.playerClass.c_str());
            } else {
                snprintf(label, sizeof(label), "%s - %s (Dead)", entity.displayName.c_str(), entity.playerClass.c_str());
            }
        } else {
            if (hasLoot) {
                snprintf(label, sizeof(label), "%s (LOOT)", entity.displayName.c_str());
            } else {
                snprintf(label, sizeof(label), "%s (Dead)", entity.displayName.c_str());
            }
        }
        DrawLabelCentered(draw, ImVec2(sx, sy - xSize - S(10)), label, deadColor);

        // Distance
        DrawDistance(draw, ImVec2(sx, sy + xSize + S(4)), entity.distanceMeters);

        // Equipment on dead bodies
        if (hasLoot) {
            float equipY = sy + xSize + S(18);
            for (const auto& item : entity.equipment) {
                ImU32 itemColor = GetEquipmentItemColor(item);
                DrawLabelCentered(draw, ImVec2(sx, equipY), item.c_str(), itemColor);
                equipY += S(14);
            }
        }
        return;
    }

    // ---- Alive entity: full box + health bar ----
    float boxTop, boxBottom, boxCenterX, boxWidth;

    if (entity.hasBoneData && entity.headOnScreen && entity.feetOnScreen) {
        // Bone-based: box spans from projected head to projected feet
        boxTop = (std::min)(entity.screenHead.Y, entity.screenFeet.Y);
        boxBottom = (std::max)(entity.screenHead.Y, entity.screenFeet.Y);
        boxCenterX = (entity.screenHead.X + entity.screenFeet.X) / 2.0f;
        float boneHeight = boxBottom - boxTop;
        boneHeight = (std::max)(boneHeight, S(8.0f));
        boxWidth = boneHeight * 0.35f;
        float headPad = boneHeight * 0.10f;
        boxTop -= headPad;
    } else {
        // Fallback: estimate box from distance (scaled for resolution)
        float refScreenHeight = S(100.0f); // 100px at 1080p, 200px at 4K (at 10m)
        float refDistance = 10.0f;
        float scale = refDistance / (std::max)(entity.distanceMeters, 1.0f);
        float estHeight = refScreenHeight * scale;
        estHeight = (std::max)(estHeight, S(12.0f));
        estHeight = (std::min)(estHeight, S(400.0f));
        float estWidth = estHeight * 0.35f;
        estWidth = (std::max)(estWidth, S(6.0f));

        boxTop = sy - estHeight * 0.7f;
        boxBottom = sy + estHeight * 0.3f;
        boxCenterX = sx;
        boxWidth = estWidth;
    }

    ImVec2 topLeft(boxCenterX - boxWidth, boxTop);
    ImVec2 bottomRight(boxCenterX + boxWidth, boxBottom);

    // Draw box based on style
    switch (boxStyle) {
        case 0: // 2D box
            DrawBox2D(draw, topLeft, bottomRight, color, 1.5f);
            break;
        case 1: // Filled box
            draw->AddRectFilled(topLeft, bottomRight, (color & 0x00FFFFFF) | 0x30000000);
            DrawBox2D(draw, topLeft, bottomRight, color, 2.0f);
            break;
        case 2: // Corner box
            DrawCornerBox(draw, topLeft, bottomRight, color);
            break;
        default:
            DrawBox2D(draw, topLeft, bottomRight, color, 1.5f);
            break;
    }

    // Name label (top line)
    char label[128];
    if (entity.type == EntityType::PLAYER && !entity.playerClass.empty()) {
        snprintf(label, sizeof(label), "%s - %s",
                 entity.displayName.c_str(), entity.playerClass.c_str());
    } else if (entity.type == EntityType::MONSTER && entity.monsterGrade != MonsterGrade::None
               && entity.monsterGrade != MonsterGrade::Common) {
        snprintf(label, sizeof(label), "%s (%s)",
                 entity.displayName.c_str(), MonsterGradeToString(entity.monsterGrade));
    } else {
        snprintf(label, sizeof(label), "%s", entity.displayName.c_str());
    }
    DrawLabelCentered(draw, ImVec2(boxCenterX, topLeft.y - S(14)), label, color);

    // Health bar (scaled)
    float actualBoxWidth = bottomRight.x - topLeft.x;
    float yOffset = bottomRight.y + S(4);
    if (entity.maxHealth > 0 && entity.health >= 0.0f) {
        float healthPct = entity.health / entity.maxHealth;
        DrawHealthBar(draw, ImVec2(topLeft.x, yOffset), actualBoxWidth, healthPct, S(6.0f));
        yOffset += S(10);
    }

    // Distance
    DrawDistance(draw, ImVec2(boxCenterX, yOffset), entity.distanceMeters);
    yOffset += S(14);

    // Equipment/contents list
    if (!entity.equipment.empty()) {
        for (const auto& item : entity.equipment) {
            ImU32 itemColor = GetEquipmentItemColor(item);
            DrawLabelCentered(draw, ImVec2(boxCenterX, yOffset), item.c_str(), itemColor);
            yOffset += S(14);
        }
    }
}

// ============================================================================
// Item ESP (ground loot)
// ============================================================================
void DrawItemESP(ImDrawList* draw, const GameEntity& entity) {
    if (!entity.isOnScreen) return;

    ImU32 color = RarityToColor(entity.rarity);
    float sx = entity.screenPos.X;
    float sy = entity.screenPos.Y;

    // Diamond size scales with distance (bigger when close)
    float size = S(6.0f);
    if (entity.distanceMeters > 5.0f) size *= (5.0f / entity.distanceMeters);
    size = (std::max)(size, S(3.0f));

    // Rarity-colored diamond marker
    ImVec2 diamond[4] = {
        ImVec2(sx, sy - size),
        ImVec2(sx + size, sy),
        ImVec2(sx, sy + size),
        ImVec2(sx - size, sy)
    };

    // Shadow outline
    float so = S(1.0f); // shadow offset
    ImVec2 diamondShadow[4] = {
        ImVec2(sx, sy - size - so),
        ImVec2(sx + size + so, sy),
        ImVec2(sx, sy + size + so),
        ImVec2(sx - size - so, sy)
    };
    draw->AddPolyline(diamondShadow, 4, SHADOW_COLOR, ImDrawFlags_Closed, S(2.5f));

    // Filled diamond
    ImU32 fillColor = (color & 0x00FFFFFF) | 0x60000000;
    draw->AddConvexPolyFilled(diamond, 4, fillColor);
    draw->AddPolyline(diamond, 4, color, ImDrawFlags_Closed, S(1.5f));

    // Epic+ glow ring
    if (entity.rarity >= ItemRarity::EPIC) {
        ImU32 glowColor = (color & 0x00FFFFFF) | 0x30000000;
        draw->AddCircle(ImVec2(sx, sy), size + S(3.0f), glowColor, 8, S(1.0f));
    }

    // Item name
    char label[256];
    snprintf(label, sizeof(label), "%s", entity.displayName.c_str());
    DrawLabelCentered(draw, ImVec2(sx, sy - size - S(12)), label, color);

    // Distance below
    DrawDistance(draw, ImVec2(sx, sy + size + S(4)), entity.distanceMeters);
}

// ============================================================================
// Radar minimap
// ============================================================================
void DrawRadar(ImDrawList* draw, const FVector& localPos, float localYaw,
               const std::vector<const GameEntity*>& entities,
               int screenWidth, int screenHeight,
               float radarRadius, float radarRange) {
    // Scale radar size with DPI
    float r = radarRadius * g_dpiScale;

    // Radar position: right side, moved UP to avoid in-game minimap
    ImVec2 center(screenWidth - r - S(20), screenHeight / 2.0f - S(40));

    // Background circle
    draw->AddCircleFilled(center, r + S(1), IM_COL32(20, 20, 20, 100), 32);
    draw->AddCircle(center, r, IM_COL32(80, 80, 80, 140), 32, S(1.5f));

    // Cross-hair lines
    draw->AddLine(ImVec2(center.x - r, center.y),
                  ImVec2(center.x + r, center.y),
                  IM_COL32(50, 50, 50, 120), S(1.0f));
    draw->AddLine(ImVec2(center.x, center.y - r),
                  ImVec2(center.x, center.y + r),
                  IM_COL32(50, 50, 50, 120), S(1.0f));

    // Range rings with distance labels
    float rangeMeters = radarRange / 100.0f; // UU to meters
    draw->AddCircle(center, r * 0.50f, IM_COL32(50, 50, 50, 100), 24, S(1.0f));

    // Label at 50% ring showing half-range in meters
    char ringLabel[16];
    snprintf(ringLabel, sizeof(ringLabel), "%.0fm", rangeMeters * 0.5f);
    ImVec2 ringLabelSize = ImGui::CalcTextSize(ringLabel);
    draw->AddText(ImVec2(center.x + r * 0.50f + S(2), center.y - ringLabelSize.y / 2),
                  IM_COL32(60, 60, 60, 140), ringLabel);

    // Convert yaw to radians
    float yawRad = static_cast<float>(localYaw * 3.14159265358979 / 180.0);
    float cosYaw = cosf(yawRad);
    float sinYaw = sinf(yawRad);

    // Local player direction arrow
    {
        float arrowLen = S(8.0f);
        float arrowHalf = S(4.0f);
        ImVec2 tip(center.x, center.y - arrowLen);
        ImVec2 left(center.x - arrowHalf, center.y + arrowHalf);
        ImVec2 right(center.x + arrowHalf, center.y + arrowHalf);
        draw->AddTriangleFilled(tip, left, right, IM_COL32(255, 255, 255, 240));
        draw->AddTriangle(tip, left, right, IM_COL32(200, 200, 200, 255), S(1.0f));
    }

    for (const auto* entity : entities) {
        if (entity->isLocalPlayer) continue;
        if (entity->type != EntityType::PLAYER &&
            entity->type != EntityType::MONSTER &&
            entity->type != EntityType::PORTAL)
            continue;

        float dx = static_cast<float>(entity->position.X - localPos.X);
        float dy = static_cast<float>(entity->position.Y - localPos.Y);

        // Skip entities extremely close to camera — likely the local player
        // whose address didn't match localPawn (e.g., pawn swap, spectating)
        float worldDist = sqrtf(dx * dx + dy * dy);
        if (worldDist < 50.0f && entity->type == EntityType::PLAYER) continue; // <0.5m = self

        float rx =  dx * sinYaw - dy * cosYaw;
        float ry =  dx * cosYaw + dy * sinYaw;

        float scale = r / radarRange;
        float px = rx * scale;
        float py = -ry * scale;

        float dist = sqrtf(px * px + py * py);
        if (dist > r - S(4.0f)) {
            float clampScale = (r - S(4.0f)) / dist;
            px *= clampScale;
            py *= clampScale;
        }

        ImVec2 dotPos(center.x + px, center.y + py);

        ImU32 dotColor;
        float dotSize;
        if (entity->type == EntityType::PLAYER) {
            dotColor = entity->isAlive ? IM_COL32(255, 50, 50, 255)
                                       : IM_COL32(150, 50, 50, 150);
            dotSize = S(4.0f);
        } else if (entity->type == EntityType::PORTAL) {
            dotColor = IM_COL32(50, 150, 255, 240);
            dotSize = S(5.0f);
        } else {
            dotColor = entity->isAlive ? IM_COL32(255, 200, 50, 200)
                                       : IM_COL32(100, 80, 30, 120);
            dotSize = S(3.0f);
        }

        draw->AddCircleFilled(dotPos, dotSize + S(1.0f), IM_COL32(0, 0, 0, 120));
        draw->AddCircleFilled(dotPos, dotSize, dotColor);
    }

    // Label
    DrawTextShadowed(draw, ImVec2(center.x - S(15), center.y - r - S(14)),
                     IM_COL32(180, 180, 180, 200), "RADAR");
}

// ============================================================================
// Loading/status display
// ============================================================================
void DrawLoadingStatus(ImDrawList* draw, int screenWidth, int screenHeight,
                       const char* statusText, float progress) {
    float cx = screenWidth / 2.0f;
    float cy = screenHeight / 2.0f;

    ImVec2 boxMin(cx - S(200), cy - S(40));
    ImVec2 boxMax(cx + S(200), cy + S(40));
    draw->AddRectFilled(boxMin, boxMax, IM_COL32(10, 10, 10, 200), S(8.0f));
    draw->AddRect(boxMin, boxMax, IM_COL32(80, 80, 80, 200), S(8.0f), 0, S(1.0f));

    // Title
    DrawTextShadowed(draw, ImVec2(cx - ImGui::CalcTextSize("DAD Research Tool").x / 2.0f, cy - S(30)),
                     IM_COL32(200, 200, 200, 255), "DAD Research Tool");

    // Status text
    ImVec2 statusSize = ImGui::CalcTextSize(statusText);
    DrawTextShadowed(draw, ImVec2(cx - statusSize.x / 2.0f, cy - S(5)),
                     IM_COL32(150, 150, 150, 255), statusText);

    // Progress bar
    if (progress >= 0.0f) {
        float barWidth = S(300.0f);
        float barHeight = S(4.0f);
        float barX = cx - barWidth / 2.0f;
        float barY = cy + S(20);

        draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barWidth, barY + barHeight),
                            IM_COL32(40, 40, 40, 200), S(2.0f));
        float fillWidth = barWidth * (std::min)(1.0f, progress);
        if (fillWidth > 0) {
            draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + fillWidth, barY + barHeight),
                                IM_COL32(0, 180, 255, 255), S(2.0f));
        }
    }
}

} // namespace Drawing
