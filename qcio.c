// //
//  Drivers for working with the Flash modem through calls to the NAND controller and bootloader procedures
//
#include "include.h"

// Global variables - collect them here

unsigned int nand_cmd=0x1b400000;
unsigned int spp=0;
unsigned int pagesize=0;
unsigned int sectorsize=512;
unsigned int maxblock=0;     // The total number of flash drive blocks
char flash_mfr[30]={0};
char flash_descr[30]={0};
unsigned int oobsize=0;
unsigned int bad_loader=0;
unsigned int flash16bit=0; // 0 - 8-bit flash drive, 1 - 16-bit

unsigned int badsector;    // defective block sector
unsigned int badflag;      // defective block marker
unsigned int badposition;  // defective block marker position
unsigned int badplace;     // marker location: 0-user, 1-spare
int bch_mode=0;            // ECC mode: 0 = R-S 1 = BCH
int ecc_size;              // ECC size
int ecc_bit;               // number of bits adjusted by ECC

//****************************************************************
//* Waiting for nand to complete an operation  *
//****************************************************************
//*/
void nandwait() { 
   if (get_controller() == 0) 
     while ((mempeek(nand_status)&0xf) != 0);  // MDM
   else
     while ((mempeek(nand_status)&0x3) != 0);  // MSM
}



//*************************************88
//* Setting Address Registers
//*************************************
//*/
void setaddr(int block, int page) {

int adr;  
  
adr=block*ppb+page;  // # pages from the beginning of the flash drive

if (get_controller() == 0) {
  // MDM
  mempoke(nand_addr0,adr<<16);         // the lower part of the address. 16 bit column address is 0
  mempoke(nand_addr1,(adr>>16)&0xff);  // single byte of the high part of the address
}  
else {
  // MSM
  mempoke(nand_addr0,adr<<8);
}
}

//***************************************************************
//* Run NAND controller command to wait
//***************************************************************
//*/
void exec_nand(int cmd) {

if (get_controller() == 0) {
  // MDM  
  mempoke(nand_cmd,cmd); // Reset all controller operations
  mempoke(nand_exec,0x1);
  nandwait();
}
else {
  // MSM
  mempoke(nand_cmd,cmd); // Reset all controller operations
  nandwait();
}
}



//*********************************************
//* Reset NAND Controller
//*********************************************
//*/
void nand_reset() {

exec_nand(1);  
}

//*********************************************
//* Read the sector of the flash drive at the specified address
//*********************************************
//*/

int flash_read(int block, int page, int sect) {
  
int i;

nand_reset();
// address
setaddr(block,page);
if (get_controller() == 0) {
  // MDM - set the command code once
  mempoke(nand_cmd,0x34); // reading data + ecc + spare
  // cycle of reading sectra to the desired one
  for(i=0;i<(sect+1);i++) {
    mempoke(nand_exec,0x1);
    nandwait();
  }  
}
else {
  // MSM - the command code is entered into the command register every time
  for(i=0;i<(sect+1);i++) {
    mempoke(nand_cmd,0x34); // reading data + ecc + spare
    nandwait();
  }  
}  
if (test_badblock()) return 0;
return 1;  
}


