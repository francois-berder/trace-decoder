#include <iostream>
#include <cstdio>
#include <string.h>
#if defined(LINUX) || defined(OSX)
#include <termios.h>
#endif
#if defined(LINUX)
#include <signal.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#ifdef WINDOWS
#include <winsock2.h>
#include <windows.h>
#endif

#include "swt.hpp"

#define USE_CLOSE_AND_REOPEN_HACK 1  // This is an interim workaround to avoid an issue with bogus all-zero data emerging from
// a PL2303 based adapter on the initial open() but not on subsequent open()/close() calls.

struct Args
{
   std::string serialdev;
   int srcbits;
   int port;
   bool help;
   bool autoexit;
   bool debug;
   bool pthread;
   uint32_t baud;
   bool itcprint;  // should ITC print traffic be forwarded to stdout?
   uint32_t itcprintchannel;  // if itcprint is true, then which channel should be assumed to pertain to the ITC print application?

   Args()
   {
      serialdev = "/dev/ttyUSB0";  // overridden by -device argument
      srcbits = 0;  // overridden by -srcbits argument
      port = 4568;  // overridden by -port argument
      help = false; // overridden by -h option
      autoexit = false; // overridden by -autoexit option
      debug = false; // overridden by -debug option
      baud = 115200; // overridden by -baud option
#ifdef WINDOWS
      pthread = true;  // shouldn't be overridden
#else
      pthread = false;  // Can be overridden by -pthread, however this would generally only be valuable for internal maintenance
#endif
      itcprint = false; // overridden by -itcprint <channel> option
      itcprintchannel = 0;  // overridden by -itcprint <channel> option
   }

   void parse(int argc, char *argv[]);
};

// prototypes
void usage(void);
bool openSerialDevice(const std::string &dev, int &fd, uint32_t requestedBaud);

void signal_null_handler(int signum){
   // do nothing
}


// the main program
int main(int argc, char *argv[])
{
   Args args;
#ifdef WINDOWS
   WORD wVersionRequested;
   WSADATA wsaData;
   int err;

   wVersionRequested = MAKEWORD(2, 2);

   err = WSAStartup(wVersionRequested, &wsaData);
   if (err != 0) {
	/* Tell the user that we could not find a usable */
	/* Winsock DLL.                                  */
	   printf("WSAStartup failed with error: %d\n", err);
	   return -1;
   }
   
#endif
#ifdef LINUX
   // default for SIGPIPE is to terminate program; we don't want that
   signal(SIGPIPE, signal_null_handler);
#endif   
   args.parse(argc, argv);

   if (args.help)
   {
      usage();
      return 0;
   }

   srand (time(NULL));   // for stress/fuzz testing purposes

#ifdef SWT_CPP_TEST   
   internal_components_test1();
   internal_components_test2();   
#endif
   
   NexusDataAcquisitionMessage msg;   
   NexusStream ns(args.srcbits);
   int serialFd;

   if (openSerialDevice(args.serialdev, serialFd, args.baud))
   {
      IoConnections ioConnections(args.port, args.srcbits, serialFd, args.debug, args.itcprint, args.itcprintchannel, args.pthread);
      
      while (! (args.autoexit && ioConnections.hasClientCountDecreasedToZero()) )
      {
	 ioConnections.serviceConnections();
	 if (ioConnections.didSerialDisconnect())
	 {
	    std::cerr << "Detected disconnect" << std::endl;	    	       	    
	    // Keep trying to reconnect, and if successful, continue on	    
	    for (;;)
	    {
	       std::cerr << "Attempting to reconnect to " << args.serialdev << std::endl;	    	       
	       if (openSerialDevice(args.serialdev, serialFd, args.baud))
	       {
		  std::cerr << "Reconnected to " << args.serialdev << std::endl;	    	       		  		  
		  ioConnections.setSerialDevice(serialFd);
		  break;
	       }
	       else
	       {
		  sleep(2); // in this mode when we're trying to reconnect, it should be rare, but if it happens,
		  	// it's probably best to spare the CPU when repeatedly trying to reconnect
	       }
	    }
	 }
      }
      // The call below seems to hose things.  It would be good ideally to
      //  know why, but since it happens right before process exit(), and exit()
      // will clean up everything, I'm commenting out the call.
      
      // ioConnections.closeResources();  
   }
   else
   {
      std::cerr << "Unable to open serial device " << args.serialdev << std::endl;
      return -1;
   }

   
   return 0;
}


