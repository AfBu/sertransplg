// sertransplg.cpp : Defines the entry point for the DLL application.
//

#include "stdafx.h"
#include "fsplugin.h"
#include "sertrans.h"
#include "resource.h"
#include "utils.h"

#define numportnames 11
char* portnames[numportnames]={"COM1:","COM2:","COM3:","COM4:","COM5:","COM6:","COM7:","COM8:","USB (Palm)","USBh (Handspring)","USBl (small blocks)"};
#define numspeednames 8
char* speednames[numspeednames]={"115200","57600","38400","28800","19200","9600","4800","2400"};
int speedvalues[numspeednames]={115200,57600,38400,28800,19200,9600,4800,2400};

HINSTANCE hinst;
HWND hWndMain=NULL;
BOOL fconnected=false;
UINT_PTR hardaborttimer=NULL;

char inifilename[MAX_PATH]="fsplugin.ini";
char pluginname[]="Serial Transfer Plugin";
char defrootname[]="Serial port";

BOOL soft_aborted=false;     // try to end a transfer
BOOL hard_aborted=false;     // try to abort after 5 seconds
BOOL disablereading=false;   // disable reading of subdirs to delete whole drives

BOOL APIENTRY DllMain( HANDLE hModule, 
                       DWORD  ul_reason_for_call, 
                       LPVOID lpReserved
                )
{
	if (ul_reason_for_call==DLL_PROCESS_ATTACH)
		hinst=(HINSTANCE)hModule;
	else if (ul_reason_for_call==DLL_PROCESS_DETACH) {
		if (hardaborttimer)
			KillTimer(NULL,hardaborttimer);
		hardaborttimer=NULL;
	}
	return TRUE;
}

int PluginNumber;
tProgressProc ProgressProc;
tLogProc LogProc;
tRequestProc RequestProc;

void _stdcall TimerProc(HWND hwnd,UINT uMsg,WPARAM idEvent,DWORD dwTime)
{
	if (hardaborttimer)
		KillTimer(NULL,hardaborttimer);
	hardaborttimer=NULL;
	if (soft_aborted)
		hard_aborted=true;
}

int lastpercent=0;
DWORD lastpercenttime=-1;

BOOL MessageLoop(void)
{
	if (ProgressProc && abs((int)(GetCurrentTime()-lastpercenttime))>250) {
		// important: also call AFTER soft_aborted is true!!!
		BOOL aborted=0!=ProgressProc(PluginNumber,NULL,NULL,lastpercent);
		// allow abort with Escape when there is no progress dialog!
		if (aborted) {
			soft_aborted=true;
			if (!hardaborttimer)
				hardaborttimer=SetTimer(NULL,0,3000,&TimerProc);
		} else if (GetAsyncKeyState(VK_ESCAPE)<0) {
			HWND hwnd=GetActiveWindow();
			if (hwnd) {
				DWORD processid=0;
				// Make sure that Escape isn't pressed in some other program!
				GetWindowThreadProcessId(hwnd,&processid);
				if (processid==GetCurrentProcessId()) {
					// Finally, make sure it's Total Commander and not Lister
					HWND hwnd2;
					char classname[MAX_PATH];
					do {
						hwnd2=GetParent(hwnd);
						if (hwnd2)
							hwnd=hwnd2;
					} while (hwnd2);
					classname[0]=0;
					GetClassName(hwnd,classname,sizeof(classname)-1);
					if (stricmp(classname,"TTOTAL_CMD")==0) {
						hard_aborted=true;
						soft_aborted=true;
					}
				}
			}
		}
		lastpercenttime=GetCurrentTime();
	}
	return hard_aborted;
}

void UpdatePercentBar(int offset,int filesize)
{
	if (!filesize)
		return;
	int percent=MulDiv(offset,100,filesize);
	if (percent<0) percent=0;
	if (percent>100) percent=100;
	lastpercent=percent;  // used for MessageLoop below

	MessageLoop();  // This actually sets the percent bar!
}

BOOL UserAbort(void)
{
	MessageLoop();
	return soft_aborted;
}

void ShowStatus(char* status)
{
	if (LogProc)
		LogProc(PluginNumber,MSGTYPE_DETAILS,status);
}

int __stdcall FsInit(int PluginNr,tProgressProc pProgressProc,tLogProc pLogProc,tRequestProc pRequestProc)
{
	ProgressProc=pProgressProc;
	LogProc=pLogProc;
	RequestProc=pRequestProc;
	PluginNumber=PluginNr;
	return 0;
}

