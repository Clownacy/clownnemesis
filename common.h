#ifndef HEADER_GUARD_28ABC8E1_BE09_4B1B_8685_B7794936BF20
#define HEADER_GUARD_28ABC8E1_BE09_4B1B_8685_B7794936BF20

/* Return codes for the below functions. */
#define CLOWNNEMESIS_ERROR -1
#define CLOWNNEMESIS_EOF -2

typedef int (*ClownNemesis_InputCallback)(void *user_data);
typedef int (*ClownNemesis_OutputCallback)(void *user_data, unsigned char byte);

#endif /* HEADER_GUARD_28ABC8E1_BE09_4B1B_8685_B7794936BF20 */
