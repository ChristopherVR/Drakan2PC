# First LVL / ODB dumps (lvl_dump.py, odb_dump.py)

Tools: `analysis/scripts/lvl_dump.py` (`--dir`, or `--iso` with `--level`/`--all`; `--analyse` adds heuristics) and
`analysis/scripts/odb_dump.py` (ODB 0x101/0x102/0x103, per-class field roles). Output goes to `build/lvl-dump/` and
`build/odb-dump/`. `--all` writes several hundred MB. Tests: `scripts/tests/test_lvl_dump.py` (synthetic SRSC).

Levels sampled: LEVEL0RO, LEVEL5NE (PAL disc).

## ODB

- **0x102 instance records**: u16 name length (includes NUL padding), name, then a u32 body. The low 16 bits of the
  first body word equal the SRSC `sub` (class id) in 20/20 (LEVEL0RO) and 117/117 (LEVEL5NE) records. Confident.
- Body length is not fixed: per-class bodies are mostly 98 or 114 bytes.
- Float triples at body offset 0x10 in several classes (Decor, Snow Dwelling, Savage Ice Gate). Magnitudes are small
  (median |x| ≈ 0.01–0.3), so they are **not** confirmed positions and not unit rotation rows. Open.
- **0x103 (zlib) schema**: all 134 declared entries parse as (kind, u16 len, name). 3496 bytes are consumed in both
  levels; the remaining tail is unexplained.

## LVL

- 0x0001 contains layer names ("layer 18"); previously guessed to be geometry. It may still hold more than names.
- 0x0020 contains effect/property labels ("Radius of sphere (wu)"); not the sector table guessed in the census.
- 0x0025 is a material-name table of u16-prefixed variable-size records.
- LEVEL5NE 0x0040 (125 entries, 2.7 MB) are most likely TXD textures.
- The autocorrelation stride heuristic is weak: the lag-96 hit on 0x0025 comes from repeated tails.

## Corrections to earlier notes

- The save class ids 0x17081069 / 0x19001255 (`save-editor.md`) do not appear in these LVL/ODB files, so save
  records are not keyed by the same ids as ODB classes (or the ids are composed differently).
- `asset-format-census.md` section 4 candidates for geometry (0x0001) and sectors (0x0020) are not supported.

## Next

Placement data is still unlocated. Candidates: remaining LVL types, the 0x102 body beyond offset 0x10 combined with
the 0x103 schema (field kinds/names give the layout), and the level loader in Ghidra.
