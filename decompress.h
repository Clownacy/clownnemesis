#ifndef HEADER_GUARD_111EDE24_F9D8_44E2_A676_16DE5186D50E
#define HEADER_GUARD_111EDE24_F9D8_44E2_A676_16DE5186D50E

/* Should return -1 on error. */
typedef int (*ClownNemesis_InputCallback)(void *user_data);

/* Should return 0 on error. */
typedef int (*ClownNemesis_OutputCallback)(void *user_data, unsigned char byte);

/* Returns 0 on error. */
int ClownNemesis_Decompress(ClownNemesis_InputCallback read_byte, const void *read_byte_user_data, ClownNemesis_OutputCallback write_byte, const void *write_byte_user_data);

#endif /* HEADER_GUARD_111EDE24_F9D8_44E2_A676_16DE5186D50E */
