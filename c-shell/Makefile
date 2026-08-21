CC = gcc
CFLAGS = -Wall -Wextra -Iinclude

shell: src/shell.c src/prompt.c src/lexer.c
	$(CC) $(CFLAGS) src/shell.c src/prompt.c src/lexer.c src/parser.c src/hop.c -o shell

clean:
	rm -f shell