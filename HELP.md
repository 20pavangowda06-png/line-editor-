# Line Editor — Help

A simple command-line line editor. You type a command, the editor acts on the
document, and the result is printed back. All line numbers are **1-based**.
Run it with `./editor`, or `./editor <file>` to load a file on startup.

## Core commands

### `i <n> <text>` — insert a line
Inserts `<text>` as line `n`, shifting existing lines down.
`n` may be one past the last line to append at the end.

```
i 1 Hello, world      ->  inserted at line 1
i 2 Second line       ->  inserted at line 2
```

### `a <text>` — append a line
Adds `<text>` after the last line (shorthand for `i <len+1> <text>`).

```
a Third line          ->  appended as line 3
```

### `d <n>` — delete a line
Removes line `n`, shifting the lines below it up.

```
d 2                   ->  deleted line 2: Second line
```

### `p` — display the document
Prints every line with its line number. `p <n>` prints just line `n`.

```
p                     ->     1  Hello, world
                                3  Third line
```

### `w <file>` — save the document
Writes all lines to `<file>`. After a `w` or `r`, a bare `w` re-uses the
last file, so you can just type `w` to save again.

```
w notes.txt           ->  wrote 3 line(s) to 'notes.txt'
w                     ->  wrote 3 line(s) to 'notes.txt'
```

### `r <file>` — load a file
Reads `<file>`, replacing the current document (and clearing undo history).

```
r notes.txt           ->  read 3 line(s) from 'notes.txt'
```

## Bonus commands

### `s <pattern>` — search
Lists every line containing `<pattern>` (the pattern may contain spaces).

```
s world               ->  line 1: Hello, world
```

### `f <old> <new>` — find & replace
Replaces every occurrence of `<old>` with `<new>` in the whole document.
With a line number — `f <n> <old> <new>` — only line `n` is changed.
`<old>` and `<new>` are single words (no spaces).

```
f world there         ->  replaced 1 occurrence(s) on 1 line(s)
f 1 Hello Hi          ->  replaced 1 occurrence(s) on line 1
```

### `u` — undo
Reverses the last change (insert, delete, or replace). Repeat `u` to undo
further back — the history is multi-level.

```
u                     ->  undone: restored 1 replaced line(s)
```

### `c` — counts
Reports line, word, and character totals.

```
c                     ->  3 line(s), 6 word(s), 32 character(s)
```

## Misc

### `h` — help
Prints a short summary of all commands.

### `q` — quit
Quits the editor. If you have unsaved changes, the first `q` only warns you —
type `q` again to quit anyway.

## Example session

```
$ ./editor
i 1 Buy milk
i 2 Walk the dog
p
   1  Buy milk
   2  Walk the dog
f dog cat
replaced 1 occurrence(s) on 1 line(s)
s cat
line 2: Walk the cat
w todo.txt
wrote 2 line(s) to 'todo.txt'
q
$
```