//**********************************************8
//* Hello bootloader activation procedure
//*
//* mode = 0 - hello auto-detection
//* mode = 1 - force start hello
//* mode = 2 - force hello to start without configuration
//**********************************************8
//*/
void hello(int mode) {

int i;  
unsigned char rbuf[1024];
char hellocmd[]="\x01QCOM fast download protocol host\x03### ";


// bootloader health applet
unsigned char cmdbuf[]={
  0x11,0x00,0x12,0x00,0xa0,0xe3,0x00,0x00,
  0xc1,0xe5,0x01,0x40,0xa0,0xe3,0x1e,0xff,
  0x2f,0xe1
};
unsigned int cfg0;

// silent initialization mode // translators note: auto maybe?
if (mode == 0) {
  i=send_cmd(cmdbuf,sizeof(cmdbuf),rbuf);
  ttyflush(); 
  i=rbuf[1];
  // Check if the bootloader was initialized earlier
  if (i == 0x12) {
     if (!test_loader()) {
       printf("\n An unpatched bootloader is used - it is impossible to continue working\n");
        exit(1);
     }  
//     printf("\n chipset = %i  base = %i",chip_type,name);
     get_flash_config();
     return;
  }  
  read(siofd,rbuf,1024);   // we clean the tail of the buffer with an error message
}  

i=send_cmd(hellocmd,strlen(hellocmd),rbuf);
if (rbuf[1] != 2) {
   printf(" Sending hello...");
   i=send_cmd(hellocmd,strlen(hellocmd),rbuf);
   if (rbuf[1] != 2) {
     printf(" repeated hello returned an error!\n");
     dump(rbuf,i,0);
     return;
   }  
   printf("ok");
}  
i=rbuf[0x2c];
rbuf[0x2d+i]=0;
if (mode == 2) {
   // silent start - bypass chipset setup
   printf("Hello ok, flash memory: %s\n",rbuf+0x2d);
   return; 
 }  
ttyflush(); 
if (!test_loader()) {
  printf("\n An unpatched bootloader is used - it is impossible to continue working\n");
  exit(1);
}  

if (get_sahara()) disable_bam(); // disable NANDc BAM if we work with new generation chipsets

get_flash_config();
cfg0=mempeek(nand_cfg0);
printf("\n HELLO Protocol Version: %i",rbuf[0x22]); 
printf("\n Chipset: %s",get_chipname()); 
printf("\n Base address of the NAND controller: %08x",nand_cmd);
printf("\n Flash memory: %s %s, %s",flash_mfr,(rbuf[0x2d] != 0x65)?((char*)(rbuf+0x2d)):"",flash_descr);
//printf("\n Max packet size: %i bytes",*((unsigned int*)&rbuf[0x24]));
printf("\n Sector Size: %u byte",(cfg0&(0x3ff<<9))>>9);
printf("\n Page Size: %u bytes (%u sectors)",pagesize,spp);
printf("\n The number of pages in the block: %u",ppb);
printf("\n OOB Size: %u byte",oobsize); 
printf("\n ECC Type: %s, %i bit",bch_mode?"BCH":"R-S",ecc_bit);
printf("\n Размер ЕСС: %u byte",ecc_size);
printf("\n Размер spare: %u byte",(cfg0>>23)&0xf);
printf("\n Bad block marker position: ");
printf("%s+%x",badplace?"spare":"user",badposition);
printf("\n Total flash size = %u blocks (%i MB)",maxblock,maxblock*ppb/1024*pagesize/1024);
printf("\n");
}

