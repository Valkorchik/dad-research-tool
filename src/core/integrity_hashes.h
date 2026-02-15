#pragma once
// ============================================================================
//  AUTO-GENERATED -- Do not edit manually!
//  Run: python tools/generate_hashes.py --generate
//
//  SHA-256 hashes of critical source files. These files define the tool's
//  read-only research behavior. If any hash mismatches at build time,
//  it means a critical file was modified -- review the change carefully
//  to ensure no write/inject/aim capabilities were added.
// ============================================================================

#include <array>
#include <string_view>
#include <utility>

namespace Integrity {

static constexpr size_t FILE_COUNT = 10;

// {filename, expected_sha256}
static constexpr std::array<std::pair<std::string_view, std::string_view>, FILE_COUNT> EXPECTED_HASHES = {{
    {"src/core/process.cpp", "125c4950d6c555ce9bd2a500fb7ee2f86621665080c3047be1ad94734a8a0269"},
    {"src/core/process.h", "d54fdd64c428e0360af59ef9f0d3180ad5ae3a29ef52abc34ca1262515534613"},
    {"src/game/entity.h", "f267faeace473f9efa750746869961f002680537240498e7e7c49e786d6628eb"},
    {"src/main.cpp", "d2e142f8cf13b1beaf586105ccd622018d14602189ff6d16ec0587036060d873"},
    {"src/render/drawing.cpp", "fc827598b1b2d33872921ac5cf64c888fa6a73828ae6537f392ddd29816d0d1d"},
    {"src/render/drawing.h", "e1b8c42c0be07d4df748a8b82d07fb539f07975fc9b954b16497c5cae5c0bcd2"},
    {"src/render/overlay.cpp", "9720d2d1126a9f232f85631ea187a3bcf2639f95db2c86e00944788fa7d2a057"},
    {"src/render/overlay.h", "d6a1c49714d504dec3efdcaac37e2ef5df37028af265240fa8761e0b001ecb21"},
    {"src/render/world_to_screen.cpp", "31d023e025bd7741e2d9d9a01e1d04496157ed75b5fc6cbceb1e1525b1633826"},
    {"src/render/world_to_screen.h", "1554cd520b51176a501a5148fc119e8661df20f02624270a9e7c070cbfc22582"},
}};

} // namespace Integrity
