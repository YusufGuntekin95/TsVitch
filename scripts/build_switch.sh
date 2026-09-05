#!/usr/bin/env bash
set -e

BUILD_DIR=cmake-build-switch

cd "$(dirname "$0")/.."
git config --global --add safe.directory "$(pwd)"

# Keep the base toolchain current.
dkp-pacman -Syu --noconfirm

# The old TsVitch build pinned wiliwili's libmpv 0.36.0-2 / FFmpeg 6.1.
# NX Media now uses the same current Deko3D libmpv package family as Switchfin.
SWITCHFIN_PORTLIBS_URL="https://github.com/dragonflylee/switchfin/releases/download/switch-portlibs"
WILIWILI_URL="https://github.com/xfangfang/wiliwili/releases/download/v0.1.0"

dkp-pacman -R --noconfirm switch-libmpv 2>/dev/null || true

PORTLIBS_PKGS=(
    "libuam-master-1-any.pkg.tar.zst"
    "switch-ffmpeg-7.1.5-5-any.pkg.tar.zst"
    "switch-libmpv-deko3d-0.36.0-5-any.pkg.tar.zst"
)
for PKG in "${PORTLIBS_PKGS[@]}"; do
    [ -f "${PKG}" ] || curl -fL -o "${PKG}" "${SWITCHFIN_PORTLIBS_URL}/${PKG}"
    dkp-pacman -U --noconfirm "${PKG}"
done

# Keep the existing forwarder packaging dependencies only; do not downgrade
# libass/FFmpeg/libmpv anymore.
LEGACY_PACKAGING_PKGS=(
    "switch-nspmini-48d4fc2-1-any.pkg.tar.xz"
    "hacBrewPack-3.05-1-any.pkg.tar.zst"
)
for PKG in "${LEGACY_PACKAGING_PKGS[@]}"; do
    [ -f "${PKG}" ] || curl -fL -o "${PKG}" "${WILIWILI_URL}/${PKG}"
    dkp-pacman -U --noconfirm "${PKG}"
done

# Subtitle-only fix: leave Xtream/fetch/navigation untouched. Text subtitles
# are drawn as a Borealis overlay from mpv's decoded `sub-text` property, which
# bypasses the Deko3D OSD path. Bitmap subtitles keep using mpv, with subtitle
# blending enabled and the newer Deko3D renderer package above.
python3 - <<'PY'
from pathlib import Path

hpp = Path('tsvitch/include/view/video_view.hpp')
s = hpp.read_text(encoding='utf-8')
if 'video/subtitle/box' not in s:
    anchor = '    BRLS_BIND(brls::Box, osdBottomBox, "video/osd/bottom/box");\n'
    if anchor not in s:
        raise SystemExit('video_view.hpp subtitle binding anchor not found')
    s = s.replace(anchor, anchor +
        '    BRLS_BIND(brls::Box, subtitleBox, "video/subtitle/box");\n'
        '    BRLS_BIND(brls::Label, subtitleLabel, "video/subtitle/label");\n', 1)

if 'subtitlePollTick' not in s:
    anchor = '    bool hide_lock_button      = false;\n'
    if anchor not in s:
        raise SystemExit('video_view.hpp subtitle state anchor not found')
    s = s.replace(anchor, anchor +
        '    int64_t subtitlePollTick   = -1;\n'
        '    std::string subtitleText;\n', 1)
hpp.write_text(s, encoding='utf-8')

cpp = Path('tsvitch/source/view/video_view.cpp')
s = cpp.read_text(encoding='utf-8')
if 'NX Media native subtitle overlay' not in s:
    anchor = '    mpvCore->draw(brls::Rect(x, y, width, height), alpha);\n'
    if anchor not in s:
        raise SystemExit('video_view.cpp subtitle draw anchor not found')
    block = r'''

    // NX Media native subtitle overlay: older Deko3D builds can select a
    // subtitle track correctly but fail to composite mpv's subtitle OSD.
    // `sub-text` is mpv's already-decoded plain subtitle text, so rendering it
    // with Borealis bypasses that broken compositor for text subtitle formats.
    const int64_t currentSubtitleTick = static_cast<int64_t>(mpvCore->playback_time * 10.0);
    if (currentSubtitleTick != subtitlePollTick) {
        subtitlePollTick = currentSubtitleTick;
        subtitleText.clear();

        mpv_handle* handle = mpvCore->getHandle();
        char* rawSubtitle = nullptr;
        if (handle && mpvGetProperty(handle, "sub-text", MPV_FORMAT_STRING, &rawSubtitle) >= 0 && rawSubtitle) {
            subtitleText = rawSubtitle;
            mpvFree(rawSubtitle);
        }

        if (subtitleText.empty()) {
            subtitleBox->setVisibility(brls::Visibility::INVISIBLE);
        } else {
            subtitleLabel->setText(subtitleText);
            subtitleBox->setVisibility(brls::Visibility::VISIBLE);
        }
    }

    if (!subtitleText.empty()) subtitleBox->frame(ctx);
'''
    s = s.replace(anchor, anchor + block, 1)
