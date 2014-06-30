#include "stdafx.h"
#include "datatypes.h"
#include "lowlvlio.h"
#include "blockio.h"
#include "sertrans.h"
#include "utils.h"

extern void ShowStatus(char* status);  // must be defined in main app
extern void UpdatePercentBar(int offset,int filesize);
extern BOOL MessageLoop();
extern BOOL UserAbort();
extern HWND hWndMain;
extern int maxblocksize;

BOOL IsCommand(int cmd)
{
	return(cmd>='A' && cmd<='Z');  // commands are uppercase characters
}

int SerialReceiveFile(HANDLE fh,char* fileName,char* block,DWORD restartat)
{
	int err;      
	UInt32 fileSize,fileRead;
	UInt32 datareceived;
	UInt32 blockoffset;
	UInt8 sequencenr;
	UInt8 expectedsequencenr;
	FILETIME fileDate;
	int retries;
	char msgbuf[MAX_PATH];

	err=TimedSendChar(RQ_READY,5000);
	if (err)
		return err_send;

	fileSize=0;
	blockoffset=0;
	fileRead=restartat;
	sequencenr=0;
	fileDate.dwHighDateTime=0;
	fileDate.dwLowDateTime=0;
	expectedsequencenr=0;

	strcpy(msgbuf,"File receive: ");
	strncat(msgbuf,fileName,sizeof(msgbuf)-1);
	ShowStatus(msgbuf);

	while (true) {
		// Receive a whole block
		retries=0;
		do {
			err=ReceiveNextBlock(block+blockoffset,maxblocksizeusb-blockoffset,&datareceived,&sequencenr);
			if (IsCommand(err)) {    // upload was aborted, this is already a new command!
				TimedSendChar(RQ_CRCERR,5000); // requests re-send
				return CMD_CRCERR;   // wrong block received!
			}
			if (err==CMD_BODY && expectedsequencenr!=sequencenr)
				err=CMD_CRCERR;	// wrong block received!
			if (err==CMD_CRCERR) {
				ShowStatus("CRC-Error");
				ClearInputBuffer();
				TimedSendChar(RQ_CRCERR,5000);
			}
			if (err==CMD_TIMEOUT) {
				ShowStatus ("Timeout!");
				Sleep(1000);
				ClearInputBuffer();
				TimedSendChar(RQ_CRCERR,500);
				Sleep(2000);
				if (MessageLoop())  // hard abort
					return err_abort;
			}
		} while ((err==CMD_CRCERR || err==CMD_TIMEOUT) && retries++<10);
		if (err==CMD_ABORT || err==CMD_RECVERR || err==CMD_CRCERR || err==CMD_TIMEOUT)
			return err;

		if (err==CMD_BODY) {
			if (expectedsequencenr==255)
				expectedsequencenr=0;
			else
				expectedsequencenr++;
		}

		if (UserAbort()) {   // soft abort
			err = TimedSendChar(RQ_FAILED,5000);
			return err_abort;
		}

		if (err==CMD_EOF) {
			if (blockoffset>0) {
				UInt32 written;
				if (!WriteFile(fh,block,blockoffset,&written,NULL))
					return err_file;
				fileRead += blockoffset;
				UpdatePercentBar(fileRead,fileSize);
			}
			if (fileDate.dwHighDateTime || fileDate.dwLowDateTime)
				SetFileTime(fh,NULL,NULL,&fileDate);
			TimedSendChar(RQ_OK,5000);
			break;
		}
		if (err==CMD_SIZE) {	// size block
			char* pdate;
			pdate=strchr(block,'\t');
			if (pdate) {
				pdate[0]=0;
				ConvertIsoDateToDateTime(++pdate,&fileDate);
			}
			fileSize = atoi (block);
			// Send acknowledge for block
			err = TimedSendChar(RQ_OK,5000);
		} else if (err==CMD_BODY) {	// body
			UInt32 writesize;
			// Send acknowledge for block
			err = TimedSendChar(RQ_OK,5000);
			// Write to file if there is any data to write
			writesize=datareceived;
			if (fh!=INVALID_HANDLE_VALUE) {
				// If less than 16k received, just increase block offset
				// but only if blockoffset+2*DataReceived<maxblocksize
				if (writesize<16384 && blockoffset+2*writesize<maxblocksizeusb) {
					blockoffset+=writesize;
				} else {
					UInt32 written;
					if (!WriteFile(fh,block,blockoffset+writesize,&written,NULL))
						return err_file;
					blockoffset=0;
				}
				fileRead += writesize;
				UpdatePercentBar(fileRead,fileSize);
			} else
				UpdatePercentBar(0,100);
		}
		if (MessageLoop())  // hard abort
			return err_abort;
	}
	return 0;
}

