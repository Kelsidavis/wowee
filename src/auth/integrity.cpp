#include "auth/integrity.hpp"
#include "auth/crypto.hpp"

#include <fstream>
#include <sstream>

namespace wowee {
namespace auth {

static bool readWholeFile(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        err = "missing: " + path;
        return false;
    }
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg();
    if (size < 0) size = 0;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0) {
        f.read(reinterpret_cast<char*>(out.data()), size);
        if (!f) {
            err = "read failed: " + path;
            return false;
        }
    }
    return true;
}

bool computeIntegrityHashWin32WithExe(const std::array<uint8_t, 16>& checksumSalt,
                                      const std::vector<uint8_t>& clientPublicKeyA,
                                      const std::string& miscDir,
                                      const std::string& exeName,
                                      std::array<uint8_t, 20>& outHash,
                                      std::string& outError) {
    // Files expected by 1.12.x Windows clients for the integrity check.
    // If this needs to vary by build, make it data-driven in expansion.json later.
    const char* kFiles[] = {
        nullptr, // exeName
        "fmod.dll",
        "ijl15.dll",
        "dbghelp.dll",
        "unicows.dll",
    };

    std::vector<uint8_t> allFiles;
    std::string err;
    for (size_t idx = 0; idx < (sizeof(kFiles) / sizeof(kFiles[0])); ++idx) {
        const char* name = kFiles[idx];
        std::string nameStr = name ? std::string(name) : exeName;
        std::vector<uint8_t> bytes;
        std::string path = miscDir;
        if (!path.empty() && path.back() != '/') path += '/';
        path += nameStr;
        if (!readWholeFile(path, bytes, err)) {
            outError = err;
            return false;
        }
        allFiles.insert(allFiles.end(), bytes.begin(), bytes.end());
    }

    // HMAC_SHA1(checksumSalt, allFiles)
    std::vector<uint8_t> key(checksumSalt.begin(), checksumSalt.end());
    const std::vector<uint8_t> checksum = Crypto::hmacSHA1(key, allFiles); // 20 bytes

    // SHA1(A || checksum)
    std::vector<uint8_t> shaIn;
    shaIn.reserve(clientPublicKeyA.size() + checksum.size());
    shaIn.insert(shaIn.end(), clientPublicKeyA.begin(), clientPublicKeyA.end());
    shaIn.insert(shaIn.end(), checksum.begin(), checksum.end());
    const std::vector<uint8_t> finalHash = Crypto::sha1(shaIn);

    if (finalHash.size() != outHash.size()) {
        outError = "unexpected sha1 size";
        return false;
    }
    std::copy(finalHash.begin(), finalHash.end(), outHash.begin());
    return true;
}

bool computeIntegrityHashWin32(const std::array<uint8_t, 16>& checksumSalt,
                               const std::vector<uint8_t>& clientPublicKeyA,
                               const std::string& miscDir,
                               std::array<uint8_t, 20>& outHash,
                               std::string& outError) {
    return computeIntegrityHashWin32WithExe(checksumSalt, clientPublicKeyA, miscDir, "WoW.exe", outHash, outError);
}

} // namespace auth
} // namespace wowee
