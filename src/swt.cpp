#include "swt.hpp"

#include <string.h>
#include <algorithm>
#include <memory>
#include <iostream>
#include <sstream>
#include <queue>

#include <time.h>
#if defined(LINUX) || defined(OSX)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#define closesocket close
#endif
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#ifdef WINDOWS
typedef int socklen_t;
#endif

// Internal debug scaffolding
// #define SWT_CPP_TEST 1
//#define DEBUG_PRINT 1
#define DEBUG_PTHREAD 1


#define QUEUE_STALLED_CLIENT_SUSPICION_THRESHOLD (512 * 1024)
#define SUSPECT_PROTOCOL_LINE_LENGTH_THRESHOLD 30

// globals (trying to minimize those)

bool useSimulatedSerialData;


// non-forward declarations

#ifdef DEBUG_PTHREAD
#define CHECK_PTHREAD_RESULT()  do { if ((pthread_result) != 0) {std::cerr << "Error: pthread_result is " << pthread_result << std::endl;} } while(0)
#else
#define CHECK_PTHREAD_RESULT() do { (void)pthread_result; } while(0)
#endif


// SwtTestMessageStream method definitions
SwtTestMessageStream::SwtTestMessageStream(std::vector<uint8_t> vec)
   : vec(vec)
{
   it = this->vec.begin();
}


bool SwtTestMessageStream::nextByte(uint8_t & ch)
{
   if (it == vec.end())
   {
      return false;
   }
   else
   {
      ch = *it++;
      return true;
   }
}




// Utility routines

void bytes_dump(uint8_t *buf, int numbits)
{
   // simple byte wise dump, from lower to higher addresses
   // not the same as bytes_dump; this one is a little endian view rather than a byte wise view
   char temp[50];
   int numbytes = (numbits+7)/8;
   for (int i = 0; i < numbytes; i++)
   {
      sprintf(temp, "%02X", (int)buf[i]);
      std::cout << temp;
   }
   std::cout << std::endl;
   
}

void buf_dump(uint8_t *buf, int numbits)
{
   // not the same as bytes_dump; this one is a little endian view rather than a byte wise view
   int numbytes = (numbits+7)/8;
   std::cout << std::hex;
   for (int i = numbytes-1; i >= 0; i--)
   {
      std::cout << (int)buf[i];
   }
   std::cout << std::endl;
}

int buf_get_bit(uint8_t *buf, int bitpos)
{
   int byteoffset = bitpos / 8;
   int bitoffset = bitpos % 8;

   return ((buf[byteoffset] & (1 << bitoffset)) ? 1 : 0);
}

void buf_set_bit(uint8_t *buf, int bitpos, int bitval)
{
   int byteoffset = bitpos / 8;
   int bitoffset = bitpos % 8;

   if (bitval)
   {
      buf[byteoffset] |= (1 << bitoffset);      
   }
   else
   {
      buf[byteoffset] &= ~(1 << bitoffset);      
   }
}

void u8_to_buf(uint8_t src, int numbits, uint8_t *buf, int bufbitpos)
{
   uint8_t val = src;
   int curpos = bufbitpos;
   for (int i = 0; i < numbits; i++)
   {
      int bit = val & 0x1;
      buf_set_bit(buf, curpos, bit);

      val >>= 1;
      curpos++;
   }
}

uint64_t buf_to_u64(uint8_t *buf, int bufbitpos, int numbits)
{
   uint64_t val = 0;
   for (int i = 0; i < numbits; i++)
   {
      uint64_t bit = buf_get_bit(buf, bufbitpos+i);
      val |= (bit << i);
   }
   return val;
}


// NexusSliceUnwrapper method definitions
NexusSliceUnwrapper::NexusSliceUnwrapper(NexusSliceAcceptor &acceptor)
   : acceptor(acceptor), inMessage(false), datanumbits(0), dataoverflowed(false)
{
   emptyData();
}

void NexusSliceUnwrapper::emptyData()
{
   memset(data, 0, sizeof(data));
   dataoverflowed = false;
   datanumbits = 0;
}
   
void NexusSliceUnwrapper::appendByte(uint8_t byte)
{
   int meso = byte & 0x3;
   int mdo = byte >> 2;

   if (!inMessage && meso == 0)
   {
      // we'll be optimistic that this is the start of a message
      inMessage = true;
      acceptor.startMessage(mdo);

      // don't include TCODE in the message data, since we already sent it here
   }
   else if (inMessage)
   {
      if (datanumbits < sizeof(data)*8 - 6)
      {
	 u8_to_buf(mdo, 6, data, datanumbits);
	 datanumbits += 6;
      }
      else
      {
	 dataoverflowed = true;
      }
      if (meso != 0)
      {
	 acceptor.messageData(datanumbits, data, dataoverflowed);
	 if ((meso & 0x1) != 0)
	 {
	    acceptor.endField();
	 }
	 if ((meso & 0x2) != 0)
	 {
	    acceptor.endMessage();
	    inMessage = false;
	 }
	 emptyData();
      }
   }
   else
   {
      // ignore this slice. we must have caught the stream in the middle of a message.
      // We'll catch the next meso==0 and resync there
   }
}


// TestAcceptor method definitions
void TestAcceptor::startMessage(int tcode)
{
   std::cout << "start message, tcode = " << std::dec << tcode << std::endl;
}

void TestAcceptor::messageData(int numbits, uint8_t *data, bool overflowed)
{
   std::cout << "message data, numbits=" << std::dec << numbits << ", overflowed=" << overflowed << std::endl;
   buf_dump(data, numbits);
}
   
void TestAcceptor::endField()
{
   std::cout << "end field" << std::endl;
}

void TestAcceptor::endMessage()
{
   std::cout << "end message" << std::endl;      
}


// NexusDataAcquisitionMessage method definitions
NexusDataAcquisitionMessage::NexusDataAcquisitionMessage()
{
   clear();
}

void NexusDataAcquisitionMessage::clear()
{
   tcode = 0x7;
   haveTimestamp = false;
   timestamp = 0;
   haveSrc = false;
   src = 0;
   idtag = 0;
   dqdata = 0;
}

