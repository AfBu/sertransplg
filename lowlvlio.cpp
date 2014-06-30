#include "stdafx.h"
#include "datatypes.h"
#include "lowlvlio.h"
#include "utils.h"

extern void ShowStatus(char* status);  // must be defined in main app
extern BOOL MessageLoop(void);
extern HWND hWndMain;
BOOL isHandspring=false;

const DWORD myfourbyteidentifier = 'PMTR'; // registered by myself, please register your own on palmos.com!
const DWORD serialcradleidentifier = 'sync';
DWORD fourbyteidentifier;

const int serialcommbuffer=maxblocksizeserial+8+2;	// block plus header
int maxblocksize=maxblocksizeusb;
int g_informonconnectmsg=NULL;           // message to send when connection established
HWND hWndCallback=NULL;

#ifdef watcom
HMODULE Gdi32Library=NULL;
short hSerialPort=0;
#else
HANDLE hSerialPort=INVALID_HANDLE_VALUE;
#endif
HANDLE hUSBPort=INVALID_HANDLE_VALUE;

#ifdef PALMUSBSUPPORT    // the whole USB stuff is only used in the client

HMODULE UsbLibrary=NULL;
BOOL UsbLibraryLoaded=false;
PVOID UsbDeviceInterfacePtr=NULL;

typedef DWORD (*PalmUsbGetAttachedDevicesProc)(DWORD* pdwDeviceContext,char* pBuffer,DWORD* pdwBufferSize);
typedef PVOID (*PalmUsbRegisterDeviceInterfaceProc)(HWND hwnd);
typedef VOID (*PalmUsbUnRegisterDeviceInterfaceProc)(PVOID hDevNotify);
typedef BOOL (*PalmUsbIsPalmOSDeviceNotificationProc)(DWORD dwEventData,DWORD dwFourByteIdentifier,char* fgPortName,PVOID unused);
typedef HANDLE (*PalmUsbOpenPortProc)(char* PortName,DWORD dwFourByteIdentifier);
typedef BOOL (*PalmUsbClosePortProc)(HANDLE hUsbPort);
typedef DWORD (*PalmUsbReceiveBytesProc)(HANDLE hUsbPort,void* pBuffer,DWORD dwBufferSize,DWORD* pdwBytesReceived);
typedef DWORD (*PalmUsbSendBytesProc)(HANDLE hUsbPort,void* pBuffer,DWORD dwBufferSize,DWORD* pdwBytesSent);
typedef DWORD (*PalmUsbSetTimeoutsProc)(HANDLE hUsbPort,DWORD* UsbTimeouts);

static PalmUsbGetAttachedDevicesProc			PalmUsbGetAttachedDevices;
static PalmUsbRegisterDeviceInterfaceProc		PalmUsbRegisterDeviceInterface;
static PalmUsbUnRegisterDeviceInterfaceProc		PalmUsbUnRegisterDeviceInterface;
static PalmUsbIsPalmOSDeviceNotificationProc	PalmUsbIsPalmOSDeviceNotification;
static PalmUsbOpenPortProc						PalmUsbOpenPort;
static PalmUsbClosePortProc						PalmUsbClosePort;
static PalmUsbReceiveBytesProc					PalmUsbReceiveBytes;
static PalmUsbSendBytesProc						PalmUsbSendBytes;
static PalmUsbSetTimeoutsProc					PalmUsbSetTimeouts;
long __stdcall DllCallbackProc(HWND Window,unsigned int Message,WPARAM wParam, LPARAM lParam);

void UsbOpenPort(char* UsbPortName)
{
	if (hUSBPort==INVALID_HANDLE_VALUE)
	{
		hUSBPort=PalmUsbOpenPort(UsbPortName, fourbyteidentifier);
		if (hUSBPort == NULL)
			hUSBPort = INVALID_HANDLE_VALUE;

		if (hUSBPort!=INVALID_HANDLE_VALUE)
		{
			ShowStatus("Connect!");

			if (g_informonconnectmsg)
				PostMessage(hWndMain,g_informonconnectmsg,0,0);  // start transfer!
			
			DWORD usbtimeouts[2];
			// Set this to two seconds each!
			// Don't make it smaller, it can cause read errors!
			usbtimeouts[0]=2000;
			usbtimeouts[1]=2000;
			PalmUsbSetTimeouts(hUSBPort, &usbtimeouts[0]);
		}
	}
}

