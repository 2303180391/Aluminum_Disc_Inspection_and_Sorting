#include "yolo_load_param.h"

FIL fil;
FATFS fatfs;


char FileBias[14][32] 		= {"l0_b.bin", "l2_b.bin", "l4_b.bin", "l6_b.bin", "l8_b.bin", "l8_b.bin", "l10_b.bin", "l12_b.bin",
                         	 	"l13_b.bin", "l14_b.bin", "l15_b.bin", "l18_b.bin", "l21_b.bin", "l22_b.bin"};
char FileLeakyRelu[14][32] 	= {"l0_r.bin", "l2_r.bin", "l4_r.bin", "l6_r.bin", "l820_r.bin", "l8_r.bin", "l10_r.bin", "l12_r.bin",
                         	 	"l13_r.bin", "l14_r.bin", "l15_r.bin", "l18_r.bin", "l21_r.bin", "l22_r.bin"};
char FileWeight[14][32] 	= {"l0_w.bin", "l2_w.bin", "l4_w.bin", "l6_w.bin", "l8_w.bin", "l8_w.bin", "l10_w.bin", "l12_w.bin",
                         	 	"l13_w.bin", "l14_w.bin", "l15_w.bin", "l18_w.bin", "l21_w.bin", "l22_w.bin"};
int BiasAddr[15] = {BIAS_STORE_BASEADDR,
                    BIAS_STORE_BASEADDR+16*4,
                    BIAS_STORE_BASEADDR+(16+32)*4,
                    BIAS_STORE_BASEADDR+(16+32+64)*4,
                    BIAS_STORE_BASEADDR+(16+32+64+128)*4,
                    BIAS_STORE_BASEADDR+(16+32+64+128+256)*4,
					BIAS_STORE_BASEADDR+(16+32+64+128+256*2)*4,
                    BIAS_STORE_BASEADDR+(16+32+64+128+256*2+512)*4,
                    BIAS_STORE_BASEADDR+(16+32+64+128+256*2+512+1024)*4,
                    BIAS_STORE_BASEADDR+(16+32+64+128+256*2+512+1024+256)*4,
                    BIAS_STORE_BASEADDR+(16+32+64+128+256*2+512+1024+256+512)*4,
                    BIAS_STORE_BASEADDR+(16+32+64+128+256*2+512+1024+256+512+24)*4,
                    BIAS_STORE_BASEADDR+(16+32+64+128+256*2+512+1024+256+512+24+128)*4,
                    BIAS_STORE_BASEADDR+(16+32+64+128+256*2+512+1024+256+512+24+128+256)*4,
                    BIAS_STORE_BASEADDR+(16+32+64+128+256*2+512+1024+256+512+24+128+256+24)*4};

int ActAddr[15] =  {ACT_STORE_BASEADDR,
		    ACT_STORE_BASEADDR+256,
		    ACT_STORE_BASEADDR+256*2,
		    ACT_STORE_BASEADDR+256*3,
		    ACT_STORE_BASEADDR+256*4,
		    ACT_STORE_BASEADDR+256*5,
		    ACT_STORE_BASEADDR+256*6,
		    ACT_STORE_BASEADDR+256*7,
		    ACT_STORE_BASEADDR+256*8,
		    ACT_STORE_BASEADDR+256*9,
		    ACT_STORE_BASEADDR+256*10,
		    ACT_STORE_BASEADDR+256*11,
		    ACT_STORE_BASEADDR+256*12,
		    ACT_STORE_BASEADDR+256*13,
			ACT_STORE_BASEADDR+256*14};

int WeightAddr[15] =   {WEIGHT_STORE_BASEADDR,
                        WEIGHT_STORE_BASEADDR+16*8*9,
                        WEIGHT_STORE_BASEADDR+16*8*9+32*16*9,
                        WEIGHT_STORE_BASEADDR+16*8*9+32*16*9+64*32*9,
                        WEIGHT_STORE_BASEADDR+16*8*9+32*16*9+64*32*9+128*64*9,
                        WEIGHT_STORE_BASEADDR+16*8*9+32*16*9+64*32*9+128*64*9+256*128*9,
						WEIGHT_STORE_BASEADDR+16*8*9+32*16*9+64*32*9+128*64*9+256*128*9*2,
                        WEIGHT_STORE_BASEADDR+16*8*9+32*16*9+64*32*9+128*64*9+256*128*9*2+512*256*9,
                        WEIGHT_STORE_BASEADDR+16*8*9+32*16*9+64*32*9+128*64*9+256*128*9*2+512*256*9+1024*512*9,
                        WEIGHT_STORE_BASEADDR+16*8*9+32*16*9+64*32*9+128*64*9+256*128*9*2+512*256*9+1024*512*9+256*1024,
                        WEIGHT_STORE_BASEADDR+16*8*9+32*16*9+64*32*9+128*64*9+256*128*9*2+512*256*9+1024*512*9+256*1024+512*256*9,
                        WEIGHT_STORE_BASEADDR+16*8*9+32*16*9+64*32*9+128*64*9+256*128*9*2+512*256*9+1024*512*9+256*1024+512*256*9+24*512,
                        WEIGHT_STORE_BASEADDR+16*8*9+32*16*9+64*32*9+128*64*9+256*128*9*2+512*256*9+1024*512*9+256*1024+512*256*9+24*512+128*256,
                        WEIGHT_STORE_BASEADDR+16*8*9+32*16*9+64*32*9+128*64*9+256*128*9*2+512*256*9+1024*512*9+256*1024+512*256*9+24*512+128*256+256*384*9,
                        WEIGHT_STORE_BASEADDR+16*8*9+32*16*9+64*32*9+128*64*9+256*128*9*2+512*256*9+1024*512*9+256*1024+512*256*9+24*512+128*256+256*384*9+24*256};

