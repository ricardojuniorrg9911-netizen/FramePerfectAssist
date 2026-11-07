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

    int predictLatestSafeJumpWaitFrames(
        PlayerObject* player,
        float playerLeft,
        float playerRight,
        float playerBottom,
        float obstacleLeft,
        float obstacleRight,
        float obstacleTop,
        float horizontalVelocity
    ) {
        // The current predictor is intentionally limited to a normal-gravity cube.
        // It uses the live jump/gravity values and simulates the 240 Hz physics ticks.
        if (!player || player->m_isUpsideDown || horizontalVelocity <= 0.001f) {
            return -1;
        }

        const float sizeMultiplier = player->m_vehicleSize != 1.0f ? 0.8f : 1.0f;
        const float launchVelocity = static_cast<float>(player->m_yStart) * sizeMultiplier;
        const float gravity = static_cast<float>(player->m_gravity * player->m_gravityMod);
        if (launchVelocity <= 0.f || gravity <= 0.f) {
            return -1;
        }

        const int contactFrames = std::max(1, static_cast<int>(std::ceil(
            (obstacleLeft - playerRight) / horizontalVelocity * kPhysicsStepsPerSecond
        )));
        const float playerWidth = playerRight - playerLeft;
        const int crossingFrames = std::max(1, static_cast<int>(std::ceil(
            (obstacleRight - playerLeft) / horizontalVelocity * kPhysicsStepsPerSecond
        )));
        const int totalFrames = contactFrames + crossingFrames + 2;
        constexpr float kClearance = 1.5f;
        constexpr float kPhysicsDt = 1.f / kPhysicsStepsPerSecond;

        // Search from latest to earliest: the first safe result is the latest
        // launch tick that clears the complete horizontal overlap interval.
        for (int waitFrames = contactFrames; waitFrames >= 0; --waitFrames) {
            float verticalOffset = 0.f;
            float verticalVelocity = 0.f;
            bool launched = false;
            bool overlapped = false;
            bool safe = true;

            for (int step = 1; step <= totalFrames; ++step) {
                if (!launched && step == waitFrames + 1) {
                    verticalVelocity = launchVelocity;
                    launched = true;
                } else if (launched) {
                    // Normal cube gravity branch: velocity is integrated once per
                    // physics tick using the player's live gravity modifiers.
                    verticalVelocity -= gravity * kPhysicsDt;
                }

                if (launched) {
                    verticalOffset += verticalVelocity * kPhysicsDt;
                }

                const float futureRight = playerRight
                    + horizontalVelocity * (static_cast<float>(step) * kPhysicsDt);
                const float futureLeft = futureRight - playerWidth;

                if (futureRight >= obstacleLeft && futureLeft <= obstacleRight) {
                    overlapped = true;
                    if (playerBottom + verticalOffset < obstacleTop + kClearance) {
                        safe = false;
                        break;
                    }
                }
            }

            if (safe && overlapped) {
                return waitFrames;
            }
        }

        return -1;
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
        const int earlySafetyFrames = Mod::get()->getSettingValue<int>("correction_frames");
        const auto obstacleRect = nearestObstacle->getObjectRect();
        const int predictedWaitFrames = predictLatestSafeJumpWaitFrames(
            player,
            playerRect.getMinX(),
            playerRight,
            playerRect.getMinY(),
            obstacleRect.getMinX(),
            obstacleRect.getMaxX(),
            obstacleRect.getMaxY(),
            s_smoothedHorizontalVelocity
        );
        const int jumpWaitFrames = predictedWaitFrames >= 0
            ? std::max(0, predictedWaitFrames - earlySafetyFrames)
            : -1;

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
                && jumpWaitFrames == 0
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
                    "FPA cube predictive jump: queued for {} {} at the latest safe tick (contact in {} estimated frames, predicted wait {}, early margin {}).",
                    nearestObstacle->m_objectType == GameObjectType::Hazard ? "hazard" : "solid block",
                    nearestObstacle->m_objectID,
                    estimatedFrames,
                    predictedWaitFrames,
                    earlySafetyFrames
                );
            }

            const bool changedByTenOrMore =
                s_lastLoggedEstimate < 0
                || estimatedFrames <= s_lastLoggedEstimate - 10
                || estimatedFrames >= s_lastLoggedEstimate + 10;

            if (changedByTenOrMore) {
                log::info(
                    "FPA cube scan: obstacle {} is estimated {} physics frames away (predictor wait: {}).",
                    nearestObstacle->m_objectID,
                    estimatedFrames,
                    jumpWaitFrames
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
    log::info("Frame Perfect Assist loaded. Cube auto-jump uses a live-physics trajectory predictor; normal gravity only.");
    log::info("Cube scanner checks hazards and vertically relevant solid blocks; predictive auto-jump remains experimental.");
}
