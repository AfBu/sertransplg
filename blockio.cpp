#include "stdafx.h"
#include "datatypes.h"
#include "lowlvlio.h"
#include "blockio.h"
#include "utils.h"

extern void ShowStatus(char* status);  // must be defined in main app

int SendNextBlock(char request,char* databuf,UInt32 datalen,UInt8 sequencenr)
{
	Int32 l;
	UInt16 crc;
	Err err;
	char headerbuf[8];
	char crcbuf[2];
	char bytereceived,prevbytereceived;

	if (!Connected())
		return RQ_TRANSFERERR;
	
	headerbuf[0]='F';
	headerbuf[1]='T';
	if (request==CMD_BODY)
		headerbuf[2]=(char)sequencenr;
	else
		headerbuf[2]='H';
	headerbuf[3]=request;
	l=8+datalen;
	if (datalen)
		l+=2;	// the crc!
	headerbuf[4]=(l >> 8) & 255;
	headerbuf[5]=l & 255;
	crc = Crc16CalcBlock(headerbuf, 6, 0);
	headerbuf[6]=(char)(crc >> 8);
	headerbuf[7]=(char)crc;

	// Send header
	err=TimedSendBytes(headerbuf,8,5000);
	if (err) {
		ShowStatus("Header Write error!");
        if (err==err_abort)
			return RQ_LOCALABORT;
		else
			return RQ_LOCALTIMEOUT;
	}

	if (datalen>0) {
		// Important: calculate CRC before sending data!
		// Reason: Receiving the final CRC is time-critical
		// because if 1-2 bytes are lost, filepc2pda has to react quickly
		crc = Crc16CalcBlock(&headerbuf[6], 2, crc);
		crc = Crc16CalcBlock(databuf, datalen, crc);
		crcbuf[0]=(char)(crc >> 8);
		crcbuf[1]=(char)crc;
		
		// Send body
		err=TimedSendBytes(databuf,datalen,5000);  // this can take a while
		if (err) {
			ShowStatus("Body Write error!");
			if (err==err_abort)
				return RQ_LOCALABORT;
			else
				return RQ_LOCALTIMEOUT;
		}
		// Send crc
		err=TimedSendBytes(crcbuf,2,5000);
		if (err) {
			ShowStatus("CRC Write error!");
			if (err==err_abort)
				return RQ_LOCALABORT;
			else
				return RQ_LOCALTIMEOUT;
		}
	
	}
	// wait max. 5 seconds for reply.
	err=TimedReceiveChar(&bytereceived,5000);
	if (err)
		if (err==err_abort)
			return RQ_LOCALABORT;
		else
			return RQ_LOCALTIMEOUT;

	// Server sends RQ_WAIT for long operations - wait up to 1 minute!
	while (bytereceived==RQ_WAIT) {
		err=TimedReceiveChar(&bytereceived,60000);
		if (err)
			if (err==err_abort)
				return RQ_LOCALABORT;
			else
				return RQ_LOCALTIMEOUT;
	}

	// server: if 'F' received, client tries to send new command!!!
	if (bytereceived==RQ_F)
		return RQ_TRANSFERERR;  // abort operation!

	while (err==0 && !(bytereceived==RQ_OK || bytereceived==RQ_CRCERR || bytereceived==RQ_FAILED || bytereceived==RQ_TRANSFERERR)) {
		prevbytereceived=bytereceived;
		err=TimedReceiveChar(&bytereceived,100); // delete any bytes before the actual reply!
		if (err)
			bytereceived=prevbytereceived;
	}
	if (bytereceived==RQ_OK || bytereceived==RQ_CRCERR || bytereceived==RQ_FAILED || bytereceived==RQ_TRANSFERERR)
		return bytereceived;
	if (err==err_abort)
		return RQ_LOCALABORT;
	else
		return RQ_LOCALTIMEOUT;
}