typedef struct {
   char* dirlisting;
   char* plastname;
} tLastFindStuct,*pLastFindStuct;

char* GetNextName(char* plastname,WIN32_FIND_DATA *FindData)
{
	if (!plastname[0])
		return NULL;
	
	BOOL lastprocessed=false;
	char* pendname=strchr(plastname,'\r');
	if (!pendname) {
		lastprocessed=true;
		pendname=plastname+strlen(plastname);
	}
	pendname[0]=0;

	FindData->dwFileAttributes=0;
	FindData->ftLastWriteTime.dwHighDateTime=0xFFFFFFFF;
	FindData->ftLastWriteTime.dwLowDateTime=0xFFFFFFFE;
	FindData->nFileSizeLow=0;

	char* psizefield=strchr(plastname,'\t');
	if (psizefield) {
		psizefield[0]=0;
		psizefield++;
		char* pdatetimefield=strchr(psizefield,'\t');
		if (pdatetimefield) {
			pdatetimefield[0]=0;
			pdatetimefield++;
			char* pattrfield=strchr(pdatetimefield,'\t');
			if (pattrfield) {
				pattrfield[0]=0;
				pattrfield++;
				char* potherfields=strchr(pattrfield,'\t');  // future extensions
				if (potherfields)
					potherfields[0]=0;
				FindData->dwFileAttributes=atoi(pattrfield);
			}
			ConvertIsoDateToDateTime(pdatetimefield,&FindData->ftLastWriteTime);
		}
		FindData->nFileSizeLow=atoi(psizefield);
	}
	if (plastname[0]) {
		BOOL isdir=false;
		int l=strlen(plastname);
		if (l>0) {
			if (plastname[l-1]=='\\') {
				plastname[l-1]=0;
				isdir=true;
			}
		}
		strlcpy(FindData->cFileName,plastname,sizeof(FindData->cFileName)-1);
		if (isdir) {
			FindData->dwFileAttributes|=FILE_ATTRIBUTE_DIRECTORY;
		} else {
			if (FindData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				FindData->dwFileAttributes-=FILE_ATTRIBUTE_DIRECTORY;
		}
	} else {
		pendname=NULL;
		lastprocessed=true;
	}
	if (!lastprocessed)
		return pendname+2;
	else
		return pendname;
	
}

BOOL __stdcall FsDisconnect(char* DisconnectRoot)
{
	if (fconnected) {
		LogProc(PluginNumber,MSGTYPE_DISCONNECT,"DISCONNECT \\");
		SerialCloseConnection();
		fconnected=false;
	}
	return TRUE;
}

char* theport_ptr;
int* speedptr;
BOOL configonly;

int __stdcall ConnectDlgProc(HWND hWnd,unsigned int Message,WPARAM wParam,LPARAM lParam)
{
	RECT rt1,rt2;
	int i,w,h,DlgWidth,DlgHeight,NewPosX,NewPosY;
	char speedname[16];

	switch (Message) {
	case WM_INITDIALOG: {
		BOOL showdialog=GetPrivateProfileInt(pluginname,"ShowConnectDialog",1,inifilename)!=0;

		CheckDlgButton(hWnd,IDC_SHOWDIALOG,showdialog ? BST_UNCHECKED : BST_CHECKED);

		for (i=0;i<numportnames;i++)
			SendDlgItemMessage(hWnd,IDC_PORTLIST,LB_ADDSTRING,0,(LPARAM)portnames[i]);

		if (LB_ERR==SendDlgItemMessage(hWnd,IDC_PORTLIST,LB_SELECTSTRING,-1,(LPARAM)theport_ptr))
			SendDlgItemMessage(hWnd,IDC_PORTLIST,LB_SETCURSEL,0,0);

		for (i=0;i<numspeednames;i++)
			SendDlgItemMessage(hWnd,IDC_SPEEDLIST,LB_ADDSTRING,0,(LPARAM)speednames[i]);

		itoa(*speedptr,speedname,10);
		if (LB_ERR==SendDlgItemMessage(hWnd,IDC_SPEEDLIST,LB_SELECTSTRING,-1,(LPARAM)&speedname))
		   SendDlgItemMessage(hWnd,IDC_SPEEDLIST,LB_SETCURSEL,0,0);

		if (configonly)
			SetDlgItemText(hWnd,IDOK,"OK");

		SetFocus(GetDlgItem(hWnd,IDC_PORTLIST));
		// trying to center the About dialog
		if (GetWindowRect(hWnd, &rt1) && GetWindowRect(GetParent(hWnd), &rt2)) {
			w=rt2.right-rt2.left;
			h=rt2.bottom-rt2.top;
			DlgWidth   = rt1.right - rt1.left;
			DlgHeight   = rt1.bottom - rt1.top ;
			NewPosX      =rt2.left + (w - DlgWidth)/2;
			NewPosY      =rt2.top + (h - DlgHeight)/2;
			SetWindowPos(hWnd, 0, NewPosX, NewPosY,
				0, 0, SWP_NOZORDER | SWP_NOSIZE);
		}
		return 1;
		break;
	}
	case WM_COMMAND: {
		switch(LOWORD(wParam)) {
			case IDOK: {
				i=SendDlgItemMessage(hWnd,IDC_PORTLIST,LB_GETCURSEL,0,0);
				if (i!=LB_ERR) {
					SendDlgItemMessage(hWnd,IDC_PORTLIST,LB_GETTEXT,i,(LPARAM)theport_ptr);
					if (strncmp(theport_ptr,"USB",3)==0)
						if (theport_ptr[3]=='h' || theport_ptr[3]=='l')
							theport_ptr[4]=0;   // cut off explanation!
						else
							theport_ptr[3]=0;   // cut off explanation!
				}

				i=SendDlgItemMessage(hWnd,IDC_SPEEDLIST,LB_GETCURSEL,0,0);
				if (i!=LB_ERR)
					*speedptr=speedvalues[i];

				WritePrivateProfileString(pluginname,"port",theport_ptr,inifilename);
				itoa(*speedptr,speedname,10);
				WritePrivateProfileString(pluginname,"TransferSpeed",speedname,inifilename);
				BOOL showdialog=IsDlgButtonChecked(hWnd,IDC_SHOWDIALOG)!=BST_CHECKED;
				WritePrivateProfileString(pluginname,"ShowConnectDialog",showdialog ? "1" : "0",inifilename);

				EndDialog(hWnd, IDOK);
				return 1;
			}
			case IDCANCEL: {
				EndDialog(hWnd, IDCANCEL);
				return 1;
			}
		}
	}
	}
	return 0;
}

BOOL ShowConnectDialog(char* theport,int* speed,BOOL configure)
{
	theport_ptr=theport;
	speedptr=speed;
	configonly=configure;
	return (IDOK==DialogBox(hinst,MAKEINTRESOURCE(IDD_CONNECTDIALOG),GetActiveWindow(),(DLGPROC)ConnectDlgProc));
}

BOOL ConnectIfNeeded(char* Path)
{
	char theport[MAX_PATH];
	int speed;
	BOOL showdialog,wasconnected;

	wasconnected=false;

	if (fconnected && !SerialConnected()) {  // connection  was lost
		fconnected=false;
		wasconnected=true;
	}

	if (!fconnected) {
		// Get connection settings here
		showdialog=GetPrivateProfileInt(pluginname,"ShowConnectDialog",1,inifilename)!=0;
		GetPrivateProfileString(pluginname,"port","COM1:",theport,sizeof(theport)-1,inifilename);
		speed=GetPrivateProfileInt(pluginname,"TransferSpeed",115200,inifilename);

		if (!showdialog || ShowConnectDialog(theport,&speed,false)) {
			if (ProgressProc(PluginNumber,Path,"temp",0))
				return false;
			if (!SerialConnect(GetActiveWindow(),hinst,theport,speed,0))
				return false;
			if (SerialTryConnection()) {
				if (LogProc && !wasconnected)
					LogProc(PluginNumber,MSGTYPE_CONNECT,"CONNECT \\");
				fconnected=true;
			} else {
				SerialCloseConnection();
				return false;
			}
		}
	}
	return fconnected;
}


HANDLE __stdcall FsFindFirst(char* Path,WIN32_FIND_DATA *FindData)
{
	char remotedir[MAX_PATH],temppath[MAX_PATH],tempname[MAX_PATH];
	pLastFindStuct lf;

	soft_aborted=false;
	hard_aborted=false;

	lastpercent=0;
	lastpercenttime=GetCurrentTime();  // avoid progress dialog for short connections

	if (disablereading) {
		SetLastError(ERROR_PATH_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}

	if (!ConnectIfNeeded(Path)) {
		SetLastError(ERROR_PATH_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}

	if (ProgressProc(PluginNumber,Path,"temp",0)) {
		SetLastError(ERROR_PATH_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}

	memset(FindData,0,sizeof(WIN32_FIND_DATA));
	strcpy(remotedir,Path);

	// Retrieve the directory
	if (0==GetTempPath(MAX_PATH-1,temppath))
		strcpy(temppath,".");
	GetTempFileName(temppath,"PALM",0,tempname);
	if (ser_up_ok!=SerialDownloadFile(remotedir,tempname,true,NULL,0,TRUE)) {
		DeleteFile(tempname);
		SetLastError(ERROR_PATH_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}

	// Read the names
	HANDLE fh=CreateFile(tempname,GENERIC_READ,FILE_SHARE_READ,
		NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	if (fh) {
		int sz=GetFileSize(fh,NULL);
		if (sz) {
			char* dirlisting=(char*)malloc(sz+1);
			if (dirlisting) {
				DWORD szr=0;
				ReadFile(fh,dirlisting,sz,&szr,NULL);
				if (szr>=0)
					dirlisting[szr]=0;
				else
					dirlisting[0]=0;
				char* plastname=dirlisting;

				CloseHandle(fh);
				DeleteFile(tempname);

				if (plastname=GetNextName(plastname,FindData)) {
					lf=(pLastFindStuct)malloc(sizeof(tLastFindStuct));
					lf->dirlisting=dirlisting;
					lf->plastname=plastname;
					return (HANDLE)lf;
				} else {
					SetLastError(ERROR_NO_MORE_FILES);
					return INVALID_HANDLE_VALUE;
				}
			}
		}
		CloseHandle(fh);
		DeleteFile(tempname);
		if (!sz) {
			SetLastError(ERROR_NO_MORE_FILES);
			return INVALID_HANDLE_VALUE;
		}
	}
	return INVALID_HANDLE_VALUE;
}

BOOL __stdcall FsFindNext(HANDLE Hdl,WIN32_FIND_DATA *FindData)
{
	pLastFindStuct lf;

	if ((int)Hdl==1)
		return false;

	lf=(pLastFindStuct)Hdl;
	if (lf!=INVALID_HANDLE_VALUE) {
		lf->plastname=GetNextName(lf->plastname,FindData);
		return (lf->plastname!=NULL);
	}
	return false;
}

int __stdcall FsFindClose(HANDLE Hdl)
{
   if (Hdl==INVALID_HANDLE_VALUE)
      return 0;
   pLastFindStuct lf;
   lf=(pLastFindStuct)Hdl;
   if (lf->dirlisting) {
	   free(lf->dirlisting);
	   lf->dirlisting=NULL;
	   lf->plastname=NULL;
   }
   free(lf);
   return 0;
}

BOOL __stdcall FsMkDir(char* Path)
{
	lastpercent=0;
	soft_aborted=false;
	hard_aborted=false;
	return SerialCreateDirectory(Path);
}

int __stdcall FsExecuteFile(HWND MainWin,char* RemoteName,char* Verb)
{
	if (stricmp(Verb,"open")==0) {
		return FS_EXEC_YOURSELF;
	} else if (stricmp(Verb,"properties")==0) {
		if (strcmp(RemoteName,"\\")==0) {
			char theport[MAX_PATH];
			int speed;

			GetPrivateProfileString(pluginname,"port","COM1:",theport,sizeof(theport)-1,inifilename);
			speed=GetPrivateProfileInt(pluginname,"TransferSpeed",115200,inifilename);
			ShowConnectDialog(theport,&speed,true);
			return FS_EXEC_OK;
		} else
			return FS_EXEC_ERROR;
	} else
		return FS_EXEC_ERROR;
}

int __stdcall FsRenMovFile(char* OldName,char* NewName,BOOL Move,BOOL OverWrite,RemoteInfoStruct* ri)
{
	lastpercent=0;
	if (SerialRenameMoveFile(OldName,NewName,Move))
		return FS_FILE_OK;
	else
		return FS_FILE_NOTFOUND;
}

BOOL FileExists(char* LocalName)
{
	WIN32_FIND_DATA s;
	HANDLE findhandle;
	findhandle=FindFirstFile(LocalName,&s);
	if (findhandle==INVALID_HANDLE_VALUE)
		return false;
	else {
		FindClose(findhandle);
		return true;
	}
}

void RemoveInalidChars(char* p)
{
   while (p[0]) {
      if ((unsigned char)(p[0])<32)
         p[0]=' ';
      else if (p[0]==':' || p[0]=='|' || p[0]=='*' || p[0]=='?' || p[0]=='\\' || p[0]=='/' || p[0]=='"')
         p[0]='_';
      p++;
   }
}

int __stdcall FsGetFile(char* RemoteName,char* LocalName,int CopyFlags,RemoteInfoStruct* ri)
{
	int err;
	BOOL OverWrite,Resume,Move;

	lastpercent=0;
	soft_aborted=false;
	hard_aborted=false;
	OverWrite=CopyFlags & FS_COPYFLAGS_OVERWRITE;
	Resume=CopyFlags & FS_COPYFLAGS_RESUME;
	Move=CopyFlags & FS_COPYFLAGS_MOVE;

	if (strlen(RemoteName)<3)
		return FS_FILE_NOTFOUND;

	char* p=strrchr(LocalName,'\\');
	if (p)
		RemoveInalidChars(p+1);  // Changes the name passed in!

	err=ProgressProc(PluginNumber,RemoteName,LocalName,0);
	if (err)
		return FS_FILE_USERABORT;
	if (OverWrite)
		DeleteFile(LocalName);
	else {
		if (!Resume && FileExists(LocalName))
			return FS_FILE_EXISTSRESUMEALLOWED;
	}

	switch (SerialDownloadFile(RemoteName,LocalName,true,&ri->LastWriteTime,Resume,false)) {
		case ser_up_ok:return FS_FILE_OK;
		case ser_up_exists:return FS_FILE_EXISTS;
		case ser_up_readfailed:return FS_FILE_READERROR;
		case ser_up_writefailed:return FS_FILE_WRITEERROR;
		case ser_up_abort:return FS_FILE_USERABORT;
	}
	return FS_FILE_OK;
}


int __stdcall FsPutFile(char* LocalName,char* RemoteName,int CopyFlags)
{
	int err;
	DWORD resumeat;
	BOOL OverWrite,Resume,Move;

	lastpercent=0;
	soft_aborted=false;
	hard_aborted=false;
	OverWrite=CopyFlags & FS_COPYFLAGS_OVERWRITE;
	Resume=CopyFlags & FS_COPYFLAGS_RESUME;
	Move=CopyFlags & FS_COPYFLAGS_MOVE;

	resumeat=0;
	if (Resume) {
		resumeat=SerialGetFileSize(RemoteName);
		if (resumeat==0xFFFFFFFF)
			return FS_FILE_NOTSUPPORTED;
	}

	if (strlen(RemoteName)<3)
		return FS_FILE_WRITEERROR;

	err=ProgressProc(PluginNumber,LocalName,RemoteName,0);
	if (err)
		return FS_FILE_USERABORT;

	if (OverWrite && !Resume)
		SerialSetOverwriteFlag();

	switch (SerialUploadFile(LocalName,RemoteName,resumeat)) {
		case ser_up_ok:return FS_FILE_OK;
		case ser_up_exists:return FS_FILE_EXISTSRESUMEALLOWED;
		case ser_up_readfailed:return FS_FILE_READERROR;
		case ser_up_writefailed:return FS_FILE_WRITEERROR;
		case ser_up_abort:return FS_FILE_USERABORT;
	}
	return FS_FILE_NOTFOUND;
}

BOOL __stdcall FsDeleteFile(char* RemoteName)
{
	if (strlen(RemoteName)<3)
		return false;

	lastpercent=0;
	soft_aborted=false;
	hard_aborted=false;
	return SerialDeleteFile(RemoteName);
}

BOOL __stdcall FsRemoveDir(char* RemoteName)
{
	if (strlen(RemoteName)<3)
		return false;

	lastpercent=0;
	soft_aborted=false;
	hard_aborted=false;

	return SerialDeleteFile(RemoteName);
}

BOOL __stdcall FsSetAttr(char* RemoteName,int NewAttr)
{
	lastpercent=0;
	soft_aborted=false;
	hard_aborted=false;
	return SerialSetAttr(RemoteName,NewAttr);
}

BOOL __stdcall FsSetTime(char* RemoteName,FILETIME *CreationTime,
      FILETIME *LastAccessTime,FILETIME *LastWriteTime)
{
	lastpercent=0;
	soft_aborted=false;
	hard_aborted=false;
	return SerialSetDateTime(RemoteName,LastWriteTime);
}

void __stdcall FsStatusInfo(char* RemoteDir,int InfoStartEnd,int InfoOperation)
{
	if (InfoOperation==FS_STATUS_OP_DELETE && strlen(RemoteDir)<2)
	   if (InfoStartEnd==FS_STATUS_START)
			disablereading=true;
	   else
			disablereading=false;
}

void __stdcall FsGetDefRootName(char* DefRootName,int maxlen)
{
   strlcpy(DefRootName,defrootname,maxlen);
}

void __stdcall FsSetDefaultParams(FsDefaultParamStruct* dps)
{
   strlcpy(inifilename,dps->DefaultIniName,MAX_PATH-1);
}
