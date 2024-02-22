#include "compress.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>

#ifdef CLOWNNEMESIS_DEBUG
#include <stdio.h>
#endif

#include "clowncommon/clowncommon.h"

#include "common-internal.h"

#define MAXIMUM_RUN_NYBBLE 0x10
#define MAXIMUM_RUN_LENGTH 8

#define TOTAL_SYMBOLS (MAXIMUM_RUN_NYBBLE * MAXIMUM_RUN_LENGTH)
#define MAXIMUM_BITS 8

typedef unsigned char NybbleRunsIndex[TOTAL_SYMBOLS];

typedef struct InternalBufferIndices
{
	unsigned int read_index, write_index;
} InternalBufferIndices;

typedef struct Node
{
	cc_bool is_leaf;
	unsigned int occurrences;

	union
	{
		struct
		{
			unsigned short left_child, right_child;
		} internal;
		struct
		{
			struct NybbleRun *nybble_run;
		} leaf;
	} shared;
} Node;

typedef struct NybbleRun
{
	unsigned int occurrences;
	unsigned char code;
	unsigned char total_code_bits;
} NybbleRun;

typedef struct State
{
	StateCommon common;

	NybbleRun nybble_runs[MAXIMUM_RUN_NYBBLE][MAXIMUM_RUN_LENGTH];
	union
	{
		struct
		{
			NybbleRunsIndex nybble_runs_sorted;
			unsigned char code;
			unsigned char total_code_bits;
		} fano;
		struct
		{
			/* Contains the leaf nodes (size: TOTAL_SYMBOLS), and two queues for the internal nodes (size: TOTAL_SYMBOLS * MAXIMUM_BITS). */
			Node node_pool[TOTAL_SYMBOLS + TOTAL_SYMBOLS * MAXIMUM_BITS * 2];
			unsigned int leaf_read_index;
			InternalBufferIndices internal[2];
			unsigned char internal_buffer_flip_flop;
		} huffman;
	} generator;

	unsigned int total_runs;
	unsigned int bytes_read;
	unsigned int total_bits;

	unsigned char previous_nybble;

	unsigned char input_byte_buffer[4];
	unsigned char input_byte_buffer_index;
	unsigned char input_nybble_buffer;
	unsigned char nybble_reader_flip_flop;

	unsigned char output_byte_buffer;
	unsigned char output_bits_done;

	cc_bool xor_mode_enabled;
} State;

/* TODO: Just replace this with using direct pointers. */
static NybbleRun* NybbleRunFromIndex(State* const state, const unsigned int index)
{
	return &state->nybble_runs[index % CC_COUNT_OF(state->nybble_runs)][index / CC_COUNT_OF(state->nybble_runs)];
}

static cc_bool ComparisonOccurrence(const NybbleRun* const nybble_run_1, const NybbleRun* const nybble_run_2)
{
	return nybble_run_1->occurrences > nybble_run_2->occurrences;
}

