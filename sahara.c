#include "include.h"

//****************************************
//* sahara loading
//****************************************
int dload_sahara() {

FILE* in;
char infilename[200]="loaders/";
unsigned char sendbuf[131072];
unsigned char replybuf[128];
unsigned int iolen,offset,len,donestat,imgid;
unsigned char helloreply[60]={
 02, 00, 00, 00, 48, 00, 00, 00, 02, 00, 00, 00, 01, 00, 00, 00,
 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00,
 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00, 00
}; 
unsigned char donemes[8]={5,0,0,0,8,0,0,0};

printf("\n Waiting for a Hello packet from the device...\n");
port_timeout(100); // Hello packet we will wait 10 seconds
iolen=read(siofd,replybuf,48);  // read Hello
if ((iolen != 48)||(replybuf[0] != 1)) {
	sendbuf[0]=0x3a; // может быть любое число
	write(siofd,sendbuf,1); // initiate sending a Hello packet
	iolen=read(siofd,replybuf,48);  // try reading Hello again
	if ((iolen != 48)||(replybuf[0] != 1)) { // now everything - nothing more to wait
		printf("\n Hello packet from device not received\n");
		dump(replybuf,iolen,0);
		return 1;
	}
}

// Got Hello,
ttyflush();  // clear the receive buffer
port_timeout(10); // now packet exchange will go faster - timeout 1 s
write(siofd,helloreply,48);   // send Hello Response with mode switching
iolen=read(siofd,replybuf,20); // response packet
  if (iolen == 0) {
    printf("\n No response from device\n");
    return 1;
  }  
// in replybuf there should be a request for the first loader block
imgid=*((unsigned int*)&replybuf[8]); // image id
printf("\n Boot image id: %08x\n",imgid);
switch (imgid) {

	case 0x07:
	  strcat(infilename,get_nprg());
	break;

	case 0x0d:
	  strcat(infilename,get_enprg());
	break;

	default:
          printf("\n Unknown identifier - no such image!\n");
	return 1;
}
printf("\n Download %s...\n",infilename); fflush(stdout);
in=fopen(infilename,"rb");
if (in == 0) {
  printf("\n Error opening input file %s\n",infilename);
  return 1;
}

// The main loader code transfer loop
printf("\n We transfer the bootloader to the device...\n");
while(replybuf[0] != 4) { // EOIT message
 if (replybuf[0] != 3) { // read data message
    printf("\n Package with Invalid Code - Abort Download!");
    dump(replybuf,iolen,0);
    fclose(in);
    return 1;
 }
  // select the parameters of the file fragment
  offset=*((unsigned int*)&replybuf[12]);
  len=*((unsigned int*)&replybuf[16]);
//  printf("\n address=%08x length=%08x",offset,len);
  fseek(in,offset,SEEK_SET);
  fread(sendbuf,1,len,in);
  // send data block sahara
  write(siofd,sendbuf,len);
  // we get the answer
  iolen=read(siofd,replybuf,20);      // response packet
  if (iolen == 0) {
    printf("\n No response from device\n");
    fclose(in);
    return 1;
  }  
}
// got EOIT, end of download
write(siofd,donemes,8);   // send package Done
iolen=read(siofd,replybuf,12); // expecting a done response
if (iolen == 0) {
  printf("\n No response from device\n");
  fclose(in);
  return 1;
} 
// get status
donestat=*((unsigned int*)&replybuf[12]); 
if (donestat == 0) {
  printf("\n Loader transferred successfully\n");
} else {
  printf("\n Bootloader transfer error\n");
}
fclose(in);

return donestat;

}

