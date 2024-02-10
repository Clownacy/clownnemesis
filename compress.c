#include "compress.h"

#ifdef CLOWNNEMESIS_DEBUG
#include <stdio.h>
#endif

#include "clowncommon/clowncommon.h"

#include "common-internal.h"

/* TODO: XOR mode. */
/* TODO: Use the package-merge algorithm to improve Huffman encoding. */

/* Select a particular encoding algorithm. */
/*#define SHANNON_CODING*/
#define FANO_CODING /* Currently the most efficient. */
/*#define HUFFMAN_CODING*/

#define MAXIMUM_RUN_NYBBLE 0x10
#define MAXIMUM_RUN_LENGTH 8

#if defined(SHANNON_CODING)
	#define CODE_GENERATOR_NYBBLE_DATA \
		unsigned int occurrances_accumulated;

	#define CODE_GENERATOR_STATE \
		unsigned int total_runs_with_codes;
#elif defined(FANO_CODING)
	#define CODE_GENERATOR_NYBBLE_DATA

	#define CODE_GENERATOR_STATE \
		NybbleRunsIndex nybble_runs_sorted; \
		unsigned char code; \
		unsigned char total_code_bits;

	typedef unsigned char NybbleRunsIndex[MAXIMUM_RUN_NYBBLE * MAXIMUM_RUN_LENGTH];
#elif defined(HUFFMAN_CODING)
	#include <stdlib.h>

	#define CODE_GENERATOR_NYBBLE_DATA

	#define CODE_GENERATOR_STATE \
		Node node_pool[MAXIMUM_RUN_NYBBLE * MAXIMUM_RUN_LENGTH * 2]; \
		unsigned int leaf_read_index; \
		unsigned int internal_read_index, internal_write_index; \
		unsigned char code; \
		unsigned char total_code_bits;

	typedef struct Node
	{
		cc_bool is_leaf;
		unsigned int occurrances;

		union
		{
			struct
			{
				unsigned char left_child, right_child;
			} internal;
			struct
			{
				NybbleRun *nybble_run;
			} leaf;
		} shared;
	} Node;
#endif

typedef struct NybbleRun
{
	unsigned int occurrances;
	CODE_GENERATOR_NYBBLE_DATA
	unsigned char code;
	unsigned char total_code_bits;
} NybbleRun;

typedef struct State
{
	StateCommon common;

	NybbleRun nybble_runs[MAXIMUM_RUN_NYBBLE][MAXIMUM_RUN_LENGTH];
	CODE_GENERATOR_STATE

	unsigned int total_runs;
	unsigned int nybbles_read;

	unsigned char previous_nybble;

	unsigned char input_nybble_buffer;
	unsigned char nybble_reader_flip_flop;

	unsigned char output_byte_buffer;
	unsigned char output_bits_done;
} State;

static int ReadNybble(State* const state)
{
	if (state->nybble_reader_flip_flop)
	{
		state->input_nybble_buffer <<= 4;
	}
	else
	{
		const int value = ReadByte(&state->common);

		if (value == CLOWNNEMESIS_EOF)
			return CLOWNNEMESIS_EOF;

		state->input_nybble_buffer = (unsigned char)value;
	}

	state->nybble_reader_flip_flop = !state->nybble_reader_flip_flop;
	++state->nybbles_read;

	return (state->input_nybble_buffer >> 4) & 0xF;
}

static void FindRuns(State* const state, void (*callback)(State *state, unsigned int nybble, unsigned int length))
{
	int new_nybble, previous_nybble, run_length;

	new_nybble = ReadNybble(state);
	run_length = 0;

	while (new_nybble != CLOWNNEMESIS_EOF)
	{
		++run_length;

		previous_nybble = new_nybble;
		new_nybble = ReadNybble(state);

		if (run_length == 8 || new_nybble != previous_nybble)
		{
			callback(state, previous_nybble, run_length);
			run_length = 0;
		}
	}
}

