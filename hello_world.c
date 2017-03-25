
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
DIR Dir;
FILINFO Finfo;

int PLAY = 1;
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

static void button_press(void* context, alt_u32 id){
	xprintf("play_pause \n");
	xprintf("%d \n", IORD(BUTTON_PIO_BASE,0));
//	switch (IORD(BUTTON_PIO_BASE,0)) {
////		case 14:
////			// seek / forward
//		case 13:
//			// play / pause
//			if (PLAY) {
//				PLAY = 0;
//				IOWR(BUTTON_PIO_BASE,3,0);
//				wait();
//			} else {
//				PLAY = 1;
//				IOWR(BUTTON_PIO_BASE,3,0);
//				main();
//			}
//		case 11:
//			STOP = 1;
//			PLAY = 0;
//			IOWR(BUTTON_PIO_BASE,3,0);
////		case 7:
////			// seek / backwards
//		default:
//			IOWR(BUTTON_PIO_BASE,3,0);
//	}
	IOWR(BUTTON_PIO_BASE,3,0);
}


int main(void){
	char Line[256];                 /* Console input buffer */
	long p1;

	char filename[20][13];
	unsigned long fileSize[20];

	char *ptr;
	uint32_t ofs;
	int fifospace;
	alt_up_audio_dev * audio_dev;
	int cnt=0, i;
	uint8_t res;
	unsigned int l_buf;
	unsigned int r_buf;

	// open the Audio port
	audio_dev = alt_up_audio_open_dev ("/dev/Audio");
	if ( audio_dev == NULL)
	alt_printf ("Error: could not open audio device \n");
	else
	alt_printf ("Opened audio device \n");
	IoInit();

	//init interupt
	IOWR(BUTTON_PIO_BASE,2,15);
	alt_irq_register(BUTTON_PIO_IRQ, (void*)0, button_press);
	IOWR(BUTTON_PIO_BASE,3,0);


	// own init code

    xprintf("rc=%d\n", (uint16_t) disk_initialize((uint8_t) 0)); // di	--  0 == p1
    put_rc(f_mount((uint8_t) 0, &Fatfs[0])); // fi	-- 0 == p1


    i = fileList(filename, fileSize);
	xprintf("list of files \n");
	int a;
    for (a = 0; a < i; a++) {
		xprintf("%s", filename[a]);
		xprintf("\n");
    }

    int q = 3; // song index number

    xprintf("hello \n");
    ptr = filename[q];

    put_rc(f_open(&File1, ptr, (uint8_t) 1)); // fo -- 1 == p1
    p1 = fileSize[q];

    ofs = File1.fptr;

    while (p1) {
    	if (p1 <= 0){
    		break;
    	}
		if ((uint32_t) p1 >= 512) {
			cnt = 512;
			p1 -= 512;
		} else {
			cnt = p1;
			p1 = 0;
		}
		res = f_read(&File1, Buff, cnt, &cnt);
		if (res != FR_OK) {
			put_rc(res);
			break;
		}
		if (!cnt)
			break;
		ofs += 512;
		int words = 0;
		int i = 0;

		while (i < (cnt -5)){
			fifospace = alt_up_audio_write_fifo_space(audio_dev, ALT_UP_AUDIO_RIGHT); // try this for now

			while ( i < (cnt - 5) && words < fifospace) {
				l_buf = Buff[i] | Buff[++i] << 8;
				r_buf = Buff[++i] | Buff[++i] << 8;
				i++;
				// write audio buffer
				alt_up_audio_write_fifo (audio_dev, &(l_buf), 1, ALT_UP_AUDIO_LEFT);
				alt_up_audio_write_fifo (audio_dev, &(r_buf), 1, ALT_UP_AUDIO_RIGHT);
				words++;
			}
		}

    }
//	char filename[] = "abc.wav";
//	xprintf("%d", isWav(filename));
	return 0;
}

int isWav(char *filename){
	int len = strlen(filename);
//	char *src;
//	strcpy(src, ".WAV");
//	return filename[len - 4] == src;
	return filename[len - 4] == '.' && filename[len - 3] == 'W' && filename[len - 2] == 'A' && filename[len - 1] == 'V' && filename[len] == '\0';
}

/*
 stores the .wav files in the filename array, and the file sizes in the fileSize array
 returns the number of .wav files
 */
int fileList(char filename[][13], unsigned long *fileSize) {
	int i = 0;
	xprintf("in filelist \n");
	uint8_t res;
	char *ptr;
	while (*ptr == ' ')
		ptr++;
	res = f_opendir(&Dir, ptr);
	if (res) {
		put_rc(res);
		return 0;
	}
	for (;;) {
		res = f_readdir(&Dir, &Finfo);
		if ((res != FR_OK) || !Finfo.fname[0]) {
			break;
		}
		xprintf("%d\n", i);
		if (isWav(&(Finfo.fname[0]))) {
			strcpy(&filename[i][0], &(Finfo.fname[0]));
			fileSize[i++] = Finfo.fsize;
//			xprintf("%9lu  %s", Finfo.fsize, &(Finfo.fname[0]));
		}
	}
	return i;
}

void wait() {
	while(1) {
		xprintf("waiting\n");
	}
}

