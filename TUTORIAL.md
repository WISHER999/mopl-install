# MOPL# Tutorial

Everything below was run against the current interpreter (`mopl.c`) before
being written down — every code block's output is real, not illustrative.

## 1. Setup

```bash
chmod +x build.sh
./build.sh
```

Compiles and links `mopl` from `mopl.c` and `graphics_pipeline.m`. Must be
run on an Apple Silicon Mac with Xcode Command Line Tools installed.

```bash
./mopl run script.mopl
```

## 2. Your First Script

`first.mopl`:
```
Tag.x = num.5
Terminal 'x'
```

```bash
./mopl run first.mopl
```
```
5
```

`Tag.x = num.5` declares a tag named `x`, of type `num`, value `5`.
`Terminal 'x'` prints its current value.

## 3. Reassigning a Tag (the "+" Law)

Each time you write to an existing tag, add one more `+` before `=`, and
the new value **accumulates onto** the old one — it does not replace it.

```
Tag.x = num.5      // 1st write (0 '+'), sets x = 5
Tag.x += num.10     // 2nd write (1 '+'), adds: x = 5 + 10
Terminal 'x'
Tag.x ++= num.20    // 3rd write (2 '+'), adds: x = 15 + 20
Terminal 'x'
```
```
15
35
```

The `+` count must exactly match how many writes have already happened —
skip a `+` (or add an extra one) and the interpreter refuses the write
instead of silently doing the wrong thing:

```
Tag.x = num.5
Tag.x ++= num.10   // wrong: this is the 2nd write, needs exactly one '+', not two
Terminal 'x'
```
```
5
```
```
runtime error: '+' count does not match assignment level for 'x'
```
The rejected write leaves `x` unchanged, so `Terminal 'x'` still prints
the old value (`5`).

## 4. The Four Data Types

```
Tag.a = num.42            // numeric
Tag.b = state.true        // boolean
Tag.c = blea.Hello World  // text
Tag.d = logic.            // code block (not yet executable)
```

