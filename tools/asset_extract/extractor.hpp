#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <cstdint>

namespace wowee {
namespace tools {

/**
 * Extraction pipeline: MPQ archives → loose files + manifest
 */
class Extractor {
public:
    struct Options {
        std::string mpqDir;       // Path to WoW Data directory
        std::string outputDir;    // Output directory for extracted assets
        std::string expansion;    // "classic", "tbc", "wotlk", or "" for auto-detect
        std::string locale;       // "enUS", "deDE", etc., or "" for auto-detect
        int threads = 0;          // 0 = auto-detect
        bool verify = false;      // CRC32 verify after extraction
        bool verbose = false;     // Verbose logging
        bool generateDbcCsv = false; // Convert selected DBFilesClient/*.dbc to CSV for committing
        bool skipDbcExtraction = false; // Extract visual assets only (recommended when CSV DBCs are in repo)
        bool onlyUsedDbcs = false; // Extract only the DBC files wowee uses (implies DBFilesClient/*.dbc filter)
        std::string dbcCsvOutputDir; // When set, write CSVs into this directory instead of outputDir/expansions/<exp>/db
        std::string referenceManifest; // If set, only extract files NOT in this manifest (delta extraction)
    };

    struct Stats {
        std::atomic<uint64_t> filesExtracted{0};
        std::atomic<uint64_t> bytesExtracted{0};
        std::atomic<uint64_t> filesSkipped{0};
        std::atomic<uint64_t> filesFailed{0};
    };

    /**
     * Auto-detect expansion from files in mpqDir.
     * @return "classic", "tbc", "wotlk", or "" if unknown
     */
    static std::string detectExpansion(const std::string& mpqDir);

    /**
     * Auto-detect locale by scanning for locale subdirectories.
     * @return locale string like "enUS", or "" if none found
     */
    static std::string detectLocale(const std::string& mpqDir);

    /**
     * Run the extraction pipeline
     * @return true on success
     */
    static bool run(const Options& opts);

private:
    static bool enumerateFiles(const Options& opts,
                               std::vector<std::string>& outFiles);
};

} // namespace tools
} // namespace wowee
