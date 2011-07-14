CC := gcc
CC_FLAGS := -Wall -Wextra
DEFINES :=

ifeq (${CC},gcc)
    # We can enable the library strndup.
    DEFINES += -D_GNU_SOURCE
endif

scrutineer: scrutineer.o
	${CC} ${CC_FLAGS} ${DEFINES} -o $@ $<

%.o: %.c
	${CC} ${CC_FLAGS} ${DEFINES} -o $@ -c $<

clean:
	rm -f *.o scrutineer
