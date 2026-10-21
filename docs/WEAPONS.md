# How a gun works in GoldenEye 007 (Wii)

What makes up a weapon, where each part lives, and what a custom weapon needs. All of it was read
from the game's own data and code (addresses are the DOL's, SJBE52). The custom weapons of
`mods/weapons` (port/customweapons.cpp) are built on this.

## The pieces

| piece | where | keyed by |
|---|---|---|
| stats, sounds, effects, the gun's scripts | weapons table: spreadsheet `1400001C` of `o_guns.edb` | key `4B00xxxx` |
| animations to play, parents to fall back to, attachments allowed | definitions table in the DOL at `80578AB0` | inventory id `0200xxxx` |
| the gun model (its parts) | an animscript of `o_guns.edb` (row `+0x178`) | `04xxxxxx` |
| the gun parts' own motion (slide, magazine) | animscripts of `o_guns.edb` (definition `+0x1C`) | `04xxxxxx` |
| the arms' animations | anim modes / sets / scripts / animations of `cp_bondfp.edb` | `09` / `0A` / `04` / `03` |
| the loadout menu's list | DOL `80585C80`, menu pages `7D0000C0` (primary), `7D0000C1` (secondary), `7D0000E8` (all) | key |
| preset loadouts, unlock levels | spreadsheets `14000038`, `1400003D` of `o_loadouts.edb` | loadout hash, item hash |

### The weapons table (805D1A4C)

`ge_weapons_load` (800CC9D0) reads `o_guns.edb` (0109000F) once: spreadsheet 1400001C has eight
sheets of 69 rows (0x44, 0x84, 0x84, 0x34, 0x24, 0x54, 0x68, 0x10 bytes a row, each but the first
starting with its own copy of the key). They are put together into one row of 0x254 and inserted
sorted by key (fn_800CD950, a copy). Then rows of one id with and without the silenced byte are
paired. The table is freed at a level's end (fn_80006C40) and read again.

| offset | what | P99 | AK-47 |
|---|---|---|---|
| +0x000 | key | 4B000009 | 4B00000D |
| +0x004 | class (4B2000xx) | 4B200001 | 4B200004 |
| +0x008 | byte: silenced variant | 0 | 0 |
| +0x00C | byte: automatic | 0 | 1 |
| +0x00D | byte: clip | 16 | 30 |
| +0x00E | byte: clips | 12 | 8 |
| +0x010 | float, probably shots a second | 5 | 10 |
| +0x014 | float, probably the longest range | 200 | 200 |
| +0x028..+0x030 | ammo kinds (4B1000xx) | | |
| +0x044, +0x048, +0x04C, +0x050 | the player's damage near, near range, far range, damage far | 40 10 20 35 | 35 25 45 20 |
| +0x054..+0x140 | spread, recoil and the same set again from +0xC4 (not all named) | | |
| +0x14C..+0x158 | a guard's damage near, ranges, far | 35 4 14 6 | 7.5 11.5 28 2 |
| +0x174 | inventory id | 0200016E | 02000319 |
| +0x178 | the gun as a script of o_guns (also what a guard holds) | 04001DAC | 04001DB3 |
| +0x17C, +0x180 | the reload scripts of cp_bondfp (own, then a shared class one) | 04003148 | 04003140 |
| +0x188, +0x18C | the ammo's and the clip's items | | |
| +0x190 | name text (43xxxxxx) | 430010B2 | 43001093 |
| +0x194..+0x1E0 | shot effect scripts of o_guns section 0 (flash, shells, tracers) | | |
| +0x1E4..+0x240 | sounds (2D70xxxx) | | |
| +0x244 | the gun's own sound bank (sb_wpn_*) | none | 2D50002E |
| +0x248..+0x250 | particles (06xxxxxx) | | |

### The definitions (80578AB0)

0x2C an entry, ending with a 0 id. Filled into a map sorted by id (805D4624) on first use (byte
806EBB38) through `ge_weapon_def_add` (80169480); the last of the game's is 02000001.

