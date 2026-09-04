#include "utils/xtream_helper.hpp"
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include <curl/curl.h>
#include <thread>
#include <sstream>
#include <algorithm>

using namespace tsvitch;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

static std::string jsonString(const json& value) {
    if (value.is_null()) return "";
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return std::to_string(value.get<long long>());
    if (value.is_number_unsigned()) return std::to_string(value.get<unsigned long long>());
    if (value.is_number_float()) {
        std::ostringstream out;
        out << value.get<double>();
        return out.str();
    }
    if (value.is_boolean()) return value.get<bool>() ? "1" : "0";
    return "";
}

static std::string field(const json& item, const char* key) {
    if (!item.contains(key)) return "";
    return jsonString(item.at(key));
}

static std::vector<XtreamCategory> parseCategories(const json& response) {
    std::vector<XtreamCategory> categories;
    if (!response.is_array()) return categories;
    categories.reserve(response.size());
    for (const auto& item : response) {
        XtreamCategory category;
        category.category_id = field(item, "category_id");
        category.category_name = field(item, "category_name");
        category.parent_id = field(item, "parent_id");
        categories.push_back(std::move(category));
    }
    return categories;
}

void XtreamAPI::setCredentials(const std::string& serverUrl, const std::string& username, const std::string& password) {
    this->serverUrl = serverUrl;
    this->username = username;
    this->password = password;
    while (!this->serverUrl.empty() && this->serverUrl.back() == '/') this->serverUrl.pop_back();
    brls::Logger::info("XtreamAPI: credentials configured for {}", this->serverUrl);
}

bool XtreamAPI::isConfigured() const {
    return !serverUrl.empty() && !username.empty() && !password.empty();
}

std::string XtreamAPI::buildApiUrl(const std::string& action) const {
    if (!isConfigured()) return "";
    std::ostringstream url;
    url << serverUrl << "/player_api.php?username=" << username
        << "&password=" << password << "&action=" << action;
    return url.str();
}

std::string XtreamAPI::getStreamUrl(const std::string& streamId, const std::string& extension) const {
    if (!isConfigured()) return "";
    std::ostringstream url;
    url << serverUrl << "/live/" << username << "/" << password << "/" << streamId << "." << extension;
    return url.str();
}

std::string XtreamAPI::getMovieUrl(const std::string& streamId, const std::string& extension) const {
    if (!isConfigured()) return "";
    std::ostringstream url;
    url << serverUrl << "/movie/" << username << "/" << password << "/" << streamId << "."
        << (extension.empty() ? "mp4" : extension);
    return url.str();
}

std::string XtreamAPI::getEpisodeUrl(const std::string& episodeId, const std::string& extension) const {
    if (!isConfigured()) return "";
    std::ostringstream url;
    url << serverUrl << "/series/" << username << "/" << password << "/" << episodeId << "."
        << (extension.empty() ? "mp4" : extension);
    return url.str();
}

void XtreamAPI::makeRequest(const std::string& url, std::function<void(const json&, bool, const std::string&)> callback) {
    std::thread([url, callback]() {
        CURL* curl = curl_easy_init();
        if (!curl) {
            brls::sync([callback]() { callback(json{}, false, "CURL baslatilamadi"); });
            return;
        }

        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 12L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "NXMedia/1.0");

        CURLcode res = curl_easy_perform(curl);
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        curl_easy_cleanup(curl);

        brls::sync([callback, response = std::move(response), res, httpCode]() {
            if (res != CURLE_OK || httpCode != 200) {
                callback(json{}, false, "HTTP " + std::to_string(httpCode) + ": " + curl_easy_strerror(res));
                return;
            }
            try {
                callback(json::parse(response), true, "");
            } catch (const std::exception& e) {
                callback(json{}, false, std::string("JSON hatasi: ") + e.what());
            }
        });
    }).detach();
}

