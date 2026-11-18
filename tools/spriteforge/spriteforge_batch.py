#!/usr/bin/env python3
"""Export deterministic SpriteForge directional frames using Playwright."""
import argparse
import base64
import json
import math
import pathlib
import shutil

from PIL import Image
from playwright.sync_api import sync_playwright


def main():
    parser = argparse.ArgumentParser(description="SpriteForge headless sprite guide export")
    parser.add_argument("--preset", required=True)
    parser.add_argument("--mesh", help="OBJ/GLB source mesh, required for imported mesh preset")
    parser.add_argument("--output", default="spriteforge_output")
    parser.add_argument("--html", default=str(pathlib.Path(__file__).with_name("spriteforge.html")))
    args = parser.parse_args()
    preset = json.loads(pathlib.Path(args.preset).read_text(encoding="utf-8"))
    config = preset.get("configuration", preset)
    if config.get("asset") == "mesh" and not args.mesh:
        parser.error("--mesh is required for imported asset presets")
    output = pathlib.Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(
            headless=True,
            executable_path=shutil.which("chromium") or shutil.which("google-chrome") or None,
            args=["--no-sandbox"],
        )
        try:
            page = browser.new_page(viewport={"width": 1500, "height": 950})
            page.set_content(pathlib.Path(args.html).read_text(encoding="utf-8"), wait_until="load")
            if args.mesh:
                page.locator("#meshFile").set_input_files(str(pathlib.Path(args.mesh).resolve()))
                page.wait_for_function(
                    "document.querySelector('#meshStatus').textContent.includes('triangles')"
                )
            page.evaluate("p => window.SpriteForgeAPI.applyPreset(p)", preset)
            manifest = page.evaluate("window.SpriteForgeAPI.exportManifest()")
            count = int(config.get("facings", 8))
            for index in range(count):
                encoded = page.evaluate(
                    """index => {
                        const p = window.SpriteForgeAPI.params();
                        const canvas = document.createElement('canvas');
                        canvas.width = p.fw;
                        canvas.height = p.fh;
                        window.SpriteForgeAPI.draw(canvas.getContext('2d'), p,
                            window.SpriteForgeAPI.angleAt(index, p), true);
                        return canvas.toDataURL('image/png').split(',')[1];
                    }""",
                    index,
                )
                (output / f"frame_{index:02d}.png").write_bytes(base64.b64decode(encoded))
            images = [
                Image.open(output / f"frame_{i:02d}.png").convert("RGBA")
                for i in range(count)
            ]
            columns = min(4, count)
            rows = math.ceil(count / columns)
            width, height = images[0].size
            sheet = Image.new("RGBA", (columns * width, rows * height))
            for i, image in enumerate(images):
                sheet.paste(image, ((i % columns) * width, (i // columns) * height))
            sheet.save(output / "spritesheet.png")
            (output / "manifest.json").write_text(
                json.dumps(manifest, indent=2), encoding="utf-8"
            )
        finally:
            browser.close()
    print(f"Exported {count} frames to {output.resolve()}")


if __name__ == "__main__":
    main()