std::string NexusDataAcquisitionMessage::serialized()
{
   std::ostringstream out;

   out << std::hex;  // hexadecimal for all values

   out << "tcode=" << tcode;      

   if (haveSrc)
   {
      out << " src=" << src;
   }
   out << " idtag=" << idtag;
   out <<  " dqdata=" << dqdata;
   if (haveTimestamp)
   {
      out << " timestamp=" << timestamp;

   }
      
   out << std::endl;
   return out.str();
}

void NexusDataAcquisitionMessage::dump()
{
   std::cout << std::dec << "tcode = " << tcode << ", haveTimestamp = " << haveTimestamp << ", timestamp = 0x" <<
      std::hex << timestamp << ", haveSrc = " << haveSrc << ", src = " << std::dec <<
      src << ", idtag = 0x" << std::hex << idtag << ", dqdata = 0x" << dqdata
	     << std::endl;
}


// NexusMessageReassembler method definitions

NexusMessageReassembler::NexusMessageReassembler(int srcbits)
   : srcbits(srcbits), acceptingMessage(false), fieldcount(0), 
     messageHasOverflowedField(false), messageReady(false)
{

}
void NexusMessageReassembler::startMessage(int tcode)
{
#ifdef DEBUG_PRINT
   std::cout << "start message, tcode = " << std::dec << tcode << std::endl;      
#endif
   messageReady = false;
   messageHasOverflowedField = false;
   fieldcount = 0;
   dqm.clear();      
   acceptingMessage = (tcode == 7);
}

void NexusMessageReassembler::messageData(int numbits, uint8_t *data, bool overflowed)
{
#ifdef DEBUG_PRINT      
   std::cout << "message data, numbits=" << std::dec << numbits << ", overflowed=" << overflowed << std::endl;      
   buf_dump(data, numbits);
#endif      
      
   if (acceptingMessage)
   {
      if ( /* fieldcount > 3 || */ overflowed)
      {
#if 0	    
	 // evidently we encountered a message with TCODE=7 but more than 3 variable fields, which seems like
	 //  the message was corrupted somehow (serial noise?).   Maybe have an option for logging this if it happens?
#ifdef DEBUG_PRINT
	 std::cout << "Message has too many fields, or had a field with much larger width than is reasonable for actual data" << std::endl;
#endif	    
	 acceptingMessage = false;
#endif
	 // don't do anything with an overflowed field, except remember for later that we saw an overflowed field
	 messageHasOverflowedField = true;
      }
      else
      {
	 int tagBitPos = 0;
	 switch (fieldcount)
	 {
	    case 0:
	       if (srcbits != 0)
	       {
		  tagBitPos = srcbits;
		  dqm.haveSrc = true;
		  dqm.src = buf_to_u64(data, 0, srcbits);
	       }
	       dqm.idtag = buf_to_u64(data, tagBitPos, numbits-srcbits);
	       break;
	    case 1:
	       dqm.dqdata = buf_to_u64(data, 0, std::min(numbits, 32));
	       break;
	    case 2:
	       dqm.haveTimestamp = true;
	       dqm.timestamp = buf_to_u64(data, 0, std::min(numbits, 64));
	       break;
	    default:
	       // Message must be malformed (extra fields) We're not prepared to put this data anywhere,
	       //  so just drop it.  And later we'll see the higher than expected fieldcount, and decide
	       //  mot to treat this as a valid message.
	       break;
	 }
      }
   } else {
      // guess we're in a message we don't care about?
   }
}
   
void NexusMessageReassembler::endField()
{
#ifdef DEBUG_PRINT
   std::cout << "end field" << std::endl;
#endif      
   fieldcount++;
}

void NexusMessageReassembler::endMessage()
{
#ifdef DEBUG_PRINT
   std::cout << "end message" << std::endl;
#endif      
   if (acceptingMessage)
   {
      acceptingMessage = false;
      if (fieldcount >= 2 && fieldcount <= 3 && !messageHasOverflowedField)
      {
	 messageReady = true;
      }
      else
      {
#ifdef DEBUG_PRINT
	 std::cout << "Message discarded because the number of fields doesn't match what is expected for valid data, or a field value overflowed" << std::endl;
	 std::cout << "fieldcount = " << std::dec << fieldcount << ", messageHasOverflowedField = " << messageHasOverflowedField << std::endl;
#endif	    
      }
   }
#ifdef DEBUG_PRINT
   if (messageReady)
   {
      std::cout << "endMessage: ";
      dqm.dump();
      std::cout << std::endl;
   }
#endif
}

bool NexusMessageReassembler::getMessage(NexusDataAcquisitionMessage &msg)
{
   bool ready = messageReady;
   if (ready)
   {
      msg = dqm;
      messageReady = false;  // the act of getting a message also consumes it
   }
   return ready;
}


// NexusStream method definitions

NexusStream::NexusStream(int srcbits)
   : reassembler(srcbits), unwrapper(reassembler)
{

}

bool NexusStream::appendByteAndCheckForMessage(uint8_t byte, NexusDataAcquisitionMessage &msg)
{
   unwrapper.appendByte(byte);
   bool gotMessage = reassembler.getMessage(msg);
   return gotMessage;
}




// SwtMessageStreamBuilder method definitions

SwtByteStream *SwtMessageStreamBuilder::makeByteStream()
{
   return new SwtTestMessageStream(slices);
}

void SwtMessageStreamBuilder::freeByteStream(SwtByteStream *stream)
{
   delete stream;
}
   
void SwtMessageStreamBuilder::addDataAcquisitionMessage(int srcbits, int src, int itcIdx, int size, uint32_t data, bool haveTimestamp, uint64_t timestamp)
{
   uint32_t tag = itcIdx * 4;
   if (size == 2)
   {
      tag += 2;
   } else if (size == 1)
   {
      tag += 3;
   }
	 
   beginMessage();
   appendFixedField(0x7, 6, false);  // fill up first slice
   appendFixedField(src, srcbits, false);
   appendVarField(tag, sizeof(tag)*8, false);
   appendVarField(data, size*8, !haveTimestamp);

   if (haveTimestamp)
   {
      appendVarField(timestamp, 64, true);
   }
}
   