void UnloadUsbLibrary(void)
{
	if (UsbDeviceInterfacePtr)
	{
		PalmUsbUnRegisterDeviceInterface(UsbDeviceInterfacePtr);
		UsbDeviceInterfacePtr = NULL;
	}
	if (hWndCallback)
	{
		DestroyWindow (hWndCallback);
		hWndCallback = NULL;
	}

	PalmUsbRegisterDeviceInterface = NULL;
	PalmUsbUnRegisterDeviceInterface = NULL;
	PalmUsbIsPalmOSDeviceNotification = NULL;
	PalmUsbOpenPort = NULL;
	PalmUsbClosePort = NULL;
	PalmUsbReceiveBytes = NULL;
	PalmUsbSendBytes = NULL;
	PalmUsbSetTimeouts = NULL;
	
	if (UsbLibrary)
	{
		FreeLibrary (UsbLibrary);
		UsbLibrary = NULL;
	}
	UsbLibraryLoaded=false;
}

#define S_PALMCORE "Software\\U.S. Robotics\\Pilot Desktop\\Core"
#define S_BASEDIR "DesktopPath"

void LoadUsbLibrary(HWND hWnd,HINSTANCE hInst)
{
	HKEY Key;

	if (UsbLibraryLoaded)
		return;

	int olderrmd=SetErrorMode(SEM_NOOPENFILEERRORBOX | SEM_FAILCRITICALERRORS);

	UsbLibrary=LoadLibrary("USBPort.dll");

	// try to load the USB library in the palm desktop path
	if (!UsbLibrary) {
        if (RegOpenKeyEx(HKEY_CURRENT_USER,S_PALMCORE,0,KEY_READ,&Key) == ERROR_SUCCESS) {
			char basedir[MAX_PATH];
			DWORD RegType=REG_SZ;
			DWORD RegSize=sizeof(basedir)-1;
			if (RegQueryValueEx(Key, S_BASEDIR, NULL, &RegType, (unsigned char*)(&basedir), &RegSize)== ERROR_SUCCESS) {
				strcatbackslash(basedir);
				strcat(basedir,"USBPort.dll");

				DWORD handle;
				DWORD dwSize = GetFileVersionInfoSize(basedir, &handle);
				if(dwSize) {
					VS_FIXEDFILEINFO *lpBuffer;
					void *pData=malloc(dwSize);
					GetFileVersionInfo(basedir, handle, dwSize, pData);
					if (VerQueryValue(pData, "\\", (void **)&lpBuffer, (unsigned int *)&dwSize)) {
						DWORD verhigh=lpBuffer->dwFileVersionMS >> 16;
						DWORD verlow=lpBuffer->dwFileVersionMS & 0xFFFF;
						// make sure we have the correct DLL
						if (!(verhigh>4 || (verhigh==4 && verlow>=3))) {
							SetErrorMode(olderrmd);
							MessageBox(hWnd,"USBPort.dll too old, please install a newer version of palm desktop!","Error",MB_ICONEXCLAMATION);
							return;
						}

					}
					free(pData);
				}
				UsbLibrary=LoadLibrary(basedir);
			}
			RegCloseKey(Key);
        }   
	}

	SetErrorMode(olderrmd);
	if (!UsbLibrary)
	{
		MessageBox(hWnd,"USBPort.dll not found (need version 4.3 or newer), please copy to program dir!","Error",MB_ICONEXCLAMATION);
		return;
	}

	PalmUsbGetAttachedDevices=(PalmUsbGetAttachedDevicesProc)GetProcAddress(UsbLibrary,"PalmUsbGetAttachedDevices");
	PalmUsbRegisterDeviceInterface=(PalmUsbRegisterDeviceInterfaceProc)GetProcAddress(UsbLibrary,"PalmUsbRegisterDeviceInterface");
	PalmUsbUnRegisterDeviceInterface=(PalmUsbUnRegisterDeviceInterfaceProc)GetProcAddress(UsbLibrary,"PalmUsbUnRegisterDeviceInterface");
	PalmUsbIsPalmOSDeviceNotification=(PalmUsbIsPalmOSDeviceNotificationProc)GetProcAddress(UsbLibrary,"PalmUsbIsPalmOSDeviceNotification");
	PalmUsbOpenPort=(PalmUsbOpenPortProc)GetProcAddress(UsbLibrary,"PalmUsbOpenPort");
	PalmUsbClosePort=(PalmUsbClosePortProc)GetProcAddress(UsbLibrary,"PalmUsbClosePort");
	PalmUsbReceiveBytes=(PalmUsbReceiveBytesProc)GetProcAddress(UsbLibrary,"PalmUsbReceiveBytes");
	PalmUsbSendBytes=(PalmUsbSendBytesProc)GetProcAddress(UsbLibrary,"PalmUsbSendBytes");
	PalmUsbSetTimeouts=(PalmUsbSetTimeoutsProc)GetProcAddress(UsbLibrary,"PalmUsbSetTimeouts");

	if (!PalmUsbGetAttachedDevices ||
		!PalmUsbRegisterDeviceInterface ||
		!PalmUsbUnRegisterDeviceInterface ||
		!PalmUsbIsPalmOSDeviceNotification ||
		!PalmUsbOpenPort ||
		!PalmUsbClosePort ||
		!PalmUsbReceiveBytes ||
		!PalmUsbSendBytes ||
		!PalmUsbSetTimeouts)
	{
		UnloadUsbLibrary();
		MessageBox(hWnd,"USB DLL does not contain all required functions!","Error",MB_ICONEXCLAMATION);
		return;
	}

	WNDCLASS WndClass;
	memset(&WndClass,0,sizeof(WndClass));
	WndClass.hInstance=hInst;
	WndClass.lpfnWndProc=&(WNDPROC)DllCallbackProc;
	WndClass.lpszClassName="USB_Callback";
	RegisterClass(&WndClass);

	hWndCallback=CreateWindow("USB_Callback","USB_Callback",0,0,0,0,0,
								NULL, NULL,hInst, NULL);
	if (hWndCallback) {
		UsbDeviceInterfacePtr=PalmUsbRegisterDeviceInterface(hWndCallback);
		if (!UsbDeviceInterfacePtr)
		{
			// we can live without it -> delete the now unneeded callback window
			DestroyWindow (hWndCallback);
			hWndCallback = NULL;
		}
	}
	UsbLibraryLoaded=TRUE;
}

