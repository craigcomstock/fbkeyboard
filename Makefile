fbkeyboard: fbkeyboard.c
	gcc -o fbkeyboard fbkeyboard.c $(shell pkg-config freetype2 --cflags --libs) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f fbkeyboard

