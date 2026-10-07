CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L

editor: editor.c
	$(CC) $(CFLAGS) -o editor editor.c

clean:
	rm -f editor

.PHONY: clean
