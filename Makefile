# PARI のヘッダパスは環境に合わせて変更
# <pari/pari.h> を解決するには pari/ ディレクトリの親を指定する
PARI_INCLUDE = /home/iwao/include
PARI_LIB = /home/iwao/lib
LEAN_INCLUDE  = $(shell lean --print-prefix)/include

LDFLAGS = -L$(PARI_LIB) -lpari -llean
CFLAGS = -O2 -fPIC -I$(PARI_INCLUDE) -I$(LEAN_INCLUDE)

.PHONY: all clean

C_OBJS = c/pari_lean.o c/pari_ell_lean.o

all: $(C_OBJS)
	lake build

c/pari_lean.o: c/pari_lean.c c/pari_lean_internal.h
	gcc $(CFLAGS) -c $< -o $@ $(LDFLAGS)

c/pari_ell_lean.o: c/pari_ell_lean.c c/pari_lean_internal.h
	gcc $(CFLAGS) -c $< -o $@ $(LDFLAGS)

clean:
	rm -f $(C_OBJS)
	lake clean