//**********************************************************
//*  Getting flash drive format parameters from the controller
//**********************************************************
//*/
void get_flash_config() {
  
unsigned int cfg0, cfg1, nandid, pid, fid, blocksize, devcfg, chipsize;
unsigned int ecccfg;
int linuxcwsize;
int i;
int c_badmark_pos; // computed marker position

struct {
  char* type;   // text description of type
  unsigned int id;      // Flash drive ID
  unsigned int chipsize; // flash drive size in megabytes
} nand_ids[]= {

	{"NAND 16MiB 1,8V 8-bit",	0x33, 16},
	{"NAND 16MiB 3,3V 8-bit",	0x73, 16}, 
	{"NAND 16MiB 1,8V 16-bit",	0x43, 16}, 
	{"NAND 16MiB 3,3V 16-bit",	0x53, 16}, 

	{"NAND 32MiB 1,8V 8-bit",	0x35, 32},
	{"NAND 32MiB 3,3V 8-bit",	0x75, 32},
	{"NAND 32MiB 1,8V 16-bit",	0x45, 32},
	{"NAND 32MiB 3,3V 16-bit",	0x55, 32},

	{"NAND 64MiB 1,8V 8-bit",	0x36, 64},
	{"NAND 64MiB 3,3V 8-bit",	0x76, 64},
	{"NAND 64MiB 1,8V 16-bit",	0x46, 64},
	{"NAND 64MiB 3,3V 16-bit",	0x56, 64},

	{"NAND 128MiB 1,8V 8-bit",	0x78, 128},
	{"NAND 128MiB 1,8V 8-bit",	0x39, 128},
	{"NAND 128MiB 3,3V 8-bit",	0x79, 128},
	{"NAND 128MiB 1,8V 16-bit",	0x72, 128},
	{"NAND 128MiB 1,8V 16-bit",	0x49, 128},
	{"NAND 128MiB 3,3V 16-bit",	0x74, 128},
	{"NAND 128MiB 3,3V 16-bit",	0x59, 128},

	{"NAND 256MiB 3,3V 8-bit",	0x71, 256},

	/*512 Megabit */
	{"NAND 64MiB 1,8V 8-bit",	0xA2, 64},   
	{"NAND 64MiB 1,8V 8-bit",	0xA0, 64},
	{"NAND 64MiB 3,3V 8-bit",	0xF2, 64},
	{"NAND 64MiB 3,3V 8-bit",	0xD0, 64},
	{"NAND 64MiB 1,8V 16-bit",	0xB2, 64},
	{"NAND 64MiB 1,8V 16-bit",	0xB0, 64},
	{"NAND 64MiB 3,3V 16-bit",	0xC2, 64},
	{"NAND 64MiB 3,3V 16-bit",	0xC0, 64},

	/* 1 Gigabit */
	{"NAND 128MiB 1,8V 8-bit",	0xA1,128},
	{"NAND 128MiB 3,3V 8-bit",	0xF1,128},
	{"NAND 128MiB 3,3V 8-bit",	0xD1,128},
	{"NAND 128MiB 1,8V 16-bit",	0xB1,128},
	{"NAND 128MiB 3,3V 16-bit",	0xC1,128},
	{"NAND 128MiB 1,8V 16-bit",     0xAD,128},

	/* 2 Gigabit */
	{"NAND 256MiB 1.8V 8-bit",	0xAA,256},
	{"NAND 256MiB 3.3V 8-bit",	0xDA,256},
	{"NAND 256MiB 1.8V 16-bit",	0xBA,256},
	{"NAND 256MiB 3.3V 16-bit",	0xCA,256},

	/* 4 Gigabit */
	{"NAND 512MiB 1.8V 8-bit",	0xAC,512},
	{"NAND 512MiB 3.3V 8-bit",	0xDC,512},
	{"NAND 512MiB 1.8V 16-bit",	0xBC,512},
	{"NAND 512MiB 3.3V 16-bit",	0xCC,512},

	/* 8 Gigabit */
	{"NAND 1GiB 1.8V 8-bit",	0xA3,1024},
	{"NAND 1GiB 3.3V 8-bit",	0xD3,1024},
	{"NAND 1GiB 1.8V 16-bit",	0xB3,1024},
	{"NAND 1GiB 3.3V 16-bit",	0xC3,1024},

	/* 16 Gigabit */
	{"NAND 2GiB 1.8V 8-bit",	0xA5,2048},
	{"NAND 2GiB 3.3V 8-bit",	0xD5,2048},
	{"NAND 2GiB 1.8V 16-bit",	0xB5,2048},
	{"NAND 2GiB 3.3V 16-bit",	0xC5,2048},

	/* 32 Gigabit */
	{"NAND 4GiB 1.8V 8-bit",	0xA7,4096},
	{"NAND 4GiB 3.3V 8-bit",	0xD7,4096},
	{"NAND 4GiB 1.8V 16-bit",	0xB7,4096},
	{"NAND 4GiB 3.3V 16-bit",	0xC7,4096},

	/* 64 Gigabit */
	{"NAND 8GiB 1.8V 8-bit",	0xAE,8192},
	{"NAND 8GiB 3.3V 8-bit",	0xDE,8192},
	{"NAND 8GiB 1.8V 16-bit",	0xBE,8192},
	{"NAND 8GiB 3.3V 16-bit",	0xCE,8192},

	/* 128 Gigabit */
	{"NAND 16GiB 1.8V 8-bit",	0x1A,16384},
	{"NAND 16GiB 3.3V 8-bit",	0x3A,16384},
	{"NAND 16GiB 1.8V 16-bit",	0x2A,16384},
	{"NAND 16GiB 3.3V 16-bit",	0x4A,16384},
                                                  
	/* 256 Gigabit */
	{"NAND 32GiB 1.8V 8-bit",	0x1C,32768},
	{"NAND 32GiB 3.3V 8-bit",	0x3C,32768},
	{"NAND 32GiB 1.8V 16-bit",	0x2C,32768},
	{"NAND 32GiB 3.3V 16-bit",	0x4C,32768},

	/* 512 Gigabit */
	{"NAND 64GiB 1.8V 8-bit",	0x1E,65536},
	{"NAND 64GiB 3.3V 8-bit",	0x3E,65536},
	{"NAND 64GiB 1.8V 16-bit",	0x2E,65536},
	{"NAND 64GiB 3.3V 16-bit",	0x4E,65536},
	{0,0,0},
};


struct  {
  unsigned int id;
  char* name;
}  nand_manuf_ids[] = {
	{0x98, "Toshiba"},
	{0xec, "Samsung"},
	{0x04, "Fujitsu"},
	{0x8f, "National"},
	{0x07, "Renesas"},
	{0x20, "ST Micro"},
	{0xad, "Hynix"},
	{0x2c, "Micron"},
	{0xc8, "Elite Semiconductor"},
	{0x01, "Spansion/AMD"},
	{0xef, "Winbond"},
	{0x0, 0}
};

mempoke(nand_cmd,0x8000b); // Extended Fetch ID Team
mempoke(nand_exec,1); 
nandwait();
nandid=mempeek(NAND_FLASH_READ_ID); // get the flash drive ID
chipsize=0;

fid=(nandid>>8)&0xff;
pid=nandid&0xff;

// Determine the manufacturer of the flash drive
i=0;
while (nand_manuf_ids[i].id != 0) {
	if (nand_manuf_ids[i].id == pid) {
	strcpy(flash_mfr,nand_manuf_ids[i].name);
	break;
	}
i++;
}  
    
// Determine the capacity of the flash drive
i=0;
while (nand_ids[i].id != 0) {
if (nand_ids[i].id == fid) {
	chipsize=nand_ids[i].chipsize;
	strcpy(flash_descr,nand_ids[i].type);
	break;
	}
i++;
}  
if (chipsize == 0) {
	printf("\n Undefined Flash ID = %02x",fid);
}  

// We take out the configuration parameters

cfg0=mempeek(nand_cfg0);
cfg1=mempeek(nand_cfg1);
ecccfg=mempeek(nand_ecc_cfg);
sectorsize=512;
//sectorsize=(cfg0&(0x3ff<<9))>>9; //UD_SIZE_BYTES = blocksize

devcfg = (nandid>>24) & 0xff;
pagesize = 1024 << (devcfg & 0x3); // page size in bytes
blocksize = 64 << ((devcfg >> 4) & 0x3);  // block size in kilobytes
spp = pagesize/sectorsize; // sectors per page

if ((((cfg0>>6)&7)|((cfg0>>2)&8)) == 0) {
  // for older chipsets, the lower 2 bytes of CFG0 must be configured by hand
  if (!bad_loader) mempoke(nand_cfg0,(cfg0|0x40000|(((spp-1)&8)<<2)|(((spp-1)&7)<<6)));
}  

// Determine the type and size of ECC
if (((cfg1>>27)&1) != 0) bch_mode=1;
if (bch_mode) { 
  // for bch
  ecc_size=(ecccfg>>8)&0x1f; 
  ecc_bit=((ecccfg>>4)&3) ? (((ecccfg>>4)&3)+1)*4 : 4;
}
else {
  // For R-S
  ecc_size=(cfg0>>19)&0xf;
  ecc_bit=4;
}  

badposition=(cfg1>>6)&0x3ff;
badplace=(cfg1>>16)&1;

linuxcwsize=528;
if (bch_mode && (ecc_bit == 8)) linuxcwsize=532;

// Setting up a bad marker if it is not auto-tuned

c_badmark_pos = (pagesize-(linuxcwsize*(spp-1))+1);
if (badposition == 0) {
  printf("\n! Attention - the position of the marker of defective blocks is automatically determined!\n");  
  badplace=0;
  badposition=c_badmark_pos;
}  
if (badposition != c_badmark_pos) {
  printf("\n! Warning - the current position of the marker of defective blocks %x does not match the calculated %x!\n",
     badposition,c_badmark_pos);  
}

// check the sign of a 16-bit flash drive
if ((cfg1&2) != 0) flash16bit=1;
if (chipsize != 0)   maxblock=chipsize*1024/blocksize;
else                 maxblock=0x800;

if (oobsize == 0) {
	// Micron MT29F4G08ABBEA3W and Toshiba MD5N04G02GSD2ARK:
	// actually 224, determined by 128, really
	// 160 is used, 256 is more obvious for raw mode :)
	if ((nandid == 0x2690ac2c) || (nandid == 0x2690ac98)) oobsize = 256; 
	else oobsize = (8 << ((devcfg >> 2) & 0x1)) * (pagesize >> 9);
} 

}