static void EmitHeader(State* const state)
{
	/* TODO: XOR mode. */
	const unsigned int nybbles_per_tile = 8 * 8;
	const unsigned int total_tiles = state->nybbles_read / nybbles_per_tile;

	if (state->nybbles_read % nybbles_per_tile != 0)
	{
	#ifdef CLOWNNEMESIS_DEBUG
		fputs("Input data size is not a multiple of 0x20 bytes.\n", stderr);
	#endif
		longjmp(state->common.jump_buffer, 1);
	}
	else if (total_tiles > 0x7FFF)
	{
	#ifdef CLOWNNEMESIS_DEBUG
		fputs("Input data is larger than the header allows.\n", stderr);
	#endif
		longjmp(state->common.jump_buffer, 1);
	}

	WriteByte(&state->common, (total_tiles >> 8) & 0xFF);
	WriteByte(&state->common, (total_tiles >> 0) & 0xFF);
}

static NybbleRun* NybbleRunFromIndex(State* const state, const unsigned int index)
{
	return &state->nybble_runs[index / CC_COUNT_OF(state->nybble_runs[0])][index % CC_COUNT_OF(state->nybble_runs[0])];
}

static void ComputeSortedRuns(State* const state, NybbleRunsIndex runs_reordered)
{
	unsigned int i;
	cc_bool not_done;

	for (i = 0; i < MAXIMUM_RUN_NYBBLE * MAXIMUM_RUN_LENGTH; ++i)
		runs_reordered[i] = i;

	/* Sort from most occurring to least occurring. */
	/* TODO: Anything better than bubblesort. */
	do
	{
		not_done = cc_false;

		for (i = 1; i < MAXIMUM_RUN_NYBBLE * MAXIMUM_RUN_LENGTH; ++i)
		{
			const NybbleRun* const previous_nybble_run = NybbleRunFromIndex(state, runs_reordered[i - 1]);
			const NybbleRun* const nybble_run = NybbleRunFromIndex(state, runs_reordered[i]);

			if (previous_nybble_run->occurrances < nybble_run->occurrances)
			{
				const unsigned int temp = runs_reordered[i - 1];
				runs_reordered[i - 1] = runs_reordered[i];
				runs_reordered[i] = temp;
				not_done = cc_true;
			}
		}
	}
	while (not_done);
}

static void IterateNybbleRuns(State* const state, void (*callback)(State *state, unsigned int run_nybble, unsigned int run_length))
{
	unsigned int i;

	for (i = 0; i < CC_COUNT_OF(state->nybble_runs); ++i)
	{
		unsigned int j;

		for (j = 0; j < CC_COUNT_OF(state->nybble_runs[i]); ++j)
			callback(state, i, j + 1);
	}
}

/******************/
/* Shannon Coding */
/******************/

#ifdef SHANNON_CODING

static unsigned int NextPowerOfTwo(unsigned int v)
{
	/* All hail the glorious bit-twiddler! */
	/* https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2 */
	/* We only care about 16-bit here. */
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v++;

	return v;
}

static unsigned int ComputeCodeLength(NybbleRun* const nybble_run, const unsigned int total_occurrances)
{
	/* This is a wacky integer-only way of computing '-log2(occurrances / total_runs)' rounded up the nearest integer. */
	const unsigned int code_length_pre_log2 = NextPowerOfTwo(CC_DIVIDE_CEILING(total_occurrances, nybble_run->occurrances));

	/* In Nemesis, the maximum code length is 8 bits. */
	if (code_length_pre_log2 > 1 << 8)
	{
		return 0;
	}
	else if (code_length_pre_log2 == 1 << 8)
	{
		return 8;
	}
	else
	{
		/* The -1 values are junk. */
		static const signed char nybble_to_bit_index[9] = {0, 0, 1, -1, 2, -1, -1, -1, 3};
		const unsigned int bit_offset = code_length_pre_log2 > 0xF ? 4 : 0;

		return bit_offset + nybble_to_bit_index[(code_length_pre_log2 >> bit_offset) & 0xF];
	}
}

