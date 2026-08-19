from PIL import Image
import numpy as np
import os

SIZE = 160
OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "src", "assets")
os.makedirs(OUT_DIR, exist_ok=True)

def center_square_crop(img):
    w, h = img.size
    s = min(w, h)
    left = (w - s) // 2
    top = (h - s) // 2
    return img.crop((left, top, left + s, top + s))

def to_rgb565(img, size):
    img = center_square_crop(img).convert("RGB").resize((size, size), Image.LANCZOS)
    px = np.array(img, dtype=np.uint32)
    r = (px[:, :, 0] >> 3) & 0x1F
    g = (px[:, :, 1] >> 2) & 0x3F
    b = (px[:, :, 2] >> 3) & 0x1F
    return ((r << 11) | (g << 5) | b).astype(np.uint16)

def write_c_array(name, values, size):
    h_path = os.path.join(OUT_DIR, f"{name}.h")
    cpp_path = os.path.join(OUT_DIR, f"{name}.cpp")
    var = name.upper() + "_IMG"

    with open(h_path, "w") as f:
        f.write("#pragma once\n#include <cstdint>\n\n")
        f.write(f"extern const uint16_t {var}[];\n")
        f.write(f"constexpr int {var}_SIZE = {size};\n")

    flat = values.flatten()
    with open(cpp_path, "w") as f:
        f.write(f'#include "{name}.h"\n\n')
        f.write(f"const uint16_t {var}[{len(flat)}] = {{\n")
        line = []
        for i, v in enumerate(flat):
            line.append(str(int(v)))
            if len(line) == 16:
                f.write(",".join(line) + ",\n")
                line = []
        if line:
            f.write(",".join(line) + ",\n")
        f.write("};\n")

    print(f"{name}: {size}x{size} -> {cpp_path} ({os.path.getsize(cpp_path)//1024} KB)")

assets_dir = os.path.dirname(__file__)
cat = Image.open(os.path.join(assets_dir, "cat.png"))
google = Image.open(os.path.join(assets_dir, "google.png"))

write_c_array("cat", to_rgb565(cat, SIZE), SIZE)
write_c_array("google", to_rgb565(google, SIZE), SIZE)
