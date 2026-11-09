"""Vendor pinned source dependencies for the built-in overlay (build time only)."""
from pathlib import Path
from urllib.request import Request, urlopen
from io import BytesIO
import hashlib
import json
import re
import zipfile

__author__ = "sclliang"
__company__ = "sixunited"
__copyright__ = "Copyright (c) 2026 sixunited. Author: sclliang"

root = Path(__file__).resolve().parents[1]
dest = root / "third_party" / "game_overlay"
dest.mkdir(parents=True, exist_ok=True)
pins = {}

def archive(repo, tag):
    url = f"https://codeload.github.com/{repo}/zip/refs/tags/{tag}"
    req = Request(url, headers={"User-Agent": "CpuOverlay-source-setup"})
    with urlopen(req, timeout=60) as response:
        data = response.read()
    pins[repo] = {"tag": tag, "url": url, "sha256": hashlib.sha256(data).hexdigest()}
    return zipfile.ZipFile(BytesIO(data))

def copy_files(z, prefix, target, include):
    for entry in z.namelist():
        if entry.endswith("/") or not entry.startswith(prefix):
            continue
        relative = entry[len(prefix):]
        if not include(relative):
            continue
        path = (target / relative).resolve()
        if not path.is_relative_to(dest.resolve()):
            raise ValueError("Unexpected archive path")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(z.read(entry))

with archive("TsudaKageyu/minhook", "v1.3.4") as z:
    prefix = z.namelist()[0]
    copy_files(z, prefix, dest / "minhook", lambda p: p.startswith(("include/", "src/")) or p == "LICENSE.txt")

with archive("KhronosGroup/Vulkan-Headers", "v1.3.296") as z:
    prefix = z.namelist()[0]
    copy_files(z, prefix, dest / "vulkan", lambda p: p.startswith("include/") or p.startswith("LICENSE"))

with archive("ocornut/imgui", "v1.91.9b") as z:
    prefix = z.namelist()[0]
    (dest / "LICENSE-imgui.txt").write_bytes(z.read(prefix + "LICENSE.txt"))
    source = z.read(prefix + "backends/imgui_impl_vulkan.cpp").decode("utf-8")
    arrays = re.findall(r"static uint32_t __glsl_shader_(?:vert|frag)_spv\[\]\s*=\s*\{.*?\};", source, re.S)
    if len(arrays) != 2:
        raise ValueError("Pinned Vulkan shader arrays were not found")
    out = "// Vulkan shader bytecode from Dear ImGui v1.91.9b, MIT. See LICENSE-imgui.txt.\n#pragma once\n#include <cstdint>\nnamespace NativeOverlayShaders {\n"
    out += "\n".join(a.replace("static uint32_t", "inline constexpr uint32_t").replace("__glsl_shader_", "") for a in arrays)
    out += "\n}\n"
    (dest / "vulkan_shaders.h").write_text(out, encoding="utf-8")

license_text = (dest / "vulkan" / "LICENSE.md").read_text(encoding="utf-8")
for path in sorted((dest / "vulkan" / "LICENSES").glob("*")):
    if path.is_file():
        license_text += "\n\n" + path.name + "\n" + path.read_text(encoding="utf-8")
(dest / "LICENSE-Vulkan-Headers.txt").write_text(license_text, encoding="utf-8")

(dest / "pins.json").write_text(json.dumps(pins, indent=2) + "\n", encoding="utf-8")
print("Vendored MinHook, Vulkan headers, and MIT-licensed Vulkan shader bytecode.")
