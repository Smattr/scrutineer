scrutineer: scrutineer.o
	gcc -o $@ $<

%.o: %.c
	gcc -o $@ -c $<

clean:
	rm *.o scrutineer
