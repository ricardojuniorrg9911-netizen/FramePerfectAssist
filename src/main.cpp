#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace geode::prelude;

namespace {
    constexpr float kPhysicsStepsPerSecond = 240.f;
    constexpr float kVelocitySmoothing = 0.25f;

    int s_lastReportedObjectID = -1;
    int s_lastLoggedEstimate = -1;
    float s_smoothedHorizontalVelocity = 0.f;
    float s_lastPlayerRight = 0.f;
    bool s_hasLastPlayerPosition = false;
    bool s_jumpReleasePending = false;
    int s_lastAutoJumpObjectID = -1;

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
        s_lastReportedObjectID = -1;
        s_lastLoggedEstimate = -1;
        s_smoothedHorizontalVelocity = 0.f;
        s_lastPlayerRight = 0.f;
        s_hasLastPlayerPosition = false;
        s_lastAutoJumpObjectID = -1;
    }
}

class $modify(FramePerfectPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (s_jumpReleasePending) {
            this->queueButton(1, false, false, utils::getInputTimestamp());
            if (auto* uiLayer = this->getChildByType<UILayer>(0)) {
                uiLayer->m_p1Jumping = false;
            }
            s_jumpReleasePending = false;
        }

        if (!Mod::get()->getSettingValue<bool>("auto_click")) {
            resetScannerState();
            return;
        }

        auto* player = m_player1;
        if (!player || player->m_isDead || !isCube(player) || !m_objects) {
            resetScannerState();
            return;
        }

        const auto playerRect = player->getObjectRect();
        const float playerRight = playerRect.getMaxX();
        const float previousRight = s_lastPlayerRight;
        const bool hadPreviousPosition = s_hasLastPlayerPosition;
        s_lastPlayerRight = playerRight;
        s_hasLastPlayerPosition = true;

        if (hadPreviousPosition && dt > 0.0001f) {
            const float deltaX = playerRight - previousRight;
            if (deltaX > 0.f) {
                const float measuredVelocity = deltaX / dt;
                if (s_smoothedHorizontalVelocity <= 0.f) {
                    s_smoothedHorizontalVelocity = measuredVelocity;
                } else {
                    s_smoothedHorizontalVelocity =
                        s_smoothedHorizontalVelocity * (1.f - kVelocitySmoothing)
                        + measuredVelocity * kVelocitySmoothing;
                }
            }
        }

        GameObject* nearestObstacle = nullptr;
        float nearestDistance = std::numeric_limits<float>::max();

        for (unsigned int i = 0; i < m_objects->count(); ++i) {
            auto* object = static_cast<GameObject*>(m_objects->objectAtIndex(i));
            if (!object) {
                continue;
            }

            const bool isHazard = object->m_objectType == GameObjectType::Hazard;
            const bool isSolidBlock = object->m_objectType == GameObjectType::Solid;
            if (!isHazard && !isSolidBlock) {
                continue;
            }

            const auto objectRect = object->getObjectRect();

            // Ignore solid floor/platform pieces that do not overlap the
            // cube's current vertical band. Keep hazards regardless.
            if (isSolidBlock
                && (objectRect.getMaxY() <= playerRect.getMinY()
                    || objectRect.getMinY() >= playerRect.getMaxY())) {
                continue;
            }

            if (objectRect.getMaxX() <= playerRight) {
                continue;
            }

            const float distance = std::max(0.f, objectRect.getMinX() - playerRight);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestObstacle = object;
            }
        }

        if (!nearestObstacle) {
            s_lastReportedObjectID = -1;
            s_lastLoggedEstimate = -1;
            s_lastAutoJumpObjectID = -1;
            return;
        }

        const int objectID = nearestObstacle->m_uniqueID;
        const int estimatedFrames = s_smoothedHorizontalVelocity > 0.001f
            ? static_cast<int>(std::ceil(
                nearestDistance / s_smoothedHorizontalVelocity * kPhysicsStepsPerSecond
            ))
            : -1;
        const int correctionFrames = Mod::get()->getSettingValue<int>("correction_frames");
        const int anticipationFrames = Mod::get()->getSettingValue<int>("jump_anticipation_frames");
        const int jumpTriggerFrames = anticipationFrames + correctionFrames;

        if (objectID != s_lastReportedObjectID) {
            s_lastReportedObjectID = objectID;
            s_lastLoggedEstimate = estimatedFrames;
            if (estimatedFrames >= 0) {
                log::info(
                    "FPA cube scan: {} {} is {:.2f} units ahead; estimated {} physics frames away.",
                    nearestObstacle->m_objectType == GameObjectType::Hazard ? "hazard" : "solid block",
                    nearestObstacle->m_objectID,
                    nearestDistance,
                    estimatedFrames
                );
            }
            return;
        }

        if (estimatedFrames >= 0) {
            if (Mod::get()->getSettingValue<bool>("auto_jump")
                && estimatedFrames <= jumpTriggerFrames
                && player->m_isOnGround
                && s_lastAutoJumpObjectID != objectID
                && m_started
                && !m_isPaused) {
                this->queueButton(1, true, false, utils::getInputTimestamp());
                if (auto* uiLayer = this->getChildByType<UILayer>(0)) {
                    uiLayer->m_p1Jumping = true;
                }
                s_jumpReleasePending = true;
                s_lastAutoJumpObjectID = objectID;
                log::info(
                    "FPA cube auto-jump: queued for {} {} at {} estimated frames (trigger: {}).",
                    nearestObstacle->m_objectType == GameObjectType::Hazard ? "hazard" : "solid block",
                    nearestObstacle->m_objectID,
                    estimatedFrames,
                    jumpTriggerFrames
                );
            }

            const bool changedByTenOrMore =
                s_lastLoggedEstimate < 0
                || estimatedFrames <= s_lastLoggedEstimate - 10
                || estimatedFrames >= s_lastLoggedEstimate + 10;

            if (changedByTenOrMore) {
                log::info(
                    "FPA cube scan: obstacle {} is estimated {} physics frames away (jump trigger: {}).",
                    nearestObstacle->m_objectID,
                    estimatedFrames,
                    jumpTriggerFrames
                );
                s_lastLoggedEstimate = estimatedFrames;
            }
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        resetScannerState();
        s_lastPlayerRight = 0.f;
    }
};

$on_mod(Loaded) {
    const int correction = Mod::get()->getSettingValue<int>("correction_frames");
    const int anticipation = Mod::get()->getSettingValue<int>("jump_anticipation_frames");
    log::info(
        "Frame Perfect Assist loaded. Cube jump trigger: {} physics frames (base {} + margin {}).",
        anticipation + correction,
        anticipation,
        correction
    );
    log::info("Cube scanner checks hazards and vertically relevant solid blocks; auto-jump remains experimental.");
}
