// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

// Project 

// multiple terminals

#define KEY_UP               0xE2
#define KEY_DN               0xE3
#define INPUT_BUF 128
#define C(x)  ((x)-'@')  // Control - x
#define A(x)  (x + 100) //  Alt - {1,2,3,4,5,6}

static char footers[6][7] = {"[tty 1]", "[tty 2]", "[tty 3]", "[tty 4]", "[tty 5]", "[tty 6]"};
static char terminals[6][2000];
static int currentTerminal = 0;
static int currentPos[6] = {0,0,0,0,0,0};

struct {
	char buf[6][INPUT_BUF];
	uint r[6];  // Read index
	uint w[6];  // Write index
	uint e[6];  // Edit index
	int  color_fg[6];
	int  color_bg[6];
} input;

void saveTerminal(int);
void writeTerminal(int);
void cursorSetter(int);
void initColors();
void initFooters();
void printFooter(int);

// colour f-s
void fgColorPicker(char *);
void bgColorPicker(char *);

// comand history
#define MAX_HISTORY          (9)
#define MAX_COMMAND_LENGTH   (128)

static char commandHistory[MAX_HISTORY][MAX_COMMAND_LENGTH];
static int commandCounter = 0;
static int currentCommand = 0;

void remove_char();

// Project

static void consputc(int);

static int panicked = 0;

static struct {
	struct spinlock lock;
	int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
	static char digits[] = "0123456789abcdef";
	char buf[16];
	int i;
	uint x;

	if(sign && (sign = xx < 0))
		x = -xx;
	else
		x = xx;

	i = 0;
	do{
		buf[i++] = digits[x % base];
	}while((x /= base) != 0);

	if(sign)
		buf[i++] = '-';

	while(--i >= 0)
		consputc(buf[i]);
}

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
	int i, c, locking;
	uint *argp;
	char *s;

	locking = cons.locking;
	if(locking)
		acquire(&cons.lock);

	if (fmt == 0)
		panic("null fmt");

	argp = (uint*)(void*)(&fmt + 1);
	for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
		if(c != '%'){
			consputc(c);
			continue;
		}
		c = fmt[++i] & 0xff;
		if(c == 0)
			break;
		switch(c){
		case 'd':
			printint(*argp++, 10, 1);
			break;
		case 'x':
		case 'p':
			printint(*argp++, 16, 0);
			break;
		case 's':
			if((s = (char*)*argp++) == 0)
				s = "(null)";
			for(; *s; s++)
				consputc(*s);
			break;
		case '%':
			consputc('%');
			break;
		default:
			// Print unknown % sequence to draw attention.
			consputc('%');
			consputc(c);
			break;
		}
	}

	if(locking)
		release(&cons.lock);
}

void
panic(char *s)
{
	int i;
	uint pcs[10];

	cli();
	cons.locking = 0;
	// use lapiccpunum so that we can call panic from mycpu()
	cprintf("lapicid %d: panic: ", lapicid());
	cprintf(s);
	cprintf("\n");
	getcallerpcs(&s, pcs);
	for(i=0; i<10; i++)
		cprintf(" %p", pcs[i]);
	panicked = 1; // freeze other CPU
	for(;;)
		;
}

#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory


static void
cgaputc(int c)
{
	int pos;

	// Cursor position: col + 80*row.
	outb(CRTPORT, 14);
	pos = inb(CRTPORT+1) << 8;
	outb(CRTPORT, 15);
	pos |= inb(CRTPORT+1);

	if(c == '\n')
		pos += 80 - pos%80;
	else if(c == BACKSPACE){
		if(pos > 0) --pos;
	} else
		crt[pos++] = (c&0xff) | (input.color_fg[currentTerminal] + input.color_bg[currentTerminal]);

	if(pos < 0 || pos > 25*80)
		panic("pos under/overflow");

	if((pos/80) >= 24){  // Scroll up.
		memmove(crt, crt+80, sizeof(crt[0])*23*80);
		pos -= 80;
		memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));

		// add color 
		for(int i = 1840; i < 2000; i++){
			crt[i] |= (input.color_fg[currentTerminal] + input.color_bg[currentTerminal]);
		}
	}

	outb(CRTPORT, 14);
	outb(CRTPORT+1, pos>>8);
	outb(CRTPORT, 15);
	outb(CRTPORT+1, pos);
	crt[pos] = ' ' | (input.color_fg[currentTerminal] + input.color_bg[currentTerminal]);
	currentPos[currentTerminal] = pos;
}

