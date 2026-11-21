# Order of the Flame credits

The PC credits clear to black. They scroll the Surreal Software logo from texture records 8807 and 8808 before text
record 8804, then the Psygnosis logo from texture record 8806 before text record 8805. All five records are in the PC
`Dragon.rrc`. The PC interface's Credits Music ID is 7. The converter uses these records from the local install and
formats the original text for the PAL credits screen.

The PAL credits screen copies each block from the cached archive before splitting it into lines. The campaign override
fills that private copy, so switching games does not leave campaign text in the archive cache. The credits texture
lookups use separate IDs for the campaign artwork and use the disc's own IDs when The Ancients' Gates is selected.

In a private fixture, `OOTF_TEST_ENDING=8` sent the four final boss death messages after loading Rift World. The normal
counter, timer and mirror-room scene led into screen 19. The roll showed both original logos against black, and the
audio service streamed track 7 from `PS2MUSIC.SDB`. The released pack was converted without the fixture timers.
