# The frame upload: the gate, and the two driver calls it cannot reach

`uploadFrameTexture` (`GuiScreen.h`) is driven by `gui_machine_window_test`
with a fake host — the decision, the geometry, the refusals and the pixels.
What no gate on this machine can execute is `GlTextureHost`'s two lines,
`glBindTexture` + `glTexImage2D`. This is their dated manual pass.

| File | What it is |
|---|---|
| `plus_compact_runner.png` | Macintosh Plus, HD20SC — the **compact** runner, whose source format this change moved from `GL_RGBA` to `GL_BGRA` |
| `lcii_v8_runner.png` | Macintosh LC II, GIST PERSO — the V8 runner, `GL_BGRA` before and after |

Both under Xvfb with `LIBGL_ALWAYS_SOFTWARE=1`, screenshotted 55-60 s after
launch:

```
xvfb-run -a --server-args="-screen 0 1400x1000x24" \
  bash -c 'LIBGL_ALWAYS_SOFTWARE=1 POM68K_APPLETALK=0 ./build/POM68K \
           "roms/128KB ROMs/1986-03 - 4D1F8172 - MacPlus v3.ROM" hdv/HD20SC.vhd & \
           sleep 55; import -window root plus_compact_runner.png; kill -9 %1'
```

The Plus reaches its Finder with HD20SC mounted, desktop pattern, Trash and
menu bar intact — which is the point: a compact publishes only `$FF000000`
and `$FFFFFFFF` (`MacVideo.h`, 1 bpp), so `GL_RGBA` and `GL_BGRA` are the
same four bytes, and the unification changes no pixel. The LC II is the
colour witness, at 512×384 1 bpp on this volume.