void
consputc(int c)
{
	if(panicked){
		cli();
		for(;;)
			;
	}

	if(c == BACKSPACE){
		uartputc('\b'); uartputc(' '); uartputc('\b');
	} else
		uartputc(c);
	cgaputc(c);
}

void
consoleintr(int (*getc)(void))
{
	int c, doprocdump = 0;
	int j = 0;

	acquire(&cons.lock);
	while((c = getc()) >= 0){
		switch(c){
		case A('1'):
			if (currentTerminal != 0){
			saveTerminal(currentTerminal);
			writeTerminal(0);
			}
			break;
		case A('2'):
			if (currentTerminal != 1){
			saveTerminal(currentTerminal);
			writeTerminal(1);
			}
			break;
		case A('3'):
			if (currentTerminal != 2){
			saveTerminal(currentTerminal);
			writeTerminal(2);
			}
			break;
		case A('4'):
			if (currentTerminal != 3){
			saveTerminal(currentTerminal);
			writeTerminal(3);
			}
			break;
		case A('5'):
			if (currentTerminal != 4){
			saveTerminal(currentTerminal);
			writeTerminal(4);
			}
			break;
		case A('6'):
			if (currentTerminal != 5){
			saveTerminal(currentTerminal);
			writeTerminal(5);
			}
			break;		
		case KEY_DN:
			if(currentTerminal != 0)break;
			while(input.e[0] > input.w[0]){
				input.e[0]--;
				remove_char();
			}

			if(currentCommand == MAX_HISTORY-1) currentCommand = MAX_HISTORY-1; 
			else currentCommand++;

			while(commandHistory[currentCommand][j] != '\0'){
				consputc(commandHistory[currentCommand][j]);
				input.buf[0][input.e[0]++] = commandHistory[currentCommand][j];
				j++;
			}

			break;
		case KEY_UP:
			if(currentTerminal != 0)break;
			while(input.e[0] > input.w[0]){
				input.e[0]--;
				remove_char();
			}

			if(currentCommand == 0) currentCommand = 0; 
			else currentCommand--;
			
			while(commandHistory[currentCommand][j] != '\0'){
				consputc(commandHistory[currentCommand][j]);
				input.buf[0][input.e[0]++] = commandHistory[currentCommand][j];
				j++;
			}

			break;
		case C('P'):  // Process listing.
			// procdump() locks cons.lock indirectly; invoke later
			doprocdump = 1;
			break;
		case C('U'):  // Kill line.
			while(input.e[currentTerminal] != input.w[currentTerminal] &&
			      input.buf[currentTerminal][(input.e[currentTerminal]-1) % INPUT_BUF] != '\n'){
				input.e[currentTerminal]--;
				consputc(BACKSPACE);
			}
			break;
		case C('H'): case '\x7f':  // Backspace
			if(input.e[currentTerminal] != input.w[currentTerminal]){
				input.e[currentTerminal]--;
				consputc(BACKSPACE);
			}
			break;
		default:
			if(c != 0 && input.e[currentTerminal]-input.r[currentTerminal] < INPUT_BUF){
				c = (c == '\r') ? '\n' : c;
				input.buf[currentTerminal][input.e[currentTerminal]++ % INPUT_BUF] = c;
				consputc(c);
				if(c == '\n' || c == C('D') || input.e[currentTerminal] == input.r[currentTerminal]+INPUT_BUF){
					
					// adding history input
					if(input.w[0] != input.e[0]-1 && currentTerminal == 0){

					int k = 0;
					for(int i = input.w[0]; i < input.e[0]-1; i++){
						commandHistory[commandCounter][k] = input.buf[0][i % INPUT_BUF];
						 k++;
					}
					commandHistory[commandCounter][(input.e[0]-1-input.w[0]) % INPUT_BUF] = '\0';
					commandCounter = (commandCounter + 1) % MAX_HISTORY;
					// shift backwards
					if(!commandCounter){
						for(int k = 0; k < MAX_HISTORY - 1; k++){
							for(int j = 0; j < MAX_COMMAND_LENGTH; j++){
								commandHistory[k][j] = commandHistory[k+1][j];
							}
						}
						commandCounter = MAX_HISTORY - 1;
					}
				
					currentCommand = commandCounter;
					}
					// history 
					
					input.w[currentTerminal] = input.e[currentTerminal];
					wakeup(&input.r[currentTerminal]);
				}
			}
			break;
		}
	}

	release(&cons.lock);
	if(doprocdump) {
		procdump();  // now call procdump() wo. cons.lock held
	}
}