static void ComputePreliminaryCodeLengths(State* const state, const unsigned int run_nybble, const unsigned int run_length)
{
	NybbleRun* const nybble_run = &state->nybble_runs[run_nybble][run_length - 1];

	/* Rare runs aren't added to the code table. */
	if (nybble_run->occurrances > 2)
	{
		const unsigned int code_length = ComputeCodeLength(nybble_run, state->total_runs);

		/* 0 means it's too big. */
		if (code_length != 0)
		{
			nybble_run->total_code_bits = code_length;
			state->total_runs_with_codes += nybble_run->occurrances;
		}
	}
}

static void ComputeAccumulatedOccurrances(State* const state)
{
	unsigned int i;
	NybbleRunsIndex runs_reordered;
	unsigned int occurrances_accumulator;

	ComputeSortedRuns(state, runs_reordered);

	/* Calculate the accumulated occurrances. */
	occurrances_accumulator = 0;

	for (i = 0; i < CC_COUNT_OF(runs_reordered); ++i)
	{
		NybbleRun* const nybble_run = NybbleRunFromIndex(state, runs_reordered[i]);

		if (nybble_run->total_code_bits != 0)
		{
			nybble_run->occurrances_accumulated = occurrances_accumulator;
			occurrances_accumulator += nybble_run->occurrances;
		}
	}
}

static void ComputeCode(State* const state, const unsigned int run_nybble, const unsigned int run_length)
{
	NybbleRun* const nybble_run = &state->nybble_runs[run_nybble][run_length - 1];

	if (nybble_run->total_code_bits != 0)
	{
		/* This length won't be too big since we culled the large ones earlier and these can only be smaller, not larger, than before. */
		const unsigned int code_length = ComputeCodeLength(nybble_run, state->total_runs_with_codes);
		const unsigned int code = (nybble_run->occurrances_accumulated << code_length) / state->total_runs_with_codes;

		if (code_length >= 6 && (code >> (code_length - 6) & 0x3F) == 0x3F)
		{
			/* Reject it; it's using a reserved bit string! */
			nybble_run->total_code_bits = 0;
		}
		else
		{
			nybble_run->code = code;
			nybble_run->total_code_bits = code_length;
		}
	}
}

static void ComputeCodesShannon(State* const state)
{
	/* Compute preliminate code lengths and total the runs that have valid code lengths.
	   We will use this total to produce even smaller codes later. */
	IterateNybbleRuns(state, ComputePreliminaryCodeLengths);

	ComputeAccumulatedOccurrances(state);

	/* Compute the codes using the above total and the accumulated occurrances. */
	IterateNybbleRuns(state, ComputeCode);
}

#endif

/*************************/
/* End of Shannon Coding */
/*************************/

/***************/
/* Fano Coding */
/***************/

#ifdef FANO_CODING

static unsigned int TotalValidRuns(State* const state)
{
	unsigned int total_valid_runs;
	unsigned int i;

	total_valid_runs = 0;

	for (i = 0; i < CC_COUNT_OF(state->nybble_runs_sorted); ++i)
	{
		NybbleRun* const nybble_run = NybbleRunFromIndex(state, state->nybble_runs_sorted[i]);

		/* We only want runs that occur 3 or more times. */
		if (nybble_run->occurrances < 3)
			break;

		total_valid_runs += nybble_run->occurrances;
	}

	return total_valid_runs;
}

