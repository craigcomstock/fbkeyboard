/*
 * fbkeyboard.c : framebuffer softkeyboard for touchscreen devices
 *
 * Copyright 2017 Julian Winkler <julia.winkler1@web.de>
 * Copyright 2020 Ferenc Bakonyi <bakonyi.ferenc@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <linux/vt.h>
#include <ft2build.h>
#include FT_FREETYPE_H

volatile sig_atomic_t done = 0;

char *font = "/usr/share/fonts/ttf-dejavu/DejaVuSans.ttf";
char *device = NULL;
char *special[][7] = {
	{ "Esc", "Tab", "F10", " / ", " - ", " . ", " \\ " },
	{ "Esc", "Tab", "F10", " ? ", " _ ", " > ", " | " },
};

char *layout[] = {
	"qwertyuiopasdfghjklzxcvbnm",
	"QWERTYUIOPASDFGHJKLZXCVBNM",
	"1234567890-=[];\'\\,.`/     ",
	"!@#$%^&*()_+{}:\"|<>~?     "
};

int layoutuse = 0;
int ctrllock = 0;
int altlock = 0;
__u16 keys[][26] = {
	{ KEY_ESC, KEY_TAB, KEY_F10, KEY_SLASH, KEY_MINUS, KEY_DOT, KEY_BACKSLASH },
	{ KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P,
	  KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L,
	  KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M },
	{ KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
	  KEY_MINUS, KEY_EQUAL, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_BACKSLASH, KEY_COMMA, KEY_DOT,
	  KEY_GRAVE, KEY_SLASH, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M },
	{ KEY_LEFTSHIFT, KEY_BACKSPACE },
	{ KEY_LEFTALT, KEY_SPACE, KEY_RIGHTCTRL, KEY_ENTER },
	{ KEY_HOME, KEY_UP, KEY_PAGEUP,
	  KEY_LEFT, KEY_ENTER, KEY_RIGHT,
	  KEY_END, KEY_DOWN, KEY_PAGEDOWN,
	  KEY_RIGHTSHIFT }
};

#define TOUCHCOLOR 0x4444ee
#define BUTTONCOLOR 0x111122
#define BACKLITCOLOR 0xffa500
#define TERMCOLOR 0x000000
int gap = 2;

int rotate = 0;

struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
char *buf;
unsigned int buflen;
int fbheight;	// of framebuffer
int fbwidth;	// of framebuffer
int fblinelength;	// of one line of framebuffer
int height;	// of one row of keys
int width;	// of keyboard (= width of screen)
int linelength;	// of one line of keyboard shape in bytes
int landscape;	// false = portrait

FT_Face face;
int advance;	// offset to the next glyph

int fduinput;
struct input_event ie;
int theight;	// of touchscreen
int twidth;	// of touchscreen
int trowh;	// heigth of one keyboard row on touchscreen

void fill_rect(int x, int y, int w, int h, int color)
{
	int i, j, t;
	int32_t *line;
	switch (rotate) {
		case FB_ROTATE_UR:
			break;
		case FB_ROTATE_UD:
			x = width - x - w;
			y = (height * 5) - y - h;
			break;
		case FB_ROTATE_CW:
			y = (height * 5) - y - h;
			t = w; w = h; h = t;
			t = x; x = y; y = t;
			break;
		case FB_ROTATE_CCW:
			x = width - x - w;
			t = w; w = h; h = t;
			t = x; x = y; y = t;
			break;
	}
	for (i = 0; i < h; i++) {
		line = (int32_t *) (buf + linelength * (y + i));
		for (j = 0; j < w; j++) {
			*(line + x + j) = color;
		}
	}
}

void draw_char(int x, int y, char c)
{
	int i, j, t;
	int pixel_on;
	FT_Matrix matrix;
	switch (rotate) {
		case FB_ROTATE_UR:
			FT_Load_Char(face, c, FT_LOAD_RENDER);
			x += face->glyph->bitmap_left;
			y += (face->size->metrics.ascender >> 6) - face->glyph->bitmap_top;
			advance = face->glyph->advance.x >> 6;
			break;
		case FB_ROTATE_UD:
			matrix.xx = (FT_Fixed)(-1 * 0x10000L);
			matrix.xy = (FT_Fixed)(0);
			matrix.yx = (FT_Fixed)(0);
			matrix.yy = (FT_Fixed)(-1 * 0x10000L);
			FT_Set_Transform(face, &matrix, NULL);
			FT_Load_Char(face, c, FT_LOAD_RENDER);
			x = width - x;
			y = (height * 5) - y;
			x += (face->glyph->advance.x >> 6) - face->glyph->bitmap.width - face->glyph->bitmap_left;
			y -= (face->size->metrics.ascender >> 6) + face->glyph->bitmap_top;
			advance = -(face->glyph->advance.x >> 6);
			break;
		case FB_ROTATE_CW:
			matrix.xx = (FT_Fixed)(0);
			matrix.xy = (FT_Fixed)( 1 * 0x10000L);
			matrix.yx = (FT_Fixed)(-1 * 0x10000L);
			matrix.yy = (FT_Fixed)(0);
			FT_Set_Transform(face, &matrix, NULL);
			FT_Load_Char(face, c, FT_LOAD_RENDER);
			y = (height * 5) - y;
			x -= face->glyph->bitmap_top;
			y += face->glyph->bitmap_left - (face->size->metrics.ascender >> 6);
			t = x; x = y; y = t;
			advance = -(face->glyph->advance.y >> 6);
			break;
		case FB_ROTATE_CCW:
			matrix.xx = (FT_Fixed)(0);
			matrix.xy = (FT_Fixed)(-1 * 0x10000L);
			matrix.yx = (FT_Fixed)( 1 * 0x10000L);
			matrix.yy = (FT_Fixed)(0);
			FT_Set_Transform(face, &matrix, NULL);
			FT_Load_Char(face, c, FT_LOAD_RENDER);
			x = width - x;
			x -= face->glyph->bitmap_top;
			y += face->glyph->bitmap_left + (face->size->metrics.ascender >> 6);
			t = x; x = y; y = t;
			advance = face->glyph->advance.y >> 6;
			break;
	}
	for (i = 0; i < (face->glyph->bitmap.rows); i++)
		for (j = 0; j < face->glyph->bitmap.width; j++) {
			pixel_on =
			    *(face->glyph->bitmap.buffer +
			      face->glyph->bitmap.pitch * i + j);
			if (pixel_on) {
                            *(int32_t *)(buf + linelength * (i + y) + (j + x) * 4) = BACKLITCOLOR;
			}
		}
}

void draw_text(int x, int y, char *text)
{
	while (*text) {
		draw_char(x, y, *text);
		text++;
		x += advance;
	}
}

void draw_key(int x, int y, int w, int h, int color)
{
	fill_rect(x + gap, y + gap, w - 2 * gap, 1, BACKLITCOLOR);
	fill_rect(x + gap, y + h - gap, w - 2 * gap, 1, BACKLITCOLOR);
	fill_rect(x + gap, y + gap, 1, h - 2 * gap, BACKLITCOLOR);
	fill_rect(x + w - gap, y + gap, 1, h - 2 * gap, BACKLITCOLOR);
	fill_rect(x + gap + 1, y + gap + 1, w - 2 * gap - 2,
		  h - 2 * gap - 2, color);
}

void draw_textbutton(int x, int y, int w, int h, int color, char *text)
{
	draw_key(x, y, w, h, color);
	draw_text(x + gap + 14, y + gap + 24, text);
}

void draw_button(int x, int y, int w, int h, int color, char chr)
{
	draw_key(x, y, w, h, color);
	draw_char(x + gap + 7, y + gap + 7, chr);
}

void draw_keyboard(int row, int pressed)
{
	int key;
	for (key = 0; key < 7; key++) {
		draw_textbutton(key * width / 7 + 1, 1,
				width / 7 - 1, height - 1,
				(row == 0
				 && key ==
				 pressed) ? TOUCHCOLOR :
				BUTTONCOLOR,
				special[layoutuse & 1][key]);
	}
	for (key = 0; key < 10; key++) {
		draw_button(key * width / 10 + 1, height * 1,
			    width / 10 - 1, height - 1,
			    (row == 1
			     && key ==
			     pressed) ? TOUCHCOLOR : BUTTONCOLOR,
			    layout[layoutuse][key]);
	}
	for (key = 0; key < 9; key++) {
		draw_button(width / 20 +
			    key * width / 10, height * 2,
			    width / 10 - 1, height - 1,
			    (row == 1
			     && key + 10 ==
			     pressed) ? TOUCHCOLOR : BUTTONCOLOR,
			    layout[layoutuse][key + 10]);
	}
	draw_textbutton(1, height * 3, width * 3 / 20 - 1,
			height - 1,
			(layoutuse & 1) ? TOUCHCOLOR : BUTTONCOLOR,
			"Shift");
	for (key = 0; key < 7; key++) {
		draw_button(width * 3 / 20 +
			    key * width / 10, height * 3,
			    width / 10 - 1, height - 1,
			    (row == 1
			     && key + 19 ==
			     pressed) ? TOUCHCOLOR : BUTTONCOLOR,
			    layout[layoutuse][key + 19]);
	}
	draw_textbutton(width * 17 / 20, height * 3,
			width * 3 / 20 - 1, height - 1,
			(row == 3
		 && 1 ==
			 pressed) ? TOUCHCOLOR : BUTTONCOLOR,
			"Bcksp");
	draw_textbutton(1, height * 4, width * 3 / 20 - 1,
			height - 1,
			(99 == pressed) ? TOUCHCOLOR : BUTTONCOLOR,
			(layoutuse & 2) ? "abcABC" : "123!@\"");
	draw_textbutton(width * 3 / 20, height * 4,
			width / 10 - 1, height - 1,
			(altlock) ? TOUCHCOLOR : BUTTONCOLOR,
			"Alt");
	draw_button(width / 4, height * 4, width / 2 - 1,
		    height - 1, (row == 4
				&& 1 ==
				pressed) ? TOUCHCOLOR :
		    BUTTONCOLOR, ' ');
	draw_textbutton(width * 3 / 4, height * 4,
			width / 10 - 1, height - 1,
			(ctrllock) ? TOUCHCOLOR : BUTTONCOLOR,
			"Ctrl");
	draw_textbutton(width * 17 / 20, height * 4,
			width * 3 / 20 - 1, height - 1,
			(row == 4
			 && 3 ==
			 pressed) ? TOUCHCOLOR : BUTTONCOLOR,
			"Enter");
}

void show_fbkeyboard(int fbfd)
{
	switch (rotate) {
		case FB_ROTATE_UR:
			lseek(fbfd, fblinelength * (fbheight - height * 5), SEEK_SET);
			write(fbfd, buf, buflen);
			break;
		case FB_ROTATE_UD:
			lseek(fbfd, 0, SEEK_SET);
			write(fbfd, buf, buflen);
			break;
		case FB_ROTATE_CW:
			for (int i = 0; i < width; i++) {
				lseek(fbfd, fblinelength * i, SEEK_SET);
				write(fbfd, (int32_t *) (buf + linelength * i), linelength);
			}
			break;
		case FB_ROTATE_CCW:
			for (int i = 0; i < width; i++) {
				lseek(fbfd, fblinelength * i + (fbwidth - height * 5) * 4, SEEK_SET);
				write(fbfd, (int32_t *) (buf + linelength * i), linelength);
			}
			break;
	}
}

/*
 * Waits for a relevant input event. Returns 0 if touched, 1 if released.
 */
