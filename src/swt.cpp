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
#endif
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#ifdef WINDOWS
typedef int socklen_t;
#endif

// Internal debug scaffolding
// #define SWT_CPP_TEST 1
// #define DEBUG_PRINT 1


#define QUEUE_STALLED_CLIENT_SUSPICION_THRESHOLD (512 * 1024)
#define SUSPECT_PROTOCOL_LINE_LENGTH_THRESHOLD 30

// globals (trying to minimize those)

bool useSimulatedSerialData;


// non-forward declarations





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

void buf_dump(uint8_t *buf, int numbits)
{
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

IoConnections::IoConnections(int port, int srcbits, int serialFd, bool dumpNexusMessagesToStdout)
   : ns(srcbits), serialFd(serialFd), numClientsHighWater(0),
     warnedAboutSerialDeviceClosed(false),
     dumpNexusMessagesToStdout(dumpNexusMessagesToStdout)
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
   address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // INADDR_ANY would allow cross host usage of this server, but that's not the intent, and opens up security issues
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

void IoConnections::serviceConnections()
{
   if (waitForIoActivity())
   {
      // check for new connection (see if server socket is readable)
      if (FD_ISSET(serverSocketFd, &readfds))
      {
	 // Accept the data packet from client and verification
	 struct sockaddr_in clientAddr;
	 socklen_t len = sizeof(clientAddr);	 
	 int fd = accept(serverSocketFd, (struct sockaddr *)&clientAddr, &len);
	 if (fd >= 0)
	 {
	    // std::cout << "New connection!" << std::endl;
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
      if (isSerialIoReadable())
      {
	 do
	 {
	    numSerialBytesRead = serialReadBytes(bytes, sizeof(bytes));


	    if (dumpNexusMessagesToStdout)
	    {
	       std::cout << "num bytes read: " << numSerialBytesRead << std::endl;
	       std::cout << "raw bytes: ";
	       buf_dump(bytes, numSerialBytesRead*8);
	    }

	    if (numSerialBytesRead < 0)
	    {
	       if (!warnedAboutSerialDeviceClosed)
	       {
		  std::cerr << "Serial device was disconnected" << std::endl;
		  warnedAboutSerialDeviceClosed = true;
		  close(serialFd);
		  serialFd = -1;
	       }
	    }
	    if (numSerialBytesRead)
	    {
	       if (dumpNexusMessagesToStdout)
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


      // Service all readable clients, in case they have disconnected
      std::list<IoConnection>::iterator it = connections.begin();
      while (it != connections.end())
      {
	 // uint8_t buf[1024];
	 char buf[1024];  // Windows wants this to be char instead of uint_8
	 if (FD_ISSET(it->fd, &readfds))
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
		  uint32_t filterMask;

		  if (isItcFilterCommand(command, filterMask))
		  {
		     it->itcFilterMask = filterMask;
		  }
		  else
		  {
		     // Unknown command -- not really much to do except drop it
		     // Maybe log it or send a message to stderr... TODO?
		  }
		  
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
	       
	       it++;
	    }
	 }
	 else
	 {
	    // not readable, so don't read
	    it++;
	 }
      }
      
      
      // For all connections that we have data to send, and if socket is writable, then try to send all remaning queued bytes but be prepared that socket may only accept some of the bytes
      for (std::list<IoConnection>::iterator it = connections.begin(); it != connections.end(); it++)
      {
	 if (!it->bytesToSend.empty() && FD_ISSET(it->fd, &writefds))
	 {
	    const char *data = it->bytesToSend.data();
	    int numsent = send(it->fd, data, it->bytesToSend.length(), 0);
	    // remove the sent bytes from the queue
	    it->bytesToSend.erase(0, numsent);
	 }
      }
      
   }
   else
   {
      // Nothing to do; we'll try to do more when called again
   }
}

int IoConnections::serialReadBytes(uint8_t *bytes, size_t numbytes)
{
   if (useSimulatedSerialData)
   {
      return simulatedSerialReadBytes(bytes, numbytes);
   }
   else
   {
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
#if defined(LINUX) || defined(OSX)
	return waitUsingSelectForAllIo();  // these platforms support mixing sockets and non-socket descriptors in select()
#else
	return waitUsingThreadsAndConditionVar();
#endif	
}


bool IoConnections::waitUsingSelectForAllIo()
{
   int nfds = 0;
   struct timeval *ptimeout = NULL;  // no timeout, for now (no current reason to use a timeout)
   
   FD_ZERO(&readfds);
   FD_ZERO(&writefds);
   FD_ZERO(&exceptfds);

   // always include server socket in read set; that's how we'll know when a new socket connection is being made
   FD_SET(serverSocketFd, &readfds);
   FD_SET(serverSocketFd, &exceptfds);
   nfds = serverSocketFd;

   // serial port device, which, in our particular case,  is readable only
   if (serialFd != -1)
   {
      FD_SET(serialFd, &readfds);
   }
   
   for (std::list<IoConnection>::iterator it = connections.begin(); it != connections.end(); it++)
   {
      // we'll add the client sockets to the read set, write set, and except set
      FD_SET(it->fd, &readfds);
      FD_SET(it->fd, &writefds);
      FD_SET(it->fd, &exceptfds);            
      nfds = std::max(nfds, it->fd);
   }
   int selectResult = select(nfds+1, &readfds, &writefds, &exceptfds, ptimeout);
   return selectResult > 0;
}

bool IoConnections::waitUsingThreadsAndConditionVar()
{
	return waitUsingSelectForAllIo();  // TODO - RESOLVE!  SWITCH THIS TO USING THREADS AND CONDITION VARIABLE!!
}


bool IoConnections::isSerialIoReadable()
{
   return serialFd != -1 && FD_ISSET(serialFd, &readfds);
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
