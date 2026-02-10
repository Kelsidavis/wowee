#include <StormLib.h>
#include <iostream>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mpq_file> [search_pattern]\n";
        return 1;
    }

    HANDLE hMpq;
    if (!SFileOpenArchive(argv[1], 0, 0, &hMpq)) {
        std::cerr << "Failed to open MPQ: " << argv[1] << "\n";
        return 1;
    }

    const char* pattern = argc > 2 ? argv[2] : "*";
    
    SFILE_FIND_DATA findData;
    HANDLE hFind = SFileFindFirstFile(hMpq, pattern, &findData, nullptr);
    
    if (hFind == nullptr) {
        std::cout << "No files found matching: " << pattern << "\n";
    } else {
        int count = 0;
        do {
            std::cout << findData.cFileName << " (" << findData.dwFileSize << " bytes)\n";
            count++;
            if (count > 50) {
                std::cout << "... (showing first 50 matches)\n";
                break;
            }
        } while (SFileFindNextFile(hFind, &findData));
        
        SFileFindClose(hFind);
    }
    
    SFileCloseArchive(hMpq);
    return 0;
}