static void DoSplit(State* const state, const unsigned int starting_sorted_nybble_run_index, const unsigned int total_occurrances)
{
	NybbleRun* const first_nybble_run = NybbleRunFromIndex(state, state->nybble_runs_sorted[starting_sorted_nybble_run_index]);

	/* If there is only one run left, then the code belongs to it. */
	if (first_nybble_run->occurrances == total_occurrances)
	{
		first_nybble_run->code = state->code;
		first_nybble_run->total_code_bits = state->total_code_bits;
	}
	/* Give up if we've reached the limit. */
	else if (state->total_code_bits != 8)
	{
		/* This performs a Fano binary split, splitting the list of probabilities into two roughly-equal halves and recursing. */
		unsigned int occurrance_accumulator;
		unsigned int sorted_nybble_run_index;

		const unsigned int halfway = total_occurrances / 2;

		occurrance_accumulator = 0;

		for (sorted_nybble_run_index = starting_sorted_nybble_run_index; ; ++sorted_nybble_run_index)
		{
			const NybbleRun* const nybble_run = NybbleRunFromIndex(state, state->nybble_runs_sorted[sorted_nybble_run_index]);
			const unsigned int occurrance_accumulator_next = occurrance_accumulator + nybble_run->occurrances;

			if (occurrance_accumulator_next > halfway)
			{
				const unsigned int delta_1 = halfway - occurrance_accumulator;
				const unsigned int delta_2 = occurrance_accumulator_next - halfway;

				const unsigned int split_occurrance = delta_1 < delta_2 ? occurrance_accumulator : occurrance_accumulator_next;
				const unsigned int split_index = delta_1 < delta_2 ? sorted_nybble_run_index : sorted_nybble_run_index + 1;

				/* Skip the reserved code (0x3F). */
				const cc_bool skip_reserved = state->total_code_bits == 5 && state->code == 0x1F;
				const unsigned int bits = skip_reserved ? 2 : 1;

				/* Extend the code. */
				state->code <<= bits;
				state->total_code_bits += bits;

				/* Do bit 0. */
			#ifdef CLOWNNEMESIS_DEBUG
				fprintf(stderr, "Splitting 0 %d/%d/%d.\n", starting_sorted_nybble_run_index, split_occurrance, total_occurrances);
			#endif
				DoSplit(state, starting_sorted_nybble_run_index, split_occurrance);

				/* Do bit 1. */
				state->code |= 1;

			#ifdef CLOWNNEMESIS_DEBUG
				fprintf(stderr, "Splitting 1 %d/%d/%d.\n", starting_sorted_nybble_run_index, split_occurrance, total_occurrances);
			#endif
				DoSplit(state, split_index, total_occurrances - split_occurrance);

				/* Revert. */
				state->code >>= bits;
				state->total_code_bits -= bits;

				break;
			}

			occurrance_accumulator = occurrance_accumulator_next;
		}
	}
}

static void ComputeCodesFano(State* const state)
{
	ComputeSortedRuns(state, state->nybble_runs_sorted);

	state->code = 0;
	state->total_code_bits = 0;
	DoSplit(state, 0, TotalValidRuns(state));
}

#endif

/**********************/
/* End of Fano Coding */
/**********************/

/******************/
/* Huffman Coding */
/******************/

#ifdef HUFFMAN_CODING

static void CreateLeafNode(State* const state, const unsigned int run_nybble, const unsigned int run_length)
{
	Node* const node = &state->node_pool[run_nybble * MAXIMUM_RUN_LENGTH + run_length - 1];
	NybbleRun* const nybble_run = &state->nybble_runs[run_nybble][run_length - 1];

	node->is_leaf = cc_true;
	node->occurrances = nybble_run->occurrances;
	node->shared.leaf.nybble_run = nybble_run;
}

static int CompareNodes(const void* const a, const void* const b)
{
	const Node* const node_a = (const Node*)a;
	const Node* const node_b = (const Node*)b;

	return node_a->occurrances - node_b->occurrances;
}

static unsigned int PopSmallestNode(State* const state)
{
	const unsigned int read_limit = MAXIMUM_RUN_NYBBLE * MAXIMUM_RUN_LENGTH;
	const unsigned int leaf_nodes_available = read_limit - state->leaf_read_index;
	const unsigned int internal_nodes_available = state->internal_write_index - state->internal_read_index;

	if (leaf_nodes_available != 0 && internal_nodes_available != 0)
	{
		/* Prefer leaf nodes when equal to avoid long codes. */
		if (state->node_pool[state->leaf_read_index].occurrances <= state->node_pool[state->internal_read_index].occurrances)
			return state->leaf_read_index++;
		else
			return state->internal_read_index++;
	}
	else if (leaf_nodes_available != 0)
	{
		return state->leaf_read_index++;
	}
	else if (internal_nodes_available != 0)
	{
		return state->internal_read_index++;
	}
	else
	{
		return -1;
	}
}

