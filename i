#gcc --verbose -o t -I/usr/include/freetype2 -I/usr/include/libpng16 -L/lib/x86_64-linux-gnu -lfreetype t.c
# debian-12 libre (x220 tablet)
#sudo ./fbkeyboard -d /dev/input/event3
# alpine edge :)
sudo modprobe uinput
#sudo ./fbkeyboard -d /dev/input/event5 -f /usr/share/fonts/dejavu/DejaVuSans.ttf
sudo fbkeyboard -f /usr/share/fonts/dejavu/DejaVuSans.ttf 