void SwtMessageStreamBuilder::addMalformedDataAcquisitionMessageNoTag(int srcbits, int src, int itcIdx, int size, uint32_t data, bool haveTimestamp, uint64_t timestamp)
{
   uint32_t tag = itcIdx * 4;
   if (size == 2)
   {
      tag += 2;
   } else if (size == 1)
   {
      tag += 3;
   }
	 
   beginMessage();
   appendFixedField(0x7, 6, false);  // fill up first slice
   appendFixedField(src, srcbits, false);
   // This is the part omitted: appendVarField(tag, sizeof(tag)*8, false);
   appendVarField(data, size*8, !haveTimestamp);

   if (haveTimestamp)
   {
      appendVarField(timestamp, 64, true);
   }
}
   
void SwtMessageStreamBuilder::addRandomizedSlice()
{
   curslice = rand();
   flushSlice();
}

void SwtMessageStreamBuilder::addLiteralSlice(uint8_t slice)
{
   curslice = slice;
   flushSlice();
}
   
void SwtMessageStreamBuilder::dump()
{
   std::cout << "Slices:" << std::endl;
   for (std::vector<uint8_t>::size_type i = 0; i < slices.size(); i++) {
      std::cout << std::hex << slices.at(i) << std::endl;
   }
}
   
void SwtMessageStreamBuilder::beginMessage()
{
   curslice = 0;
   cursliceOffset = 2;
}

void SwtMessageStreamBuilder::append(uint64_t val, int numbits)
{
   int left = numbits;
   while (left != 0)
   {
      int chunk = std::min(left, 8-cursliceOffset);

      int mask = (1 << chunk) - 1;

      if (cursliceOffset == 8)
      {
	 // OK to flush here without setting any of the lower 2 bits,
	 // because by definition the slice wrap is happening within a
	 // particular field, since there are bytes left to add,
	 // which means the slice being flushed is neither an end of var
	 // field nor end of message
	 flushSlice();  
      }
	 
      curslice |= ((val & mask) << cursliceOffset);

      left -= chunk;
      val >>= chunk;
      cursliceOffset += chunk;
   }
}

void SwtMessageStreamBuilder::appendFixedField(uint64_t val, int numbits, bool endOfMessage)
{
   append(val, numbits);
   // Only set EOM indicator and flush if we're at the end of message.
   // Fixed fields don't consume any extra slice space
   if (endOfMessage)
   {
      curslice |= 0x3;	 
      flushSlice();
   }
}

void SwtMessageStreamBuilder::appendVarField(uint64_t val, int numbits, bool endOfMessage)
{
   append(val, minBits(val, numbits));
   curslice |= (endOfMessage ? 0x3 : 0x1);
   // always flush at end of var field because var field fills up its last slice with padded zeroes
   flushSlice();
}


void SwtMessageStreamBuilder::flushSlice()
{
   //
   slices.push_back(curslice);
   curslice = 0;
   cursliceOffset = 2;
}

int SwtMessageStreamBuilder::minBits(uint64_t val, int numbits)
{
   int result = numbits;
   int bit;

   for (bit = numbits-1; bit >= 0; bit--)
   {
      int mask = 1 << bit;
      if ((val & mask) == 0)
      {
	 --result;
      }
      else
      {
	 break;
      }
   }
   // Need at least 1 bit for any variable field written (even zero)
   //  to fit in with the way the slice advancement/flushing code works
   return std::max(result, 1);  
}
   




// IoConnection method definitions

