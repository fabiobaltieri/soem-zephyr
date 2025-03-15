#ifndef _osal_defs_
#define _osal_defs_

// define if debug printf is needed
//#define EC_DEBUG

#ifdef EC_DEBUG
#define EC_PRINT printk
#else
#define EC_PRINT(...) do {} while (0)
#endif

#ifndef PACKED
#define PACKED_BEGIN
#define PACKED  __attribute__((__packed__))
#define PACKED_END
#endif

#endif