| offset | what |
|---|---|
| +0x00 | id |
| +0x08 | parent (the Torka's is the P99, most rifles' the AK-47, the AK-47's 02000001) |
| +0x0C | animation list |
| +0x10 | second parent |
| +0x14 | animation list when aimed in (may be 0) |
| +0x18 | more animations |
| +0x1C | script list: the parent's gun scripts and this weapon's in their place |
| +0x28 | attachments allowed: a bit by the attachment hash's low byte (fn_801691D0) |

An animation list is rows of five words: an action (a generic anim mode of cp_bondfp, 09xxxxxx),
then this weapon's own anim mode for each firing mode. `fn_80168C50` looks an action up and falls
back to the parents. So a weapon only lists what differs: the Torka has three rows, everything
else is the P99's. The P99 has 13 actions (idle, fire, reload, equip, unequip, sprint... not all
named yet; 090005D7 and 090007A7 play the same 34-frame clip, 09000765 a 61-frame one).

### First-person animation data (cp_bondfp.edb, 01020016)

28 sections, only section 0 stays loaded; a level loads the sections its weapons use (a network
game has all). 277 anim modes, 270 anim sets, 270 animscripts, 281 animations.

action (09) → weapon's anim mode (09: a small program, `0C000002 n` starts anim set n) → anim set
(0A: plays animscript n, `840000nn`) → animscript (04: commands at +0x44: cmd 2 plays an animation
`83nnnnnn` on the arms skin 0D00016F of the `sk_arms_*` file 010A002B, cmd 5 a sound, cmd 0xB an
event like 16000185 / 1600018E) → animation (03: skeletal motion data).

A clip is the arms only. The gun's moving parts are a separate script of o_guns played with it:
the P99's model 04001DAC is six entities (82000xxx, by index) on locators (15000168, 150000CD:
the slide, the magazine...), its reload 04001DAD is the same six with 58 frames of keyframes and
the magazine sounds. A definition's +0x1C swaps those generic scripts for the weapon's own
(the Torka: 04001DAC → 04002537, 0400207D → 04002541, 04001DAD → 04002538).

### Models (o_guns.edb, 0109000F)

424 entities, keyed by the inventory id for the guns themselves (0200016E is the P99 body, 0.2 m
long, in section 9), and 245 animscripts. `ge_gun_parts` (800CCF70) shows a model's parts by the
nine hashes at 805730D0 (body, then the attachments by flag) and hides the rest.

### The loadout menu

A loadout (profile 805DE788 + pad × 0x590, five of them) keeps keys: primary +0x148, secondary
+0x15C, then gadgets +0x198 and attachments +0x1AC. The menu pages are rows of kind 0x10 with the
key as the row's hash (7D0000C0: 21 guns, 7D0000C1: the five pistols, 7D0000E8: all 26).
`fn_80256A80` takes a pick out again unless the key is in the list at 80585C80 (key, name, about,
picture number; six places a kind of gun: pistols, SMGs, heavy, rifles, snipers, with free places
5, 11, 17, 23, 24, 30, 31), and a secondary that isn't one of the five pistols.

A network game gives each player the weapons of its loadout row by key (`fn_800C2740` from
fn_800C2530 and fn_8013B4E0), so every console in the game needs the same custom weapons.

### Poses

`ge_weapon_poses` (800FE3A0) makes, once, a pose for every definition's idle anim mode
(09000765, aimed 090007A7) into a cache by id (806EBAD4). It walks the DOL table itself, so a
weapon that isn't in it has no pose: neither the hands nor the gun are drawn.

### Online

- A player's loadout travels in its network player (+0x144 the loadout's hash, +0x148 primary
  key, +0x14C secondary, ... +0x164), all as whole 32-bit words (fn_801CCB30 writes them,
  fn_801CC660 reads them back). A preset's hash is looked up in o_loadouts on each console, a
  custom loadout uses the keys as sent (fn_800C2530).
