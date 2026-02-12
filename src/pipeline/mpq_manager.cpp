#include "pipeline/mpq_manager.hpp"
#include "core/logger.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>

#ifdef HAVE_STORMLIB
#include <StormLib.h>
#endif

// Define HANDLE and INVALID_HANDLE_VALUE for both cases
#ifndef HAVE_STORMLIB
typedef void* HANDLE;
#endif

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(long long)-1)
#endif

namespace wowee {
namespace pipeline {

namespace {
std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string normalizeVirtualFilenameForLookup(std::string value) {
    // StormLib uses backslash-separated virtual paths; treat lookups as case-insensitive.
    std::replace(value.begin(), value.end(), '/', '\\');
    value = toLowerCopy(std::move(value));
    while (!value.empty() && (value.front() == '\\' || value.front() == '/')) {
        value.erase(value.begin());
    }
    return value;
}

bool envFlagEnabled(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) {
        return false;
    }
    std::string s = toLowerCopy(v);
    return s == "1" || s == "true" || s == "yes" || s == "on";
}
}

MPQManager::MPQManager() = default;

MPQManager::~MPQManager() {
    shutdown();
}

bool MPQManager::initialize(const std::string& dataPath_) {
    if (initialized) {
        LOG_WARNING("MPQManager already initialized");
        return true;
    }

    dataPath = dataPath_;
    LOG_INFO("Initializing MPQ manager with data path: ", dataPath);

    // Check if data directory exists
    if (!std::filesystem::exists(dataPath)) {
        LOG_ERROR("Data directory does not exist: ", dataPath);
        return false;
    }

#ifdef HAVE_STORMLIB
    // Load base archives (in order of priority)
    std::vector<std::string> baseArchives = {
        "common.MPQ",
        "common-2.MPQ",
        "expansion.MPQ",
        "lichking.MPQ",
    };

    for (const auto& archive : baseArchives) {
        std::string fullPath = dataPath + "/" + archive;
        if (std::filesystem::exists(fullPath)) {
            loadArchive(fullPath, 100);  // Base archives have priority 100
        } else {
            LOG_DEBUG("Base archive not found (optional): ", archive);
        }
    }

    // Load patch archives (highest priority)
    loadPatchArchives();

    // Load locale archives
    loadLocaleArchives("enUS");  // TODO: Make configurable

    if (archives.empty()) {
        LOG_WARNING("No MPQ archives loaded - will use loose file fallback");
    } else {
        LOG_INFO("MPQ manager initialized with ", archives.size(), " archives");
    }
#else
    LOG_WARNING("StormLib not available - using loose file fallback only");
#endif

    initialized = true;
    return true;
}

void MPQManager::shutdown() {
    if (!initialized) {
        return;
    }

#ifdef HAVE_STORMLIB
    LOG_INFO("Shutting down MPQ manager");
    for (auto& entry : archives) {
        if (entry.handle != INVALID_HANDLE_VALUE) {
            SFileCloseArchive(entry.handle);
        }
    }
#endif

    archives.clear();
    archiveNames.clear();
    {
        std::lock_guard<std::mutex> lock(fileArchiveCacheMutex_);
        fileArchiveCache_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(missingFileMutex_);
        missingFileWarnings_.clear();
    }
    initialized = false;
}

bool MPQManager::loadArchive(const std::string& path, int priority) {
#ifndef HAVE_STORMLIB
    LOG_ERROR("Cannot load archive - StormLib not available");
    return false;
#endif

#ifdef HAVE_STORMLIB
    // Check if file exists
    if (!std::filesystem::exists(path)) {
        LOG_ERROR("Archive file not found: ", path);
        return false;
    }

    HANDLE handle = INVALID_HANDLE_VALUE;
    if (!SFileOpenArchive(path.c_str(), 0, 0, &handle)) {
        LOG_ERROR("Failed to open MPQ archive: ", path);
        return false;
    }

    ArchiveEntry entry;
    entry.handle = handle;
    entry.path = path;
    entry.priority = priority;

    archives.push_back(entry);
    archiveNames.push_back(path);

    // Sort archives by priority (highest first)
    std::sort(archives.begin(), archives.end(),
              [](const ArchiveEntry& a, const ArchiveEntry& b) {
                  return a.priority > b.priority;
              });

    // Archive set/priority changed, so cached filename -> archive mappings may be stale.
    {
        std::lock_guard<std::mutex> lock(fileArchiveCacheMutex_);
        fileArchiveCache_.clear();
    }

    LOG_INFO("Loaded MPQ archive: ", path, " (priority ", priority, ")");
    return true;
#endif

    return false;
}

bool MPQManager::fileExists(const std::string& filename) const {
#ifdef HAVE_STORMLIB
    // Check MPQ archives first if available
    if (!archives.empty()) {
        HANDLE archive = findFileArchive(filename);
        if (archive != INVALID_HANDLE_VALUE) {
            return true;
        }
    }
#endif

    // Fall back to checking for loose file
    std::string loosePath = filename;
    std::replace(loosePath.begin(), loosePath.end(), '\\', '/');
    std::string fullPath = dataPath + "/" + loosePath;
    return std::filesystem::exists(fullPath);
}

std::vector<uint8_t> MPQManager::readFile(const std::string& filename) const {
#ifdef HAVE_STORMLIB
    // Try MPQ archives first if available
    if (!archives.empty()) {
        HANDLE archive = findFileArchive(filename);
        if (archive != INVALID_HANDLE_VALUE) {
            std::string stormFilename = filename;
            std::replace(stormFilename.begin(), stormFilename.end(), '/', '\\');
            // Open the file
            HANDLE file = INVALID_HANDLE_VALUE;
            if (SFileOpenFileEx(archive, stormFilename.c_str(), 0, &file)) {
                // Get file size
                DWORD fileSize = SFileGetFileSize(file, nullptr);
                if (fileSize > 0 && fileSize != SFILE_INVALID_SIZE) {
                    // Read file data
                    std::vector<uint8_t> data(fileSize);
                    DWORD bytesRead = 0;
                    if (SFileReadFile(file, data.data(), fileSize, &bytesRead, nullptr)) {
                        SFileCloseFile(file);
                        LOG_DEBUG("Read file from MPQ: ", filename, " (", bytesRead, " bytes)");
                        return data;
                    }
                }
                SFileCloseFile(file);
            }
        }
    }
#endif

    // Fall back to loose file loading
    // Convert WoW path (backslashes) to filesystem path (forward slashes)
    std::string loosePath = filename;
    std::replace(loosePath.begin(), loosePath.end(), '\\', '/');

    // Try with original case
    std::string fullPath = dataPath + "/" + loosePath;
    if (std::filesystem::exists(fullPath)) {
        std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            size_t size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::vector<uint8_t> data(size);
            file.read(reinterpret_cast<char*>(data.data()), size);
            LOG_DEBUG("Read loose file: ", loosePath, " (", size, " bytes)");
            return data;
        }
    }