BOOL HotSyncRunning()
{
	return FindWindow("KittyHawk","HotSync Manager")!=NULL;
}

BOOL ConnectToUsb(HWND hWnd,HINSTANCE hInst)
{
	char* namebuffer=NULL;
	DWORD err=1;

	if (!UsbLibraryLoaded)
		LoadUsbLibrary(hWnd,hInst);
	if (UsbLibraryLoaded && hUSBPort==INVALID_HANDLE_VALUE)
	{
		ULONG	count = 0;
		ULONG	buffersize = 1024;
		namebuffer = (PTCHAR)malloc(buffersize);

		err = PalmUsbGetAttachedDevices (&count, namebuffer, &buffersize);

		if (err == 8) //Not enough allocated
		{
			free(namebuffer);
			namebuffer = (PTCHAR)malloc(buffersize * sizeof (TCHAR));
			err = PalmUsbGetAttachedDevices (&count, namebuffer, &buffersize);
		}
		if (err == 0 && count > 0) {
			// make sure that Hotsync isn't running!
			if (isHandspring)
				while (HotSyncRunning())
					if (IDCANCEL==MessageBox(hWnd,"Please close the hotsync tool first, then click OK!",
						"Warning",MB_OKCANCEL | MB_ICONEXCLAMATION))
						return false;
			UsbOpenPort(namebuffer);
		} else
			Sleep(200);
	}
	if (namebuffer)
		free(namebuffer);
	return (hUSBPort!=INVALID_HANDLE_VALUE);
}

