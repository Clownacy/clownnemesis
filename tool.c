#include <stdio.h>
#include <stdlib.h>

#include "clowncommon/clowncommon.h"

#include "compress.h"
#include "decompress.h"

static int InputCallback(void* const user_data)
{
	FILE* const file = (FILE*)user_data;
	const int character = fgetc(file);

	if (character == EOF)
	{
		if (ferror(file))
			return CLOWNNEMESIS_ERROR;

		rewind(file);
		return CLOWNNEMESIS_EOF;
	}

	return character;
}

static int OutputCallback(void* const user_data, const unsigned char byte)
{
	const int return_value = fputc(byte, (FILE*)user_data);

	return return_value == EOF ? CLOWNNEMESIS_ERROR : return_value;
}

int main(const int argc, char** const argv)
{
	int exit_code;

	exit_code = EXIT_FAILURE;

	if (argc < 4)
	{
		const char* const usage =
			"clownnemesis v1.1, by Clownacy.\n"
			"This is a Nemesis compressor and decompressor, which can compress data\n"
			"identically to Sega's original Nemesis compressor.\n"
			"\n"
			"Usage: %s options input output\n"
			"\n"
			"Options:\n"
			"  -c  - Compress (better, but not accurate to Sega's compressor)\n"
			"  -ca - Compress (worse, but accurate to Sega's compressor)\n"
			"  -d  - Decompress\n";

		fprintf(stderr, usage, argv[0]);
	}
	else
	{
		cc_bool compress, accurate, unrecognised;

		unrecognised = cc_false;

		if (argv[1][0] == '-' && argv[1][1] == 'c' && argv[1][2] == '\0')
		{
			compress = cc_true;
			accurate = cc_false;
		}
		else if (argv[1][0] == '-' && argv[1][1] == 'c' && argv[1][2] == 'a' && argv[1][3] == '\0')
		{
			compress = cc_true;
			accurate = cc_true;
		}
		else if (argv[1][0] == '-' && argv[1][1] == 'd' && argv[1][2] == '\0')
		{
			compress = cc_false;
			accurate = cc_false;
		}
		else
		{
			unrecognised = cc_true;
		}

		if (unrecognised)
		{
			fprintf(stderr, "Error: Unrecognised option '%s'.\n", argv[1]);
		}			
		else
		{
			FILE *const input_file = fopen(argv[2], "rb");

			if (input_file == NULL)
			{
				fputs("Error: Could not open input file for reading.\n", stderr);
			}
			else
			{
				FILE *const output_file = fopen(argv[3], "wb");

				if (output_file == NULL)
				{
					fputs("Error: Could not open output file for writing.\n", stderr);
				}
				else
				{
					int success;

					if (compress)
						success = ClownNemesis_Compress(accurate, InputCallback, input_file, OutputCallback, output_file);
					else
						success = ClownNemesis_Decompress(InputCallback, input_file, OutputCallback, output_file);

					if (!success)
					{
						if (compress)
							fputs("Error: Could not compress data.\nThe input data is either too large or its size is not a multiple of 0x20 bytes.\n", stderr);
						else
							fputs("Error: Could not decompress data. The input data is not valid Nemesis data.\n", stderr);
					}
					else
					{
						exit_code = EXIT_SUCCESS;
					}

					fclose(output_file);
				}

				fclose(input_file);
			}
		}
	}

	return exit_code;
}