int
consoleread(struct inode *ip, char *dst, int n)
{
	uint target;
	int c;

	iunlock(ip);
	target = n;
	acquire(&cons.lock);
	while(n > 0){
		while(input.r[currentTerminal] == input.w[currentTerminal]){
			if(myproc()->killed){
				release(&cons.lock);
				ilock(ip);
				return -1;
			}
			sleep(&input.r[ip->minor-1], &cons.lock);
		}
		c = input.buf[currentTerminal][input.r[currentTerminal]++ % INPUT_BUF];
		if(c == C('D')){  // EOF
			if(n < target){
				// Save ^D for next time, to make sure
				// caller gets a 0-byte result.
				input.r[currentTerminal]--;
			}
			break;
		}
		*dst++ = c;
		--n;
		if(c == '\n')
			break;
	}
	release(&cons.lock);
	ilock(ip);

	return target - n;
}

int
consolewrite(struct inode *ip, char *buf, int n)
{
	int i;

	if (ip->minor-1 != currentTerminal)
	{
		if (buf[0] == 10)
		{
			 	currentPos[ip->minor-1] += 80 - (currentPos[ip->minor-1] % 80);
			 	return n;
		}
		else 
		if(buf[0] == BACKSPACE)
		{
			if(currentPos[ip->minor-1] > 0) --currentPos[ip->minor-1];
			return n;
		}
		terminals[ip->minor-1][currentPos[ip->minor-1]] = buf[0];
		currentPos[ip->minor-1]++;

		if((currentPos[ip->minor-1]/80) >= 23)
		{  // Scroll up.
		memmove(terminals[ip->minor-1], terminals[ip->minor-1]+80, sizeof(terminals[0][0])*23*80);
		currentPos[ip->minor-1] -= 80;
		memset(terminals[ip->minor-1]+currentPos[ip->minor-1], 0, sizeof(terminals[0][0])*(24*80 - currentPos[ip->minor-1]));
		}	
		return n;
	}

	iunlock(ip);
	acquire(&cons.lock);
	for(i = 0; i < n; i++)
		consputc(buf[i] & 0xff);
	release(&cons.lock);
	ilock(ip);

	return n;
}

void
consoleinit(void)
{
	initlock(&cons.lock, "console");

	devsw[CONSOLE].write = consolewrite;
	devsw[CONSOLE].read = consoleread;
	cons.locking = 1;

	initColors();
	initFooters();

	ioapicenable(IRQ_KBD, 0);
}

void saveTerminal(int n){
	for (int i = 0; i < 2000; i++){
		terminals[n][i] = crt[i];
	}
}


void writeTerminal(int n){

	currentTerminal = n;
	
	int i;
	for (i = 0; i < 2000; i++){
		crt[i] = terminals[n][i] | (input.color_fg[currentTerminal] + input.color_bg[currentTerminal]);
	} 

	cursorSetter(currentPos[currentTerminal]);
}

