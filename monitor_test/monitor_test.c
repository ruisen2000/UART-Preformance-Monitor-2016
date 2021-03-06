/**
 * monitor_test.c 
 *
 * This program uses 2 timers to generate timing statistics, and display it once every second
 * The first timer counts down from 1 second, and generates an interupt every second when the timer value reaches 0.
 * The second timer is initialized to 0 before each read/write operation, then stopped after the read/write has completed.
 * Note that the timing of read includes the amount of time that read is sleeping and waiting for characters to be typed into Kermit
 *
 * The interupt sends a signal, which then informs the user program to display statistics
 * The loop first writes a character, then attempts to read a character.  If no characters are available, it goes to sleep.
 * Thus, write will only be called again after read has successfully read characters.  
 *
 * A signal handler for SIGINT is registered so that a user may exit the
 * test program by entering CTRL+C at the terminal. 
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <sys/sysinfo.h>
#include "../../user-modules/timer_driver/timer_ioctl.h"

/*
 * Global variables
 */
struct timer_ioctl_data data; // data structure for ioctl calls
int fd; // file descriptor for timer driver
int charsReceived = 0;  //total number of characters received by Kermit since program started
int charsSent = 0;	//total number of characters sent to Kermit since program started
unsigned int numCycles = 0; // number of cycles in 1s	
double readTime = 0; //number of cycles it took to complete read operation
double writeTime = 0; //number of cycles it took to complete write operation
double avgReadTime = 0;
double avgWriteTime = 0;
double totalReadTime = 0;
double totalWriteTime = 0;
time_t startTime = 0;  // start time of application


/* 
 * Function to read the value of the timer
 */
__u32 read_timer()
{
	data.offset = TIMER_REG;
	ioctl(fd, TIMER_READ_REG, &data);
	return data.data;
}

/*
int *setup_vga()
{
	fd = open("/dev/vga_driver",O_RDWR);
	if (fd < 0) {
		printf("FAILED TO OPEN DRIVER");
	}
	return (int*)mmap(NULL,BUFFER_SIZE,PROT_READ |
	PROT_WRITE, MAP_SHARED, fd, 0);
} 
*/

/*
 * SIGIO Signal Handler
 */
void sigio_handler(int signum) 
{
	clock_t programTime;
	struct sysinfo info;
	time_t currentTime = 0;
	printf("Received Signal, signum=%d (%s)\n", signum, strsignal(signum));

	printf("\n\nStatistics for Writing: \n");
	printf("Characters sent: %d\n", charsSent);
	//printf("Average write time in cycles: %f \n", avgWriteTime);
	printf("Average write time in seconds: %f \n", avgWriteTime/numCycles);
	
	printf("\n\nStatistics for Reading: \n");
	printf("Characters read from Kermit: %d\n", charsReceived);	
	//printf("Average read time in cycles: %f\n", avgReadTime);
	printf("Average read time in seconds: %f \n", avgReadTime/numCycles);	

	//programTime = clock();
	//programTime = (double)clock()/CLOCKS_PER_SEC; //time since application start
	//printf("Time that application used CPU: %ld \n", programTime);
	printf("\n\nStatistics for Time: \n");
	sysinfo(&info);
	printf("Time since boot in seconds: %ld\n", info.uptime);	
	currentTime = time(NULL);
	printf("Time of application in seconds: %ld\n", currentTime - startTime);
	printf("============================================\n\n");
	
}

/*
 * SIGINT Signal Handler
 */
void sigint_handler(int signum)
{
	printf("Received Signal, signum=%d (%s)\n", signum, strsignal(signum));

	if (fd) {
		// Turn off timer and reset device
		data.offset = CONTROL_REG;
		data.data = 0x0;
		ioctl(fd, TIMER_WRITE_REG, &data);

		// Close device file
		close(fd);
	}

	exit(EXIT_SUCCESS);
}

