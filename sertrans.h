// Functions to upload+download files, also used to retrieve dir listings

BOOL SerialConnect(HWND hWnd,HINSTANCE hInst,char* portname,int portspeed,int informonconnectmsg);
int SerialSendFile(HANDLE fh,char* filename,char* block,DWORD resumeat, BOOL sizeonly);
int SerialReceiveFile(HANDLE fh,char* fileName,char* block,DWORD restartat);
int SerialUploadFile(char* filename,char* remotename,DWORD resumeat);
int SerialDownloadFile(char* remotename,char* localname,BOOL alwaysoverwrite,FILETIME *ft,BOOL Resume,BOOL dirlist);
BOOL SerialDeleteFile(char* remotefile);
BOOL SerialCreateDirectory(char* remotedir);
void SerialCloseConnection(void);
BOOL SerialTryConnection(void);
BOOL SerialRenameMoveFile(char* OldName,char* NewName,BOOL move);
BOOL SerialSetAttr(char* remotefile,int attr);
BOOL SerialSetDateTime(char* remotefile,FILETIME *ft);
BOOL SerialSetRestart(DWORD restartat);
BOOL SerialSetOverwriteFlag();
DWORD SerialGetFileSize(char* RemoteName);
BOOL SerialConnected(void);
void SerialSendFlush(void);
int SerialSendChar(char byte);

#define ser_up_ok 0
#define ser_up_exists 1
#define ser_up_readfailed 2
#define ser_up_writefailed 3
#define ser_up_abort 4
