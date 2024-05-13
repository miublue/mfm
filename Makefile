
OUT = ./mfm
LIB = -lncurses
SRC = ./src/*.c ../mutils/*.c
INC = -I ./src -I ../mutils

all:
	tcc -o $(OUT) $(SRC) $(INC) $(LIB)

install: all
	mv $(OUT) ~/bin/