long __stdcall DllCallbackProc(HWND Window,unsigned int Message,WPARAM wParam, LPARAM lParam)
{
	char UsbPortName[MAX_PATH];
	DWORD event;

	switch (Message) {
	case WM_DEVICECHANGE:
		event=wParam;
		switch (event) {
		case DBT_DEVICEARRIVAL:
			if (PalmUsbIsPalmOSDeviceNotification(lParam,fourbyteidentifier,UsbPortName,NULL)) {
				if (hUSBPort!=INVALID_HANDLE_VALUE)
					ClosePorts(true);
				UsbOpenPort(UsbPortName);
			}
			return TRUE;  // request granted
		case DBT_DEVICEREMOVECOMPLETE:
			if (PalmUsbIsPalmOSDeviceNotification(lParam,fourbyteidentifier,UsbPortName,NULL)) {
				if (hUSBPort!=INVALID_HANDLE_VALUE)
					ClosePorts(true);
			}
			return TRUE;  // request granted
		} // switch (event)
	} // switch (message)
	return DefWindowProc(Window,Message,wParam,lParam);
}
#else
void LoadUsbLibrary(HWND hWnd,HINSTANCE hInst)
{
}

void UnloadUsbLibrary()
{
}

BOOL ConnectToUsb(HWND hWnd,HINSTANCE hInst)
{
	return false;
}
#endif


#ifdef watcom          // Win32s

#pragma pack

typedef struct  _DCB16 {
    char Id;
    WORD BaudRate;
    char ByteSize;
    char Parity;
    char StopBits;
    WORD RlsTimeout;
    WORD CtsTimeout;
    WORD DsrTimeout;

    UINT fBinary :1;
    UINT fRtsDisable :1;
    UINT fParity :1;
    UINT fOutxCtsFlow :1;
    UINT fOutxDsrFlow :1;
    UINT fDummy :2;
    UINT fDtrDisable :1;
    UINT fOutX :1;
    UINT fInX :1;
    UINT fPeChar :1;
    UINT fNull :1;
    UINT fChEvt :1;
    UINT fDtrflow :1;
    UINT fRtsflow :1;
    UINT fDummy2 :1;

    char XonChar;
    char XoffChar;
    WORD XonLim;
    WORD XoffLim;

    char PeChar;
    char EofChar;
    char EvtChar;
    WORD TxDelay;
} DCB16;

typedef struct tagCOMSTAT32 {
  BYTE fCtsHold: 1;
  BYTE fDsrHold: 1;
  BYTE fRlsdHold: 1;
  BYTE fXoffHold: 1;
  BYTE fXoffSent: 1;
  BYTE fEof: 1;
  BYTE fTxim: 1;
  WORD cbInQue;
  WORD cbOutQue;
} COMSTAT32;

typedef int PASCAL (*OpenComm32Proc)(char *ComName,int InQueue, int OutQueue);
typedef short PASCAL (*GetCommState32Proc)(int Cid,DCB16* pdcb);
typedef short PASCAL (*SetCommState32Proc)(DCB16 *pdcb);
typedef short PASCAL (*CloseComm32Proc)(int Cid); 
typedef short PASCAL (*FlushComm32Proc)(int Cid,WORD Queue);
typedef short PASCAL (*WriteComm32Proc)(int Cid,char* Buf,int Size);
typedef short PASCAL (*ReadComm32Proc)(int Cid,char* Buf,int Size);
typedef short PASCAL (*GetCommError32Proc)(int Cid,COMSTAT32* Stat);

