#include "swt.hpp"
#include <iostream>
#include <cstdio>
#include <string.h>
#if defined(LINUX) || defined(OSX)
#include <termios.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>


struct Args
{
   std::string serialdev;
   int srcbits;
   int port;
   bool help;
   bool autoexit;
   bool debug;
   uint32_t baud;

   Args()
   {
      serialdev = "/dev/ttyUSB0";  // overridden by -device argument
      srcbits = 0;  // overridden by -srcbits argument
      port = 4567;  // overridden by -port argument
      help = false; // overridden by -h option
      autoexit = false; // overridden by -autoexit option
      debug = false; // overridden by -debug option
      baud = 115200; // overridden by -baud option
   }

   void parse(int argc, char *argv[]);
};

// prototypes
void usage(void);
bool openSerialDevice(const std::string &dev, int &fd, uint32_t requestedBaud);

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
      IoConnections ioConnections(args.port, args.srcbits, serialFd, args.debug);
      
      while (! (args.autoexit && ioConnections.hasClientCountDecreasedToZero()) )
      {
	 ioConnections.serviceConnections();
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
   enum State {  NOT_IN_ARG, IN_DEVICE, IN_SRCBITS, IN_PORT, IN_BAUD };
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
   else if (baud <= 460800)
      retval = B460800;
   else if (baud <= 500000)
      retval = B500000;
   else if (baud <= 576000)
      retval = B576000;
   else if (baud <= 921600)
      retval = B921600;
   else if (baud <= 1000000)
      retval = B1000000;
   else if (baud <= 1152000)
      retval = B1152000;
   else if (baud <= 1500000)
      retval = B1500000;
   else if (baud <= 2000000)
      retval = B2000000;
   else if (baud <= 2500000)
      retval = B2500000;
   else if (baud <= 3000000)
      retval = B3000000;
   else if (baud <= 3500000)
      retval = B3500000;
   else
      retval = B4000000;
   
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
      case B460800:
	 return 460800;
      case B500000:
	 return 500000;
      case B576000:
	 return 576000;
      case B921600:
	 return 921600;
      case B1000000:
	 return 1000000;
      case B1152000:
	 return 1152000;
      case B1500000:
	 return 1500000;
      case B2000000:
	 return 2000000;
      case B2500000:
	 return 2500000;
      case B3000000:
	 return 3000000;
      case B3500000:
	 return 3500000;
      case B4000000:
	 return 4000000;
      default:
	 return -1;
   }
}

speed_t setDeviceBaud(int fd, speed_t baud)
{
   struct termios options;
   speed_t readback;
   int result;
   
   // attempt to set baud rate to requested, return the actual baud rate read back
   // from device after the attempt
   result = tcgetattr(fd, &options);
   if (result == 0)
   {
      result = cfsetispeed(&options, baud);  // no need to set output speed because we're only using the input channel
   }
   if (result == 0)
   {
      result = tcsetattr(fd, TCSANOW, &options);
   }
   if (result == 0)
   {
      result = tcgetattr(fd, &options);
   }
   if (result == 0)
   {
      readback = cfgetispeed(&options);
      result = (readback == baud ? 0 : -1);
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
// stubbed out
typedef int speed_t;
speed_t nearestBaudRate(uint32_t baud)
{
	return 0;
}
speed_t setDeviceBaud(int fd, speed_t baud)
{
	return 0;
}
#endif

bool openSerialDevice(const std::string &dev, int &fd, uint32_t requestedBaud)
{
   speed_t baud = nearestBaudRate(requestedBaud);
      
   fd = open(dev.c_str(), O_RDONLY);

   if (fd != -1)
   {
      setDeviceBaud(fd, baud);
   }

#if 0  // on Windows, this call returns correct data with target in calibration mode, but also on Windows,
   // when select() gets
   // called, it never unblocks!  Need to continue investigating this!
   // TEMP SCAFFOLDING
   char tempbytes[1024];
   volatile int temp = read(fd, tempbytes, sizeof(tempbytes));
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
	printf("-h:           Display this usage information.\n");
}
