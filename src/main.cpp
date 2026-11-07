#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

namespace {
    // First implementation stage: observe real hazard hitboxes ahead of the cube.
    // Automatic input is intentionally not sent until timing is validated in-game.
    int s_lastReportedHazardID = -1;

    bool isCube(PlayerObject* player) {
        return player
            && !player->m_isShip
            && !player->m_isBird
            && !player->m_isDart
            && !player->m_isSwing
            && !player->m_isBall
            && !player->m_isRobot
            && !player->m_isSpider;
    }
}

class $modify(FramePerfectPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!Mod::get()->getSettingValue<bool>("auto_click")) {
            return;
        }

        auto* player = m_player1;
        if (!player || player->m_isDead || !isCube(player) || !m_objects) {
            s_lastReportedHazardID = -1;
            return;
        }

        auto const& playerRect = player->getObjectRect();
        GameObject* nearestHazard = nullptr;
        float nearestDistance = std::numeric_limits<float>::max();

        for (unsigned int i = 0; i < m_objects->count(); ++i) {
            auto* object = static_cast<GameObject*>(m_objects->objectAtIndex(i));
            if (!object || object->m_objectType != GameObjectType::Hazard) {
                continue;
            }

            auto const& hazardRect = object->getObjectRect();
            if (hazardRect.getMaxX() <= playerRect.getMaxX()) {
                continue;
            }

            float distance = hazardRect.getMinX() - playerRect.getMaxX();
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestHazard = object;
            }
        }

        if (nearestHazard && nearestHazard->m_uniqueID != s_lastReportedHazardID) {
            s_lastReportedHazardID = nearestHazard->m_uniqueID;
            log::info(
                "FPA cube scan: hazard object {} is {:.2f} units ahead.",
                nearestHazard->m_objectID,
                nearestDistance
            );
        } else if (!nearestHazard) {
            s_lastReportedHazardID = -1;
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        s_lastReportedHazardID = -1;
    }
};

$on_mod(Loaded) {
    const int frames = Mod::get()->getSettingValue<int>("correction_frames");
    log::info("Frame Perfect Assist loaded. Correction window: {} frames.", frames);
    log::info("Cube hazard scanner is ready when Auto-click is enabled.");
}
