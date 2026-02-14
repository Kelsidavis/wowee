#include "auth/vanilla_crypt.hpp"

namespace wowee {
namespace auth {

void VanillaCrypt::init(const std::vector<uint8_t>& sessionKey) {
    key_ = sessionKey;
    sendIndex_ = 0;
    sendPrev_ = 0;
    recvIndex_ = 0;
    recvPrev_ = 0;
}

void VanillaCrypt::encrypt(uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        uint8_t x = (data[i] ^ key_[sendIndex_]) + sendPrev_;
        sendIndex_ = (sendIndex_ + 1) % key_.size();
        data[i] = x;
        sendPrev_ = x;
    }
}

void VanillaCrypt::decrypt(uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        uint8_t enc = data[i];
        uint8_t x = (enc - recvPrev_) ^ key_[recvIndex_];
        recvIndex_ = (recvIndex_ + 1) % key_.size();
        recvPrev_ = enc;
        data[i] = x;
    }
}

} // namespace auth
} // namespace wowee
