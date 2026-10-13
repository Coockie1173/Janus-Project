# TODO

Collected from playtester feedback. Unchecked = open, checked = already in the port.
"needs checking" = the reporter wasn't sure it is a bug.

Importance:

- **critical**: the game can't be finished
- **high**: stops progress until the fps is lowered, or gets in the way all the time
- **medium**: clearly wrong, but playable
- **low**: polish, rare, or not sure it's a bug

## Do first

- [x] **high** Vents at high fps (Dam, Facility, Nightclub, Bunker, Archives, Outpost), most likely one cause:
      fixed where it was recorded (a vent's exit at 144)
- [X] **high** Carrier ladders at high fps
- [x] **high** The camera is locked while aiming in
- [x] **high** Doors break when the intro skip is used
- [x] **high** Tank: the cars are broken: not seen any more, played at 144 on 2026-10-11 with every car riding right
- [X] **high** Flickering issue
- [x] **high** Animated textures break above 60 fps

## High FPS

Things that only break above 60 fps.

### Vents

- [x] Getting stuck at a vent's frame: a blocked player moves in the console's steps now (out of the
      recorded vent at 144, see docs/STATUS.md)
- [x] **high** Dam: the vent right after the boats dock can't be entered at 90 fps, however it is opened
- [x] **high** Facility labs vent: needs 75 or lower after the animation, 60 or lower if shot/hit
- [x] **high** Nightclub: side vent into the final area (the one going upstairs) breaks above 75 fps
- [x] **high** Nightclub: vent into the objective room breaks above 60 fps
- [x] **high** Bunker: vent in the first area has the same problems
- [x] **high** Archives: vent above the server room needs low fps
- [x] **high** Outpost: you get stuck in the vents, they're slightly too small, pushing through only works at low fps

### Movement

- [X] **high** Carrier ladders get progressively harder to climb, need sprinting from 75 or 90 fps
- [X] **medium** Harder to walk onto slight vertical geometry changes (happens a lot in Outpost and Bunker)

### Vehicles

- [ ] **medium** Tank: cars still get stuck in the ground now and then at high fps

### Cutscenes

- [x] **high** Nightclub: after the opening the camera stays locked (player state 0x22) at high fps:
      a file that's let go is unloaded after the console's time now, not four frames (seen at 144 and 275)

### Animation and timing

- [x] **high** Animated textures break above 60 fps (fire, the Nightclub screens): they stood still from 64 fps on,
      now they play at the console's speed (measured on a Nightclub flipbook at 144, see docs/STATUS.md)
- [x] **low** Bond's dancing animation is too fast
- [ ] **low** Camera shake in the Carrier ending is too fast
- [x] **low** Breaches seem faster at higher fps (needs checking)
- [x] **medium** Text scrolls at a speed that follows the frame rate (mission intros, checkpoint text, objectives, ...):
      a character a console frame now (Dam's intro 2.8 s at 30 and at 144, see docs/STATUS.md)

### Quick time events

- [x] Button bashes can't be won at high fps (a press was worth less the higher the frame rate): a press
      is worth what it is at 30 now (won by hand on Dam's at 144)
- [x] Auto Quick Time Events option (only checked on Dam's button bash)

## Quick time events

- [x] Final boss: the 3rd QTE (Trevelyan points a gun at you on the last platform) took no button, you
      died: button bashes work at any fps now (seen on Dam's, the boss not played again yet)
- [x] Prompts show the keys to press (seen on Dam's button bash, single presses come the same way)
- [x] **medium** Auto Quick Time Events on single presses and sequences (not checked yet)

## Graphics

- [X] **high** Flickering issue
- [x] **medium** Bloom still looks incorrect
- [x] **medium** Facility: the colour filter / post processing "bubble" around the player doesn't cover
      the whole screen, worse at higher FOV
- [x] **medium** Black bars at the top and bottom in windowed mode: to avoid stretching, it must always be 16:9 or 4:3
- [ ] **medium** Dam: the opening scene isn't as foggy as it should be
- [ ] **medium** Vertigo: the skybox is black in one corner of the map
- [ ] **low** Halo around characters' hair (needs checking, may be the original game or the resolution)
- [ ] **low** Dam: the sniper's lower body is visible below the window through the thermal scope (needs checking)
- [ ] **low** Other missing or different effects compared to the console (go through the levels next to Dolphin)

## Audio

- [x] **critical** crunchy audio at higher fps: a new sound played 3 ms at full volume (to be confirmed by ear)
- [ ] **medium** Online: a static noise plays when another player fires the Golden Gun
- [ ] **medium** Multiplayer: a player above you should have the low frequencies cut, not the high ones
- [x] **medium** Double audio on reloads: a reload's sounds played up to five times over at 144, once now
      (counted on the P99 at 30, 60 and 144, to be confirmed by ear)
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

### Tutorial texts

Texts that still talk about the controller.

- [x] **medium** Training: "Disable Tutorials" also hides the text boxes, the mission can't be played without them:
      the option leaves the training mission alone now
- [ ] **low** Training: the text boxes still say to move around with the analog sticks
- [ ] **low** Training: "Press [Right] to draw your P99."
- [ ] **low** Training: "You can adjust your aim with the R Stick" (probably more, the reporter didn't see them all)
- [ ] **low** Dam: "Press E while holding forward on the L Stick to climb small ledges", while the prompt on
      screen says "[L Shift] + [W]" (at the start, under the two bridges before the first building)
- [ ] **low** Dam: "Press [Down] To take out your Smartphone" should say [F] (both work, may not be possible)

## Aiming down sights

- [x] Toggle ADS option (F1 > Control Settings)
- [x] ADS sensitivity scale ("Aim Sensitivity", 0.5 by default)
- [x] Option for how far ADS zooms ("Aim Field of View")
- [x] **high** The camera is locked while aiming in, until about half a second after the gun is up
      (not on the Airfield motorbike)
- [x] **medium** Airfield motorbike ignores Aim Field of View, zooms in too much
- [x] **medium** Airfield motorbike ignores Aim Sensitivity
- [x] **medium** Airfield: Toggle ADS doesn't work on the motorbike
- [x] **medium** The mouse is still locked for a moment when first aiming in, but only when standing still:
      the game's aim snap held the view until a stick was pushed, a moving mouse lets go of it now
- [x] **medium** Dam: Aim Sensitivity and Aim Field of View do nothing on the truck
- [x] **medium** Aim Sensitivity and Aim Field of View do nothing during breaches
- [x] **medium** ACOG and reflex sights ignore Aim Field of View
- [x] **low** Rename "Lock Aim" to "Toggle ADS"
- [ ] **low** ADS snap off by default with mouse and keyboard
- [ ] **low** Maybe show Aim Field of View as a fraction of the FOV (0.75 = 75%) like the sensitivity scale, it's an angle now

## Cutscenes

- [x] **high** Doors break when the intro skip is used
- [x] **medium** Cutscenes aren't sped through when entering from another level, in time trial mode, or from Continue Game
- [ ] **medium** Outpost: crouch and melee the guard in front of the log, the camera stays near the ground
      for the whole takedown and you only hear it (there's a VOD, a clip on request)
- [ ] **low** The game can be paused while a cutscene is being skipped

## Gameplay and AI

- [x] **high** Tank: the cars are broken: not seen any more, played at 144 on 2026-10-11 with every car riding right
- [ ] **medium** Allies can be shot through (or killed, for the Carrier hostages)
- [ ] **medium** Interactable things show a red cursor, NPC models don't
- [ ] **low** Facility: a guard in the Atrium got stuck once
- [ ] **low** Enemies seem a bit more likely to throw grenades (needs checking)
- [x] **high** ADS broken on online
- [x] **low** "Cheats Active" shows when only the Extras menu is enabled, which isn't much of a cheat

## Performance

- [ ] **medium** Choppy at times, not a steady 144; the Memorial sewers are the one place that is really
      smooth. A later message says the newest build feels much better (Airfield), so measure
      frame pacing again before chasing it

## Feature requests

- [x] Options to remap the tank controls: F1 > Control Settings has the machine gun, the cannon and the
      lock on, each on the button of an action on foot (pressed by script, not played by hand)
- [x] A menu option to load any map, the unused ones too: F1 > Level Select and F1 > Multiplayer Maps are
      grids (from the main menu only), Extras has the same as lists (two test levels don't load, see docs/STATUS.md)
- [x] Options for anisotropic filtering and anti-aliasing: F1 > Screen Settings, up to 16x and 8x MSAA
- [x] An option to halve screen shake or turn it off: F1 > Screen Settings > Screen Shake, a slider from off to 5x (measured on the
      sway, explosions not seen)
- [x] An option to turn the HUD off: F1 > Screen Settings > HUD
- [x] The cursor is free in the menus and picks their items (the game's own menu pointer on the mouse;
      checked with a cursor for tests, not with a real mouse)
- [ ] A speedrun timer for the whole game: resets to 0 when Dam starts, ends when input is lost at the
      end of Cradle after killing Trevelyan, adds up the in-game times so loading screens don't count
- [ ] A film grain slider
- [ ] An option to turn bloom off

## Cheats

- [ ] Turbo mode
- [ ] Spawn any weapon
- [ ] Infinite ammo
- [ ] Grenades in single player
- [ ] Infinite sprint
- [ ] God mode
- [ ] Enemy rockets
- [x] 007 mode: Select Difficulty > 007 Custom, sliders for enemy health, enemy damage, enemy reaction speed
      and hit protection, then the mission as 007 Classic (the menu and the melee value checked, see docs/STATUS.md);
      the sliders take the mouse, and a switch turns instant kill headshots off (the switch not seen in a fight yet)
- [ ] 007 mode: a slider for enemy accuracy (the game has no value for it by difficulty)
- [ ] Invisible enemies
- [ ] Invisible player (enemies never see you)
- [ ] Fly mode (noclip that doesn't go through walls)