void Args::parse(int argc, char *argv[])
{
   enum State {  NOT_IN_ARG, IN_DEVICE, IN_SRCBITS, IN_PORT, IN_BAUD, IN_ITCPRINT };
   State state = NOT_IN_ARG;
      
   for (int i = 1; i < argc; i++)
   {
      if (state == IN_DEVICE)
      {
	 serialdev = argv[i];
	 state = NOT_IN_ARG;
      }
      else if (state == IN_SRCBITS)
      {
	 srcbits = atoi(argv[i]);
	 state = NOT_IN_ARG;
      }
      else if (state == IN_PORT)
      {
	 port = atoi(argv[i]);
	 state = NOT_IN_ARG;
      }
      else if (state == IN_BAUD)
      {
	 baud = atoi(argv[i]);
	 state = NOT_IN_ARG;
      }
      else if (state == IN_ITCPRINT)
      {
	 itcprint = 1;
	 itcprintchannel = atoi(argv[i]);
	 state = NOT_IN_ARG;
      }
      else
      {
	 // not in arg
	 if (strcmp(argv[i], "-device") == 0)
	 {
	    state = IN_DEVICE;
	 }
	 else if (strcmp(argv[i], "-srcbits") == 0)
	 {
	    state = IN_SRCBITS;
	 }
	 else if (strcmp(argv[i], "-port") == 0)
	 {
	    state = IN_PORT;
	 }
	 else if (strcmp(argv[i], "-baud") == 0)
	 {
	    state = IN_BAUD;
	 }
	 else if (strcmp(argv[i], "-itcprint") == 0)
	 {
	    state = IN_ITCPRINT;
	 }
	 else if (strcmp(argv[i], "-pthread") == 0)
	 {
#ifndef WINDOWS	    
	    pthread = true;
#endif
	    // no change in state
	 }
	 else if (strcmp(argv[i], "-d") == 0)
	 {
	    debug = true;
	    // no change in state for argument-less options	    
	 }
	 else if (strcmp(argv[i], "-h") == 0)
	 {
	    help = true;
	    // no change in state for argument-less options
	 }
	 else if (strcmp(argv[i], "-autoexit") == 0)
	 {
	    autoexit = true;
	    // no change in state for argument-less options
	 }
	 else
	 {
	    std::cerr << "Unknown option: " << argv[i] << std::endl;
	    exit(-1);
	 }
      }
   }
}

#if defined(LINUX) || defined(OSX)
speed_t nearestBaudRate(uint32_t baud)
{
   speed_t retval;

   if (baud <= 300)
      retval = B300;
   else if (baud <= 600)
      retval = B600;
   else if (baud <= 1200)
      retval = B1200;
   else if (baud <= 1800)
      retval = B1800;
   else if (baud <= 2400)
      retval = B2400;
   else if (baud <= 4800)
      retval = B4800;
   else if (baud <= 9600)
      retval = B9600;
   else if (baud <= 19200)
      retval = B19200;
   else if (baud <= 38400)
      retval = B38400;
   else if (baud <= 57600)
      retval = B57600;
   else if (baud <= 115200)
      retval = B115200;
   else if (baud <= 230400)
      retval = B230400;
#ifdef B460800
   else if (baud <= 460800)
      retval = B460800;
#endif
#ifdef B500000   
   else if (baud <= 500000)
      retval = B500000;
#endif
#ifdef B576000
   else if (baud <= 576000)
      retval = B576000;
#endif
#ifdef B921600   
   else if (baud <= 921600)
      retval = B921600;
#endif
#ifdef B1000000   
   else if (baud <= 1000000)
      retval = B1000000;
#endif
#ifdef B1152000   
   else if (baud <= 1152000)
      retval = B1152000;
#endif
#ifdef B1500000   
   else if (baud <= 1500000)
      retval = B1500000;
#endif
#ifdef B2000000   
   else if (baud <= 2000000)
      retval = B2000000;
#endif
#ifdef B2500000      
   else if (baud <= 2500000)
      retval = B2500000;
#endif
#ifdef B3000000   
   else if (baud <= 3000000)
      retval = B3000000;
#endif
#ifdef B3500000   
   else if (baud <= 3500000)
      retval = B3500000;
#endif
#ifdef B4000000   
   else
      retval = B4000000;
#else
   else
      retval = B115200;
#endif   
   
   return retval;
}


