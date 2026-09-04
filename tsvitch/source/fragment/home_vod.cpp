#include "fragment/home_vod.hpp"

#include <borealis/core/logger.hpp>
#include "view/nx_media_card.hpp"
#include "utils/xtream_helper.hpp"
#include "utils/config_helper.hpp"
#include "utils/activity_helper.hpp"
#include "core/DownloadManager.hpp"
#include "api/tsvitch/result/home_live_result.h"

namespace {

static void configureXtream() {
    auto& config = ProgramConfig::instance();
    XtreamAPI::instance().setCredentials(
        config.getXtreamServerUrl(),
        config.getXtreamUsername(),
        config.getXtreamPassword());
}

class VodDataSource : public RecyclingGridDataSource {
public:
    explicit VodDataSource(std::vector<tsvitch::XtreamMovie> movies) : movies(std::move(movies)) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        auto* card = static_cast<NxMediaCard*>(recycler->dequeueReusableCell("Cell"));
        const auto& movie = movies[index];
        std::string meta = "Film";
        if (!movie.rating.empty()) meta = "★ " + movie.rating + "   •   Film";
        card->setMedia(movie.name, movie.stream_icon, meta, movie.stream_id, movie.container_extension);
        return card;
    }

    size_t getItemCount() override { return movies.size(); }
    void clearData() override { movies.clear(); }

    void onItemSelected(RecyclingGrid*, size_t index) override {
        if (index >= movies.size()) return;
        const auto& movie = movies[index];
        tsvitch::LiveM3u8 media;
        media.id = movie.stream_id;
        media.title = movie.name;
        media.logo = movie.stream_icon;
        media.groupTitle = "Filmler";
        media.url = movie.direct_source.empty()
            ? XtreamAPI::instance().getMovieUrl(movie.stream_id, movie.container_extension)
            : movie.direct_source;
        std::vector<tsvitch::LiveM3u8> playlist{media};
        Intent::openLive(playlist, 0, []() {});
    }

private:
    std::vector<tsvitch::XtreamMovie> movies;
};

} // namespace

HomeVod::HomeVod() {
    this->inflateFromXMLRes("xml/fragment/home_vod.xml");
    recyclingGrid->registerCell("Cell", []() { return NxMediaCard::create(); });

    this->registerAction("İndir", brls::BUTTON_RT, [this](...) {
        this->downloadFocused();
        return true;
    });

    this->registerAction("Yenile", brls::BUTTON_Y, [this](...) {
        this->reload();
        return true;
    });

    reload();
}

void HomeVod::reload() {
    configureXtream();
    statusLabel->setText("Film arşivi yükleniyor…");
    statusLabel->setVisibility(brls::Visibility::VISIBLE);
    recyclingGrid->showSkeleton();

    XtreamAPI::instance().getAllVodStreams([this](const std::vector<tsvitch::XtreamMovie>& movies, bool success, const std::string& error) {
        if (!success) {
            brls::Logger::error("NX Media VOD: {}", error);
            statusLabel->setText("Film arşivi yüklenemedi");
            recyclingGrid->setError(error);
            return;
        }
        statusLabel->setText(std::to_string(movies.size()) + " film");
        recyclingGrid->setDataSource(new VodDataSource(movies));
    });
}

void HomeVod::downloadFocused() {
    auto* card = dynamic_cast<NxMediaCard*>(recyclingGrid->getFocusedItem());
    if (!card || card->getMediaId().empty()) return;

    configureXtream();
    const std::string url = XtreamAPI::instance().getMovieUrl(card->getMediaId(), card->getExtension());
    if (url.empty()) return;

    const std::string id = DownloadManager::instance().startDownload(card->getTitle(), url, card->getImageUrl());
    if (!id.empty()) statusLabel->setText("İndirme başladı: " + card->getTitle());
}

brls::View* HomeVod::create() {
    return new HomeVod();
}
