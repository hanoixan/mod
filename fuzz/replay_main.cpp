// Runs every file of the corpus folders given as arguments through a fuzz target, so a
// plain build's ctest keeps every input the fuzzers ever needed (crashes included).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    for (int i = 1; i < argc; ++i) {
        const fs::path p = argv[i];
        if (fs::is_directory(p)) {
            for (const auto& e : fs::directory_iterator(p))
                if (e.is_regular_file()) files.push_back(e.path());
        } else if (fs::is_regular_file(p)) {
            files.push_back(p);
        }
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& f : files) {
        std::ifstream in(f, std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    }
    std::printf("replayed %zu inputs\n", files.size());
    return files.empty() ? 1 : 0;
}
