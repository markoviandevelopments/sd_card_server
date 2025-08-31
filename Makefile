# Makefile for compiling C programs
CC = gcc
CFLAGS = -Wall -Wextra
LIBS = -lcurl
TARGETS = 2sd_card_client write_random_bits

all: $(TARGETS)

2sd_card_client: 2sd_card_client.c
	$(CC) $(CFLAGS) 2sd_card_client.c -o 2sd_card_client $(LIBS)

write_random_bits: write_random_bits.c
	$(CC) $(CFLAGS) write_random_bits.c -o write_random_bits $(LIBS)

clean:
	rm -f $(TARGETS)
