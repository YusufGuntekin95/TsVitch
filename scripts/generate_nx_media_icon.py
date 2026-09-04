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

# NX Media must not share TsVitch's runtime data.  The old app and NX Media
# can coexist on the same SD card without writing to the same config/cache.
config = Path("tsvitch/source/utils/config_helper.cpp")
source = config.read_text(encoding="utf-8")
source = source.replace('return "/config/tsvitch";', 'return "/config/nxmedia";', 1)
source = source.replace('this->getConfigDir() + "/tsvitch_config.json"',
                        'this->getConfigDir() + "/nxmedia_config.json"')

migration_marker = '    std::ifstream readFile(path);\n'
if "NX Media one-time TsVitch config migration" not in source:
    migration = '''#ifdef __SWITCH__
    // NX Media one-time TsVitch config migration: copy only the settings file,
    // then keep both apps fully isolated from this point on.
    const std::string legacyConfigPath = "/config/tsvitch/tsvitch_config.json";
    if (!cpr::fs::exists(path) && cpr::fs::exists(legacyConfigPath)) {
        cpr::fs::create_directories(this->getConfigDir());
        std::ifstream legacyConfig(legacyConfigPath, std::ios::binary);
        std::ofstream nxConfig(path, std::ios::binary | std::ios::trunc);
        if (legacyConfig && nxConfig) {
            nxConfig << legacyConfig.rdbuf();
            nxConfig.close();
            legacyConfig.close();
            brls::Logger::info("NX Media one-time TsVitch config migration completed");
        }
    }
#endif

'''
    if migration_marker not in source:
        raise SystemExit("NX Media config migration anchor not found")
    source = source.replace(migration_marker, migration + migration_marker, 1)

if 'return "/config/nxmedia";' not in source:
    raise SystemExit("NX Media config directory patch failed")
config.write_text(source, encoding="utf-8")
print("NX Media runtime data isolated under /config/nxmedia")