int load_param(void)
{
	//////////////////////////////////////
	// Bias
	//////////////////////////////////////
	for(int i=0; i<14; i++) {
		if(SD_Read(FileBias[i], BiasAddr[i], BiasAddr[i+1]-BiasAddr[i]) != XST_SUCCESS) {
			xil_printf("ERROR: cannot read %s\r\n", FileBias[i]);
			return XST_FAILURE;
		}
	}
	//////////////////////////////////////
	// LeakyRelu
	//////////////////////////////////////
	for(int i=0; i<14; i++) {
		if(SD_Read(FileLeakyRelu[i], ActAddr[i], ActAddr[i+1]-ActAddr[i]) != XST_SUCCESS) {
			xil_printf("ERROR: cannot read %s\r\n", FileLeakyRelu[i]);
			return XST_FAILURE;
		}
	}
	//////////////////////////////////////
	// Weight
	//////////////////////////////////////
	for(int i=0; i<14; i++) {
		if(SD_Read(FileWeight[i], WeightAddr[i], WeightAddr[i+1]-WeightAddr[i]) != XST_SUCCESS) {
			xil_printf("ERROR: cannot read %s\r\n", FileWeight[i]);
			return XST_FAILURE;
		}
	}
	return XST_SUCCESS;
}
int SD_Init()
{
	FRESULT Res;
	/*
	 * To test logical drive 0, Path should be "0:/"
	 * For logical drive 1, Path should be "1:/"
	 */
	TCHAR *Path = "0:/";

	/*
	 * Register volume work area, initialize device
	 */
	Res = f_mount(&fatfs, Path, 0);

	if (Res != FR_OK) {
		return XST_FAILURE;
	}

	/*
	 * Path - Path to logical driver, 0 - FDISK format.
	 * 0 - Cluster size is automatically determined based on Vol size.
	 */
//	Res = f_mkfs(Path, FM_FAT32, 0, work, sizeof work);
//	if (Res != FR_OK) {
//		return XST_FAILURE;
//	}
	return XST_SUCCESS;
}

int SD_Write(char *FileName, u32 SourceAddress, u32 FileSize)
{
	FRESULT Res;
	UINT NumBytesWritten;

	Res = f_open(&fil, FileName, FA_CREATE_ALWAYS | FA_WRITE);
	if (Res) {
		return XST_FAILURE;
	}

	/*
	 * Pointer to beginning of file .
	 */
	Res = f_lseek(&fil, 0);
	if (Res) {
		return XST_FAILURE;
	}

	/*
	 * Write data to file.
	 */
	Res = f_write(&fil, (const void*)SourceAddress, FileSize,
			&NumBytesWritten);
	if (Res || NumBytesWritten != FileSize) {
		f_close(&fil);
		return XST_FAILURE;
	}


	/*
	 * Close file.
	 */
	Res = f_close(&fil);
	if (Res) {
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

int SD_Read(char *FileName, u32 DestinationAddress, u32 FileSize)
{
	FRESULT Res;
	UINT NumBytesRead = 0U;
	u32 Offset = 0U;
	const UINT MaxChunk = 4U * 1024U;
	const unsigned int MaxRetries = 3U;

	Res = f_open(&fil, FileName, FA_READ);
	if (Res) {
		xil_printf("SD open failed: %s FR=%d\r\n", FileName, Res);
		return XST_FAILURE;
	}

	/*
	 * Pointer to beginning of file .
	 */
	Res = f_lseek(&fil, 0);
	if (Res) {
		xil_printf("SD seek failed: %s FR=%d\r\n", FileName, Res);
		f_close(&fil);
		return XST_FAILURE;
	}

	/* Use conservative transfers.  Some card/controller combinations become
	 * unreliable during long sequential reads when large requests are used. */
	while (Offset < FileSize) {
		UINT Chunk = (FileSize - Offset > MaxChunk) ?
				MaxChunk : (UINT)(FileSize - Offset);
		unsigned int Retry;
		int ChunkReadOK = 0;

		for (Retry = 0U; Retry <= MaxRetries; Retry++) {
			NumBytesRead = 0U;
			Res = f_read(&fil, (void *)(DestinationAddress + Offset), Chunk,
					&NumBytesRead);
			if (Res == FR_OK && NumBytesRead == Chunk) {
				ChunkReadOK = 1;
				break;
			}

			xil_printf("SD retry %u/%u: %s off=%lu FR=%d got=%u want=%u\r\n",
					Retry + 1U, MaxRetries + 1U, FileName,
					(unsigned long)Offset, Res, NumBytesRead, Chunk);

			/* FR_DISK_ERR aborts the current FatFs file object.  The disk
			 * layer marks the controller uninitialized, so remounting here
			 * performs a complete host/card reinitialization before retry. */
			f_close(&fil);
			if (Retry == MaxRetries) {
				break;
			}
			f_mount(NULL, "0:/", 0);
			Res = f_mount(&fatfs, "0:/", 1);
			if (Res == FR_OK) {
				Res = f_open(&fil, FileName, FA_READ);
			}
			if (Res == FR_OK) {
				Res = f_lseek(&fil, Offset);
			}
			if (Res != FR_OK) {
				xil_printf("SD reinit failed: %s off=%lu FR=%d\r\n",
						FileName, (unsigned long)Offset, Res);
			}
		}

		if (!ChunkReadOK) {
			xil_printf("SD read failed permanently: %s off=%lu\r\n",
					FileName, (unsigned long)Offset);
			return XST_FAILURE;
		}
		Offset += Chunk;
	}


	/*
	 * Close file.
	 */
	Res = f_close(&fil);
	if (Res) {
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}