char ReceiveNextBlock(char* recvbody,UInt32 maxbodysize,UInt32* datareceived,UInt8* sequencenr)
{
	Err err;
	UInt8 headerbuf[10];
	UInt8 crcbuf[2];
	UInt32 packetlen;
	UInt32 blockread;
	UInt32 fullblocksize;
	UInt32 thisblockpart;
	UInt32 receive;
	UInt16 crc,headcrc,fullcrc;
	UInt8* p;

	if (!Connected())
		return CMD_RECVERR;

	headerbuf[0]=0;
	headerbuf[8]=0;
	headerbuf[9]=0;
	// Wait for first 8 bytes of command

	receive=8;
	err=TimedReceiveBytes((char*)&headerbuf,&receive,-1);   // wait indefinitely or until user clicks cancel
	if (err==err_abort)
		return CMD_ABORT;

	p=headerbuf;
	while (p<headerbuf+6 && !(p[0]=='F' && p[1]=='T'  && (p[2]=='H' || p[3]==CMD_BODY)))
		p++;
	if (p!=headerbuf && p<headerbuf+6) {  // found header!
		ShowStatus("Try to fix header!");
		memmove(&headerbuf, p, 8-(p-headerbuf));
		p=headerbuf+8-(p-headerbuf);
		receive=8-(p-headerbuf);
		err=TimedReceiveBytes((char*)p,&receive,-1);   // wait indefinitely or until user clicks cancel
		if (err==err_abort)
			return CMD_ABORT;
	}

	if (sequencenr)
		*sequencenr=(UInt8)headerbuf[2];
	packetlen=(UInt32)256*headerbuf[4]+headerbuf[5];

	crc = Crc16CalcBlock ((char*)headerbuf, 6, 0);
	headcrc = (UInt16)256*headerbuf[6]+headerbuf[7];

	// Three cases:
	// 1. Completely received header with correct CRC -> get body
	// 2. Completely received header with errors -> flush input
	// 3. Receive error (not enough bytes) -> return error

	if (!err && headerbuf[0]=='F' && headerbuf[1]=='T' && (headerbuf[2]=='H' || headerbuf[3]==CMD_BODY) &&
		(crc==headcrc) &&
		packetlen>=8 && packetlen<=maxbodysize+10) {
		// Case 1:
		// Wait for remaining packetlen-8 bytes of command
		blockread=0;
		if (packetlen<10)
			fullblocksize=0;
		else
			fullblocksize=packetlen-10;  //minus header and CRC!
		if (datareceived)
			*datareceived=fullblocksize;
		while (blockread<fullblocksize) {
			// Receive block in 256 byte chunks
			thisblockpart=fullblocksize-blockread;
			if (thisblockpart>maxblockpart)    // only works with larger receive buffer above!
				thisblockpart=maxblockpart;
			err=TimedReceiveBytes(recvbody+blockread,&thisblockpart,5000);   // wait up to 5 seconds for the rest
			if (err==err_abort) {
				ShowStatus("Aborting...");
				return CMD_ABORT;
			} else if (err) {
				ShowStatus("Transfer error...");
				if (!Connected())
					return CMD_RECVERR;
				else
					return CMD_TIMEOUT;
			}
			blockread+=thisblockpart;
		} // end while
		// wait for crc of whole block
		if (fullblocksize) {
			receive=2;
			err=TimedReceiveBytes((char*)&crcbuf,&receive,1000);	// wait only 1 second for CRC. Reason: if 1-2 bytes are lost
			fullcrc=(UInt16)256*crcbuf[0]+crcbuf[1];
			if (err==err_abort)
				return CMD_ABORT;
			crc = Crc16CalcBlock((char*)headerbuf+6, 2, crc);
			crc = Crc16CalcBlock(recvbody, fullblocksize, crc);
			if (crc!=fullcrc) {
				ShowStatus("BAD CRC of body");
				return CMD_CRCERR;
			}
		}
	} else if (err==0) {    // case 2: data recieved, but bad
		// to do: get rid of any extra bytes in front of the header!
		if (!(headerbuf[0]=='F' && headerbuf[1]=='T'))
			ShowStatus("Bad header!");
		else if (crc!=headcrc)
			ShowStatus("Bad CRC");
		else ShowStatus("Header bad");
		do {   //Flush
			receive=8;
			err=TimedReceiveBytes((char*)headerbuf,&receive,250);
		} while(err==0);
		do {   //Flush
			err=TimedReceiveChar((char*)&headerbuf,250);
		} while(err==0);
		err=CMD_CRCERR;
	} else {             // case 3: data not received
		err=CMD_TIMEOUT;
		ShowStatus("Timeout!");
		recvbody[0]=0;
	}

	if (!err) {
		return headerbuf[3];
	} else
		return err;
}

int SendNextBlockRetry10(char request,char* databuf,int datalen,UInt8 sequencenr)
{
	int err;
	int retries=0;
	do {
		err=SendNextBlock(request,databuf,datalen,sequencenr);
		if (!Connected())
			return RQ_TRANSFERERR;
		if (err==RQ_CRCERR) {
			ClearOutputBuffer();					// delete any unsent data
			ClearInputBuffer();						// delete any received data
			ShowStatus("GET CRC error");
		}
		if (err==RQ_LOCALTIMEOUT) {
			Sleep(1000);
			ClearOutputBuffer();					// delete any unsent data
			ClearInputBuffer();						// delete any received data
			ShowStatus("Timeout!");
			Sleep(4000);
		}
	} while ((err==RQ_CRCERR || err==RQ_LOCALTIMEOUT) && retries++<10);
	return err;
}

