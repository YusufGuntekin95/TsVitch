

#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_slider.hpp>
#include <borealis/views/cells/cell_input.hpp>

#include "utils/config_helper.hpp"
#include "utils/shader_helper.hpp"
#include "utils/number_helper.hpp"
#include "utils/activity_helper.hpp"

#include "fragment/player_setting.hpp"

#include "view/button_close.hpp"

#include "view/video_view.hpp"
#include "view/selector_cell.hpp"
#include "view/svg_image.hpp"
#include "view/mpv_core.hpp"

using namespace brls::literals;

namespace {
struct PlayerTrack {
    int64_t id = -1;
    std::string type;
    std::string lang;
    std::string title;
    std::string codec;
    bool selected = false;
};

std::string languageLabel(const std::string& lang) {
    if (lang == "tur" || lang == "tr") return "Türkçe";
    if (lang == "eng" || lang == "en") return "İngilizce";
    if (lang == "deu" || lang == "ger" || lang == "de") return "Almanca";
    if (lang == "fra" || lang == "fre" || lang == "fr") return "Fransızca";
    if (lang == "ita" || lang == "it") return "İtalyanca";
    if (lang == "spa" || lang == "es") return "İspanyolca";
    if (lang == "por" || lang == "pt") return "Portekizce";
    if (lang == "ara" || lang == "ar") return "Arapça";
    return lang;
}

std::string trackLabel(const PlayerTrack& track, size_t fallbackIndex) {
    if (!track.title.empty()) return track.title;
    std::string lang = languageLabel(track.lang);
    if (!lang.empty()) return lang;
    if (track.type == "audio") return fmt::format("Ses {}", fallbackIndex + 1);
    return fmt::format("Altyazı {}", fallbackIndex + 1);
}

std::vector<PlayerTrack> getPlayerTracks(const std::string& wantedType) {
    std::vector<PlayerTrack> tracks;
    mpv_handle* handle = MPVCore::instance().getHandle();
    if (!handle) return tracks;

    mpv_node node{};
    if (mpvGetProperty(handle, "track-list", MPV_FORMAT_NODE, &node) < 0) return tracks;
    if (node.format != MPV_FORMAT_NODE_ARRAY || node.u.list == nullptr) {
        mpvFreeNodeContents(&node);
        return tracks;
    }

    mpv_node_list* list = node.u.list;
    for (int i = 0; i < list->num; i++) {
        const mpv_node& item = list->values[i];
        if (item.format != MPV_FORMAT_NODE_MAP || item.u.list == nullptr) continue;

        PlayerTrack track;
        mpv_node_list* map = item.u.list;
        for (int j = 0; j < map->num; j++) {
            if (map->keys[j] == nullptr) continue;
            const std::string key = map->keys[j];
            const mpv_node& value = map->values[j];
            if (key == "id" && value.format == MPV_FORMAT_INT64) {
                track.id = value.u.int64;
            } else if (key == "type" && value.format == MPV_FORMAT_STRING && value.u.string) {
                track.type = value.u.string;
            } else if (key == "lang" && value.format == MPV_FORMAT_STRING && value.u.string) {
                track.lang = value.u.string;
            } else if (key == "title" && value.format == MPV_FORMAT_STRING && value.u.string) {
                track.title = value.u.string;
            } else if (key == "codec" && value.format == MPV_FORMAT_STRING && value.u.string) {
                track.codec = value.u.string;
            } else if (key == "selected" && value.format == MPV_FORMAT_FLAG) {
                track.selected = value.u.flag != 0;
            }
        }
        if (track.type == wantedType && track.id >= 0) tracks.push_back(track);
    }

    mpvFreeNodeContents(&node);
    return tracks;
}
}  // namespace

PlayerSetting::PlayerSetting() {
    this->inflateFromXMLRes("xml/fragment/player_setting.xml");
    brls::Logger::debug("Fragment PlayerSetting: create");

    setupCommonSetting();
    setupTrackSetting();

    this->registerAction("Kapat", brls::BUTTON_B, [](...) {
        brls::Application::popActivity();
        return true;
    });

    this->cancel->registerClickAction([](...) {
        brls::Application::popActivity();
        return true;
    });
    this->cancel->addGestureRecognizer(new brls::TapGestureRecognizer(this->cancel));

    closebtn->registerClickAction([](...) {
        brls::Application::popActivity();
        return true;
    });
}

