#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <ncurses.h>
#include <signal.h>


// this needs to be one over the ring buffer size to work
#define bufsize (1024*1024*65)
#define BOUND "------------------------------------------------------------"
#define X_LOWER 4
#define X_UPPER 59
#define Y_LOWER 1
#define Y_UPPER 32

unsigned char buf[bufsize];

struct coords {
    int y;
    int x;
};

struct coords coords_arr[128];

int readlen = 2048 * 1024;

void help()
{
	printf("usage: leveladj [-b | -g | -h | -x]\n\n");
	printf("	-b	set tenbit mode ON (default: OFF)\n");
	printf("	-h	print this help message and exit\n");
	printf("	-g	display a graphical plot and run in a loop until SIGINT\n");
	printf("	-x	set sampling rate (default: native crystal frequency)\n");
	printf("		0: native\n");
	printf("		1: native * 1.25\n");
	printf("		2: native * 1.4\n");
	return;
}

// Ensure there's enough room to display the graph
int check_window_size()
{
    int row, col;
    initscr();
    getmaxyx(stdscr, row, col);
    if (row < Y_UPPER || col < X_UPPER) {
        char err[128];
        char *err_mesg = "Please resize your window: "
                        "You need %d rows x %d cols, "
                        "you have %d rows x %d cols\n";
        snprintf(err, sizeof err, err_mesg, Y_UPPER+1, X_UPPER+1, row, col);
        mvprintw(row/2, (col-strlen(err))/2, "%s", err);
        refresh();
        getch();
    	return 1;
    	}
	return 0;
}

// Generate a 3-sided box with a y-axis of 31 --> 0
void print_box()
{
    mvprintw(0, 0, "%s", BOUND);
    for (int y = 31; y > -1; y--) {
        // Print the axis with | separation, allowing for the top border
        mvprintw(32 - y, 0, "%2d%s", y, "|");
    }
    mvprintw(33, 0, "%s", BOUND);

    return;
}

void print_graph(int level)
{
	for (int i = 0; i < 32; i++) {
		int y_pos = level;
		int x_pos = i;
		// Store where we've printed so far
		coords_arr[i].y = y_pos;
		coords_arr[i].x = x_pos;
		mvprintw(y_pos, x_pos, "%s", "*");
		refresh();
		usleep(100000);
		if (i == 31) {
			erase();
			printBox();
			for (int j = 0; j < sizeof coords_arr / sizeof *coords_arr; j++) {
				// After a period of n, shift everything to the left by one
				// and erase > n so that the screen isn't cluttered
				mvprintw(coords_arr[j].y, coords_arr[j].x - 1, "%s", "*");
				refresh();
			}
			// Clear the array
			memset(coords_arr, 0, sizeof coords_arr);
		}
	}
}

void set(char *name, int level) 
{
	char str[512];
	sprintf(str, "/sys/module/cxadc/parameters/%s", name); 
	int fd = open(str, O_WRONLY);

	sprintf(str, "%d", level); 
	write(fd, str, strlen(str) + 1);

	close(fd);
}

int main(int argc, char *argv[])
{
    FILE *syssfys;
	int fd;
	int level = 20;
	int go_on = 1; // 2 after going over

	fd = open("/dev/cxadc0", O_RDONLY);
	if (fd <= 0) {
		fprintf(stderr, "/dev/cxadc0 not found\n");
		return -1;
	}
	close(fd);

	int graphics = 0;
	int tenbit   = 0;
	int tenxfsc  = 0;

	int c;
	opterr = 0;
	
	if (argc > 1) {
		while ((c = getopt(argc, argv, "bghx")) != -1) {
		switch (c) {
			case 'b':
				tenbit = 1;
				break;
			case 'g':
				graphics = 1;
				break;
			case 'h':
				help();
			case 'x':
				tenxfsc = 1;
				break;	
		}; 
	}
	} else {
	syssfys = fopen ("/sys/module/cxadc/parameters/tenbit", "r");
		if (syssfys == NULL) {
				fprintf(stderr, "no sysfs parameters\n");
				return -1;
		}
		fscanf(syssfys, "%d", &tenbit);
	fclose(syssfys);
		syssfys = fopen ("/sys/module/cxadc/parameters/tenxfsc", "r");
		if (syssfys == NULL) {
				fprintf(stderr, "no sysfs parameters\n");
				return -1;
		}
		fscanf(syssfys, "%d", &tenxfsc);
	fclose(syssfys);
	}
	
	if (graphics) {
		if (checkWindowSize()) {
        	exit(1);
		}
		int x_lower = X_LOWER, x_upper = X_UPPER;
		int y_lower = Y_LOWER, y_upper = Y_UPPER;
		printBox();

	}
	
	set("tenbit", tenbit);
	set("tenxfsc", tenxfsc);

	if (argc > optind) {
		level = atoi(argv[optind]);

		set("level", level);
		return 0;
	}

	while (go_on || graphics) {
		int over = 0;
		unsigned int low = tenbit ? 65535 : 255, high = 0;
		set("level", level);
	
		fd = open("/dev/cxadc0", O_RDONLY);

		printf("testing level %d\n", level);

		// read a bit
		read(fd, buf, readlen);	

		if (tenbit) {
			unsigned short *wbuf = (void *)buf;
			for (int i = 0; i < (readlen / 2) && (over < (readlen / 200000)); i++) {
				if (wbuf[i] < low) low = wbuf[i]; 
				if (wbuf[i] > high) high = wbuf[i]; 

				if ((wbuf[i] < 0x0800) || (wbuf[i] > 0xf800)) {
					over++;
				}
				// auto fail on 0 and 65535 
				if ((wbuf[i] == 0) || (wbuf[i] == 0xffff)) {
					over += (readlen / 50000);
				}
			}
		} else {
			for (int i = 0; i < readlen && (over < (readlen / 100000)); i++) {
				if (buf[i] < low) low = buf[i]; 
				if (buf[i] > high) high = buf[i]; 

				if ((buf[i] < 0x08) || (buf[i] > 0xf8)) {
					over++;
				}

				// auto fail on 0 and 255
				if ((buf[i] == 0) || (buf[i] == 0xff)) {
					over += (readlen / 50000);
				}
			}
		}

		if (! graphics) {
			printf("low %d high %d clipped %d nsamp %d\n", (int)low, (int)high, over, readlen);
		} else {
			print_graph(level);
		}

		if (over >= 20) {
			go_on = 2;
		} else {
			if (go_on == 2) go_on = 0;
		}

		if (go_on == 1) level++;
		else if (go_on == 2) level--;

		if ((level < 0) || (level > 31)) go_on = 0;
		if (! graphics) {
			close(fd); // SIGINT will still have this closed if using graphics mode.
		} 
	}

	return 0;
}

