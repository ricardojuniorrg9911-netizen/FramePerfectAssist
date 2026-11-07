#include <Geode/Geode.hpp>

using namespace geode::prelude;

$on_mod(Loaded) {
    const int frames = Mod::get()->getSettingValue<int>("correction_frames");

    log::info("Frame Perfect Assist loaded. Correction window: {} frames.", frames);
    log::warn("Frame-perfect input correction is not implemented yet.");
}
