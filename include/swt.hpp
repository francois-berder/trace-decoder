#include <vector>
#include <list>
#include <string>
#include <stdint.h>
#include <netinet/in.h>

class SwtByteStream
{
public:
      virtual ~SwtByteStream() {}
      virtual bool nextByte(uint8_t & ch) = 0;  
};


class SwtDummyByteStream : public SwtByteStream
{
public:
   virtual bool nextByte(uint8_t & ch);
};


class SwtTestMessageStream : public SwtByteStream
{
   std::vector<uint8_t> vec;
   std::vector<uint8_t>::iterator it;
public:
   SwtTestMessageStream(std::vector<uint8_t> vec);
   bool nextByte(uint8_t & ch);
};


class NexusSliceAcceptor
{
public:
   virtual void startMessage(int tcode) = 0;
   virtual void messageData(int numbits, uint8_t *data, bool overflowed) = 0;
   virtual void endField() = 0;
   virtual void endMessage() = 0;
};


class NexusSliceUnwrapper
{
public:
   NexusSliceUnwrapper(NexusSliceAcceptor &acceptor);
   void emptyData();
   void appendByte(uint8_t byte);
private:
   NexusSliceAcceptor& acceptor;
   bool inMessage;   
   enum { MAX_DATA = 4096 };
   uint8_t data[MAX_DATA];
   uint32_t datanumbits;
   bool dataoverflowed;
};


class TestAcceptor : public NexusSliceAcceptor
{
   void startMessage(int tcode);
   void messageData(int numbits, uint8_t *data, bool overflowed);
   void endField();
   void endMessage();
};    

struct NexusDataAcquisitionMessage {
   // optional message fields
   bool haveTimestamp;
   uint64_t timestamp;
   bool haveSrc;
   uint32_t src;

   // always-present message fields
   uint32_t tcode;
   uint32_t idtag;
   uint32_t dqdata;

   NexusDataAcquisitionMessage();
   void clear();
   std::string serialized();
   void dump();
};



class NexusMessageReassembler : public NexusSliceAcceptor
{
public:
   NexusMessageReassembler(int srcbits);
   void startMessage(int tcode);
   void messageData(int numbits, uint8_t *data, bool overflowed);
   void endField();
   void endMessage();
   bool getMessage(NexusDataAcquisitionMessage &msg);
private:
   int srcbits;
   bool acceptingMessage;
   int fieldcount;
   bool messageHasOverflowedField;
   bool messageReady;
   NexusDataAcquisitionMessage dqm;
};    



class NexusStream
{
public:
   NexusStream(int srcbits);
   bool appendByteAndCheckForMessage(uint8_t byte, NexusDataAcquisitionMessage &msg);
private:
   NexusMessageReassembler reassembler;
   NexusSliceUnwrapper unwrapper;
};



class SwtMessageStreamBuilder
{
   // For test purposes.  Enable building a stream of bytes that have well formed and/or malformed messages.
public:

   SwtByteStream *makeByteStream();
   void freeByteStream(SwtByteStream *stream);
   void addDataAcquisitionMessage(int srcbits, int src, int itcIdx, int size, uint32_t data, bool haveTimestamp, uint64_t timestamp);
   void addMalformedDataAcquisitionMessageNoTag(int srcbits, int src, int itcIdx, int size, uint32_t data, bool haveTimestamp, uint64_t timestamp);
   void addMalformedDataAcquisitionMessageNoData(int srcbits, int itcIdx, int size, uint32_t data, bool haveTimestamp, uint64_t timestamp);
   void addMalformedDataAcquisitionMessageNoBody(int srcbits, int itcIdx, int size, uint32_t data, bool haveTimestamp, uint64_t timestamp);
   void addNonDataAcquisitionMessage();
   void addRandomizedSlice();
   void addLiteralSlice(uint8_t slice);
   void dump();
private:
   uint8_t curslice;
   int cursliceOffset;
   std::vector<uint8_t> slices;

   void beginMessage();
   void append(uint64_t val, int numbits);
   void appendFixedField(uint64_t val, int numbits, bool endOfMessage);
   void appendVarField(uint64_t val, int numbits, bool endOfMessage);
   void flushSlice();
   int minBits(uint64_t val, int numbits);
};



struct IoConnection
{
   int fd;  // type probably shouldn't be int
   std::string bytesToSend;
   std::string bytesReceived;
   struct sockaddr_in clientAddr;
   bool withholding;  // are we avoiding queueing additional messages because queue got too long?
   uint32_t itcFilterMask;

   IoConnection(int fd);
   ~IoConnection();
   void disconnect();
   void enqueue(const std::string &str);
   int getQueueLength();
};


class IoConnections
{
   // manage all external IOs
public:
   IoConnections(int port, int srcbits, int serialFd);
   int serialReadBytes(uint8_t *bytes, size_t numbytes);
   bool waitForIoActivity();
   bool isSerialIoReadable();
   bool hasClientCountDecreasedToZero();
   void queueMessageToClients(NexusDataAcquisitionMessage &msg);
   void serviceConnections();
private:
   enum {SERIAL_BUFFER_NUMBYTES=1024};
   NexusStream ns;   
   int serverSocketFd;
   int serialFd;
   std::list<IoConnection> connections;
   std::list<IoConnection>::size_type numClientsHighWater;   
   fd_set readfds;
   fd_set writefds;
   fd_set exceptfds;
   bool warnedAboutSerialDeviceClosed;



   
   bool doWaitForIoActivity();

   // temp scaffolding before we have serial cable... dummy data
   SwtMessageStreamBuilder sb;
   SwtByteStream *simulatedSerialStream;
   int simulatedSerialReadBytes(uint8_t *bytes, size_t numbytes);   
   void makeSimulatedSerialPortStream();
   bool isItcFilterCommand(const std::string& str, uint32_t& filterMask);
};
