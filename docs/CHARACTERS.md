# Custom characters (Wii)

A character of your own for network games of Connect to WFC Modded: a body made in Blender on one
of the game's skeletons, picked on the online Character page. Addresses are the DOL's (SJBE52).

## Making one

```
eurochef-cli ge skin --base sk_jaws_jaws.edb --parts                  # the base's bones and meshes
eurochef-cli ge skin --base sk_jaws_jaws.edb --export jaws.gltf       # the base to start from
eurochef-cli ge skin mine.gltf --base sk_jaws_jaws.edb -o modpacks/NAME/characters/mine.edb --hash 0x010AF000 \
    --arms sk_arms_jaws.edb                                           # and mine_arms.edb, hash 0x010AF001
```

`--arms` makes the first person arms from the same scene: the body's triangles from the collar to
the wrists moved onto the arms file's skeleton (48 bones, the fingers too), with the arms file's
own hands. An arms file's hands are made flat, the fingers straight out, for its finger bones to
close them round the gun; a body's hang relaxed and would sit bent behind the gun.
`--arms-own-hands` takes the scene's hands anyway (model them as the arms file's are), each
position weighted to the fingers as the arms file's closest one. Arms can also be made by hand on
an arms file (`--base sk_arms_jaws.edb --export`, then build on it), its bones named by number
only.

The base files are the game's `sk_<who>_<outfit>.edb` (sk_bond_arkangel, sk_jaws_jaws,
sk_trev_trevel...). Every body has the same skeleton of 37 bones, so the base only decides the
proportions the animations were made for, the hands and head parts that can be kept, and the row
the character is copied from.

In Blender (File > Import > glTF):

- A model rigged to a ValveBiped (Source) or Mixamo skeleton, A or T pose: import it next to the
  export and press GoldenEye > Character > Move Onto Game Skeleton in the 3D View's sidebar
  (eurochef's `blender_addon/ge_character_rig.py`, part of the Eurochef Utility add-on). It is
  scaled, turned and posed onto the game's skeleton, its weights moved to the game's bones (fingers
  to the hands), the base's body parts deleted. An SMD/DMX goes in with Blender Source Tools' Bone
  Append Mode on Make New Armature, or its bones are added to the export's armature.
  The arms' and legs' joints (shoulders, elbows, wrists, hips, knees, ankles) are put on the
  game's, the limbs' bones made as long as the game's (0.9 to 1.0 for CS:S's): the game bends the
  body at its own joints, and a wrist 7 cm off (CS:S on sk_bond_skybrig) swung the hand from
  inside the forearm, away from the gun. The spine, neck, head and collars keep the model's shape.
  Each hand is laid onto the base's (Hand_L, Hand_R, read before they go): turned at the wrist
  and its fingers bent at their joints until it lies on the base hand as closely as it comes,
  the forearm taking the wrist's twist along its length. The game's hands hang relaxed, palm to
  the thigh, and their grips are made for that hand; a T pose hand, flat and palm down, would
  close wrong. Then its fingers are bent again into each of the base hand's grips (the
  export's shape keys `Grip_0`...) and each is kept as a shape key of the model's mesh of the same
  name, the build's grips for that hand: the fingers bend at their own joints, so a hand made
  differently (CS:S hands, three fingers fused) closes whole. The console says how far apart they
  still are and how far it bent. CS:S models (`.mdl`) come in through SourceIO.

- The export is the base at the game's size (Bond is 1.8 m), its armature `Skeleton` with bones
  named `Bone_08_Head`, `Bone_12_L_Hand`...: only the number counts. Leave the armature as it is:
  the game moves the bones from where the base has them, whatever Blender shows.
- `Body` is what the build replaces: every mesh in the scene that isn't one of the kept parts
  becomes the new body. Delete it and put yours in, weighted to the bones (Ctrl+P > With Automatic
  Weights, or paint them). Two weights a vertex are used, the two heaviest. A mesh parented to a
  bone instead (a hat on `Bone_08_Head`) follows that bone.