PlayerSetting::~PlayerSetting() { brls::Logger::debug("Fragment PlayerSetting: delete"); }

brls::View* PlayerSetting::create() { return new PlayerSetting(); }

bool PlayerSetting::isTranslucent() { return true; }

brls::View* PlayerSetting::getDefaultFocus() { return this->settings->getDefaultFocus(); }

void PlayerSetting::setupTrackSetting() {
    btnSubtitle->setText("Altyazı");
    btnAudioTrack->setText("Ses dili");
    refreshTrackDetails();

    btnSubtitle->registerClickAction([this](View* view) {
        auto tracks = getPlayerTracks("sub");
        std::vector<std::string> options = {"Kapalı"};
        int selectedIndex = 0;

        for (size_t i = 0; i < tracks.size(); i++) {
            options.push_back(trackLabel(tracks[i], i));
            if (tracks[i].selected) selectedIndex = static_cast<int>(i) + 1;
        }

        BaseDropdown::text(
            "Altyazı", options,
            [this, tracks, options](int data) {
                if (data <= 0) {
                    MPVCore::instance().command_async("set", "sid", "no");
                    MPVCore::instance().command_async("set", "sub-visibility", "no");
                    btnSubtitle->setDetailText("Kapalı");
                    return;
                }

                size_t trackIndex = static_cast<size_t>(data - 1);
                if (trackIndex >= tracks.size()) return;

                const auto& track = tracks[trackIndex];
                brls::Logger::info("NX Media altyazı seçildi: id={} lang={} codec={} title={}",
                                   track.id, track.lang, track.codec, track.title);

                MPVCore::instance().command_async("set", "sub-visibility", "yes");
                MPVCore::instance().command_async("set", "secondary-sub-visibility", "yes");
                MPVCore::instance().command_async("set", "sub-ass-override", "strip");
                MPVCore::instance().command_async("set", "sid", track.id);
                btnSubtitle->setDetailText(options[data]);
            },
            selectedIndex);
        return true;
    });

    btnAudioTrack->registerClickAction([this](View* view) {
        auto tracks = getPlayerTracks("audio");
        if (tracks.empty()) {
            btnAudioTrack->setDetailText("Yok");
            return true;
        }

        std::vector<std::string> options;
        int selectedIndex = 0;
        for (size_t i = 0; i < tracks.size(); i++) {
            options.push_back(trackLabel(tracks[i], i));
            if (tracks[i].selected) selectedIndex = static_cast<int>(i);
        }

        BaseDropdown::text(
            "Ses dili", options,
            [this, tracks, options](int data) {
                if (data < 0 || static_cast<size_t>(data) >= tracks.size()) return;
                MPVCore::instance().command_async("set", "aid", tracks[data].id);
                btnAudioTrack->setDetailText(options[data]);
            },
            selectedIndex);
        return true;
    });
}

void PlayerSetting::refreshTrackDetails() {
    auto subtitles = getPlayerTracks("sub");
    std::string subtitleDetail = "Kapalı";
    for (size_t i = 0; i < subtitles.size(); i++) {
        if (subtitles[i].selected) {
            subtitleDetail = trackLabel(subtitles[i], i);
            break;
        }
    }
    btnSubtitle->setDetailText(subtitleDetail);

    auto audioTracks = getPlayerTracks("audio");
    if (audioTracks.empty()) {
        btnAudioTrack->setDetailText("Yok");
        return;
    }

    std::string audioDetail = trackLabel(audioTracks[0], 0);
    for (size_t i = 0; i < audioTracks.size(); i++) {
        if (audioTracks[i].selected) {
            audioDetail = trackLabel(audioTracks[i], i);
            break;
        }
    }
    btnAudioTrack->setDetailText(audioDetail);
}

