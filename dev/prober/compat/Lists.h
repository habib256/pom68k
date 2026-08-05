#pragma once
/*
 * compat/Lists.h — le multiversal Retro68 génère le List Manager dans
 * Multiverse.h mais n'émet pas l'alias <Lists.h> (liste codée en dur dans
 * cincludes.rb). ListBounds n'y est pas non plus (Universal Interfaces :
 * typedef Rect ListBounds).
 */
#include <Multiverse.h>

typedef Rect ListBounds;
