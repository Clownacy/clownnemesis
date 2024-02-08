#include <stdio.h>
#include <stdlib.h>

#include "decompress.h"

static int InputCallback(void* const user_data)
{
	const int character = fgetc((FILE*)user_data);

	return character == EOF ? -1 : character;
}

static int OutputCallback(void* const user_data, const unsigned char byte)
{
	return fputc(byte, (FILE*)user_data) != EOF;
}

int main(const int argc, char** const argv)
{
	int exit_code;

	exit_code = EXIT_FAILURE;

	if (argc < 3)
	{
		fputs("Error: Must pass input and output file paths.\n", stderr);
	}
	else
	{
		FILE *const input_file = fopen(argv[1], "rb");

		if (input_file == NULL)
		{
			fputs("Error: Could not open input file for reading.\n", stderr);
		}
		else
		{
			FILE *const output_file = fopen(argv[2], "wb");

			if (output_file == NULL)
			{
				fputs("Error: Could not open output file for writing.\n", stderr);
			}
			else
			{
				if (!ClownNemesis_Decompress(InputCallback, input_file, OutputCallback, output_file))
					fputs("Error: Could not decompress data.\n", stderr);
				else
					exit_code = EXIT_SUCCESS;

				fclose(output_file);
			}

			fclose(input_file);
		}
	}

	return exit_code;
}
