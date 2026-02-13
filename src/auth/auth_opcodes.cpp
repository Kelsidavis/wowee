#include "auth/auth_opcodes.hpp"
#include <cstdio>

namespace wowee {
namespace auth {

static char authResultBuf[256];

const char* getAuthResultString(AuthResult result) {
    switch (result) {
        case AuthResult::SUCCESS: return "Success";
        case AuthResult::UNKNOWN0: return "Unknown server error (code 0x01)";
        case AuthResult::UNKNOWN1: return "Unknown server error (code 0x02)";
        case AuthResult::ACCOUNT_BANNED: return "This account has been banned";
        case AuthResult::ACCOUNT_INVALID: return "Account not found - check your username";
        case AuthResult::PASSWORD_INVALID: return "Incorrect password";
        case AuthResult::ALREADY_ONLINE: return "This account is already logged in";
        case AuthResult::OUT_OF_CREDIT: return "Account out of credit/time";
        case AuthResult::BUSY: return "Server is busy - try again later";
        case AuthResult::BUILD_INVALID:
            return "Version mismatch - the server requires a different client build/version";
        case AuthResult::BUILD_UPDATE:
            return "Client update required - the server expects a different client build/version";
        case AuthResult::INVALID_SERVER: return "Invalid server";
        case AuthResult::ACCOUNT_SUSPENDED: return "This account has been suspended";
        case AuthResult::ACCESS_DENIED: return "Access denied";
        case AuthResult::SURVEY: return "Survey required";
        case AuthResult::PARENTAL_CONTROL: return "Blocked by parental controls";
        case AuthResult::LOCK_ENFORCED: return "Account locked - check your email";
        case AuthResult::TRIAL_EXPIRED: return "Trial period has expired";
        case AuthResult::BATTLE_NET: return "Battle.net error";
        case AuthResult::ANTI_INDULGENCE: return "Anti-indulgence time limit reached";
        case AuthResult::EXPIRED: return "Account subscription has expired";
        case AuthResult::NO_GAME_ACCOUNT: return "No game account found - create one on the server website";
        case AuthResult::CHARGEBACK: return "Account locked due to chargeback";
        case AuthResult::IGR_WITHOUT_BNET:
            return "Internet Game Room access denied - account may require "
                   "registration on the server website";
        case AuthResult::GAME_ACCOUNT_LOCKED: return "Game account is locked";
        case AuthResult::UNLOCKABLE_LOCK: return "Account is locked and cannot be unlocked";
        case AuthResult::CONVERSION_REQUIRED: return "Account conversion required";
        case AuthResult::DISCONNECTED: return "Disconnected from server";
        default:
            snprintf(authResultBuf, sizeof(authResultBuf),
                     "Server rejected login (error code 0x%02X)",
                     static_cast<unsigned>(result));
            return authResultBuf;
    }
}

} // namespace auth
} // namespace wowee
