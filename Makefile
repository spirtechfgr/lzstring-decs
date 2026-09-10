# decs - see README.md. Under MSYS2 use make PY=py EXE=.exe
# make        builds decscli, runs its built-in checks then the known answer tests
# make lint   compiles decs.h alone with strict warnings
# make clean  removes the binary and Python's cache

CC     = gcc
CFLAGS = -std=c11 -Wall -Wextra -pedantic -O2
PY     = python3
EXE    =

all: decscli$(EXE)
	./decscli$(EXE) -t
	$(PY) katcheck.py ./decscli$(EXE)

decscli$(EXE): decscli.c decs.h
	$(CC) $(CFLAGS) -DSELFTEST -o $@ decscli.c

lint: decs.h
	$(CC) $(CFLAGS) -Wconversion -Wshadow -fsyntax-only -x c decs.h

clean:
	rm -rf decscli$(EXE) __pycache__

.PHONY: all lint clean
