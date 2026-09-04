#pragma once

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>
#include <borealis/core/singleton.hpp>

using json = nlohmann::json;

namespace tsvitch {

struct XtreamChannel {
    std::string num;
    std::string name;
    std::string stream_type;
    std::string stream_id;
    std::string stream_icon;
    std::string epg_channel_id;
    std::string added;
    std::string category_name;
    std::string category_id;
    std::string series_no;
    std::string live;
    std::string container_extension;
    std::string custom_sid;
    std::string tv_archive;
    std::string direct_source;
    std::string tv_archive_duration;
};

struct XtreamCategory {
    std::string category_id;
    std::string category_name;
    std::string parent_id;
};

struct XtreamMovie {
    std::string num;
    std::string name;
    std::string stream_id;
    std::string stream_icon;
    std::string rating;
    std::string added;
    std::string category_id;
    std::string container_extension;
    std::string direct_source;
};

struct XtreamSeries {
    std::string num;
    std::string name;
    std::string series_id;
    std::string cover;
    std::string plot;
    std::string cast;
    std::string director;
    std::string genre;
    std::string release_date;
    std::string rating;
    std::string last_modified;
    std::string category_id;
};

struct XtreamEpisode {
    std::string id;
    std::string episode_num;
    std::string title;
    std::string container_extension;
    std::string season;
    std::string image;
    std::string plot;
    std::string duration;
    std::string rating;
};

struct XtreamSeriesInfo {
    XtreamSeries series;
    std::vector<XtreamEpisode> episodes;
};

struct XtreamAuthInfo {
    std::string status;
    std::string message;
    std::string exp_date;
    std::string is_trial;
    std::string active_cons;
    std::string created_at;
    std::string max_connections;
    std::string allowed_output_formats;
};

class XtreamAPI : public brls::Singleton<XtreamAPI> {
public:
    using ChannelsCallback = std::function<void(const std::vector<XtreamChannel>&, bool success, const std::string& error)>;
    using CategoriesCallback = std::function<void(const std::vector<XtreamCategory>&, bool success, const std::string& error)>;
    using MoviesCallback = std::function<void(const std::vector<XtreamMovie>&, bool success, const std::string& error)>;
    using SeriesCallback = std::function<void(const std::vector<XtreamSeries>&, bool success, const std::string& error)>;
    using SeriesInfoCallback = std::function<void(const XtreamSeriesInfo&, bool success, const std::string& error)>;
    using AuthCallback = std::function<void(const XtreamAuthInfo&, bool success, const std::string& error)>;

    XtreamAPI() = default;
    ~XtreamAPI() = default;

    void setCredentials(const std::string& serverUrl, const std::string& username, const std::string& password);
    void authenticate(AuthCallback callback);

    void getLiveTVCategories(CategoriesCallback callback);
    void getLiveTVChannels(const std::string& categoryId, ChannelsCallback callback);
    void getAllLiveTVChannels(ChannelsCallback callback);

    void getVodCategories(CategoriesCallback callback);
    void getVodStreams(const std::string& categoryId, MoviesCallback callback);
    void getAllVodStreams(MoviesCallback callback);

    void getSeriesCategories(CategoriesCallback callback);
    void getSeries(const std::string& categoryId, SeriesCallback callback);
    void getAllSeries(SeriesCallback callback);
    void getSeriesInfo(const std::string& seriesId, SeriesInfoCallback callback);

    std::string getStreamUrl(const std::string& streamId, const std::string& extension = "ts") const;
    std::string getMovieUrl(const std::string& streamId, const std::string& extension = "mp4") const;
    std::string getEpisodeUrl(const std::string& episodeId, const std::string& extension = "mp4") const;

    bool isConfigured() const;

private:
    std::string serverUrl;
    std::string username;
    std::string password;

    std::string buildApiUrl(const std::string& action) const;
    void makeRequest(const std::string& url, std::function<void(const json&, bool, const std::string&)> callback);
};

} // namespace tsvitch
