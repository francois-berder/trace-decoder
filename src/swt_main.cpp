#include "swt.hpp"
#include <iostream>
#include <cstdio>
#include <string.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

struct Args
{
   std::string serialdev;
   int srcbits;
   int port;
   bool help;
   bool autoexit;
   bool debug;

   Args()
   {
      serialdev = "/dev/ttyUSB0";  // overridden by -device argument
      srcbits = 0;  // overridden by -srcbits argument
      port = 4567;  // overridden by -port argument
      help = false; // overridden by -h option
      autoexit = false; // overridden by -autoexit option
      debug = false; // overridden by -debug option
   }
};

// prototypes
void parse_args(int argc, char *argv[], Args & args);
void usage(void);
bool openSerialDevice(const std::string &dev, int &fd);

// the main program
int main(int argc, char *argv[])
{
   Args args;

   parse_args(argc, argv, args);

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

   if (openSerialDevice(args.serialdev, serialFd))
   {
      IoConnections ioConnections(args.port, args.srcbits, serialFd, args.debug);
      
      while (! (args.autoexit && ioConnections.hasClientCountDecreasedToZero()) )
      {
	 ioConnections.serviceConnections();
      }
   }
   else
   {
      std::cerr << "Unable to open serial device " << args.serialdev << std::endl;
      return -1;
   }

   
   return 0;
}


void parse_args(int argc, char *argv[], Args & args)
{
   enum State {  NOT_IN_ARG, IN_DEVICE, IN_SRCBITS, IN_PORT };
   State state = NOT_IN_ARG;
      
   for (int i = 1; i < argc; i++)
   {
      if (state == IN_DEVICE)
      {
	 args.serialdev = argv[i];
	 state = NOT_IN_ARG;
      }
      else if (state == IN_SRCBITS)
      {
	 args.srcbits = atoi(argv[i]);
	 state = NOT_IN_ARG;
      }
      else if (state == IN_PORT)
      {
	 args.port = atoi(argv[i]);
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
	 else if (strcmp(argv[i], "-d") == 0)
	 {
	    args.debug = true;
	    // no change in state for argument-less options	    
	 }
	 else if (strcmp(argv[i], "-h") == 0)
	 {
	    args.help = true;
	    // no change in state for argument-less options
	 }
	 else if (strcmp(argv[i], "-autoexit") == 0)
	 {
	    args.autoexit = true;
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
	printf("-autoexit:    This option causes the process to exit when the number of socket clients decreases from non-zero to zero\n");
	printf("-d:           Dump to standard output (for troubleshooting) the raw serial byte stream and reconstructed messages.\n");	
	printf("-h:           Display this usage information.\n");
}
