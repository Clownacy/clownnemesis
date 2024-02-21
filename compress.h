#ifndef HEADER_GUARD_3806BB18_BC2F_47C7_B9EF_8826358CB908
#define HEADER_GUARD_3806BB18_BC2F_47C7_B9EF_8826358CB908

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 0 on error. */
int ClownNemesis_Compress(int accurate, ClownNemesis_InputCallback read_byte, const void *read_byte_user_data, ClownNemesis_OutputCallback write_byte, const void *write_byte_user_data);

#ifdef __cplusplus
}
#endif

#endif /* HEADER_GUARD_3806BB18_BC2F_47C7_B9EF_8826358CB908 */