static OpenComm32Proc OpenComm32;
static GetCommState32Proc GetCommState32;
static SetCommState32Proc SetCommState32;
static CloseComm32Proc CloseComm32;
static FlushComm32Proc FlushComm32;
static WriteComm32Proc WriteComm32;
static ReadComm32Proc ReadComm32;
static GetCommError32Proc GetCommError32;

BOOL ConnectToSerial(HWND hWnd,char* portname,int baudrate)
{
        DCB16 dcb;
        DCB dcb32;
        char buf[256];

        if (Gdi32Library==NULL) {
                int olderrmd=SetErrorMode(SEM_NOOPENFILEERRORBOX | SEM_FAILCRITICALERRORS);
                Gdi32Library=LoadLibrary("gdi32.dll");
                SetErrorMode(olderrmd);

                if (!Gdi32Library)
                {
                        MessageBox(hWnd,"Could not load gdi32.dll!","Error",MB_ICONEXCLAMATION);
                        return false;
                }

                OpenComm32=(OpenComm32Proc)GetProcAddress(Gdi32Library,"OpenComm32");
                GetCommState32=(GetCommState32Proc)GetProcAddress(Gdi32Library,"GetCommState32");
                SetCommState32=(SetCommState32Proc)GetProcAddress(Gdi32Library,"SetCommState32");
                CloseComm32=(CloseComm32Proc)GetProcAddress(Gdi32Library,"CloseComm32");
                FlushComm32=(FlushComm32Proc)GetProcAddress(Gdi32Library,"FlushComm32");
                WriteComm32=(WriteComm32Proc)GetProcAddress(Gdi32Library,"WriteComm32");
                ReadComm32=(ReadComm32Proc)GetProcAddress(Gdi32Library,"ReadComm32");
                GetCommError32=(GetCommError32Proc)GetProcAddress(Gdi32Library,"GetCommError32");

                if (!OpenComm32 || !GetCommState32 || !SetCommState32 || !CloseComm32 ||
                    !FlushComm32 || !WriteComm32 || !ReadComm32 || !GetCommError32) {

                        MessageBox(hWnd,"Could not find functions in gdi32.dll!","Error",MB_ICONEXCLAMATION);
                        FreeLibrary(Gdi32Library);
                        Gdi32Library=NULL;
                        return false;
                }
        }

        // port already open?
        if (hSerialPort!=0)
                return true;

        // open the port
        hSerialPort=OpenComm32(portname,2000,2000);
        if (hSerialPort==0) {
                MessageBox(hWnd,"Cannot open port!",portname,MB_ICONSTOP);
                return false;
        }
        
        int ok=GetCommState32((int)hSerialPort,&dcb);
        if (ok!=0) {
                itoa(ok,buf,10);
                strcat(buf," GetCommState failed!");
                MessageBox(hWnd,buf,portname,MB_ICONSTOP);
                CloseComm32((int)hSerialPort);
                hSerialPort=0;
                return false;
        }
        if (ok==0) {
                dcb.Id=(int)hSerialPort;
                if (baudrate==115200)
                        dcb.BaudRate=57601;
                else
                        dcb.BaudRate=baudrate;

                dcb.ByteSize=8;
                dcb.Parity=NOPARITY;
                dcb.StopBits=ONESTOPBIT;
                dcb.RlsTimeout=0;
                dcb.CtsTimeout=2000;
                dcb.DsrTimeout=0;
                dcb.fBinary=1;
                dcb.fRtsDisable=0;
                dcb.fParity=0;
                dcb.fOutxCtsFlow=1;
                dcb.fOutxDsrFlow=0;
                dcb.fDummy=0;
                dcb.fDtrDisable=0;
                dcb.fOutX=0;
                dcb.fInX=0;
                dcb.fPeChar=0;
                dcb.fNull=0;
                dcb.fChEvt=0;
                dcb.fDtrflow=0;
                dcb.fRtsflow=1;
                dcb.fDummy2=0;                
                dcb.XonLim=50;
                dcb.XoffLim=50;
                // configure the port
                ok=SetCommState32(&dcb);
        }
        if (ok!=0) {
                CloseComm32((int)hSerialPort);
                hSerialPort=0;
                MessageBox(hWnd,"Invalid port settings!",portname,MB_ICONSTOP);
                return false;
        }
        return true;
}

