#ifdef watcom
#define true 1
#define false 0
#endif

// Helper functions
BOOL ConvertIsoDateToDateTime(char* pdatetimefield,FILETIME *ft);
BOOL CreateIsoDateString(FILETIME *ft,char* buf); //yyyymmddhhmmss
char* strlcpy(char* p,const char* p2,int maxlen);
WORD Crc16CalcBlock(char* p, int len, WORD crc16);
LPTSTR strcatbackslash(LPTSTR thedir);
