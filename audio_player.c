#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <system.h>
#include <sys/alt_alarm.h>
#include <io.h>

#include "fatfs.h"
#include "diskio.h"

#include "ff.h"
#include "monitor.h"
#include "uart.h"

#include "alt_types.h"

#include <altera_up_avalon_audio.h>
#include <altera_up_avalon_audio_and_video_config.h>

#include "altera_avalon_pio_regs.h"
#include "sys/alt_irq.h"

static alt_alarm alarm;
static unsigned long Systick = 0;
static volatile unsigned short Timer;   /* 1000Hz increment timer */

FATFS Fatfs[1];          /* File system object for each logical drive */
uint8_t Buff[8192] __attribute__ ((aligned(4)));  /* Working buffer */

FIL File1;
FILE *display;
DIR Dir;
FILINFO Finfo;

alt_up_audio_dev * audio_dev;
volatile long p1;
volatile int fifospace;

volatile int buffIndex = 0;


volatile char filename[20][13];
volatile unsigned long fileSize[20];
volatile int fileIndex = 0;
int cnt = 0;
volatile uint32_t ofs;

int playing = 0;
int STOP = 0;

static alt_u32 TimerFunction (void *context)
{
   static unsigned short wTimer10ms = 0;

   (void)context;

   Systick++;
   wTimer10ms++;
   Timer++; /* Performance counter for this module */

   if (wTimer10ms == 10)
   {
      wTimer10ms = 0;
      ffs_DiskIOTimerproc();  /* Drive timer procedure of low level disk I/O module */
   }

   return(1);
} /* TimerFunction */

static void IoInit(void)
{
   uart0_init(115200);

   /* Init diskio interface */
   ffs_DiskIOInit();

   //SetHighSpeed();

   /* Init timer system */
   alt_alarm_start(&alarm, 1, &TimerFunction, NULL);

} /* IoInit */

static
void put_rc(FRESULT rc)
{
    const char *str =
        "OK\0" "DISK_ERR\0" "INT_ERR\0" "NOT_READY\0" "NO_FILE\0" "NO_PATH\0"
        "INVALID_NAME\0" "DENIED\0" "EXIST\0" "INVALID_OBJECT\0" "WRITE_PROTECTED\0"
        "INVALID_DRIVE\0" "NOT_ENABLED\0" "NO_FILE_SYSTEM\0" "MKFS_ABORTED\0" "TIMEOUT\0"
        "LOCKED\0" "NOT_ENOUGH_CORE\0" "TOO_MANY_OPEN_FILES\0";
    FRESULT i;

    for (i = 0; i != rc && *str; i++) {
        while (*str++);
    }
    xprintf("rc=%u FR_%s\n", (uint32_t) rc, str);
}


int isWav(char *filename){
	int len = strlen(filename);
	return filename[len - 4] == '.' && filename[len - 3] == 'W' && filename[len - 2] == 'A' && filename[len - 1] == 'V' && filename[len] == '\0';
}

/*
 stores the .wav files in the filename array, and the file sizes in the fileSize array
 */
void fileList() {
	int i = 0;
	xprintf("in filelist \n");
	uint8_t res;
	char *ptr;
	while (*ptr == ' ')
		ptr++;

	res = f_opendir(&Dir, ptr);
	if (res) {
		put_rc(res);
		return;
	}
	for (;;) {
		res = f_readdir(&Dir, &Finfo);
		if ((res != FR_OK) || !Finfo.fname[0]) {
			break;
		}
		if (isWav(&(Finfo.fname[0]))) {
			strcpy(&filename[i][0], &(Finfo.fname[0]));
			fileSize[i++] = Finfo.fsize;
		}
	}
}

void loadFile () {
    put_rc(f_open(&File1, filename[fileIndex], (uint8_t) 1)); // fo -- 1 == p1
    p1 = fileSize[fileIndex];
    return;
}
//
void debounce (int button) {
//	xprintf("debounce\n");
	usleep(15000);
	while(IORD(BUTTON_PIO_BASE,0) == button); // pass when we are no longer detecting the button
	usleep(15000);
}

void stop () {
	xprintf("stopping");
	loadFile();
	printDisplay();
	return;
}

