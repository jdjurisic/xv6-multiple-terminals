#include "types.h"
#include "x86.h"
#include "defs.h"
#include "kbd.h"

int
kbdgetc(void)
{
	static uint shift;
	static uchar *charcode[4] = {
		normalmap, shiftmap, ctlmap, ctlmap
	};
	uint st, data, c;

	st = inb(KBSTATP);
	if((st & KBS_DIB) == 0)
		return -1;
	data = inb(KBDATAP);

	if(data == 0xE0){
		shift |= E0ESC;
		return 0;
	} else if(data & 0x80){
		// Key released
		data = (shift & E0ESC ? data : data & 0x7F);
		shift &= ~(shiftcode[data] | E0ESC);
		return 0;
	} else if(shift & E0ESC){
		// Last character was an E0 escape; or with 0x80
		data |= 0x80;
		shift &= ~E0ESC;
	}

	shift |= shiftcode[data];
	shift ^= togglecode[data];
	c = charcode[shift & (CTL | SHIFT)][data];
	if(shift & CAPSLOCK){
		if('a' <= c && c <= 'z')
			c += 'A' - 'a';
		else if('A' <= c && c <= 'Z')
			c += 'a' - 'A';
	}

	// A(x) (x + 100)
	if(shift & ALT){
		switch(c){
			case('1'):
				c = '1' + 100;
				break;
			case('2'):
				c = '2' + 100;
				break;
			case('3'):
				c = '3' + 100;
				break;
			case('4'):
				c = '4' + 100;
				break;
			case('5'):
				c = '5' + 100;
				break;
			case('6'):
				c = '6' + 100;
				break;
			}
	}

	return c;
}

void
kbdintr(void)
{
	consoleintr(kbdgetc);
}