//**********************************************
//* Disabling hardware block control
//**********************************************
//*/
void hardware_bad_off() {

int cfg1;

cfg1=mempeek(nand_cfg1);
cfg1 &= ~(0x3ff<<6);
mempoke(nand_cfg1,cfg1);
}

//**********************************************
//* Включение аппаратного контроля бедблоков
//**********************************************
//*/
void hardware_bad_on() {

int cfg1;

cfg1=mempeek(nand_cfg1);
cfg1 &= ~(0x7ff<<6);
cfg1 |= (badposition &0x3ff)<<6; // offset to marker
cfg1 |= badplace<<16;            // area where the marker is located (user / spare)
mempoke(nand_cfg1,cfg1);
}

//**********************************************
//* Marker position setting
//**********************************************
//*/
void set_badmark_pos (int pos, int place) {

badposition=pos;
badplace=place&1;
hardware_bad_on();
}


//**********************************
//* Closing a section data stream
//**********************************
//*/
int qclose(int errmode) {
unsigned char iobuf[600];
unsigned char cmdbuf[]={0x15};
int iolen;

iolen=send_cmd(cmdbuf,1,iobuf);
if (!errmode) return 1;
if (iobuf[1] == 0x16) return 1;
show_errpacket("close()",iobuf,iolen);
return 0;

}  

