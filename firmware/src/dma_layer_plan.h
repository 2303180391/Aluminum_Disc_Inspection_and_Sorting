#ifndef DMA_LAYER_PLAN_H
#define DMA_LAYER_PLAN_H
/* Pure range arithmetic, also compiled by host tests. No CPU writer may alter
 * the prepared source until end-of-layer. RX ownership and barriers unchanged. */
typedef struct {unsigned begin,end,ready;} DmaLayerPlan;
static inline unsigned dma_layer_bytes(unsigned layer,unsigned channels,unsigned side){
    if(layer>=15U || !channels || channels>1024U || !side || side>416U)return 0;
    /* Index 12 is 13->26 nearest upsample: config size describes its OUTPUT. */
    if(layer==12U){if(side!=26U || channels!=128U)return 0;side=13U;}
    return channels*side*side;
}
static inline void dma_layer_reset(DmaLayerPlan *p){p->begin=p->end=p->ready=0U;}
static inline int dma_layer_set(DmaLayerPlan *p,unsigned addr,unsigned bytes){
    dma_layer_reset(p);
    if(!bytes || bytes>0xFFFFFFFFU-addr)return 0;
    p->begin=addr;p->end=addr+bytes;p->ready=1U;return 1;
}
static inline int dma_layer_contains(const DmaLayerPlan *p,unsigned addr,unsigned bytes){
    return p->ready && bytes && addr>=p->begin && addr<p->end && bytes<=p->end-addr;
}
#endif
