#include "config.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "log.h"

namespace fs = std::filesystem;

namespace sk {

namespace {

constexpr std::size_t kMaxRecents = 5;

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const auto begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

} // namespace

Config::Config() {
    char appdata[MAX_PATH]{};
    const DWORD len = GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
    const fs::path dir =
        fs::path(len > 0 ? appdata : ".") / "MotorSK";
    path_ = (dir / "config.txt").string();

    std::error_code ec;
    fs::create_directories(dir, ec);

    load();
}

void Config::load() {
    std::ifstream file(path_);
    if (!file) return;

    std::string line;
    while (std::getline(file, line)) {
        const auto sep = line.find(':');
        if (sep == std::string::npos) continue;
        const std::string key = trim(line.substr(0, sep));
        const std::string value = trim(line.substr(sep + 1));
        if (key != "reciente") continue;

        const auto bar = value.find('|');
        if (bar == std::string::npos) continue;

        Recent r;
        r.openedAt = std::strtoll(value.substr(0, bar).c_str(), nullptr, 10);
        r.folder = value.substr(bar + 1);
        if (!r.folder.empty()) recents_.push_back(std::move(r));
    }

    std::sort(recents_.begin(), recents_.end(),
              [](const Recent& a, const Recent& b) { return a.openedAt > b.openedAt; });
    if (recents_.size() > kMaxRecents) recents_.resize(kMaxRecents);
}

void Config::addRecent(const std::string& folder) {
    removeRecent(folder);
    Recent r;
    r.openedAt = static_cast<long long>(std::time(nullptr));
    r.folder = folder;
    recents_.insert(recents_.begin(), std::move(r));
    if (recents_.size() > kMaxRecents) recents_.resize(kMaxRecents);
}

void Config::removeRecent(const std::string& folder) {
    recents_.erase(std::remove_if(recents_.begin(), recents_.end(),
                                  [&](const Recent& r) { return r.folder == folder; }),
                   recents_.end());
}

bool Config::save() const {
    std::ofstream file(path_, std::ios::trunc);
    if (!file) {
        SK_ERROR("Config: no se pudo escribir %s", path_.c_str());
        return false;
    }
    for (const Recent& r : recents_) {
        file << "reciente: " << r.openedAt << "|" << r.folder << "\n";
    }
    return true;
}

} // namespace sk
