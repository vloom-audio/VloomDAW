# Fonts

The interface font is Noto Sans Thai, with Noto Sans for Latin letters and
digits (Noto Sans Thai has none), bundled so the app looks the same on every OS (neither is
installed by default on Windows or macOS). `Theme::apply()` loads them and
makes them the application font, at the system's default size. Text in other
scripts (CJK, ...) falls back to the system's fonts.

| Files | Source | Licence |
|---|---|---|
| `NotoSans-Regular.ttf`, `NotoSans-SemiBold.ttf`, `NotoSans-Bold.ttf` | Noto Sans 2.015, hinted TTF, from [notofonts/notofonts.github.io](https://github.com/notofonts/notofonts.github.io) at commit `1daf901bd023a3811498e0b7e7284694ae06307e` (`fonts/NotoSans/hinted/ttf/`) | SIL Open Font License 1.1, see `LICENSE-noto-sans.txt` |
| `NotoSansThai-Regular.ttf`, `NotoSansThai-SemiBold.ttf`, `NotoSansThai-Bold.ttf` | Noto Sans Thai, hinted TTF, same repository and commit (`fonts/NotoSansThai/hinted/ttf/`) | SIL Open Font License 1.1, see `LICENSE-noto-sans-thai.txt` |

SHA-256:

```
478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823  NotoSans-Regular.ttf
a4e91fd530ac2b4ef5367240144ff37d7d65d66cf76f2e9a2187b93c676f92d0  NotoSans-SemiBold.ttf
1df075a380fc7cb898acf64c1f7b3b4dd780de3caa860178bf929de35817a913  NotoSans-Bold.ttf
61cf814eec46b294d6ea4401ac295d0cecd5207bd2331dcc5a15e7301d30ee44  NotoSansThai-Regular.ttf
d19ec702c2ddc26f74ccf934342147afd8bce43eb1435fa46a06ab93e368011b  NotoSansThai-SemiBold.ttf
2ac6c6e8a478e23b15f76e4894af1fa2210f8f350e4e6e54aad530bec03efbfb  NotoSansThai-Bold.ttf
```

Weights in use: regular, 600 (semi-bold) and 700 (bold), as the style sheet
asks for. Add another weight's file here and to `src/CMakeLists.txt` before
using it, or Qt synthesises it.