- `Hand_L`, `Hand_R`, `Head_1`... are the base's own. While their objects are in the scene they're
  kept as they are (edits to them aren't read). The hands have grips: five shapes their fingers
  are blended between (open, round a gun...), the body skeleton having no finger bones. Delete a
  hand and model your own on its bone (`Bone_12_L_Hand`, `Bone_21_R_Hand`): it gets the base hand's
  grips, each position turned and moved as the base hand's closest ones are together (as a finger
  bends), so model it about as the base's lies, fingers and all. A bigger or smaller hand, or one
  a little away, is laid over the base's at its size to look its grips up, and closes as much.
  Shape keys named `Grip_0`... on the mesh (as many as the base's grips, five) are its own grips
  instead, what the rig makes; a mesh's other shape keys aren't read. Deleted with nothing on the
  bone, the hand is left out.
- Textures are taken from the materials' base colour; the base's own textures, unchanged, stay
  the base's. A material with Blend as its blend mode (sk_bond_skybrig's hair) is drawn blended,
  after the rest: the body is then two meshes, as the game's own is.

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
  are 0x601 entities with flags 3 (a strip's texture is a place in the mesh's own list). A body
  may be a split (0x603: +0x54 how many meshes, +0x58 no tree, +0x5C a pointer to each), then the
  animskin's row has as many parts, a run table each (sk_bond_skybrig: the body and the hair, the
  hair's mesh flagged 0x40000000, drawn blended after the rest).
- **Morphs** (the grips, the eyelids): the animskin's +0x84 count and +0x88 rows (0x10: the mesh's
  hash, a number, how many targets, the first of the weights the animations give, the kind: 0 adds
  each target by its weight, 1 goes between two by a table). The mesh's +0xB4 has 4 when it has
  extras, words from +0xB8 (low byte the kind, the rest how far from that word); kind 2 points at
  the targets' pointers, each target what it adds to every position (three floats).
  fn_802A36F0 blends them into a buffer of 0x4C30 bytes (805FB3E0), so a morphed mesh has at most
  1219 positions (the build allows 1200).
- **The build** (eurochef `ge/skin.rs`) writes the scene's mesh with its positions sorted by bone
  pair into a copy of the base file: the header's lists grown for new textures, everything up to the
  meshes moved as it is, the new body in place of the base's on every level of detail, the parts not
  kept taken out of the animskins. Hands of the scene's own go into the hands' meshes on the closest
  level (with their targets), the body without them; the far levels have no hands of their own and
  get the whole body.
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
- sk_bond_skybrig (a split body, hair blended) exported and built again: the same, the hair still
  its own blended mesh; on the Character page with its hair's soft edges.
- Grips: the base's hands renamed (so the build takes them as the scene's own) get targets equal to
  the base's (none apart, with the grips taken from the closest positions' turn); on the Character page their fingers curl as Jaws's do, with the targets zeroed
  they stay straight.
- `--arms`: in a match the box robot's arms with the arms file's hands, and Jaws's body's sleeves and
  forearms with them, the hands round the gun as the game's own.
- What the game does with the grips, from the morph buffer (805FB3E0) read in a network game:
  holding a pistol, each hand is its rest positions and target 3 at 1 (the rest 0), exactly as the
  file has them. Moved over from the base, CS:S SAS's fused fingers sheared into a flat paddle
  under target 3 (straight index, curled rest); bent by its own finger bones (rig, shape keys) it
  makes the trigger grip whole, and every other grip, SAS and Phoenix on sk_bond_skybrig.
- The rig on a ValveBiped spacesuit (DMX, thick three-fingered gloves) and a Mixamo man (FBX), on
  Jaws: the hands lie on Jaws's (10 to 13% of its size apart, 1.3 to 1.4 times as big), and built,
  their grips (read back from the file) close as Jaws's do, the fist and round the gun whole, where
  before the hand stuck out flat from the forearm and the fist tore.

## Not done

- A lighter mesh for the far levels of detail (the body is used on all three).
