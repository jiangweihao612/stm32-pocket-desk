/*------------------------------------------------------------------------*/
/* ASCII only code page converter for FatFs                               */
/*------------------------------------------------------------------------*/
/* ff.c needs ff_convert() and ff_wtoupper() because _CODE_PAGE is 936 and
 * the LFN feature is on.  The launcher only ever touches 8.3 file names
 * (PH0001.BMP ...), so the full GBK pair table is not needed here.  Using
 * this file instead of mycc936.c also avoids pulling in the W25Q font
 * drivers that the table lives in on the ALIENTEK board.                  */

#include "ff.h"

WCHAR ff_convert (WCHAR src, UINT dir)
{
    (void)dir;

    if (src < 0x80) return src;     /* plain ASCII passes through */

    return 0;                       /* everything else: not supported */
}

WCHAR ff_wtoupper (WCHAR chr)
{
    if (chr >= 'a' && chr <= 'z') return (WCHAR)(chr - 32);

    return chr;
}