void *IoConnections::ThreadFuncSerial(void *arg)
{
   IoConnections *io = (IoConnections *)arg;
   int pthread_result = -1;
   uint8_t bytes[SERIAL_BUFFER_NUMBYTES];
   bool mutexLocked = false;

   for (;;)
   {

      int numread = read(io->serialFd, bytes, sizeof(bytes));

      pthread_result = pthread_mutex_lock(&io->pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();
      mutexLocked = true;

      if (io->pthreadModeData.exitThreadRequested)
      {
	 break;
      }
      else if (numread > 0)
      {
	 // append to buffered serial data
#ifdef DEBUG_PRINT	 
	 std::cout << "Appending " << numread << " bytes to lookahead" << std::endl;
#endif	 
	    
	 io->pthreadModeData.serialLookahead.append((const char*)bytes, numread);
      }

      // notify all, in case that's part of their wait predicate
      pthread_result = pthread_cond_broadcast(&io->pthreadModeData.conditionSomethingChanged);
      CHECK_PTHREAD_RESULT();
      pthread_result = pthread_mutex_unlock(&io->pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();
      mutexLocked = false;
   }


   if (mutexLocked)
   {
      pthread_result = pthread_mutex_unlock(&io->pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();
      mutexLocked = false;
   }

   return NULL;
}


void *IoConnections::ThreadFuncSelect(void *arg)
{
   IoConnections *io = (IoConnections *)arg;
   int pthread_result = -1;
   int selectResult = -1;
   bool mutexLocked = false;
   fd_set readfds;
   fd_set writefds;
   fd_set exceptfds;

   for (;;)
   {
      pthread_result = pthread_mutex_lock(&io->pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();
      mutexLocked = true;      

      // wait for a select request to be ready, or for thread exit to be requested
      while (! (io->pthreadModeData.selectRequestValid || io->pthreadModeData.exitThreadRequested) )
      {
	 pthread_cond_wait(&io->pthreadModeData.conditionSomethingChanged, &io->pthreadModeData.mutex);
      }

      if (io->pthreadModeData.exitThreadRequested)
      {
	 break;
      }

      pthread_result = pthread_mutex_unlock(&io->pthreadModeData.mutex);  // release mutex so that the select() call can block without holding the mutex
      CHECK_PTHREAD_RESULT();
      mutexLocked = false;      

      // call select here
      selectResult = io->callSelect(false, &readfds, &writefds, &exceptfds);  // omit serial port in select() call for this particular mode; it's being handled differently



      // update the thread synchronized state
      pthread_result = pthread_mutex_lock(&io->pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();
      mutexLocked = true;

      io->pthreadModeData.selectResponseAck = false;

      
      // update the thread state with the select result and copies of the fd_sets
      io->pthreadModeData.selectResult = selectResult;
      io->pthreadModeData.selectResultReadSet = readfds; 
      io->pthreadModeData.selectResultWriteSet = writefds;
      io->pthreadModeData.selectResultExceptSet = exceptfds;
      io->pthreadModeData.selectResponseValid = true;      

      // wake up any waiters
      pthread_result = pthread_cond_broadcast(&io->pthreadModeData.conditionSomethingChanged);  // so select thread can resume if it is waiting on any of the data changes we just made
      CHECK_PTHREAD_RESULT();

      // wait for response to be acked
      while (! (io->pthreadModeData.selectResponseAck || io->pthreadModeData.exitThreadRequested) )
      {
	 pthread_cond_wait(&io->pthreadModeData.conditionSomethingChanged, &io->pthreadModeData.mutex);
      }

      if (io->pthreadModeData.exitThreadRequested)
      {
	 break;
      }
      
      pthread_result = pthread_mutex_unlock(&io->pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();
      mutexLocked = false;	 
   }

   if (mutexLocked)
   {
      pthread_result = pthread_mutex_unlock(&io->pthreadModeData.mutex);  // release mutex so that the select() call can block without holding the mutex
      CHECK_PTHREAD_RESULT();
   }

   return NULL;
}


IoConnection::IoConnection(int fd) : fd(fd), withholding(false),
				     itcFilterMask(0x0)
{
      
}

IoConnection::~IoConnection()
{
   // was closing the file descriptor in the destructor, but
   //  this doesn't work so well because some ephemeral copies of
   // this class end up getting constructed/destructed, so descriptor
   // was getting prematurely closed.
}

void IoConnection::disconnect()
{
   if (fd != -1)
   {
      close(fd);
      fd = -1;
   }
}

void IoConnection::enqueue(const std::string &str)
{
   bytesToSend.append(str);
}

int IoConnection::getQueueLength()
{
   return bytesToSend.length();
}


// IoConnections (plural!) method definitions

IoConnections::IoConnections(int port, int srcbits, int serialFd, bool userDebugOutput, bool pthreadSynchronizationMode)
   : ns(srcbits), serialFd(serialFd), numClientsHighWater(0),
     pthreadSynchronizationMode(pthreadSynchronizationMode),  // this mode is only necessary for Windows; on POSIX-based OSes there's no reason to use this except for testing perhaps
     warnedAboutSerialDeviceClosed(false),
     userDebugOutput(userDebugOutput)
{
   struct sockaddr_in address = {0}; 
   int opt = 1;

   
   // Creating socket file descriptor 
   if ((serverSocketFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
   {
      std::cerr << "Attempt to create server socket failed" << std::endl;
      exit(-1);
   } 
       
   // Allow reuse of binding address
   if (setsockopt(serverSocketFd, SOL_SOCKET, SO_REUSEADDR /* | SO_REUSEPORT */, 
		  (const char *)&opt, sizeof(opt))) 
   {
      int err = errno;
      (void)err;  // quash compile warning, but being able to see err in debugger would be nice.
      std::cerr << "Attempt to set server socket options failed" << std::endl;
      exit(-1);
   } 
   address.sin_family = AF_INET; 
//   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // INADDR_ANY would allow cross host usage of this server, but that's not the intent, and opens up security issues
   address.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY would allow cross host usage of this server, but that's not the intent, and opens up security issues
   address.sin_port = htons( port );

    // Forcefully attaching socket to the port
    if (bind(serverSocketFd, (struct sockaddr *)&address,  
                                 sizeof(address))<0) 
    { 
       std::cerr << "Attempt to bind server socket to port failed" << std::endl; 
       exit(-1);
    } 
    if (listen(serverSocketFd, 3) < 0) 
    { 
       std::cerr << "Attempt to place server socket into listen mode failed" << std::endl; 
       exit(-1);
    }


    // scaffolding
    makeSimulatedSerialPortStream();


    if (pthreadSynchronizationMode)
    {
       // start select and serial threads
       int pthread_result = -1;

       pthread_result = pthread_mutex_init(&pthreadModeData.mutex, NULL);
       CHECK_PTHREAD_RESULT();
       pthread_result = pthread_cond_init(&pthreadModeData.conditionSomethingChanged, NULL);
       CHECK_PTHREAD_RESULT();
       pthread_result = pthread_create(&pthreadModeData.selectThread, NULL, ThreadFuncSelect, this);
       CHECK_PTHREAD_RESULT();
       pthread_result = pthread_create(&pthreadModeData.serialThread, NULL, ThreadFuncSerial, this);
       CHECK_PTHREAD_RESULT();
    }
}

bool IoConnections::hasClientCountDecreasedToZero()
{
   return numClientsHighWater > 0 && connections.size() == 0;
}


void IoConnections::makeSimulatedSerialPortStream()
{
   const int SRCBITS = 6;
   sb.addDataAcquisitionMessage(SRCBITS, 0x3F, 0, 4, 0x12345678, true, 0x1234567855555555UL);
   sb.addDataAcquisitionMessage(SRCBITS, 0x01, 31, 1, 0x12, true, 0x1234567855555556UL);
   sb.addDataAcquisitionMessage(SRCBITS, 0x77, 0, 4, 0x12345678, true, 0x1234567855555557UL);
      
   simulatedSerialStream = sb.makeByteStream();
}

bool IoConnections::isItcFilterCommand(const std::string& str, uint32_t& filterMask)
{
   
   size_t pos = str.find("itcfilter", 0);
   if (pos != std::string::npos)
   {
      // parse filter mask
      const char *pch = str.c_str();
      pos += 9;  // advance past "itcfilter"
      filterMask = 0;

      // advance past spaces
      while (isspace(pch[pos]))
      {
	 pos++;
      }

      while (pch[pos] != '\0' && !isspace(pch[pos]))
      {
	 int nybble = 0;
	 if (pch[pos] >= '0' && pch[pos] <= '9')
	 {
	    nybble = pch[pos] - '0';	 
	 }
	 else if (toupper(pch[pos]) >= 'A' && toupper(pch[pos]) <= 'F')
	 {
	    nybble = (pch[pos] - 'A') + 10;
	 }

	 filterMask = (filterMask << 4) | nybble;
	 pos++;
      }
      return true;
   }
   return false;
}


bool IoConnections::isSocketReadable(int fd)
{
   int pthread_result = -1;
   bool result;
   
   if (pthreadSynchronizationMode)
   {
      pthread_result = pthread_mutex_lock(&pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();      

      result = pthreadModeData.selectResponseValid && FD_ISSET(fd, &pthreadModeData.selectResultReadSet);
      
      pthread_result = pthread_mutex_unlock(&pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();      
   }
   else
   {
      result = pthreadModeData.selectResult && FD_ISSET(fd, &readfds);
   }

#ifdef DEBUG_PRINT	    
   std::cout << "isSocketReadable(" << fd << ") returned " << result << std::endl;
#endif   
   return result;
}

bool IoConnections::isSocketWritable(int fd)
{
   bool result;

   // immediate poll, because we don't include writability checks in the main wait (because writability would cause main select to unblock constantly)
   fd_set writefds;
   int nfds;
   struct timeval zerotimeout = {0};
   int selectResult;
   
   FD_ZERO(&writefds);
   FD_SET(fd, &writefds);
   nfds = fd;
   selectResult = select(nfds+1, NULL, &writefds, NULL, &zerotimeout);
   result = (selectResult > 0 && FD_ISSET(fd, &writefds));

#ifdef DEBUG_PRINT
   std::cout << "isSocketWritable(" << fd << ") returned " << result << std::endl;
#endif   
   return result;
}

bool IoConnections::isSocketExcept(int fd)
{
   int pthread_result = -1;
   bool result;
   
   if (pthreadSynchronizationMode)
   {
      pthread_result = pthread_mutex_lock(&pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();      

      result = pthreadModeData.selectResponseValid && FD_ISSET(fd, &pthreadModeData.selectResultExceptSet);
      
      pthread_result = pthread_mutex_unlock(&pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();      
   }
   else
   {
      result = pthreadModeData.selectResult != -1 && FD_ISSET(fd, &exceptfds);
   }

#ifdef DEBUG_PRINT
   std::cout << "isSocketExcept(" << fd << ") returned " << result << std::endl;
#endif   
   return result;
}

bool IoConnections::didSerialDisconnect()
{
   return serialFd == -1;
}

void IoConnections::setSerialDevice(int serialFd)
{
   this->serialFd = serialFd;
   warnedAboutSerialDeviceClosed = false;
}

bool IoConnections::isSerialReadable()
{
   int pthread_result = -1;
   bool result;
   
   if (pthreadSynchronizationMode)
   {
      pthread_result = pthread_mutex_lock(&pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();      
      result = pthreadModeData.serialLookahead.size() > 0;
      pthread_result = pthread_mutex_unlock(&pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();      
   }
   else
   {
      result = pthreadModeData.selectResult != -1 && FD_ISSET(serialFd, &readfds);
   }

#ifdef DEBUG_PRINT   
   std::cout << "isSerialReadable() returned " << result << std::endl;
#endif   
   return result;
}



void IoConnections::serviceConnections()
{
   if (waitForIoActivity())
   {
      // check for new connection (see if server socket is readable)
      if (isSocketReadable(serverSocketFd))
      {
#ifdef DEBUG_PRINT	 
	 std::cout << "Server socket is readable!" << std::endl;
#endif	 
	 // Accept the data packet from client and verification
	 struct sockaddr_in clientAddr;
	 socklen_t len = sizeof(clientAddr);
	 // std::cout << "about to call accept()" << std::endl;	 	 
	 int fd = accept(serverSocketFd, (struct sockaddr *)&clientAddr, &len);
	 // std::cout << "accept() returned " << fd << std::endl;	 
	 if (fd >= 0)
	 {
	    if (userDebugOutput)
	       std::cout << "accept() returned a new connection!" << std::endl;

	    const int sendbufSize = 1024*1024;	    
	    int result = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char *)&sendbufSize, sizeof(sendbufSize));
	    if (result != 0)
	       std::cerr << "setsockopt of SO_SNDBUF to " << sendbufSize << " returned " << result << std::endl;
	    IoConnection connection(fd);
	    connections.push_back(std::move(connection));
	    numClientsHighWater = std::max(numClientsHighWater, connections.size());
	 }
	 else
	 {
	    // new client connection not established, nothing to do except log it
	    std::cerr << "accept() result is " << fd << ", errno is " << errno << std::endl;	    
	 }
      }

      // If we have serial input available, lets consume all that is immediately available,
      // and enqueue any resulting Nexus messages to connected clients
      uint8_t bytes[SERIAL_BUFFER_NUMBYTES];
      int numSerialBytesRead;
      NexusDataAcquisitionMessage msg;
      if (isSerialReadable())
      {
	 do
	 {
	    numSerialBytesRead = serialReadBytes(bytes, sizeof(bytes));

	    if (numSerialBytesRead <= 0)
	    {
	       if (!warnedAboutSerialDeviceClosed)
	       {
		  std::cerr << "Serial device was disconnected" << std::endl;
		  warnedAboutSerialDeviceClosed = true;		  
	       }
	       close(serialFd);
	       serialFd = -1;
	    }
	    if (numSerialBytesRead > 0)
	    {
	       if (userDebugOutput)
	       {
		  std::cout << "Received " << numSerialBytesRead << " bytes from serial device: ";
		  bytes_dump(bytes, numSerialBytesRead*8);
	       }
	       
	       if (userDebugOutput)
	       {
		  for (int i = 0; i < numSerialBytesRead; i++)
		  {
		     bool haveMessage = ns.appendByteAndCheckForMessage(bytes[i], msg);
		     if (haveMessage)
		     {
			// Dump reconstructed Nexus message to stdout for debugging, but don't transmit this level of
			// abstraction to the client (client cares about the raw slice stream)
			std::cout << "Nexus message: ";
			msg.dump();
		     }
		  }
	       }

	       // shuttle the raw slice stream bytes to clients, with no translation or filtering
	       queueSerialBytesToClients(bytes, numSerialBytesRead);
	    }
	 } while (numSerialBytesRead == sizeof(bytes));
      }

      std::list<IoConnection>::iterator it;

      // Service all readable clients, in case they have disconnected
      it = connections.begin();
      while (it != connections.end())
      {
	 // uint8_t buf[1024];
	 char buf[1024];  // Windows wants this to be char instead of uint_8
	 if (isSocketReadable(it->fd))
	 {
	    int numrecv = recv(it->fd, buf, sizeof(buf), 0);
//	    std::cout << "numrecv = " << numrecv << std::endl;
	    if (numrecv <= 0)
	    {
	       // std::cerr << "Client disconnected!" << std::endl;
	       it->disconnect();
	       it = connections.erase(it);
	    }
	    else
	    {
#if OLD_CODE_KEEPING_HERE_IN_CASE_PROTOCOL_IS_CHANGED_TO_BE_BIDIRECTIONAL_SOMEHOW
	       // append chunk of input to per-connection input buffer,
	       // run through the per-connection buffer to find newline-terminated segments,
	       // act on them or reject them.  If there are more than, say, 256 characters without
	       // a newline, then just erase the entire per-connection buffer so we don't run out
	       // of memory if client is misbehaving in what it is sending over.
	       it->bytesReceived.append((const char*)buf, numrecv);
	       size_t newlinePos;
	       while ((newlinePos = it->bytesReceived.find('\n')) != std::string::npos)
	       {
		  std::string command = it->bytesReceived.substr(0, newlinePos);

		  // Server doesn't support any particular commands (it's a one-way conduit currently)
		  // Not much to do except delete the buffer (in original design there were protocol commands
		  // and just leaving this in for now)
		  
		  // std::cout << "Erasing line of length " << newlinePos << std::endl;		  
		  it->bytesReceived.erase(0, newlinePos+1);
	       }
	       // Any residual left in bytesReceived has no newline.
	       // If bytesReceived reaches a length that is much too high for the protocol that's expected,
	       //  then we're receiving invalid or hostile input, so let's empty bytesReceived just so
	       // memory pressure doesn't become an issue
	       if (it->bytesReceived.length() >= SUSPECT_PROTOCOL_LINE_LENGTH_THRESHOLD)
	       {
		  // std::cout << "Clearing bytes received" << std::endl;
		  it->bytesReceived.clear();
	       }
#else
	       // Nothing really to do except drop whatever was read; we don't act on incoming socket traffic;
	       //  we really just want to detect EOF so we can react to the dropped connection
#endif	       
	       it++;
	    }
	 }
	 else
	 {
	    // not readable, so don't read
	    it++;
	 }
      }


      it = connections.begin();      
      // For all connections that we have data to send, and if socket is writable, then try to send all remaning queued bytes but be prepared that socket may only accept some of the bytes
      while (it != connections.end())      
      {
	 if (!it->bytesToSend.empty())  // && isSocketWritable(it->fd)) ---- no, just try to send, and if there is an error return then we know there was a disconnect
	 {
	    const char *data = it->bytesToSend.data();
	    int numsent = send(it->fd, data, it->bytesToSend.length(), 0);
	    if (numsent < 0)
	    {
	       it->disconnect();
	       // std::cout << "send() returned " << numsent << ", disconnect!" << std::endl;
	       it = connections.erase(it);
	    }
	    else
	    {
	       // remove the sent bytes from the queue
	       if (userDebugOutput)
	       {
		  std::cout << "Bytes sent to client over socket: ";
		  bytes_dump((uint8_t*)data, numsent*8);
	       }
	       it->bytesToSend.erase(0, numsent);
	       it++;
	    }
	 }
	 else
	 {
	    it++;
	 }
      }
   }
   else
   {
      // Nothing to do; we'll try to do more when called again
   }
}


void IoConnections::closeResources()
{
   int pthread_result = -1;
   
   if (pthreadSynchronizationMode)
   {
      pthread_result = pthread_mutex_lock(&pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();      
      pthreadModeData.exitThreadRequested = true;  // set the exit flag before closing devices/sockets, so threads will see the flag after unblocking
      pthread_result = pthread_cond_broadcast(&pthreadModeData.conditionSomethingChanged);  // so threads can see the exit request after wait() unblocks 
      CHECK_PTHREAD_RESULT();     
      pthread_result = pthread_mutex_unlock(&pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();      
   }

   // close server socket and serial socket, to allow any blocked select() or read() calls in other threads to return
   if (serialFd != -1)
   {
      close(serialFd);
      serialFd = -1;
   }

   if (serverSocketFd != -1)
   {
      closesocket(serverSocketFd);
      serverSocketFd = -1;      
   }

   if (pthreadSynchronizationMode)
   {
      // wait for other threads to terminate
      pthread_result = pthread_join(pthreadModeData.selectThread, NULL);
      CHECK_PTHREAD_RESULT();      
      pthread_result = pthread_join(pthreadModeData.serialThread, NULL);
      CHECK_PTHREAD_RESULT();      

      // cleanup mutex and condition variable
      pthread_result = pthread_cond_destroy(&pthreadModeData.conditionSomethingChanged);
      CHECK_PTHREAD_RESULT();      
      pthread_result = pthread_mutex_destroy(&pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();      
   }
}

int IoConnections::serialReadBytes(uint8_t *bytes, size_t numbytes)
{
   if (useSimulatedSerialData)
   {
      return simulatedSerialReadBytes(bytes, numbytes);
   }
   else if (pthreadSynchronizationMode)
   {
      /* 
	 get mutex
	 copy from internal buffer that was filled by serial I/O thread
	 remove those bytes from internal buffer
	 maybe do a notifyAll if serial thread might be waiting because buffer was full
	 release mutex
      */
      int pthread_result;
      int numread = 0;

#ifdef DEBUG_PRINT      
      std::cout << "waiting for mutex in serial read bytes" << std::endl;
#endif      
      pthread_result = pthread_mutex_lock(&pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();      
      
#ifdef DEBUG_PRINT            
      std::cout << "got mutex serial read bytes" << std::endl;      
#endif
      numread = std::min(pthreadModeData.serialLookahead.size(), numbytes);
      memcpy(bytes, pthreadModeData.serialLookahead.data(), numread);

      pthreadModeData.serialLookahead.erase(0, numread);
#ifdef DEBUG_PRINT      
      std::cout << "Erasing " << numread << " bytes from start of lookahead" << std::endl;
#endif      

      pthread_result = pthread_cond_broadcast(&pthreadModeData.conditionSomethingChanged); // in case other thread was waiting for our buffer to get drained; if not, then any waiters will check their predicate condition and wait again
      CHECK_PTHREAD_RESULT();      

#ifdef DEBUG_PRINT            
      std::cout << "releasing mutex in serial read bytes" << std::endl;
#endif      
      pthread_result = pthread_mutex_unlock(&pthreadModeData.mutex);
      CHECK_PTHREAD_RESULT();      
      
      return numread;
   }
   else
   {
      // normal synchronous read, which should return something because calling code should have called
      //  isSerialReable first
      return read(serialFd, bytes, numbytes);
   }
}


int IoConnections::simulatedSerialReadBytes(uint8_t *bytes, size_t numbytes)
{
   // Development scaffolding - maybe this isn't needed anymore, but maybe it could be useful for future unit tests, so keeping it around

   static bool already_run = 0;
   static time_t before;
   time_t now;

   if (!already_run)
   {
      before = time(NULL);
      already_run = true;
      return 0;
   }

   now = time(NULL);
   if (now-before < 30)
   {
      return 0;
   }

   size_t left = numbytes;
   int offset = 0;
   while (left)
   {
      if (simulatedSerialStream->nextByte(bytes[offset]))
      {
	 offset++;
	 left--;
      }
      else
      {
	 break;
      }
   }
   return offset;
}

bool IoConnections::waitForIoActivity()
{
   bool result = doWaitForIoActivity();  // was calling this in a loop, but no need
#ifdef DEBUG_PRINT   
   std::cout << "waitForIoActivity returned " << result << std::endl;
#endif   
   return result;
}


bool IoConnections::doWaitForIoActivity()
{
   if (pthreadSynchronizationMode)
   {
	return waitUsingThreadsAndConditionVar();
   }
   else
   {
	return waitUsingSelectForAllIo();
   }
}

int IoConnections::callSelect(bool includeSerialDevice, fd_set *readfds, fd_set *writefds, fd_set *exceptfds)
{
   int nfds = 0;
   struct timeval *ptimeout = NULL;  // no timeout, for now (no current reason to use a timeout)
   
   FD_ZERO(readfds);
   FD_ZERO(writefds);
   FD_ZERO(exceptfds);

   // always include server socket in read set (and exception set; that's how we'll know when a new socket connection is being made)
   FD_SET(serverSocketFd, readfds);
   FD_SET(serverSocketFd, exceptfds);
   nfds = serverSocketFd;

   // std::cout << "added server socket to sets" << std::endl;

   // serial port device, which, in our particular case,  is readable only
   if (includeSerialDevice && serialFd != -1)
   {
      FD_SET(serialFd, readfds);
      nfds = std::max(nfds, serialFd);
      // std::cout << "added serial descriptor to sets" << std::endl;      
   }

   // We do want to find out in a timely manner when a connection has dropped, so let's add any client
   //  sockets to the read set and except set (not the write set, that would cause select() to almost always
   //  return immediately even if there isn't any data to move, which defeats the purpose of the efficient wait).
   for (std::list<IoConnection>::iterator it = connections.begin(); it != connections.end(); it++)
   {
      FD_SET(it->fd, readfds);
      FD_SET(it->fd, exceptfds);            
      nfds = std::max(nfds, it->fd);
   }

   
   int selectResult = select(nfds+1, readfds, NULL, exceptfds, ptimeout);  // trying not unblocking for writable

#ifdef DEBUG_PRINT   
   std::cout << "select() returned " << selectResult << std::endl;
   if (FD_ISSET(serverSocketFd, readfds))
   {
      std::cout << "server socket is readable" << std::endl;
   }
   if (includeSerialDevice && FD_ISSET(serialFd, readfds))
   {
      std::cout << "serial is readable" << std::endl;
   }
   for (std::list<IoConnection>::iterator it = connections.begin(); it != connections.end(); it++)
   {
      if (FD_ISSET(it->fd, readfds))
      {
	 std::cout << "descriptor " << it->fd << " is readable" << std::endl;
      }
      if (FD_ISSET(it->fd, writefds))
      {
	 std::cout << "descriptor " << it->fd << " is writable" << std::endl;
      }
      if (FD_ISSET(it->fd, exceptfds))
      {
	 std::cout << "descriptor " << it->fd << " is exceptional" << std::endl;
      }
   }
#endif
   return selectResult;
}


bool IoConnections::waitUsingSelectForAllIo()
{
   pthreadModeData.selectResult = callSelect(true, &readfds, &writefds, &exceptfds);
   return pthreadModeData.selectResult > 0;   
}

bool IoConnections::waitUsingThreadsAndConditionVar()
{
   int pthread_result = -1;
   
   pthread_result = pthread_mutex_lock(&pthreadModeData.mutex);
   CHECK_PTHREAD_RESULT();   

#ifdef DEBUG_PRINT   
   std::cout << "in waitUsingThreadsAndConditionVar()" << std::endl;
#endif   

   if (!pthreadModeData.selectRequestValid)
   {
      pthreadModeData.selectRequestValid = true;
      pthreadModeData.selectResponseValid = false;
      // Select thread might be waiting for this transition, so we must signal now to unblock it
      pthread_result = pthread_cond_broadcast(&pthreadModeData.conditionSomethingChanged);
      CHECK_PTHREAD_RESULT();      
   }

#ifdef DEBUG_PRINT      
   std::cout << "just set selectRequestValid to true, selectResponseValid to false, and now going to sleep until we have select response or serial input" << std::endl;
#endif   
   
   while (! (pthreadModeData.selectResponseValid || pthreadModeData.serialLookahead.size() > 0) )
   {
      pthread_cond_wait(&pthreadModeData.conditionSomethingChanged, &pthreadModeData.mutex);
   }

   if (pthreadModeData.selectResponseValid)
   {
      pthreadModeData.selectResponseAck = true;
      pthreadModeData.selectRequestValid = false;
      pthread_result = pthread_cond_broadcast(&pthreadModeData.conditionSomethingChanged);  // so threads waiting on any of these data transitions can proceed
      CHECK_PTHREAD_RESULT();      
   }

   pthread_result = pthread_mutex_unlock(&pthreadModeData.mutex);
   CHECK_PTHREAD_RESULT();
   
   return true;
}




void IoConnections::queueSerialBytesToClients(uint8_t *bytes, uint32_t numbytes)
{
   std::list<IoConnection>::iterator it = connections.begin();
   while (it != connections.end())
   {
      // First check whether buffer for this connection is very high, indicating that the client end of the socket
      // isn't consuming the data (e.g. it's buggy).  If so, then let's drop this transmission for that client, and maybe
      // output a warning to std err because otherwise we'll keep queueing up socket data that never gets consumed
      // and freed, jeopardizing long term stability of a long-running instance of this program.
      bool shouldWithhold = it->getQueueLength() > QUEUE_STALLED_CLIENT_SUSPICION_THRESHOLD;
      if (shouldWithhold)
      {
#if 0
	 // Disconnecting the client was originally going to be the remedy, but that might be too severe
	 std::cerr << "Socket client doesn't seem to be consuming data we're trying to send; disconnecting from that client!" << std::endl;
	 it->disconnect();
	 it = connections.erase(it);
#endif
	 // Only output message when *newly* withholding
	 if (!it->withholding)
	 {
	    std::cerr << "Socket client doesn't seem to be consuming data fast enough to keep up!  Withholding message." << std::endl;
	 }
	 // just don't enqueue the message to this particular connection
	 it++;
      }
      else
      {
	 std::string serialized((const char*)bytes, numbytes);
	 it->enqueue(serialized);
	 it++;
      }
      it->withholding = shouldWithhold;
   }
}

void IoConnections::queueMessageToClients(NexusDataAcquisitionMessage &msg)
{
   std::list<IoConnection>::iterator it = connections.begin();
   while (it != connections.end())
   {
      // First check whether buffer for this connection is very high, indicating that the client end of the socket
      // isn't consuming the data (e.g. it's buggy).  If so, then let's drop the message for that client, and maybe
      // output a warning to std err because otherwise we'll keep queueing up socket data that never gets consumed
      // and freed, jeopardizing long term stability of a long-running instance of this program.
      bool shouldWithhold = it->getQueueLength() > QUEUE_STALLED_CLIENT_SUSPICION_THRESHOLD;
      if (shouldWithhold)
      {
#if 0
	 // Disconnecting the client was originally going to be the remedy, but that might be too severe
	 std::cerr << "Socket client doesn't seem to be consuming data we're trying to send; disconnecting from that client!" << std::endl;
	 it->disconnect();
	 it = connections.erase(it);
#endif
	 // Only output message when *newly* withholding
	 if (!it->withholding)
	 {
	    std::cerr << "Socket client doesn't seem to be consuming data fast enough to keep up!  Withholding message." << std::endl;
	 }
	 // just don't enqueue the message to this particular connection
	 it++;
      }
      else
      {
	 bool shouldFilter = msg.idtag/4 < 32 && ((it->itcFilterMask & (1 << (msg.idtag/4)))) != 0;
	 if (!shouldFilter)
	 {
	    std::string serialized = msg.serialized();
	    it->enqueue(serialized);
	 }
	 it++;
      }
      it->withholding = shouldWithhold;
   }
}



PthreadModeData::PthreadModeData()
   : exitThreadRequested(false), selectRequestValid(false), selectResponseValid(false), selectResponseAck(false), selectResult(-1) 
{
   // TODO: initialize anything that needs initializing right now
}


// Internal test scaffolding; maybe this isn't needed anymore, but keeping it around, and ifdef'ed out,
// just in case
#ifdef SWT_CPP_TEST

#include <ctime>
#include <cstdlib>

void internal_components_test1()
{
   int SRCBITS=6;

   SwtMessageStreamBuilder sb;

   sb.addDataAcquisitionMessage(SRCBITS, 0x3F, 0, 4, 0x12345678, true, 0x1234567855555555UL);
   for (int i = 0; i < 10000; i++) {
      //sb.addRandomizedSlice();
      sb.addLiteralSlice(0);      
   }
   sb.addDataAcquisitionMessage(SRCBITS, 0x23, 4, 1, 0x7, true, 0x8UL);
   sb.addDataAcquisitionMessage(SRCBITS, 0x23, 5, 1, 0x8, false, 0x8UL);

   sb.addMalformedDataAcquisitionMessageNoTag(SRCBITS, 0x23, 5, 1, 0x77, false, 0);

   SwtByteStream *stream = sb.makeByteStream();
   NexusDataAcquisitionMessage msg;
   NexusStream ns(SRCBITS);
		  
   uint8_t byte;
   while (stream->nextByte(byte))
   {
      if (ns.appendByteAndCheckForMessage(byte, msg))
      {
	 msg.dump();
      }
   }

   sb.freeByteStream(stream);   
}


void internal_components_test2()
{
   int SRCBITS=6;

   SwtMessageStreamBuilder sb;

   for (int i = 0; i < 10000; i++) {
      //sb.addRandomizedSlice();
      // sb.addLiteralSlice(0);      
   }
   sb.addDataAcquisitionMessage(SRCBITS, 0x23, 4, 1, 0x7, true, 0x8UL);
   sb.addDataAcquisitionMessage(SRCBITS, 0x23, 5, 1, 0x8, false, 0x8UL);


   SwtByteStream *stream = sb.makeByteStream();
   NexusDataAcquisitionMessage msg;
   NexusStream ns(SRCBITS);
		  
   uint8_t byte;
   while (stream->nextByte(byte))
   {
      if (ns.appendByteAndCheckForMessage(byte, msg))
      {
	 msg.dump();
      }
   }

   sb.freeByteStream(stream);   
}

#endif