void XtreamAPI::authenticate(AuthCallback callback) {
    if (!isConfigured()) {
        callback(XtreamAuthInfo{}, false, "Xtream bilgileri ayarlanmamis");
        return;
    }
    makeRequest(buildApiUrl("get_account_info"), [callback](const json& response, bool success, const std::string& error) {
        if (!success) {
            callback(XtreamAuthInfo{}, false, error);
            return;
        }
        try {
            XtreamAuthInfo info;
            if (response.contains("user_info")) {
                const auto& user = response.at("user_info");
                info.status = field(user, "status");
                info.exp_date = field(user, "exp_date");
                info.is_trial = field(user, "is_trial");
                info.active_cons = field(user, "active_cons");
                info.created_at = field(user, "created_at");
                info.max_connections = field(user, "max_connections");
                if (user.contains("allowed_output_formats")) info.allowed_output_formats = user.at("allowed_output_formats").dump();
            }
            bool ok = info.status == "Active" || info.status == "active";
            info.message = ok ? "Authentication successful" : "Authentication failed: " + info.status;
            callback(info, ok, info.message);
        } catch (const std::exception& e) {
            callback(XtreamAuthInfo{}, false, e.what());
        }
    });
}

void XtreamAPI::getLiveTVCategories(CategoriesCallback callback) {
    if (!isConfigured()) { callback({}, false, "Xtream bilgileri ayarlanmamis"); return; }
    makeRequest(buildApiUrl("get_live_categories"), [callback](const json& response, bool ok, const std::string& error) {
        if (!ok) { callback({}, false, error); return; }
        callback(parseCategories(response), true, "");
    });
}

void XtreamAPI::getLiveTVChannels(const std::string& categoryId, ChannelsCallback callback) {
    if (!isConfigured()) { callback({}, false, "Xtream bilgileri ayarlanmamis"); return; }
    std::string url = buildApiUrl("get_live_streams");
    if (!categoryId.empty()) url += "&category_id=" + categoryId;
    makeRequest(url, [callback](const json& response, bool ok, const std::string& error) {
        if (!ok) { callback({}, false, error); return; }
        std::vector<XtreamChannel> channels;
        if (response.is_array()) {
            channels.reserve(response.size());
            for (const auto& item : response) {
                XtreamChannel c;
                c.num = field(item, "num"); c.name = field(item, "name"); c.stream_type = field(item, "stream_type");
                c.stream_id = field(item, "stream_id"); c.stream_icon = field(item, "stream_icon");
                c.epg_channel_id = field(item, "epg_channel_id"); c.added = field(item, "added");
                c.category_name = field(item, "category_name"); c.category_id = field(item, "category_id");
                c.series_no = field(item, "series_no"); c.live = field(item, "live");
                c.container_extension = field(item, "container_extension"); c.custom_sid = field(item, "custom_sid");
                c.tv_archive = field(item, "tv_archive"); c.direct_source = field(item, "direct_source");
                c.tv_archive_duration = field(item, "tv_archive_duration");
                channels.push_back(std::move(c));
            }
        }
        callback(channels, true, "");
    });
}

void XtreamAPI::getAllLiveTVChannels(ChannelsCallback callback) { getLiveTVChannels("", callback); }

void XtreamAPI::getVodCategories(CategoriesCallback callback) {
    if (!isConfigured()) { callback({}, false, "Xtream bilgileri ayarlanmamis"); return; }
    makeRequest(buildApiUrl("get_vod_categories"), [callback](const json& response, bool ok, const std::string& error) {
        if (!ok) { callback({}, false, error); return; }
        callback(parseCategories(response), true, "");
    });
}

void XtreamAPI::getVodStreams(const std::string& categoryId, MoviesCallback callback) {
    if (!isConfigured()) { callback({}, false, "Xtream bilgileri ayarlanmamis"); return; }
    std::string url = buildApiUrl("get_vod_streams");
    if (!categoryId.empty()) url += "&category_id=" + categoryId;
    makeRequest(url, [callback](const json& response, bool ok, const std::string& error) {
        if (!ok) { callback({}, false, error); return; }
        std::vector<XtreamMovie> movies;
        if (response.is_array()) {
            movies.reserve(response.size());
            for (const auto& item : response) {
                XtreamMovie m;
                m.num = field(item, "num"); m.name = field(item, "name"); m.stream_id = field(item, "stream_id");
                m.stream_icon = field(item, "stream_icon"); m.rating = field(item, "rating"); m.added = field(item, "added");
                m.category_id = field(item, "category_id"); m.container_extension = field(item, "container_extension");
                m.direct_source = field(item, "direct_source");
                movies.push_back(std::move(m));
            }
        }
        callback(movies, true, "");
    });
}

void XtreamAPI::getAllVodStreams(MoviesCallback callback) { getVodStreams("", callback); }

