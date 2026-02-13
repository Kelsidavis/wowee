#include "auth/auth_packets.hpp"
#include "network/tcp_socket.hpp"
#include "network/packet.hpp"
#include "core/logger.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

using namespace wowee;

static void usage() {
    std::cerr
        << "Usage:\n"
        << "  auth_probe <host> <port> <account> <major> <minor> <patch> <build> <proto> <locale> [platform] [os]\n"
        << "Example:\n"
        << "  auth_probe logon.turtle-server-eu.kz 3724 test 1 12 1 5875 8 enGB x86 Win\n";
}

int main(int argc, char** argv) {
    if (argc < 10) {
        usage();
        return 2;
    }

    const std::string host = argv[1];
    const int port = std::atoi(argv[2]);
    const std::string account = argv[3];
    const int major = std::atoi(argv[4]);
    const int minor = std::atoi(argv[5]);
    const int patch = std::atoi(argv[6]);
    const int build = std::atoi(argv[7]);
    const int proto = std::atoi(argv[8]);
    const std::string locale = argv[9];
    const std::string platform = (argc >= 11) ? argv[10] : "x86";
    const std::string os = (argc >= 12) ? argv[11] : "Win";

    auth::ClientInfo info;
    info.majorVersion = static_cast<uint8_t>(major);
    info.minorVersion = static_cast<uint8_t>(minor);
    info.patchVersion = static_cast<uint8_t>(patch);
    info.build = static_cast<uint16_t>(build);
    info.protocolVersion = static_cast<uint8_t>(proto);
    info.locale = locale;
    info.platform = platform;
    info.os = os;

    std::atomic<bool> done{false};
    std::atomic<int> resultCode{0xFF};
    std::atomic<bool> gotResponse{false};

    network::TCPSocket sock;
    sock.setPacketCallback([&](const network::Packet& p) {
        network::Packet pkt = p;
        if (pkt.getSize() < 3) {
            return;
        }
        uint8_t opcode = pkt.readUInt8();
        if (opcode != static_cast<uint8_t>(auth::AuthOpcode::LOGON_CHALLENGE)) {
            return;
        }

        auth::LogonChallengeResponse resp{};
        if (!auth::LogonChallengeResponseParser::parse(pkt, resp)) {
            std::cerr << "Parse failed\n";
            resultCode = 0xFE;
        } else {
            resultCode = static_cast<int>(resp.result);
            if (resp.isSuccess()) {
                std::cerr << "SUCCESS secFlags=0x" << std::hex << (int)resp.securityFlags << std::dec << "\n";
            } else {
                std::cerr << "FAIL code=0x" << std::hex << (int)resp.result << std::dec
                          << " (" << auth::getAuthResultString(resp.result) << ")\n";
            }
        }
        gotResponse = true;
        done = true;
    });

    if (!sock.connect(host, static_cast<uint16_t>(port))) {
        std::cerr << "Connect failed\n";
        return 3;
    }

    auto challenge = auth::LogonChallengePacket::build(account, info);
    sock.send(challenge);

    auto start = std::chrono::steady_clock::now();
    while (!done) {
        sock.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > 4000) {
            break;
        }
    }

    sock.disconnect();

    if (!gotResponse) {
        std::cerr << "Timeout waiting for response\n";
        return 4;
    }

    return resultCode.load();
}

