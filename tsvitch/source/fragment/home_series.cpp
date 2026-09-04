#include "fragment/home_series.hpp"

#include <borealis/core/logger.hpp>
#include "view/nx_media_card.hpp"
#include "view/grid_dropdown.hpp"
#include "utils/config_helper.hpp"
#include "utils/activity_helper.hpp"
#include "core/DownloadManager.hpp"
#include "api/tsvitch/result/home_live_result.h"

namespace {

static void configureXtreamSeries() {
    auto& config = ProgramConfig::instance();
    XtreamAPI::instance().setCredentials(
        config.getXtreamServerUrl(),
        config.getXtreamUsername(),
        config.getXtreamPassword());
}

class SeriesDataSource : public RecyclingGridDataSource {
public:
    using SelectCallback = std::function<void(const tsvitch::XtreamSeries&)>;

    SeriesDataSource(std::vector<tsvitch::XtreamSeries> data, SelectCallback callback)
        : series(std::move(data)), callback(std::move(callback)) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        auto* card = static_cast<NxMediaCard*>(recycler->dequeueReusableCell("Cell"));
        const auto& item = series[index];
        std::string meta = item.genre.empty() ? "Dizi" : item.genre;
        if (!item.rating.empty()) meta = "★ " + item.rating + "   •   " + meta;
        card->setMedia(item.name, item.cover, meta, item.series_id, "");
        return card;
    }

    size_t getItemCount() override { return series.size(); }
    void clearData() override { series.clear(); }

    void onItemSelected(RecyclingGrid*, size_t index) override {
        if (index < series.size() && callback) callback(series[index]);
    }

private:
    std::vector<tsvitch::XtreamSeries> series;
    SelectCallback callback;
};

} // namespace

HomeSeries::HomeSeries() {
    this->inflateFromXMLRes("xml/fragment/home_series.xml");
    recyclingGrid->registerCell("Cell", []() { return NxMediaCard::create(); });

    this->registerAction("Bölüm indir", brls::BUTTON_RT, [this](...) {
        this->downloadFocused();
        return true;
    });

    this->registerAction("Yenile", brls::BUTTON_Y, [this](...) {
        this->reload();
        return true;
    });

    reload();
}

void HomeSeries::reload() {
    configureXtreamSeries();
    statusLabel->setText("Dizi arşivi yükleniyor…");
    recyclingGrid->showSkeleton();

    XtreamAPI::instance().getAllSeries([this](const std::vector<tsvitch::XtreamSeries>& data, bool success, const std::string& error) {
        if (!success) {
            brls::Logger::error("NX Media Series: {}", error);
            statusLabel->setText("Dizi arşivi yüklenemedi");
            recyclingGrid->setError(error);
            return;
        }

        statusLabel->setText(std::to_string(data.size()) + " dizi");
        recyclingGrid->setDataSource(new SeriesDataSource(data, [this](const tsvitch::XtreamSeries& series) {
            this->openSeries(series);
        }));
    });
}

void HomeSeries::openSeries(const tsvitch::XtreamSeries& series) {
    configureXtreamSeries();
    statusLabel->setText(series.name + " • bölümler yükleniyor…");
    XtreamAPI::instance().getSeriesInfo(series.series_id,
        [this, series](const tsvitch::XtreamSeriesInfo& info, bool success, const std::string& error) {
            if (!success) {
                statusLabel->setText("Bölümler yüklenemedi");
                brls::Logger::error("NX Media series info: {}", error);
                return;
            }
            statusLabel->setText(std::to_string(info.episodes.size()) + " bölüm");
            this->showEpisodePicker(series, info, false);
        });
}

void HomeSeries::downloadFocused() {
    auto* card = dynamic_cast<NxMediaCard*>(recyclingGrid->getFocusedItem());
    if (!card || card->getMediaId().empty()) return;

    tsvitch::XtreamSeries series;
    series.series_id = card->getMediaId();
    series.name = card->getTitle();
    series.cover = card->getImageUrl();

    configureXtreamSeries();
    statusLabel->setText(series.name + " • indirilecek bölüm seçiliyor…");
    XtreamAPI::instance().getSeriesInfo(series.series_id,
        [this, series](const tsvitch::XtreamSeriesInfo& info, bool success, const std::string& error) {
            if (!success) {
                statusLabel->setText("Bölümler yüklenemedi");
                brls::Logger::error("NX Media series download info: {}", error);
                return;
            }
            this->showEpisodePicker(series, info, true);
        });
}

void HomeSeries::showEpisodePicker(const tsvitch::XtreamSeries& series,
                                   const tsvitch::XtreamSeriesInfo& info,
                                   bool downloadMode) {
    if (info.episodes.empty()) {
        statusLabel->setText("Bu dizi için bölüm bulunamadı");
        return;
    }

    std::vector<std::string> options;
    options.reserve(info.episodes.size());
    for (const auto& episode : info.episodes) {
        std::string label = "S" + episode.season + " • B" + episode.episode_num;
        if (!episode.title.empty()) label += " — " + episode.title;
        options.push_back(std::move(label));
    }

    BaseDropdown::text(
        downloadMode ? "İndirilecek bölüm" : series.name,
        options,
        [this, series, episodes = info.episodes, downloadMode](int selected) {
            if (selected < 0 || static_cast<size_t>(selected) >= episodes.size()) return;
            const auto& episode = episodes[static_cast<size_t>(selected)];
            const std::string url = XtreamAPI::instance().getEpisodeUrl(episode.id, episode.container_extension);
            if (url.empty()) return;

            std::string episodeTitle = series.name + " S" + episode.season + "B" + episode.episode_num;
            if (!episode.title.empty()) episodeTitle += " - " + episode.title;
            const std::string image = episode.image.empty() ? series.cover : episode.image;

            if (downloadMode) {
                const std::string id = DownloadManager::instance().startDownload(episodeTitle, url, image);
                if (!id.empty()) statusLabel->setText("İndirme başladı: " + episodeTitle);
                return;
            }

            tsvitch::LiveM3u8 media;
            media.id = episode.id;
            media.title = episodeTitle;
            media.logo = image;
            media.groupTitle = "Diziler";
            media.url = url;
            std::vector<tsvitch::LiveM3u8> playlist{media};
            Intent::openLive(playlist, 0, []() {});
        },
        0,
        downloadMode ? "R ile dizi kartından açtığın indirme menüsü" : "A ile bölümü oynat");
}

brls::View* HomeSeries::create() {
    return new HomeSeries();
}