static void ComputeSortedRuns(State* const state, NybbleRunsIndex runs_reordered, cc_bool (*comparison)(const NybbleRun *nybble_run_1, const NybbleRun *nybble_run_2))
{
	unsigned int i;
	cc_bool not_done;

	for (i = 0; i < TOTAL_SYMBOLS; ++i)
		runs_reordered[i] = i;

	/* Sort from most occurring to least occurring. */
	/* This needs to be a stable sorting algorithm so that the Fano algorithm matches Sega's compressor. */
	/* TODO: Did Sega's compressor actually use a stable sort? */
	/* TODO: Anything better than bubblesort. */
	do
	{
		not_done = cc_false;

		for (i = 1; i < TOTAL_SYMBOLS; ++i)
		{
			const NybbleRun* const previous_nybble_run = NybbleRunFromIndex(state, runs_reordered[i - 1]);
			const NybbleRun* const nybble_run = NybbleRunFromIndex(state, runs_reordered[i]);

			if (comparison(nybble_run, previous_nybble_run))
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

static void IterateNybbleRuns(State* const state, void (* const callback)(State *state, unsigned int run_nybble, unsigned int run_length_minus_one))
{
	unsigned int i;

	for (i = 0; i < CC_COUNT_OF(state->nybble_runs); ++i)
	{
		unsigned int j;

		for (j = 0; j < CC_COUNT_OF(state->nybble_runs[i]); ++j)
			callback(state, i, j);
	}
}

static void SumTotalBits(State* const state, const unsigned int run_nybble, const unsigned int run_length_minus_one)
{
	NybbleRun* const nybble_run = &state->nybble_runs[run_nybble][run_length_minus_one];

	if (nybble_run->total_code_bits != 0)
	{
		cc_bool is_the_first;
		unsigned int i;

		/* Find out if this will have the first code table entry with this nybble. */
		is_the_first = cc_true;

		for (i = 0; i < run_length_minus_one; ++i)
		{
			if (state->nybble_runs[run_nybble][i].total_code_bits != 0)
			{
				is_the_first = cc_false;
				break;
			}
		}

		/* The code table entry uses either 16 bits or 24 bits depending on whether it's the first with its nybble. */
		state->total_bits += is_the_first ? 24 : 16;
		state->total_bits += nybble_run->total_code_bits * nybble_run->occurrences;
	}
	else
	{
		/* An inlined nybble runs costs 13 bits. */
		state->total_bits += (6 + 3 + 4) * nybble_run->occurrences;
	}
}

static void ComputeTotalEncodedBits(State* const state)
{
	state->total_bits = 0;
	IterateNybbleRuns(state, SumTotalBits);
}

/***************/
/* Fano Coding */
/***************/

/* https://en.wikipedia.org/wiki/Shannon%E2%80%93Fano_coding */

static unsigned int TotalValidRuns(State* const state)
{
	unsigned int total_valid_runs;
	unsigned int i;

	total_valid_runs = 0;

	for (i = 0; i < CC_COUNT_OF(state->generator.fano.nybble_runs_sorted); ++i)
	{
		NybbleRun* const nybble_run = NybbleRunFromIndex(state, state->generator.fano.nybble_runs_sorted[i]);

		/* We only want runs that occur 3 or more times. */
		if (nybble_run->occurrences < 3)
			break;

		total_valid_runs += nybble_run->occurrences;
	}

	return total_valid_runs;
}

static void DoSplit(State* const state, const unsigned int starting_sorted_nybble_run_index, const unsigned int total_occurrences)
{
	NybbleRun* const first_nybble_run = NybbleRunFromIndex(state, state->generator.fano.nybble_runs_sorted[starting_sorted_nybble_run_index]);

	/* If there is only one run left, then the code belongs to it. */
	if (first_nybble_run->occurrences == total_occurrences)
	{
		first_nybble_run->code = state->generator.fano.code;
		first_nybble_run->total_code_bits = state->generator.fano.total_code_bits;

		/* Avoid forming a prefix of the reserved code (0x3F). */
		if (state->generator.fano.code == (1u << state->generator.fano.total_code_bits) - 1)
		{
			first_nybble_run->code <<= 1;
			++first_nybble_run->total_code_bits;
		}
	}
	/* Give up if we've reached the limit. */
	else if (state->generator.fano.total_code_bits != 8)
	{
		/* This performs a Fano binary split, splitting the list of probabilities into two roughly-equal halves and recursing. */
		unsigned int occurrence_accumulator;
		unsigned int sorted_nybble_run_index;

		const unsigned int halfway = total_occurrences / 2;

		occurrence_accumulator = 0;

		for (sorted_nybble_run_index = starting_sorted_nybble_run_index; ; ++sorted_nybble_run_index)
		{
			const NybbleRun* const nybble_run = NybbleRunFromIndex(state, state->generator.fano.nybble_runs_sorted[sorted_nybble_run_index]);
			const unsigned int occurrence_accumulator_next = occurrence_accumulator + nybble_run->occurrences;

			if (occurrence_accumulator_next > halfway)
			{
				const unsigned int delta_1 = halfway - occurrence_accumulator;
				const unsigned int delta_2 = occurrence_accumulator_next - halfway;

				const unsigned int split_occurrence = delta_1 < delta_2 ? occurrence_accumulator : occurrence_accumulator_next;
				const unsigned int split_index = delta_1 < delta_2 ? sorted_nybble_run_index : sorted_nybble_run_index + 1;

				/* Skip the reserved code (0x3F). */
				const cc_bool skip_reserved = state->generator.fano.total_code_bits == 5 && state->generator.fano.code == 0x1F;
				const unsigned int bits = skip_reserved ? 2 : 1;

				/* Extend the code. */
				state->generator.fano.code <<= bits;
				state->generator.fano.total_code_bits += bits;

				/* Do bit 0. */
			#ifdef CLOWNNEMESIS_DEBUG
				fprintf(stderr, "Splitting 0 %d/%d/%d.\n", starting_sorted_nybble_run_index, split_occurrence, total_occurrences);
			#endif
				DoSplit(state, starting_sorted_nybble_run_index, split_occurrence);

				/* Do bit 1. */
				state->generator.fano.code |= 1;

			#ifdef CLOWNNEMESIS_DEBUG
				fprintf(stderr, "Splitting 1 %d/%d/%d.\n", starting_sorted_nybble_run_index, split_occurrence, total_occurrences);
			#endif
				DoSplit(state, split_index, total_occurrences - split_occurrence);

				/* Revert. */
				state->generator.fano.code >>= bits;
				state->generator.fano.total_code_bits -= bits;

				break;
			}

			occurrence_accumulator = occurrence_accumulator_next;
		}
	}
}

static void ComputeCodesFano(State* const state)
{
	ComputeSortedRuns(state, state->generator.fano.nybble_runs_sorted, ComparisonOccurrence);

	state->generator.fano.code = 0;
	state->generator.fano.total_code_bits = 0;
	DoSplit(state, 0, TotalValidRuns(state));

	/* As an optimisation, the computed codes are sorted by their nybble runs' occurrences. This assigns the shorter codes to the more-common nybble runs. */
	/* Sega's compressor did the same thing. */
	{
	unsigned int i;

	/* Sort from most occurring to least occurring. */
	/* In order to match Sega's compressor, we use a Selection Sort here. */
	for (i = 0; i < TOTAL_SYMBOLS - 1; ++i)
	{
		unsigned int smallest_code_size;
		unsigned int smallest_code_index;
		unsigned int j;

		NybbleRun* const nybble_run = NybbleRunFromIndex(state, state->generator.fano.nybble_runs_sorted[i]);
		const unsigned int total_code_bits = nybble_run->total_code_bits == 0 ? UINT_MAX : nybble_run->total_code_bits;

		smallest_code_size = total_code_bits;
		smallest_code_index = i;

		for (j = i + 1; j < TOTAL_SYMBOLS; ++j)
		{
			NybbleRun* const later_nybble_run = NybbleRunFromIndex(state, state->generator.fano.nybble_runs_sorted[j]);
			const unsigned int later_total_code_bits = later_nybble_run->total_code_bits == 0 ? UINT_MAX : later_nybble_run->total_code_bits;

			if (later_total_code_bits < smallest_code_size)
			{
				smallest_code_size = later_total_code_bits;
				smallest_code_index = j;
			}
		}

		if (smallest_code_index != i)
		{
			NybbleRun* const later_nybble_run = NybbleRunFromIndex(state, state->generator.fano.nybble_runs_sorted[smallest_code_index]);

			const unsigned char code = later_nybble_run->code;
			const unsigned char total_code_bits = later_nybble_run->total_code_bits;
			later_nybble_run->code = nybble_run->code;
			later_nybble_run->total_code_bits = nybble_run->total_code_bits;
			nybble_run->code = code;
			nybble_run->total_code_bits = total_code_bits;
		}
	}
	}
}

/**********************/
/* End of Fano Coding */
/**********************/

/******************/
/* Huffman Coding */
/******************/

/* https://en.wikipedia.org/wiki/Huffman_coding */
/* https://en.wikipedia.org/wiki/Package-merge_algorithm */
/* https://create.stephan-brumme.com/length-limited-prefix-codes/ */
/* https://experiencestack.co/length-limited-huffman-codes-21971f021d43 */

static void CreateLeafNode(State* const state, const unsigned int run_nybble, const unsigned int run_length_minus_one)
{
	Node* const node = &state->generator.huffman.node_pool[run_nybble * MAXIMUM_RUN_LENGTH + run_length_minus_one];
	NybbleRun* const nybble_run = &state->nybble_runs[run_nybble][run_length_minus_one];

	node->is_leaf = cc_true;
	node->occurrences = nybble_run->occurrences;
	node->shared.leaf.nybble_run = nybble_run;
}

static int CompareNodes(const void* const a, const void* const b)
{
	const Node* const node_a = (const Node*)a;
	const Node* const node_b = (const Node*)b;

	return node_a->occurrences - node_b->occurrences;
}

static unsigned int PopSmallestNode(State* const state)
{
	InternalBufferIndices* const input_internal = &state->generator.huffman.internal[state->generator.huffman.internal_buffer_flip_flop];
	const unsigned int leaf_nodes_available = TOTAL_SYMBOLS - state->generator.huffman.leaf_read_index;
	const unsigned int internal_nodes_available = input_internal->write_index - input_internal->read_index;

	if (leaf_nodes_available != 0 && internal_nodes_available != 0)
	{
		if (state->generator.huffman.node_pool[state->generator.huffman.leaf_read_index].occurrences <= state->generator.huffman.node_pool[input_internal->read_index].occurrences)
			return state->generator.huffman.leaf_read_index++;
		else
			return input_internal->read_index++;
	}
	else if (leaf_nodes_available != 0)
	{
		return state->generator.huffman.leaf_read_index++;
	}
	else if (internal_nodes_available != 0)
	{
		return input_internal->read_index++;
	}
	else
	{
		return -1;
	}
}

static void ComputeTrees(State* const state)
{
	unsigned int iterations_done;

	const unsigned int starting_leaf_node_index = state->generator.huffman.leaf_read_index;

	/* Welcome to Hell. */
	/* This is the heart of the package-merge algorithm. */
	state->generator.huffman.internal[0].read_index = state->generator.huffman.internal[0].write_index = TOTAL_SYMBOLS;
	state->generator.huffman.internal[1].read_index = state->generator.huffman.internal[1].write_index = TOTAL_SYMBOLS + TOTAL_SYMBOLS * MAXIMUM_BITS;

	iterations_done = 0;

	for (;;)
	{
		InternalBufferIndices* const output_internal = &state->generator.huffman.internal[state->generator.huffman.internal_buffer_flip_flop ^ 1];

		const unsigned int right_child = PopSmallestNode(state);
		const unsigned int left_child = PopSmallestNode(state);

		if (left_child == (unsigned int)-1 || right_child == (unsigned int)-1)
		{
			state->generator.huffman.leaf_read_index = starting_leaf_node_index;

			state->generator.huffman.internal_buffer_flip_flop ^= 1;

			if (++iterations_done == 7)
				break;

			continue;
		}

		state->generator.huffman.node_pool[output_internal->write_index].is_leaf = cc_false;
		state->generator.huffman.node_pool[output_internal->write_index].occurrences = state->generator.huffman.node_pool[left_child].occurrences + state->generator.huffman.node_pool[right_child].occurrences;
		state->generator.huffman.node_pool[output_internal->write_index].shared.internal.left_child = left_child;
		state->generator.huffman.node_pool[output_internal->write_index].shared.internal.right_child = right_child;
		++output_internal->write_index;
	}
}

static void ResetCodeLength(State* const state, const unsigned int run_nybble, const unsigned int run_length_minus_one)
{
	NybbleRun* const nybble_run = &state->nybble_runs[run_nybble][run_length_minus_one];

	nybble_run->total_code_bits = 0;
}

static void RecurseNode(State* const state, const Node* const node)
{
	if (node->is_leaf)
	{
		++node->shared.leaf.nybble_run->total_code_bits;
	}
	else
	{
		RecurseNode(state, &state->generator.huffman.node_pool[node->shared.internal.left_child]);
		RecurseNode(state, &state->generator.huffman.node_pool[node->shared.internal.right_child]);
	}
}

static void ComputeCodeLengths(State* const state)
{
	unsigned int i;

	InternalBufferIndices* const internal_buffer = &state->generator.huffman.internal[state->generator.huffman.internal_buffer_flip_flop];

	/* Reset the code lengths to 0, just in case. */
	IterateNybbleRuns(state, ResetCodeLength);

	/* Recurse through the generated trees and increment the code length of each leaf encountered. */
	for (i = internal_buffer->read_index; i < internal_buffer->write_index; ++i)
		RecurseNode(state, &state->generator.huffman.node_pool[i]);

	/* I wish I knew why this is necessary, but I don't. Without this, data with few unique nybble runs will be entirely inlined. */
	/* TODO: Figure out what is going on here. */
	for (i = state->generator.huffman.leaf_read_index; i < TOTAL_SYMBOLS; ++i)
	{
		NybbleRun* const nybble_run = state->generator.huffman.node_pool[i].shared.leaf.nybble_run;

		/* Without this check, codes will always be one bit too long. I have absolutely no fucking idea why. */
		if (nybble_run->total_code_bits == 0)
			++nybble_run->total_code_bits;
	}
}

static void ComputeBestCodeLengths(State* const state)
{
	unsigned int best_starting_leaf_read_index;
	unsigned int best_total_bits;

	/* Brute-force the optimal number of coded nybble runs. */
	/* We do this because, the more coded nybble runs there are, the more likely it is that common nybble runs will be
	   given longer codes, leading to larger compressed data, so it might be beneficial to use fewer coded nybble runs. */
	best_starting_leaf_read_index = state->generator.huffman.leaf_read_index;
	best_total_bits = (unsigned int)-1;

	/* Gradually ignore nybble runs, starting with the rarest ones. */
	for (; state->generator.huffman.leaf_read_index < TOTAL_SYMBOLS - 1; ++state->generator.huffman.leaf_read_index)
	{
		ComputeTrees(state);
		ComputeCodeLengths(state);

		/* Find out how many bits this number of coded nybble runs uses. */
		ComputeTotalEncodedBits(state);

		/* Track the number of coded nybble runs with the lowest number of bits. */
		if (state->total_bits < best_total_bits)
		{
			best_total_bits = state->total_bits;
			best_starting_leaf_read_index = state->generator.huffman.leaf_read_index;
		}
	}

	/* Now that we know the ideal number of coded nybble runs, use it to continue compression. */
	state->generator.huffman.leaf_read_index = best_starting_leaf_read_index;
	ComputeTrees(state);
	ComputeCodeLengths(state);
}

static cc_bool ComparisonCodeTotalBits(const NybbleRun* const nybble_run_1, const NybbleRun* const nybble_run_2)
{
	/* Sort by total code bits first, and sort by occurrence second. */
	/* We sort by occurrence so that the nybble runs that get bumped to 8 bits are the least common, costing the least space. */
	if (nybble_run_1->total_code_bits == nybble_run_2->total_code_bits)
		return nybble_run_1->occurrences > nybble_run_2->occurrences;
	else
		return nybble_run_1->total_code_bits < nybble_run_2->total_code_bits;
}

static void ComputeCodesFromLengths(State* const state)
{
	NybbleRunsIndex runs_reordered;
	unsigned int code, previous_code_length;
	unsigned int i;
	unsigned int total_code_bits_modifier;

	/* Get a sorted list of the nybble runs, ordered by their total code bits. */
	ComputeSortedRuns(state, runs_reordered, ComparisonCodeTotalBits);

	code = -1;
	total_code_bits_modifier = 0;

	/* Ignore all of the nybble runs that don't have a code... */
	for (i = 0; i < TOTAL_SYMBOLS; ++i)
		if (NybbleRunFromIndex(state, runs_reordered[i])->total_code_bits != 0)
			break;

	/* ..and iterate over the ones that do. */
	for (; i < TOTAL_SYMBOLS; ++i)
	{
		unsigned int nybble_run_index = runs_reordered[i];
		NybbleRun* const nybble_run = NybbleRunFromIndex(state, nybble_run_index);

		/* What we're doing here is computing the 'canonical Huffman codes' from the code lengths. */
		++code;

		nybble_run->total_code_bits += total_code_bits_modifier;

		if (nybble_run->total_code_bits != previous_code_length)
		{
			assert(previous_code_length != 9);
			code <<= nybble_run->total_code_bits - previous_code_length;
			previous_code_length = nybble_run->total_code_bits;
		}

		/* Prevent conflicting with the reserved inline prefix. */
		if (total_code_bits_modifier == 0 /* Don't do this more than once. */
		 && ((nybble_run->total_code_bits >= 6 && (code >> (nybble_run->total_code_bits - 6)) == 0x3E) /* Soon-to-be suffix of 111111. */
		  || (nybble_run->total_code_bits < 6 && code == (1u << nybble_run->total_code_bits) - 1))) /* Prefix of 111111. */
		{
		#ifdef CLOWNNEMESIS_DEBUG
			fputs("Performing conflict avoidance.\n", stderr);
		#endif
			code <<= 1;
			++nybble_run->total_code_bits;
			++previous_code_length;
			total_code_bits_modifier = 1;

			assert(previous_code_length != 9);
		}

		nybble_run->code = code;

	#ifdef CLOWNNEMESIS_DEBUG
		{
			unsigned int j;

			fprintf(stderr, "Nybble %X of length %d has code ", nybble_run_index / 8, (nybble_run_index % 8) + 1);

			for (j = 0; j < 8; ++j)
				fputc((code & (1 << (8 - 1 - j))) != 0 ? '1' : '0', stderr);

			fprintf(stderr, " of %d bits\n", nybble_run->total_code_bits);
		}
	#endif
	}
}

static void ComputeCodesHuffman(State* const state)
{
	/* Create leaf nodes. */
	IterateNybbleRuns(state, CreateLeafNode);

	/* Now sort them by their occurrences. */
	qsort(state->generator.huffman.node_pool, TOTAL_SYMBOLS, sizeof(Node), CompareNodes);

	/* Find the first node with a decent probability. */
	for (state->generator.huffman.leaf_read_index = 0; state->generator.huffman.leaf_read_index < TOTAL_SYMBOLS; ++state->generator.huffman.leaf_read_index)
		if (state->generator.huffman.node_pool[state->generator.huffman.leaf_read_index].occurrences >= 3)
			break;

	/* Compute code lengths. */
	ComputeBestCodeLengths(state);

	/* With the lengths, we can compute the codes. */
	ComputeCodesFromLengths(state);
}

/*************************/
/* End of Huffman Coding */
/*************************/

static int ReadByteThatMightBeXORed(State* const state)
{
	const int value = ReadByte(&state->common);

	if (value == CLOWNNEMESIS_EOF)
	{
		state->input_byte_buffer_index = 0;
		state->input_byte_buffer[0] = state->input_byte_buffer[1] = state->input_byte_buffer[2] = state->input_byte_buffer[3] = 0;
		return CLOWNNEMESIS_EOF;
	}
	else
	{
		const unsigned int index = state->input_byte_buffer_index;
		const unsigned int previous_value = state->input_byte_buffer[index];

		state->input_byte_buffer_index = (state->input_byte_buffer_index + 1) % CC_COUNT_OF(state->input_byte_buffer);
		state->input_byte_buffer[index] = value;

		return value ^ (state->xor_mode_enabled ? previous_value : 0);
	}
}

static int ReadNybble(State* const state)
{
	if (state->nybble_reader_flip_flop)
	{
		state->input_nybble_buffer <<= 4;
	}
	else
	{
		const int value = ReadByteThatMightBeXORed(state);

		if (value == CLOWNNEMESIS_EOF)
			return CLOWNNEMESIS_EOF;

		if (state->bytes_read == UINT_MAX)
		{
		#ifdef CLOWNNEMESIS_DEBUG
			fputs("Input data is too large.\n", stderr);
		#endif
			longjmp(state->common.jump_buffer, 1);
		}

		++state->bytes_read;
		state->input_nybble_buffer = (unsigned char)value;
	}

	state->nybble_reader_flip_flop = !state->nybble_reader_flip_flop;

	return (state->input_nybble_buffer >> 4) & 0xF;
}

static void FindRuns(State* const state, void (* const callback)(State *state, unsigned int run_nybble, unsigned int run_length))
{
	int new_nybble, previous_nybble, run_length;

	state->bytes_read = 0;
	state->nybble_reader_flip_flop = cc_false;
	state->input_byte_buffer[0] = state->input_byte_buffer[1] = state->input_byte_buffer[2] = state->input_byte_buffer[3] = 0;
	state->input_byte_buffer_index = 0;

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

static void LogOccurrence(State* const state, const unsigned int run_nybble, const unsigned int run_length)
{
	++state->nybble_runs[run_nybble][run_length - 1].occurrences;
	++state->total_runs;
}

static void ResetNybbleRun(State* const state, const unsigned int run_nybble, const unsigned int run_length_minus_one)
{
	NybbleRun* const nybble_run = &state->nybble_runs[run_nybble][run_length_minus_one];

	nybble_run->occurrences = nybble_run->code = nybble_run->total_code_bits = 0;
}

static unsigned int ComputeCodesInternal(State* const state, const cc_bool xor_mode_enabled, const cc_bool accurate)
{
	state->xor_mode_enabled = xor_mode_enabled;

	/* Reset occurances to 0. */
	IterateNybbleRuns(state, ResetNybbleRun);

	/* Count how many times each nybble run occurs in the source data. */
	/* Also count how many nybbles (bytes) are in the input data. */
	FindRuns(state, LogOccurrence);

	/* Do the coding-specific tasks. */
	if (accurate)
		ComputeCodesFano(state);
	else
		ComputeCodesHuffman(state);

	ComputeTotalEncodedBits(state);

	/* This division is needed in order for Sonic 1's Basaran tiles to compress accurately. Sega's compressor only counted the bytes, not the bits. */
	/* This division must round up in order for Sonic 3's Buggernaut tiles to compress accurately. */
	return CC_DIVIDE_CEILING(state->total_bits, 8);
}

static void ComputeCodes(State* const state, const cc_bool accurate)
{
	/* Process the input data in both regular and XOR mode, seeing which produces the smaller data. */
	const unsigned int total_bytes_regular_mode = ComputeCodesInternal(state, cc_false, accurate);
	const unsigned int total_bytes_xor_mode = ComputeCodesInternal(state, cc_true, accurate);

#ifdef CLOWNNEMESIS_DEBUG
	fprintf(stderr, "Regular: %d bytes.\nXOR:     %d bytes.\n", total_bytes_regular_mode, total_bytes_xor_mode);
#endif

	/* If regular mode was smaller or equivalent, then process the data in that mode again since it's currently in XOR mode still. */
	/* TODO: Avoid this third pass by caching the first two. */
	if (total_bytes_regular_mode <= total_bytes_xor_mode)
		ComputeCodesInternal(state, cc_false, accurate);
}

static void EmitHeader(State* const state)
{
	const unsigned int bytes_per_tile = 0x20;
	const unsigned int total_tiles = state->bytes_read / bytes_per_tile;

	/* TODO: Maybe do this check in ComputeCodes? */
	if (state->bytes_read % bytes_per_tile != 0)
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

	WriteByte(&state->common, total_tiles >> 8 | state->xor_mode_enabled << 7);
	WriteByte(&state->common, total_tiles & 0xFF);
}

static void EmitCodeTableEntry(State* const state, const unsigned int run_nybble, const unsigned int run_length_minus_one)
{
	NybbleRun* const nybble_run = &state->nybble_runs[run_nybble][run_length_minus_one];

	if (nybble_run->total_code_bits != 0)
	{
	#ifdef CLOWNNEMESIS_DEBUG
		fprintf(stderr, "Run of nybble %X of length %d occurred %d times (code is ", run_nybble, run_length_minus_one + 1, nybble_run->occurrences);

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

		WriteByte(&state->common, run_length_minus_one << 4 | nybble_run->total_code_bits);
		WriteByte(&state->common, nybble_run->code);
	}
}

static void EmitCodeTable(State* const state)
{
	/* Finally, emit the code table. */
	state->previous_nybble = 0xFF; /* Deliberately invalid. */
	IterateNybbleRuns(state, EmitCodeTableEntry);

	/* Mark the end of the code table. */
	WriteByte(&state->common, 0xFF);

#ifdef CLOWNNEMESIS_DEBUG
	fprintf(stderr, "Total runs: %d\n", state->total_runs);
#endif
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

static void EmitCode(State* const state, const unsigned int run_nybble, const unsigned int run_length)
{
	const NybbleRun* const nybble_run = &state->nybble_runs[run_nybble][run_length - 1];

	if (nybble_run->total_code_bits != 0)
	{
	#ifdef CLOWNNEMESIS_DEBUG
		unsigned int i;

		fputs("Emitting code ", stderr);

		for (i = 0; i < 8; ++i)
			fputc((nybble_run->code & (1 << (8 - 1 - i))) != 0 ? '1' : '0', stderr);

		fprintf(stderr, " of length %d for nybble %X of length %d.\n", nybble_run->total_code_bits, run_nybble, run_length);
	#endif

		WriteBits(state, nybble_run->code, nybble_run->total_code_bits);
	}
	else
	{
	#ifdef CLOWNNEMESIS_DEBUG
		fprintf(stderr, "Emitting reject for nybble %X of length %d.\n", run_nybble, run_length);
	#endif

		/* This run doesn't have a code, so inline it. */
		WriteBits(state, 0x3F, 6);
		WriteBits(state, run_length - 1, 3);
		WriteBits(state, run_nybble, 4);
	}
}

static void EmitCodes(State* const state, const cc_bool accurate)
{
	/* TODO: Use clownlzss to find the most efficient way of encoding the uncompressed data using the available codes. */
	FindRuns(state, EmitCode);

	/* Output any codes that haven't yet been flushed. */
	/* Foolishly, Sega's compressor would redundantly emit an empty byte here if there are no unflushed bits. */
	if (state->output_bits_done != 0 || accurate)
		WriteByte(&state->common, (state->output_byte_buffer << (8 - state->output_bits_done)) & 0xFF);
}

int ClownNemesis_Compress(const int accurate_int, const ClownNemesis_InputCallback read_byte, const void* const read_byte_user_data, const ClownNemesis_OutputCallback write_byte, const void* const write_byte_user_data)
{
	int success;
	State state = {0};

	const cc_bool accurate = accurate_int != 0;

	success = 0;

	InitialiseCommon(&state.common, read_byte, read_byte_user_data, write_byte, write_byte_user_data);
	state.common.throw_on_eof = cc_false;

	if (!setjmp(state.common.jump_buffer))
	{
		ComputeCodes(&state, accurate);

		EmitHeader(&state);
		EmitCodeTable(&state);
		EmitCodes(&state, accurate);

		success = 1;
	}

	return success;
}
