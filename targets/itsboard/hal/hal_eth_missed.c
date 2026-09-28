#include "hal_eth_missed.h"

#define CADS_MFBOCR_MFC_MASK   0x0000FFFFu
#define CADS_MFBOCR_OMFC       (1u << 16)
#define CADS_MFBOCR_MFA_SHIFT  17u
#define CADS_MFBOCR_MFA_MASK   0x7FFu
#define CADS_MFBOCR_OFOC       (1u << 28)

cads_eth_missed_t cads_eth_missed_decode(uint32_t dmamfbocr) {
    cads_eth_missed_t out;
    out.no_descriptor = dmamfbocr & CADS_MFBOCR_MFC_MASK;
    if(dmamfbocr & CADS_MFBOCR_OMFC) out.no_descriptor += CADS_MFBOCR_MFC_MASK + 1u;
    out.fifo_overflow = (dmamfbocr >> CADS_MFBOCR_MFA_SHIFT) & CADS_MFBOCR_MFA_MASK;
    if(dmamfbocr & CADS_MFBOCR_OFOC) out.fifo_overflow += CADS_MFBOCR_MFA_MASK + 1u;
    return out;
}