void cursorSetter (int n){

	outb(CRTPORT, 14);
	outb(CRTPORT+1, n>>8);
	outb(CRTPORT, 15);
	outb(CRTPORT+1, n);
	crt[n] = ' ' | (input.color_fg[currentTerminal] + input.color_bg[currentTerminal]);   

}

void initFooters(){
	for(int i = 0; i < 6; i++){
		printFooter(i);
	}
}

void printFooter(int k){

	int i,j = 0;

	// tty1 footer init
	if(k == 0)
	{
			for(i = 1992; i < 1999; i++)
			{
	    	crt[i] = (footers[currentTerminal][j]&0xff) | (input.color_fg[currentTerminal] + input.color_bg[currentTerminal]);
			j++;
			}
	}

	// tty2-6 footer init
	else
	{
	
			for(i = 1992; i < 1999; i++)
			{
			terminals[k][i] = (footers[k][j]&0xff) | (input.color_fg[currentTerminal] + input.color_bg[currentTerminal]);
			j++;
			}
	}
}

void initColors(){

	input.color_bg[0] = 0x0000;
	input.color_fg[0] = 0x0700;

	input.color_bg[1] = 0xe000;
	input.color_fg[1] = 0x0100;
	
	input.color_bg[2] = 0x5000;
	input.color_fg[2] = 0x0a00;
	
	input.color_bg[3] = 0x7000;
	input.color_fg[3] = 0x0f00;

	input.color_bg[4] = 0x4000;
	input.color_fg[4] = 0x0b00;

	input.color_bg[5] = 0x0000;
	input.color_fg[5] = 0x0400;

}

// colour functions
int strcmp(char *strg1, char *strg2)
{

    while( ( *strg1 != '\0' && *strg2 != '\0' ) && *strg1 == *strg2 )
    {
        strg1++;
        strg2++;
    }

    if(*strg1 == *strg2)
    {
        return 0; // strings are identical
    }

    else
    {
        return *strg1 - *strg2;
    }
}


int 
sys_rstclr(void){
 	
 	input.color_fg[currentTerminal] = 0x0700;
 	input.color_bg[currentTerminal] = 0x0000;
 	
 	for(int i=0; i < 2000; i++){
 		crt[i] = (crt[i]&0xff) | (input.color_fg[currentTerminal] + input.color_bg[currentTerminal]);
 	}

 	return 0;
}


int 
sys_setfg(void){
 	
 	char *color;

 	if(argstr(0, &color) < 0) return -1;
 	fgColorPicker(color);
 	return 1;

}

int 
sys_setbg(void){

 	char *color;

 	if(argstr(0, &color) < 0) return -1;
 	bgColorPicker(color);
 	return 1;
}

// makes a number from two ascii hexa characters
int ahex2int(char a, char b){

    a = (a <= '9') ? a - '0' : (a & 0x7) + 9;
    b = (b <= '9') ? b - '0' : (b & 0x7) + 9;

    return (a << 4) + b;
}

int 
sys_sethex(void){

 	char *color;
 	int fg,bg;

 	if(argstr(0, &color) < 0) return -1;

 	/* color 0 x _ _
 	             / \
 	            /   \
 	           /     \
 	          /       \
 	   color[2]       color[3] */

 	fg = ahex2int(color[2],'0');
 	fg = fg << 8;
 	
 	bg = ahex2int('0',color[3]);
 	bg = bg << 8;

 	input.color_fg[currentTerminal] = fg;
 	input.color_bg[currentTerminal] = bg;
 	
 	for(int i=0; i < 2000; i++){
 		crt[i] = (crt[i]&0xff) | (input.color_fg[currentTerminal] + input.color_bg[currentTerminal]);
 	}

 	return 1;
}