uint32_t speedToInteger(speed_t speed)
{
   switch (speed)
   {
      case B300:
	 return 300;
      case B600:
	 return 600;
      case B1200:
	 return 1200;
      case B1800:
	 return 1800;
      case B2400:
	 return 2400;
      case B4800:
	 return 4800;
      case B9600:
	 return 9600;
      case B19200:
	 return 19200;
      case B38400:
	 return 38400;
      case B57600:
	 return 57600;
      case B115200:
	 return 115200;
      case B230400:
	 return 230400;
#ifdef B460800	 
      case B460800:
	 return 460800;
#endif
#ifdef B500000	 
      case B500000:
	 return 500000;
#endif
#ifdef B576000	 
      case B576000:
	 return 576000;
#endif
#ifdef B921600	 
      case B921600:
	 return 921600;
#endif
#ifdef B1000000	 
      case B1000000:
	 return 1000000;
#endif
#ifdef B1152000	 
      case B1152000:
	 return 1152000;
#endif
#ifdef B1500000	 
      case B1500000:
	 return 1500000;
#endif
#ifdef B2000000	 
      case B2000000:
	 return 2000000;
#endif
#ifdef B2500000	 
      case B2500000:
	 return 2500000;
#endif
#ifdef B3000000	 
      case B3000000:
	 return 3000000;
#endif
#ifdef B3500000	 
      case B3500000:
	 return 3500000;
#endif
#ifdef B4000000	 
      case B4000000:
	 return 4000000;
#endif	 
      default:
	 return -1;
   }
}

speed_t initSerialDevice(int fd, speed_t baud)
{
   struct termios options;
   speed_t readback;
   int result;


   // This is how minicom seems to successfully set up the device (and it works after that); we'll try to do the same
   // ioctl(3, TCGETS, {c_iflags=0x1, c_oflags=0, c_cflags=0x1cb2, c_lflags=0, c_line=0, c_cc[VMIN]=1, c_cc[VTIME]=5, c_cc="\x03\x1c\x7f\x15\x04\x05\x01\x00\x11\x13\x1a\x00\x12\x0f\x17\x16\x00\x00\x00"}) = 0
   
   // attempt to set baud rate to requested, return the actual baud rate read back
   // from device after the attempt
   result = tcgetattr(fd, &options);
   if (result == 0)
   {
      result = cfsetispeed(&options, baud);  // no need to set output speed because we're only using the input channel
   }
   if (result == 0)
   {
      // We're only using the input channel, but on MacOS, failing to specify both
      // input and output speed (identically) seems to result in an invalid
      // argument error from the subsequent tcsetattr() call.        
      result = cfsetospeed(&options, baud);
   }
   if (result == 0)
   {
      options.c_iflag &= ~(ICRNL | IXON);
      options.c_iflag |= IGNBRK;

      options.c_oflag &= ~(OPOST | ONLCR);

      options.c_lflag &= ~ICANON;  // turning off ICANON seems to be critical; the others I'm just trying to match what works for minicom
      options.c_lflag &= ~ISIG;  // not sure if this needs to be turned off, but minicom turns it off
      options.c_lflag &= ~ECHO;  // not sure if this needs to be turned off, but minicom turns it off
      options.c_lflag &= ~ECHOE;  // not sure if this needs to be turned off, but minicom turns it off
      options.c_lflag &= ~ECHOK;  // not sure if this needs to be turned off, but minicom turns it off
      options.c_lflag &= ~ECHONL;  // not sure if this needs to be turned off, but minicom turns it off
      options.c_lflag &= ~NOFLSH;  // not sure if this needs to be turned off, but minicom turns it off
      options.c_lflag &= ~TOSTOP;  // not sure if this needs to be turned off, but minicom turns it off

      options.c_lflag &= ~ECHOCTL;  // not sure if this needs to be turned off, but minicom turns it off
      options.c_lflag &= ~ECHOKE;  // not sure if this needs to be turned off, but minicom turns it off
      options.c_lflag &= ~ECHOPRT;  // not sure if this needs to be turned off, but minicom turns it off
      options.c_lflag &= ~IEXTEN;  // not sure if this needs to be turned off, but minicom turns it off                  
      
      options.c_cflag &= ~PARENB;
      options.c_cflag &= ~CSTOPB;
      options.c_cflag &= ~CSIZE;
      options.c_cflag |= CS8;
      options.c_cflag  |= (CLOCAL | CREAD);   
   }

   if (result == 0)
   {
      result = tcsetattr(fd, TCSANOW, &options);
   }
   if (result == 0)
   {
      result = tcflush(fd, TCIOFLUSH);
   }
   if (result == 0)
   {
      result = tcgetattr(fd, &options);
   }
   if (result == 0)
   {
      readback = cfgetispeed(&options);
      result = (readback == baud ? 0 : -1);
      // std::cerr << "baud rate readback is " << speedToInteger(readback) << std::endl;      
   }
   
   if (result == 0)
   {
      return readback;
   }
   else
   {
      std::cerr << "Unable to set serial device baud rate to " << speedToInteger(baud) << std::endl;
      return -1;
   }
}
#endif

