#ifndef _SKEIN_IV_H_
#define _SKEIN_IV_H_

#include "skein.h"    /* get Skein macros and types */

/*
***************** Pre-computed Skein IVs *******************
**
** NOTE: these values are not "magic" constants, but
** are generated using the Threefish block function.
** They are pre-computed here only for speed; i.e., to
** avoid the need for a Threefish call during Init().
**
** The IV for any fixed hash length may be pre-computed.
** Only the most common values are included here.
**
************************************************************
**/

#define MK_64 SKEIN_MK_64

/* blkSize =  512 bits. hashSize =  128 bits */
const uint64_t SKEIN_512_IV_128[] =
    {
    MK_64(0x477DF9EF,0xAFC4F08A),
    MK_64(0x7A64D342,0x33660E14),
    MK_64(0x71653C44,0xCEBC89C5),
    MK_64(0x63D2A36D,0x65B0AB91),
    MK_64(0x52B93FB0,0x9782EA89),
    MK_64(0x20F36980,0x8B960829),
    MK_64(0xE8DF80FB,0x30303B9B),
    MK_64(0xB89D3902,0x1A476D1F)
    };

/* blkSize =  512 bits. hashSize =  160 bits */
const uint64_t SKEIN_512_IV_160[] =
    {
    MK_64(0x0045FA2C,0xAD913A2C),
    MK_64(0xF45C9A76,0xBF75CE81),
    MK_64(0x0ED758A9,0x3D1F266B),
    MK_64(0xC0E65E85,0x1EDCD67A),
    MK_64(0x1E024D51,0xF5E7583E),
    MK_64(0xA271F855,0x4E52B0E1),
    MK_64(0x5292867D,0x8AC674F9),
    MK_64(0xADA325FA,0x60C3B226)
    };

/* blkSize =  512 bits. hashSize =  224 bits */
const uint64_t SKEIN_512_IV_224[] =
    {
    MK_64(0xF2DAA169,0x8216CC98),
    MK_64(0x00E06A48,0x8983AE05),
    MK_64(0xC080CEA9,0x5948958F),
    MK_64(0x2A8F314B,0x57F4ADD1),
    MK_64(0xBCD06591,0x360A405A),
    MK_64(0xF81A11A1,0x02D91F70),
    MK_64(0x85C6FFA5,0x4810A739),
    MK_64(0x1E07AFE0,0x1802CE74)
    };

/* blkSize =  512 bits. hashSize =  256 bits */
const uint64_t SKEIN_512_IV_256[] =
    {
    MK_64(0x88C07F38,0xD4F95AD4),
    MK_64(0x3DF0D33A,0x8610E240),
    MK_64(0x3E243F6E,0xDB6FAC74),
    MK_64(0xBAC4F4CD,0xD7A90A24),
    MK_64(0xDF90FD1F,0xDEEEBA04),
    MK_64(0xA4F5796B,0xDB7FDDA8),
    MK_64(0xDA182FD2,0x964BC923),
    MK_64(0x55F76677,0xEF6961F9)
    };

#endif /* _SKEIN_IV_H_ */
