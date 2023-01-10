NOM=scat

all: $(NOM).o
	gcc -o $(NOM) $(NOM).o -lpcsclite -lreadline

$(NOM).o: $(NOM).c
	gcc -c -Wall -Os -I/usr/include/PCSC $(NOM).c

clean:
	rm -f $(NOM) $(NOM).o

run: $(NOM)
	./$(NOM)