int SerialSendFile(HANDLE fh,char* filename,char* block,DWORD resumeat, BOOL sizeonly)
{
	UInt32 filesize;
	UInt8 sequencenr;
	FILETIME ft;
	int err;
	char msgbuf[MAX_PATH];
	char bytereceived;
	HANDLE searchhandle;
	WIN32_FIND_DATA srec;

	strcpy(msgbuf,"File send: ");
	strncat(msgbuf,filename,sizeof(msgbuf)-1);
	ShowStatus(msgbuf);

	if (fh!=INVALID_HANDLE_VALUE) {
		filesize=GetFileSize(fh,NULL);
		searchhandle=0;
	} else {
		char dirbuf[MAX_PATH];
		filesize=0xFFFFFFFF;
		strlcpy(dirbuf,filename+1,sizeof(dirbuf)-2);
		strcatbackslash(dirbuf);
		strncat(dirbuf,"*.*",sizeof(dirbuf)-1);
		searchhandle=FindFirstFile(dirbuf,&srec);
	}

	sequencenr=0;

	// Wait for RQ_READY
	err=TimedReceiveChar(&bytereceived,5000);

	if (err) {	// timeout!
		ShowStatus("Initial byte timeout!");
		if (searchhandle!=INVALID_HANDLE_VALUE)
			FindClose(searchhandle);
		return err;
	}
	if (bytereceived!=RQ_READY) {
		ShowStatus("Initial byte receive error!");
		if (searchhandle!=INVALID_HANDLE_VALUE)
			FindClose(searchhandle);
		return err_recv;
	}	
	// send header block
	itoa(filesize,block,10);
	// new: send also date, separated by a Tab
	if (GetFileTime(fh,NULL,NULL,&ft)) {
		char *p=block+strlen(block);
		strcpy(p,"\t");
		if (!CreateIsoDateString(&ft,p+1)) //yyyymmddhhmmss
			p[0]=0;   // don't send tab in case of error
	}
	err=SendNextBlockRetry10(CMD_SIZE,block,strlen(block)+1,0);
	if (err!=RQ_OK) {
		ShowStatus("Size block not acknowledged!");
		if (searchhandle!=INVALID_HANDLE_VALUE)
			FindClose(searchhandle);
		if (err==RQ_LOCALABORT)
			return err_abort;
		else
			return err_recv;
	}

	if (sizeonly) {
		if (searchhandle!=INVALID_HANDLE_VALUE)
			FindClose(searchhandle);
		return 0;
	}
	// send body
	DWORD offset = resumeat;

	while (offset < filesize)
	{
		if (MessageLoop()) {
			if (searchhandle!=INVALID_HANDLE_VALUE)
				FindClose(searchhandle);
			return err_abort;
		}
		int blocksize=filesize-offset;
		if (blocksize>maxblocksize)
			blocksize=maxblocksize;

		if (filename[0]=='\\' && filename[1]==0) {  // drives!
			block[0]=0;
			char* pNextName=block;
			char drvbuf[8];
			char ch;

			for (ch='A';ch<='Z';ch++) {
				strcpy(drvbuf,"a:\\");
				drvbuf[0]=ch;
				int tp=GetDriveType(drvbuf);
				if (tp!=DRIVE_UNKNOWN && tp!=DRIVE_NO_ROOT_DIR) {
					strcpy(pNextName,drvbuf);
					strcat(pNextName,"\r\n");
					pNextName+=strlen(pNextName);
				}
			}
			blocksize=pNextName-(char*)block;   // without #0 at end!
			offset=filesize;
		} else if (fh==INVALID_HANDLE_VALUE) {  // directory!
			block[0]=0;
			blocksize=0;
			char* pEndBody;
			char* pNextName=block;
			blocksize=1024;	//maxblocksize;
			pEndBody=block+blocksize-262;	// if more files, continue in next block
			while (searchhandle!=INVALID_HANDLE_VALUE && pNextName<pEndBody) {
				if (strcmp(srec.cFileName,".")!=0 && strcmp(srec.cFileName,"..")!=0) {
					pNextName=strcpy(pNextName,srec.cFileName);
					pNextName+=strlen(pNextName);
					if (srec.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
						strcpy(pNextName,"\\");
						pNextName++;
						strcat(pNextName,"\t0");
					} else {
						pNextName[0]='\t';
#ifdef watcom
						_itoa(srec.nFileSizeLow,pNextName+1,10);
#else
						_i64toa((INT64)(srec.nFileSizeHigh<<32)+srec.nFileSizeLow,pNextName+1,10);
#endif
					}
					pNextName+=strlen(pNextName);
					pNextName[0]='\t';
					CreateIsoDateString(&srec.ftLastWriteTime,pNextName+1);
					pNextName+=strlen(pNextName);
					pNextName[0]='\t';
					itoa(srec.dwFileAttributes,pNextName+1,10);
					pNextName+=strlen(pNextName);
					strcpy(pNextName,"\r\n");
					pNextName+=2;
				}
				if (!FindNextFile(searchhandle,&srec)) {
					FindClose(searchhandle);
					searchhandle=INVALID_HANDLE_VALUE;
				}
				blocksize=pNextName-(char*)block; // without #0 at end!
			}
			if (searchhandle==INVALID_HANDLE_VALUE )
				offset=filesize;
		} else if (blocksize>0) {
			DWORD numBytesRead;
			if (!ReadFile(fh,block,blocksize,&numBytesRead,NULL)) {
				ShowStatus("ReadFile failed!");
				return err_recv;
			}
		}
		err=SendNextBlockRetry10(CMD_BODY,block,blocksize,sequencenr);

		if (err!=RQ_OK) {
			ShowStatus("Body block not acknowledged!");
			if (searchhandle!=INVALID_HANDLE_VALUE)
				FindClose(searchhandle);
			if (err==RQ_LOCALABORT)
				return err_abort;
			else
				return err_recv;
		}
		if (sequencenr==255)
			sequencenr=0;
		else
			sequencenr++;

		if (filesize!=0xFFFFFFFF) {
			offset += blocksize;
			UpdatePercentBar(offset,filesize);
		}

		if (UserAbort()) {
			if (searchhandle!=INVALID_HANDLE_VALUE)
				FindClose(searchhandle);
			return err_abort;
		}
	}
	if (searchhandle!=INVALID_HANDLE_VALUE)
		FindClose(searchhandle);

	// Send end of file block
	err=SendNextBlockRetry10(CMD_EOF,NULL,0,0);
	if (err!=RQ_OK) {	// timeout!
		ShowStatus("End of file block not acknowledged!");
		if (err==RQ_LOCALABORT)
			return err_abort;
		else
			return err_recv;
	}
	return err_ok;
}