- What another console's copy of a player holds comes from its inventory, sent item by item as
  a 6-bit place in the list at 80574E0C (37 entries: unarmed, the 27 guns, the phone, the gadgets;
  fn_80129780 / fn_80129830 and the same search inlined in fn_801266A0, fn_80127A20,
  fn_80128C00, fn_80128D90, fn_8012AD60). An id not in it is sent as nothing.
- In a match the loadout is picked under "Game starting in...": from Close Quarters, Right goes
  Shotgun, Sniper, Custom 1 ... Custom 5.

## What a custom weapon is now (stage 1)

`mods/weapons/NAME.txt` (see port/customweapons.cpp's header for the keys):

```
name = Golden AK
about = An AK-47 plated in gold. Twice the damage, 45 rounds.
base = AK-47
clip = 45
damage = 2
```

- a copy of the base's row with its own key (4B000100 up, in file name order), id (0200F000
  up), name and about texts, and what the file changes;
- a copy of the base's definition with its own id, so it has the base's animations, gun scripts
  and attachments; a pose made for it after the game's own;
- a free place in the loadout list (its base's kind first; seven places in all) and a row in each
  loadout page of its kind (a pistol in the secondary's, the rest in the primary's, all in 7D0000E8);
- a place in the network item list after the game's 37 (26 custom weapons at most);
- its files go into the version of a modded network game, so only consoles with the same
  weapons find each other.

Outside a modded network game (Connect to Nintendo WFC) the pages don't have them, and a loadout
that has one gets its base when it is given; the profile isn't changed.

**Checked**: Dam with it forced (`GE_RANDOMIZER_WEAPON=0200F000`): the AK drawn in the hands,
45 / 360. Online, a host and a joiner against the local server: the weapon in Choose Primary
Weapon, saved in Custom 1, picked in the match: the host holds it (fires, reloads, 45-round
clip), and the joiner's copy of the host holds 0200F000 too (it held nothing before the network
list had it). Through Connect to Nintendo WFC the same loadout spawns an AK-47.

