#include "extractor.hpp"
#include <iostream>
#include <string>
#include <cstring>

static void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " --mpq-dir <path> --output <path> [options]\n"
              << "\n"
              << "Extract WoW MPQ archives to organized loose files with manifest.\n"
              << "\n"
              << "Required:\n"
              << "  --mpq-dir <path>    Path to WoW Data directory containing MPQ files\n"
              << "  --output <path>     Output directory for extracted assets\n"
              << "\n"
              << "Options:\n"
              << "  --expansion <id>    Expansion ID (classic/tbc/wotlk/cata).\n"
              << "                      Output goes to <output>/expansions/<id>/\n"
              << "  --verify            CRC32 verify all extracted files\n"
              << "  --threads <N>       Number of extraction threads (default: auto)\n"
              << "  --verbose           Verbose output\n"
              << "  --help              Show this help\n";
}

int main(int argc, char** argv) {
    wowee::tools::Extractor::Options opts;
    std::string expansion;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mpq-dir") == 0 && i + 1 < argc) {
            opts.mpqDir = argv[++i];
        } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            opts.outputDir = argv[++i];
        } else if (std::strcmp(argv[i], "--expansion") == 0 && i + 1 < argc) {
            expansion = argv[++i];
        } else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            opts.threads = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--verify") == 0) {
            opts.verify = true;
        } else if (std::strcmp(argv[i], "--verbose") == 0) {
            opts.verbose = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (opts.mpqDir.empty() || opts.outputDir.empty()) {
        std::cerr << "Error: --mpq-dir and --output are required\n\n";
        printUsage(argv[0]);
        return 1;
    }

    // If --expansion given, redirect output into expansions/<id>/ subdirectory
    if (!expansion.empty()) {
        opts.outputDir += "/expansions/" + expansion;
    }

    std::cout << "=== Wowee Asset Extractor ===\n";
    std::cout << "MPQ directory: " << opts.mpqDir << "\n";
    std::cout << "Output:        " << opts.outputDir << "\n";
    if (!expansion.empty()) {
        std::cout << "Expansion:     " << expansion << "\n";
    }

    if (!wowee::tools::Extractor::run(opts)) {
        std::cerr << "Extraction failed!\n";
        return 1;
    }

    return 0;
}