void play() {
	unsigned int l_buf;
	unsigned int r_buf;
	uint8_t res;
    ofs = File1.fptr;
    if ((uint32_t) p1 >= 0) { // song has not ended
    	// refilling buffer
		if (buffIndex >= cnt && (uint32_t) p1 >= 512) {//what does buffIndex >= cnt do
			cnt = 512;
			p1 -= 512;
			res = f_read(&File1, Buff, cnt, &cnt);
			buffIndex = 0;

		} else if (buffIndex >= cnt) { // reached the end of the song / file
			xprintf("END OF SONG");
			cnt = p1;
			p1 = 0;
			res = f_read(&File1, Buff, cnt, &cnt);
			buffIndex = 0;
			playing = 0;
			stop();
			return;
		}
		// writing
		fifospace = alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_RIGHT);
		if (fifospace > 0) {
			l_buf = Buff[buffIndex] | Buff[buffIndex + 1] << 8;
			r_buf = Buff[buffIndex + 2] | Buff[buffIndex + 3] << 8;
			// write audio buffer
			alt_up_audio_write_fifo (audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT);
			alt_up_audio_write_fifo (audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT);
			buffIndex +=4;
		}

    }
    return;
//
//	while (p1) {
//		fifospace = alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_RIGHT);
//		if (fifospace > 0) {
//			l_buf = Buff[buffIndex] | Buff[buffIndex + 1] << 8;
//			r_buf = Buff[buffIndex + 2] | Buff[buffIndex + 3] << 8;
//			buffIndex +=4;
//			// write audio buffer
//			alt_up_audio_write_fifo (audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT);
//			alt_up_audio_write_fifo (audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT);
//		}
//
//	}
//	playing = 0;
}

void printDisplay() {
	fprintf(display, "%c%s", 27, "[2J"); // clears the screen
	fprintf(display, "%s\n", filename[fileIndex]);
}

void skipForward() {
	fileIndex = (fileIndex + 1) % 13;
	loadFile();
	printDisplay();
}

void fastForwards() {
	if (playing) {
		buffIndex += 8;
	}
}

void skipBackward() {
	fileIndex = (fileIndex > 0) ? fileIndex - 1 : 12;
	loadFile();
	xprintf("%d", fileIndex);
	printDisplay();
}

void init() {
	// open up display
	display = fopen("/dev/lcd_display", "w");
	// open the Audio port
	audio_dev = alt_up_audio_open_dev ("/dev/Audio");
	if ( audio_dev == NULL)
	alt_printf ("Error: could not open audio device \n");
	else
	alt_printf ("Opened audio device \n");
	IoInit();
	xprintf("rc=%d\n", (uint16_t) disk_initialize((uint8_t) 0)); // di	--  0 == p1
	put_rc(f_mount((uint8_t) 0, &Fatfs[0])); // fi	-- 0 == p1
	fileList(); // sets up file system
}

int main(void){
	init();
    fileIndex = 0;
	xprintf("list of files \n");
	int a;
	char button;
	uint8_t res;
    loadFile();
    buffIndex = 0;
//    play();
	printDisplay();

    while(1) { // tight polling
    	button = IORD(BUTTON_PIO_BASE,0);
    	switch (button) {
			case 7: // seek / backwards
				skipBackward();
    			debounce(7);
				break;
    		case 11: // stop
    			playing = 0;
    			stop();
    			debounce(11);
    			break;
    		case 13: // play / pause
    			xprintf("IM HERE\n");
    			if (playing) {
    				// pause code
    				printDisplay();
    				playing = 0;
    				printDisplay();
    			} else { // playing code
    				playing = 1;

    				//playing = 1;
    				if ((uint32_t) p1 >= 512) {
						cnt = 512;
						p1 -= 512;
					} else {
						cnt = p1;
						p1 = 0;
					}
    				res = f_read(&File1, Buff, cnt, &cnt);
    				// change this to for loop?
    				// fill buffer

    				for (buffIndex = 0; buffIndex < 512;){
        				unsigned int l_buf;
        				unsigned int r_buf;
    					fifospace = alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_RIGHT);
    					if (fifospace > 0) {
    						l_buf = Buff[buffIndex] | Buff[buffIndex + 1] << 8;
    						r_buf = Buff[buffIndex + 2] | Buff[buffIndex + 3] << 8;
    						// write audio buffer
    						alt_up_audio_write_fifo (audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT);
    						alt_up_audio_write_fifo (audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT);
    						buffIndex +=4;
    					}
    				}
    				// add playing  = 1 here?
    				printDisplay();
    			}
    			debounce(13);
    			break;
    		case 14: // seek / forward
    			skipForward();
    			debounce(14);
    			break;
    		default:
    			if (playing) {
    				play();
    			}
    	}
    }
	return 0;
}

