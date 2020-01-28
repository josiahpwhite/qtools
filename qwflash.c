#include "include.h"

// Record Block Size
#define wbsize 1024
//#define wbsize 1538
  
// partition table storage
struct  {
  char filename[50];
  char partname[16];
} ptable[30];

unsigned int npart=0;    // number of sections in a table

unsigned int cfg0,cfg1; // save controller configuration

//*****************************************************
//*  Restore controller configuration
//*****************************************************
// */
void restore_reg() {
  
return;  
mempoke(nand_cfg0,cfg0);  
mempoke(nand_cfg1,cfg1);  
}


//***********************************8
//* Setting secure mode
//***********************************8
// */
int secure_mode() {
  
unsigned char iobuf[600];
unsigned char cmdbuf[]={0x17,1};
int iolen;

iolen=send_cmd(cmdbuf,2,iobuf);
if (iobuf[1] == 0x18) return 1;
show_errpacket("secure_mode()",iobuf,iolen);
return 0; // there was a mistake

}  


//*******************************************
//* Sending Partition Table Loader
//*******************************************
// */

void send_ptable(char* ptraw, unsigned int len, unsigned int mode) {
  
unsigned char iobuf[600];
unsigned char cmdbuf[8192]={0x19,0};
int iolen;
  
memcpy(cmdbuf+2,ptraw,len);
// firmware mode: 0 - flashing, 1 - full flashing
cmdbuf[1]=mode;
//printf("\n");
//dump(cmdbuf,len+2,0);

printf("\n We send the partition table...");
iolen=send_cmd(cmdbuf,len+2,iobuf);

if (iobuf[1] != 0x1a) {
  printf(" error!");
  show_errpacket("send_ptable()",iobuf,iolen);
  if (iolen == 0) {
    printf("\n Требуется загрузчик в режиме тихого запуска\n"); // this translation didnt make sense: "Silent bootloader required"
  }
  exit(1);
}  
if (iobuf[2] == 0) {
  printf("ok");
  return;
}  
printf("\n Partition tables do not match - full modem flashing required (-f switch)\n");
exit(1);
}

//*******************************************
//* Sending a section header to the loader
//*******************************************
// */
int send_head(char* name) {

unsigned char iobuf[600];
unsigned char cmdbuf[32]={0x1b,0x0e};
int iolen;
  
strcpy(cmdbuf+2,name);
iolen=send_cmd(cmdbuf,strlen(cmdbuf)+1,iobuf);
if (iobuf[1] == 0x1c) return 1;
show_errpacket("send_head()",iobuf,iolen);
return 0; // there was a mistake
}