int check_input_events(int fdinput, int *x, int *y)
{
	int released = 0;
	int key = 1;
	int absolute_x = -1, absolute_y = -1;
	while (!done && !released && (absolute_x == -1 || absolute_y == -1))
		while (read(fdinput, &ie, sizeof(struct input_event))
		       && !(ie.type == EV_SYN && ie.code == SYN_REPORT)) {
			if (ie.type == EV_ABS) {
				switch (ie.code) {
					case ABS_MT_POSITION_X:
						absolute_x = ie.value;
						released = 0;
						key = 0;
						break;
					case ABS_MT_POSITION_Y:
						absolute_y = ie.value;
						released = 0;
						key = 0;
						break;
					case ABS_MT_TRACKING_ID:
						if (ie.value == -1) {
							released = 1;
						}
						break;
				}
			}
			if (ie.type == EV_SYN && ie.code == SYN_MT_REPORT && key) {
				released = 1;
			}
		}
	switch (rotate) {
		case FB_ROTATE_UR:
			*x = absolute_x * 0x10000 / twidth;
			*y = absolute_y * 0x10000 / theight;
			break;
		case FB_ROTATE_UD:
			*x = 0x10000 - absolute_x * 0x10000 / twidth;
			*y = 0x10000 - absolute_y * 0x10000 / theight;
			break;
		case FB_ROTATE_CW:
			*x = absolute_y * 0x10000 / theight;
			*y = 0x10000 - absolute_x * 0x10000 / twidth;
			break;
		case FB_ROTATE_CCW:
			*x = 0x10000 - absolute_y * 0x10000 / theight;
			*y = absolute_x * 0x10000 / twidth;
			break;
	}
	return released;
}

