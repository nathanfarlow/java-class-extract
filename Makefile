classextract: main.c
	$(CC) -O3 main.c -o classextract

clean:
	rm -f classextract
