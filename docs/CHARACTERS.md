# Custom characters (Wii)

A character of your own for network games of Connect to WFC Modded: a body made in Blender on one
of the game's skeletons, picked on the online Character page. Addresses are the DOL's (SJBE52).

## Making one

```
eurochef-cli ge skin --base sk_jaws_jaws.edb --parts                  # the base's bones and meshes
eurochef-cli ge skin --base sk_jaws_jaws.edb --export jaws.gltf       # the base to start from
eurochef-cli ge skin mine.gltf --base sk_jaws_jaws.edb -o modpacks/NAME/characters/mine.edb --hash 0x010AF000
```

Arms for first person are made the same way on an arms file (`--base sk_arms_jaws.edb`: its own
skeleton of 48 bones, the fingers too, named by number only).

The base files are the game's `sk_<who>_<outfit>.edb` (sk_bond_arkangel, sk_jaws_jaws,
sk_trev_trevel...). Every body has the same skeleton of 37 bones, so the base only decides the
proportions the animations were made for, the hands and head parts that can be kept, and the row
the character is copied from.

In Blender (File > Import > glTF):

- The export is the base at the game's size (Bond is 1.8 m), its armature `Skeleton` with bones
  named `Bone_08_Head`, `Bone_12_L_Hand`...: only the number counts. Leave the armature as it is:
  the game moves the bones from where the base has them, whatever Blender shows.
- `Body` is what the build replaces: every mesh in the scene that isn't one of the kept parts
  becomes the new body. Delete it and put yours in, weighted to the bones (Ctrl+P > With Automatic
  Weights, or paint them). Two weights a vertex are used, the two heaviest. A mesh parented to a
  bone instead (a hat on `Bone_08_Head`) follows that bone.
- `Hand_L`, `Hand_R`, `Head_1`... are the base's own, with the hands' grips (morphs). While their
  objects are in the scene they're kept as they are (edits to them aren't read); deleted, they're
  left out, then model hands into your body.
- Textures are taken from the materials' base colour; the base's own textures, unchanged, stay
  the base's.

## The character's file

`modpacks/NAME/characters/mine.txt`, next to the .edb:

```
name = Agent Smith       # what the Character page shows
base = Jaws              # the game's character it is made from, by name or hash (4D00xxxx)
body = mine.edb
arms = mine_arms.edb     # optional: first person arms of its own, the base's when left out
width = 1.0              # optional: its size, the base's when left out
height = 1.15
```

Two bodies may have the same hash: the pack gives the second another one. The pack's character
files go into the network game's version, so only consoles with the same characters find each
other. A pack has at most 64.

## How it works

- **The body file**: the animskins (header list at 0x60, rows of 0x24) are the body's levels of
  detail, 0D00016E then 8D000001 and 8D000002 (a row's +0x14 is the next one). Each is a block of
  relative pointers: +4 the bone count, +0x10 the body's box, +0x40 each bone's place in the bind
  pose (only moved, never turned), +0x48 parent and last descendant, +0x68 and +0x70 how many
  skinned meshes in two lists at +0x6C (0x14 each: skin data, parts, a section, the entity, a morph).
  The skin data is a table: a word to the weights, then runs of 6 bytes (how many vertices, the
  bone, the second bone or FF, FFFF) over the mesh's positions in order, never across a multiple of
  160 (fn_802A36F0 skins 160 at a time through ge_skin1/ge_skin2); the weights are two shorts a
  vertex, 0x2000 the whole, the heavier bone first (the normal follows only that one). The meshes
  are 0x601 entities with flags 3 (a strip's texture is a place in the mesh's own list).
- **The build** (eurochef `ge/skin.rs`) writes the scene's mesh with its positions sorted by bone
  pair into a copy of the base file: the header's lists grown for new textures, everything up to the
  meshes moved as it is, the new body in place of the base's on every level of detail, the parts not
  kept taken out of the animskins.
- **The character sheet** (805D1598): +8 count, +0xC character rows (0x44: hash, set, name text,
  animations file, +0x18 body file, +0x1C its animskin...: what a preview or a guard is drawn from),
  +0xD0 count, +0xD4 skin rows (0x18: hash, arms file, body file, animskin, width, height: what a
  network game's players are drawn from). ge_character_sheet_load (800B4300) fills it from
  spreadsheet 14000021 of o_chrctr.edb (01090013) whenever the characters were freed (fn_800B4260
  at each level's end; the skins stay), each row through ge_character_insert / ge_skin_insert
  (sorted, a hash already there is refused), then builds the sets and split screen's roster from
  pointers to the first rows.
- **The port** (`port/mods/characters.cpp`): the custom characters' rows (4D00F000 up, after all of
  the game's, name texts 4300F600 up) go in before the game's first row of each load, so nothing
  that points at rows sees one move, and are filled from the base's rows once the load is done.
  The Character page (`port/net/character.cpp`) lists them after the game's; a pick travels as its
  hash like the game's.

## Checked

- Jaws, Bond and Trevelyan exported and built again unchanged skin the same triangles to the same
  places; the bones turned at random (as Blender does on import) build the same body.
- In the game: the rebuilt Jaws and a body of boxes with a texture of its own (weighted, blended at
  the joints) on the Character page, idling, with Jaws's hands; in a network game on Archives the
  other console draws the box body holding its gun; Jaws's arms rebuilt with a green texture as
  `arms =` are the first person arms in a match. sk_arms_jaws exported and built again: the same.

## Not done

- A lighter mesh for the far levels of detail (the
  body is used on all three), the morphs (grips, eyelids) for a mesh of your own.
