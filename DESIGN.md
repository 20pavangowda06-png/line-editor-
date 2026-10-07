# Line Editor — Design Notes

*(This is the paper design for Activity 7: data structure choice, command set,
and function outline, agreed before any code was typed.)*

## Data structure choice: dynamic array of strings

The document is held as a `Document` struct:

```c
typedef struct {
    char **lines;   // heap-allocated, NUL-terminated strings (no '\n' stored)
    int count;      // number of lines in use
    int capacity;   // allocated slots
} Document;
```

**Why this, and not a linked list of lines?**

| Concern | Dynamic array | Linked list |
|---|---|---|
| Access line *n* | O(1) index | O(n) walk |
| Insert / delete at *n* | O(n) memmove | O(n) to find node + O(1) splice |
| Memory overhead | ~8 bytes/line | ~16 bytes/line + allocator overhead per node |
| Save/load, print-all | trivial linear scan | trivial linear scan |
| Cache behaviour | contiguous, friendly | pointer chasing |

Both are O(n) for insert/delete (the list still has to *find* line *n* first),
so the linked list buys nothing here while costing per-node allocations and
worse locality. The array's `memmove` shifts are cheap for the small documents
this editor targets, and capacity doubling gives amortised O(1) appends.
**Decision: dynamic array, capacity doubling from 8.**

## Command set (all 1-based line numbers)

| Command | Meaning |
|---|---|
| `i <n> <text>` | Insert `<text>` as line *n*, shifting lines down (`n` may be `len+1` to append) |
| `a <text>` | Append `<text>` after the last line |
| `d <n>` | Delete line *n*, shifting lines up |
| `p` | Print all lines with numbers |
| `p <n>` | Print line *n* |
| `w <file>` | Save document to `<file>` (`w` alone re-uses the last file) |
| `r <file>` | Load `<file>`, replacing the current document |
| `s <pattern>` | Search: list line numbers containing `<pattern>` |
| `f <old> <new>` | Replace every `<old>` with `<new>` in the whole document |
| `f <n> <old> <new>` | Replace on line *n* only (when first word is a valid line number) |
| `u` | Undo the last modifying action (multi-level) |
| `c` | Report line / word / character counts |
| `h` | Print a short command summary |
| `q` | Quit (warns once about unsaved changes) |

`<old>` / `<new>` are single words (no spaces) — documented limitation.
The editor can also be started as `./editor [file]` to load a file on startup.

## Function outline

- `doc_init / doc_free` — lifecycle
- `doc_reserve` — capacity doubling
- `doc_insert_raw(doc, idx, text)` / `doc_delete_raw(doc, idx)` — no undo bookkeeping
- `doc_print(doc)` / `doc_print_line(doc, idx)`
- `doc_save(doc, path)` / `doc_load(doc, path)` — returns line count or -1 on error
- `doc_search(doc, pattern)` — strstr per line
- `doc_replace(doc, old, new, line_only)` — returns changes for undo
- `doc_stats(doc, *lines, *words, *chars)`
- Undo: `undo_push_insert / undo_push_delete / undo_push_replace`,
  `undo_pop_apply(doc)`, `undo_clear`, `undo_free`
  - insert → inverse is delete; delete → inverse is re-insert of saved text;
    replace → inverse restores saved per-line texts. A stack gives multi-level undo.
- `replace_all(src, old, new, *nrep)` — string helper, counts replacements
- `main` — read lines with `getline`, parse, dispatch; all numeric input validated

## Edge cases to handle (robustness)

- Insert at 0, past `len+1`, or non-numeric → error, no crash
- Delete from empty document / bad line number → error
- Search/replace on empty document → "not found" style message
- Replace with empty `<old>` → rejected (would loop forever)
- Save/load with bad path → `perror`-style message, editor keeps running
- `q` with unsaved changes → warns once, quits on second `q`
- Unknown command → "unknown command (type h for help)"
- All heap memory freed on exit; undo history cleared on load
