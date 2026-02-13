#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace wowee {
namespace auth {

// Computes the LOGON_PROOF "CRC hash" / integrity hash for the legacy WoW login protocol.
//
// Algorithm (per WoWDev/gtker docs):
//   checksum = HMAC_SHA1(checksumSalt, concatenated_file_bytes)
//   crc_hash = SHA1(clientPublicKey || checksum)
//
// clientPublicKey is the 32-byte A as sent on the wire.
//
// Returns false if any file is missing/unreadable.
bool computeIntegrityHashWin32(const std::array<uint8_t, 16>& checksumSalt,
                               const std::vector<uint8_t>& clientPublicKeyA,
                               const std::string& miscDir,
                               std::array<uint8_t, 20>& outHash,
                               std::string& outError);

// Same as computeIntegrityHashWin32, but allows selecting the EXE filename used in the file set.
bool computeIntegrityHashWin32WithExe(const std::array<uint8_t, 16>& checksumSalt,
                                      const std::vector<uint8_t>& clientPublicKeyA,
                                      const std::string& miscDir,
                                      const std::string& exeName,
                                      std::array<uint8_t, 20>& outHash,
                                      std::string& outError);

} // namespace auth
} // namespace wowee
