# App icon

`vloomdaw-source.png` (2048 x 2048, transparent background) is the master
artwork, provided by the Vloom Audio maintainers. Everything else here is
generated from it by `scripts/make_app_icons.py` (Pillow):

| File | Used by |
|---|---|
| `vloomdaw.ico` | Windows: the executable icon (`src/app/VloomDAW.rc`), 16 to 256 px |
| `vloomdaw.icns` | macOS: the app bundle icon, on Apple's icon grid |
| `vloomdaw.png` | Window icon through Qt (`:/app/vloomdaw.png`), all platforms |

After changing the artwork, run `python scripts/make_app_icons.py` and commit
the regenerated files.