void XtreamAPI::getSeriesCategories(CategoriesCallback callback) {
    if (!isConfigured()) { callback({}, false, "Xtream bilgileri ayarlanmamis"); return; }
    makeRequest(buildApiUrl("get_series_categories"), [callback](const json& response, bool ok, const std::string& error) {
        if (!ok) { callback({}, false, error); return; }
        callback(parseCategories(response), true, "");
    });
}

void XtreamAPI::getSeries(const std::string& categoryId, SeriesCallback callback) {
    if (!isConfigured()) { callback({}, false, "Xtream bilgileri ayarlanmamis"); return; }
    std::string url = buildApiUrl("get_series");
    if (!categoryId.empty()) url += "&category_id=" + categoryId;
    makeRequest(url, [callback](const json& response, bool ok, const std::string& error) {
        if (!ok) { callback({}, false, error); return; }
        std::vector<XtreamSeries> series;
        if (response.is_array()) {
            series.reserve(response.size());
            for (const auto& item : response) {
                XtreamSeries s;
                s.num = field(item, "num"); s.name = field(item, "name"); s.series_id = field(item, "series_id");
                s.cover = field(item, "cover"); s.plot = field(item, "plot"); s.cast = field(item, "cast");
                s.director = field(item, "director"); s.genre = field(item, "genre"); s.release_date = field(item, "releaseDate");
                if (s.release_date.empty()) s.release_date = field(item, "release_date");
                s.rating = field(item, "rating"); s.last_modified = field(item, "last_modified"); s.category_id = field(item, "category_id");
                series.push_back(std::move(s));
            }
        }
        callback(series, true, "");
    });
}

void XtreamAPI::getAllSeries(SeriesCallback callback) { getSeries("", callback); }

void XtreamAPI::getSeriesInfo(const std::string& seriesId, SeriesInfoCallback callback) {
    if (!isConfigured()) { callback({}, false, "Xtream bilgileri ayarlanmamis"); return; }
    std::string url = buildApiUrl("get_series_info") + "&series_id=" + seriesId;
    makeRequest(url, [callback](const json& response, bool ok, const std::string& error) {
        if (!ok) { callback({}, false, error); return; }
        try {
            XtreamSeriesInfo result;
            if (response.contains("info") && response.at("info").is_object()) {
                const auto& item = response.at("info");
                result.series.name = field(item, "name"); result.series.series_id = field(item, "series_id");
                result.series.cover = field(item, "cover"); result.series.plot = field(item, "plot");
                result.series.cast = field(item, "cast"); result.series.director = field(item, "director");
                result.series.genre = field(item, "genre"); result.series.release_date = field(item, "releaseDate");
                result.series.rating = field(item, "rating"); result.series.category_id = field(item, "category_id");
            }
            if (response.contains("episodes") && response.at("episodes").is_object()) {
                const auto& seasons = response.at("episodes");
                for (auto it = seasons.begin(); it != seasons.end(); ++it) {
                    const std::string seasonNo = it.key();
                    if (!it.value().is_array()) continue;
                    for (const auto& item : it.value()) {
                        XtreamEpisode e;
                        e.id = field(item, "id"); e.episode_num = field(item, "episode_num"); e.title = field(item, "title");
                        e.container_extension = field(item, "container_extension"); e.season = seasonNo;
                        if (item.contains("info") && item.at("info").is_object()) {
                            const auto& info = item.at("info");
                            e.image = field(info, "movie_image"); e.plot = field(info, "plot");
                            e.duration = field(info, "duration"); e.rating = field(info, "rating");
                        }
                        result.episodes.push_back(std::move(e));
                    }
                }
                std::sort(result.episodes.begin(), result.episodes.end(), [](const XtreamEpisode& a, const XtreamEpisode& b) {
                    int sa = 0, sb = 0, ea = 0, eb = 0;
                    try { sa = std::stoi(a.season); } catch (...) {}
                    try { sb = std::stoi(b.season); } catch (...) {}
                    try { ea = std::stoi(a.episode_num); } catch (...) {}
                    try { eb = std::stoi(b.episode_num); } catch (...) {}
                    return sa == sb ? ea < eb : sa < sb;
                });
            }
            callback(result, true, "");
        } catch (const std::exception& e) {
            callback({}, false, std::string("Dizi bilgisi okunamadi: ") + e.what());
        }
    });
}
