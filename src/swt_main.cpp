#include "swt.hpp"
#include <iostream>
#include <cstdio>
#include <string.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


// prototypes
void parse_args(int argc, char *argv[], std::string &serialdev, int &srcbits, int &port, bool &help);
void usage(void);
bool openSerialDevice(const std::string &dev, int &fd);

// the main program
int main(int argc, char *argv[])
{
   std::string serialdev = "/dev/ttyUSB0";  // overridden by -device argument
   int srcbits = 0;  // overridden by -srcbits argument
   int port = 4567;  // overridden by -port argument
   bool help = false;

   parse_args(argc, argv, serialdev, srcbits, port, help);

   if (help)
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
   NexusStream ns(srcbits);
   int serialFd;

   if (openSerialDevice(serialdev, serialFd))
   {
      IoConnections ioConnections(port, srcbits, serialFd);
      
      while (!ioConnections.hasClientCountDecreasedToZero())
      {
	 ioConnections.serviceConnections();
      }
   }
   else
   {
      std::cerr << "Unable to open serial device " << serialdev << std::endl;
      return -1;
   }

   
   return 0;
}


void parse_args(int argc, char *argv[], std::string &serialdev, int &srcbits, int &port, bool &help)
{
   enum State {  NOT_IN_ARG, IN_DEVICE, IN_SRCBITS, IN_PORT };
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
	 else if (strcmp(argv[i], "-h") == 0)
	 {
	    help = true;
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


bool openSerialDevice(const std::string &dev, int &fd)
{
   struct termios options;
   int result;
      
   fd = open(dev.c_str(), O_RDONLY);

   if (fd != -1)
   {
      result = tcgetattr(fd, &options);
      if (result == 0)
      {
	 result = cfsetispeed(&options, B115200);  // no need to set output speed because we're only using the input channel
      }
      if (result == 0)
      {
	 result = tcsetattr(fd, TCSANOW, &options);
      }
      if (result != 0)
      {
	 std::cerr << "Error attempting to set serial device baud rate to 115200" << std::endl;
      }
   }

      
   return fd != -1;
}

void usage(void)
{
 	printf("Usage: swt [-device serial_device] [-port n] [-srcbits n]\n");
	printf("\n");
	printf("-device serial_device:   The file system path of the serial device to use.  Default is /dev/ttyUSB0.\n");
	printf("-port n:   The TCP port on which swt will listen for client socket connections.  Default is 4567.\n");	
	printf("-srcbits n:   The size in bits of the src field in the trace messages. n must 0 to 8. Setting srcbits to 0 disables\n");
	printf("              multi-core. n > 0 enables multi-core. If the -srcbits=n switch is not used, srcbits is 0 by default.\n");
	printf("-h:           Display this usage information.\n");
}
