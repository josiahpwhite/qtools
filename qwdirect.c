#include "include.h"


//**********************************************************
//*   Setting the chipset to the Linux format of the flash drive partition
//**********************************************************
// */
void set_linux_format() {
  
unsigned int sparnum, cfgecctemp;

if (nand_ecc_cfg != 0xffff) cfgecctemp=mempeek(nand_ecc_cfg);
else cfgecctemp=0;
sparnum = 6-((((cfgecctemp>>4)&3)?(((cfgecctemp>>4)&3)+1)*4:4)>>1);
// For ECC- R-S
if (! (is_chipset("MDM9x25") || is_chipset("MDM9x3x") || is_chipset("MDM9x4x"))) set_blocksize(516,1,10); // data - 516, spare - 1 byte, ecc - 10
// For ECC - BCH
else {
	set_udsize(516); // data - 516, spare - 2 или 4 байта
	set_sparesize(sparnum);
}
}  
  

//*******************************************
//@@@@@@@@@@@@ Head program
// */
void main(int argc, char* argv[]) {
  

			     
unsigned char datacmd[1024]; // sector buffer
			     
unsigned char srcbuf[8192]; // page buffer
unsigned char membuf[1024]; // verification buffer
unsigned char databuf[8192], oobuf[8192]; // data sector and OOB buffers
unsigned int fsize;
FILE* in;
int vflag=0;
int cflag=0;
unsigned int flen=0;
#ifndef WIN32
char devname[]="/dev/ttyUSB0";
#else
char devname[20]="";
#endif
unsigned int cfg0bak,cfg1bak,cfgeccbak,cfgecctemp;
unsigned int i,opt;
unsigned int block,page,sector;
unsigned int startblock=0;
unsigned int bsize;
unsigned int fileoffset=0;
int badflag;
int uxflag=0, ucflag=0, usflag=0, umflag=0, ubflag=0;
int wmode=0; // recording mode
int readlen;

#define w_standart 0
#define w_linux    1
#define w_yaffs    2
#define w_image    3
#define w_linout   4

while ((opt = getopt(argc, argv, "hp:k:b:f:vc:z:l:o:u:")) != -1) {
  switch (opt) {
   case 'h': 
    printf("\n  The utility is designed to write a raw flash image through the controller registers\n\
The following keys are valid:\n\n\
-p <tty>  - indicates the name of the serial port device for communication with the bootloader\n\
-k #      - chipset code (-kl - get a list of codes)\n\
-b #      - starting block number for writing \n\
-f <x>    - record format selection:\n\
        -fs (default) - record only data sectors\n\
        -fl - write only Linux data sectors\n\
        -fy - record yaffs2-images\n\
	-fi - raw image recording + OOB data, as is, without ECC recount\n\
	-fo - at the input - only data, on a flash drive - Linux format\n");
printf("\
-z #      - OOB size per page, in bytes (overrides auto-determined size)\n\
-l #      - number of writable blocks, by default - to the end of the input file\n\
-o #      - offset in blocks in the source file before the beginning of the recorded section\n\
-ux       - disable hardware control of defective blocks\n\
-us       - ignore bad block signs noted in input file\n\
-uc       - simulate defective input file blocks\n\
-um       - check for defective file blocks and flash drives\n\
-ub       - Do not check for defective flash drive blocks before recording (DANGER!)\n\
-v        - verification of recorded data after recording\n\
-c n      - only erase n (hex) blocks, starting from the initial one.\n\
\n");
    return;
    
   case 'k':
    define_chipset(optarg);
    break;

   case 'p':
    strcpy(devname,optarg);
    break;
    
   case 'c':
     sscanf(optarg,"%x",&cflag);
     if (cflag == 0) {
       printf("\n Invalid key argument -с");
       return;
     }  
     break;
     
   case 'b':
     sscanf(optarg,"%x",&startblock);
     break;
     
   case 'z':
     sscanf(optarg,"%u",&oobsize);
     break;
     
   case 'l':
     sscanf(optarg,"%x",&flen);
     break;
     
   case 'o':
     sscanf(optarg,"%x",&fileoffset);
     break;
     
   case 'v':
     vflag=1;
     break;
     
   case 'f':
     switch(*optarg) {
       case 's':
        wmode=w_standart;
	break;
	
       case 'l':
        wmode=w_linux;
	break;
	
       case 'y':
        wmode=w_yaffs;
	break;
	
       case 'i':
        wmode=w_image;
	break;

       case 'o':
        wmode=w_linout;
	break;
	
       default:
	printf("\n Invalid key value -f\n");
	return;
     }
     break;
     
   case 'u':  
     switch (*optarg) {
       case 'x':
         uxflag=1;
	 break;
	 
       case 's':
         usflag=1;
	 break;
	 
       case 'c':
         ucflag=1;
	 break;
	 
       case 'm':
         umflag=1;
	 break;
	 
       case 'b':
         ubflag=1;
	 break;
	 
       default:
	printf("\n Invalid key value -u\n");
	return;
     }
     break;
     
   case '?':
   case ':':  
     return;
  }
}  

if (uxflag+usflag+ucflag+umflag > 1) {
  printf("\n The -ux, -us, -uc, -um keys are incompatible\n");
  return;
}  

if (uxflag+ubflag > 1) {
  printf("\n The -ux and -ub switches are incompatible\n");
  return;
}  

if (uxflag && (wmode != w_image)) {
  printf("\n The -ux switch is valid only in -fi mode\n");
  return;
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

if (!cflag) { 
 in=fopen(argv[optind],"rb");
 if (in == 0) {
   printf("\nError opening input file\n");
   return;
 }
 
}
else if (optind < argc) {// in erase mode, the input file is not needed
  printf("\n The input file is not allowed with the -c key\n");
  return;
}

hello(0);


if ((wmode == w_standart)||(wmode == w_linux)) oobsize=0; // for input files without OOB
oobsize/=spp;   // now oobsize is the size of the OOB per sector

// Reset nand controller
nand_reset();

// Saving values of controller registers
cfg0bak=mempeek(nand_cfg0);
cfg1bak=mempeek(nand_cfg1);
cfgeccbak=mempeek(nand_ecc_cfg);

//-------------------------------------------
// erase mode
//-------------------------------------------
// */
if (cflag) {
  if ((startblock+cflag) > maxblock) cflag=maxblock-startblock;
  printf("\n");
  for (block=startblock;block<(startblock+cflag);block++) {
    printf("\r Block erase %03x",block); 
    if (!ubflag) 
      if (check_block(block)) {
	printf(" - badblock, erasing is prohibited\n");
	continue; 
      }	
    block_erase(block);
  }  
  printf("\n");
  return;
}

//ECC on-off
if (wmode != w_image) {
  mempoke(nand_ecc_cfg,mempeek(nand_ecc_cfg)&0xfffffffe); 
  mempoke(nand_cfg1,mempeek(nand_cfg1)&0xfffffffe); 
}
else {
  mempoke(nand_ecc_cfg,mempeek(nand_ecc_cfg)|1); 
  mempoke(nand_cfg1,mempeek(nand_cfg1)|1); 
}
  
// Determine the file size
if (wmode == w_linout) bsize=pagesize*ppb; // for this mode, the file does not contain OOB data, but it needs to be written to OOB
else bsize=(pagesize+oobsize*spp)*ppb;  // size in bytes of the full flash drive block, including OOB
fileoffset*=bsize; // translate the offset from blocks to bytes
fseek(in,0,SEEK_END);
i=ftell(in);
if (i<=fileoffset) {
  printf("\n Offset %i goes beyond the file boundary\n",fileoffset/bsize);
  return;
}
i-=fileoffset; // cut off the size of the skipped section from the file length
fseek(in,fileoffset,SEEK_SET); // stand at the beginning of the recorded section
fsize=i/bsize; // block size
if ((i%bsize) != 0) fsize++; // round up to the border of the block

if (flen == 0) flen=fsize;
else if (flen>fsize) {
  printf("\n The specified length %u exceeds the file size %u\n",flen,fsize);
  return;
} 
  
printf("\n Entry from file %s, start block %03x, size %03x\n Recording mode: ",argv[optind],startblock,flen);


switch (wmode) {
  case w_standart:
    printf("data only, standard format\n");
    break;
    
  case w_linux: 
    printf("only data, Linux input format\n");
    break;
    
  case w_image: 
    printf("raw image without ECC calculation\n");
	printf(" Data format: %u+%u\n",sectorsize,oobsize); 
    break;
    
  case w_yaffs: 
    printf("form yaffs2\n");
    set_linux_format();
    break;

  case w_linout: 
     printf("Linux format on a flash drive\n");
    set_linux_format();
    break;
}   
    
port_timeout(1000);

// block cycle
if ((startblock+flen) > maxblock) flen=maxblock-startblock;
for(block=startblock;block<(startblock+flen);block++) {
  // check if necessary, defective block
  badflag=0;
  if (!uxflag && !ubflag)  badflag=check_block(block);
  // target block is defective
  if (badflag) {
//    printf("\n %x - badflag\n",block);
    // skip the defective block and move on
    if (!umflag && !ubflag) {
      flen++;   // we shift the boundary of completion of the input file - we skipped the block, the data is moved apart
      printf("\n Block %x defective - skip\n",block);
      continue;
    }  
  }  
  // we erase the block
  if (!badflag || ubflag) {
    block_erase(block);
  }  
              
  bch_reset();

  // page loop
  for(page=0;page<ppb;page++) {

    memset(oobuf,0xff,sizeof(oobuf));
    memset(srcbuf,0xff,pagesize); // fill the FF buffer for reading incomplete pages
    // read the whole page dump
    if (wmode == w_linout) readlen=fread(srcbuf,1,pagesize,in);
    else readlen=fread(srcbuf,1,pagesize+(spp*oobsize),in);
    if (readlen == 0) goto endpage;  // 0 - all data from the file is read
 
    // srcbuf read - check if the badblock is there
    if (test_badpattern(srcbuf)) {
      // there really is a badblock
      if (!usflag) {
	if (page == 0) printf("\n A sign of a defective block in the input dump is detected - skip\n");
	continue;  // -us - skip this block, page by page
      }
      if (ucflag) {
	// creating a badblock
	mark_bad(block);
	if (page == 0) printf("\r Block %x marked as defective according to input file!\n",block);
	continue;
      }
      if (umflag && !badflag) {
	// the input badblock does not match the badblock on the flash drive
	printf("\n Block %x: no flash defect detected, shut down!\n",block);
	return;
      }
      if (umflag && badflag && page == 0) printf("\r Block %x - defects correspond, continue recording\n",block);
    }
    else if (umflag && badflag) {
	printf("\n Block %x: an unexpected defect was detected on flash, shut down!\n",block);
	return;
    }
      
    // parsing the dump into buffers
    switch (wmode) {
      case w_standart:
      case w_linux:
      case w_image:
      // for all modes except yaffs and linout - input file format 512 + obb
      for (i=0;i<spp;i++) {
		memcpy(databuf+sectorsize*i,srcbuf+(sectorsize+oobsize)*i,sectorsize);
		if (oobsize != 0) memcpy(oobuf+oobsize*i,srcbuf+(sectorsize+oobsize)*i+sectorsize,oobsize);
     }  
	break;
	 
      case w_yaffs:
      // for yaffs mode - pagesize + obb input file format
		memcpy(databuf,srcbuf,sectorsize*spp);
		memcpy(oobuf,srcbuf+sectorsize*spp,oobsize*spp);
      break;

      case w_linout:
      // for this mode - in the input file only data with the size of pagesize
		memcpy(databuf,srcbuf,pagesize);
	break;
    }
    
    // set the address of the flash drive
    printf("\r Block: %04x Page: %02x",block,page); fflush(stdout);
    setaddr(block,page);

    // set the write command code
    switch (wmode) {
	case w_standart:
	mempoke(nand_cmd,0x36); // page program - we write only the body of the block
    break;

	case w_linux:
	case w_yaffs:
	case w_linout:
        mempoke(nand_cmd,0x39); // data + spare record, ECC is calculated by the controller
    break;
	 
	case w_image:
        mempoke(nand_cmd,0x39); // record data + spare + ecc, all data from the buffer goes directly to the USB flash drive
    break;
    }

    // sector cycle
    for(sector=0;sector<spp;sector++) {
      memset(datacmd,0xff,sectorsize+64); // fill the sector buffer FF - default values
      
      // fill the sector buffer with data
      switch (wmode) {
        case w_linux:
	// Linux (Chinese perverted) version of the data layout, recording without OOB
          if (sector < (spp-1))  
	 //first n sectors
             memcpy(datacmd,databuf+sector*(sectorsize+4),sectorsize+4); 
          else 
	 // last sector
             memcpy(datacmd,databuf+(spp-1)*(sectorsize+4),sectorsize-4*(spp-1)); // data of the last sector - we shorten
	  break;
	  
        case w_standart:
	 // standard format - only sectors of 512 bytes, without OOB
          memcpy(datacmd,databuf+sector*sectorsize,sectorsize); 
	  break;
	  
	case w_image:
	 // raw image - data + oob, ECC not calculated
          memcpy(datacmd,databuf+sector*sectorsize,sectorsize);       // data
          memcpy(datacmd+sectorsize,oobuf+sector*oobsize,oobsize);    // oob
	  break;

	case w_yaffs:
	 // yaffs image - write only data in 516-byte blocks
	 //  and yaffs tag at the end of the last block
	 // the input file has the format page + oob, but the tag lies at position 0 OOB
          if (sector < (spp-1))  
	 //first n sectors
             memcpy(datacmd,databuf+sector*(sectorsize+4),sectorsize+4); 
          else  {
	 // last sector
             memcpy(datacmd,databuf+(spp-1)*(sectorsize+4),sectorsize-4*(spp-1)); // data of the last sector - we shorten
             memcpy(datacmd+sectorsize-4*(spp-1),oobuf,16 );    // the yaffs tag is attached to it
		  }
	  break;

	case w_linout:
	 // write only data with 516-byte blocks
          if (sector < (spp-1))  
	 //first n sectors
             memcpy(datacmd,databuf+sector*(sectorsize+4),sectorsize+4); 
          else  {
	 // last sector
             memcpy(datacmd,databuf+(spp-1)*(sectorsize+4),sectorsize-4*(spp-1)); // data of the last sector - we shorten
		  }
	  break;

      }
      // send sector to sector buffer
	  if (!memwrite(sector_buf,datacmd,sectorsize+oobsize)) {
		printf("\n Sector Buffer Transfer Error\n");
		return;
      }	
      // if necessary, disable the control of the bad blocks
      if (uxflag) hardware_bad_off();
      // execute the write command and wait for it to complete
      mempoke(nand_exec,0x1);
      nandwait(); 
      // turn back on the control of the bad blocks
      if (uxflag) hardware_bad_on();
     }  // end of recording cycle by sector
     if (!vflag) continue;   // verification not required
    // data verification
     printf("\r");
     setaddr(block,page);
     mempoke(nand_cmd,0x34); // reading data + ecc + spare
     
     // sector verification cycle
     for(sector=0;sector<spp;sector++) {
      // read the next sector
      mempoke(nand_exec,0x1);
      nandwait();
      
      // read sector buffer
      memread(membuf,sector_buf,sectorsize+oobsize);
      switch (wmode) {
        case w_linux:
 	// Linux verification
	  if (sector != (spp-1)) {
	    // all sectors except the last
	    for (i=0;i<sectorsize+4;i++) 
	      if (membuf[i] != databuf[sector*(sectorsize+4)+i])
                 printf("! block: %04x  page:%02x  sector:%u  byte: %03x  %02x != %02x\n",
			block,page,sector,i,membuf[i],databuf[sector*(sectorsize+4)+i]); 
	  }  
	  else {
	      // last sector
	    for (i=0;i<sectorsize-4*(spp-1);i++) 
	      if (membuf[i] != databuf[(spp-1)*(sectorsize+4)+i])
                 printf("! block: %04x  page:%02x  sector:%u  byte: %03x  %02x != %02x\n",
			block,page,sector,i,membuf[i],databuf[(spp-1)*(sectorsize+4)+i]); 
	  }    
	  break; 
	  
		 case w_standart:
	     case w_image:  
         case w_yaffs:  
	     case w_linout: // not working yet!
          // standard format verification
	  for (i=0;i<sectorsize;i++) 
	      if (membuf[i] != databuf[sector*sectorsize+i])
                 printf("! block: %04x  page:%02x  sector:%u  byte: %03x  %02x != %02x\n",
			block,page,sector,i,membuf[i],databuf[sector*sectorsize+i]); 
	  break;   
      }  // switch(wmode)
    }  // end of sector verification cycle
  }  // end of page loop
} // block end of cycle
endpage:  
mempoke(nand_cfg0,cfg0bak);
mempoke(nand_cfg1,cfg1bak);
mempoke(nand_ecc_cfg,cfgeccbak);
printf("\n");
}

