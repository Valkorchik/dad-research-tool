#pragma once
#include "core/config.h"

class Menu {
public:
    explicit Menu(AppConfig& config) : m_config(config) {}

    void Render();
    bool IsVisible() const { return m_visible; }
    void Toggle() { m_visible = !m_visible; }

    // Debug info setters
    void SetEntityCount(int count) { m_entityCount = count; }
    void SetFps(float fps) { m_fps = fps; }
    void SetGWorldAddr(uintptr_t addr) { m_gworldAddr = addr; }
    void SetGNamesAddr(uintptr_t addr) { m_gnamesAddr = addr; }
    void SetAttached(bool attached) { m_attached = attached; }

private:
    AppConfig& m_config;
    bool m_visible = false;
    bool m_fontSizeChanged = false;

    // Debug info
    int m_entityCount = 0;
    float m_fps = 0.0f;
    uintptr_t m_gworldAddr = 0;
    uintptr_t m_gnamesAddr = 0;
    bool m_attached = false;
};
