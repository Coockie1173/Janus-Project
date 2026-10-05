# TODO

Collected from playtester feedback. Unchecked = open, checked = already in the port.
"needs checking" = the reporter wasn't sure it is a bug.

Importance:

- **critical**: the game can't be finished
- **high**: stops progress until the fps is lowered, or gets in the way all the time
- **medium**: clearly wrong, but playable
- **low**: polish, rare, or not sure it's a bug

## Do first

- [ ] **critical** Final boss: the 3rd QTE takes no button
- [ ] **high** Vents at high fps (Dam, Facility, Nightclub, Bunker, Archives, Outpost), most likely one cause
- [ ] **high** Button bashes can't be won at high fps
- [ ] **high** Carrier ladders at high fps
- [ ] **high** The camera is locked while aiming in
- [ ] **high** Doors break when the intro skip is used
- [ ] **high** Tank: the cars are broken
- [ ] **high** Flickering issue
- [ ] **high** Animated textures break above 60 fps

## High FPS

Things that only break above 60 fps.

### Vents

- [ ] **high** Dam: the vent right after the boats dock can't be entered at 90 fps, however it is opened
- [ ] **high** Facility labs vent: needs 75 or lower after the animation, 60 or lower if shot/hit
- [ ] **high** Nightclub: side vent into the final area (the one going upstairs) breaks above 75 fps
- [ ] **high** Nightclub: vent into the objective room breaks above 60 fps
- [ ] **high** Bunker: vent in the first area has the same problems
- [ ] **high** Archives: vent above the server room needs low fps
- [ ] **high** Outpost: you get stuck in the vents, they're slightly too small, pushing through only works at low fps

### Movement

- [ ] **high** Carrier ladders get progressively harder to climb, need sprinting from 75 or 90 fps
- [ ] **medium** Harder to walk onto slight vertical geometry changes (happens a lot in Outpost and Bunker)

### Doors

- [ ] **medium** Nightclub doors rest in the wrong position
- [ ] **medium** Archives doors stay ajar

### Animation and timing

- [ ] **high** Animated textures break above 60 fps (fire, the Nightclub screens)
- [ ] **low** Bond's dancing animation is too fast
- [ ] **low** Camera shake in the Carrier ending is too fast
- [ ] **low** Breaches seem faster at higher fps (needs checking)

### Quick time events

- [ ] **high** Button bashes can't be won at high fps (a press is worth less the higher the frame rate);
      testers pause and drop to 30. Auto Quick Time Events gets past them but is off by default
- [x] Auto Quick Time Events option (only checked on Dam's button bash)

## Quick time events

- [ ] **critical** Final boss: the 3rd QTE (Trevelyan points a gun at you on the last platform) takes no
      button, you die. Two testers hit it, the game can't be finished
- [x] Prompts show the keys to press (seen on Dam's button bash, single presses come the same way)
- [ ] **medium** Auto Quick Time Events on single presses and sequences (not checked yet)

## Graphics

- [ ] **high** Flickering issue
- [ ] **medium** Bloom still looks incorrect
- [ ] **medium** Facility: the colour filter / post processing "bubble" around the player doesn't cover
      the whole screen, worse at higher FOV
- [ ] **medium** Black bars at the top and bottom in windowed mode
- [ ] **medium** Dam: the opening scene isn't as foggy as it should be
- [ ] **low** Halo around characters' hair (needs checking, may be the original game or the resolution)
- [ ] **low** Dam: the sniper's lower body is visible below the window through the thermal scope (needs checking)
- [ ] **low** Other missing or different effects compared to the console (go through the levels next to Dolphin)

## Audio

- [ ] **medium** Double audio on reloads
- [ ] **medium** Main menu cursor sound is sometimes a loud click
- [ ] **medium** Nightclub: the score keeps playing over the breach music
- [ ] **low** Nightclub: occlusion of the song and crowd transitions harshly around Zukovsky's office (needs checking)

## Input and controls

The end goal: keys are bound to actions the player can make, not to controller buttons that
do several things.

- [x] Sprint on its own key (Left Shift), no longer picks things up
- [x] 1 / 2 / 3 take out the weapon in that slot
- [x] E is the action button
- [x] Get rid of all the A prompts, show the bound key instead of a controller button (texts, hud prompts,
      QTEs, menus; a tutorial box that waits for an action still shows the button, see docs/STATUS.md)
- [ ] **medium** One key, one action for the rest (melee on F by default, Enter and E prompts that agree)
- [ ] **medium** Phone works with mouse 1
- [ ] **medium** Space stands up from a crouch and vaults while holding forward: a jump button in every
      way except the jump
  - check for other actions that shouldn't count as vaulting

## Aiming down sights

- [x] Toggle ADS option (it's "Lock Aim" in F1 > Control Settings)
- [x] ADS sensitivity scale ("Aim Sensitivity", 0.5 by default)
- [x] Option for how far ADS zooms ("Aim Field of View")
- [ ] **high** The camera is locked while aiming in, until about half a second after the gun is up
      (not on the Airfield motorbike)
- [ ] **medium** Airfield motorbike ignores Aim Field of View, zooms in too much
- [ ] **medium** ACOG and reflex sights ignore Aim Field of View
- [ ] **low** Rename "Lock Aim" to "Toggle ADS" (reads like ADS Snap now; "Toggle Aim" also works)
- [ ] **low** ADS snap off by default with mouse and keyboard
- [ ] **low** Maybe show Aim Field of View as a fraction of the FOV (0.75 = 75%) like the sensitivity scale, it's an angle now

## Cutscenes

- [ ] **high** Doors break when the intro skip is used
- [ ] **medium** Cutscenes aren't sped through when entering from another level, in time trial mode, or from Continue Game
- [ ] **medium** Outpost: crouch and melee the guard in front of the log, the camera stays near the ground
      for the whole takedown and you only hear it (there's a VOD, a clip on request)

## Gameplay and AI

- [ ] **high** Tank: the cars are broken
- [ ] **medium** Allies can be shot through (or killed, for the Carrier hostages)
- [ ] **medium** Interactable things show a red cursor, NPC models don't
- [ ] **low** Facility: a guard in the Atrium got stuck once
- [ ] **low** Enemies seem a bit more likely to throw grenades (needs checking)

## Performance

- [ ] **medium** Choppy at times, not a steady 144; the Memorial sewers are the one place that is really
      smooth. A later message says the newest build feels much better (Airfield), so measure
      frame pacing again before chasing it
