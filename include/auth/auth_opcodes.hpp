#pragma once

#include <cstdint>

namespace wowee {
namespace auth {

// Authentication server opcodes
enum class AuthOpcode : uint8_t {
    LOGON_CHALLENGE = 0x00,
    LOGON_PROOF = 0x01,
    RECONNECT_CHALLENGE = 0x02,
    RECONNECT_PROOF = 0x03,
    AUTHENTICATOR = 0x04,  // TrinityCore-style Google Authenticator token
    REALM_LIST = 0x10,
};

// LOGON_CHALLENGE response status codes
enum class AuthResult : uint8_t {
    SUCCESS = 0x00,
    UNKNOWN0 = 0x01,
    UNKNOWN1 = 0x02,
    ACCOUNT_BANNED = 0x03,
    ACCOUNT_INVALID = 0x04,
    PASSWORD_INVALID = 0x05,
    ALREADY_ONLINE = 0x06,
    OUT_OF_CREDIT = 0x07,
    BUSY = 0x08,
    BUILD_INVALID = 0x09,
    BUILD_UPDATE = 0x0A,
    INVALID_SERVER = 0x0B,
    ACCOUNT_SUSPENDED = 0x0C,
    ACCESS_DENIED = 0x0D,
    SURVEY = 0x0E,
    PARENTAL_CONTROL = 0x0F,
    LOCK_ENFORCED = 0x10,
    TRIAL_EXPIRED = 0x11,
    BATTLE_NET = 0x12,
    ANTI_INDULGENCE = 0x13,
    EXPIRED = 0x14,
    NO_GAME_ACCOUNT = 0x15,
    CHARGEBACK = 0x16,
    IGR_WITHOUT_BNET = 0x17,
    GAME_ACCOUNT_LOCKED = 0x18,
    UNLOCKABLE_LOCK = 0x19,
    CONVERSION_REQUIRED = 0x20,
    DISCONNECTED = 0xFF,
};

const char* getAuthResultString(AuthResult result);

} // namespace auth
} // namespace wowee