/*
 * x, y and trowh are scaled to 2^16 (e.g. min x = 0, max x = 65535)
 */
void identify_touched_key(int x, int y, int *row, int *pressed)
{
	switch ((0x10000 - y) / trowh) {
		case 4:
			*row = 0;		// Esc, Tab, F10, etc
			*pressed = x * 7 / 0x10000;
			break;
		case 3:
			*row = 1;		// q - p
			*pressed = x * 10 / 0x10000;
			break;
		case 2:
			*row = 1;		// a - l
			if (x > 0x10000 / 20 && x < 0x10000 * 19 / 20)
				*pressed = 10 + (x * 10 - 0x10000 / 2) / 0x10000;
			break;
		case 1:
			if (x < 0x10000 * 3 / 20) {
				*row = 3;
				*pressed = 0;	// Left Shift
			} else if (x < 0x10000 * 17 / 20) {
				*row = 1;	// z - m
				*pressed = 19 + (x * 10 - 0x10000 * 3 / 2) / 0x10000;
			} else {
				*row = 3;
				*pressed = 1;	// Bcksp
			}
			break;
		case 0:
			*row = 4;
			if (x < 0x10000 * 3 / 20)
				*pressed = 99;	// 123!@
			else if (x < 0x10000 * 5 / 20)
				*pressed = 0;	// Left Alt
			else if (x < 0x10000 * 15 / 20)
				*pressed = 1;	// Space
			else if (x < 0x10000 * 17 / 20)
				*pressed = 2;	// Right Ctrl
			else
				*pressed = 3;	// Enter
			break;
		default:
			*row = 5;		// cursor, Enter, Home, PgDn, etc
			*pressed = 3 * y / (0x10000 - trowh * 5);
			*pressed *= 3;
			*pressed += 3 * x / 0x10000;
			break;
	}
}

