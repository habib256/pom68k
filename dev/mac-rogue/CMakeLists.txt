cmake_minimum_required(VERSION 3.5)
project(MacRogue C)

# add_application() comes from Retro68's toolchain file
# (retro68.toolchain.cmake). It produces the Macintosh artefacts -- .APPL,
# .bin (MacBinary) and .dsk (a mountable disk image) -- from the linked
# code+data binary.
add_application(Rogue
    mac_shell.c         # window, menus, event pump, CopyBits
    snd.c               # Sound Manager square-wave SFX engine
    vdp.c               # software TMS9918 Graphics-I compositor
    rogue_gfx.c         # GENERATED -- tools/extract_rogue_assets.py
    rogue_map.c         # map buffer, tiles, collision, PRNG
    rogue_gen.c         # dungeon generation + level population
    rogue_fov.c         # recursive shadowcasting
    rogue_mon.c         # monster AI, combat, XP
    rogue_item.c        # items, bag, buffs, throwing
    rogue_draw.c        # playfield, sprites, HUD
    rogue_msg.c         # message line + history ring
    rogue_ui.c          # title, briefing, help, bag modal, endings
    rogue_main.c        # start, turn loop, level transitions
)

# -Os matters here: the oldest target is a 7.8 MHz 68000 with a full-screen
# repaint per keypress.
target_compile_options(Rogue PRIVATE -Os -Wall)
