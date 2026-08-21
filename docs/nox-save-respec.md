# Nox saved-character respec

While Nox is waiting at its main menu, before choosing a saved game, open
**View > Respec (main menu only)**. This is a separate window from **Hack** and
is disabled everywhere except the verified pre-save menu loop. It offers
**Save 1**, **Save 2**, the six character slots, the character's currently
equipped items, and controls for STR, DEX, and INT.

The three attributes can only be redistributed: every value must remain at
least 8 and their original total must be preserved. **Apply respec and
unequip** edits the selected `DATA.SAVE.GAME1` or `DATA.SAVE.GAME2` on the
mounted HDV and removes everything equipped by that character, including both
hands and all head, torso/cloak, boots, gloves, ring, and necklace slots. The
items remain in inventory.

NAC enables this editor from the game's executed main-menu control flow and
disables and closes it before the selected menu action begins. It never
inspects or changes the running party or game UI. Equipment names and state are
read from the selected save and the HDV's item-definition file.

Unequipping updates all three save representations used by Nox: inventory
readiness masks, the eight-slot equipment summary, and the party record's
cached equipment values and hand labels. Before writing, NAC rereads and
compares the complete save file with the data shown in the editor. Every
affected 512-byte disk block is snapshotted, written, and read back as one
transaction; if any block fails verification, NAC restores and verifies all
original blocks.
