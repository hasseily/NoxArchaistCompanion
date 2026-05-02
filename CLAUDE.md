# NoxArchaistCompanion (NAC) — Project Notes

NAC is a desktop companion for the Apple II game Nox Archaist. It embeds a stripped Apple //e emulator (derived from AppleWin) and adds a sidebar with maps, hint sheets, and integration with Grid Cartographer (GC) over shared memory.

## Target platforms
- **Windows** — full features including GC integration
- **Linux** — full features including GC integration (GC has community Linux support)
- **macOS** — emulator + sidebar, but **no Grid Cartographer integration** (GC doesn't run on macOS)

## Architecture (post-SDL3 conversion)
- Window/input/audio/timing: SDL3 (fall back to SDL2 only if SDL3 blocks something)
- Rendering: OpenGL (3.3 core / GLES 3.0 minimum)
- Embedded emulator: stripped AppleWin (Enhanced Apple //e, 65C02, 128 KiB)
- Build: CMake

## Embedded AppleWin scope
**Keep:** Enhanced Apple //e (65C02, 128 KiB RAM), all video output modes, Mockingboard, Smartport, Hard Disk, post-processing shader pipeline (from `pp` branch).
**Drop:** Disk II floppy emulation, all other expansion cards, Windows-only frontends, debugger UI, Qt/libretro/ncurses frontends, every platform other than Windows/Linux/macOS.

## Coding rules (apply to ALL work in this repo)

### Simplicity & no bloat
- Always pick the simplest implementation that works.
- When refactoring, actively remove bloat. The diff should usually be net-negative.
- **No dead code.** If a function, file, branch, or `#ifdef` path isn't reachable in our supported configurations, delete it. Don't comment it out.
- Don't add features, abstractions, or layers that aren't required by the task in front of us. No designing for hypothetical future needs.
- Don't add error handling, validation, or fallbacks for cases that can't happen. Trust internal invariants; only validate at true system boundaries.
- No backwards-compatibility shims for features we control. If we change a thing, change every caller.

### Comments & documentation
- **Default to no comments.** Well-named identifiers explain *what*; only write a comment when the *why* is non-obvious (a hidden constraint, a subtle invariant, a workaround for a specific bug, behaviour that would surprise a reader).
- **Inline comments describe the code as it is now**, not its history. Don't write "previously this used X", "removed Y", "refactored from Z". Git log is the history.
- Historical context is allowed *only* when it explains why something is done in a non-intuitive way today (e.g. "ordering matters here because chip X latches on the falling edge — reversing the writes breaks Mockingboard").
- No multi-paragraph docstrings, no banner comments.

### Per-feature documentation
- After a major feature is implemented and working, write a short Markdown doc covering: what the feature does, the key entry points, any non-obvious decisions. Keep these docs in `docs/` and commit them with the feature.
- Doc files describe current state, not change history. Update or delete them when the feature changes; don't append changelogs.

### Commits
- One logical change per commit. Conventional, imperative subject lines.
- Commit a feature together with its doc.
