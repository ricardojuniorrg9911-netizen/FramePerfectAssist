#include <Geode/Geode.hpp>

using namespace geode::prelude;

$on_mod(Loaded) {
    int frames = Mod::get()->getSettingValue<int>("correction_frames");

    log::info(
        "Frame Perfect Assist loaded. Window: {} frames.",
        frames
    );

    log::warn("Input correction is not implemented yet.");
}
