// Block message format:
// 3 byte marker = 'FTH'
// 1 byte command
// 1 byte block length high byte
// 1 byte block length low byte
// 1 byte header-crc high byte
// 1 byte header-crc low byte
// x byte user-data (depends on block type)
// rest: not yet defined

// replies with RQ_OK, RQ_TRANSFERERR or RQ_FAILED (=file exists)

#define RQ_OK  27          // binary 0011011      // block OK
#define RQ_FAILED 73       // binary 1001001      // block OK, operation failed
#define RQ_TRANSFERERR 85  // binary 1010101      // block transfer error
#define RQ_READY 119       // binary 1110111      // signal "transfer ready"
#define RQ_CRCERR 99       // binary 1100011      // block has bad CRC
#define RQ_WAIT 60         // binary 0111100      // tell the client to wait an extended time - for operations taking more than 5 seconds
#define RQ_LOCALABORT 0    // binary 0000000      // user aborted operation - never sent across line! Use RQ_FAILED instead
#define RQ_LOCALTIMEOUT 1  // binary 0000001      // Send or receive call timed out -> retry after 5 seconds delay
#define RQ_F 'F'           // binary 1000110      // Start of a new block (other direction) -> abort operation!

// commands are uppercase, data blocks lowercase
#define CMD_ABORT   '1'
#define CMD_RECVERR '2'
#define CMD_CRCERR  '3'
#define CMD_TIMEOUT '4'
#define CMD_LIST    'L'
#define CMD_GET     'G'
#define CMD_SEND    'S'
#define CMD_DELETE  'D'
#define CMD_RENFR   'F'
#define CMD_RENTO   'T'
#define CMD_COPYTO  'O'
#define CMD_MKDIR   'M'
#define CMD_CONNECT 'C'
#define CMD_SETATTR 'A'
#define CMD_SETTIME 'I'
#define CMD_RESTART 'R'
#define CMD_GETSIZE 'Q'
#define CMD_OVERWRITE 'W'

#define CMD_SIZE    's'
#define CMD_BODY    'b'
#define CMD_EOF     'e'

#define maxblockpart 8192

int SendNextBlock(char request,char* databuf,UInt32 datalen,UInt8 sequencenr);
char ReceiveNextBlock(char* recvbody,UInt32 maxbodysize,UInt32* datareceived,UInt8* sequencenr);
int SendNextBlockRetry10(char request,char* databuf,int datalen,UInt8 sequencenr);

