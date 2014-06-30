// Error Codes

#define err_ok 0
#define err_abort 1
#define err_recv 2
#define err_send 3
#define err_mem 4
#define err_file 5

#define maxblocksizeusb 63*1024		// as large as possible
#define maxblocksizeserial 1*1024	// XModem-1k block size

// Function prototypes

BOOL ConnectToPort(HWND hWnd,HINSTANCE hInst,char* portname,int portspeed,int informonconnectmsg);
void ClosePorts(BOOL onlyusb);
void FreePortResources();
void ClearInputBuffer(void);
void ClearOutputBuffer(void);
BOOL Connected(void);
Err TimedSendBytes(char* sendbuf,Int32 numbytes,Int32 timeout_msec);
int TimedSendChar(char byte,int timeout_msec);
Err TimedReceiveBytes(char* recvbuf,UInt32* numbytes,Int32 timeout_msec);
int TimedReceiveChar(char* ch,Int32 timeout_msec);