int main(void)
{
	clock_t programTime;
	int oflags;
	int serial_fd;
	struct termios tio; 
	char towrite[] = "testaaaaaaaaaaaaaaaaaaaaaaaaaaa";  //write buffer
	char c[100] = {'\0'};	//read buffer
	
	//return values of read/write
	int charsWritten = 0;
	int charsRead = 0;
	
	startTime = time(NULL);
	// Register handler for SIGINT, sent when pressing CTRL+C at terminal
	signal(SIGINT, &sigint_handler);

	// Open device driver file
	if (!(fd = open("/dev/timer_driver", O_RDWR))) {
		perror("open");
		exit(EXIT_FAILURE);
	}
	
	
	 
	// Rune the timer for 1 second to get the number of clock ticks per second

	// Inititalize the control register bits
	data.offset = CONTROL_REG;
	data.data = T0INT;
	ioctl(fd, TIMER_WRITE_REG, &data);

	// Reset the counter to 0 (load register)
	data.offset = LOAD_REG;
	data.data = 0x0;
	ioctl(fd, TIMER_WRITE_REG, &data);

	// Set control bits to load value in load register into counter
	// move value from load register into timer register
	data.offset = CONTROL_REG;
	data.data = LOAD0;
	ioctl(fd, TIMER_WRITE_REG, &data);

	sleep(1);

	// Set control bits to enable timer, count up
	data.offset = CONTROL_REG;
	data.data = ENT0;
	ioctl(fd, TIMER_WRITE_REG, &data);

	sleep(1);

	// Clear control bits to disable timer
	data.offset = CONTROL_REG;
	data.data = 0x0;
	ioctl(fd, TIMER_WRITE_REG, &data);

	// Read value from timer
	numCycles = read_timer();
	//printf("timer value after 1 seconds  = %u\n", numCycles);
	//printf("there are this many clock cycles in 1ms\n");

		

	// Set up Asynchronous Notification
	signal(SIGIO, &sigio_handler);
	fcntl(fd, F_SETOWN, getpid());
	oflags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, oflags | FASYNC);

	// Set one of the timers to keep counting down from 1 second and generating interupts and the signal every second
	
	// Load countdown timer initial value for ~1 sec
	data.offset = LOAD_REG;
	data.data = 100e6; // timer runs on 100 MHz clock
	ioctl(fd, TIMER_WRITE_REG, &data);
	sleep(1);

	// Set control bits to load value in load register into counter
	data.offset = CONTROL_REG;
	data.data = LOAD0;
	ioctl(fd, TIMER_WRITE_REG, &data);

	// Set control bits to enable timer, enable interrupt, auto reload mode,
	//  and count down
	data.data = ENT0 | ENIT0 | ARHT0 | UDT0;
	ioctl(fd, TIMER_WRITE_REG, &data);

	// setup connection to kermit
	memset(&tio, 0, sizeof(tio));
	serial_fd = open("/dev/ttyPS0",O_RDWR);
	if(serial_fd == -1)printf("Failed to open serial port... :( \n");
	tcgetattr(serial_fd, &tio);
	cfsetospeed(&tio, B115200);
	cfsetispeed(&tio, B115200);
	tcsetattr(serial_fd, TCSANOW, &tio); 	

	//programTime = clock();
	//programTime = (double)clock()/CLOCKS_PER_SEC; //time since application start
	//printf("Time since application start in seconds: %ld \n", programTime);
	printf("\n\n=================== System Preformance: ===================== \n\n");
	

	// Wait for signals
	while (1){
		
		
		
		//Enable timer, then start writing operation to write 1 byte, then stop timer.
		//Calculate time required and total time spent writing so far 
		// Note: Write only writes again if characters have been read.  If not characters are read then read() sleeps and no new characters will be written
		// so system preformance for write won't update (remains at 1 characters sent)

		// Reset the counter to 0 (load register)
		data.offset = LOAD_REG1;
		data.data = 0x0;
		ioctl(fd, TIMER_WRITE_REG, &data);
	
		// move value from load register into timer register
		data.offset = CONTROL_REG1;
		data.data = LOAD0;
		ioctl(fd, TIMER_WRITE_REG, &data);
			
		// Set control bits to enable timer, count up
		data.offset = CONTROL_REG1;
		data.data = ENT0;
		ioctl(fd, TIMER_WRITE_REG, &data);

		// write 1 character	
		charsWritten = write(serial_fd, &towrite, 1);
		
		// disable timer
		data.offset = CONTROL_REG1;
		data.data = 0x0;
		ioctl(fd, TIMER_WRITE_REG, &data);	
	
		if(charsWritten == 1) {
		writeTime = read_timer();
		totalWriteTime = totalWriteTime + writeTime;
		charsSent++;	
		avgWriteTime = totalWriteTime / charsSent;	
		
		}
		else {
		printf("writing failed\n");
		}

		/* 
			===  Reset timer for testing reading ===

		*/

		// Reset the counter to 0 (load register)
		data.offset = LOAD_REG1;
		data.data = 0x0;
		ioctl(fd, TIMER_WRITE_REG, &data);
	
		// move value from load register into timer register
		data.offset = CONTROL_REG1;
		data.data = LOAD0;
		ioctl(fd, TIMER_WRITE_REG, &data);
			
		// Set control bits to enable timer, count up
		data.offset = CONTROL_REG1;
		data.data = ENT0;
		ioctl(fd, TIMER_WRITE_REG, &data);
		charsRead = read(serial_fd, &c, 1); // read 1 byte from /dev/ttyPS0, and store it in buffer pointed to by c
		// Note: Read function sleeps if no input is available, so read time includes time waiting for input
			
		// Clear control bits to disable timer
		data.offset = CONTROL_REG1;
		data.data = 0x0;
		ioctl(fd, TIMER_WRITE_REG, &data);

		if(charsRead == 1)
		{
		readTime = read_timer(); 
		totalReadTime = totalReadTime + readTime;
		charsReceived++;	
		avgReadTime = totalReadTime / charsReceived;
		
		}
		else {
		printf("reading failed\n");
		}
		

	}

	

	return 0;
}

