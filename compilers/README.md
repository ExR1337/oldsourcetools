# Old Source compilers

Valve's **vbsp**, **vvis** and **vrad** from the Source SDK 2013 (the 2025 Team Fortress 2 SDK code, Valve's version of 9 October 2026), changed so they write compiled maps in the **Old Source format**: maps they compile load in Old Source games without converting.

- `changes/src/...` – the 8 changed files, in the same folders as in Valve's code
- `oldsource_compilers.patch` – the same changes as one patch
- `../.github/workflows/build-compilers.yml` – GitHub builds the compilers on Windows whenever the changes change (or by hand: Actions > Build Old Source compilers > Run workflow); the result is the **OldSourceCompilers** artifact of the run
- `INSTALL.txt` – where the five built files go

## What is different from Valve's compilers

- **vrad**: light for models (leaf ambient lighting) is written as one light cube per leaf, like the Old Source compilers; static props always get vertex lighting, never per-texel lightmaps; it reads static props of version 4, 5, 6 and 10; maps without HDR lighting lose the HDR default cubemaps.
- **vbsp**: static props are written as version 5; default cubemaps as VTF 7.2.
- **All three**: no face ids, displacement collision lump or overlay fades (newer engine only); no `hammerid` / `mapversion` entity keys; the newer "no shadows" face bit and overlay render order are cleared.
- The banner: `Valve Software - vbsp.exe (Oct 22 2026, Old Source format)`.

Everything else is Valve's code unchanged. Valve's code: Copyright (c) Valve Corporation, under the SOURCE 1 SDK LICENSE (`LICENSE`, `thirdpartylegalnotices.txt`).

Changes for the Old Source format: ExR - made by Claude