static const Node* ComputeHuffmanTree(State* const state)
{
	/* TODO: Handle there being no codeable nodes. */

	/* Welcome to Hell. */
	state->internal_read_index = state->internal_write_index = MAXIMUM_RUN_NYBBLE * MAXIMUM_RUN_LENGTH;

	for (;;)
	{
		const unsigned int right_child = PopSmallestNode(state);
		const unsigned int left_child = PopSmallestNode(state);

		/* Is there is only one node left, then it is our root node. */
		if (left_child == (unsigned int)-1)
			return &state->node_pool[right_child];

		state->node_pool[state->internal_write_index].is_leaf = cc_false;
		state->node_pool[state->internal_write_index].occurrances = state->node_pool[left_child].occurrances + state->node_pool[right_child].occurrances;
		state->node_pool[state->internal_write_index].shared.internal.left_child = left_child;
		state->node_pool[state->internal_write_index].shared.internal.right_child = right_child;
		++state->internal_write_index;
	}
}

static void RecurseNode(State* const state, const Node* const node)
{
	if (node->is_leaf)
	{
		NybbleRun* const nybble_run = node->shared.leaf.nybble_run;

		/* Prevent conflicting with the reserved code (0x3F). */
		if (state->code == (1 << state->total_code_bits) - 1)
		{
			if (state->total_code_bits != 8)
			{
				nybble_run->code = state->code << 1;
				nybble_run->total_code_bits = state->total_code_bits + 1;
			}
		}
		else
		{
			nybble_run->code = state->code;
			nybble_run->total_code_bits = state->total_code_bits;
		}
	}
	/* Give up if we've reached the limit. */
	else if (state->total_code_bits != 8)
	{
		const cc_bool skip_reserved = state->total_code_bits == 5 && state->code == 0x1F;

		/* Extend the code. */
		state->code <<= 1;
		++state->total_code_bits;

		/* Do bit 0. */
		RecurseNode(state, &state->node_pool[node->shared.internal.left_child]);

		/* Do bit 1. */
		state->code |= 1;

		/* Prevent conflicting with the reserved code (0x3F). */
		if (skip_reserved)
		{
			state->code <<= 1;
			++state->total_code_bits;
		}

		RecurseNode(state, &state->node_pool[node->shared.internal.right_child]);

		if (skip_reserved)
		{
			state->code >>= 1;
			--state->total_code_bits;
		}

		/* Revert. */
		state->code >>= 1;
		--state->total_code_bits;
	}
}

static void ComputeCodesHuffman(State* const state)
{
	IterateNybbleRuns(state, CreateLeafNode);

	/* TODO: A stable sorting function. */
	qsort(state->node_pool, MAXIMUM_RUN_NYBBLE * MAXIMUM_RUN_LENGTH, sizeof(Node), CompareNodes);

	/* TODO: What if there aren't any? */
	/* Find the first node with a decent probability. */
	while (state->node_pool[state->leaf_read_index].occurrances < 3)
		++state->leaf_read_index;

	state->code = state->total_code_bits = 0;
	RecurseNode(state, ComputeHuffmanTree(state));
}

#endif

/*************************/
/* End of Huffman Coding */
/*************************/

static void EmitCodeTableEntry(State* const state, const unsigned int run_nybble, const unsigned int run_length)
{
	NybbleRun* const nybble_run = &state->nybble_runs[run_nybble][run_length - 1];

	if (nybble_run->total_code_bits != 0)
	{
	#ifdef CLOWNNEMESIS_DEBUG
		fprintf(stderr, "Run of nybble %X of length %d occurred %d times (code is ", run_nybble, run_length, nybble_run->occurrances);

		{
			unsigned int i;
			for (i = 0; i < 8; ++i)
				fputc((nybble_run->code & 1 << (8 - 1 - i)) != 0 ? '1' : '0', stderr);
		}

		fprintf(stderr, ", length is %d).\n", nybble_run->total_code_bits);
	#endif

		if (run_nybble != state->previous_nybble)
		{
			state->previous_nybble = run_nybble;
			WriteByte(&state->common, 0x80 | run_nybble);
		}

		WriteByte(&state->common, (run_length - 1) << 4 | nybble_run->total_code_bits);
		WriteByte(&state->common, nybble_run->code);
	}
}

