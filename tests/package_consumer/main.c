#include <minlz.h>

#include <string.h>

int main(void) {
    static const unsigned char input[] = "minlz package smoke";
    unsigned char encoded[128];
    unsigned char decoded[sizeof(input)];
    size_t encoded_len = 0;
    size_t decoded_len = 0;

    if (strcmp(MINLZ_VERSION_STRING, "1.2.5") != 0) {
        return 1;
    }
    if (minlz_encode(encoded, sizeof(encoded), &encoded_len,
                     input, sizeof(input), MINLZ_LEVEL_FASTEST) != MINLZ_OK) {
        return 2;
    }
    if (minlz_decode(decoded, sizeof(decoded), &decoded_len,
                     encoded, encoded_len) != MINLZ_OK) {
        return 3;
    }
    return decoded_len == sizeof(input) &&
                   memcmp(decoded, input, sizeof(input)) == 0
               ? 0
               : 4;
}
