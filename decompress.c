#include "decompress.h"

#include <setjmp.h>
#include <stddef.h>
#include <string.h>

#define MAXIMUM_CODE_BITS 8

typedef struct NybbleRun
{
	unsigned char exists;
	unsigned char total_code_bits;
	unsigned char value;
	unsigned char length;
} NybbleRun;

typedef struct State
{
	unsigned long output_buffer, previous_output_buffer;
	unsigned char output_buffer_nybbles_done;

	unsigned char xor_mode_enabled;
	unsigned short total_tiles;

	unsigned char bits_available;
	unsigned char bits_buffer;

	jmp_buf jump_buffer;
	ClownNemesis_InputCallback read_byte;
	void *read_byte_user_data;
	ClownNemesis_OutputCallback write_byte;
	void *write_byte_user_data;

	NybbleRun nybble_runs[1 << MAXIMUM_CODE_BITS];
} State;

static unsigned char ReadByte(State* const state)
{
	const int value = state->read_byte(state->read_byte_user_data);

	if (value == -1)
		longjmp(state->jump_buffer, 1);

	return value;
}

static void WriteByte(State* const state, const unsigned char byte)
{
	if (!state->write_byte(state->write_byte_user_data, byte))
		longjmp(state->jump_buffer, 1);
}

static unsigned int PopBit(State* const state)
{
	state->bits_buffer <<= 1;

	if (state->bits_available == 0)
	{
		state->bits_available = 8;
		state->bits_buffer = ReadByte(state);
	}

	--state->bits_available;

	return (state->bits_buffer & 0x80) != 0;
}

static unsigned int PopBits(State* const state, const unsigned int total_bits)
{
	unsigned int value;
	unsigned int i;

	value = 0;

	for (i = 0; i < total_bits; ++i)
	{
		value <<= 1;
		value |= PopBit(state);
	}

	return value;
}

static const NybbleRun* FindCode(State* const state)
{
	unsigned int code, total_code_bits;
	const NybbleRun *nybble_run;

	code = total_code_bits = 0;

	do
	{
		if (total_code_bits == MAXIMUM_CODE_BITS)
			longjmp(state->jump_buffer, 1);

		code <<= 1;
		code |= PopBit(state);
		++total_code_bits;
		nybble_run = &state->nybble_runs[code];
	}
	while (!((nybble_run->exists && nybble_run->total_code_bits == total_code_bits) || code == 0x3F));

	return code == 0x3F ? NULL : nybble_run;
}

static void OutputNybble(State* const state, const unsigned int nybble)
{
	state->output_buffer <<= 4;
	state->output_buffer |= nybble;

	if ((++state->output_buffer_nybbles_done & 7) == 0)
	{
		unsigned int i;

		const unsigned long final_output = state->output_buffer ^ (state->xor_mode_enabled ? state->previous_output_buffer : 0);

		for (i = 0; i < 4; ++i)
			WriteByte(state, (final_output >> (4 - 1 - i) * 8) & 0xFF);

		state->previous_output_buffer = final_output;
	}
}

static void OutputNybbles(State* const state, const unsigned int nybble, const unsigned int total_nybbles)
{
	unsigned int i;

	for (i = 0; i < total_nybbles; ++i)
		OutputNybble(state, nybble);
}

static void ProcessHeader(State* const state)
{
	const unsigned char header_byte_1 = ReadByte(state);
	const unsigned char header_byte_2 = ReadByte(state);
	const unsigned int header_word = (unsigned int)header_byte_1 << 8 | header_byte_2;

	state->xor_mode_enabled = (header_word & 0x8000) != 0;
	state->total_tiles = header_word & 0x7FFF;
}

static void GenerateCodeTable(State* const state)
{
	unsigned char byte, nybble_run_value;

	memset(state->nybble_runs, 0, sizeof(state->nybble_runs));

	byte = ReadByte(state);
	nybble_run_value = 0; /* Not necessary, but shuts up a compiler warning. */

	while (byte != 0xFF)
	{
		if ((byte & 0x80) != 0)
		{
			nybble_run_value = byte & 0xF;
			byte = ReadByte(state);
		}
		else
		{
			const unsigned char run_length = ((byte >> 4) & 7) + 1;
			const unsigned char total_code_bits = byte & 0xF;
			const unsigned char code = ReadByte(state);

			NybbleRun* const nybble_run = &state->nybble_runs[code];
			nybble_run->exists = 1;
			nybble_run->total_code_bits = total_code_bits;
			nybble_run->value = nybble_run_value;
			nybble_run->length = run_length;

			byte = ReadByte(state);
		}
	}
}

static void ProcessCodes(State* const state)
{
	unsigned long nybbles_remaining;

	nybbles_remaining = state->total_tiles * (8 * 8);

	while (nybbles_remaining != 0)
	{
		const NybbleRun* const nybble_run = FindCode(state);
		const unsigned int run_length = nybble_run != NULL ? nybble_run->length : PopBits(state, 3) + 1;
		const unsigned int nybble = nybble_run != NULL ? nybble_run->value : PopBits(state, 4);

		if (run_length > nybbles_remaining)
			longjmp(state->jump_buffer, 1);

		OutputNybbles(state, nybble, run_length);

		nybbles_remaining -= run_length;
	}
}

int ClownNemesis_Decompress(const ClownNemesis_InputCallback read_byte, const void* const read_byte_user_data, const ClownNemesis_OutputCallback write_byte, const void* const write_byte_user_data)
{
	int success;
	State state = {0};

	success = 0;

	state.read_byte = read_byte;
	state.read_byte_user_data = (void*)read_byte_user_data;
	state.write_byte = write_byte;
	state.write_byte_user_data = (void*)write_byte_user_data;

	if (!setjmp(state.jump_buffer))
	{
		ProcessHeader(&state);
		GenerateCodeTable(&state);
		ProcessCodes(&state);

		success = 1;
	}

	return success;
}

#undef MAXIMUM_CODE_BITS
