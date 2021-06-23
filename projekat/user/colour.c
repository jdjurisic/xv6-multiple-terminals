#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
#include "kernel/fcntl.h"

void helpMenu(){
	printf("\nUse this program to change color of current terminal.\nUsage: colour [OPTION] ...\n\nCommand line options:\n-h, --help: Show help prompt.\n-bg, --background: Set background color.\n-fg, --foreground: Set foreground color.\n reset: Reset color to default (black'n'white).\n 0x____: Set background/foreground color for given hexadecimal input.\n");
}

int hexCheck(char a){
	switch(a){
		case '0':case '1':case '2':case '3':case '4':
		case '5':case '6':case '7':case '8':case '9':
		case 'a':case 'b':case 'c':case 'd':case 'e':case 'f':
		case 'A':case 'B':case 'C':case 'D':case 'E':case 'F':
			return 1;
		default:
			return 0;
	}
}

int
main(int argc, char *argv[])
{
		int i = 1;

		char *foreground = 0, *background = 0, *hexColor = 0;

		if(argc > 1) while(i < argc)
		{

		//printf("i - %d - argv[i] - %s\n",i,argv[i]);

		if(!(strcmp(argv[i],"--help")) || !(strcmp(argv[i],"-h")))
		{
			helpMenu();
		}
		else
		if(!(strcmp(argv[i],"--foreground")) || !(strcmp(argv[i],"-fg")))
		{
			foreground = argv[i+1];
			setfg(argv[i+1]);
			i++;
		}
		else 
		if(!(strcmp(argv[i],"--background")) || !(strcmp(argv[i],"-bg")))
		{
			background = argv[i+1];
			setbg(argv[i+1]);
			i++;
		}
		else 
		if(!(strcmp(argv[i],"reset")))
		{
			rstclr();
		}
		else 
		{			
			// Format: 0x__
			if(strlen(argv[i]) == 4 && argv[i][0] == '0' && argv[i][1] == 'x' && hexCheck(argv[i][2]) && hexCheck(argv[i][3]))
			{
				hexColor = argv[i];
				sethex(argv[i]);
			}
			else printf("Invalid color value.\n");
		}

		i++;

		}
	
	//printf("\n FG %s\n BG %s\n HEX %s",foreground,background,hexColor);
	exit();
}

// colour functions
// void rstclr(void);
// void setfg(char*);
// void setbg(char*);
// void sethex(char*);