static void EmitCodeTable(State* const state)
{
#if defined(SHANNON_CODING)
	ComputeCodesShannon(state);
#elif defined(FANO_CODING)
	ComputeCodesFano(state);
#elif defined(HUFFMAN_CODING)
	ComputeCodesHuffman(state);
#endif

	/* Finally, emit the code table. */
	state->previous_nybble = 0xFF; /* Deliberately invalid. */
	IterateNybbleRuns(state, EmitCodeTableEntry);

	/* Mark the end of the code table. */
	WriteByte(&state->common, 0xFF);

#ifdef CLOWNNEMESIS_DEBUG
	fprintf(stderr, "Total runs: %d\n", state->total_runs);
#ifdef SHANNON_CODING
	fprintf(stderr, "Total runs with codes: %d\n", state->total_runs_with_codes);
#endif
#endif
}

static void LogOccurrance(State* const state, const unsigned int nybble, const unsigned int length)
{
	++state->nybble_runs[nybble][length - 1].occurrances;
	++state->total_runs;
}

static void WriteBit(State* const state, const unsigned int bit)
{
	state->output_byte_buffer <<= 1;
	state->output_byte_buffer |= bit;

	if (++state->output_bits_done == 8)
	{
		state->output_bits_done = 0;
		WriteByte(&state->common, state->output_byte_buffer & 0xFF);
	}
}

static void WriteBits(State* const state, const unsigned int bits, const unsigned int total_bits)
{
	unsigned int i;

	for (i = 0; i < total_bits; ++i)
		WriteBit(state, (bits & (1 << (total_bits - 1 - i))) != 0);
}

static void EmitCode(State* const state, const unsigned int nybble, const unsigned int length)
{
	const NybbleRun* const nybble_run = &state->nybble_runs[nybble][length - 1];

	if (nybble_run->total_code_bits != 0)
	{
	#ifdef CLOWNNEMESIS_DEBUG
		fprintf(stderr, "Emitting code %X of length %d for nybble %X of length %d.\n", nybble_run->code, nybble_run->total_code_bits, nybble, length);
	#endif

		WriteBits(state, nybble_run->code, nybble_run->total_code_bits);
	}
	else
	{
	#ifdef CLOWNNEMESIS_DEBUG
		fprintf(stderr, "Emitting reject for nybble %X of length %d.\n", nybble, length);
	#endif

		/* This run doesn't have a code, so inline it. */
		WriteBits(state, 0x3F, 6);
		WriteBits(state, length - 1, 3);
		WriteBits(state, nybble, 4);
	}
}

static void EmitCodes(State* const state)
{
	FindRuns(state, EmitCode);

	/* Output any codes that haven't yet been flushed. */
	if (state->output_bits_done != 0)
		WriteByte(&state->common, (state->output_byte_buffer << (8 - state->output_bits_done)) & 0xFF);
}

int ClownNemesis_Compress(const ClownNemesis_InputCallback read_byte, const void* const read_byte_user_data, const ClownNemesis_OutputCallback write_byte, const void* const write_byte_user_data)
{
	int success;
	State state = {0};

	success = 0;

	InitialiseCommon(&state.common, read_byte, read_byte_user_data, write_byte, write_byte_user_data);
	state.common.throw_on_eof = cc_false;

	if (!setjmp(state.common.jump_buffer))
	{
		FindRuns(&state, LogOccurrance);
		EmitHeader(&state);
		EmitCodeTable(&state);
		EmitCodes(&state);

		success = 1;
	}

	return success;
}
