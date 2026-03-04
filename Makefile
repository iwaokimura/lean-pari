# PARI のヘッダパスは環境に合わせて変更
# <pari/pari.h> を解決するには pari/ ディレクトリの親を指定する
PARI_INCLUDE = /home/iwao/include
PARI_LIB = /home/iwao/lib
LEAN_INCLUDE  = $(shell lean --print-prefix)/include

LDFLAGS = -L$(PARI_LIB) -lpari -llean
CFLAGS = -O2 -fPIC -I$(PARI_INCLUDE) -I$(LEAN_INCLUDE)

.PHONY: all clean

all: c/pari_lean.o
	lake build

c/pari_lean.o: c/pari_lean.c
	gcc $(CFLAGS) -c $< -o $@ $(LDFLAGS)

clean:
	rm -f c/pari_lean.o
	lake clean
