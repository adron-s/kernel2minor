all:
	gcc kernel2minor.c -o kernel2minor

clean:
	rm -f *.o *.bin kernel2minor
