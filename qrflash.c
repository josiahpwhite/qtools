#include "include.h"

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//* Read modem flash to file
//
// */

// flags of the handling of badblocks
enum {
  BAD_UNDEF,
  BAD_FILL,
  BAD_SKIP,
  BAD_IGNORE,
  BAD_DISABLE
};


// Readable Forms
enum {
  RF_AUTO,
  RF_STANDART,
  RF_LINUX,
  RF_YAFFS
};  

int bad_processing_flag=BAD_UNDEF;
unsigned char *blockbuf;

// Structure for saving a list of read errors

struct {
  int page;
  int sector;
  int errcode;
} errlist[1000];  

int errcount;
int qflag=0;

//********************************************************************************
//* Loading a block into a block buffer
//*
//*  returns 0 if the block is defective, or 1 if it was normally read
//* cwsize - size of the read sector (including OOB, if necessary)
//********************************************************************************
// */
unsigned int load_block(int blk, int cwsize) {

int pg,sec;
int oldudsize,cfg0;
unsigned int cfgecctemp;
int status;

errcount=0;
if (bad_processing_flag == BAD_DISABLE) hardware_bad_off();
else if (bad_processing_flag != BAD_IGNORE) {
   if (check_block(blk)) {
    // Bad block detected
    memset(blockbuf,0xbb,cwsize*spp*ppb); // fill the block buffer with a placeholder
    return 0; // return the sign of the bad block
  }
} 
// a good block, or we do not give a damn about the bad blocks - we read the block

// set udsize to the size of the read section
cfg0=mempeek(nand_cfg0);
//oldudsize=get_udsize();
//set_udsize(cwsize);
//set_sparesize(0);

nand_reset();
if (cwsize>(sectorsize+4)) mempoke(nand_cmd,0x34); // reading data + ecc + spare without correction
else mempoke(nand_cmd,0x33);    // reading data with correction

bch_reset();
for(pg=0;pg<ppb;pg++) {
  setaddr(blk,pg);
  for (sec=0;sec<spp;sec++) {
   mempoke(nand_exec,0x1); 
   nandwait();
   status=check_ecc_status();
   if (status != 0) {
//     printf("\n--- blk %x  pg %i  sec  %i err %i---\n",blk,pg,sec,check_ecc_status());
     errlist[errcount].page=pg;
     errlist[errcount].sector=sec;
     errlist[errcount].errcode=status;
     errcount++;
   }
   
   memread(blockbuf+(pg*spp+sec)*cwsize,sector_buf, cwsize);
//   dump(blockbuf+(pg*spp+sec)*cwsize,cwsize,0);
  } 
}  
if (bad_processing_flag == BAD_DISABLE) hardware_bad_on();
//set_udsize(oldudsize);
mempoke(nand_cfg0,cfg0);
return 1; // Fuck - block read
}
  
//***************************************
//* Reading a data block into an output file
//***************************************
// */
unsigned int read_block(int block,int cwsize,FILE* out) {

unsigned int okflag=0;

okflag=load_block(block,cwsize);
if (okflag || (bad_processing_flag != BAD_SKIP)) {
  // the block was read or not read, but we skip the bad blocks
   fwrite(blockbuf,1,cwsize*spp*ppb,out); // write it to a file
}
return !okflag;
} 

//********************************************************************************
//* Reading a data block for partitions with protected spare (516 byte sectors)
//*   yaffsmode = 0 - read data, 1 - read data and yaffs2 tag
//********************************************************************************
// */
unsigned int read_block_ext(int block, FILE* out, int yaffsmode) {
unsigned int page,sec;
unsigned int okflag;
unsigned char* pgoffset;
unsigned char* udoffset;
unsigned char extbuf[2048]; // pseudo-oob assembly buffer

okflag=load_block(block,sectorsize+4);
if (!okflag && (bad_processing_flag == BAD_SKIP)) return 1; // Bad block detected

// page loop
for(page=0;page<ppb;page++)  {
  pgoffset=blockbuf+page*spp*(sectorsize+4); // offset to current page
  // по секторам  
  for(sec=0;sec<spp;sec++) {
   udoffset=pgoffset+sec*(sectorsize+4); // offset to the current sector
//   printf("\n page %i  sector %i\n",page,sec);
   if (sec != (spp-1)) {
     // For non-recent sectors
     fwrite(udoffset,1,sectorsize+4,out);    // Sector body + 4 bytes OBB
//     dump(udoffset,sectorsize+4,udoffset-blockbuf);
   }  
   else { 
     // for the last sector
     fwrite(udoffset,1,sectorsize-4*(spp-1),out);   // Sector body - oob tail
//     dump(udoffset,sectorsize-4*(spp-1),udoffset-blockbuf);
   }  
  }

// Reading yaffs2 tag images
  if (yaffsmode) {
    memset(extbuf,0xff,oobsize);
    memcpy(extbuf,pgoffset+(sectorsize+4)*(spp-1)+(sectorsize-4*(spp-1)),16);
//    printf("\n page %i oob\n",page);
//    dump(pgoffset+(sectorsize+4)*(spp-1)+(sectorsize-4*(spp-1)),16,pgoffset+(sectorsize+4)*(spp-1)+(sectorsize-4*(spp-1))-blockbuf);
    fwrite(extbuf,1,oobsize,out);
  }  
}

return !okflag; 
} 