#else

BOOL ConnectToSerial(HWND hWnd,char* portname,int baudrate)
{
	DCB dcb;

	// port already open?
	if (hSerialPort!=INVALID_HANDLE_VALUE)
		return true;
	
	// open the port
	hSerialPort=CreateFile(portname,GENERIC_READ | GENERIC_WRITE,0,NULL,OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,NULL);
	if (hSerialPort==INVALID_HANDLE_VALUE) {
		MessageBox(hWnd,"Cannot open port!",portname,MB_ICONSTOP);
		return false;
	}
	dcb.DCBlength=sizeof(dcb);
	BOOL ok=GetCommState(hSerialPort,&dcb);
	if (ok) {
		dcb.BaudRate=baudrate;
		dcb.fBinary=1;
		dcb.fParity=0;
		dcb.fOutxCtsFlow=1;
		dcb.fOutxDsrFlow=0;
		dcb.fDtrControl=DTR_CONTROL_ENABLE;
		dcb.fDsrSensitivity=0;
		dcb.fTXContinueOnXoff=0;
		dcb.fOutX=0;
		dcb.fInX=0;
		dcb.fNull=0;
		dcb.fRtsControl=RTS_CONTROL_HANDSHAKE;
		dcb.fAbortOnError=1;
		dcb.ByteSize=8;
		dcb.Parity=NOPARITY;
		dcb.StopBits=ONESTOPBIT;
		// configure the port
		ok=SetCommState(hSerialPort,&dcb);
	}
	if (!ok) {
		int err=GetLastError();
		CloseHandle(hSerialPort);
		hSerialPort=INVALID_HANDLE_VALUE;
		if (err)
			MessageBox(hWnd,"Invalid port settings!",portname,MB_ICONSTOP);
		return false;
	}
	//In summary, set both readIntervalTimeout and readTotalTimeoutMultiplier
	//to 16rFFFFFFFF. Set readTotalTimeoutConstant to some reasonable timeout
	//interval (we've found we like 100 ms)
	COMMTIMEOUTS ctm;
 
	ctm.ReadIntervalTimeout=MAXDWORD;
	ctm.ReadTotalTimeoutMultiplier=0;
	ctm.ReadTotalTimeoutConstant=250;
	ctm.WriteTotalTimeoutMultiplier=0;
	ctm.WriteTotalTimeoutConstant=250; 
	SetCommTimeouts(hSerialPort,&ctm);

	SetupComm(hSerialPort,serialcommbuffer,serialcommbuffer);
	return true;
}
#endif

BOOL ConnectToPort(HWND hWnd,HINSTANCE hInst,char* portname,int portspeed,int informonconnectmsg)
{
	BOOL retval=false;
	ClosePorts(false);
	g_informonconnectmsg=informonconnectmsg;
	if (strncmp(portname,"USB",3)==0) {
		isHandspring=portname[3]=='h';

		if (isHandspring)
			fourbyteidentifier=serialcradleidentifier;
		else
			fourbyteidentifier=myfourbyteidentifier;

		retval=ConnectToUsb(hWnd,hInst);
		if (portname[3]=='l')
			maxblocksize=maxblocksizeserial;
		else
			maxblocksize=maxblocksizeusb;
	} else {
		retval=ConnectToSerial(hWnd,portname,portspeed);
		maxblocksize=maxblocksizeserial;
	}
	return retval;
}

#ifdef watcom
void ClosePorts(BOOL onlyusb)
{
        if (hSerialPort && !onlyusb)
        {
                CloseComm32((int)hSerialPort);
                ShowStatus(" Not connected");
                MessageBeep(MB_ICONSTOP);
                hSerialPort = 0;
        }
}

BOOL Connected(void)
{
	return hSerialPort!=NULL;
}

#else

