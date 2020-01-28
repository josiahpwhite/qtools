#include "include.h"

// Boot block size
#define dlblock 1017

//****************************************************
//*  Separate partition tables into separate files
//****************************************************
//*/
void extract_ptable() {
  
FILE* out;

// load the system partition table from MIBIB
load_ptable("@");
printf("-----------------------------------------------------");
// check the table
if (!validpart) {
   printf("\nSystem partition table not found\n");
   return;
}
out=fopen("ptable/current-r.bin","wb");
if (out == 0) {
  printf("\n Error opening output file ptable/current-r.bin");
  return;
}  
fwrite(&fptable,sizeof(fptable),1,out);
printf("\n * Read mode partition table found");
fclose (out);
/*
// We are looking for a record table
for (pg=pg+1;pg<ppb;pg++) {
  flash_read(blk, pg, 0);  // sector 0 - beginning of the partition table
  memread(buf,sector_buf, udsize);
  if (memcmp(buf,"\x9a\x1b\x7d\xaa\xbc\x48\x7d\x1f",8) != 0) continue; // signature not found - look further

  // found a record table 
  mempoke(nand_exec,1);     // sector 1 - continuation of the table
  nandwait();
  memread(buf+udsize,sector_buf, udsize);
  npar=*((unsigned int*)&buf[12]); // number of sections in a table
  out=fopen("ptable/current-w.bin","wb");
  if (out == 0) {
    printf("\n Error opening output file ptable/current-w.bin");
    return;
  }  
  fwrite(buf,16+28*npar,1,out);
  fclose (out);
  printf("\n * Record partition table found");
  return;
}
printf("\n - Partition table not found");
*/
printf("\n");
  
}



