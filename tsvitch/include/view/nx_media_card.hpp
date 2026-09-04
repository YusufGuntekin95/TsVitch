#pragma once

#include <string>
#include "view/recycling_grid.hpp"

class NxMediaCard : public RecyclingGridItem {
public:
    NxMediaCard();
    ~NxMediaCard() override = default;

    void setMedia(const std::string& title,
                  const std::string& imageUrl,
                  const std::string& meta,
                  const std::string& mediaId,
                  const std::string& extension = "");

    const std::string& getTitle() const { return mediaTitle; }
    const std::string& getImageUrl() const { return mediaImage; }
    const std::string& getMediaId() const { return mediaId; }
    const std::string& getExtension() const { return mediaExtension; }

    void prepareForReuse() override;
    void cacheForReuse() override;

    static RecyclingGridItem* create();

private:
    std::string mediaTitle;
    std::string mediaImage;
    std::string mediaId;
    std::string mediaExtension;

    BRLS_BIND(brls::Image, picture, "nx/card/picture");
    BRLS_BIND(brls::Label, titleLabel, "nx/card/title");
    BRLS_BIND(brls::Label, metaLabel, "nx/card/meta");
};