void ClosePorts(BOOL onlyusb)
{
#ifdef PALMUSBSUPPORT
	if (hUSBPort!=INVALID_HANDLE_VALUE)
	{
		PalmUsbClosePort(hUSBPort);
		ShowStatus("Not connected");
		MessageBeep(MB_ICONSTOP);
		hUSBPort = INVALID_HANDLE_VALUE;
	}
#endif
	if (hSerialPort!=INVALID_HANDLE_VALUE && !onlyusb)
	{
		CloseHandle(hSerialPort);
		ShowStatus("Not connected");
		MessageBeep(MB_ICONSTOP);
		hSerialPort = INVALID_HANDLE_VALUE;
	}
}

BOOL Connected(void)
{
	return hUSBPort!=INVALID_HANDLE_VALUE || hSerialPort!=INVALID_HANDLE_VALUE;
}

#endif

void FreePortResources(void)
{
	UnloadUsbLibrary();
}

#ifdef watcom
void ClearInputBuffer(void)
{
	if (hSerialPort)
		FlushComm32((int)hSerialPort,1); 
}

void ClearOutputBuffer(void)
{
	if (hSerialPort)
		FlushComm32((int)hSerialPort,0); 
}

Err TimedSendBytes(char* sendbuf,Int32 numbytes,Int32 timeout_msec)
{
	char* blockptr;
	int err;
	int sent=0;
	char buf[64];
	DWORD starttime=GetTickCount();

	blockptr=(char*)sendbuf;
	while (numbytes && (timeout_msec<0 || abs(GetTickCount() - starttime) < timeout_msec)) {
		if (hSerialPort!=0) {
			sent=WriteComm32((int)hSerialPort,blockptr,numbytes);
			if (sent<=0) {
				sent=-sent;  // sent was negative!
				err=GetCommError32((int)hSerialPort,NULL);
				if (err) {
	    		    itoa((int)err,buf,10);
		    	    strcat(buf," comm error in send!");
        			ShowStatus(buf);
				}
			} else
				err=0;
			if (err & 0x20 /*CE_CTSTO*/ || (int)sent<numbytes) {
				if (MessageLoop())
					return err_abort;
			} else if (err)
				return err_send;
		} else
			return err_send;
		blockptr+=sent;
		numbytes-=sent;
	}
	if (numbytes)
		return err_send;
	else
		return err_ok;
}

Err TimedReceiveBytes(char* recvbuf,UInt32* numbytes,Int32 timeout_msec)
{
	int err;
	DWORD starttime;
	int bytesreceived=0;
	char buf[64];
	int bytesrequested=0;

	int bytesneeded=(int)*numbytes;

	starttime = GetTickCount();
	while (bytesneeded && (timeout_msec<0 || abs(GetTickCount() - starttime) < timeout_msec)) {
		if (hSerialPort!=0) {
			if (bytesneeded>maxblocksizeserial)
				bytesrequested=maxblocksizeserial;
			else
				bytesrequested=bytesneeded;

			bytesreceived=ReadComm32((int)hSerialPort,recvbuf,bytesrequested);
			if (bytesreceived<bytesrequested) {
				err=GetCommError32((int)hSerialPort,NULL);
				if (err) {
					itoa((int)err,buf,10);
					strcat(buf," comm error in recv!");
					ShowStatus(buf);
				}
			} else
				err=0;

			if (bytesreceived>0) {
				bytesneeded-=bytesreceived;
				recvbuf+=bytesreceived;
			}
			if (bytesreceived<bytesrequested)
				if (MessageLoop())
					return err_abort;
			// Break if there was an error receiving the data.
			if (err!=0) {
				*numbytes-=bytesneeded;
				return err_recv;
			}
		} else {
			*numbytes-=bytesneeded;
			return err_recv;
		}
	}
	*numbytes-=bytesneeded;
	if (bytesneeded)
		return err_recv;
	else
		return 0;
}

#else

void ClearInputBuffer(void)
{
	if (hSerialPort!=INVALID_HANDLE_VALUE)
		PurgeComm(hSerialPort,PURGE_RXCLEAR);
}

