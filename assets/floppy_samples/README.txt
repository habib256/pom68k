POM68K — Drive mechanical sound samples
=======================================

Two sample sets from the MAME project drive POM68K's FloppySound device
(a port of MAME's src/devices/imagedev/floppy.cpp::floppy_sound_device,
via POM2's FloppySoundDevice):

  35_*.wav   — 3.5" drives: voices the Sony MFD-51W / MFD-75W
               (Plus / Mac II / LC II / Quadra internal drives)
  525_*.wav  — 5.25" mechanism: used at low gain as the SCSI hard-disk
               seek/spin proxy (no HDD sample set exists in MAME)

Source
------
https://github.com/mamedev/mame/tree/master/samples/floppy
(copied 2026-07-23 from POM2's roms/floppy_samples, itself fetched
2026-05-14 from master)

License
-------
MAME samples are BSD-3-Clause, redistributable with attribution:
Copyright (c) MAME development team and contributors.

Files (per form factor — prefix `35_` or `525_`)
------------------------------------------------
{prefix}_seek_2ms.wav           fast seek loop (2 ms/track cadence)
{prefix}_seek_6ms.wav           seek loop, 6 ms/track
{prefix}_seek_12ms.wav          seek loop, 12 ms/track
{prefix}_seek_20ms.wav          slow seek loop, 20 ms/track
{prefix}_step_1_1.wav           single head step (also insert/eject click)
{prefix}_spin_start_empty.wav   spin-up, no disk
{prefix}_spin_start_loaded.wav  spin-up, disk loaded
{prefix}_spin_empty.wav         spin loop, no disk
{prefix}_spin_loaded.wav        spin loop, disk loaded
{prefix}_spin_end.wav           spin-down
