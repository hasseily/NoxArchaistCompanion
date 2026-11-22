# Emulator speed

The //e runs at a speed multiplier of the base 1.0205 MHz clock, from 0x
(frozen) to 1000x. The value is persisted as `speed` in `nac.json`.

- **Emulator > Speed** lists the presets (Sloth 0.5x through Demigod), also
  reachable with Cmd/Ctrl+1..7. Demigod is the 1000x maximum.
- **Custom Speed...** opens a floating window with a logarithmic slider and an
  exact-value field. Ctrl+click the slider to type a value; the field's +/-
  buttons step by 0.01x, or by 1x with Ctrl held. It is checked in the menu
  whenever the speed isn't a preset.
- The window also shows the **actual** speed, measured every half second, and
  marks it as this computer's maximum when the host can't keep up.
- Holding **Scroll Lock** warps to the maximum and restores the previous speed
  on release or when the window loses focus.
- At 1000x, including Scroll Lock warp, the Companion mutes emulator audio.
  Slowing down restores the configured speaker and music volumes.

Entry points in `src/main.cpp`: `SetSpeed` (clamps and applies),
`RenderSpeedWindow`, and the pacing block at the top of `SDL_AppIterate`.

## Pacing

Each frame converts elapsed wall time (capped at 100 ms, so stalls aren't
caught up) into owed CPU cycles. Fractions and opcode overshoot carry into the
next frame so very slow speeds stay exact. Cycles run in ~1 ms batches, capped
at one video frame, until either the debt is paid or 10 ms of wall time is
spent; anything left over is dropped. High speeds therefore run as fast as the
host can while the UI stays responsive.

A frame that ends with cycles still owed was cut short by the budget. When
that happens in most frames of a half-second window, the measured speed is the
host's maximum. That maximum reflects only the 10 ms per frame given to
emulation, not the CPU core's full throughput.

`g_fCurrentCLK6502` follows the speed but never drops below the 44.1 kHz
speaker sample rate, because the speaker divides by whole cycles per sample.
Below ~0.043x the speaker simply produces fewer samples than real time.