//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@2
void main(int argc, char* argv[]) {

int opt;
unsigned int start=0;
#ifndef WIN32
char devname[50]="/dev/ttyUSB0";
#else
char devname[50]="";
#endif
FILE* in;
struct stat fstatus;
unsigned int i,partsize,iolen,adr,helloflag=0;
unsigned int sahara_flag=0;
unsigned int tflag=0;
unsigned int ident_flag=0, iaddr,ichipset;
unsigned int filesize;

unsigned char iobuf[4096];
unsigned char cmd1[]={0x06};
unsigned char cmd2[]={0x07};
unsigned char cmddl[2048]={0xf};
unsigned char cmdstart[2048]={0x5,0,0,0,0};
unsigned int delay=2;



while ((opt = getopt(argc, argv, "p:k:a:histd:q")) != -1) {
  switch (opt) {
   case 'h': 
     printf("\n The utility is designed to download (E)NPRG flasher programs to the modem memory\n\n\
The following keys are valid.:\n\n\
-p <tty>  - indicates the name of the serial port device translated into download mode\n\
-i        - starts the HELLO procedure to initialize the bootloader\n\
-q        - starts the HELLO procedure in a simplified mode without register settings\n\
-t        - removes partition tables from modem to files ptable/current-r(w).bin\n\
-s        - use SAHARA protocol\n\
-k #      - chipset code (-kl - get a list of codes)\n\
-a <adr>  - default download address 41700000\n\
-d <n>    - delay for bootloader initialization, 0.1с\n\
\n");
    return;
     
   case 'p':
    strcpy(devname,optarg);
    break;

   case 'k':
    define_chipset(optarg);
    break;

   case 'i':
    helloflag=1;
    break;
    
   case 'q':
    helloflag=2;
    break;
    
   case 's':
    sahara_flag=1;
    break;
    
   case 't':
    tflag=1;
    break;
    
   case 'a':
     sscanf(optarg,"%x",&start);
     break;

   case 'd':
     sscanf(optarg,"%u",&delay);
     break;

   case '?':
   case ':':  
     return;
    
  }     
}

if ((tflag == 1) && (helloflag == 0)) {
  printf("\n The -t switch cannot be specified without the -i switch\n");
  exit(1);
}  

delay*=100000; // convert to microseconds
#ifdef WIN32
if (*devname == '\0')
{
   printf("\n - Serial Port Not Set\n"); 
   return; 
}
#endif

if (!open_port(devname))  {
#ifndef WIN32
   printf("\n -Serial port %s does not open\n", devname); 
#else
   printf("\n - Serial port COM%s does not open\n", devname); 
#endif
   return; 
}

// Delete old partition tables

unlink("ptable/current-r.bin");
unlink("ptable/current-w.bin");


// If the chipset is already defined by the keys, we determine the sahara mode
if (chip_type != 0) sahara_flag=get_sahara();

if (!sahara_flag) {
 // open the input file
 in=fopen(argv[optind],"rb");
 if (in == 0) {
  printf("\nError opening input file\n");
  return;
 } 
 // Identify bootloader
 // We are looking for an identification block
 fseek(in,-12,SEEK_END);
 fread(&i,4,1,in);

 if (i == 0xdeadbeef) {
   // found the block - disassemble
   printf("\n Loader Identification Block Found");
   fread(&ichipset,4,1,in);
   fread(&iaddr,4,1,in);
   ident_flag=1;
   if (start == 0) start=iaddr;
   if (chip_type == 0) set_chipset(ichipset);  // we change the type of chipset to a specific one from the identification unit
 }
 rewind(in);
} 

// check the type of chipset
if ((chip_type == 0)&&(helloflag==1)) {
  printf("\n Chipset type not specified - full initialization not possible\n");
  helloflag=2;
}  

if ((helloflag == 0)&& (chip_type != 0))  printf("\n Chipset: %s",get_chipname());

//printf("\n chip_type = %i   sahara = %i",chip_type,sahara_flag);

if ((start == 0) && !sahara_flag) {
  printf("\n Missing download address\n");
  fclose(in);
  return;
}  



//----- sahara loading option -------

if (sahara_flag) {
  if (dload_sahara() == 0) {
	#ifndef WIN32
	usleep(200000);   // waiting for bootloader initialization
	#else
	Sleep(200);   // waiting for bootloader initialization
	#endif

	if (helloflag) {
		hello(helloflag);
		printf("\n");
		if (tflag && (helloflag != 2)) extract_ptable();  // we take out the partition tables
	}
  }
  return;
}	

//------- Download option by writing bootloader to memory ----------

printf("\n Bootloader file: %s\n Download address: %08x",argv[optind],start);
iolen=send_cmd_base(cmd1,1,iobuf,1);
if (iolen != 5) {
   printf("\n The modem is not in boot mode\n");
//   dump(iobuf,iolen,0);
   fclose(in);
   return;
}   
iolen=send_cmd_base(cmd2,1,iobuf,1);
#ifndef WIN32
fstat(fileno(in),&fstatus);
#else
fstat(_fileno(in),&fstatus);
#endif
filesize=fstatus.st_size;
if (ident_flag) filesize-=12; // cut off tail - identification block
printf("\n file size: %i\n",(unsigned int)filesize);
partsize=dlblock;

// Block loading cycle
for(i=0;i<filesize;i+=dlblock) {  
 if ((filesize-i) < dlblock) partsize=filesize-i;
 fread(cmddl+7,1,partsize,in);          // read the block directly into the command buffer
 adr=start+i;                           // download address of this block
   // As usual with wretched Chinese, numbers fit through the ass - in Big Endian format // translators note: ?????
   // enter the download address of this block
   cmddl[1]=(adr>>24)&0xff;
   cmddl[2]=(adr>>16)&0xff;
   cmddl[3]=(adr>>8)&0xff;
   cmddl[4]=(adr)&0xff;
   // enter the block size
   cmddl[5]=(partsize>>8)&0xff;
   cmddl[6]=(partsize)&0xff;
 iolen=send_cmd_base(cmddl,partsize+7,iobuf,1);
 printf("\r Uploaded: %i",i+partsize);
// dump(iobuf,iolen,0);
} 
// enter the address in the start command
printf("\n Bootloader launch..."); fflush(stdout);
cmdstart[1]=(start>>24)&0xff;
cmdstart[2]=(start>>16)&0xff;
cmdstart[3]=(start>>8)&0xff;
cmdstart[4]=(start)&0xff;
iolen=send_cmd_base(cmdstart,5,iobuf,1);
close_port();
#ifndef WIN32
usleep(delay);   // waiting for bootloader initialization
#else
Sleep(delay/1000);   // waiting for bootloader initialization
#endif
printf("ok\n");
if (helloflag) {
  if (!open_port(devname))  {
#ifndef WIN32
     printf("\n - Serial port %s does not open\n", devname); 
#else
     printf("\n - Serial port COM%s does not open\n", devname); 
#endif
     fclose(in);
     return; 
  }
  hello(helloflag);
  if (helloflag != 2)
     if (!bad_loader && tflag) extract_ptable();  // we take out the partition tables
}  
printf("\n");
fclose(in);
}

