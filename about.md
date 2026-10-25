# AFWC - Advanced Frame Window Counter

Shows **how many frames (ticks) of room every click of a run has**, and measures it by re-playing the level.

## Two things it does

**High-accuracy frame analysis**
- Open the **AFWC menu** from the pause menu or the main menu (AFWC button) or press **O** (desktop, while paused).
- **Scan** takes the recorded attempt, moves each click earlier and later one tick at a time with every other input left
  exactly as it was, and stops at the first tick that fails. Window = ticks that still work.
- The re-simulation is compared with how your real attempt ended. If it does not match, the panel says so instead of guessing.

**Frame window counter**
- A coloured ring with the frame window appears on the player at every click, plus counters per label (1F, 2F, 3F ...)
  and an info line at the bottom.

## How to use
1. Play a level (AFWC records every attempt).
2. Pause, open AFWC, press **Use attempt** (or let a completed run save by itself).
3. Press **Scan** and let it finish. Then restart the level to see the markers.

The menu also has quick switches for the HUD, markers and scanner. Settings let you change the search radius, how long a shifted click must survive, shift mode (whole tap or press only),
marker size and the HUD.
