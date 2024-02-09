#ifndef HEADER_GUARD_C5162833_6D01_493F_8EC0_1A5E3A4E66EC
#define HEADER_GUARD_C5162833_6D01_493F_8EC0_1A5E3A4E66EC

#include <setjmp.h>

#include "clowncommon/clowncommon.h"

#include "common.h"

typedef struct StateCommon
{
	ClownNemesis_InputCallback read_byte;
	void *read_byte_user_data;
	ClownNemesis_OutputCallback write_byte;
	void *write_byte_user_data;
	jmp_buf jump_buffer;
	cc_bool throw_on_eof;
} StateCommon;

int ReadByte(StateCommon *state);
void WriteByte(StateCommon *state, unsigned char byte);
void InitialiseCommon(StateCommon *state, ClownNemesis_InputCallback read_byte, const void *read_byte_user_data, ClownNemesis_OutputCallback write_byte, const void *write_byte_user_data);

#endif /* HEADER_GUARD_C5162833_6D01_493F_8EC0_1A5E3A4E66EC */
