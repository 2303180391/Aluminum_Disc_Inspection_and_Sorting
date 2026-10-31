#ifndef YOLO_DIAG_H
#define YOLO_DIAG_H
#include "xil_types.h"

#define DIAG_BASE 0x00E00000U
enum { D_BOOT=1, D_LOAD=2, D_READY=3, D_CAPTURE=4, D_FRAME=5,
       D_TASK=6, D_IRQ_WAIT=7, D_DMA_WAIT=8, D_LAYER=9,
       D_COMPLETE=10, D_FAILED=255 };
enum { E_SD=1, E_IRQ_INIT=2, E_DMA_RESET=3, E_DMA_STATUS=4,
       E_DMA_TIMEOUT=5, E_PL_TIMEOUT=6, E_CAPTURE=7,
       E_INPUT_CHANGED=8, E_LAYER_CHANGED=9, E_BAD_TRANSFER=10,
       E_PARAM_CHANGED=11 };
/* Exactly 64 words. Uncached mailbox at DIAG_BASE; only request is host-written. */
typedef struct {
    u32 magic, version, stage, error;                         /* words 0..3 */
    u32 request, frame, layer, task;                          /* 4..7 */
    u32 command, tx_addr, tx_len, rx_addr, rx_len;             /* 8..12 */
    u32 tx_status, rx_status, irq_count, irq_start;            /* 13..16 */
    u32 stale_irq, direction, completed_tasks, input_crc;      /* 17..20 */
    u32 input_reference, vdma_status, last_crc, expected_crc;  /* 21..24 */
    u32 feature_size, in_batch, out_batch, row_batch;          /* 25..28 */
    u32 transfer_us, reserved0, reserved1;                    /* 29..31 */
    u32 reference[15], reserved2, current[15], reserved3;      /* 32..63 */
} YoloDiag;
extern volatile YoloDiag * const yolo_diag;
void diag_init(void);
void diag_params_init(void);
void diag_params_check(u32 phase);
void diag_stage(u32 stage);
void diag_wait_capture(void);
void diag_frame_begin(void);
void diag_frame_end(void);
void diag_layer_begin(u32 layer);
void diag_layer_done(u32 layer);
void diag_write_reg(UINTPTR addr, u32 value);
void diag_dma_start(u32 direction, u32 addr, u32 len);
int diag_wait_task(void);
void diag_irq(void);
void diag_fail(u32 error) __attribute__((noreturn));
void yolo_diag_failed(void) __attribute__((noreturn,noinline));
void yolo_diag_complete(void) __attribute__((noreturn,noinline));
#endif
