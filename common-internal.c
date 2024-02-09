#include "common-internal.h"

int ReadByte(StateCommon* const state)
{
	const int value = state->read_byte(state->read_byte_user_data);

	if (value == CLOWNNEMESIS_ERROR || (state->throw_on_eof && value == CLOWNNEMESIS_EOF))
		longjmp(state->jump_buffer, 1);

	return value;
}

void WriteByte(StateCommon* const state, const unsigned char byte)
{
	if (state->write_byte(state->write_byte_user_data, byte) == CLOWNNEMESIS_ERROR)
		longjmp(state->jump_buffer, 1);
}

void InitialiseCommon(StateCommon* const state, const ClownNemesis_InputCallback read_byte, const void* const read_byte_user_data, const ClownNemesis_OutputCallback write_byte, const void* const write_byte_user_data)
{
	state->read_byte = read_byte;
	state->read_byte_user_data = (void*)read_byte_user_data;
	state->write_byte = write_byte;
	state->write_byte_user_data = (void*)write_byte_user_data;

	state->throw_on_eof = cc_true;
}