//************************
//* Erasing a flash drive block
//************************
//*/

void block_erase(int block) {
  
int oldcfg;  
  
nand_reset();
mempoke(nand_addr0,block*ppb);         // the lower part of the address is # pages
mempoke(nand_addr1,0);                 // the upper part of the address is always 0

oldcfg=mempeek(nand_cfg0);
mempoke(nand_cfg0,oldcfg&~(0x1c0));    // set CW_PER_PAGE = 0, as the datasheet requires

mempoke(nand_cmd,0x3a); // erasing. Last page bit set
mempoke(nand_exec,0x1);
nandwait();
mempoke(nand_cfg0,oldcfg);   // restore CFG0
}

//****************************************
//* Disabling NANDc BAM
//****************************************
//*/
void disable_bam() {

unsigned int i,nandcstate[256],bcraddr=0xfc401a40;

if (is_chipset("MDM9x4x")) bcraddr=0x0183f000;
for (i=0;i<0xec;i+=4) nandcstate[i]=mempeek(nand_cmd+i); // save the state of the NAND controller

mempoke(bcraddr,1); // GCC_QPIC_BCR
mempoke(bcraddr,0); // full asynchronous QPIC reset

for (i=0;i<0xec;i+=4) mempoke(nand_cmd+i,nandcstate[i]);  // restore state
mempoke(nand_exec,1); // dummy reading to remove write protection of the address registers of the controller
}


//****************************************************
//* Checking array for nonzero values
//*
//*  0 - there are only zeros in the array
//*  1 - there are nonzeros in the array
//****************************************************
//*/
int test_zero(unsigned char* buf, int len) {
  
int i;
for (i=0;i<len;i++)
  if (buf[i] != 0) return 1;
return 0;
}