void send_key(__u16 code)
{
	ie.type = EV_KEY;
	ie.code = code;
	ie.value = 1;
	if (write(fduinput, &ie, sizeof(ie)) != sizeof(ie))
		fprintf(stderr, "error: sending uinput event\n");
	ie.value = 0;
	if (write(fduinput, &ie, sizeof(ie)) != sizeof(ie))
		fprintf(stderr, "error: sending uinput event\n");
//	ie.code = KEY_RIGHTCTRL;
//	if(write(fduinput, &ie, sizeof(ie)) != sizeof(ie))
//		fprintf(stderr, "error: sending uinput event\n");
	ie.type = EV_SYN;
	ie.code = SYN_REPORT;
	if (write(fduinput, &ie, sizeof(ie)) != sizeof(ie))
		fprintf(stderr, "error: sending uinput event\n");
}

void send_uinput_event(int row, int pressed)
{
	if (pressed == 99)	// second page
		layoutuse ^= 2;
	else if (row == 1) {	// normal keys (abc, 123, !@#)
		send_key(keys[row + (layoutuse >> 1)]
			 [pressed]);
	} else if (row == 3 && pressed == 0) {	// Left Shift
		layoutuse ^= 1;
		ie.type = EV_KEY;
		ie.code = KEY_LEFTSHIFT;
		ie.value = layoutuse & 1;
		if (write(fduinput, &ie, sizeof(ie)) !=
		    sizeof(ie))
			fprintf(stderr,
				"error sending uinput event\n");
	} else if (row == 4 && pressed == 0) {	// Left Alt
		altlock ^= 1;
		ie.type = EV_KEY;
		ie.code = KEY_LEFTALT;
		ie.value = altlock;
		if (write(fduinput, &ie, sizeof(ie)) !=
		    sizeof(ie))
			fprintf(stderr,
				"error sending uinput event\n");
	} else if (row == 4 && pressed == 2) {	// Right Ctrl
		ctrllock ^= 1;
		ie.type = EV_KEY;
		ie.code = KEY_RIGHTCTRL;
		ie.value = ctrllock;
		if (write(fduinput, &ie, sizeof(ie)) !=
		    sizeof(ie))
			fprintf(stderr,
				"error sending uinput event\n");
	} else {
		send_key(keys[row][pressed]);
	}
}