void fgPainter(int fgcolor){

 	input.color_fg[currentTerminal] = fgcolor;
 	
 	for(int i=0; i < 2000; i++){
 		crt[i] = (crt[i]&0xff) | (input.color_fg[currentTerminal] + input.color_bg[currentTerminal]);
 	}

}

void bgPainter(int bgcolor){

 	input.color_bg[currentTerminal] = bgcolor;
 	
 	for(int i=0; i < 2000; i++){
 		crt[i] = (crt[i]&0xff) | (input.color_fg[currentTerminal] + input.color_bg[currentTerminal]);
 	}

}

void fgColorPicker(char *color){

	if(!(strcmp(color,"black")))fgPainter(0x0000);
	else 
	if(!(strcmp(color,"blue")))fgPainter(0x0100);
	else
	if(!(strcmp(color,"green")))fgPainter(0x0200);
	else 
	if(!(strcmp(color,"aqua")))fgPainter(0x0300);
	else
	if(!(strcmp(color,"red")))fgPainter(0x0400);
	else 
	if(!(strcmp(color,"purple")))fgPainter(0x0500);
	else
	if(!(strcmp(color,"yellow")))fgPainter(0x0600);
	else 
	if(!(strcmp(color,"white")))fgPainter(0x0700);
	else
	if(!(strcmp(color,"Lblack")))fgPainter(0x0800);
	else 
	if(!(strcmp(color,"Lblue")))fgPainter(0x0900);
	else
	if(!(strcmp(color,"Lgreen")))fgPainter(0x0a00);
	else 
	if(!(strcmp(color,"Laqua")))fgPainter(0x0b00);
	else
	if(!(strcmp(color,"Lred")))fgPainter(0x0c00);
	else 
	if(!(strcmp(color,"Lpurple")))fgPainter(0x0d00);
	else
	if(!(strcmp(color,"Lyellow")))fgPainter(0x0e00);
	else 
	if(!(strcmp(color,"Lwhite")))fgPainter(0x0f00);

}

void bgColorPicker(char *color){

	if(!(strcmp(color,"black")))bgPainter(0x0000);
	else 
	if(!(strcmp(color,"blue")))bgPainter(0x1000);
	else
	if(!(strcmp(color,"green")))bgPainter(0x2000);
	else 
	if(!(strcmp(color,"aqua")))bgPainter(0x3000);
	else
	if(!(strcmp(color,"red")))bgPainter(0x4000);
	else 
	if(!(strcmp(color,"purple")))bgPainter(0x5000);
	else
	if(!(strcmp(color,"yellow")))bgPainter(0x6000);
	else 
	if(!(strcmp(color,"white")))bgPainter(0x7000);
	else
	if(!(strcmp(color,"Lblack")))bgPainter(0x8000);
	else 
	if(!(strcmp(color,"Lblue")))bgPainter(0x9000);
	else
	if(!(strcmp(color,"Lgreen")))bgPainter(0xa000);
	else 
	if(!(strcmp(color,"Laqua")))bgPainter(0xb000);
	else
	if(!(strcmp(color,"Lred")))bgPainter(0xc000);
	else 
	if(!(strcmp(color,"Lpurple")))bgPainter(0xd000);
	else
	if(!(strcmp(color,"Lyellow")))bgPainter(0xe000);
	else 
	if(!(strcmp(color,"Lwhite")))bgPainter(0xf000);

}

// terminal history
void remove_char(){
  
  // get cursor position
  outb(CRTPORT, 14);                  
  currentPos[0] = inb(CRTPORT+1) << 8;
  outb(CRTPORT, 15);
  currentPos[0] |= inb(CRTPORT+1);    

  // move back
  currentPos[0]--;

  // reset cursor
  outb(CRTPORT, 15);
  outb(CRTPORT+1, (unsigned char)(currentPos[0]&0xFF));
  outb(CRTPORT, 14);
  outb(CRTPORT+1, (unsigned char )((currentPos[0]>>8)&0xFF));
  crt[currentPos[0]] = ' ' | 0x0700;
  currentPos[0] = currentPos[0];
}