`state` values accumulate the same way `num` does when reassigned (a
`state` tag's `true`/`false` is stored as `1`/`0` internally). `blea`
overwrites on reassignment rather than concatenating.

## 5. Doing Math — `Op of`

```
Tag.a = num.4
Tag.b = num.3
Op of a add b ==
Terminal '_'
```
```
7
```

The result of any `Op of ... ==` lands in a special tag called `_`.
Print it right after with `Terminal '_'`.

### Supported binary operators

```
Tag.a = num.4
Tag.b = num.3

Op of a add b ==        Terminal '_'   // 7
Op of a subtract b ==    Terminal '_'   // 1
Op of a times b ==       Terminal '_'   // 12
Op of a divide b ==      Terminal '_'   // 1   (integer division)
Op of a mod b ==         Terminal '_'   // 1
Op of a and b ==         Terminal '_'   // 0   (bitwise AND)
Op of a or b ==          Terminal '_'   // 7   (bitwise OR)
Op of a xor b ==         Terminal '_'   // 7   (bitwise XOR)
Op of a shl b ==         Terminal '_'   // 32  (shift left)
Op of a shr b ==         Terminal '_'   // 0   (shift right)
Op of a min b ==         Terminal '_'   // 3
Op of a max b ==         Terminal '_'   // 4
```

Every one of those actually prints `7, 1, 12, 1, 1, 0, 7, 7, 32, 0, 3, 4`
in order when run as a single script.

Operands can be tag names (their current value is used) or numeric
literals directly — `Op of a add 10 ==` works the same as using a tag.

### Special operators: `rnd` and `len`

```
Tag.msg = blea.Hello World
Op of len msg ==
Terminal '_'

Op of rnd 1 10 ==
Terminal '_'
```
```
11
3
```
(`rnd`'s actual printed number will vary run to run — `3` was just what
this run produced, within the requested `1`–`10` range.)

`Op of len <tag>` only works on `blea` tags; it returns the string's
character count. `Op of rnd <min> <max>` returns a random integer in
that inclusive range.

## 6. Inspecting Tags — `Dif`

```
Tag.x = num.1
Tag.x += num.2
Dif in x
```
```
x = num, level 2
```

"Level" is the number of times the tag has been written to (`= ` counts
as write #1, each subsequent `+`-prefixed write increments it by one).

List multiple tags: `Dif in x, y, z`.

```
Tag.x = num.1
Tag.x += num.2
Tag.y = state.true
Tag.z = blea.hi
Dif in x, y, z
```
```
x = num, level 2
y = state, level 1
z = blea, level 1
```

## 7. Loops — `Cycle` / `EndCycle`

```
Tag.n = num.0
Cycle 5
Tag.n += num.1
Terminal 'n'
EndCycle
```
```
1
2
3
4
5
```

`Cycle <N>` captures every line up to the matching `EndCycle` and replays
it `N` times. Note: inside a `Cycle` body, the "+" Law's `+`-count check
is relaxed — a captured line like `Tag.n += num.1` has the same literal
text (and the same single `+`) on every replay even though `n`'s level
keeps climbing, so a mismatch there is treated as "accumulate again"
rather than a real Law violation. Outside of a loop, the strict check
from section 3 still applies.

## 8. Graphics — `Gfx.*`

```
Gfx.init "My Window" 400 300
Gfx.clear 4278190080
Gfx.rect 10 10 50 50 4294901760
Gfx.circle 200 150 40 4278255360
Gfx.line 0 0 399 299 4294967295
Gfx.text "hi" 20 20 16 4294967295
Gfx.present
Gfx.poll
Gfx.shutdown
```

Opens a native Cocoa window and draws into an offscreen canvas. Colors
are packed 32-bit integers in `0xAARRGGBB` order (alpha first) — for
example `4294901760` is opaque red, `4278190080` is opaque black,
`4294967295` is opaque white.

| Command | Arguments |
|---|---|
| `Gfx.init` | `"title" width height` |
| `Gfx.clear` | `color` |
| `Gfx.rect` | `x y w h color` |
| `Gfx.rect_outline` | `x y w h thickness color` |
| `Gfx.line` | `x0 y0 x1 y1 color` |
| `Gfx.circle` | `cx cy radius color` |
| `Gfx.text` | `"string" x y size color` |
| `Gfx.pixel` | `x y color` |
| `Gfx.present` | — (pushes the canvas to the window, paced to 60 FPS) |
| `Gfx.poll` | — (processes window/input events; call once per frame) |
| `Gfx.shutdown` | — (closes the window) |

For an animated or interactive program, `Gfx.poll` and `Gfx.present`
are meant to run inside a `Cycle` loop, once per frame.

## 9. Full Example

```
Tag.x = num.5
Tag.x += num.10
Terminal 'x'

Tag.HI = state.true
Tag.Hi = num.1
Op of Hi add HI ==
Terminal '_'

Dif in x, HI, Hi
```
```
15
2
x = num, level 2
HI = state, level 1
Hi = num, level 1
```

`x` is `15` (5 + 10, per the "+" Law). `Op of Hi add HI` treats the
`state` tag `HI` (`true` → `1`) and the `num` tag `Hi` (`1`) as plain
integers, so `1 + 1 = 2`.

## 10. What Doesn't Work Yet

Recognized but silently skipped — writing these won't error, but they
also won't do anything:

```
Tag.x = num.5
Check x greater 0
    Terminal 'x'
EndCheck
Window main
    In 'title' at 0,0
EndWindow
Tag.func = logic.
    Terminal 'unreachable'
EndLogic
Terminal 'x'
```
```
5
5
```
(`Terminal 'x'` prints `5` both times — once from inside the skipped
`Check` block having no effect either way since the block doesn't run,
and once from the final line. The `logic` body and `Window`/`In` block
are likewise parsed and ignored.)

- **`Check`/`EndCheck`** — conditionals aren't evaluated yet.
- **`Window`/`In`/`At`** — this is a second, unimplemented window syntax;
  use `Gfx.init` (section 8) instead, which already creates a window
  with an explicit title and size.
- **`logic`/`EndLogic`** — function bodies are stored as empty
  placeholders, not executed. Calling one does nothing.

## 11. Current Limitations

- `blea` (text) tags overwrite on reassignment rather than
  concatenating — `Tag.msg += blea.more text` replaces the string
  rather than appending to it.
- `Op of` with an unrecognized operator name prints a runtime error
  (`unknown operator '...'`) rather than silently doing nothing.
- The "+" Law's write-count check is strict everywhere except inside a
  `Cycle` body (see section 7).

## 12. Running in VS Code

Add a `.vscode/tasks.json` with a build task (`bash build.sh`) and a run
task (`./mopl run "${file}"`) so you can build and run the current file
with a keybinding instead of switching to the terminal each time.