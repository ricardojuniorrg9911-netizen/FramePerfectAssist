#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

namespace {
    // Diagnostic-only cube stage. Input is not sent until timing is validated in-game.
    int s_lastReportedHazardID = -1;
    int s_lastLoggedEstimate = -1;
    float s_lastPlayerRight = 0.f;
    bool s_hasLastPlayerPosition = false;

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

    void resetScannerState() {
        s_lastReportedHazardID = -1;
        s_lastLoggedEstimate = -1;
        s_lastPlayerRight = 0.f;
        s_hasLastPlayerPosition = false;
    }
}

class $modify(FramePerfectPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!Mod::get()->getSettingValue<bool>("auto_click")) {
            resetScannerState();
            return;
        }

        auto* player = m_player1;
        if (!player || player->m_isDead || !isCube(player) || !m_objects) {
            resetScannerState();
            return;
        }

        auto const& playerRect = player->getObjectRect();
        const float playerRight = playerRect.getMaxX();
        float movementPerUpdate = 0.f;
        if (s_hasLastPlayerPosition) {
            movementPerUpdate = playerRight - s_lastPlayerRight;
        }
        s_lastPlayerRight = playerRight;
        s_hasLastPlayerPosition = true;

        GameObject* nearestHazard = nullptr;
        float nearestDistance = std::numeric_limits<float>::max();

        for (unsigned int i = 0; i < m_objects->count(); ++i) {
            auto* object = static_cast<GameObject*>(m_objects->objectAtIndex(i));
            if (!object || object->m_objectType != GameObjectType::Hazard) {
                continue;
            }

            auto const& hazardRect = object->getObjectRect();
            if (hazardRect.getMaxX() <= playerRight) {
                continue;
            }

            const float distance = hazardRect.getMinX() - playerRight;
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestHazard = object;
            }
        }

        if (!nearestHazard) {
            s_lastReportedHazardID = -1;
            s_lastLoggedEstimate = -1;
            return;
        }

        const int hazardID = nearestHazard->m_uniqueID;
        const int estimatedUpdates = movementPerUpdate > 0.001f
            ? static_cast<int>(nearestDistance / movementPerUpdate)
            : -1;
        const int correctionFrames = Mod::get()->getSettingValue<int>("correction_frames");

        if (hazardID != s_lastReportedHazardID) {
            s_lastReportedHazardID = hazardID;
            s_lastLoggedEstimate = estimatedUpdates;
            if (estimatedUpdates >= 0) {
                log::info(
                    "FPA cube scan: hazard {} is {:.2f} units ahead; estimated {} update steps away.",
                    nearestHazard->m_objectID,
                    nearestDistance,
                    estimatedUpdates
                );
            } else {
                log::info(
                    "FPA cube scan: hazard {} is {:.2f} units ahead; measuring movement.",
                    nearestHazard->m_objectID,
                    nearestDistance
                );
            }
            return;
        }

        // Log meaningful estimate changes and the first entry into the configured window.
        if (estimatedUpdates >= 0) {
            const bool enteredCorrectionWindow =
                s_lastLoggedEstimate > correctionFrames
                && estimatedUpdates <= correctionFrames;
            const bool changedByTenOrMore =
                s_lastLoggedEstimate < 0
                || estimatedUpdates <= s_lastLoggedEstimate - 10
                || estimatedUpdates >= s_lastLoggedEstimate + 10;

            if (enteredCorrectionWindow || changedByTenOrMore) {
                log::info(
                    "FPA cube scan: hazard {} is now estimated {} update steps away (correction window: {}).",
                    nearestHazard->m_objectID,
                    estimatedUpdates,
                    correctionFrames
                );
                s_lastLoggedEstimate = estimatedUpdates;
            }
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        resetScannerState();
    }
};

$on_mod(Loaded) {
    const int frames = Mod::get()->getSettingValue<int>("correction_frames");
    log::info("Frame Perfect Assist loaded. Correction window: {} frames.", frames);
    log::info("Cube hazard scanner is diagnostic only; no automatic input is sent.");
}