/*
 * return max of rows
 */
int reset_window_size(int fd)
{
	struct winsize win = { 0, 0, 0, 0 };

	if (ioctl(fd, TIOCGWINSZ, &win)) {
		if (errno != EINVAL) {
			perror("error resetting window size");
			return 0;
		}
		memset(&win, 0, sizeof(win));
	}

	win.ws_row += 2;
	if (!ioctl(fd, TIOCSWINSZ, (char *) &win)) {
		do {
			win.ws_row *= landscape ? 4 : 3;
			win.ws_row /= 2;
		} while (!ioctl(fd, TIOCSWINSZ, (char *) &win));
		do {
			win.ws_row--;
		} while (ioctl(fd, TIOCSWINSZ, (char *) &win));
	}
	return win.ws_row;
}

void set_window_size(int fd)
{
	struct winsize win = { 0, 0, 0, 0 };
	int rows;

	rows = reset_window_size(fd);
	if (ioctl(fd, TIOCGWINSZ, &win)) {
		if (errno != EINVAL)
			goto bail;
		memset(&win, 0, sizeof(win));
	}

	win.ws_row *= 2;
	win.ws_row /= landscape ? 4 : 3;
	if (ioctl(fd, TIOCSWINSZ, (char *) &win))
bail:
		perror("error setting window size");
}

void term(int signum)
{
	done = 1;
}

