/*
 * devs.h — peripheriques : ADB, stockage, son.
 *
 * ── Pourquoi ces trois-la ensemble ──────────────────────────────────────
 * Ce sont les trois sous-systemes qu'un rapport d'identite ordinaire ne
 * fait pas apparaitre du tout, et que les deux publics veulent voir :
 *
 *   ADB       "quel clavier, quelle souris" — et cote POM68K, c'est la
 *             validation VUE DU GUEST du modele de peripherique `AdbLine`
 *             (identifiants de gestionnaire, adresse d'origine, SRQ) que
 *             rien n'observait jusqu'ici depuis l'interieur.
 *   Stockage  "quel lecteur, quel volume, protege en ecriture ?" — la file
 *             des lecteurs traverse SCSI, SWIM/IWM et le type de media.
 *   Son       les capacites annoncees (stereo, 16 bits, cadences). Le test
 *             AUDIBLE est deliberement un choix de menu, jamais automatique :
 *             un diagnostic qui fait du bruit sans prevenir est un mauvais
 *             diagnostic.
 *
 * Comme partout : valeurs BRUTES ici, lecture humaine dans interp.c.
 */
#ifndef PROBER_DEVS_H
#define PROBER_DEVS_H

#include "report.h"

/* Sections "adb", "drive", "volume" et "sound". Chaque collecte dit
 * explicitement quand elle ne trouve rien — une section muette ne se
 * distingue pas d'un test qui n'a pas tourne. */
void Devs_Collect(Report *r);

/* Tonalite de test, sur action explicite de l'utilisateur. */
void Devs_PlayTestTone(void);

#endif /* PROBER_DEVS_H */