#ifdef WINDOWS
// for Windows let's just treat speed_t as the literal non-quantized uint-represented baud rate
typedef uint32_t speed_t;
speed_t nearestBaudRate(uint32_t baud)
{
	// no quantization or translation, at all
	return baud;
}
speed_t initSerialDevice(int fd, speed_t baud)
{
   DCB dcb;
   BOOL result;
   HANDLE h = (HANDLE)_get_osfhandle(fd);
   // std::cout << "_get_osfhandle returned " << h << std::endl;      
   result = GetCommState(h, &dcb);
   if (result)
   {
      // std::cout << "GetCommState returned success" << std::endl;
      char buf[256];
      sprintf(buf, "baud=%d parity=N data=8 stop=1", baud);
      result = BuildCommDCBA(buf, &dcb);
   }
   if (result)
   {
      // std::cout << "BuildCommDCBA returned success" << std::endl;      
      result = SetCommState(h, &dcb);
   }
   if (result)
   {
      result = GetCommState(h, &dcb);
   }
   if (result)
   {
      // std::cout << "GetCommState returned success, baud rate readback is: " << dcb.BaudRate << std::endl;      
      return dcb.BaudRate;
   }
   else
   {
      std::cerr << "There was an error trying to set the baud rate" << std::endl;
      exit(-1);
      return -1;
   }
}
#endif

bool openSerialDevice(const std::string &dev, int &fd, uint32_t requestedBaud)
{
   speed_t baud = nearestBaudRate(requestedBaud);
      
   fd = open(dev.c_str(), O_RDONLY);   

   if (fd != -1)
   {
      if (initSerialDevice(fd, baud) == (speed_t)-1)
      {
	 return false;
      }
   }

#if defined(LINUX) && defined(USE_CLOSE_AND_REOPEN_HACK)
   // This is a total hack that seems to mask an issue on a PL2303-based adapter that on the first "open" after
   //  connecting it to USB, read() calls come back as all zeroes.  But on subsequent runs of the program, good data
   //  emerges instead.  Comparing tcgetattr() outputs in both cases, the termios settings are identical, so it's unclear
   //  what the root cause is, or why closing and reopening is a workaround.  If we ever find a root cause, and fix it, then
   //  this hack should be removed.
   if (fd != -1)
   {
      // close, reopen, re-init
      close(fd);
      fd = open(dev.c_str(), O_RDONLY);   
      if (fd != -1)
      {
	 if (initSerialDevice(fd, baud) == (speed_t)-1)
	 {
	    return false;
	 }
      }
   }
#endif   

   return fd != -1;
}

void usage(void)
{
 	printf("Usage: swt [-device serial_device] [-port n] [-srcbits n]\n");
	printf("\n");
	printf("-device serial_device:   The file system path of the serial device to use.  Default is /dev/ttyUSB0.\n");
	printf("-port n:   The TCP port on which swt will listen for client socket connections.  Default is 4567.\n");
	printf("-baud n:   The baud rate with which to set the serial port.  921600 tends to be the highest that works with standard serial drivers and common USB/serial adapter cables.  Must closely match the baud rate setting of the target's Probe Interface Block (PIB).  Default is 115200.\n");		
	printf("-srcbits n:   The size in bits of the src field in the trace messages. n must 0 to 8. Setting srcbits to 0 disables\n");
	printf("              multi-core. n > 0 enables multi-core. If the -srcbits=n switch is not used, srcbits is 0 by default.\n");
	printf("-autoexit:    This option causes the process to exit when the number of socket clients decreases from non-zero to zero\n");
	printf("-d:           Dump to standard output (for troubleshooting) the raw serial byte stream and reconstructed messages.\n");
	printf("-pthread:     This is for developer use (and for troubleshooting).  Not recommended unless you have a specific reason for using it.\n");
	printf("-h:           Display this usage information.\n");
}
