/***********************************************************************
**
** Implementation of the Skein hash function.
**
** Source code author: Doug Whiting, 2008.
**
** This algorithm and source code is released to the public domain.
** 
************************************************************************/

#include <string.h>
#include "skein.h"       /* get the Skein API definitions   */
#include "skein_iv.h"    /* get precomputed IVs */
#include "util.h"
#include "endian.h"

/*****************************************************************/
/* External function to process blkCnt (nonzero) full block(s) of data. */
void    Skein_512_Process_Block(Skein_512_Ctxt_t *ctx,const uint8_t *blkPtr,size_t blkCnt,size_t byteCntAdd);

/*****************************************************************/
/*     512-bit Skein                                             */
/*****************************************************************/

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* init the context for a straight hashing operation  */
int Skein_512_Init(Skein_512_Ctxt_t *ctx, size_t hashBitLen)
    {
    ctx->h.hashBitLen = hashBitLen;         /* output hash bit count */

    switch (hashBitLen)
        {             /* use pre-computed values, where available */
        case  256: memcpy(ctx->X,SKEIN_512_IV_256,sizeof(ctx->X));  break;
        case  224: memcpy(ctx->X,SKEIN_512_IV_224,sizeof(ctx->X));  break;
        case  128: memcpy(ctx->X,SKEIN_512_IV_128,sizeof(ctx->X));  break;
        default: die("hashBitLen %d is not one of the precomputed settings", hashBitLen);
        }

    /* The chaining vars ctx->X are now initialized for the given hashBitLen. */
    /* Set up to process the data message portion of the hash (default) */
    Skein_Start_New_Type(ctx,MSG);              /* T0=0, T1= MSG type */

    return SKEIN_SUCCESS;
    }

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* process the input bytes */
int Skein_512_Update(Skein_512_Ctxt_t *ctx, const uint8_t *msg, size_t msgByteCnt)
    {
    size_t n;

    /* process full blocks, if any */
    if (msgByteCnt + ctx->h.bCnt > SKEIN_512_BLOCK_BYTES)
        {
        if (ctx->h.bCnt)                              /* finish up any buffered message data */
            {
            n = SKEIN_512_BLOCK_BYTES - ctx->h.bCnt;  /* # bytes free in buffer b[] */
            if (n)
                {
                memcpy(&ctx->b[ctx->h.bCnt],msg,n);
                msgByteCnt  -= n;
                msg         += n;
                ctx->h.bCnt += n;
                }
            Skein_512_Process_Block(ctx,ctx->b,1,SKEIN_512_BLOCK_BYTES);
            ctx->h.bCnt = 0;
            }
        /* now process any remaining full blocks, directly from input message data */
        if (msgByteCnt > SKEIN_512_BLOCK_BYTES)
            {
            n = (msgByteCnt-1) / SKEIN_512_BLOCK_BYTES;   /* number of full blocks to process */
            Skein_512_Process_Block(ctx,msg,n,SKEIN_512_BLOCK_BYTES);
            msgByteCnt -= n * SKEIN_512_BLOCK_BYTES;
            msg        += n * SKEIN_512_BLOCK_BYTES;
            }
        }

    /* copy any remaining source message data bytes into b[] */
    if (msgByteCnt)
        {
        memcpy(&ctx->b[ctx->h.bCnt],msg,msgByteCnt);
        ctx->h.bCnt += msgByteCnt;
        }

    return SKEIN_SUCCESS;
    }
   
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/* finalize the hash computation and output the result */
int Skein_512_Final(Skein_512_Ctxt_t *ctx, uint8_t *hashVal)
    {
    size_t i,n,byteCnt;
    uint64_t X[SKEIN_512_STATE_WORDS];

    ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;                 /* tag as the final block */
    if (ctx->h.bCnt < SKEIN_512_BLOCK_BYTES)            /* zero pad b[] if necessary */
        memset(&ctx->b[ctx->h.bCnt],0,SKEIN_512_BLOCK_BYTES - ctx->h.bCnt);

    Skein_512_Process_Block(ctx,ctx->b,1,ctx->h.bCnt);  /* process the final block */
    
    /* now output the result */
    byteCnt = (ctx->h.hashBitLen + 7) >> 3;             /* total number of output bytes */

    /* run Threefish in "counter mode" to generate output */
    memset(ctx->b,0,sizeof(ctx->b));  /* zero out b[], so it can hold the counter */
    memcpy(X,ctx->X,sizeof(X));       /* keep a local copy of counter mode "key" */
    for (i=0;i*SKEIN_512_BLOCK_BYTES < byteCnt;i++)
        {
        ((uint64_t *)ctx->b)[0]= htole64(i); /* build the counter block */
        Skein_Start_New_Type(ctx,OUT_FINAL);
        Skein_512_Process_Block(ctx,ctx->b,1,sizeof(uint64_t)); /* run "counter mode" */
        n = byteCnt - i*SKEIN_512_BLOCK_BYTES;   /* number of output bytes left to go */
        if (n >= SKEIN_512_BLOCK_BYTES)
            n  = SKEIN_512_BLOCK_BYTES;
        memcpy_htole64(hashVal+i*SKEIN_512_BLOCK_BYTES,ctx->X,n);   /* "output" the ctr mode bytes */
        Skein_Show_Final(512,&ctx->h,n,hashVal+i*SKEIN_512_BLOCK_BYTES);
        memcpy(ctx->X,X,sizeof(X));   /* restore the counter mode key for next time */
        }
    return SKEIN_SUCCESS;
    }

#if defined(SKEIN_CODE_SIZE) || defined(SKEIN_PERF)
size_t Skein_512_API_CodeSize(void)
    {
    return ((uint8_t *) Skein_512_API_CodeSize) -
           ((uint8_t *) Skein_512_Init);
    }
#endif