//***************************************************************
//* Chipset identification via applet based on bootloader signature
//*
//* return -1 - the bootloader does not support command 11
//*         0 - no chipset identification signature found in bootloader
//*         the rest is the chipset code from the bootloader
//***************************************************************
//*/
int identify_chipset() {

char cmdbuf[]={ 
  0x11,0x00,0x04,0x10,0x2d,0xe5,0x0e,0x00,0xa0,0xe1,0x03,0x00,0xc0,0xe3,0xff,0x30,
  0x80,0xe2,0x34,0x10,0x9f,0xe5,0x04,0x20,0x90,0xe4,0x01,0x00,0x52,0xe1,0x03,0x00,
  0x00,0x0a,0x03,0x00,0x50,0xe1,0xfa,0xff,0xff,0x3a,0x00,0x00,0xa0,0xe3,0x00,0x00,
  0x00,0xea,0x00,0x00,0x90,0xe5,0x04,0x10,0x9d,0xe4,0x01,0x00,0xc1,0xe5,0xaa,0x00,
  0xa0,0xe3,0x00,0x00,0xc1,0xe5,0x02,0x40,0xa0,0xe3,0x1e,0xff,0x2f,0xe1,0xef,0xbe,
  0xad,0xde
};
unsigned char iobuf[1024];
send_cmd(cmdbuf,sizeof(cmdbuf),iobuf);
if (iobuf[1] != 0xaa) return -1;
return iobuf[2];
}

//*******************************************************
//* Testing the bootloader patch
//*
//* Returns 0 if command 11 is not supported
//* and sets the global variable bad_loader = 1
//*******************************************************
//*/
int test_loader() {

int i;

i=identify_chipset();
//printf("\n ident = %i\n",i);
if (i<=0) {
  bad_loader=1;
  return 0;
}
if (chip_type == 0) set_chipset(i); // if the chipset has not been explicitly set
return 1;
}

//****************************************************************
//*  Checking the defective block flag of a previous read operation
//*
//* 0 - no badblock
//* 1 - is
//****************************************************************
//*/

int test_badblock() {

unsigned int st,r,badflag=0;

// The high 2 bytes of the register nand_buffer_status reflect the marker read from the flash drive.
// For 8-bit flash drives, only the low byte is used, for 16-bit flash drives - both bytes
st=r=mempeek(nand_buffer_status)&0xffff0000;
if (flash16bit == 0) {
  if (st != 0xff0000) { 
    badflag=1;  
//     printf("\nst=%08x",r);    
  }
}  
else  if (st != 0xffff0000) badflag=1;
return badflag;
}


//*********************************
//*  Block defect check
//*********************************
//*/
int check_block(int blk) {

nand_reset(); // discharge
setaddr(blk,0);
mempoke(nand_cmd,0x34); // reading data + ecc + spare
mempoke(nand_exec,0x1);
nandwait();
return test_badblock();
}  

//*********************************
//* Writing a bad marker
//*********************************
//*/
void write_badmark(unsigned int blk, int val) {
  
char buf[1000];
const int udsize=0x220;
int i;
unsigned int cfg1bak,cfgeccbak;

cfg1bak=mempeek(nand_cfg1);
cfgeccbak=mempeek(nand_ecc_cfg);
mempoke(nand_ecc_cfg,mempeek(nand_ecc_cfg)|1); 
mempoke(nand_cfg1,mempeek(nand_cfg1)|1); 

hardware_bad_off();
memset(buf,val,udsize);
buf[0]=0xeb;   // sign of artificially created bad block

nand_reset();
nandwait();

setaddr(blk,0);
mempoke(nand_cmd,0x39); // write data + ecc + spare
for (i=0;i<spp;i++) {
 memwrite(sector_buf, buf, udsize);
 mempoke(nand_exec,1);
 nandwait();
}
hardware_bad_on();
mempoke(nand_cfg1,cfg1bak);
mempoke(nand_ecc_cfg,cfgeccbak);
}


//************************************************
//* Setting a bad marker
//* -> 0 - the block was already defective
//*    1 - was normal and defective
//**********************************************
//*/
int mark_bad(unsigned int blk) {

//flash_read(blk,0,0);  
if (!check_block(blk)) {  
 write_badmark(blk,0);
 return 1;
}
return 0;
}


//************************************************
//* Removing a bad marker
//* -> 0 - the unit was not defective
//*    1 - was defective and made normal
//************************************************
//*/
int unmark_bad(unsigned int blk) {
  

//flash_read(blk,0,0);  
if (check_block(blk)) {  
 block_erase(blk);
 return 1;
}
return 0;
}

//****************************************************
//* Checking the buffer for a placeholder pad
//****************************************************
//*/
int test_badpattern(unsigned char* buf) {
  
int i;
for(i=0;i<512;i++) {
  if (buf[i] != 0xbb) return 0;
}
return 1;
}

