CC_FLAGS := -Wall

scrutineer: scrutineer.o
	gcc ${CC_FLAGS} -o $@ $<

%.o: %.c
	gcc ${CC_FLAGS} -o $@ -c $<

clean:
	rm -f *.o scrutineer
