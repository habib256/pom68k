cmake_minimum_required(VERSION 3.5)
project(BonjourPommeOne C)

# add_application() est fourni par le fichier de toolchain Retro68
# (retro68.toolchain.cmake). Il produit les artefacts Mac : .APPL / .bin
# (MacBinary) / .dsk (image disque montable) a partir du binaire code+data.
add_application(BonjourPommeOne
    main.c
)