//**********************************************************
//* Setting the size of the sector data field
//**********************************************************
//*/
void set_udsize(unsigned int size) {

unsigned int tmpreg=mempeek(nand_cfg0);  

tmpreg=(tmpreg&(~(0x3ff<<9)))|(size<<9); // CFG0.UD_SIZE_BYTES
mempoke(nand_cfg0,tmpreg);

if (((mempeek(nand_cfg1)>>27)&1) != 0) { // BCH ECC
  tmpreg=mempeek(nand_ecc_cfg);
  tmpreg=(tmpreg&(~(0x3ff<<16))|(size<<16)); //ECC_CFG.ECC_NUM_DATA_BYTES
  mempoke(nand_ecc_cfg,tmpreg);
}  
}

//**********************************************************
//* Setting the size of the spare field
//**********************************************************
//*/
void set_sparesize(unsigned int size) {

unsigned int cfg0=mempeek(nand_cfg0);  
cfg0=cfg0&(~(0xf<<23))|(size<<23); //SPARE_SIZE_BYTES 
mempoke(nand_cfg0,cfg0);
}

//**********************************************************
//* Setting ECC Field Size
//**********************************************************
//*/
void set_eccsize(unsigned int size) {

uint32 cfg0, cfg1, ecccfg, bch_mode=0;

cfg1=mempeek(nand_cfg1);
  
// Определяем тип ЕСС
if (((cfg1>>27)&1) != 0) bch_mode=1;
  
if (bch_mode) {
  ecccfg=mempeek(nand_ecc_cfg);
  ecccfg= (ecccfg&(~(0x1f<<8))|(size<<8));
  mempoke(nand_ecc_cfg,ecccfg);
}  
else {
  cfg0=mempeek(nand_cfg0);  
  cfg0=cfg0&(~(0xf<<19))|(size<<19); //ECC_PARITY_SIZE_BYTES = eccs
  mempoke(nand_cfg0,cfg0);
} 
}

  
//**********************************************************
//*  Setting the sector format in the controller configuration
//*
//*  udsize - size of data in bytes
//*  ss - size spare in xs what units
//*  eccs - ecc size in bytes
//**********************************************************
//*/
void set_blocksize(unsigned int udsize, unsigned int ss,unsigned int eccs) {

set_udsize(udsize);
set_sparesize(ss);
set_eccsize(eccs);
}

//******************************************************************
//*  Getting current udsize
//******************************************************************
int get_udsize() {

return ( mempeek(nand_cfg0) & (0x3ff<<9) )>>9;
}  
  

//******************************************************************
//* Analysis of the parameters of the key determining the position of the bed marker
//*
//* Parameter Format:
//*   xxx  - sector data marker
//*   Uxxx - sector data marker
//*   Sxxx - marker in the field of OOB (in spare)
//*
//*  badpos - marker position
//*  badloc - area where the marker is located (0-user, 1-spare)
//******************************************************************
//*/
void parse_badblock_arg(char* arg, int* badpos, int* badloc) {

char* str=arg;
  
*badloc=0;
if       (toupper(str[0]) == 'U') str++;
else if  (toupper(str[0]) == 'S') {
  *badloc=1;
  str++;
}

sscanf(str,"%x",badpos);
}


//***************************************************************
//* Determining the state of ECC correction after a read operation
//*
//* Returns:
//*  0 - there was no correction
//* -1 - uncorrectable error
//* > 0 - the number of corrected errors
//***************************************************************
//*/

int check_ecc_status() {
  
int bs;

bs=mempeek(nand_buffer_status);
if (((bs&0x100) != 0) && ((mempeek(nand_cmd+0xec) & 0x40) == 0)) return -1; // uncorrectable error
return bs&0x1f; // 	number of correctable errors
}

//***************************************************************
//*  Reset ECC VSN engine
//***************************************************************
//*/
void bch_reset() {

int cfgecctemp;  
  
if (!bch_mode) return;
cfgecctemp=mempeek(nand_ecc_cfg); // ECC enable / disable configuration
mempoke(nand_ecc_cfg,cfgecctemp|2); // BCH engine reset
mempoke(nand_ecc_cfg,cfgecctemp); // BCH configuration recovery
}