void ClearOutputBuffer(void)
{
	if (hSerialPort!=INVALID_HANDLE_VALUE)
		PurgeComm(hSerialPort,PURGE_TXCLEAR);
}

Err TimedSendBytes(char* sendbuf,Int32 numbytes,Int32 timeout_msec)
{
	char* blockptr;
	int err;
	DWORD sent=0;
	DWORD starttime=GetTickCount();

	blockptr=(char*)sendbuf;
	while (numbytes && (timeout_msec<0 || abs((int)(GetTickCount() - starttime)) < timeout_msec)) {
#ifdef PALMUSBSUPPORT
		if (hUSBPort!=INVALID_HANDLE_VALUE) {
			int err = PalmUsbSendBytes (hUSBPort,blockptr,numbytes,&sent);
			if (err==4) {   // Connection lost
				ClosePorts(true);
				break;
			} else if (err==2) {   // Send timeout
				if (MessageLoop())
					return err_abort;
			} else if (err)
				return err_send;
		} else
#endif
		if (hSerialPort!=INVALID_HANDLE_VALUE) {
			if (!WriteFile(hSerialPort,blockptr,numbytes,&sent,NULL)) {
				DWORD Errors;
				err=GetLastError();
				ClearCommError(hSerialPort,&Errors,NULL);
			} else
				err=0;
			if ((int)sent<numbytes) {
				if (MessageLoop())
					return err_abort;
			} else if (err)
				return err_send;
		} else
			return err_send;
		blockptr+=sent;
		numbytes-=sent;
	}
	if (numbytes)
		return err_send;
	else
		return err_ok;
}

Err TimedReceiveBytes(char* recvbuf,UInt32* numbytes,Int32 timeout_msec)
{
	int err;
	DWORD starttime;
	UInt32 bytesreceived=0;
	UInt32 bytesrequested=0;

	Int32 bytesneeded=*numbytes;

	starttime = GetTickCount();
	while (bytesneeded && (timeout_msec<0 || abs((int)(GetTickCount() - starttime)) < timeout_msec)) {
#ifdef PALMUSBSUPPORT
		if (hUSBPort!=INVALID_HANDLE_VALUE) {
			err = PalmUsbReceiveBytes (hUSBPort, recvbuf, bytesneeded, &bytesreceived);
			if (err==4) {   // Connection lost
				ClosePorts(true);
				break;
			} 
			if (bytesreceived>0) {
				bytesneeded-=bytesreceived;
				recvbuf+=bytesreceived;
			}
			if (bytesreceived==0 || err == 3) {    // Recv timed out
				if (MessageLoop())
					return err_abort;
			} else if (err) {                      // Other error? -> Break
				*numbytes-=bytesneeded;
				return err_recv;
			}
		} else
#endif
		if (hSerialPort!=INVALID_HANDLE_VALUE) {
			if (bytesneeded>maxblocksizeserial)
				bytesrequested=maxblocksizeserial;
			else
				bytesrequested=bytesneeded;
			if (!ReadFile(hSerialPort,recvbuf,bytesrequested,&bytesreceived,NULL)) {
				DWORD Errors;
				err=GetLastError();
				ClearCommError(hSerialPort,&Errors,NULL);
			}
			if (bytesreceived>0) {
				bytesneeded-=bytesreceived;
				recvbuf+=bytesreceived;
			}
			if (bytesreceived<bytesrequested)
				if (MessageLoop())
					return err_abort;
		} else {
			*numbytes-=bytesneeded;
			return err_recv;
		}
	}
	*numbytes-=bytesneeded;
	if (bytesneeded)
		return err_recv;
	else
		return 0;
}

#endif

int TimedSendChar(char byte,int timeout_msec)
{
	long len=1;
	return TimedSendBytes(&byte,1,timeout_msec);
}

int TimedReceiveChar(char* ch,Int32 timeout_msec)
{
   UInt32 receive=1;
   return TimedReceiveBytes(ch,&receive,timeout_msec);
}

