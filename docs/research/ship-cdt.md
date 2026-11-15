# Shipping artem-ogre/CDT with the adaptive vertex mode

Research for [#25](https://github.com/spippoli/hipsterismo/issues/25) "How adaptive ships the CDT library", part of map [#6](https://github.com/spippoli/hipsterismo/issues/6). This note collects facts for the grilling ticket. It does not decide anything.

**Tags.**
- **[src]:** the cited primary source says so.
- **[measured]:** checked on this machine.
- **[derived]:** my own reading.

**Sources.**
- **CDT checkout:** CDT at tag `2.0.2` (commit `b4413ee`), checked out at the tag.
- **MPL-2.0:** the text is CDT's `LICENSE`. It is identical to `CDT/LICENSE`.
- **openFrameworks 0.12.1:** the makefiles under `libs/openFrameworksCompiled/project/makefileCommon/`.
- **This repo:** `tests/config.make`.

## TL;DR

1. **Vendoring the unmodified headers is allowed, and the app's own licence stays free.** These conditions apply:
   - Keep the per-file MPL notices [MPL §3.4].
   - Ship or point to the MPL text [§3.1].
   - When you distribute a binary, tell recipients where to get CDT's source [§3.2(a)]. A notice that names the upstream URL and tag is enough.

   A Larger Work can be under any terms [§3.3]. CDT carries no "Incompatible With Secondary Licenses" notice, so a later GPL-2.0+/LGPL/AGPL choice is also compatible [§1.5, §1.12, §3.3]. MIT and proprietary are fine too, as long as the CDT files themselves stay MPL [§3.3]. [src; derived]
2. **Modifying a CDT file keeps that file under MPL.** A modified file is a "Modification" [§1.10] and must be distributed in source under MPL [§3.1]. The obligation is per file: our own files are untouched. If we ever patch CDT, the change stays visible and MPL, unlike the ofxDelaunay patch. [src; derived]
3. **Two headers carry other permissive licences that must travel with them.**
   - `predicates.h` is BSD-3, © 2019 William C. Lenthe. Its clause 2 requires the copyright notice in the documentation of binary distributions.
   - `portable_nth_element.hpp` is Apache-2.0 WITH LLVM-exception, copied from libc++. The LLVM exception waives §4(a)(b)(d) for portions embedded in object code.
   - CDT ships both licence texts as `CDT/LICENSE-predicates.txt` and `CDT/LICENSE-LLVM.txt`.

   [src: file headers, licence files]
4. **Header-only use needs 11 headers** (296 KB, 7 842 lines), no Boost, and C++11 or later. They build cleanly with this machine's g++ under OF's flags, in both C++17 and C++20, including across two translation units. [src; measured]
5. **Recommended option: vendor `CDT/include/*` unmodified into a subfolder of `src/mosaic/` (e.g. `src/mosaic/CDT/`)**, together with the three licence files and a short notice naming upstream, tag and commit.
   - It needs no change to the build. OF's makefiles add every subfolder of `src/` and of `PROJECT_EXTERNAL_SOURCE_PATHS` as a source and include path. So the app and the tests both pick it up, and the Mosaic module stays self-contained, as `CLAUDE.md` requires.
   - The other options all push a step onto every clone or every IDE project:
     - a local addon;
     - cloning it like the other addons;
     - a Makefile fetch.

   [src: `config.project.mk`; derived]

## 1. MPL-2.0 obligations

| Situation | What MPL-2.0 requires | Clause |
|---|---|---|
| Vendoring the source unmodified | Distribution in source form stays under MPL. Tell recipients it is MPL and how to get the licence. Don't restrict their rights. | §3.1 |
| Per-file notices | Don't remove or alter licence, copyright or warranty notices. The Exhibit A notice can instead sit "in a location (such as a LICENSE file in a relevant directory) where a recipient would be likely to look". | §3.4, Exhibit A |
| Binary distribution (the app built with CDT inside) | Make CDT's source available, "and You must inform recipients of the Executable Form how they can obtain a copy". The binary itself can be under other terms, as long as they don't limit rights in CDT's source. | §3.2(a), (b) |
| The app as a whole | "You may create and distribute a Larger Work under terms of Your choice", provided the MPL requirements for the Covered Software are met. | §3.3 |
| GPL family later | Secondary Licenses are GPL-2.0, LGPL-2.1, AGPL-3.0 "or any later versions". CDT is not "Incompatible With Secondary Licenses": no file carries the Exhibit B notice (checked with grep). So in a Larger Work combined with GPL code, CDT may additionally be distributed under the GPL. | §1.5, §1.12, §3.3 |
| Modified CDT files | Any change to a CDT file, or a new file containing CDT code, is a Modification. It stays MPL and must be available in source. | §1.10, §3.1 |

**Per-file notices in 2.0.2.** 8 of the 11 headers start with the MPL Exhibit A notice:
- `CDT.h`, `CDT.hpp`, `CDTUtils.h`, `CDTUtils.hpp`, `KDTree.h`, `LocatorKDTree.h`, `Triangulation.h`, `Triangulation.hpp`.

The other three:
- `predicates.h` carries the full BSD-3 text.
- `portable_nth_element.hpp` carries the SPDX line `Apache-2.0 WITH LLVM-exception`.
- `remove_at.hpp` has no notice in the file. It is covered by the repository's MPL `LICENSE`.

[src: file heads, checked one by one]

**The repo has no LICENSE file yet.** None of the clauses above depends on the app having one. If the app is ever distributed as a binary, it will need a third-party notice anyway, listing:
- CDT (MPL-2.0 and source URL);
- the Lenthe BSD-3 notice;
- the LLVM notice;
- ofxDelaunay, whose licence situation was covered in #17.

[derived]

## 2. CDT 2.0.2 packaging

- **Headers for header-only use.** All 11 files in `CDT/include/` are needed:
  - `CDT.h`, `CDT.hpp`, `CDTUtils.h`, `CDTUtils.hpp`;
  - `KDTree.h`, `LocatorKDTree.h`;
  - `Triangulation.h`, `Triangulation.hpp`;
  - `portable_nth_element.hpp`, `predicates.h`, `remove_at.hpp`.

  The internal includes reach each of them from `CDT.h`. `CDT/src/CDT.cpp` and the generated `cdt_export.h` are needed only with `CDT_USE_AS_COMPILED_LIBRARY`. Leave them out: OF would compile any `.cpp` under `src/`. [src: `#include` lines; `CDTUtils.h` lines 36–48]
- **Configuration macros.** Header-only is the default: without `CDT_USE_AS_COMPILED_LIBRARY`, functions are `inline`. Boost is only used when the macros below are set, or below C++11:
  - `CDT_USE_BOOST`;
  - `CDT_USE_STRONG_TYPING`.

  `CDT_USE_64_BIT_INDEX_TYPE` is optional; 32-bit indices are plenty for 320×240. [src: `CDTUtils.h` lines 14–110, 237]
- **Floating point.** CDT wraps its exact predicates in `CDT_ENSURE_PRECISE_MATH`, which uses per-compiler pragmas:
  - GCC: `no-fast-math`, `fp-contract=off`;
  - Clang: `float_control(precise)`;
  - MSVC: `float_control`, `fp_contract(off)`.

  So even fast-math flags would not break robustness. OF 0.12's Linux makefiles set no `-ffast-math` (checked with grep). [src: `CDTUtils.h` lines 120–148; derived]
- **Build check on this machine** (g++ 15.2.0, Ubuntu). Probe sources are in [`ship-cdt-probe/`](ship-cdt-probe/): two translation units, each including `CDT.h`, triangulating with a constraint edge in `double` and `float`.
  - `-std=c++20 -Wall -Wextra -Werror=return-type -O3`: exit 0, **0 warnings**, correct output (6 triangles), and no ODR or duplicate-symbol errors across the two units.
  - `-std=c++17 -Wall -Wextra -Wpedantic -Werror=return-type -O0`: exit 0, 0 warnings.
  - OF's own Linux flags are `-Wall -Werror=return-type` with `-std=c++17` or `-std=c++20` [src: `config.linux.common.mk` lines 138–177]. There is no clang on this machine, so clang was not tried locally.

  [measured]
- **Other platforms (CDT's CI at 2.0.2).**
  - The `builds` job runs Conan builds, header-only and compiled, with and without Boost, on Windows 2025 (MSVC), Ubuntu 24.04 x64 and arm64 (GCC), and macOS 15 arm64 (Apple Clang).
  - The `standards` job builds with g++ at C++98 (with Boost) through C++26.
  - The `fast-math` job builds with MSVC `/fp:fast /fp:contract`, GCC `-ffast-math` and Clang `-ffast-math`.

  [src: `.github/workflows/ci_builds.yml` at `b4413ee`] I did not check the CI run results, only the workflow definition.

## 3. How an OF project can carry it

Facts about OF 0.12.1's makefiles:
- A project compiles every source file in every directory under `src/`, and every directory is also an `-I` path. [src: `config.project.mk` lines 297–327]
- `PROJECT_EXTERNAL_SOURCE_PATHS` and all its non-hidden subdirectories get the same treatment. [src: lines 303–305]
- The tests project sets `PROJECT_EXTERNAL_SOURCE_PATHS = ../src/mosaic`. [src: `tests/config.make`]
- `addons.make` entries are first looked up relative to the project (`$(PROJECT_ROOT)/<entry>`), then in `OF_ROOT/addons`. Local addons have been supported "via the commandLine PG and the addons.make file" since OF 0.9.0. [src: `config.addons.mk` `parse_addon`; `CHANGELOG.md` OF 0.9.0]

| Option | Build changes | App | Tests | IDE projects (projectGenerator) | Every clone | Notes |
|---|---|---|---|---|---|---|
| **A. Vendor into `src/mosaic/CDT/`** | none | picked up automatically | picked up via `PROJECT_EXTERNAL_SOURCE_PATHS` subdirs | included as project source | nothing to do | Mosaic module stays self-contained. ~300 KB in the repo. Upgrades are a manual copy. Its headers become global include names (`CDT.h`, `KDTree.h`, `predicates.h`, …); none of them exists in OF's `libs/` or `addons/` (checked with find). |
| B. Local addon in the repo (e.g. `local_addons/ofxCDT/` with `libs/CDT/include/` and an `addon_config.mk`) | an `addons.make` line in the app (`local_addons/ofxCDT`) and in the tests (`../local_addons/ofxCDT`) | yes | yes, with its own line | PG reads `addons.make`, local paths supported | nothing to do | Same licence duties as A. The Mosaic module then depends on something outside `src/mosaic/`. |
| C. Clone upstream like the other addons | `git clone` step in `CLAUDE.md`, plus include paths | upstream's layout is not an addon: the root holds `CDT/src/CDT.cpp`, `tests/`, `visualizer/`, which OF's addon parser would try to compile | same | needs manual include paths | one more manual step | Without a fork or a wrapper, there is nowhere to put an `addon_config.mk`, and the user forbids pushing outside spippoli/hipsterismo. |
| D. Makefile fetches it | custom rule in `config.make`/`Makefile` | Linux/macOS make only | separate rule | IDE projects (Xcode, VS) would not run it | network at build time | Least portable. |

[derived from the facts above]

**Pinning in option A.** Upstream gives no single file for this, so it is a convention. A `src/mosaic/CDT/README.md` should record:
- the upstream URL;
- the tag `2.0.2` and commit `b4413ee`;
- the statement that the files are unmodified;
- the MPL notice location.

Next to it go `LICENSE` (MPL-2.0), `LICENSE-predicates.txt` and `LICENSE-LLVM.txt`. That meets §3.1 and Exhibit A. A `.md` file is not compiled by OF. [derived]

## 4. Open points for the grilling

- Where exactly A puts the files: `src/mosaic/CDT/` or `src/mosaic/vendor/CDT/`.
- Whether a top-level third-party notice (for binaries) belongs in this effort or waits for the repo's LICENSE decision.
- Whether CDT should later replace ofxDelaunay for spline and stable too (noted in #17). That would remove the patched addon but change spline's output, which is out of scope on map #6.