cpp.write_text(s, encoding='utf-8')

xml = Path('resources/xml/views/video_view.xml')
s = xml.read_text(encoding='utf-8')
if 'id="video/subtitle/box"' not in s:
    anchor = '''        <VideoProfile
                id="video/profile"'''
    if anchor not in s:
        raise SystemExit('video_view.xml subtitle overlay anchor not found')
    block = '''        <brls:Box
                id="video/subtitle/box"
                positionType="absolute"
                positionLeft="120"
                positionBottom="92"
                width="1040"
                height="auto"
                visibility="invisible"
                justifyContent="center"
                alignItems="center"
                cornerRadius="7"
                backgroundColor="#000000B8">
                <brls:Label
                        id="video/subtitle/label"
                        width="100%"
                        height="auto"
                        margin="10"
                        fontSize="30"
                        horizontalAlign="center"
                        verticalAlign="center"
                        textColor="#FFFFFF" />
        </brls:Box>

'''
    s = s.replace(anchor, block + anchor, 1)
xml.write_text(s, encoding='utf-8')

mpv = Path('tsvitch/source/view/mpv_core.cpp')
s = mpv.read_text(encoding='utf-8')
if '"blend-subtitles", "yes"' not in s:
    anchor = '    mpvSetOptionString(mpv, "vo", "libmpv");\n'
    if anchor not in s:
        raise SystemExit('mpv_core.cpp blend subtitle anchor not found')
    s = s.replace(anchor, anchor +
        '    mpvSetOptionString(mpv, "blend-subtitles", "yes");\n'
        '    mpvSetOptionString(mpv, "subs-fallback", "yes");\n', 1)

# Match the current Switchfin Deko3D render-context setup. Advanced control is
# useful only when the app fully owns frame timing; it is unnecessary here and
# can interfere with OSD/subtitle invalidation on the older TsVitch loop.
old = '''#elif defined(BOREALIS_USE_DEKO3D)
    int advanced_control{1};
    auto switchPlatform = (brls::SwitchVideoContext *)brls::Application::getPlatform()->getVideoContext();
    mpv_deko3d_init_params deko_init_params{switchPlatform->getDeko3dDevice()};
    mpv_render_param params[]{{MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_DEKO3D)},
                              {MPV_RENDER_PARAM_DEKO3D_INIT_PARAMS, &deko_init_params},
                              {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced_control},
                              {MPV_RENDER_PARAM_INVALID, nullptr}};
'''
new = '''#elif defined(BOREALIS_USE_DEKO3D)
    auto switchPlatform = (brls::SwitchVideoContext *)brls::Application::getPlatform()->getVideoContext();
    mpv_deko3d_init_params deko_init_params{switchPlatform->getDeko3dDevice()};
    mpv_render_param params[]{{MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_DEKO3D)},
                              {MPV_RENDER_PARAM_DEKO3D_INIT_PARAMS, &deko_init_params},
                              {MPV_RENDER_PARAM_INVALID, nullptr}};
'''
if old in s:
    s = s.replace(old, new, 1)
elif 'MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced_control' in s:
    raise SystemExit('Unexpected Deko3D advanced-control layout')
mpv.write_text(s, encoding='utf-8')

print('NX Media subtitle renderer: native text overlay + current Deko3D path enabled')
PY

if [ -z "${GA_ID}" ] || [ -z "${GA_KEY}" ]; then
    echo "GA_ID or GA_KEY not found in environment"
    exit 1
fi
if [ -z "${SERVER_URL}" ]; then
    echo "SERVER_URL not found in environment"
    exit 1
fi
if [ -z "${SERVER_TOKEN}" ]; then
    echo "SERVER_TOKEN not found in environment"
    exit 1
fi
if [ -z "${M3U8_URL}" ]; then
    echo "M3U8_URL not found in environment"
    exit 1
fi

GITHUB_TOKEN_FLAG=""
if [ -n "${GITHUB_TOKEN}" ]; then
    GITHUB_TOKEN_FLAG="-DGITHUB_TOKEN=\"${GITHUB_TOKEN}\""
fi

UNITY_BUILD_FLAG="-DBRLS_UNITY_BUILD=OFF"
if [ "${ENABLE_UNITY_BUILD}" = "true" ]; then
    UNITY_BUILD_FLAG="-DBRLS_UNITY_BUILD=ON"
fi

cmake -B ${BUILD_DIR} \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILTIN_NSP=ON \
  -DPLATFORM_SWITCH=ON \
  ${UNITY_BUILD_FLAG} \
  -DCMAKE_UNITY_BUILD_BATCH_SIZE=16 \
  -DANALYTICS=ON \
  -DANALYTICS_ID="${GA_ID}" \
  -DANALYTICS_KEY="${GA_KEY}" \
  -DSERVER_URL="${SERVER_URL}" \
  -DSERVER_TOKEN="${SERVER_TOKEN}" \
  -DM3U8_URL="${M3U8_URL}" \
  ${GITHUB_TOKEN_FLAG}

make -C ${BUILD_DIR} TsVitch.nro -j$(nproc)
