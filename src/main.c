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
  char subchunk1ID[4];
  uint32_t subchunk1Size;
  uint16_t audioFormat;
  uint16_t numChannels;
  uint32_t sampleRate;
  uint32_t byteRate;
  uint16_t blockAlign;
  uint16_t bitsPerSample;
  char subchunk2ID[4];
  uint32_t subchunk2Size;
} WavHeader;
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
  printf("  --xor -x     Bitwise XOR with value (0-255)\n");
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
  }

  FILE *input_file = fopen(input_filename, "rb");
  if (!input_file) {
    printf("Error: cannot open input file %s\n", input_filename);
    return 1;
  }

  WavHeader header;
  if (fread(&header, sizeof(WavHeader), 1, input_file) != 1) {
    printf("Error: cannot read WAV header\n");
    fclose(input_file);
    return 1;
  }

  if (strncmp(header.chunkID, "RIFF", 4) != 0 ||
      strncmp(header.format, "WAVE", 4) != 0) {
    printf("Error: not a valid WAV file\n");
    fclose(input_file);
    return 1;
  }

  if (header.audioFormat != 1) {
    printf("Error: only PCM format supported\n");
    fclose(input_file);
    return 1;
  }

  printf("WAV file info:\n");
  printf("  Channels: %d\n", header.numChannels);
  printf("  Sample rate: %d Hz\n", header.sampleRate);
  printf("  Bits per sample: %d\n", header.bitsPerSample);
  printf("  Data size: %d bytes\n", header.subchunk2Size);

  uint8_t *audio_data = (uint8_t *)malloc(header.subchunk2Size);
  if (!audio_data) {
    printf("Error: cannot allocate memory for audio data\n");
    fclose(input_file);
    return 1;
  }

  if (fread(audio_data, 1, header.subchunk2Size, input_file) !=
      header.subchunk2Size) {
    printf("Error: cannot read audio data\n");
    free(audio_data);
    fclose(input_file);
    return 1;
  }

  fclose(input_file);

  if (strcmp(operation, "--right") == 0 || strcmp(operation, "-r") == 0) {
    if (value < 0 || value > 7) {
      printf("Error: shift value must be in range 0-7\n");
      free(audio_data);
      return 1;
    }
    printf("Applying right shift by %d...\n", value);
    apply_right_shift(audio_data, header.subchunk2Size, value);
  } else if (strcmp(operation, "--left") == 0 || strcmp(operation, "-l") == 0) {
    if (value < 0 || value > 7) {
      printf("Error: shift value must be in range 0-7\n");
      free(audio_data);
      return 1;
    }
    printf("Applying left shift by %d...\n", value);
    apply_left_shift(audio_data, header.subchunk2Size, value);
  } else if (strcmp(operation, "--not") == 0 || strcmp(operation, "-n") == 0) {
    printf("Applying bitwise NOT...\n");
    apply_not(audio_data, header.subchunk2Size, value);
  } else if (strcmp(operation, "--and") == 0 || strcmp(operation, "-a") == 0) {
    if (value < 0 || value > 255) {
      printf("Error: AND value must be in range 0-255\n");
      free(audio_data);
      return 1;
    }
    printf("Applying bitwise AND with %d...\n", value);
    apply_and(audio_data, header.subchunk2Size, value);
  } else if (strcmp(operation, "--or") == 0 || strcmp(operation, "-o") == 0) {
    if (value < 0 || value > 255) {
      printf("Error: OR value must be in range 0-255\n");
      free(audio_data);
      return 1;
    }
    printf("Applying bitwise OR with %d...\n", value);
    apply_or(audio_data, header.subchunk2Size, value);
  } else if (strcmp(operation, "--xor") == 0 || strcmp(operation, "-x") == 0) {
    if (value < 0 || value > 255) {
      printf("Error: XOR value must be in range 0-255\n");
      free(audio_data);
      return 1;
    }
    printf("Applying bitwise XOR with %d...\n", value);
    apply_xor(audio_data, header.subchunk2Size, value);
  } else {
    printf("Error: unknown operation %s\n", operation);
    print_usage(argv[0]);
    free(audio_data);
    return 1;
  }

  FILE *output_file = fopen(output_filename, "wb");
  if (!output_file) {
    printf("Error: cannot create output file %s\n", output_filename);
    free(audio_data);
    return 1;
  }

  if (fwrite(&header, sizeof(WavHeader), 1, output_file) != 1) {
    printf("Error: cannot write header\n");
    free(audio_data);
    fclose(output_file);
    return 1;
  }

  if (fwrite(audio_data, 1, header.subchunk2Size, output_file) !=
      header.subchunk2Size) {
    printf("Error: cannot write audio data\n");
    free(audio_data);
    fclose(output_file);
    return 1;
  }

  free(audio_data);
  fclose(output_file);

  printf("Done! Result saved to %s\n", output_filename);

  return 0;
}