int main(int argc, char *argv[])
{
	char *p = NULL;
	int fbfd, fdinput, fdcons;
	int tty = 0;
	int resized[MAX_NR_CONSOLES + 1];
	struct input_absinfo abs_x, abs_y;
	FT_Library library;
	int x, y, row, pressed = -1, released, key;

	struct sigaction action;
	struct vt_stat ttyinfo;

	memset(&resized, 0, sizeof(resized));

	fdcons = open("/dev/tty0", O_RDWR | O_NOCTTY);
	if (fdcons < 0) {
		perror("Error opening /dev/tty0");
		exit(-1);
	}

	memset(&action, 0, sizeof(action));
	action.sa_handler = term;
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);

	char c;
	while ((c = getopt(argc, argv, "d:f:r:h")) != (char) -1) {
		switch (c) {
		case 'd':
			device = optarg;
			break;
		case 'f':
			font = optarg;
			break;
		case 'r':
			errno = 0;
			rotate = strtol(optarg, &p, 10) % 4;
			if (errno != 0 || p == optarg || p == NULL || *p != '\0') {
				printf("Invalid numeric value for -r option\n");
				exit(0);
			}
			break;
		case 'h':
			printf("usage: %s [options]\npossible options are:\n -h: print this help\n -d: set path to inputdevice\n -f: set path to font\n -r: set rotation\n",
			     argv[0]);
			exit(0);
			break;
		case '?':
			fprintf(stderr, "unrecognized option -%c\n",
				optopt);
			break;
		}
	}

	fbfd = open("/dev/fb0", O_RDWR);
	if (fbfd == -1) {
		perror("error: opening framebuffer device /dev/fb0");
		exit(-1);
	}
	if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1) {
		perror("error: reading fixed framebuffer information");
		exit(-1);
	}
	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
		perror("error: reading variable framebuffer information");
		exit(-1);
	}
	fbwidth = vinfo.xres;
	fbheight = vinfo.yres;
	fblinelength = finfo.line_length;
	switch (rotate) {
		case FB_ROTATE_UR:
		case FB_ROTATE_UD:
			landscape = fbheight < fbwidth;
			width = fbwidth;
			height = fbheight / (landscape ? 2 : 3) / 5;	// height of one row
			trowh = height * 0x10000 / fbheight;
			linelength = fblinelength;
			buflen = linelength * (height * 5 + 1);
			break;
		case FB_ROTATE_CW:
		case FB_ROTATE_CCW:
			landscape = fbheight > fbwidth;
			width = fbheight;
			height = fbwidth / (landscape ? 2 : 3) / 5;	// height of one row
			trowh = height * 0x10000 / fbwidth;
			linelength = height * 5 * 4;
			buflen = width * 4 * (height * 5 + 1);
			break;
	}

	if (FT_Init_FreeType(&library)) {
		perror("error: freetype initialization");
		exit(-1);
	}
	if (FT_New_Face(library, font, 0, &face)) {
		perror("unable to load font file");
		exit(-1);
	}
	if (FT_Set_Pixel_Sizes(face, height * 1 / 4, height * 1 / 4)) {
		perror("FT_Set_Pixel_Sizes failed");
		exit(-1);
	}

	if (device) {
		if ((fdinput = open(device, O_RDONLY)) == -1) {
			perror("failed to open input device node");
			exit(-1);
		}
	} else {
		DIR *inputdevs = opendir("/dev/input");
		struct dirent *dptr;
		fdinput = -1;
		while ((dptr = readdir(inputdevs))) {
			if ((fdinput =
			     openat(dirfd(inputdevs), dptr->d_name,
				    O_RDONLY)) != -1
			    && ioctl(fdinput, EVIOCGBIT(0, sizeof(key)),
				     &key) != -1 && key >> EV_ABS & 1)
				break;
			if (fdinput != -1) {
				close(fdinput);
				fdinput = -1;
			}
		}
		if (fdinput == -1) {
			fprintf(stderr,
				"no absolute axes device found in /dev/input\n");
			exit(-1);
		}
	}
	if ((ioctl(fdinput, EVIOCGABS(ABS_MT_POSITION_X), &abs_x) == -1) ||
	    (ioctl(fdinput, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y) == -1)) {
		perror("error: getting touchscreen size");
		exit(-1);
	}
	twidth = abs_x.maximum;
	theight = abs_y.maximum;

	fduinput = open("/dev/uinput", O_WRONLY);
	if (fduinput == -1) {
		perror("error: cannot open uinput device /dev/uinput");
		exit(-1);
	}
	if (ioctl(fduinput, UI_SET_EVBIT, EV_KEY) == -1) {
		perror("error: SET_EVBIT EV_KEY");
		exit(-1);
	}
	if (ioctl(fduinput, UI_SET_EVBIT, EV_SYN) == -1) {
		perror("error: SET_EVBIT EV_SYN");
		exit(-1);
	}
	for (row = 0; row < 6; row++)
		for (key = 0; key < sizeof(keys[row]); key++)
			ioctl(fduinput, UI_SET_KEYBIT, keys[row][key]);
	struct uinput_user_dev uidev;
	memset(&uidev, 0, sizeof(uidev));
	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "fbkeyboard");
	uidev.id.bustype = BUS_USB;
	uidev.id.vendor = 1;
	uidev.id.product = 1;
	uidev.id.version = 1;
	if (write(fduinput, &uidev, sizeof(uidev)) != sizeof(uidev)) {
		fprintf(stderr, "error setting up uinput\n");
		exit(-1);
	}
	if (ioctl(fduinput, UI_DEV_CREATE) == -1) {
		perror("error creating uinput dev");
		exit(-1);
	}

	buf = malloc(buflen);
	if (buf == 0) {
		perror("malloc failed");
		exit(-1);
	}
	fill_rect(0, 0, width - 1, height * 5, TERMCOLOR);


	while (!done) {
		if (!ioctl(fdcons, VT_GETSTATE, &ttyinfo)) {
			if (tty != ttyinfo.v_active) {
				tty = ttyinfo.v_active;
				close(fdcons);
				fdcons = open("/dev/tty0", O_RDWR | O_NOCTTY);
				set_window_size(fdcons);
				resized[tty] = 1;
			}
		} else {
			perror("VT_GETSTATE ioctl failed");
		}

		draw_keyboard(row, pressed);
		show_fbkeyboard(fbfd);

		released = check_input_events(fdinput, &x, &y);
		if (released && pressed != -1)
			send_uinput_event(row, pressed);

		pressed = -1;
		if (!released)
			identify_touched_key(x, y, &row, &pressed);
	}

	int i;
	char buf[12];
	for (i = 1; i <= MAX_NR_CONSOLES; i++) {
		snprintf(buf, 12, "/dev/tty%d", i);
		if (resized[i]) {
			close(fdcons);
			fdcons = open(buf, O_RDWR | O_NOCTTY);
			if (fdcons < 0) {
				perror("Error opening /dev/tty[i]");
				exit(-1);
			}
			reset_window_size(fdcons);
		}
	}
}