//*************************************************************
//* Reading a data block for non-file Linux partitions
//*************************************************************
// */
unsigned int read_block_resequence(int block, FILE* out) {
 return read_block_ext(block,out,0);
} 

//*************************************************************
//* Reading a data block for file yaffs2 partitions
//*************************************************************
// */
unsigned int read_block_yaffs(int block, FILE* out) {
 return read_block_ext(block,out,1);
} 

//****************************************
//* Displays a list of read errors
//****************************************
// */
void show_errlist() {
  
int i;  
  
if (qflag || (errcount == 0)) return; // there were no mistakes
for (i=0;i<errcount;i++) {
  if (errlist[i].errcode == -1) printf("\n!   Page %d sector %d: uncorrectable read error",
                                   errlist[i].page,errlist[i].sector);
  else                          printf("\n!   Page %d sector %d: bit corrected: %d",
                                   errlist[i].page,errlist[i].sector,errlist[i].errcode);
}
printf("\n");
}

//*****************************
//* reading raw flash
//*****************************
// */
void read_raw(int start,int len,int cwsize,FILE* out, unsigned int rflag) {
  
int block;  
unsigned int badflag;

printf("\n Reading blocks %08x - %08x",start,start+len-1);
printf("\n Data format: %u+%i\n",sectorsize,cwsize-sectorsize);
// main cycle
// by blocks
for (block=start;block<(start+len);block++) {
  printf("\r Block: %08x",block); fflush(stdout);
  switch (rflag) {
    case RF_AUTO:
    case RF_STANDART:
       badflag=read_block(block,cwsize,out);
       break;
      
    case RF_LINUX:   
       badflag=read_block_resequence(block,out); 
       break;
       
    case RF_YAFFS:
       badflag=read_block_yaffs(block,out); 
       break;
  }  
  show_errlist(); 
  if (badflag != 0) printf(" - Badblock\n");   
} 
printf("\n"); 
}


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
void main(int argc, char* argv[]) {
  
unsigned char filename[300]={0};
unsigned int i,block,filepos,lastpos;
unsigned char c;
unsigned int start=0,len=0,opt;
unsigned int partlist[60]; // list of sections allowed to read
unsigned int cwsize;  // portion size of data read from the sector buffer at a time
FILE* out;
int partflag=0;  // 0 - raw flash, 1 - work with partition table
int eccflag=0;  // 1 - disable ECC, 0 - enable
int partnumber=-1; // flag selected section for reading, -1 - all sections, 1 - according to the list
int rflag=RF_AUTO;     // format of sections: 0 - auto, 1 - standard, 2 - Linux Chinese, 3 - yaffs2
int listmode=0;    // 1- partition map output
int truncflag=0;  //  1 - cut off all FF from the end of the section
int xflag=0;      // 
unsigned int badflag;

int forced_oobsize=-1;

// Partition table source. By default - the MIBIB section on the USB flash drive
char ptable_file[200]="@";

#ifndef WIN32
char devname[50]="/dev/ttyUSB0";
#else
char devname[50]="";
#endif

memset(partlist,0,sizeof(partlist)); // clear the list of sections allowed for reading

while ((opt = getopt(argc, argv, "hp:b:l:o:xs:ef:mtk:r:z:u:q")) != -1) {
  switch (opt) {
   case 'h': 
     
printf("\n The utility is designed to read a flash image through a modified bootloader\n\
 The following keys are valid.:\n\n\
-p <tty> - serial port for communication with the bootloader\n\
-e - disable ECC correction while reading\n\
-x - read the full sector - data + oob (without a key - only data)\n\n\
-k # - chipset code (-kl - get a list of codes)\n\
-z # - OOB size per page, in bytes (overrides auto-detected size)\n\
-q   - disable reading error list\n\
-u <x> - determines the processing mode of defective blocks:\n\
   -uf - fill the image of the defective block in the output file with byte 0xbb\n\
   -us - skip defective blocks while reading\n\
   -ui - ignore defective block marker (read as read)\n\
   -ux - disable hardware control of defective blocks\n");
printf("\n * For unformatted reading and checking of bad blocks:\n\
-b <blk> - starting block number to read (default 0)\n\
-l <num> - the number of readable blocks, 0 - to the end of the flash drive\n\
-o <file> - output file name (qflash.bin by default) (read-only)\n\n\
 * For section reading mode:\n\n\
-s <file> - take partition map from file\n\
-s @ - take partition map from flash (default for -f and -m)\n\
-s - - take partition map from file ptable/current-r.bin\n\
-m   - display a full partition map and exit\n\
-f n - read only the section with the number n (can be specified several times to create a list of sections)\n\
-f * - читать все разделы\n\
-t - cut off all FF after the last significant byte of the section\n\
-r <x> - data format:\n\
    -ra - (by default and only for partition mode) auto-detect format by section attribute\n\
    -rs - standard format (512-byte blocks)\n\
    -rl - Linux format (516-byte blocks, except the last on the page)\n\
    -ry - yaffs2 format (page + tag)\n\
");
    return;
    
   case 'k':
    define_chipset(optarg);
    break;
    
   case 'p':
    strcpy(devname,optarg);
    break;

   case 'e':
     eccflag=1;
     break;
    
   case 'o':
    strcpy(filename,optarg);
    break;
    
   case 'b':
     sscanf(optarg,"%x",&start);
     break;

   case 'l':
     sscanf(optarg,"%x",&len);
     break;

   case 'z':
     sscanf(optarg,"%u",&forced_oobsize);
     break;

   case 'u':
     if (bad_processing_flag != BAD_UNDEF) {
       printf("\n Duplicated key u - error\n");
       return;
     }  
     switch(*optarg) {
       case 'f':
	 bad_processing_flag=BAD_FILL;
	 break; 
       case 'i':
	 bad_processing_flag=BAD_IGNORE;
	 break; 
       case 's':
	 bad_processing_flag=BAD_SKIP;
	 break; 
       case 'x':
	 bad_processing_flag=BAD_DISABLE;
	 break;
       default:
	 printf("\n Invalid key value u\n");
	 return;
     } 
     break;
	 

   case 'r':
     switch(*optarg) {
       case 'a':
	 rflag=RF_AUTO;   // auto
	 break;     
       case 's':
	 rflag=RF_STANDART;   // standard
	 break;
       case 'l':
	 rflag=RF_LINUX;   // Linux
	 break;
       case 'y':
         rflag=RF_YAFFS;
         break;	 
       default:
	 printf("\n Invalid key value r\n");
	 return;
     } 
     break;
     
   case 'x':
     xflag=1;
     break;
     
   case 's':
     partflag=1;
     strcpy(ptable_file,optarg);
     break;
     
   case 'm':
     partflag=1;  // force partition mode
     listmode=1;
     break;
     
   case 'f':
     partflag=1; // force partition mode
     if (optarg[0] == '*') {
       // все разделы
       partnumber=-1;
       break;
     }
     // building a list of sections
     partnumber=1;
     sscanf(optarg,"%u",&i);
     partlist[i]=1;
     break;
     
   case 't':
     truncflag=1;
     break;

   case 'q':
     qflag=1;
     break;
     
   case '?':
   case ':':  
     return;
  }
}  

// Checking for start without keys
if ((start == 0) && (len == 0) && !xflag && !partflag) {
  printf("\n No mode key specified\n");
  return;
}  

// Define the default -u switch
if (bad_processing_flag==BAD_UNDEF) {
  if (partflag == 0) bad_processing_flag=BAD_FILL; // to read a range of blocks
  else bad_processing_flag=BAD_SKIP;               // for reading sections
}  

if ((truncflag == 1)&&(xflag == 1)) {
  printf("\nThe -t and -x keys are incompatible\n");
  return;
}  


// Configuring port and flash drive parameters
// In the mode of outputting the partition map from the file, all this setting is skipped
//----------------------------------------------------------------------------
//*/
if (! (listmode && ptable_file[0] != '@')) {

#ifdef WIN32
 if (*devname == '\0') {
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
// load flash drive parameters
hello(0);
// allocate memory for block buffer
blockbuf=(unsigned char*)malloc(300000);

if (forced_oobsize != -1) {
  printf("\n! OOB %d size is used instead of %d\n",forced_oobsize,oobsize);
  oobsize=forced_oobsize;
}  

cwsize=sectorsize;
if (xflag) cwsize+=oobsize/spp; // increment codeword size by OOB portion size per sector
mempoke(nand_ecc_cfg,mempeek(nand_ecc_cfg)&0xfffffffe|eccflag); // ECC on/off
mempoke(nand_cfg1,mempeek(nand_cfg1)&0xfffffffe|eccflag); // ECC on/off

mempoke(nand_cmd,1); // Reset all controller operations
mempoke(nand_exec,0x1);
nandwait();
  
// set the command code
mempoke(nand_cmd,0x34); // reading data + ecc + spare

// clean sector buffer
for(i=0;i<cwsize;i+=4) mempoke(sector_buf+i,0xffffffff);
}


//###################################################
// Raw Flash Read Mode
//###################################################
// */

if (partflag == 0) {

if (len == 0) len=maxblock-start; //  to the end of the flash drive
  if (filename[0] == 0) {
    switch(rflag) {
      case RF_AUTO:
      case RF_STANDART:
	strcpy(filename,"qrflash.bin");
	break;
      case RF_LINUX:
        strcpy(filename,"qrflash.oob");
        break;
      case RF_YAFFS:
        strcpy(filename,"qrflash.yaffs");
        break;
    }
  } 
  out=fopen(filename,"wb");
  if (out == 0) {
    printf("\n Error opening output file %s",filename);
    return;
  }  
  read_raw(start,len,cwsize,out,rflag);
  fclose(out);
  return;
}  


//###################################################
// Partition table read mode
//###################################################
// */

// load the partition table
if (!load_ptable(ptable_file)) { 
    printf("\n Partition table not found. We are finishing work.\n");
    return;
}

// check if the table has loaded
if (!validpart) {
   printf("\nPartition table not found or damaged\n");
   return;
}

// Partition Table View
if (listmode) {
  list_ptable();
  return;
}  

if ((partnumber != -1) && (partnumber>=fptable.numparts)) {
  printf("\nInvalid partition number: %i, total partitions %u\n",partnumber,fptable.numparts);
  return;
}  

print_ptable_head();

// Main cycle - by sections
for(i=0;i<fptable.numparts;i++) {

  // We read the section
 
  // If the mode of all partitions is not set, check if this particular partition is selected
  if ((partnumber == -1) || (partlist[i]==1)) { 
  // Display a description of the section
  show_part(i);
  // form the file name depending on the format
  if (rflag == RF_YAFFS) sprintf(filename,"%02u-%s.yaffs2",i,part_name(i)); 
  else if (cwsize == sectorsize) sprintf(filename,"%02u-%s.bin",i,part_name(i)); 
  else                   sprintf(filename,"%02u-%s.oob",i,part_name(i));  
  // replace the colon with a minus in the file name
  if (filename[4] == ':') filename[4]='-';
  // open the output file
  out=fopen(filename,"wb");  
  if (out == 0) {
	  printf("\n Error opening output file %s\n",filename);
	  return;
  }
  // Section Block Cycle
  for(block=part_start(i); block < (part_start(i)+part_len(i)); block++) {
          printf("\r * R: block %06x [start+%03x] (%i%%)",block,block-part_start(i),(block-part_start(i)+1)*100/part_len(i)); 
	  fflush(stdout);
	  
    //	  Actually reading a block
  switch (rflag) {
    case RF_AUTO: // auto format selection
      if ((fptable.part[i].attr2 != 1)||(cwsize>(sectorsize+4))) 
         // raw reading or reading unrooted sections
         badflag=read_block(block,cwsize,out);
      else 
	 // reading Chinese-Linux sections
	 badflag=read_block_resequence(block,out);
      break;
	       
    case RF_STANDART: // standard format
      badflag=read_block(block,cwsize,out);
      break;
	      
    case RF_LINUX: // sino-linux format 
      badflag=read_block_resequence(block,out);
      break;
	      
    case RF_YAFFS: // file partition image
      badflag=read_block_yaffs(block,out);
      break;
  }
  // display a list of errors found
  show_errlist(); 
  if (badflag != 0) {
      printf(" - defective block");
      if (bad_processing_flag == BAD_SKIP) printf (", skipping");
      if (bad_processing_flag == BAD_IGNORE) printf (", read as is");
      if (bad_processing_flag == BAD_FILL) printf (", mark in the output file");
      printf("\n");
  }  
}    // block end of cycle

  fclose(out);
// Trim all FF tail
  if (truncflag) {
      out=fopen(filename,"r+b");  // rediscover the output file
      fseek(out,0,SEEK_SET);  // rewind the file to the beginning
      lastpos=0;
      for(filepos=0;;filepos++) {
	c=fgetc(out);
	if (feof(out)) break;
	if (c != 0xff) lastpos=filepos;  // found a significant byte
      }
#ifndef WIN32
       ftruncate(fileno(out),lastpos+1);   // crop file
#else
       _chsize(_fileno(out),lastpos+1);   // crop file
#endif
      fclose(out);
  }	
 }  // section selection check
}   // section cycle

// restore ECC
mempoke(nand_ecc_cfg,mempeek(nand_ecc_cfg)&0xfffffffe); // ECC on BCH
mempoke(nand_cfg1,mempeek(nand_cfg1)&0xfffffffe); // ECC on R-S

printf("\n"); 
    
} 
