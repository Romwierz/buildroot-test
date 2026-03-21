CC=$(CROSS_COMPILE)gcc
MY_CFLAGS=$(filter-out -g0, $(CFLAGS))
MY_CFLAGS+=-g

all: my-app

my-app : my-app.c
	$(CC) $(MY_CFLAGS) -o my-app my-app.c
