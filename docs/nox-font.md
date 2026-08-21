# Nox Archaist interface fonts

NAC defaults to the game's original first/runic font, `FONT1`. The exact
768-byte character data is stored in
`NoxArchaistCompanion/NoxData/NoxFont1.bin`: 96 consecutive 7x8 glyphs for
codes `$20-$7F`, one byte per scanline. `src/NoxFont.cpp` doubles each source
pixel to preserve the game's 14x16 on-screen character-cell geometry.

`View -> Interface Font` switches live between:

- **Nox Archaist FONT1** - the original first/runic face and the default.
- **a2sharp** - the previously integrated DirectXTK bitmap face.

Both fonts use Dear ImGui's embedded vector font only for characters outside
their printable-ASCII ranges. The selected face is persisted as
`interface_font` in `nac.json`.

The memory viewer keeps a small ProggyForever instance because its hex and
ASCII tables require dense fixed-width columns.

`FONT1` was extracted from `FONTS/FONT1` on `na.graphics.dsk` with:

```powershell
python tools\extract_nox_font1.py `
  na.graphics.dsk `
  NoxArchaistCompanion\NoxData\NoxFont1.bin
```

To inspect or export a bundled DirectXTK font:

```powershell
python tools\extract_spritefont.py `
  NoxArchaistCompanion\NoxData\a2sharp.spritefont `
  extracted\a2sharp
```

The command writes the raw atlas as PNG, a rendered preview, and glyph metrics
as JSON.
