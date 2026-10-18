#include "unminlz.h"  // MinLZ header
#include <stdio.h>  // For file operations
#include <stdint.h> // For fixed-width integer types
#include <stdlib.h> // For memory allocation

#define MAX_BLOCK_SIZE (8 << 20) // 8MB, maximum block size for decoding

int main(int argc, char* argv[]) {
    // Check if the correct number of arguments is provided
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input_file> <output_file>\n", argv[0]);
        return 1;
    }

    // Assign filenames from command-line arguments
    const char* input_filename = argv[1];
    const char* output_filename = argv[2];

    // Step 1: Open the input file in binary mode
    FILE* input_file = fopen(input_filename, "rb");
    if (!input_file) {
        fprintf(stderr, "Error: Could not open input file '%s'\n", input_filename);
        return 1;
    }

    // Step 2: Determine the size of the input file
    fseek(input_file, 0, SEEK_END);    // Move to the end of the file
    size_t src_len = ftell(input_file); // Get the file size
    fseek(input_file, 0, SEEK_SET);    // Rewind to the beginning

    // Step 3: Allocate memory to hold the compressed data
    uint8_t* src = malloc(src_len);
    if (!src) {
        fprintf(stderr, "Error: Memory allocation failed for source data\n");
        fclose(input_file);
        return 1;
    }

    // Step 4: Read the compressed data from the file
    if (fread(src, 1, src_len, input_file) != src_len) {
        fprintf(stderr, "Error: Failed to read input file '%s'\n", input_filename);
        free(src);
        fclose(input_file);
        return 1;
    }
    fclose(input_file); // Close the input file as we no longer need it

    // Step 5: Allocate memory for the decompressed data (up to 8MB)
    uint8_t* dst = malloc(MAX_BLOCK_SIZE);
    if (!dst) {
        fprintf(stderr, "Error: Memory allocation failed for destination buffer\n");
        free(src);
        return 1;
    }

    // Step 6: Decode the compressed data
    int result = minlz_decode_block(dst, MAX_BLOCK_SIZE, src, src_len);

    if (result < 0) {
        fprintf(stderr, "Error: Decoding failed with code %d\n", result);
        free(src);
        free(dst);
        return 1;
    }


    // Step 7: Open the output file in binary mode
    FILE* output_file = fopen(output_filename, "wb");
    if (!output_file) {
        fprintf(stderr, "Error: Could not open output file '%s'\n", output_filename);
        free(src);
        free(dst);
        return 1;
    }

    // Step 8: Write the decompressed data to the output file
    if (fwrite(dst, 1, result, output_file) != (size_t)result) {
        fprintf(stderr, "Error: Failed to write to output file '%s'\n", output_filename);
        fclose(output_file);
        free(src);
        free(dst);
        return 1;
    }

    // Step 9: Clean up resources
    fclose(output_file); // Close the output file
    free(src);          // Free the source buffer
    free(dst);          // Free the destination buffer

    // Step 10: Report success
    printf("Successfully decoded %d bytes to '%s'\n", result, output_filename);
    return 0;
}
