# Line Editor

**Team:** Pavan B S (R25EJ097) · Pavan Srujan (R25EJ98)

A simple command-line line editor in C (Activity 7, Portfolio Building).
It holds the document in memory as a dynamic array of strings and lets you
create, view, and modify text one line at a time — entirely from the terminal.

## Features implemented

**Core (all 4):** insert a line · delete a line · display the document ·
save / load a file

**Bonus (all 4):** search · find & replace (whole document or single line) ·
multi-level undo · line / word / character counts

## Compile & run

```sh
make            # builds ./editor (or: gcc -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -o editor editor.c)
./editor        # start with an empty document
./editor notes.txt   # start by loading notes.txt
```

## Files

| File | What it is |
|---|---|
| `editor.c` | The whole editor (~700 lines, compiles warning-free with `-Wall -Wextra`) |
| `Makefile` | Build rules |
| `DESIGN.md` | Paper design: data-structure choice & justification, command set, function outline |
| `HELP.md` | Full command reference with usage examples |
| `README.md` | This file |

## Design in brief

Lines live in a dynamic array (`char **lines`, capacity doubling) — O(1)
indexed access, O(n) insert/delete via `memmove`, no per-line node overhead
like a linked list would add. Undo is a stack of inverse operations (insert ↔
delete, replace → restore saved texts), so `u` can be repeated. See
`DESIGN.md` for the full rationale. Type `h` inside the editor for a command
summary.
