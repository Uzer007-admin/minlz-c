#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "minlz.h"

int main(int argc, char *argv[]) {
    // Check command-line arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }
    const char *input_filename = argv[1];

    // Open the input file in binary mode
    FILE *input_file = fopen(input_filename, "rb");
    if (input_file == NULL) {
        perror("Error opening input file");
        return 1;
    }

    // Determine the size of the input file
    fseek(input_file, 0, SEEK_END);
    size_t input_size = ftell(input_file);
    fseek(input_file, 0, SEEK_SET);

    // Allocate memory for the input data
    uint8_t *input_data = malloc(input_size);
    if (input_data == NULL) {
        perror("Error allocating input buffer");
        fclose(input_file);
        return 1;
    }

    // Read the input file into the buffer
    size_t read_size = fread(input_data, 1, input_size, input_file);
    if (read_size != input_size) {
        perror("Error reading input file");
        free(input_data);
        fclose(input_file);
        return 1;
    }
    fclose(input_file);

    // Prepare the output file name by appending ".mzb"
    char output_filename[256]; // Assumes a maximum filename length of 255 characters
    strcpy(output_filename, input_filename);
    strcat(output_filename, ".mzb");

    // Calculate the maximum possible size of the compressed data
    int max_compressed_size = minlz_max_encoded_len(input_size);
    if (max_compressed_size < 0) {
        fprintf(stderr, "Input too large to compress\n");
        free(input_data);
        return 1;
    }

    // Allocate memory for the compressed data
    uint8_t *compressed_data = malloc(max_compressed_size);
    if (compressed_data == NULL) {
        perror("Error allocating compressed data buffer");
        free(input_data);
        return 1;
    }

    // Compress the input data
    int compressed_size = minlz_encode_minlz_block(compressed_data, max_compressed_size, input_data, input_size, 0);
    if (compressed_size < 0) {
        fprintf(stderr, "Compression error: %d\n", compressed_size);
        free(input_data);
        free(compressed_data);
        return 1;
    }

    // Open the output file in binary mode
    FILE *output_file = fopen(output_filename, "wb");
    if (output_file == NULL) {
        perror("Error opening output file");
        free(input_data);
        free(compressed_data);
        return 1;
    }

    // Write the compressed data to the output file
    size_t write_size = fwrite(compressed_data, 1, compressed_size, output_file);
    if (write_size != compressed_size) {
        perror("Error writing output file");
        fclose(output_file);
        free(input_data);
        free(compressed_data);
        return 1;
    }
    fclose(output_file);

    // Clean up allocated memory
    free(input_data);
    free(compressed_data);

    // Print success message
    printf("Compressed %s (%ld) -> %s (%d)\n", input_filename, input_size, output_filename, compressed_size);
    return 0;
}
