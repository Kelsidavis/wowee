#pragma once

#include "network/socket.hpp"
#include "network/packet.hpp"
#include "network/net_platform.hpp"
#include "auth/rc4.hpp"
#include "auth/vanilla_crypt.hpp"
#include <functional>
#include <vector>
#include <cstdint>

namespace wowee {
namespace network {

/**
 * World Server Socket
 *
 * Handles WoW world server protocol with header encryption.
 * Supports both vanilla/TBC (XOR+addition cipher) and WotLK (RC4).
 *
 * Key Differences from Auth Server:
 * - Outgoing: 6-byte header (2 bytes size + 4 bytes opcode, big-endian)
 * - Incoming: 4-byte header (2 bytes size + 2 bytes opcode)
 * - Headers are encrypted after CMSG_AUTH_SESSION
 * - Packet bodies remain unencrypted
 * - Size field includes opcode bytes (payloadLen = size - 2)
 */
class WorldSocket : public Socket {
public:
    WorldSocket();
    ~WorldSocket() override;

    bool connect(const std::string& host, uint16_t port) override;
    void disconnect() override;
    bool isConnected() const override;

    /**
     * Send a world packet
     * Automatically encrypts 6-byte header if encryption is enabled
     *
     * @param packet Packet to send
     */
    void send(const Packet& packet) override;

    /**
     * Update socket - receive data and parse packets
     * Should be called regularly (e.g., each frame)
     */
    void update();

    /**
     * Set callback for complete packets
     *
     * @param callback Function to call when packet is received
     */
    void setPacketCallback(std::function<void(const Packet&)> callback) {
        packetCallback = callback;
    }

    /**
     * Initialize header encryption for packet headers
     * Must be called after CMSG_AUTH_SESSION before further communication
     *
     * @param sessionKey 40-byte session key from auth server
     * @param build Client build number (determines cipher: <= 8606 = XOR, > 8606 = RC4)
     */
    void initEncryption(const std::vector<uint8_t>& sessionKey, uint32_t build = 12340);

    /**
     * Check if header encryption is enabled
     */
    bool isEncryptionEnabled() const { return encryptionEnabled; }

private:
    /**
     * Try to parse complete packets from receive buffer
     */
    void tryParsePackets();

    socket_t sockfd = INVALID_SOCK;
    bool connected = false;
    bool encryptionEnabled = false;
    bool useVanillaCrypt = false;  // true = XOR cipher, false = RC4

    // WotLK RC4 ciphers for header encryption/decryption
    auth::RC4 encryptCipher;
    auth::RC4 decryptCipher;

    // Vanilla/TBC XOR+addition cipher
    auth::VanillaCrypt vanillaCrypt;

    // Receive buffer
    std::vector<uint8_t> receiveBuffer;

    // Track how many header bytes have been decrypted (0-4)
    // This prevents re-decrypting the same header when waiting for more data
    size_t headerBytesDecrypted = 0;

    // Debug-only tracing window for post-auth packet framing verification.
    int headerTracePacketsLeft = 0;

    // Packet callback
    std::function<void(const Packet&)> packetCallback;
};

} // namespace network
} // namespace wowee
