#ifndef YOLO_LOAD_PARAM_H
#define YOLO_LOAD_PARAM_H

#include "xsdps.h"		/* SD device driver */
#include "ff.h"
#include "xstatus.h"
#include "xil_printf.h"

#define	BIAS_STORE_BASEADDR		0x2000000
#define	ACT_STORE_BASEADDR		0x2004000
#define	WEIGHT_STORE_BASEADDR	0x2005000

extern FIL fil;		/* File object */
extern FATFS fatfs;
extern int BiasAddr[15];
extern int ActAddr[15];
extern int WeightAddr[15];

int SD_Init();
int SD_Write(char *FileName, u32 SourceAddress, u32 FileSize);
int SD_Read(char *FileName, u32 DestinationAddress, u32 FileSize);

int load_param(void);

#endif
