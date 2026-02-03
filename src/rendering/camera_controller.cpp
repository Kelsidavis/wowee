#include "rendering/camera_controller.hpp"
#include "rendering/terrain_manager.hpp"
#include "rendering/wmo_renderer.hpp"
#include "rendering/m2_renderer.hpp"
#include "rendering/water_renderer.hpp"
#include "game/opcodes.hpp"
#include "core/logger.hpp"
#include <glm/glm.hpp>
#include <imgui.h>

namespace wowee {
namespace rendering {

CameraController::CameraController(Camera* cam) : camera(cam) {
    yaw = defaultYaw;
    pitch = defaultPitch;
    reset();
}

void CameraController::update(float deltaTime) {
    if (!enabled || !camera) {
        return;
    }

    auto& input = core::Input::getInstance();

    // Don't process keyboard input when UI (e.g. chat box) has focus
    bool uiWantsKeyboard = ImGui::GetIO().WantCaptureKeyboard;

    // Determine current key states
    bool nowForward = !uiWantsKeyboard && !sitting && input.isKeyPressed(SDL_SCANCODE_W);
    bool nowBackward = !uiWantsKeyboard && !sitting && input.isKeyPressed(SDL_SCANCODE_S);
    bool nowStrafeLeft = !uiWantsKeyboard && !sitting && input.isKeyPressed(SDL_SCANCODE_A);
    bool nowStrafeRight = !uiWantsKeyboard && !sitting && input.isKeyPressed(SDL_SCANCODE_D);
    bool nowJump = !uiWantsKeyboard && !sitting && input.isKeyPressed(SDL_SCANCODE_SPACE);

    // Select physics constants based on mode
    float gravity = useWoWSpeed ? WOW_GRAVITY : GRAVITY;
    float jumpVel = useWoWSpeed ? WOW_JUMP_VELOCITY : JUMP_VELOCITY;

    // Calculate movement speed based on direction and modifiers
    float speed;
    if (useWoWSpeed) {
        // Movement speeds (Shift = sprint, Ctrl = walk)
        if (nowBackward && !nowForward) {
            speed = WOW_BACK_SPEED;
        } else if (!uiWantsKeyboard && (input.isKeyPressed(SDL_SCANCODE_LSHIFT) || input.isKeyPressed(SDL_SCANCODE_RSHIFT))) {
            speed = WOW_SPRINT_SPEED;  // Shift = sprint (faster)
        } else if (!uiWantsKeyboard && (input.isKeyPressed(SDL_SCANCODE_LCTRL) || input.isKeyPressed(SDL_SCANCODE_RCTRL))) {
            speed = WOW_WALK_SPEED;    // Ctrl = walk (slower)
        } else {
            speed = WOW_RUN_SPEED;     // Normal run
        }
    } else {
        // Exploration mode (original behavior)
        speed = movementSpeed;
        if (!uiWantsKeyboard && (input.isKeyPressed(SDL_SCANCODE_LSHIFT) || input.isKeyPressed(SDL_SCANCODE_RSHIFT))) {
            speed *= sprintMultiplier;
        }
        if (!uiWantsKeyboard && (input.isKeyPressed(SDL_SCANCODE_LCTRL) || input.isKeyPressed(SDL_SCANCODE_RCTRL))) {
            speed *= slowMultiplier;
        }
    }

    // Get camera axes — project forward onto XY plane for walking
    glm::vec3 forward3D = camera->getForward();
    glm::vec3 forward = glm::normalize(glm::vec3(forward3D.x, forward3D.y, 0.0f));
    glm::vec3 right = camera->getRight();
    right.z = 0.0f;
    if (glm::length(right) > 0.001f) {
        right = glm::normalize(right);
    }

    // Toggle sit/crouch with X or C key (edge-triggered) — only when UI doesn't want keyboard
    bool xDown = !uiWantsKeyboard && (input.isKeyPressed(SDL_SCANCODE_X) || input.isKeyPressed(SDL_SCANCODE_C));
    if (xDown && !xKeyWasDown) {
        sitting = !sitting;
    }
    xKeyWasDown = xDown;

    // Update eye height based on crouch state (smooth transition)
    float targetEyeHeight = sitting ? CROUCH_EYE_HEIGHT : STAND_EYE_HEIGHT;
    float heightLerpSpeed = 10.0f * deltaTime;
    eyeHeight = eyeHeight + (targetEyeHeight - eyeHeight) * std::min(1.0f, heightLerpSpeed);

    // Calculate horizontal movement vector
    glm::vec3 movement(0.0f);

    if (nowForward) movement += forward;
    if (nowBackward) movement -= forward;
    if (nowStrafeLeft) movement -= right;
    if (nowStrafeRight) movement += right;

    // Stand up if jumping while crouched
    if (!uiWantsKeyboard && sitting && input.isKeyPressed(SDL_SCANCODE_SPACE)) {
        sitting = false;
    }

    // Third-person orbit camera mode
    if (thirdPerson && followTarget) {
        // Move the follow target (character position) instead of the camera
        glm::vec3 targetPos = *followTarget;

        // Check for water at current position
        std::optional<float> waterH;
        if (waterRenderer) {
            waterH = waterRenderer->getWaterHeightAt(targetPos.x, targetPos.y);
        }
        bool inWater = waterH && targetPos.z < *waterH;


        if (inWater) {
            swimming = true;
            // Reduce horizontal speed while swimming
            float swimSpeed = speed * SWIM_SPEED_FACTOR;

            if (glm::length(movement) > 0.001f) {
                movement = glm::normalize(movement);
                targetPos += movement * swimSpeed * deltaTime;
            }

            // Spacebar = swim up (continuous, not a jump)
            if (nowJump) {
                verticalVelocity = SWIM_BUOYANCY;
            } else {
                // Gentle sink when not pressing space
                verticalVelocity += SWIM_GRAVITY * deltaTime;
                if (verticalVelocity < SWIM_SINK_SPEED) {
                    verticalVelocity = SWIM_SINK_SPEED;
                }
            }

            targetPos.z += verticalVelocity * deltaTime;

            // Don't rise above water surface
            if (waterH && targetPos.z > *waterH - WATER_SURFACE_OFFSET) {
                targetPos.z = *waterH - WATER_SURFACE_OFFSET;
                if (verticalVelocity > 0.0f) verticalVelocity = 0.0f;
            }

            grounded = false;
        } else {
            swimming = false;

            if (glm::length(movement) > 0.001f) {
                movement = glm::normalize(movement);
                targetPos += movement * speed * deltaTime;
            }

            // Jump
            if (nowJump && grounded) {
                verticalVelocity = jumpVel;
                grounded = false;
            }

            // Apply gravity
            verticalVelocity += gravity * deltaTime;
            targetPos.z += verticalVelocity * deltaTime;
        }

        // Wall collision for character (WMO buildings)
        if (wmoRenderer) {
            glm::vec3 feetPos = targetPos;
            glm::vec3 oldFeetPos = *followTarget;
            glm::vec3 adjusted;
            if (wmoRenderer->checkWallCollision(oldFeetPos, feetPos, adjusted)) {
                targetPos.x = adjusted.x;
                targetPos.y = adjusted.y;
                targetPos.z = adjusted.z;
            }
        }

        // Collision with M2 doodads (fences, boxes, etc.)
        if (m2Renderer) {
            glm::vec3 adjusted;
            if (m2Renderer->checkCollision(*followTarget, targetPos, adjusted)) {
                targetPos.x = adjusted.x;
                targetPos.y = adjusted.y;
            }
        }

        // Ground the character to terrain or WMO floor
        {
            std::optional<float> terrainH;
            std::optional<float> wmoH;

            if (terrainManager) {
                terrainH = terrainManager->getHeightAt(targetPos.x, targetPos.y);
            }
            if (wmoRenderer) {
                wmoH = wmoRenderer->getFloorHeight(targetPos.x, targetPos.y, targetPos.z + eyeHeight);
            }

            std::optional<float> groundH;
            if (terrainH && wmoH) {
                groundH = std::max(*terrainH, *wmoH);
            } else if (terrainH) {
                groundH = terrainH;
            } else if (wmoH) {
                groundH = wmoH;
            }

            if (groundH) {
                // Smooth ground height to prevent stumbling on uneven terrain
                float groundDiff = *groundH - lastGroundZ;
                if (std::abs(groundDiff) < 2.0f) {
                    // Small height difference - smooth it
                    lastGroundZ += groundDiff * std::min(1.0f, deltaTime * 15.0f);
                } else {
                    // Large height difference (stairs, ledges) - snap
                    lastGroundZ = *groundH;
                }

                if (targetPos.z <= lastGroundZ + 0.1f) {
                    targetPos.z = lastGroundZ;
                    verticalVelocity = 0.0f;
                    grounded = true;
                    swimming = false;  // Touching ground = wading, not swimming
                } else if (!swimming) {
                    grounded = false;
                }
            } else if (!swimming) {
                // No terrain found — hold at last known ground
                targetPos.z = lastGroundZ;
                verticalVelocity = 0.0f;
                grounded = true;
            }
        }

        // Update follow target position
        *followTarget = targetPos;

        // Compute camera position orbiting behind the character
        glm::vec3 lookAtPoint = targetPos + glm::vec3(0.0f, 0.0f, eyeHeight);

        // Camera collision detection - raycast from character head to desired camera position
        glm::vec3 rayDir = -forward3D;  // Direction from character toward camera
        float desiredDist = orbitDistance;
        float actualDist = desiredDist;
        const float cameraOffset = 0.3f;  // Small offset to not clip into walls

        // Raycast against WMO bounding boxes
        if (wmoRenderer) {
            float wmoHit = wmoRenderer->raycastBoundingBoxes(lookAtPoint, rayDir, desiredDist);
            if (wmoHit < actualDist) {
                actualDist = std::max(minOrbitDistance, wmoHit - cameraOffset);
            }
        }

        // Raycast against M2 bounding boxes (larger objects only affect camera)
        if (m2Renderer) {
            float m2Hit = m2Renderer->raycastBoundingBoxes(lookAtPoint, rayDir, desiredDist);
            if (m2Hit < actualDist) {
                actualDist = std::max(minOrbitDistance, m2Hit - cameraOffset);
            }
        }

        glm::vec3 camPos = lookAtPoint + rayDir * actualDist;

        // Clamp camera above terrain/WMO floor
        {
            float minCamZ = camPos.z;
            if (terrainManager) {
                auto h = terrainManager->getHeightAt(camPos.x, camPos.y);
                if (h) minCamZ = *h + 1.0f; // 1 unit above ground
            }
            if (wmoRenderer) {
                auto wh = wmoRenderer->getFloorHeight(camPos.x, camPos.y, camPos.z + eyeHeight);
                if (wh && (*wh + 1.0f) > minCamZ) minCamZ = *wh + 1.0f;
            }
            if (camPos.z < minCamZ) {
                camPos.z = minCamZ;
            }
        }

        camera->setPosition(camPos);
    } else {
        // Free-fly camera mode (original behavior)
        glm::vec3 newPos = camera->getPosition();
        float feetZ = newPos.z - eyeHeight;

        // Check for water at feet position
        std::optional<float> waterH;
        if (waterRenderer) {
            waterH = waterRenderer->getWaterHeightAt(newPos.x, newPos.y);
        }
        bool inWater = waterH && feetZ < *waterH;


        if (inWater) {
            swimming = true;
            float swimSpeed = speed * SWIM_SPEED_FACTOR;

            if (glm::length(movement) > 0.001f) {
                movement = glm::normalize(movement);
                newPos += movement * swimSpeed * deltaTime;
            }

            if (nowJump) {
                verticalVelocity = SWIM_BUOYANCY;
            } else {
                verticalVelocity += SWIM_GRAVITY * deltaTime;
                if (verticalVelocity < SWIM_SINK_SPEED) {
                    verticalVelocity = SWIM_SINK_SPEED;
                }
            }

            newPos.z += verticalVelocity * deltaTime;

            // Don't rise above water surface (feet at water level)
            if (waterH && (newPos.z - eyeHeight) > *waterH - WATER_SURFACE_OFFSET) {
                newPos.z = *waterH - WATER_SURFACE_OFFSET + eyeHeight;
                if (verticalVelocity > 0.0f) verticalVelocity = 0.0f;
            }

            grounded = false;
        } else {
            swimming = false;

            if (glm::length(movement) > 0.001f) {
                movement = glm::normalize(movement);
                newPos += movement * speed * deltaTime;
            }

            // Jump
            if (nowJump && grounded) {
                verticalVelocity = jumpVel;
                grounded = false;
            }

            // Apply gravity
            verticalVelocity += gravity * deltaTime;
            newPos.z += verticalVelocity * deltaTime;
        }

        // Wall collision — push out of WMO walls before grounding
        if (wmoRenderer) {
            glm::vec3 feetPos = newPos - glm::vec3(0, 0, eyeHeight);
            glm::vec3 oldFeetPos = camera->getPosition() - glm::vec3(0, 0, eyeHeight);
            glm::vec3 adjusted;
            if (wmoRenderer->checkWallCollision(oldFeetPos, feetPos, adjusted)) {
                newPos.x = adjusted.x;
                newPos.y = adjusted.y;
                newPos.z = adjusted.z + eyeHeight;
            }
        }

        // Ground to terrain or WMO floor
        {
            std::optional<float> terrainH;
            std::optional<float> wmoH;

            if (terrainManager) {
                terrainH = terrainManager->getHeightAt(newPos.x, newPos.y);
            }
            if (wmoRenderer) {
                wmoH = wmoRenderer->getFloorHeight(newPos.x, newPos.y, newPos.z);
            }

            std::optional<float> groundH;
            if (terrainH && wmoH) {
                groundH = std::max(*terrainH, *wmoH);
            } else if (terrainH) {
                groundH = terrainH;
            } else if (wmoH) {
                groundH = wmoH;
            }

            if (groundH) {
                lastGroundZ = *groundH;
                float groundZ = *groundH + eyeHeight;
                if (newPos.z <= groundZ) {
                    newPos.z = groundZ;
                    verticalVelocity = 0.0f;
                    grounded = true;
                    swimming = false;  // Touching ground = wading
                } else if (!swimming) {
                    grounded = false;
                }
            } else if (!swimming) {
                float groundZ = lastGroundZ + eyeHeight;
                newPos.z = groundZ;
                verticalVelocity = 0.0f;
                grounded = true;
            }
        }

        camera->setPosition(newPos);
    }

    // --- Edge-detection: send movement opcodes on state transitions ---
    if (movementCallback) {
        // Forward/backward
        if (nowForward && !wasMovingForward) {
            movementCallback(static_cast<uint32_t>(game::Opcode::CMSG_MOVE_START_FORWARD));
        }
        if (nowBackward && !wasMovingBackward) {
            movementCallback(static_cast<uint32_t>(game::Opcode::CMSG_MOVE_START_BACKWARD));
        }
        if ((!nowForward && wasMovingForward) || (!nowBackward && wasMovingBackward)) {
            if (!nowForward && !nowBackward) {
                movementCallback(static_cast<uint32_t>(game::Opcode::CMSG_MOVE_STOP));
            }
        }

        // Strafing
        if (nowStrafeLeft && !wasStrafingLeft) {
            movementCallback(static_cast<uint32_t>(game::Opcode::CMSG_MOVE_START_STRAFE_LEFT));
        }
        if (nowStrafeRight && !wasStrafingRight) {
            movementCallback(static_cast<uint32_t>(game::Opcode::CMSG_MOVE_START_STRAFE_RIGHT));
        }
        if ((!nowStrafeLeft && wasStrafingLeft) || (!nowStrafeRight && wasStrafingRight)) {
            if (!nowStrafeLeft && !nowStrafeRight) {
                movementCallback(static_cast<uint32_t>(game::Opcode::CMSG_MOVE_STOP_STRAFE));
            }
        }

        // Jump
        if (nowJump && !wasJumping && grounded) {
            movementCallback(static_cast<uint32_t>(game::Opcode::CMSG_MOVE_JUMP));
        }

        // Fall landing
        if (wasFalling && grounded) {
            movementCallback(static_cast<uint32_t>(game::Opcode::CMSG_MOVE_FALL_LAND));
        }
    }

    // Swimming state transitions
    if (movementCallback) {
        if (swimming && !wasSwimming) {
            movementCallback(static_cast<uint32_t>(game::Opcode::CMSG_MOVE_START_SWIM));
        } else if (!swimming && wasSwimming) {
            movementCallback(static_cast<uint32_t>(game::Opcode::CMSG_MOVE_STOP_SWIM));
        }
    }

    // Update previous-frame state
    wasSwimming = swimming;
    wasMovingForward = nowForward;
    wasMovingBackward = nowBackward;
    wasStrafingLeft = nowStrafeLeft;
    wasStrafingRight = nowStrafeRight;
    wasJumping = nowJump;
    wasFalling = !grounded && verticalVelocity <= 0.0f;

    // Reset camera (R key)
    if (!uiWantsKeyboard && input.isKeyPressed(SDL_SCANCODE_R)) {
        reset();
    }
}

void CameraController::processMouseMotion(const SDL_MouseMotionEvent& event) {
    if (!enabled || !camera) {
        return;
    }

    if (!mouseButtonDown) {
        return;
    }

    // Directly update stored yaw/pitch (no lossy forward-vector derivation)
    yaw -= event.xrel * mouseSensitivity;
    pitch += event.yrel * mouseSensitivity;

    pitch = glm::clamp(pitch, -89.0f, 89.0f);

    camera->setRotation(yaw, pitch);
}

void CameraController::processMouseButton(const SDL_MouseButtonEvent& event) {
    if (!enabled) {
        return;
    }

    if (event.button == SDL_BUTTON_LEFT) {
        leftMouseDown = (event.state == SDL_PRESSED);
    }
    if (event.button == SDL_BUTTON_RIGHT) {
        rightMouseDown = (event.state == SDL_PRESSED);
    }

    bool anyDown = leftMouseDown || rightMouseDown;
    if (anyDown && !mouseButtonDown) {
        SDL_SetRelativeMouseMode(SDL_TRUE);
    } else if (!anyDown && mouseButtonDown) {
        SDL_SetRelativeMouseMode(SDL_FALSE);
    }
    mouseButtonDown = anyDown;
}

void CameraController::reset() {
    if (!camera) {
        return;
    }

    yaw = defaultYaw;
    pitch = defaultPitch;
    verticalVelocity = 0.0f;
    grounded = false;

    glm::vec3 spawnPos = defaultPosition;

    // Snap spawn to terrain or WMO surface
    std::optional<float> h;
    if (terrainManager) {
        h = terrainManager->getHeightAt(spawnPos.x, spawnPos.y);
    }
    if (wmoRenderer) {
        auto wh = wmoRenderer->getFloorHeight(spawnPos.x, spawnPos.y, spawnPos.z);
        if (wh && (!h || *wh > *h)) {
            h = wh;
        }
    }
    if (h) {
        lastGroundZ = *h;
        spawnPos.z = *h + eyeHeight;
    }

    camera->setPosition(spawnPos);
    camera->setRotation(yaw, pitch);

    LOG_INFO("Camera reset to default position");
}

void CameraController::processMouseWheel(float delta) {
    orbitDistance -= delta * zoomSpeed;
    orbitDistance = glm::clamp(orbitDistance, minOrbitDistance, maxOrbitDistance);
}

void CameraController::setFollowTarget(glm::vec3* target) {
    followTarget = target;
    if (target) {
        thirdPerson = true;
        LOG_INFO("Third-person camera enabled");
    } else {
        thirdPerson = false;
        LOG_INFO("Free-fly camera enabled");
    }
}

bool CameraController::isMoving() const {
    if (!enabled || !camera) {
        return false;
    }

    if (ImGui::GetIO().WantCaptureKeyboard) {
        return false;
    }

    auto& input = core::Input::getInstance();

    return input.isKeyPressed(SDL_SCANCODE_W) ||
           input.isKeyPressed(SDL_SCANCODE_S) ||
           input.isKeyPressed(SDL_SCANCODE_A) ||
           input.isKeyPressed(SDL_SCANCODE_D);
}

bool CameraController::isSprinting() const {
    if (!enabled || !camera) {
        return false;
    }
    if (ImGui::GetIO().WantCaptureKeyboard) {
        return false;
    }
    auto& input = core::Input::getInstance();
    return isMoving() && (input.isKeyPressed(SDL_SCANCODE_LSHIFT) || input.isKeyPressed(SDL_SCANCODE_RSHIFT));
}

} // namespace rendering
} // namespace wowee
