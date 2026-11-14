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
- [x] **high** Settings reset (see Settings)

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
- [x] **low** Camera shake in the Carrier ending is too fast
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
- [x] **medium** Dam: the opening scene isn't as foggy as it should be: the hardware's fog wasn't drawn at all,
      it is now (needs checking on Dam's opening)
- [ ] **medium** Vertigo: the skybox is black in one corner of the map
- [x] ~~**low** Halo around characters' hair (needs checking, may be the original game or the resolution)~~
- [ ] **low** Dam: the sniper's lower body is visible below the window through the thermal scope (needs checking)
- [x] **medium** Colour filter option only works on Facility: on Dam, Airfield, Nightclub (kitchen), Archives,
      Memorial and Solar the filter is always on, whatever the option, and only goes away while hipfiring an
      automatic weapon: those levels tint with an orb round the camera, left out now too (seen on Dam, logged on
      Airfield, Nightclub and 01810146; Archives, Memorial and Solar not reached by script)
- [ ] **low** Tank: the colour filter option changes nothing (the option's text lists Tank, may have no filter at all)
- [x] **low** The colour filter option's text only names Facility, Tank and Solar

## Audio

- [x] **critical** crunchy audio at higher fps: a new sound played 3 ms at full volume (to be confirmed by ear)
- [ ] **medium** Online: a static noise plays when another player fires the Golden Gun
- [x] **medium** Multiplayer: a player above you should have the high frequencies cut, not the low ones
- [x] **medium** Double audio on reloads: a reload's sounds played up to five times over at 144, once now
      (counted on the P99 at 30, 60 and 144, to be confirmed by ear)
- [x] **medium** Main menu cursor sound is sometimes a loud click
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
- [x] **medium** One key, one action for the rest (melee on F by default, Enter and E prompts that agree): melee is on F
      (and V, Mouse X1), the smartphone moved from F to Q; a key file from before is moved once (Enter and E prompts not looked at)
- [x] **medium** Phone works with mouse 1: with the phone in the hand the left button is the action, which takes the
      picture or hacks ("Interact" in the game's texts), held for a hack (seen turning into the action on Dam, no picture taken)
- [ ] **medium** Space stands up from a crouch and vaults while holding forward: a jump button in every
      way except the jump
  - check for other actions that shouldn't count as vaulting

### Tutorial texts

Texts that still talk about the controller.

- [x] **medium** Training: "Disable Tutorials" also hides the text boxes, the mission can't be played without them:
      the option leaves the training mission alone now
- [ ] **low** Training: the text boxes still say to move around with the analog sticks
- [x] ~~**low** Training: "Press [Right] to draw your P99."~~
- [ ] **low** Training: "You can adjust your aim with the R Stick"
- [ ] **low** Dam: "Press E while holding forward on the L Stick to climb small ledges", while the prompt on
      screen says "[L Shift] + [W]" (at the start, under the two bridges before the first building)
- [ ] **low** Dam: "Press [Down] To take out your Smartphone" should say [F] (both work, may not be possible)
- [x] **medium** Disable Tutorials no longer hides the tutorial pop-ups (it did once); the reporter would like them
      fully gone, the training mission too: the training exception is gone, the option hides them everywhere
- [x] **low** The Disable Tutorials text is unclear

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
- [x] **medium** Aimed with the sniper the mouse feels twice as fast: a scope turns the mouse down by
      its zoom now (the pistol unchanged on Dam, a scope not tried by script, see docs/STATUS.md)
- [ ] **low** ADS snap off by default with mouse and keyboard
- [ ] **low** Maybe show Aim Field of View as a fraction of the FOV (0.75 = 75%) like the sensitivity scale, it's an angle now
- [ ] **medium** ADS values still differ between single player and multiplayer, at least on the P99 (reported before)

## Cutscenes

- [x] **high** Doors break when the intro skip is used
- [x] **medium** Cutscenes aren't sped through when entering from another level, in time trial mode, or from Continue Game
- [x] **low** The game can be paused while a cutscene is being skipped: + and Esc wait until the skip is over
      (pressed every half second through Dam's opening skip)
- [x] **medium** Cradle: the opening skip goes slightly too far, leave a little more time before the slow motion part
      (fades back in 2 s earlier now, to be played)
- [ ] **low** An option to turn off the fade out when skipping a cutscene, to follow where it goes
- [ ] ~~**low** Cradle: the Natalya cutscene in the first area could be skipped~~
- [x] **low** Cradle: the last Trevelyan encounter could be skipped up to "For England, James?" (leaves time to get
      ready to shoot and keeps that line)
- [x] **low** Solar: the cutscene going up the stairs, first looking at the big tower, could be skipped
- [x] **low** With Auto Quick Time Events on, skip QTE cutscenes too (Cradle: the first Trevelyan encounter): Cradle's
      fight with Trevelyan up to the shot and Jungle's fight with Xenia (Cradle checked by hand, Jungle not played yet)

## Gameplay and AI

- [x] **high** Tank: the cars are broken: not seen any more, played at 144 on 2026-10-11 with every car riding right
- [ ] **medium** Allies can be shot through (or killed, for the Carrier hostages)
- [ ] **medium** Interactable things show a red cursor, NPC models don't
- [ ] **low** Facility: a guard in the Atrium got stuck once
- [ ] **low** Enemies seem a bit more likely to throw grenades (needs checking)
- [x] **high** ADS broken on online
- [x] **low** "Cheats Active" shows when only the Extras menu is enabled, which isn't much of a cheat

## Settings

- [x] **high** Settings reset now and then: anisotropic filtering, anti-aliasing and screen shake often; infinite
      ammo and infinite sprint sometimes, seemingly per map (maybe only on test maps, not every campaign level tried):
      the launcher's window wrote back only its own settings, and the cheats were never saved (a restart turned
      them off); both kept now (see docs/STATUS.md)

## Performance

- [ ] **medium** Choppy at times, not a steady 144; the Memorial sewers are the one place that is really
      smooth. A later message says the newest build feels much better (Airfield), so measure
      frame pacing again before chasing it
- [x] **medium** Anisotropic filtering costs far too much (the biggest hit the reporter has seen, at only 4x):
      nothing measurable at 16x on an RTX 4070 (Dam, 1440p); screen copies (bloom, colour filter, depth of
      field) don't get it any more, which is where a weaker GPU paid for it. The MSAA stutter fixed on
      2026-11-10 may have been what was felt (needs checking by the reporter)
- [ ] ~~**medium** Pauses at 0 fps now and then, more than in other games on the same machine (shader
      compiles? check the shader cache)~~

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
- [x] A speedrun timer for the whole game: resets to 0 when Dam starts, ends when input is lost at the
      end of Cradle after killing Trevelyan, adds up the in-game times so loading screens don't count:
      F1 > Speedrun Timer, a split a mission on the level timer, LiveSplit .lss in and out (the last split
      ends at Cradle's credits, not when input is lost)
- [x] A film grain slider: F1 > Enhancements > Visuals > Film Grain, off to the game's own
- [x] An option to turn bloom off: F1 > Enhancements > Visuals > Disable Bloom
- [x] The in-game menus scroll with the mouse wheel, like the F1 menus: in a menu or the pause menu a notch
      is up or down, in play it still changes weapon (main menu checked by script)

## Cheats

- [x] Turbo mode: F1 > Cheats, a slider up to 5x the game's speed, campaign only (3x walked 13.94 m where 1x walked 4.65)
- [x] Jumping
- [x]-ish Spawn any weapon
- [ ] All weapons (unless it breaks the way multiplayer maps do in single player, and whether that can be fixed)
- ~~[ ] Force Sgt Garcia's best spawn point~~
- [x] Infinite ammo: F1 > Cheats, campaign only; every kind of ammo stays full (41 shots from a 16 round P99 on Dam)
- [x] Grenades in single player: F1 > Cheats, four frag grenades each level, campaign only (thrown and exploding in Nightclub)
- [x] Infinite sprint: F1 > Cheats, campaign only (a sprint on Dam ends with all 3.1 s left)
- [x] God mode: F1 > Cheats, campaign only; the player's damage is left out (not seen in a fight yet, no scripted run got hurt)
- [x] Enemy rockets
- [x] 007 mode: Select Difficulty > 007 Custom, sliders for enemy health, enemy damage, enemy reaction speed
      and hit protection, then the mission as 007 Classic (the menu and the melee value checked, see docs/STATUS.md);
      the sliders take the mouse, and a switch turns instant kill headshots off (the switch not seen in a fight yet)
- [x] 007 mode: a slider for enemy accuracy (the game has no value for it by difficulty): Enemy Accuracy, 0 to 200%,
      the spread of a guard's shots is twice as wide at 0 and gone at 200 (the guards' accuracy seen bent in Solar)
- [x] Invisible enemies: F1 > Cheats > Invisible Enemies, enemies aren't drawn, their shots are (needs checking
      in play: every thing of the draw list hidden is seen gone, a guard in play is drawn from it, not seen hidden on a picture)
- [x] Invisible player (enemies never see you): F1 > Cheats > Invisible Player, the game's own "can be seen" byte held off
      and the stimuli about you left out (needs checking: in the one replay tried you shoot, and the guards still came)
- [ ] Fly mode (noclip that doesn't go through walls)