    // Try case-insensitive search (common for Linux)
    std::filesystem::path searchPath = dataPath;
    std::vector<std::string> pathComponents;
    std::istringstream iss(loosePath);
    std::string component;
    while (std::getline(iss, component, '/')) {
        if (!component.empty()) {
            pathComponents.push_back(component);
        }
    }

    // Try to find file with case-insensitive matching
    for (const auto& comp : pathComponents) {
        bool found = false;
        if (std::filesystem::exists(searchPath) && std::filesystem::is_directory(searchPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(searchPath)) {
                std::string entryName = entry.path().filename().string();
                // Case-insensitive comparison
                if (std::equal(comp.begin(), comp.end(), entryName.begin(), entryName.end(),
                              [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
                    searchPath = entry.path();
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            logMissingFileOnce(filename);
            return std::vector<uint8_t>();
        }
    }

    // Try to read the found file
    if (std::filesystem::exists(searchPath) && std::filesystem::is_regular_file(searchPath)) {
        std::ifstream file(searchPath, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            size_t size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::vector<uint8_t> data(size);
            file.read(reinterpret_cast<char*>(data.data()), size);
            LOG_DEBUG("Read loose file (case-insensitive): ", searchPath.string(), " (", size, " bytes)");
            return data;
        }
    }

    logMissingFileOnce(filename);
    return std::vector<uint8_t>();
}

void MPQManager::logMissingFileOnce(const std::string& filename) const {
    std::string normalized = toLowerCopy(filename);
    std::lock_guard<std::mutex> lock(missingFileMutex_);
    if (missingFileWarnings_.insert(normalized).second) {
        LOG_WARNING("File not found: ", filename);
    }
}

uint32_t MPQManager::getFileSize(const std::string& filename) const {
#ifndef HAVE_STORMLIB
    return 0;
#endif

#ifdef HAVE_STORMLIB
    HANDLE archive = findFileArchive(filename);
    if (archive == INVALID_HANDLE_VALUE) {
        return 0;
    }

    std::string stormFilename = filename;
    std::replace(stormFilename.begin(), stormFilename.end(), '/', '\\');
    HANDLE file = INVALID_HANDLE_VALUE;
    if (!SFileOpenFileEx(archive, stormFilename.c_str(), 0, &file)) {
        return 0;
    }

    DWORD fileSize = SFileGetFileSize(file, nullptr);
    SFileCloseFile(file);

    return (fileSize == SFILE_INVALID_SIZE) ? 0 : fileSize;
#endif

    return 0;
}

HANDLE MPQManager::findFileArchive(const std::string& filename) const {
#ifndef HAVE_STORMLIB
    return INVALID_HANDLE_VALUE;
#endif

#ifdef HAVE_STORMLIB
    std::string cacheKey = normalizeVirtualFilenameForLookup(filename);
    {
        std::lock_guard<std::mutex> lock(fileArchiveCacheMutex_);
        auto it = fileArchiveCache_.find(cacheKey);
        if (it != fileArchiveCache_.end()) {
            return it->second;
        }
    }

    std::string stormFilename = filename;
    std::replace(stormFilename.begin(), stormFilename.end(), '/', '\\');

    const auto start = std::chrono::steady_clock::now();
    HANDLE found = INVALID_HANDLE_VALUE;
    // Search archives in priority order (already sorted)
    for (const auto& entry : archives) {
        if (SFileHasFile(entry.handle, stormFilename.c_str())) {
            found = entry.handle;
            break;
        }
    }

    const auto end = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    {
        std::lock_guard<std::mutex> lock(fileArchiveCacheMutex_);
        // Another thread may have raced to populate; if so, prefer the existing value.
        auto [it, inserted] = fileArchiveCache_.emplace(std::move(cacheKey), found);
        if (!inserted) {
            found = it->second;
        }
    }

    // With caching this should only happen once per unique filename; keep threshold conservative.
    if (ms >= 100) {
        LOG_WARNING("Slow MPQ lookup: '", filename, "' scanned ", archives.size(), " archives in ", ms, " ms");
    }

    return found;
#endif

    return INVALID_HANDLE_VALUE;
}

bool MPQManager::loadPatchArchives() {
#ifndef HAVE_STORMLIB
    return false;
#endif

    const bool disableLetterPatches = envFlagEnabled("WOWEE_DISABLE_LETTER_PATCHES");
    const bool disableNumericPatches = envFlagEnabled("WOWEE_DISABLE_NUMERIC_PATCHES");

    if (disableLetterPatches) {
        LOG_WARNING("MPQ letter patches disabled via WOWEE_DISABLE_LETTER_PATCHES=1");
    }
    if (disableNumericPatches) {
        LOG_WARNING("MPQ numeric patches disabled via WOWEE_DISABLE_NUMERIC_PATCHES=1");
    }

    // WoW 3.3.5a patch archives (in order of priority, highest first)
    std::vector<std::pair<std::string, int>> patchArchives = {
        // Lettered patch MPQs are used by some clients/distributions (e.g. Patch-A.mpq..Patch-E.mpq).
        // Treat them as higher priority than numeric patch MPQs.
        // Keep priorities well above numeric patch-*.MPQ so lettered patches always win when both exist.
        {"Patch-Z.mpq", 925}, {"Patch-Y.mpq", 924}, {"Patch-X.mpq", 923}, {"Patch-W.mpq", 922},
        {"Patch-V.mpq", 921}, {"Patch-U.mpq", 920}, {"Patch-T.mpq", 919}, {"Patch-S.mpq", 918},
        {"Patch-R.mpq", 917}, {"Patch-Q.mpq", 916}, {"Patch-P.mpq", 915}, {"Patch-O.mpq", 914},
        {"Patch-N.mpq", 913}, {"Patch-M.mpq", 912}, {"Patch-L.mpq", 911}, {"Patch-K.mpq", 910},
        {"Patch-J.mpq", 909}, {"Patch-I.mpq", 908}, {"Patch-H.mpq", 907}, {"Patch-G.mpq", 906},
        {"Patch-F.mpq", 905}, {"Patch-E.mpq", 904}, {"Patch-D.mpq", 903}, {"Patch-C.mpq", 902},
        {"Patch-B.mpq", 901}, {"Patch-A.mpq", 900},
        // Lowercase variants (Linux case-sensitive filesystems).
        {"patch-z.mpq", 825}, {"patch-y.mpq", 824}, {"patch-x.mpq", 823}, {"patch-w.mpq", 822},
        {"patch-v.mpq", 821}, {"patch-u.mpq", 820}, {"patch-t.mpq", 819}, {"patch-s.mpq", 818},
        {"patch-r.mpq", 817}, {"patch-q.mpq", 816}, {"patch-p.mpq", 815}, {"patch-o.mpq", 814},
        {"patch-n.mpq", 813}, {"patch-m.mpq", 812}, {"patch-l.mpq", 811}, {"patch-k.mpq", 810},
        {"patch-j.mpq", 809}, {"patch-i.mpq", 808}, {"patch-h.mpq", 807}, {"patch-g.mpq", 806},
        {"patch-f.mpq", 805}, {"patch-e.mpq", 804}, {"patch-d.mpq", 803}, {"patch-c.mpq", 802},
        {"patch-b.mpq", 801}, {"patch-a.mpq", 800},

        {"patch-5.MPQ", 500},
        {"patch-4.MPQ", 400},
        {"patch-3.MPQ", 300},
        {"patch-2.MPQ", 200},
        {"patch.MPQ", 150},
    };

    int loadedPatches = 0;
    for (const auto& [archive, priority] : patchArchives) {
        const bool isLetterPatch =
            (archive.size() >= 10) &&
            (toLowerCopy(archive).rfind("patch-", 0) != 0) && // not patch-*.MPQ
            (toLowerCopy(archive).rfind("patch.", 0) != 0);  // not patch.MPQ
        if (isLetterPatch && disableLetterPatches) {
            continue;
        }
        if (!isLetterPatch && disableNumericPatches) {
            continue;
        }

        std::string fullPath = dataPath + "/" + archive;
        if (std::filesystem::exists(fullPath)) {
            if (loadArchive(fullPath, priority)) {
                loadedPatches++;
            }
        }
    }

    LOG_INFO("Loaded ", loadedPatches, " patch archives");
    return loadedPatches > 0;
}

bool MPQManager::loadLocaleArchives(const std::string& locale) {
#ifndef HAVE_STORMLIB
    return false;
#endif

    std::string localePath = dataPath + "/" + locale;
    if (!std::filesystem::exists(localePath)) {
        LOG_WARNING("Locale directory not found: ", localePath);
        return false;
    }

    // Locale-specific archives (including speech MPQs for NPC voices)
    std::vector<std::pair<std::string, int>> localeArchives = {
        {"locale-" + locale + ".MPQ", 250},
        {"speech-" + locale + ".MPQ", 240},  // Base speech/NPC voices
        {"expansion-speech-" + locale + ".MPQ", 245},  // TBC speech
        {"lichking-speech-" + locale + ".MPQ", 248},  // WotLK speech
        {"patch-" + locale + ".MPQ", 450},
        {"patch-" + locale + "-2.MPQ", 460},
        {"patch-" + locale + "-3.MPQ", 470},
    };

    int loadedLocale = 0;
    for (const auto& [archive, priority] : localeArchives) {
        std::string fullPath = localePath + "/" + archive;
        if (std::filesystem::exists(fullPath)) {
            if (loadArchive(fullPath, priority)) {
                loadedLocale++;
            }
        }
    }

    LOG_INFO("Loaded ", loadedLocale, " locale archives for ", locale);
    return loadedLocale > 0;
}

} // namespace pipeline
} // namespace wowee
