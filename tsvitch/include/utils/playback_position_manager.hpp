#pragma once

#include <string>
#include <fstream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <borealis/core/logger.hpp>
#include "utils/config_helper.hpp"

namespace tsvitch {

class PlaybackPositionManager {
public:
    static void savePosition(const std::string& url, int64_t position, int64_t duration) {
        if (url.empty() || position < 5) {
            return;
        }

        // Bir içerik bitmiş sayılıyorsa eski devam noktasını tamamen temizle.
        if (duration > 0) {
            const int64_t remaining = std::max<int64_t>(0, duration - position);
            const double progress = static_cast<double>(position) / static_cast<double>(duration);
            if (remaining <= 30 || progress >= 0.95) {
                clearPosition(url);
                brls::Logger::info("PlaybackPosition: Marked completed and cleared resume point for URL: {}", url);
                return;
            }
        }

        try {
            nlohmann::json data = loadCache();
            data[url] = {
                {"position", position},
                {"duration", duration},
                {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
            };
            saveCache(data);
            brls::Logger::info("PlaybackPosition: Saved position {} / {} for URL: {}", position, duration, url);
        } catch (const std::exception& e) {
            brls::Logger::error("PlaybackPosition: Error saving position: {}", e.what());
        }
    }

    static int64_t getPosition(const std::string& url) {
        if (url.empty()) {
            return 0;
        }

        try {
            nlohmann::json data = loadCache();
            if (!data.contains(url) || !data[url].is_object()) {
                return 0;
            }

            auto entry = data[url];
            if (!entry.contains("position")) {
                return 0;
            }

            if (entry.contains("timestamp")) {
                auto savedTime = std::chrono::system_clock::time_point(
                    std::chrono::system_clock::duration(entry["timestamp"].get<int64_t>())
                );
                auto now = std::chrono::system_clock::now();
                auto daysPassed = std::chrono::duration_cast<std::chrono::hours>(now - savedTime).count() / 24;
                if (daysPassed > 30) {
                    clearPosition(url);
                    return 0;
                }
            }

            const int64_t position = entry.value("position", static_cast<int64_t>(0));
            const int64_t duration = entry.value("duration", static_cast<int64_t>(0));
            if (position < 5) {
                return 0;
            }

            if (duration > 0) {
                const int64_t remaining = std::max<int64_t>(0, duration - position);
                const double progress = static_cast<double>(position) / static_cast<double>(duration);
                if (remaining <= 30 || progress >= 0.95) {
                    clearPosition(url);
                    return 0;
                }
            }

            brls::Logger::info("PlaybackPosition: Resume point {} for URL: {}", position, url);
            return position;
        } catch (const std::exception& e) {
            brls::Logger::error("PlaybackPosition: Error getting position: {}", e.what());
            return 0;
        }
    }

    static double getProgress(const std::string& url) {
        try {
            nlohmann::json data = loadCache();
            if (!data.contains(url) || !data[url].is_object()) {
                return 0.0;
            }
            const int64_t position = data[url].value("position", static_cast<int64_t>(0));
            const int64_t duration = data[url].value("duration", static_cast<int64_t>(0));
            if (position <= 0 || duration <= 0) {
                return 0.0;
            }
            return std::clamp(static_cast<double>(position) / static_cast<double>(duration), 0.0, 1.0);
        } catch (...) {
            return 0.0;
        }
    }

    static void clearPosition(const std::string& url) {
        try {
            nlohmann::json data = loadCache();
            if (data.contains(url)) {
                data.erase(url);
                saveCache(data);
                brls::Logger::info("PlaybackPosition: Cleared position for URL: {}", url);
            }
        } catch (const std::exception& e) {
            brls::Logger::error("PlaybackPosition: Error clearing position: {}", e.what());
        }
    }

    static void cleanupExpiredPositions() {
        try {
            nlohmann::json data = loadCache();
            auto now = std::chrono::system_clock::now();
            std::vector<std::string> toRemove;

            for (auto& [url, entry] : data.items()) {
                if (!entry.is_object()) {
                    toRemove.push_back(url);
                    continue;
                }

                if (entry.contains("timestamp")) {
                    auto savedTime = std::chrono::system_clock::time_point(
                        std::chrono::system_clock::duration(entry["timestamp"].get<int64_t>())
                    );
                    auto daysPassed = std::chrono::duration_cast<std::chrono::hours>(now - savedTime).count() / 24;
                    if (daysPassed > 30) {
                        toRemove.push_back(url);
                    }
                }
            }

            for (const auto& url : toRemove) {
                data.erase(url);
            }

            if (!toRemove.empty()) {
                saveCache(data);
            }
        } catch (const std::exception& e) {
            brls::Logger::error("PlaybackPosition: Error cleaning up positions: {}", e.what());
        }
    }

private:
    static std::string getCachePath() {
        return ProgramConfig::instance().getConfigDir() + "/playback_positions.json";
    }

    static nlohmann::json loadCache() {
        std::ifstream file(getCachePath());
        if (!file.is_open()) {
            return nlohmann::json::object();
        }

        try {
            nlohmann::json data;
            file >> data;
            return data.is_object() ? data : nlohmann::json::object();
        } catch (const std::exception& e) {
            brls::Logger::warning("PlaybackPosition: Error loading cache: {}", e.what());
            return nlohmann::json::object();
        }
    }

    static void saveCache(const nlohmann::json& data) {
        std::ofstream file(getCachePath(), std::ios::trunc);
        if (!file.is_open()) {
            brls::Logger::error("PlaybackPosition: Cannot open cache file for writing");
            return;
        }
        file << data.dump(2);
    }
};

} // namespace tsvitch