void PlayerSetting::setupCommonSetting() {
    auto locale = brls::Application::getLocale();

    btnMirror->init("Görüntüyü yansıt", MPVCore::VIDEO_MIRROR, [](bool value) {
        MPVCore::instance().setMirror(!MPVCore::VIDEO_MIRROR);
        GA("player_setting", {{"mirror", value ? "true" : "false"}});

        if (MPVCore::HARDWARE_DEC) {
            std::string hwdec = MPVCore::VIDEO_MIRROR ? "auto-copy" : MPVCore::PLAYER_HWDEC_METHOD;
            MPVCore::instance().command_async("set", "hwdec", hwdec);
            brls::Logger::info("MPV hardware decode: {}", hwdec);
        }
    });

    btnSleep->setText("Uyku zamanlayıcısı");
    updateCountdown(tsvitch::getUnixTime());
    btnSleep->registerClickAction([this](View* view) {
        std::vector<int> timeList = {15, 30, 60, 90, 120};
        std::vector<std::string> optionList = {"15 dakika", "30 dakika", "60 dakika", "90 dakika", "120 dakika"};
        bool countdownStarted = MPVCore::CLOSE_TIME != 0 && tsvitch::getUnixTime() < MPVCore::CLOSE_TIME;
        if (countdownStarted) {
            timeList.insert(timeList.begin(), -1);
            optionList.insert(optionList.begin(), "Kapalı");
        }
        BaseDropdown::text(
            "Uyku zamanlayıcısı", optionList,
            [this, timeList, countdownStarted](int data) {
                if (countdownStarted && data == 0) {
                    MPVCore::CLOSE_TIME = 0;
                    GA("player_setting", {{"sleep", "-1"}});
                } else {
                    MPVCore::CLOSE_TIME = tsvitch::getUnixTime() + timeList[data] * 60;
                    GA("player_setting", {{"sleep", timeList[data]}});
                }
                updateCountdown(tsvitch::getUnixTime());
            },
            -1);
        return true;
    });

#ifdef ALLOW_FULLSCREEN
    auto& conf = ProgramConfig::instance();
    btnFullscreen->init("Tam ekran", conf.getBoolOption(SettingItem::FULLSCREEN),
                        [](bool value) {
                            ProgramConfig::instance().setSettingItem(SettingItem::FULLSCREEN, value);
                            VideoContext::FULLSCREEN = value;
                            brls::Application::getPlatform()->getVideoContext()->fullScreen(value);
                            GA("player_setting", {{"fullscreen", value ? "true" : "false"}});
                        });

    auto setOnTopCell = [this](bool enabled) {
        if (enabled) {
            btnOnTopMode->setDetailTextColor(brls::Application::getTheme()["brls/list/listItem_value_color"]);
        } else {
            btnOnTopMode->setDetailTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        }
    };
    setOnTopCell(conf.getIntOptionIndex(SettingItem::ON_TOP_MODE) != 0);
    int onTopModeIndex = conf.getIntOption(SettingItem::ON_TOP_MODE);
    btnOnTopMode->setText("Her zaman üstte");
    std::vector<std::string> onTopOptionList = {"Kapalı", "Açık", "Otomatik"};
    btnOnTopMode->setDetailText(onTopOptionList[onTopModeIndex]);
    btnOnTopMode->registerClickAction([this, onTopOptionList, setOnTopCell](brls::View* view) {
        BaseDropdown::text(
            "Her zaman üstte", onTopOptionList,
            [this, onTopOptionList, setOnTopCell](int data) {
                btnOnTopMode->setDetailText(onTopOptionList[data]);
                ProgramConfig::instance().setSettingItem(SettingItem::ON_TOP_MODE, data);
                ProgramConfig::instance().checkOnTop();
                setOnTopCell(data != 0);
                GA("player_setting", {{"on_top_mode", data}});
            },
            ProgramConfig::instance().getIntOption(SettingItem::ON_TOP_MODE));
        return true;
    });
#else
    btnFullscreen->setVisibility(brls::Visibility::GONE);
    btnOnTopMode->setVisibility(brls::Visibility::GONE);
#endif

    btnEqualizerReset->registerClickAction([this](View* view) {
        btnEqualizerBrightness->slider->setProgress(0.5f);
        btnEqualizerContrast->slider->setProgress(0.5f);
        btnEqualizerSaturation->slider->setProgress(0.5f);
        btnEqualizerGamma->slider->setProgress(0.5f);
        btnEqualizerHue->slider->setProgress(0.5f);
        return true;
    });
    registerHideBackground(btnEqualizerReset);

    setupEqualizerSetting(btnEqualizerBrightness, "Parlaklık", SettingItem::PLAYER_BRIGHTNESS,
                          MPVCore::instance().getBrightness());
    setupEqualizerSetting(btnEqualizerContrast, "Kontrast", SettingItem::PLAYER_CONTRAST,
                          MPVCore::instance().getContrast());
    setupEqualizerSetting(btnEqualizerSaturation, "Doygunluk", SettingItem::PLAYER_SATURATION,
                          MPVCore::instance().getSaturation());
    setupEqualizerSetting(btnEqualizerGamma, "Gamma", SettingItem::PLAYER_GAMMA,
                          MPVCore::instance().getGamma());
    setupEqualizerSetting(btnEqualizerHue, "Renk tonu", SettingItem::PLAYER_HUE,
                          MPVCore::instance().getHue());
}

