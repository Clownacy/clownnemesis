#include "decompress.h"

#include <setjmp.h>
#include <stddef.h>
#ifdef CLOWNNEMESIS_DEBUG
#include <stdio.h>
#endif
#include <string.h>

#include "common-internal.h"

#define MAXIMUM_CODE_BITS 8

typedef struct NybbleRun
{
	unsigned char total_code_bits;
	unsigned char value;
	unsigned char length;
#ifdef CLOWNNEMESIS_DEBUG
	unsigned int seen;
#endif
} NybbleRun;

typedef struct State
{
	StateCommon common;

	NybbleRun nybble_runs[1 << MAXIMUM_CODE_BITS];

	unsigned long output_buffer, previous_output_buffer;
	unsigned char output_buffer_nybbles_done;

	unsigned char xor_mode_enabled;
	unsigned short total_tiles;

	unsigned char bits_available;
	unsigned char bits_buffer;
} State;

static unsigned int PopBit(State* const state)
{
	state->bits_buffer <<= 1;

	if (state->bits_available == 0)
	{
		state->bits_available = 8;
		state->bits_buffer = ReadByte(&state->common);
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

static cc_bool NybbleRunExists(const NybbleRun* const nybble_run)
{
	return nybble_run->length != 0;
}

static const NybbleRun* FindCode(State* const state)
{
	unsigned int code, total_code_bits;

	code = total_code_bits = 0;

	for (;;)
	{
		if (total_code_bits == MAXIMUM_CODE_BITS)
		{
		#ifdef CLOWNNEMESIS_DEBUG
			fprintf(stderr, "Tried to find a code which did not exist (0x%X).\n", code);
		#endif
			longjmp(state->common.jump_buffer, 1);
		}

		code <<= 1;
		code |= PopBit(state);
		++total_code_bits;

		/* Detect inline data. */
		if (total_code_bits == 6 && code == 0x3F)
		{
			return NULL;
		}
		else
		{
			const NybbleRun* const nybble_run = &state->nybble_runs[code << (8 - total_code_bits)];

			if ((NybbleRunExists(nybble_run) && nybble_run->total_code_bits == total_code_bits))
				return nybble_run;
		}
	}
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
			WriteByte(&state->common, (final_output >> (4 - 1 - i) * 8) & 0xFF);

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
	const unsigned char header_byte_1 = ReadByte(&state->common);
	const unsigned char header_byte_2 = ReadByte(&state->common);
	const unsigned int header_word = (unsigned int)header_byte_1 << 8 | header_byte_2;

	state->xor_mode_enabled = (header_word & 0x8000) != 0;
	state->total_tiles = header_word & 0x7FFF;
}

static void ProcessCodeTable(State* const state)
{
	unsigned char byte, nybble_run_value;

	nybble_run_value = 0; /* Not necessary, but shuts up a compiler warning. */

	memset(state->nybble_runs, 0, sizeof(state->nybble_runs));

	byte = ReadByte(&state->common);

	while (byte != 0xFF)
	{
		if ((byte & 0x80) != 0)
		{
			nybble_run_value = byte & 0xF;
			byte = ReadByte(&state->common);
		}
		else
		{
			const unsigned char run_length = ((byte >> 4) & 7) + 1;
			const unsigned char total_code_bits = byte & 0xF;
			const unsigned char code = ReadByte(&state->common);
			const unsigned int nybble_run_index = (unsigned int)code << (8u - total_code_bits);
			NybbleRun* const nybble_run = &state->nybble_runs[nybble_run_index];

			if (total_code_bits > 8 || total_code_bits == 0 || nybble_run_index > CC_COUNT_OF(state->nybble_runs))
			{
			#ifdef CLOWNNEMESIS_DEBUG
				fputs("Invalid code table entry.\n", stderr);
			#endif
				longjmp(state->common.jump_buffer, 1);
			}

			nybble_run->total_code_bits = total_code_bits;
			nybble_run->value = nybble_run_value;
			nybble_run->length = run_length;

		#ifdef CLOWNNEMESIS_DEBUG
			{
				unsigned int i;

				fputs("Code ", stderr);

				for (i = 0; i < 8; ++i)
					fputc((code & 1 << (8 - 1 - i)) != 0 ? '1' : '0', stderr);
				
				fprintf(stderr, " of %d bits encodes nybble %X of length %d.\n", nybble_run->total_code_bits, nybble_run->value, nybble_run->length);
			}
		#endif

			byte = ReadByte(&state->common);
		}
	}
}

static void ProcessCodes(State* const state)
{
	unsigned long nybbles_remaining;
	unsigned int total_runs;

	nybbles_remaining = state->total_tiles * (8 * 8);
	total_runs = 0;

	while (nybbles_remaining != 0)
	{
		/* TODO: Undo this hack! */
		NybbleRun* const nybble_run = (NybbleRun*)FindCode(state);
		const unsigned int run_length = nybble_run != NULL ? nybble_run->length : PopBits(state, 3) + 1;
		const unsigned int nybble = nybble_run != NULL ? nybble_run->value : PopBits(state, 4);

		if (nybble_run != NULL)
		{
		#ifdef CLOWNNEMESIS_DEBUG
			fputs("Code", stderr);
			++nybble_run->seen;
		#endif
			++total_runs;
		}
	#ifdef CLOWNNEMESIS_DEBUG
		else
		{
			fputs("Reject", stderr);
		}

		fprintf(stderr, " found: nybble %X of length %d\n", nybble, run_length);
	#endif

		if (run_length > nybbles_remaining)
		{
		#ifdef CLOWNNEMESIS_DEBUG
			fputs("Data was longer than header declared.\n", stderr);
		#endif
			longjmp(state->common.jump_buffer, 1);
		}

		OutputNybbles(state, nybble, run_length);

		nybbles_remaining -= run_length;
	}

#ifdef CLOWNNEMESIS_DEBUG
	fprintf(stderr, "Total runs: %d\n", total_runs);
#endif
}

int ClownNemesis_Decompress(const ClownNemesis_InputCallback read_byte, const void* const read_byte_user_data, const ClownNemesis_OutputCallback write_byte, const void* const write_byte_user_data)
{
	int success;
	State state = {0};

	success = 0;

	InitialiseCommon(&state.common, read_byte, read_byte_user_data, write_byte, write_byte_user_data);

	if (!setjmp(state.common.jump_buffer))
	{
		ProcessHeader(&state);
		ProcessCodeTable(&state);
		ProcessCodes(&state);

	#ifdef CLOWNNEMESIS_DEBUG
		{
			unsigned int i;

			for (i = 0; i < 1 << 8; ++i)
			{
				NybbleRun* const nybble_run = &state.nybble_runs[i];

				if (NybbleRunExists(nybble_run))
				{
					unsigned int j;

					fputs("Code ", stderr);

					for (j = 0; j < nybble_run->total_code_bits; ++j)
						fputc((i & 1 << (8 - 1 - j)) != 0 ? '1' : '0', stderr);

					for (; j < 8; ++j)
						fputc(' ', stderr);
					
					fprintf(stderr, " encodes nybble %X of length %d and was seen %d times.\n", nybble_run->value, nybble_run->length, nybble_run->seen);
				}
			}
		}
	#endif

		success = 1;
	}

	return success;
}

#undef MAXIMUM_CODE_BITS
