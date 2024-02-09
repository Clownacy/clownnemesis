#include <stdio.h>
#include <stdlib.h>

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
		fprintf(stderr, "Usage: %s -c (or -d) input output\n", argv[0]);
	}
	else
	{
		if (argv[1][0] != '-' || (argv[1][1] != 'c' && argv[1][1] != 'd') || argv[1][2] != '\0')
		{
			fputs("Error: Could not open input file for reading.\n", stderr);
		}			
		else
		{
			const int compress = argv[1][1] == 'c';

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
					if (compress)
					{
						if (!ClownNemesis_Compress(InputCallback, input_file, OutputCallback, output_file))
							fputs("Error: Could not compress data.\n", stderr);
						else
							exit_code = EXIT_SUCCESS;
					}
					else
					{
						if (!ClownNemesis_Decompress(InputCallback, input_file, OutputCallback, output_file))
							fputs("Error: Could not decompress data.\n", stderr);
						else
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
