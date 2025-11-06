#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#pragma pack(push, 1)
typedef struct {
  char chunkID[4];
  uint32_t chunkSize;
  char format[4];
} WavRiffHeader;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  char subchunkID[4];
  uint32_t subchunkSize;
} WavChunkHeader;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
  uint16_t audioFormat;
  uint16_t numChannels;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
} WavFmtData;
#pragma pack(pop)

void apply_right_shift(uint8_t *data, size_t size, int shift) {
  for (size_t i = 0; i < size; i++) {
    data[i] = (data[i] >> shift) & 0xFF;
  }
}

void apply_left_shift(uint8_t *data, size_t size, int shift) {
  for (size_t i = 0; i < size; i++) {
    data[i] = (data[i] << shift) & 0xFF;
  }
}

void apply_not(uint8_t *data, size_t size, int value) {
  for (size_t i = 0; i < size; i++) {
    data[i] = ~data[i] & 0xFF;
  }
}

void apply_and(uint8_t *data, size_t size, int value) {
  for (size_t i = 0; i < size; i++) {
    data[i] = (data[i] & value) & 0xFF;
  }
}

void apply_or(uint8_t *data, size_t size, int value) {
  for (size_t i = 0; i < size; i++) {
    data[i] = (data[i] | value) & 0xFF;
  }
}

void apply_xor(uint8_t *data, size_t size, int value) {
  for (size_t i = 0; i < size; i++) {
    data[i] = (data[i] ^ value) & 0xFF;
  }
}

void print_usage(const char *program_name) {
  printf("Usage: %s <input.wav> <output.wav> <operation> <value>\n",
         program_name);
  printf("Operations:\n");
  printf("  --right -r   Right shift by value (0-7)\n");
  printf("  --left -l    Left shift by value (0-7)\n");
  printf("  --not -n     Bitwise NOT (value ignored)\n");
  printf("  --and -a     Bitwise AND with value (0-255)\n");
  printf("  --or -o      Bitwise OR with value (0-255)\n");
  printf("  --xor -z     Bitwise XOR with value (0-255)\n");
}

int parse_wav_file(FILE *file, WavFmtData *fmtData, uint32_t *data_size,
                   long *data_offset) {
  WavRiffHeader riffHeader;

  if (fread(&riffHeader, sizeof(WavRiffHeader), 1, file) != 1) {
    return 0;
  }

  if (strncmp(riffHeader.chunkID, "RIFF", 4) != 0 ||
      strncmp(riffHeader.format, "WAVE", 4) != 0) {
    return 0;
  }

  WavChunkHeader chunkHeader;
  int fmt_found = 0;
  int data_found = 0;

  while (fread(&chunkHeader, sizeof(WavChunkHeader), 1, file) == 1) {
    if (strncmp(chunkHeader.subchunkID, "fmt ", 4) == 0) {
      if (fread(fmtData, sizeof(WavFmtData), 1, file) != 1) {
        return 0;
      }

      if (chunkHeader.subchunkSize > sizeof(WavFmtData)) {
        fseek(file, chunkHeader.subchunkSize - sizeof(WavFmtData), SEEK_CUR);
      }
      fmt_found = 1;
    } else if (strncmp(chunkHeader.subchunkID, "data", 4) == 0) {
      *data_size = chunkHeader.subchunkSize;
      *data_offset = ftell(file);
      data_found = 1;
      break;
    } else {
      fseek(file, chunkHeader.subchunkSize, SEEK_CUR);
    }
  }

  return (fmt_found && data_found);
}

