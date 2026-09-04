#include "view/nx_media_card.hpp"
#include "utils/image_helper.hpp"

NxMediaCard::NxMediaCard() {
    this->inflateFromXMLRes("xml/views/nx_media_card.xml");
}

void NxMediaCard::setMedia(const std::string& title,
                           const std::string& imageUrl,
                           const std::string& meta,
                           const std::string& id,
                           const std::string& extension) {
    mediaTitle = title;
    mediaImage = imageUrl;
    mediaId = id;
    mediaExtension = extension;

    titleLabel->setText(title);
    metaLabel->setText(meta);

    if (imageUrl.empty()) {
        picture->setImageFromRes("pictures/video-card-bg.png");
    } else {
        ImageHelper::with(picture)->load(imageUrl);
    }
}

void NxMediaCard::prepareForReuse() {
    mediaTitle.clear();
    mediaImage.clear();
    mediaId.clear();
    mediaExtension.clear();
    titleLabel->setText("");
    metaLabel->setText("");
    picture->setImageFromRes("pictures/video-card-bg.png");
}

void NxMediaCard::cacheForReuse() {
    ImageHelper::clear(picture);
}

RecyclingGridItem* NxMediaCard::create() {
    return new NxMediaCard();
}