**Not checked**: what the joiner draws of the host's gun and hears of its shots (they never
faced each other in the runs), damage done to another player, a silencer on one (the row has
no silenced pair), more than two players, the stat bars of the menu (they show the base's).

## A gun of its own (stage 2)

A weapon's `model = NAME.edb` (in `mods/weapons`, next to its .txt) is a file made by eurochef:

```
eurochef-cli ge weapon --base AK-47 --from o_guns.edb --parts                 # the base gun's parts
eurochef-cli ge weapon --base AK-47 --from o_guns.edb --export ak47.gltf      # the base, rigged
eurochef-cli ge weapon mygun.gltf --base AK-47 --from o_guns.edb -o mods/weapons/mygun.edb
```

The eurochef GUI does the same: File > New GoldenEye 007 weapon (base gun, o_guns.edb, scene,
gun file, brightness, file hash; Export base rig, Build, and the weapon's .txt with name, base
and model if asked).

- **The rig** (`ge/weapon_rig.rs`): the base gun as one skinned mesh, an armature with a bone
  `Part_N_role` (role: the locator's, from `locator_name`) for each part, put where the first
  script that names it (the model script for most) puts it on frame 0, from that command's
  controller (+0x34 list, the command's byte 10; channels 0x4 position, 0x8 rotation x y z w,
  0x10 scale, keyframes of a frame number and the value). Every part's triangles weighted all to
  its bone, so the gun is seen assembled and a part is a vertex group. The scripts with more than
  one frame go along as animations named by their hash (fire, reload...), not read back. A part
  without a controller in its first script rests at the origin (the AK's parts 9 and 10).
- **The build** takes a skinned mesh's triangles to the part of their heaviest joint (`Part_N...`)
  and each vertex into that joint's space by the inverse bind matrix, so the copied scripts put it
  back; `Part_N` nodes as before still work. Checked: AK-47 rig exported, part 0's materials
  tinted, built: all 16 parts' vertices the same as the old `Part_N` path's to 3e-8.

- **The file**: the scene's nodes `Part_N` (or `Body` for part 0) are the gun's parts, 0x601 mesh
  entities with flags 2 as the game's are; a part the scene leaves out is empty. Then copies of
  every script the base gun is drawn and moved with (its model, fire, reload..., by firing mode:
  `BASE_GUNS` in eurochef's `ge/weapon.rs`), byte for byte, with the parts they name renumbered
  as the file's. A script is one block from its header to its keyframes with only relative
  pointers inside. Parts are numbered as the scripts first name them; two on the same locator
  are one.
- **In the game** (`port/customweapons.cpp`):
  - the file is a mod (mods.cpp reads `weapons/`); its hash is what the mods give it.
  - the first time a script of it is asked for in a level the file is got as `fn_801461C0` gets
    o_guns (`fn_800E3740`: files manager 805D36A4, the hash, 0), which loads it there and then.
    Loaded that way its lists' things have no place yet (a row's +0xC is set and its section
    short gets bit 0x8000 when `fn_802E9610`, the set-up `fn_802EC4C0` does once a file is in,
    runs): the port runs `fn_802E9610(data, -1)` itself.
  - its scripts get the weapon's own hashes (top half 04F0 + the weapon's number, the base's low
    half, so the sorted list stays sorted); the row's +0x178 and a new +0x1C list in the
    definition (the base and its parents' rows, the nearest one's for each generic script, with
    the file's copies in) name them.
  - `ge_script_set` (8031A8E0, the script object's vtable +0x20: object, file, hash; it reads the
    script through the list row's +0xC) gets the gun's file for those hashes, and
    `ge_file_object` (800E3D20: a file with the section of one of its objects loaded, what a
    guard's AttachGun asks for o_guns and the row's +0x178) answers with it.
- **Checked**: the AK-47's 16 parts exported, the body tinted gold, built again: on Dam the
  whole AK is drawn with the gold body, fires (45 to 15, the recoil the fire script moves),
  guards take it (none fails AttachGun). With only `Body` in the scene, the body alone.

- **A guard's gun, a gun on the floor**: `ge_gun_parts` (800CCF70), with its last argument set as
  a guard's AttachGun passes it, shows only the parts on the locators of 805730D0 (the body first)
  and sets their node's +0x136 and +0x137 to 1 (other code does the same for a gun on the floor).
  A node with +0x137 set is drawn (`fn_80312F20`) from its copy numbered +0x136 at +0x114, out of
  its entity row's **levels of detail** (`fn_8033AAC0`): the row's +0x10 word (low byte 0x20, above
  it where the list is), rows of 0x18 bytes ended by a 0 word: where the entity is from the row,
  a distance and three 0 words, a short FFFF and flags (2000 on the first). The P99's body: itself,
  then entity 0 (0200016E), the whole gun in one mesh in the body's space, which is what a guard
  holds. A row without the list has no copy 1: nothing is drawn (Bond in third person draws copy
  0 and shows it). eurochef's `ge weapon` gives the body that list with the whole gun (the parts
  the model script puts on it, attachments left out, each put where it rests on frame 0 and taken
  into the body's space), even for a base whose body has none (the Golden Gun), and an attachment
  that has one in the base (the silencer) a copy of itself.
- **Points**: block 5 of the blocks a mesh's +0x40 word lists (low byte which blocks there are,
  above it where their table is; a table row is where its block is and the block's number, 0x405
  for block 5 right behind it). The block's +5 is the count, from +6 where the 0x30-byte points
  are: a hash (10xxxxxx), three floats of 0.5, a position at +0x14, a rotation at +0x20
  (`fn_80313EA0` finds one by its hash, in the level of detail drawn). The P99's whole gun has
  1000008B (the grip) and 100000B8 (the muzzle), its frame 100000B8 and 10000147. eurochef copies
  each base part's points, and the base's far ones to the far meshes; the port moves them with the
  gun (`model offset`, `model scale`).
- **A sliver out of the gun** (the user's deagle, a spike up from the rear sight, in the hands
  and on a guard): a skinned triangle goes to the part most of its corners are weighted to, but
  each corner was taken into its own joint's space, so a corner weighted to the slide in a
  triangle of the frame stayed 4 cm off. Now all three corners go into the chosen part's joint's
  space (eurochef's importer, `chosen`).
- A gun file built before any of these has to be built again.
- **No gun for guards in the row** (the Golden Gun's +0x178 is FFFFFFFF), or a file without the
  copy of the base's: the row gets the file's copy of the gun the hands hold (what the
  definitions give for 04001DAC), whose body draws the whole gun on a guard; without a file
  the Golden Gun's gets 0400293D, the one Golden Gun mode lays in the level.
- **A guard's damage** with a custom weapon is the player's (+0x44..+0x50 copied to
  +0x14C..+0x158) unless the file sets one of those itself.
- **The randomizer** (F1 > For Fun) has the custom weapons in its pool and in Forced Weapon's list,
  after the game's 27.

## A shot of its own (sound)

`fire sound = NAME.wav` (next to the .txt; `fire volume` scales it) plays that file for every
shot of the weapon in place of its base's sounds (`port/customsound.cpp`, `port/customweapons.cpp`):

- **the player's and another player's**: `ge_weapon_fire_sound` (8012E3A0: controller, the shot's
  place) plays the held row's (the controller's byte +0xC98 in the weapons table) +0x1E4..+0x1F0,
  or three of +0x1F4..+0x22C. A local shot (fn_8012E1A0) also sends network message 0x21
  (fn_801DE7E0); the console that gets it calls the same function from 801DEF00 / 801DEFC0 for
  that player. The port runs it with its byte 806EBA7C set (no sounds, the noise the guards
  hear stays) and plays the file at the place.
- **a guard's**: fn_80057060 plays the guard's row (+0x754) +0x214..+0x21C through
  `ge_character_sound` (80283F60: character, sound, 0; at the entity's +0xF8), the call returning
  to 800577A0. From there a custom weapon's are left out and the file plays once.
- **the mix**: the file is mixed on the host after the game's mix (wiikit `audio_set_mixer`), at
  0.08 of full scale (the game's own shots peak at about 2400 of 32768), full within 2.5 m of the
  listener, fading out by 100 m, panned by the listener's +0x34 axis. The game's volume options
  don't reach it, nor do the occlusion and the height filter.

## How a reload is put together

Two scripts play side by side, both at 30 fps, started by the same action:

- **The arms**: the weapon's anim mode (cp_bondfp, 09) starts an anim set (0A), which plays an
  animscript (04): cmd 2 plays the arms' skeletal animation (03, `83nnnnnn`) on the `sk_arms`
  skin, cmd 5 plays sounds, cmd 0xB raises events (16000185, which the weapon code waits for).
  The AK's: 04003140, 74 frames, sounds on frames 0, 12, 20, 35, the event on 47.
- **The gun's parts**: the o_guns script of the firing mode (the AK's reload 04001DF1, 73
  frames): cmd 3 puts each part on its locator with a controller of keyframes (the magazine
  82000037: 27 keys of position and rotation, 820000A9: 13 of position), cmd 5 the magazine's sounds (frames 8 to 53),
  cmd 0x11 timing jumps.

A reload of its own would need:

- **gun parts**: an animscript writer from glTF animations of the rig's bones (keyframes per
  controller, the command list of the base kept); the rig export already writes the base's
  keyframes out, the way back is not done.
- **arms**: decoding cp_bondfp's type-03 motion data (not done), then writing it; until then
  the base's arm clip plays, and the parts' keyframes have to stay in step with it (73 vs 74
  frames on the AK).

## What custom animations still need

- **The gun parts' motion** (slide, magazine on reload, fire): animscripts with keyframed
  controllers (position, rotation, scale). The copies are the base's; a writer of new keyframes
  (from glTF animations of the parts) would give a gun its own.
- **The arms**: skeletal animations of cp_bondfp (type 03). eurochef has no decoder for their
  motion data yet, so the format has to be read from the game's decoder first. Until then a
  custom weapon uses its base's arm animations, which are shared by kind anyway (the pistols the
  P99's, the rifles mostly the AK-47's).
