CC = gcc
CFLAGS = -Wall -Wextra -Iinclude

shell: src/shell.c src/prompt.c
	$(CC) $(CFLAGS) src/shell.c src/prompt.c -o shell

clean:
	rm -f shell