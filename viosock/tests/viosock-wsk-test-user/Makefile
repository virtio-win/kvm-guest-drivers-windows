CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra

wsk_test_user: wsk_test_user.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f wsk_test_user

.PHONY: clean