void ShowTransferComplete(BOOL transferok,char* operation,char* name,DWORD bytes,DWORD msec)
{
	char buf[MAX_PATH];
	char* p=buf;
	DWORD speed;

	if (!transferok) {
		buf[0]='!';
		p++;
	}
	strcpy(p,operation);
	strcat(buf," ");
	p=buf+strlen(buf);
	itoa(bytes,p,10);	
	strcat(buf," bytes, ");
	if (msec/1000) {
		speed=bytes/(msec/1000);
	} else if (msec) {
		speed=bytes*1000/msec;
	}
	p=buf+strlen(buf);
	if (speed>1024) {
		itoa(speed/1024,p,10);
		if (speed/102400==0) {
			strcat(buf,".");
			p=buf+strlen(buf);
			itoa((10*speed/1024) % 10,p,10);
		}
		strcat(buf," k/s, ");
	} else {
		itoa(speed,p,10);
		strcat(buf," b/s, ");
	}
	strncat(buf,name,sizeof(buf)-1);
	ShowStatus(buf);
}

// ser_up_ok=0
// ser_up_exists=1
// ser_up_readfailed=2
// ser_up_writefailed=3
// ser_up_abort 4

#define dmDBNameLength 32
#define dmHdrAttrResDB 1 << 8  // network byte order!!!