int process_wav_file(const char *input_filename, const char *output_filename,
                     const char *operation, int value) {
  FILE *input_file = fopen(input_filename, "rb");
  if (!input_file) {
    printf("Error: cannot open input file %s\n", input_filename);
    return 1;
  }

  WavFmtData fmtData;
  uint32_t data_size;
  long data_offset;

  if (!parse_wav_file(input_file, &fmtData, &data_size, &data_offset)) {
    printf("Error: invalid WAV file format\n");
    fclose(input_file);
    return 1;
  }

  printf("WAV file info:\n");
  printf("  Channels: %d\n", fmtData.numChannels);
  printf("  Sample rate: %d Hz\n", fmtData.sampleRate);
  printf("  Bits per sample: %d\n", fmtData.bitsPerSample);
  printf("  Data size: %u bytes\n", data_size);
  printf("  Data offset: %ld bytes\n", data_offset);

  if (fmtData.audioFormat != 1) {
    printf("Error: only PCM format supported\n");
    fclose(input_file);
    return 1;
  }

  if (fmtData.bitsPerSample != 8 && fmtData.bitsPerSample != 16) {
    printf("Error: only 8-bit and 16-bit PCM supported\n");
    fclose(input_file);
    return 1;
  }

  FILE *output_file = fopen(output_filename, "wb");
  if (!output_file) {
    printf("Error: cannot create output file %s\n", output_filename);
    fclose(input_file);
    return 1;
  }

  rewind(input_file);
  uint8_t *header_buffer = (uint8_t *)malloc(data_offset);
  if (!header_buffer) {
    printf("Error: cannot allocate memory for header\n");
    fclose(input_file);
    fclose(output_file);
    return 1;
  }

  if (fread(header_buffer, 1, data_offset, input_file) != data_offset) {
    printf("Error: cannot read file header\n");
    free(header_buffer);
    fclose(input_file);
    fclose(output_file);
    return 1;
  }

  if (fwrite(header_buffer, 1, data_offset, output_file) != data_offset) {
    printf("Error: cannot write file header\n");
    free(header_buffer);
    fclose(input_file);
    fclose(output_file);
    return 1;
  }

  free(header_buffer);

  const size_t BUFFER_SIZE = 1024 * 1024;
  uint8_t *buffer = (uint8_t *)malloc(BUFFER_SIZE);
  if (!buffer) {
    printf("Error: cannot allocate buffer\n");
    fclose(input_file);
    fclose(output_file);
    return 1;
  }

  size_t total_processed = 0;
  size_t bytes_remaining = data_size;

  printf("Processing audio data...\n");

  while (bytes_remaining > 0) {
    size_t chunk_size =
        (bytes_remaining < BUFFER_SIZE) ? bytes_remaining : BUFFER_SIZE;

    size_t bytes_read = fread(buffer, 1, chunk_size, input_file);
    if (bytes_read != chunk_size) {
      printf("Error: read incomplete chunk\n");
      free(buffer);
      fclose(input_file);
      fclose(output_file);
      return 1;
    }

    if (strcmp(operation, "--right") == 0 || strcmp(operation, "-r") == 0) {
      apply_right_shift(buffer, chunk_size, value);
    } else if (strcmp(operation, "--left") == 0 ||
               strcmp(operation, "-l") == 0) {
      apply_left_shift(buffer, chunk_size, value);
    } else if (strcmp(operation, "--not") == 0 ||
               strcmp(operation, "-n") == 0) {
      apply_not(buffer, chunk_size, value);
    } else if (strcmp(operation, "--and") == 0 ||
               strcmp(operation, "-a") == 0) {
      apply_and(buffer, chunk_size, value);
    } else if (strcmp(operation, "--or") == 0 || strcmp(operation, "-o") == 0) {
      apply_or(buffer, chunk_size, value);
    } else if (strcmp(operation, "--xor") == 0 ||
               strcmp(operation, "-z") == 0) {
      apply_xor(buffer, chunk_size, value);
    }

    size_t bytes_written = fwrite(buffer, 1, chunk_size, output_file);
    if (bytes_written != chunk_size) {
      printf("Error: write incomplete chunk\n");
      free(buffer);
      fclose(input_file);
      fclose(output_file);
      return 1;
    }

    total_processed += chunk_size;
    bytes_remaining -= chunk_size;

    if (data_size > 0) {
      int progress = (int)((total_processed * 100) / data_size);
      printf("\rProgress: %d%% (%zu/%u bytes)", progress, total_processed,
             data_size);
      fflush(stdout);
    }
  }

  printf("\n");

  free(buffer);
  fclose(input_file);
  fclose(output_file);

  return 0;
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  if (argc != 5 && !(argc == 4 && (strcmp(argv[3], "--not") == 0 ||
                                   strcmp(argv[3], "-n") == 0))) {
    print_usage(argv[0]);
    return 1;
  }

  const char *input_filename = argv[1];
  const char *output_filename = argv[2];
  const char *operation = argv[3];
  int value = 0;

  if (argc == 5) {
    value = atoi(argv[4]);

    if ((strcmp(operation, "--right") == 0 || strcmp(operation, "-r") == 0 ||
         strcmp(operation, "--left") == 0 || strcmp(operation, "-l") == 0)) {
      if (value < 0 || value > 7) {
        printf("Error: shift value must be in range 0-7\n");
        return 1;
      }
    } else if (strcmp(operation, "--and") == 0 ||
               strcmp(operation, "-a") == 0 || strcmp(operation, "--or") == 0 ||
               strcmp(operation, "-o") == 0 ||
               strcmp(operation, "--xor") == 0 ||
               strcmp(operation, "-z") == 0) {
      if (value < 0 || value > 255) {
        printf("Error: operation value must be in range 0-255\n");
        return 1;
      }
    }
  }

  printf("Operation: %s", operation);
  if (argc == 5) {
    printf(" with value %d", value);
  }
  printf("\n");

  int result =
      process_wav_file(input_filename, output_filename, operation, value);

  if (result == 0) {
    printf("Done! Result saved to %s\n", output_filename);
  } else {
    printf("Error processing file\n");
  }

  return result;
}
