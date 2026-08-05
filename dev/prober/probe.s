| probe.s — handler Bus Error ($08) + Address Error ($0C) pour le sondage
| de topologie du POM68K Prober. Syntaxe GNU as m68k (Retro68). Voir SPEC.md §7.
|
| Approche FRAME-AGNOSTIQUE : on ne fait PAS de RTE (qui, selon le format de
| frame 68000/010/020/030/040, ré-exécuterait la faute ou reprendrait le cycle
| bus). Le handler pose gProbeFaulted, bascule sur une pile scratch privée, puis
| appelle longjmp(gProbeEnv, 1) — qui restaure D2-D7/A2-A7/PC (et le SR si le
| setjmp du libc le sauve) posés par le setjmp dans ProbeReadable (ident.c).
| Le frame d'exception abandonné sur la pile superviseur est inoffensif : le
| prober est éphémère et ProbeRestore rétablit les vecteurs juste après.
|
| Hypothèses : VBR = 0 (vecteurs à $0, convention Mac classique) ; low-mem
| inscriptible en mode user (vrai sur Mac classique). À revérifier sur 68040.
|
| Symboles C référencés (ELF Retro68, pas de préfixe '_') :
|   gProbeEnv     : jmp_buf (défini dans ident.c)
|   gProbeFaulted : volatile long (défini dans ident.c)
|   longjmp       : libc

        .global ProbeInstall
        .global ProbeRestore
        .global ProbeHandler

        .text
        .even

| void ProbeInstall(void) — sauve puis remplace les vecteurs $08/$0C.
ProbeInstall:
        move.l  0x8, gOldBus            | sauvegarde Bus Error
        move.l  0xC, gOldAddr           | sauvegarde Address Error
        move.l  #ProbeHandler, 0x8      | installe le nôtre
        move.l  #ProbeHandler, 0xC
        rts

| void ProbeRestore(void) — restaure les vecteurs d'origine.
ProbeRestore:
        move.l  gOldBus,  0x8
        move.l  gOldAddr, 0xC
        rts

| Entrée d'exception (mode superviseur, frame d'exception sur SSP).
ProbeHandler:
        move.l  #1, gProbeFaulted       | signale la faute
        lea     gProbeStack+512, %sp    | pile scratch privée pour l'appel
        move.l  #1, -(%sp)              | longjmp: val (arg le plus à droite)
        pea     gProbeEnv               | longjmp: env (jmp_buf*)
        jsr     longjmp                 | restaure le contexte du setjmp
        | longjmp ne revient jamais ; garde-fou si jamais :
        rts

        .data
        .even
gOldBus:        .long 0
gOldAddr:       .long 0

        .bss
        .even
gProbeStack:    .space 512
