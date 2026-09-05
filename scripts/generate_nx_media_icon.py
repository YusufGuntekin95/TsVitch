from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

SIZE = 256
OUT = Path("resources/icon/icon.jpg")

# Premium dark background with a subtle red glow.
img = Image.new("RGB", (SIZE, SIZE), (7, 8, 11))
px = img.load()
for y in range(SIZE):
    for x in range(SIZE):
        dx = x - 132
        dy = y - 116
        d = (dx * dx + dy * dy) ** 0.5
        glow = max(0.0, 1.0 - d / 185.0)
        r = int(7 + 22 * glow)
        g = int(8 + 2 * glow)
        b = int(11 + 3 * glow)
        px[x, y] = (r, g, b)

# Soft red halo behind the mark.
halo = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
hd = ImageDraw.Draw(halo)
hd.ellipse((39, 39, 217, 217), fill=(225, 26, 42, 72))
halo = halo.filter(ImageFilter.GaussianBlur(24))
img = Image.alpha_composite(img.convert("RGBA"), halo)

d = ImageDraw.Draw(img)

# Rounded premium tile frame.
d.rounded_rectangle((15, 15, 241, 241), radius=46, outline=(255, 255, 255, 30), width=2)
d.rounded_rectangle((20, 20, 236, 236), radius=42, outline=(225, 26, 42, 120), width=3)

# Play glyph.
d.polygon([(47, 86), (47, 170), (106, 128)], fill=(228, 28, 45, 255))

font_candidates = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
]
font_path = next((p for p in font_candidates if Path(p).exists()), None)
if font_path:
    font_nx = ImageFont.truetype(font_path, 76)
    font_media = ImageFont.truetype(font_path, 20)
else:
    font_nx = ImageFont.load_default()
    font_media = ImageFont.load_default()

# NX wordmark.
d.text((101, 77), "N", font=font_nx, fill=(248, 248, 250, 255), stroke_width=1)
d.text((150, 77), "X", font=font_nx, fill=(228, 28, 45, 255), stroke_width=1)

label = "MEDIA"
bbox = d.textbbox((0, 0), label, font=font_media)
label_w = bbox[2] - bbox[0]
d.text(((SIZE - label_w) / 2, 188), label, font=font_media, fill=(210, 213, 221, 255))

# JPEG for elf2nro.
OUT.parent.mkdir(parents=True, exist_ok=True)
img.convert("RGB").save(OUT, "JPEG", quality=95, subsampling=0)
print(f"Generated {OUT}")


def patch_subtitle_overlay():
    """Add a Switch-side text subtitle fallback rendered by Borealis.

    libmpv on Switch can expose the selected subtitle text through the sub-text
    property even when libass text glyphs fail to appear in the deko3d video
    render path. This overlay keeps bitmap subtitles on libmpv while giving
    text subtitles a UI-rendered fallback.
    """
    header = Path("tsvitch/include/view/video_view.hpp")
    cpp = Path("tsvitch/source/view/video_view.cpp")
    xml = Path("resources/xml/views/video_view.xml")

    hs = header.read_text(encoding="utf-8")
    if 'video/subtitle/box' not in hs:
        bind_anchor = '    BRLS_BIND(brls::Box, osdBottomBox, "video/osd/bottom/box");\n'
        bind_block = bind_anchor + '''\n    // NX Media Switch subtitle fallback overlay.\n    BRLS_BIND(brls::Box, subtitleBox, "video/subtitle/box");\n    BRLS_BIND(brls::Label, subtitleLabel, "video/subtitle/label");\n'''
        if bind_anchor not in hs:
            raise SystemExit("Subtitle overlay header bind anchor not found")
        hs = hs.replace(bind_anchor, bind_block, 1)

        member_anchor = '    MPVCore* mpvCore;\n'
        member_block = member_anchor + '''\n    std::string lastSubtitleText;\n    int subtitlePollDivider = 0;\n'''
        if member_anchor not in hs:
            raise SystemExit("Subtitle overlay member anchor not found")
        hs = hs.replace(member_anchor, member_block, 1)

        method_anchor = '    float getRealDuration();\n'
        method_block = '''    void updateSubtitleOverlay();\n\n''' + method_anchor
        if method_anchor not in hs:
            raise SystemExit("Subtitle overlay method anchor not found")
        hs = hs.replace(method_anchor, method_block, 1)
        header.write_text(hs, encoding="utf-8")

    xs = xml.read_text(encoding="utf-8")
    if 'id="video/subtitle/box"' not in xs:
        xml_anchor = '''        <VideoProfile\n                id="video/profile"'''
        overlay = '''        <!-- NX Media: text subtitle fallback shown above the player controls. -->\n        <brls:Box\n                id="video/subtitle/box"\n                positionType="absolute"\n                positionBottom="118"\n                width="100%"\n                height="106"\n                alignItems="center"\n                justifyContent="center"\n                visibility="gone">\n                <brls:Label\n                        id="video/subtitle/label"\n                        width="1000"\n                        height="96"\n                        fontSize="32"\n                        textColor="#FFFFFF"\n                        backgroundColor="#000000B8"\n                        cornerRadius="8"\n                        horizontalAlign="center"\n                        verticalAlign="center" />\n        </brls:Box>\n\n'''
        if xml_anchor not in xs:
            raise SystemExit("Subtitle overlay XML anchor not found")
        xs = xs.replace(xml_anchor, overlay + xml_anchor, 1)
        xml.write_text(xs, encoding="utf-8")

    cs = cpp.read_text(encoding="utf-8")
    if 'void VideoView::updateSubtitleOverlay()' not in cs:
        draw_anchor = '''void VideoView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style,\n                     brls::FrameContext* ctx) {'''
        method = r'''void VideoView::updateSubtitleOverlay() {
#ifdef __SWITCH__
    // Poll at roughly 10 Hz instead of every frame to avoid adding player load.
    if (++subtitlePollDivider < 6) return;
    subtitlePollDivider = 0;

    std::string text = mpvCore->getString("sub-text");

    // Some ASS streams expose literal line-break escapes through sub-text.
    size_t pos = 0;
    while ((pos = text.find("\\N", pos)) != std::string::npos) {
        text.replace(pos, 2, "\n");
        pos += 1;
    }

    if (text == lastSubtitleText) return;
    lastSubtitleText = text;

    if (text.empty()) {
        subtitleLabel->setText("");
        subtitleBox->setVisibility(brls::Visibility::GONE);
        return;
    }

    subtitleLabel->setText(text);
    subtitleBox->setVisibility(brls::Visibility::VISIBLE);
#endif
}

'''
        if draw_anchor not in cs:
            raise SystemExit("Subtitle overlay draw anchor not found")
        cs = cs.replace(draw_anchor, method + draw_anchor, 1)

        draw_call_anchor = '    mpvCore->draw(brls::Rect(x, y, width, height), alpha);\n'
        draw_call_block = draw_call_anchor + '''\n    updateSubtitleOverlay();\n    if (subtitleBox->getVisibility() == brls::Visibility::VISIBLE) {\n        subtitleBox->frame(ctx);\n    }\n'''
        if draw_call_anchor not in cs:
            raise SystemExit("Subtitle overlay frame anchor not found")
        cs = cs.replace(draw_call_anchor, draw_call_block, 1)
        cpp.write_text(cs, encoding="utf-8")

    print("Patched NX Media Switch subtitle fallback overlay")


patch_subtitle_overlay()