typedef struct { 
	char	name[dmDBNameLength]; 
	UInt16	attributes; 
	UInt32	creationDate; 
	UInt32	modificationDate; 
	UInt32	lastBackupDate; 
	UInt32	modificationNumber; 
	LocalID	appInfoID; 
	LocalID	sortInfoID; 
	UInt32	type; 
	UInt32	creator; 
	UInt32	uniqueIDSeed; 
} DatabaseHdrType;

DWORD swapbyteorder32(UInt32 data)
{
	return	(data & 0xFF)<<24 |
			(data & 0xFF00)<<8 |
			(data & 0xFF0000)>>8 |
			(data & 0xFF000000)>>24;
}

int SerialUploadFile(char* filename,char* remotename,DWORD resumeat)
{
	char localname[MAX_PATH];
	DatabaseHdrType dbheader;
	char* block;
	HANDLE fh;
	DWORD bytesread,filesize;
	int retval;

	strcpy(localname,filename);

	block=(char*)malloc(maxblocksizeusb);
	fh=CreateFile(localname,GENERIC_READ,FILE_SHARE_READ,
				NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	if (fh!=INVALID_HANDLE_VALUE && block) {

		// a palm database? If yes, send its real name!
		if (strncmp(remotename,"\\0:\\",4)==0 && strchr(remotename+4,'\\')==NULL) {
			memset(&dbheader,0,sizeof(dbheader));
			filesize=GetFileSize(fh,NULL);
			ReadFile(fh,&dbheader,sizeof(dbheader),&bytesread,NULL);
			dbheader.name[dmDBNameLength-1]=0;
			if (bytesread>0 && dbheader.name[0] &&
				swapbyteorder32(dbheader.sortInfoID)<filesize &&
				swapbyteorder32(dbheader.appInfoID)<filesize) {
				strcpy(remotename+4,dbheader.name);  // changes name to caller!
				if (dbheader.attributes & dmHdrAttrResDB)
					strncat(remotename,".prc",MAX_PATH-1);
				else
					strncat(remotename,".pdb",MAX_PATH-1);
			} else {	// the header is invalid!
				CloseHandle(fh);
				return ser_up_writefailed;
			}
			if (resumeat==0)
				SetFilePointer(fh,0,NULL,FILE_BEGIN);
		}
		if (resumeat!=0) {
			if (SetFilePointer(fh,resumeat,NULL,FILE_BEGIN)==-1) {
				CloseHandle(fh);
				return ser_up_readfailed;
			}
			if (!SerialSetRestart(resumeat)) {
				CloseHandle(fh);
				return ser_up_writefailed;
			}
		}
		
		ClearInputBuffer();
		int err=SendNextBlockRetry10(CMD_SEND,remotename,strlen(remotename)+1,0);
		if (err==RQ_FAILED)
			retval=ser_up_exists;
		else if (err==RQ_OK) {
			DWORD starttime=GetTickCount();
			if (err=SerialSendFile(fh,remotename,block,resumeat,false)==err_ok)
				retval=ser_up_ok;
			else if (err==err_abort)
				retval=ser_up_abort;
			else
				retval=ser_up_writefailed;
			ShowTransferComplete(retval==ser_up_ok,"PUT",remotename,
				SetFilePointer(fh,0,NULL,FILE_CURRENT)-resumeat,
				abs((int)(GetCurrentTime()-starttime)));
		} else if (err=RQ_LOCALABORT)
			retval=ser_up_abort;
		else
			retval=ser_up_writefailed;
	} else
		retval=ser_up_readfailed;
	if (block)
		free(block);
	if (fh)
		CloseHandle(fh);
	return retval;
}

int SerialDownloadFile(char* remotename,char* localname,BOOL alwaysoverwrite,FILETIME *ft,BOOL Resume,BOOL dirlist)
{
	HANDLE fh;
	char* block;
	int retval,err;

	if (!alwaysoverwrite && !Resume) {
		fh=CreateFile(localname,GENERIC_READ,FILE_SHARE_READ,
					NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
		if (fh!=INVALID_HANDLE_VALUE) {
			CloseHandle(fh);
			return ser_up_exists;
		}
	}
	block=(char*)malloc(maxblocksizeusb);
	fh=CreateFile(localname,GENERIC_WRITE,FILE_SHARE_READ,
		NULL,Resume ? OPEN_EXISTING : CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
	if (block && fh!=INVALID_HANDLE_VALUE) {
		ClearInputBuffer();

		UInt32 restartat=0;

		if (Resume) {
			restartat=SetFilePointer(fh,0,NULL,FILE_END);
			if (restartat>0)
				if (!SerialSetRestart(restartat)) {
					CloseHandle(fh);
					return ser_up_readfailed;
				}
		}

		if (RQ_OK==SendNextBlockRetry10(dirlist ? CMD_LIST : CMD_GET,remotename,strlen(remotename)+1,0)) {
			DWORD starttime=GetTickCount();
			if (err=SerialReceiveFile(fh,remotename,block,restartat)==0) {
				if (ft)
					SetFileTime(fh,NULL,NULL,ft);
				retval=ser_up_ok;
			} else {
				if (err==err_abort)
					retval=ser_up_readfailed;
				else
					retval=ser_up_abort;
			}
			ShowTransferComplete(retval==ser_up_ok,"GET",remotename,
				SetFilePointer(fh,0,NULL,FILE_CURRENT)-restartat,
				abs((int)(GetCurrentTime()-starttime)));
		} else
			retval=ser_up_readfailed;
	} else
		retval=ser_up_writefailed;
	if (fh!=INVALID_HANDLE_VALUE)
		CloseHandle(fh);
	if (block)
		free(block);
	return retval;
}

BOOL SerialDeleteFile(char* remotefile)
{
	return RQ_OK==SendNextBlockRetry10(CMD_DELETE,remotefile,strlen(remotefile)+1,0);
}

BOOL SerialCreateDirectory(char* remotedir)
{
	return RQ_OK==SendNextBlockRetry10(CMD_MKDIR,remotedir,strlen(remotedir)+1,0);
}

BOOL SerialConnect(HWND hWnd,HINSTANCE hInst,char* portname,int portspeed,int informonconnectmsg)
{
	return ConnectToPort(hWnd,hInst,portname,portspeed,informonconnectmsg);
}

void SerialCloseConnection(void)
{
	ClosePorts(false);
	FreePortResources();
}

BOOL SerialTryConnection(void)
{
	return RQ_OK==SendNextBlock(CMD_CONNECT," ",0,0); // No retry!
}

BOOL SerialRenameMoveFile(char* OldName,char* NewName,BOOL move)
{
	if (RQ_OK==SendNextBlockRetry10(CMD_RENFR,OldName,strlen(OldName)+1,0)) {
		if (move)
			return RQ_OK==SendNextBlockRetry10(CMD_RENTO,NewName,strlen(NewName)+1,0);
		else
			return RQ_OK==SendNextBlockRetry10(CMD_COPYTO,NewName,strlen(NewName)+1,0);
	}
	return false;
}

BOOL SerialSetAttr(char* remotefile,int attr)
{
	char remotename[MAX_PATH+8];
	strlcpy(remotename,remotefile,sizeof(remotename)-9);
	strncat(remotename,"\t",sizeof(remotename)-8);
	itoa(attr,remotename+strlen(remotename),10);
	return RQ_OK==SendNextBlockRetry10(CMD_SETATTR,remotename,strlen(remotename)+1,0);
}

BOOL SerialSetDateTime(char* remotefile,FILETIME *ft)
{
	char remotename[MAX_PATH+20];
	strlcpy(remotename,remotefile,sizeof(remotename)-20);
	strncat(remotename,"\t",sizeof(remotename)-20);

	char *p=remotename+strlen(remotename);
	if (ft && CreateIsoDateString(ft,p)) //yyyymmddhhmmss
		return RQ_OK==SendNextBlockRetry10(CMD_SETTIME,remotename,strlen(remotename)+1,0);
	else
		return false;
}

BOOL SerialSetRestart(DWORD restartat)
{
	char buffer[32];
	itoa(restartat,buffer,10);
	return RQ_OK==SendNextBlockRetry10(CMD_RESTART,buffer,strlen(buffer)+1,0);
}

BOOL SerialSetOverwriteFlag()
{
	return RQ_OK==SendNextBlockRetry10(CMD_OVERWRITE,NULL,0,0);
}

DWORD SerialGetFileSize(char* RemoteName)
{
	DWORD fileSize;
	UInt32 datareceived;
	char block[32];
	int err,retries;
	UInt8 sequencenr;

	if (RQ_OK==SendNextBlockRetry10(CMD_GETSIZE,RemoteName,strlen(RemoteName)+1,0)) {
		err=TimedSendChar(RQ_READY,5000);
		if (err)
			return -1;

		fileSize=-1;
		while (true) {
			// Receive a whole block
			retries=0;
			do {
				err=ReceiveNextBlock(block,sizeof(block),&datareceived,&sequencenr);
				if (err==CMD_CRCERR) {
					ShowStatus("CRC-Error");
					ClearInputBuffer();
					TimedSendChar(RQ_CRCERR,5000);
				}
				if (err==CMD_TIMEOUT) {
					ShowStatus ("Timeout!");
					Sleep(1000);
					ClearInputBuffer();
					TimedSendChar(RQ_CRCERR,500);
					Sleep(2000);
					if (MessageLoop())  // hard abort
						return -1;
				}
			} while ((err==CMD_CRCERR || err==CMD_TIMEOUT) && retries++<10);

			if (err==CMD_ABORT || err==CMD_RECVERR || err==CMD_CRCERR || err==CMD_TIMEOUT)
				return -1;

			if (UserAbort()) {   // soft abort
				err = TimedSendChar(RQ_FAILED,5000);
				return -1;
			}

			if (err==CMD_SIZE) {	// size block
				fileSize = atoi (block);
				// Send acknowledge for block
				err = TimedSendChar(RQ_OK,5000);
				break;
			}
			if (MessageLoop())  // hard abort
				return -1;
		}
	}
	return fileSize;
}

BOOL SerialConnected(void)
{
	return Connected();
}

void SerialSendFlush(void)
{
	ClearOutputBuffer();
}

int SerialSendChar(char byte)
{
	return TimedSendChar(byte,5000);
}
