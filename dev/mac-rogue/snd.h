/*
 * snd.h -- sound effects for the Macintosh port of TMS_Rogue.
 * ---------------------------------------------------------------------------
 * One Sound Manager square-wave channel (noteSynth), driven entirely by
 * queued commands: an effect is a tiny score of timbre / amplitude / note /
 * rest steps that plays ASYNCHRONOUSLY while the game runs on. A new effect
 * flushes whatever is still queued -- last event wins, nothing ever blocks.
 *
 * The Apple-1 original was silent; the soundscape here follows the era's
 * grammar instead (Dark Castle, Crystal Quest): short square-wave motifs,
 * bright timbre for good news, buzzy timbre for violence.
 *
 * Degrades to silence -- never to SysBeep spam -- when the Sound Manager is
 * absent (System < 6) or the channel cannot be opened.
 */

#ifndef SND_H
#define SND_H

enum {
    SFX_NONE = 0,
    SFX_TITLE,      /* title screen fanfare                      */
    SFX_STAIRS,     /* descending to the next depth              */
    SFX_HIT,        /* you hit a monster                         */
    SFX_KILL,       /* you kill a monster                        */
    SFX_HURT,       /* a monster hits you                        */
    SFX_HURT_LOW,   /* ...and you are nearly dead (warning tail) */
    SFX_GLANCE,     /* armour ate the whole hit                  */
    SFX_BOSS_HIT,   /* the demon strikes you                     */
    SFX_PICKUP,     /* item or dagger enters the bag             */
    SFX_QUAFF,      /* food or potion heals                      */
    SFX_SCROLL,     /* the map scroll reveals the level          */
    SFX_BUFF,       /* sword / tunic / ring / torch activates    */
    SFX_THROW,      /* dagger leaves the hand                    */
    SFX_PIT,        /* you fall into a pit                       */
    SFX_LEVELUP,    /* an XP threshold lands                     */
    SFX_DEATH,      /* the death screen                          */
    SFX_WIN,        /* the demon is dead                         */
    SFX_COUNT
};

void SfxInit(void);          /* open the channel; safe to call on any System */
void SfxShutdown(void);      /* dispose the channel (quiet immediately)      */
void SfxPlay(short id);      /* fire and forget; flushes the previous effect */
int  SfxEnabled(void);       /* 1 = playing (channel open AND user wants it) */
int  SfxAvailable(void);     /* 1 = the channel opened (menu item enabler)   */
void SfxSetEnabled(int on);  /* menu toggle; 0 also silences immediately     */

#endif /* SND_H */
