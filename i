#gcc --verbose -o t -I/usr/include/freetype2 -I/usr/include/libpng16 -L/lib/x86_64-linux-gnu -lfreetype t.c
# debian-12 libre (x220 tablet)
#sudo ./fbkeyboard -d /dev/input/event3
# alpine edge :)
#sudo ./fbkeyboard -d /dev/input/event5 -f /usr/share/fonts/dejavu/DejaVuSans.ttf
command_args="-r $(cat /sys/class/graphics/fbcon/rotate) -f /usr/share/fonts/dejavu/DejaVuSans.ttf"
sudo modprobe uinput
sudo ./fbkeyboard -d /dev/input/event5 $command_args
