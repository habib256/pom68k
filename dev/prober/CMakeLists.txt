cmake_minimum_required(VERSION 3.5)
project(POM68KProber C ASM)

# add_application() (fourni par retro68.toolchain.cmake) produit .APPL/.bin/.dsk.
# probe.s = handler bus/address-error assemble par l'assembleur m68k de Retro68.
add_application(POM68KProber
    main.c
    report.c
    ident.c
    net.c
    interp.c
    bench.c
    gfx.c
    power.c
    ui.c
    chart.c
    devs.c
    probe.s
    compat/appletalk_glue.c
)

# compat/ : alias Lists.h + API AppleTalk absents du jeu multiversal.
target_include_directories(POM68KProber PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/compat)
