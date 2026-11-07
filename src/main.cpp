#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace geode::prelude;

namespace {
    // Diagnostic-only cube stage. No input is sent until jump timing is validated.
    constexpr float kPhysicsStepsPerSecond = 240.f;
    constexpr float kVelocitySmoothing = 0.25f;

    int s_lastReportedHazardID = -1;
    int s_lastLoggedEstimate = -1;
    float s_smoothedHorizontalVelocity = 0.f;
    float s_lastPlayerRight = 0.f;
    bool s_hasLastPlayerPosition = false;
    bool s_jumpReleasePending = false;
    int s_lastAutoJumpHazardID = -1;

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
        s_smoothedHorizontalVelocity = 0.f;
        s_lastPlayerRight = 0.f;
        s_hasLastPlayerPosition = false;
        s_lastAutoJumpHazardID = -1;
    }
}

class $modify(FramePerfectPlayLayer, PlayLayer) {
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        // Always release a queued test jump on the following update.
        if (s_jumpReleasePending) {
            this->queueButton(1, false, false, utils::getInputTimestamp());
            if (auto* uiLayer = this->getChildByType<UILayer>(0)) {
                uiLayer->m_p1Jumping = false;
            }
            s_jumpReleasePending = false;
        }

        if (!Mod::get()->getSettingValue<bool>("auto_click")) {
            resetScannerState();
            s_hasLastPlayerPosition = false;
            return;
        }

        auto* player = m_player1;
        if (!player || player->m_isDead || !isCube(player) || !m_objects) {
            resetScannerState();
            s_hasLastPlayerPosition = false;
            return;
        }

        const float playerRight = player->getObjectRect().getMaxX();
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

        GameObject* nearestHazard = nullptr;
        float nearestDistance = std::numeric_limits<float>::max();

        for (unsigned int i = 0; i < m_objects->count(); ++i) {
            auto* object = static_cast<GameObject*>(m_objects->objectAtIndex(i));
            if (!object || object->m_objectType != GameObjectType::Hazard) {
                continue;
            }

            const auto& hazardRect = object->getObjectRect();
            if (hazardRect.getMaxX() <= playerRight) {
                continue;
            }

            // If the player's hitbox has begun overlapping this hazard,
            // keep it as the immediate target instead of producing a negative
            // distance that could distort the estimated arrival time.
            const float distance = std::max(0.f, hazardRect.getMinX() - playerRight);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestHazard = object;
            }
        }

        if (!nearestHazard) {
            s_lastReportedHazardID = -1;
            s_lastLoggedEstimate = -1;
            s_lastAutoJumpHazardID = -1;
            return;
        }

        const int hazardID = nearestHazard->m_uniqueID;
        const int estimatedFrames = s_smoothedHorizontalVelocity > 0.001f
            ? static_cast<int>(std::ceil(
                nearestDistance / s_smoothedHorizontalVelocity * kPhysicsStepsPerSecond
            ))
            : -1;
        const int correctionFrames = Mod::get()->getSettingValue<int>("correction_frames");

        if (hazardID != s_lastReportedHazardID) {
            s_lastReportedHazardID = hazardID;
            s_lastLoggedEstimate = estimatedFrames;
            if (estimatedFrames >= 0) {
                log::info(
                    "FPA cube scan: hazard {} is {:.2f} units ahead; estimated {} physics frames away.",
                    nearestHazard->m_objectID,
                    nearestDistance,
                    estimatedFrames
                );
            } else {
                log::info(
                    "FPA cube scan: hazard {} is {:.2f} units ahead; measuring speed.",
                    nearestHazard->m_objectID,
                    nearestDistance
                );
            }
            return;
        }

        if (estimatedFrames >= 0) {
            // Experimental cube-only automatic tap. It is separately opt-in,
            // only fires once per hazard, and requires the cube to be grounded.
            if (Mod::get()->getSettingValue<bool>("auto_jump")
                && estimatedFrames <= correctionFrames
                && player->m_isOnGround
                && s_lastAutoJumpHazardID != hazardID
                && m_started
                && !m_isPaused) {
                this->queueButton(1, true, false, utils::getInputTimestamp());
                if (auto* uiLayer = this->getChildByType<UILayer>(0)) {
                    uiLayer->m_p1Jumping = true;
                }
                s_jumpReleasePending = true;
                s_lastAutoJumpHazardID = hazardID;
                log::info(
                    "FPA cube auto-jump test: queued one jump for hazard {} at estimated {} frames.",
                    nearestHazard->m_objectID,
                    estimatedFrames
                );
            }

            const bool enteredCorrectionWindow =
                s_lastLoggedEstimate > correctionFrames
                && estimatedFrames <= correctionFrames;
            const bool changedByTenOrMore =
                s_lastLoggedEstimate < 0
                || estimatedFrames <= s_lastLoggedEstimate - 10
                || estimatedFrames >= s_lastLoggedEstimate + 10;

            if (enteredCorrectionWindow || changedByTenOrMore) {
                log::info(
                    "FPA cube scan: hazard {} is now estimated {} physics frames away (configured window: {}).",
                    nearestHazard->m_objectID,
                    estimatedFrames,
                    correctionFrames
                );

                if (enteredCorrectionWindow) {
                    log::info(
                        "FPA cube timing diagnostic: hazard {} entered the configured window; cube grounded: {}. This is a timing marker only, not a validated jump command.",
                        nearestHazard->m_objectID,
                        player->m_isOnGround ? "yes" : "no"
                    );
                }

                s_lastLoggedEstimate = estimatedFrames;
            }
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        resetScannerState();
        s_hasLastPlayerPosition = false;
        s_lastPlayerRight = 0.f;
    }

};

$on_mod(Loaded) {
    const int frames = Mod::get()->getSettingValue<int>("correction_frames");
    log::info("Frame Perfect Assist loaded. Correction window: {} physics frames.", frames);
    log::info("Cube hazard scanner is diagnostic. Experimental auto-jump is separately opt-in and disabled by default.");
}