//*******************************************
//@@@@@@@@@@@@ Head program
// */
void main(int argc, char* argv[]) {
  
unsigned char iobuf[14048];
unsigned char scmd[13068]={0x7,0,0,0};
char ptabraw[4048];
FILE* part;
int ptflag=0;
int listmode=0;

char* fptr;
#ifndef WIN32
char devname[50]="/dev/ttyUSB0";
#else
char devname[50]="";
#endif
unsigned int i,opt,iolen;
unsigned int adr,len;
unsigned int fsize;
unsigned int forceflag=0;

while ((opt = getopt(argc, argv, "hp:s:w:mk:f")) != -1) {
  switch (opt) {
   case 'h': 
    printf("\n  The utility is designed to write partitions (according to the table) to the modem flash\n\
The following keys are valid.:\n\n\
-p <tty>  - indicates the name of the serial port device for communication with the bootloader\n\
-k #      - chipset code (-kl - get a list of codes)\n\
-s <file> - take a partition map from the specified file\n\
-s -      - get partition map from ptable/current-w.bin file\n\
-f        - full flashing of the modem with changing the partition map\n\
-w file:part - write the section with the name part from the file file, the name of the section part is indicated without 0:\n\
-m        - only viewing the firmware map without real recording\n");
    return;
    
   case 'k':
    define_chipset(optarg);
    break;
    
   case 'p':
    strcpy(devname,optarg);
    break;
    
   case 'w':
     // definition of sections for recording
     strcpy(iobuf,optarg);
     
     // select the file name
     fptr=strchr(iobuf,':');
     if (fptr == 0) {
       printf("\nError in switch parameters -w - section name is not specified: %s\n",optarg);
       return;
     }
     *fptr=0; // file name delimiter
     strcpy(ptable[npart].filename,iobuf); // copy the file name
     fptr++;
     // copy the name of the section
     strcpy(ptable[npart].partname,"0:");
     strcat(ptable[npart].partname,fptr); 
     npart++;
     if (npart>19) {
       printf("\nToo many sections\n");
       return;
     }
     break;
     
   case 's':
       // load the partition table from the file
       if (optarg[0] == '-')   part=fopen("ptable/current-w.bin","rb");
       else part=fopen(optarg,"rb");
       if (part == 0) {
         printf("\nError opening partition table file\n");
         return;
       }	 
       fread(ptabraw,1024,1,part); // read the partition table from the file
       fclose(part);       
       ptflag=1; 
     break;
     
   case 'm':
     listmode=1;
     break;

   case 'f':
     forceflag=1;
     break;
     
   case '?':
   case ':':  
     return;
  }
}  


#ifdef WIN32
if (*devname == '\0')
{
   printf("\n - Serial Port Not Set\n"); 
   return; 
}
#endif

if (!open_port(devname))  {
#ifndef WIN32
   printf("\n - Serial port %s does not open\n", devname); 
#else
   printf("\n - Serial port COM%s does not open\n", devname); 
#endif
   return; 
}
hello(2);
// save the controller configuration
//cfg0=mempeek(nand_cfg0);
//cfg1=mempeek(nand_cfg1);



// firmware table output

printf("\n #  --Section-- ------- File -----");     
for(i=0;i<npart;i++) {
    printf("\n%02u  %-14.14s  %s",i,ptable[i].partname,ptable[i].filename);
}
printf("\n");
if (listmode)  return; // the -m switch is all.
  
printf("\n secure mode...");
if (!secure_mode()) {
  printf("\n Error entering Secure mode\n");
//  restore_reg();
  return;
}
printf("ok");
qclose(0);  //####################################################
#ifndef WIN32
  usleep(50000);
#else
  Sleep(50);
#endif
// send the partition table
if (ptflag) send_ptable(ptabraw,16+28*npart,forceflag);

// - the main recording cycle - by sections: -
port_timeout(1000);
for(i=0;i<npart;i++) {
  part=fopen(ptable[i].filename,"rb");
  if (part == 0) {
    printf("\n Section %u: file open error %s\n",i,ptable[i].filename);
    return;
  }
  
  // we get the size of the partition
  fseek(part,0,SEEK_END);
  fsize=ftell(part);
  rewind(part);

  printf("\n Section Record %u (%s)",i,ptable[i].partname); fflush(stdout);
  // send header
  if (!send_head(ptable[i].partname)) {
    printf("\n! Modem rejected section header\n");
    fclose(part);
    return;
  }  
  // write cycle of 1K section pieces per command
  for(adr=0;;adr+=wbsize) {  
    // address
    scmd[0]=7;
    scmd[1]=(adr)&0xff;
    scmd[2]=(adr>>8)&0xff;
    scmd[3]=(adr>>16)&0xff;
    scmd[4]=(adr>>24)&0xff;
    memset(scmd+5,0xff,wbsize+1);   // fill the FF data buffer
    len=fread(scmd+5,1,wbsize,part);
    printf("\r Section entry %u (%s): byte %u of %u (%i%%) ",i,ptable[i].partname,adr,fsize,(adr+1)*100/fsize); fflush(stdout);
    iolen=send_cmd_base(scmd,len+5,iobuf,0);
    if ((iolen == 0) || (iobuf[1] != 8)) {
      show_errpacket("Data packet ",iobuf,iolen);
      printf("\n Error writing partition %u (%s): address: %06x\n",i,ptable[i].partname,adr);
      fclose(part);
      return;
    }
    if (feof(part)) break; // end of section and end of file
  }
  // Section fully transferred
  if (!qclose(1)) {
    printf("\n Error closing data stream\n");
    fclose(part);
    return;
  }  
  printf(" ... recording completed");
#ifndef WIN32
  usleep(500000);
#else
  Sleep(500);
#endif
}
printf("\n");
fclose(part);
}


