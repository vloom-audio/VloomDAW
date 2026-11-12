# Icons

| Files | Source | Licence |
|---|---|---|
| `folder.svg`, `settings.svg`, `browser-folder.svg` (Lucide `folder`), `browser-file.svg` (Lucide `file-music`) | [Lucide](https://lucide.dev) `lucide-static` v0.460.0, recoloured (`currentColor` → theme colour, since Qt SVG does not support `currentColor`) | ISC — see `LICENSE-lucide.txt` |
| everything else | drawn for VloomDAW | GPL-3.0-or-later, like the rest of the repository |

Draw icons in these placeholder colours only: text `#e6e9ee`, dim text `#8b95a3`,
accent `#ff8a3d`, record `#e81123`. Load them with `Theme::icon("name")`, which
swaps each placeholder for the current theme's colour when the icon is drawn, so
icons follow the theme (palette, tuning and per-role colours) without a restart.
Other colours in an SVG stay as they are.
When adding a Lucide icon, keep its `@license` comment and add it to the table.