void PlayerSetting::setupEqualizerSetting(brls::SliderCell* cell, const std::string& title, SettingItem item,
                                          int initValue) {
    if (initValue < -100) initValue = -100;
    if (initValue > 100) initValue = 100;
    cell->detail->setWidth(50);
    cell->title->setWidth(116);
    cell->title->setMarginRight(0);
    cell->slider->setStep(0.05f);
    cell->slider->setMarginRight(0);
    cell->slider->setPointerSize(20);
    cell->setDetailText(std::to_string(initValue));
    cell->init(title, (initValue + 100) * 0.005f, [cell, item](float value) {
        int data = (int)(value * 200 - 100);
        if (data < -100) data = -100;
        if (data > 100) data = 100;
        cell->detail->setText(std::to_string(data));
        switch (item) {
            case SettingItem::PLAYER_BRIGHTNESS:
                MPVCore::instance().setBrightness(data);
                break;
            case SettingItem::PLAYER_CONTRAST:
                MPVCore::instance().setContrast(data);
                break;
            case SettingItem::PLAYER_SATURATION:
                MPVCore::instance().setSaturation(data);
                break;
            case SettingItem::PLAYER_GAMMA:
                MPVCore::instance().setGamma(data);
                break;
            case SettingItem::PLAYER_HUE:
                MPVCore::instance().setHue(data);
                break;
            default:
                break;
        }
        static size_t iter = 0;
        brls::cancelDelay(iter);
        iter = brls::delay(200, []() {
            ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_BRIGHTNESS, MPVCore::VIDEO_BRIGHTNESS, false);
            ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_CONTRAST, MPVCore::VIDEO_CONTRAST, false);
            ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_SATURATION, MPVCore::VIDEO_SATURATION, false);
            ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_GAMMA, MPVCore::VIDEO_GAMMA, false);
            ProgramConfig::instance().setSettingItem(SettingItem::PLAYER_HUE, MPVCore::VIDEO_HUE, false);
            ProgramConfig::instance().save();
        });
    });
    registerHideBackground(cell->getDefaultFocus());
}

void PlayerSetting::registerHideBackground(brls::View* view) {
    view->getFocusEvent()->subscribe([this](...) { this->setBackgroundColor(nvgRGBAf(0.0f, 0.0f, 0.0f, 0.0f)); });
    view->getFocusLostEvent()->subscribe(
        [this](...) { this->setBackgroundColor(brls::Application::getTheme().getColor("brls/backdrop")); });
}

void PlayerSetting::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,
                         brls::FrameContext* ctx) {
    static size_t updateTime = 0;
    size_t now = tsvitch::getUnixTime();
    if (now != updateTime) {
        updateTime = now;
        updateCountdown(now);
    }
    Box::draw(vg, x, y, width, height, style, ctx);
}

void PlayerSetting::updateCountdown(size_t now) {
    if (MPVCore::CLOSE_TIME == 0 || now > MPVCore::CLOSE_TIME) {
        btnSleep->setDetailTextColor(brls::Application::getTheme()["brls/text_disabled"]);
        btnSleep->setDetailText("Kapalı");
    } else {
        btnSleep->setDetailTextColor(brls::Application::getTheme()["brls/list/listItem_value_color"]);
        btnSleep->setDetailText(tsvitch::sec2Time(MPVCore::CLOSE_TIME - now));
    }
}
