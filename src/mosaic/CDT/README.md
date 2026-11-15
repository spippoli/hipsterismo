# Vendored CDT

Constrained Delaunay triangulation, used by the Mosaic module's adaptive vertex mode.

- Upstream: https://github.com/artem-ogre/CDT
- Tag: `2.0.2`
- Commit: `b4413ee44affb489099ed4d6ec65e94788cb6708` (`b4413ee`)
- Files: the 11 headers of upstream's `CDT/include/`, used header-only
  (`CDT_USE_AS_COMPILED_LIBRARY` is not defined).

## Licences

- `LICENSE`: Mozilla Public License 2.0, for CDT itself.
- `LICENSE-predicates.txt`: BSD 3-Clause, for `predicates.h`.
- `LICENSE-LLVM.txt`: Apache 2.0 with LLVM Exceptions, for `portable_nth_element.hpp`.

## Never edit these files

They are upstream's files, unmodified. A modified file would stay under the
MPL and its source would have to be offered. To update, replace all 11 headers
and the licence files with those of a new upstream tag, and update the tag and
commit above.
