/*
 * basenc.c - Windows compatible implementation of GNU coreutils basenc
 * 
 * This is a single-file C implementation of the GNU coreutils basenc utility,
 * designed to be compiled directly with MinGW GCC or MSVC on Windows.
 * 
 * Supports all encoding types and options of the original GNU basenc:
 * - base64, base64url, base32, base32hex, base16, base2msbf, base2lsbf, z85
 * - decode mode (-d, --decode)
 * - wrap column (-w, --wrap=COLS)
 * - ignore garbage (-i, --ignore-garbage)
 * 
 * Usage: basenc [OPTION]... [FILE]
 * 
 * Compile with:
 *   MinGW: gcc -o basenc.exe basenc.c
 *   MSVC:  cl basenc.c /Fe:basenc.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define SET_BINARY_MODE(file) _setmode(_fileno(file), _O_BINARY)
#else
#define SET_BINARY_MODE(file) ((void)0)
#endif


char *PROGRAM_NAME = "basenc";


#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1


#define ENC_BLOCKSIZE (1024 * 3 * 10)
#define DEC_BLOCKSIZE (1024 * 5)


typedef enum {
    ENC_NONE = 0,
    ENC_BASE64,
    ENC_BASE64URL,
    ENC_BASE32,
    ENC_BASE32HEX,
    ENC_BASE16,
    ENC_BASE2MSBF,
    ENC_BASE2LSBF,
    ENC_Z85
} encoding_type_t;

/* Program parameters */
typedef struct {
    int decode;
    int ignore_garbage;
    int wrap_column;
    encoding_type_t encoding_type;
    const char *input_file;
} params_t;


void usage(int status);
void version(void);
void write_error(void);
void exit_with_error(const char *message, const char *arg);
int parse_arguments(int argc, char **argv, params_t *params);
void wrap_write(const char *buffer, size_t len, size_t wrap_column, size_t *current_column, FILE *out);
void do_encode(FILE *in, const char *infile, FILE *out, size_t wrap_column, encoding_type_t encoding_type);
void do_decode(FILE *in, const char *infile, FILE *out, int ignore_garbage, encoding_type_t encoding_type);

/* Base64 implementation */
static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const char base64url_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static int is_base64(unsigned char c) {
    return (isalnum(c) || (c == '+') || (c == '/'));
}

static int is_base64url(unsigned char c) {
    return (isalnum(c) || (c == '-') || (c == '_'));
}

static int base64_char_to_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int base64url_char_to_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

static size_t base64_encode_block(const unsigned char *in, size_t inlen, char *out, size_t outlen, const char *charset) {
    size_t i = 0, j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    size_t out_len = 0;

    while (inlen--) {
        char_array_3[i++] = *(in++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++) {
                if (j < outlen) {
                    out[j++] = charset[char_array_4[i]];
                    out_len++;
                }
            }
            i = 0;
        }
    }

    if (i) {
        for (size_t k = i; k < 3; k++) {
            char_array_3[k] = '\0';
        }

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (size_t k = 0; k < i + 1; k++) {
            if (j < outlen) {
                out[j++] = charset[char_array_4[k]];
                out_len++;
            }
        }

        if (charset == base64_chars) {
            while (i++ < 3) {
                if (j < outlen) {
                    out[j++] = '=';
                    out_len++;
                }
            }
        }
    }

    return out_len;
}

static size_t base64_decode_block(const char *in, size_t inlen, unsigned char *out, size_t outlen, int is_url, int ignore_garbage) {
    size_t i = 0, j = 0, k = 0;
    unsigned char char_array_4[4], char_array_3[3];
    size_t out_len = 0;
    int (*is_valid_char)(unsigned char) = is_url ? is_base64url : is_base64;
    int (*char_to_value)(char) = is_url ? base64url_char_to_value : base64_char_to_value;

    while (inlen-- && (in[k] != '=') && (is_valid_char(in[k]) || ignore_garbage)) {
        if (!is_valid_char(in[k])) {
            if (ignore_garbage) {
                k++;
                continue;
            } else {
                exit_with_error("invalid input", NULL);
            }
        }
        char_array_4[i++] = in[k++];
        if (i == 4) {
            for (i = 0; i < 4; i++) {
                char_array_4[i] = char_to_value(char_array_4[i]);
            }

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; i < 3; i++) {
                if (j < outlen) {
                    out[j++] = char_array_3[i];
                    out_len++;
                }
            }
            i = 0;
        }
    }

    if (i) {
        for (size_t l = i; l < 4; l++) {
            char_array_4[l] = 0;
        }

        for (size_t l = 0; l < 4; l++) {
            if (in[k-i+l] != '=') {
                char_array_4[l] = char_to_value(in[k-i+l]);
            } else {
                char_array_4[l] = 0;
            }
        }

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (size_t l = 0; l < i - 1; l++) {
            if (j < outlen) {
                out[j++] = char_array_3[l];
                out_len++;
            }
        }
    }

    return out_len;
}

/* Base32 implementation */
static const char base32_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
static const char base32hex_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUV";

static int is_base32(unsigned char c, const char *alphabet) {
    return (strchr(alphabet, toupper(c)) != NULL);
}

static int base32_char_to_value(char c, const char *alphabet) {
    const char *p = strchr(alphabet, toupper(c));
    if (p) {
        return p - alphabet;
    }
    return -1;
}

static size_t base32_encode_block(const unsigned char *in, size_t inlen, char *out, size_t outlen, const char *alphabet) {
    size_t i = 0, index = 0;
    size_t out_len = 0;
    unsigned char buffer[5];

    while (i < inlen) {
        size_t buffer_size = 0;

        while (buffer_size < 5 && i < inlen) {
            buffer[buffer_size++] = in[i++];
        }

        while (buffer_size < 5) {
            buffer[buffer_size++] = 0;
        }


        if (index < outlen) { out[index++] = alphabet[(buffer[0] >> 3) & 0x1F]; out_len++; }
        if (index < outlen) { out[index++] = alphabet[((buffer[0] & 0x07) << 2) | ((buffer[1] >> 6) & 0x03)]; out_len++; }
        if (index < outlen) { out[index++] = alphabet[(buffer[1] >> 1) & 0x1F]; out_len++; }
        if (index < outlen) { out[index++] = alphabet[((buffer[1] & 0x01) << 4) | ((buffer[2] >> 4) & 0x0F)]; out_len++; }
        if (index < outlen) { out[index++] = alphabet[((buffer[2] & 0x0F) << 1) | ((buffer[3] >> 7) & 0x01)]; out_len++; }
        if (index < outlen) { out[index++] = alphabet[(buffer[3] >> 2) & 0x1F]; out_len++; }
        if (index < outlen) { out[index++] = alphabet[((buffer[3] & 0x03) << 3) | ((buffer[4] >> 5) & 0x07)]; out_len++; }
        if (index < outlen) { out[index++] = alphabet[buffer[4] & 0x1F]; out_len++; }

        if (i >= inlen) {
            switch (inlen % 5) {
                case 1:
                    if (index > 2 && index <= outlen) {
                        for (size_t j = 2; j < 8 && index < outlen; j++) {
                            out[index++] = '=';
                            out_len++;
                        }
                    }
                    break;
                case 2:
                    if (index > 4 && index <= outlen) {
                        for (size_t j = 4; j < 8 && index < outlen; j++) {
                            out[index++] = '=';
                            out_len++;
                        }
                    }
                    break;
                case 3:
                    if (index > 5 && index <= outlen) {
                        for (size_t j = 5; j < 8 && index < outlen; j++) {
                            out[index++] = '=';
                            out_len++;
                        }
                    }
                    break;
                case 4:
                    if (index > 7 && index <= outlen) {
                        out[index++] = '=';
                        out_len++;
                    }
                    break;
            }
        }
    }

    return out_len;
}

static size_t base32_decode_block(const char *in, size_t inlen, unsigned char *out, size_t outlen, const char *alphabet, int ignore_garbage) {
    size_t i = 0, j = 0;
    size_t out_len = 0;
    unsigned char buffer[8];
    size_t buffer_size = 0;

    while (i < inlen) {
        // Skip non-alphabet characters if ignore_garbage is set
        if (in[i] == '\n' || in[i] == '\r') {
            i++;
            continue;
        }
        
        if (in[i] == '=') {
            i++;
            continue;
        }
        
        int value = base32_char_to_value(in[i], alphabet);
        if (value == -1) {
            if (ignore_garbage) {
                i++;
                continue;
            } else {
                exit_with_error("invalid input", NULL);
            }
        }
        
        buffer[buffer_size++] = value;
        i++;
        
        if (buffer_size == 8) {
            if (j < outlen) { out[j++] = (buffer[0] << 3) | (buffer[1] >> 2); out_len++; }
            if (j < outlen) { out[j++] = (buffer[1] << 6) | (buffer[2] << 1) | (buffer[3] >> 4); out_len++; }
            if (j < outlen) { out[j++] = (buffer[3] << 4) | (buffer[4] >> 1); out_len++; }
            if (j < outlen) { out[j++] = (buffer[4] << 7) | (buffer[5] << 2) | (buffer[6] >> 3); out_len++; }
            if (j < outlen) { out[j++] = (buffer[6] << 5) | buffer[7]; out_len++; }
            
            buffer_size = 0;
        }
    }
    
    // Handle remaining characters
    if (buffer_size > 0) {
        for (size_t k = buffer_size; k < 8; k++) {
            buffer[k] = 0;
        }
        
        if (buffer_size >= 2 && j < outlen) { out[j++] = (buffer[0] << 3) | (buffer[1] >> 2); out_len++; }
        if (buffer_size >= 4 && j < outlen) { out[j++] = (buffer[1] << 6) | (buffer[2] << 1) | (buffer[3] >> 4); out_len++; }
        if (buffer_size >= 5 && j < outlen) { out[j++] = (buffer[3] << 4) | (buffer[4] >> 1); out_len++; }
        if (buffer_size >= 7 && j < outlen) { out[j++] = (buffer[4] << 7) | (buffer[5] << 2) | (buffer[6] >> 3); out_len++; }
    }
    
    return out_len;
}

/* Base16 (hex) implementation */
static int is_base16(unsigned char c) {
    return (isxdigit(c) != 0);
}

static int base16_char_to_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static size_t base16_encode_block(const unsigned char *in, size_t inlen, char *out, size_t outlen) {
    static const char hex_chars[] = "0123456789ABCDEF";
    size_t i, j;
    size_t out_len = 0;
    
    for (i = 0, j = 0; i < inlen && j + 1 < outlen; i++) {
        out[j++] = hex_chars[(in[i] >> 4) & 0x0F];
        out[j++] = hex_chars[in[i] & 0x0F];
        out_len += 2;
    }
    
    return out_len;
}

static size_t base16_decode_block(const char *in, size_t inlen, unsigned char *out, size_t outlen, int ignore_garbage) {
    size_t i, j;
    size_t out_len = 0;
    
    for (i = 0, j = 0; i + 1 < inlen && j < outlen;) {
        if (in[i] == '\n' || in[i] == '\r') {
            i++;
            continue;
        }
        
        int high = base16_char_to_value(in[i]);
        if (high == -1) {
            if (ignore_garbage) {
                i++;
                continue;
            } else {
                exit_with_error("invalid input", NULL);
            }
        }
        
        if (in[i+1] == '\n' || in[i+1] == '\r') {
            i++;
            continue;
        }
        
        int low = base16_char_to_value(in[i+1]);
        if (low == -1) {
            if (ignore_garbage) {
                i++;
                continue;
            } else {
                exit_with_error("invalid input", NULL);
            }
        }
        
        out[j++] = (high << 4) | low;
        out_len++;
        i += 2;
    }
    
    return out_len;
}

static int is_base2(unsigned char c) {
    return (c == '0' || c == '1');
}

static size_t base2_encode_block(const unsigned char *in, size_t inlen, char *out, size_t outlen, int msb_first) {
    size_t i, j;
    size_t out_len = 0;
    
    for (i = 0, j = 0; i < inlen && j + 7 < outlen; i++) {
        unsigned char byte = in[i];
        
        if (msb_first) {
            for (int bit = 7; bit >= 0; bit--) {
                out[j++] = ((byte >> bit) & 0x01) ? '1' : '0';
            }
        } else {
            for (int bit = 0; bit < 8; bit++) {
                out[j++] = ((byte >> bit) & 0x01) ? '1' : '0';
            }
        }
        
        out_len += 8;
    }
    
    return out_len;
}

static size_t base2_decode_block(const char *in, size_t inlen, unsigned char *out, size_t outlen, int msb_first, int ignore_garbage) {
    size_t i, j;
    size_t out_len = 0;
    unsigned char byte = 0;
    int bit_count = 0;
    
    for (i = 0, j = 0; i < inlen && j < outlen; i++) {
        if (in[i] == '\n' || in[i] == '\r') {
            continue;
        }
        
        if (!is_base2(in[i])) {
            if (ignore_garbage) {
                continue;
            } else {
                exit_with_error("invalid input", NULL);
            }
        }
        
        int bit = (in[i] == '1') ? 1 : 0;
        
        if (msb_first) {
            byte = (byte << 1) | bit;
        } else {
            byte |= (bit << bit_count);
        }
        
        bit_count++;
        
        if (bit_count == 8) {
            out[j++] = byte;
            out_len++;
            byte = 0;
            bit_count = 0;
        }
    }
    

    if (bit_count > 0) {
        exit_with_error("invalid input: number of bits not a multiple of 8", NULL);
    }
    
    return out_len;
}

/* Z85 implementation */
static const char z85_encoding_chars[] = 
    "0123456789"
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    ".-:+=^!/*?&<>()[]{}@%$#";

// Z85 decoding table for ASCII values 33-125
static const signed char z85_decoding_table[93] = {
    68, -1, 84, 83, 82, 72, -1, 75, 76, 70, 65, -1, 63, 62, 69,  // ! to /
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 64, -1, 73, 66, 74, 71,  // 0 to ?
    36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,  // @ to O
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, -1, 78, 67, -1, -1,  // P to `
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  // a to p
    26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 79, -1, 80, -1, -1       // q to DEL-1
};

static int is_z85(unsigned char c) {
    return (c >= 33 && c <= 125 && z85_decoding_table[c - 33] != -1);
}

static size_t z85_encode_block(const unsigned char *in, size_t inlen, char *out, size_t outlen) {
    size_t i, j;
    size_t out_len = 0;
    
    if (inlen % 4 != 0) {
        exit_with_error("invalid input: Z85 encoding input length must be a multiple of 4", NULL);
    }
    
    for (i = 0, j = 0; i + 3 < inlen && j + 4 < outlen; i += 4) {
        unsigned int value = ((unsigned int)in[i] << 24) |
                             ((unsigned int)in[i+1] << 16) |
                             ((unsigned int)in[i+2] << 8) |
                             ((unsigned int)in[i+3]);
        
        for (int k = 4; k >= 0; k--) {
            out[j+k] = z85_encoding_chars[value % 85];
            value /= 85;
        }
        
        j += 5;
        out_len += 5;
    }
    
    return out_len;
}

static size_t z85_decode_block(const char *in, size_t inlen, unsigned char *out, size_t outlen, int ignore_garbage) {
    size_t i, j;
    size_t out_len = 0;
    unsigned char buffer[5];
    size_t buffer_size = 0;
    
    if (inlen % 5 != 0 && !ignore_garbage) {
        exit_with_error("invalid input: Z85 decoding input length must be a multiple of 5", NULL);
    }
    
    for (i = 0, j = 0; i < inlen && j + 3 < outlen;) {
        if (in[i] == '\n' || in[i] == '\r') {
            i++;
            continue;
        }
        
        if (!is_z85(in[i])) {
            if (ignore_garbage) {
                i++;
                continue;
            } else {
                exit_with_error("invalid input", NULL);
            }
        }
        
        buffer[buffer_size++] = z85_decoding_table[in[i] - 33];
        i++;
        
        if (buffer_size == 5) {
            unsigned int value = 0;
            for (int k = 0; k < 5; k++) {
                value = value * 85 + buffer[k];
            }
            
            out[j++] = (value >> 24) & 0xFF;
            out[j++] = (value >> 16) & 0xFF;
            out[j++] = (value >> 8) & 0xFF;
            out[j++] = value & 0xFF;
            
            out_len += 4;
            buffer_size = 0;
        }
    }
    

    if (buffer_size > 0) {
        exit_with_error("invalid input: Z85 decoding input length must be a multiple of 5", NULL);
    }
    
    return out_len;
}

/* Main encoding/decoding functions */
void do_encode(FILE *in, const char *infile, FILE *out, size_t wrap_column, encoding_type_t encoding_type) {
    unsigned char *inbuf;
    char *outbuf;
    size_t sum;
    size_t current_column = 0;

    inbuf = (unsigned char *)malloc(ENC_BLOCKSIZE);
    if (!inbuf) {
        exit_with_error("memory allocation failed", NULL);
    }

    size_t outbuf_size;
    switch (encoding_type) {
        case ENC_BASE64:
        case ENC_BASE64URL:
            outbuf_size = ((ENC_BLOCKSIZE + 2) / 3) * 4 + 1;
            break;
        case ENC_BASE32:
        case ENC_BASE32HEX:
            outbuf_size = ((ENC_BLOCKSIZE + 4) / 5) * 8 + 1;
            break;
        case ENC_BASE16:
            outbuf_size = ENC_BLOCKSIZE * 2 + 1;
            break;
        case ENC_BASE2MSBF:
        case ENC_BASE2LSBF:
            outbuf_size = ENC_BLOCKSIZE * 8 + 1;
            break;
        case ENC_Z85:
            outbuf_size = ((ENC_BLOCKSIZE + 3) / 4) * 5 + 1;
            break;
        default:
            exit_with_error("unknown encoding type", NULL);
    }

    outbuf = (char *)malloc(outbuf_size);
    if (!outbuf) {
        free(inbuf);
        exit_with_error("memory allocation failed", NULL);
    }

    do {
        sum = 0;
        do {
            size_t n = fread(inbuf + sum, 1, ENC_BLOCKSIZE - sum, in);
            sum += n;
        } while (!feof(in) && !ferror(in) && sum < ENC_BLOCKSIZE);

        if (sum > 0) {
            size_t encoded_len = 0;

            switch (encoding_type) {
                case ENC_BASE64:
                    encoded_len = base64_encode_block(inbuf, sum, outbuf, outbuf_size - 1, base64_chars);
                    break;
                case ENC_BASE64URL:
                    encoded_len = base64_encode_block(inbuf, sum, outbuf, outbuf_size - 1, base64url_chars);
                    break;
                case ENC_BASE32:
                    encoded_len = base32_encode_block(inbuf, sum, outbuf, outbuf_size - 1, base32_chars);
                    break;
                case ENC_BASE32HEX:
                    encoded_len = base32_encode_block(inbuf, sum, outbuf, outbuf_size - 1, base32hex_chars);
                    break;
                case ENC_BASE16:
                    encoded_len = base16_encode_block(inbuf, sum, outbuf, outbuf_size - 1);
                    break;
                case ENC_BASE2MSBF:
                    encoded_len = base2_encode_block(inbuf, sum, outbuf, outbuf_size - 1, 1);
                    break;
                case ENC_BASE2LSBF:
                    encoded_len = base2_encode_block(inbuf, sum, outbuf, outbuf_size - 1, 0);
                    break;
                case ENC_Z85:
                    if (sum % 4 != 0) {
                        free(inbuf);
                        free(outbuf);
                        exit_with_error("invalid input: Z85 encoding input length must be a multiple of 4", NULL);
                    }
                    encoded_len = z85_encode_block(inbuf, sum, outbuf, outbuf_size - 1);
                    break;
                default:
                    free(inbuf);
                    free(outbuf);
                    exit_with_error("unknown encoding type", NULL);
            }

            outbuf[encoded_len] = '\0';
            wrap_write(outbuf, encoded_len, wrap_column, &current_column, out);
        }
    } while (!feof(in) && !ferror(in) && sum == ENC_BLOCKSIZE);

    if (wrap_column > 0 && current_column > 0) {
        if (fputc('\n', out) == EOF) {
            write_error();
        }
    }

    if (ferror(in)) {
        free(inbuf);
        free(outbuf);
        exit_with_error("read error", NULL);
    }

    free(inbuf);
    free(outbuf);

    if (fclose(in) != 0) {
        if (strcmp(infile, "-") == 0) {
            exit_with_error("closing standard input", NULL);
        } else {
            exit_with_error(infile, strerror(errno));
        }
    }

    exit(EXIT_SUCCESS);
}

void do_decode(FILE *in, const char *infile, FILE *out, int ignore_garbage, encoding_type_t encoding_type) {
    char *inbuf;
    unsigned char *outbuf;
    size_t sum;

    size_t inbuf_size;
    switch (encoding_type) {
        case ENC_BASE64:
        case ENC_BASE64URL:
            inbuf_size = DEC_BLOCKSIZE * 4 / 3 + 4;
            break;
        case ENC_BASE32:
        case ENC_BASE32HEX:
            inbuf_size = DEC_BLOCKSIZE * 8 / 5 + 8;
            break;
        case ENC_BASE16:
            inbuf_size = DEC_BLOCKSIZE * 2 + 2;
            break;
        case ENC_BASE2MSBF:
        case ENC_BASE2LSBF:
            inbuf_size = DEC_BLOCKSIZE * 8 + 8;
            break;
        case ENC_Z85:
            inbuf_size = DEC_BLOCKSIZE * 5 / 4 + 5;
            break;
        default:
            exit_with_error("unknown encoding type", NULL);
    }

    inbuf = (char *)malloc(inbuf_size);
    outbuf = (unsigned char *)malloc(DEC_BLOCKSIZE);
    if (!inbuf || !outbuf) {
        free(inbuf);
        free(outbuf);
        exit_with_error("memory allocation failed", NULL);
    }

    do {
        sum = 0;
        do {
            size_t n = fread(inbuf + sum, 1, inbuf_size - sum - 1, in);
            sum += n;
        } while (!feof(in) && !ferror(in) && sum < inbuf_size - 1);

        inbuf[sum] = '\0';

        if (sum > 0) {
            size_t decoded_len = 0;

            switch (encoding_type) {
                case ENC_BASE64:
                    decoded_len = base64_decode_block(inbuf, sum, outbuf, DEC_BLOCKSIZE, 0, ignore_garbage);
                    break;
                case ENC_BASE64URL:
                    decoded_len = base64_decode_block(inbuf, sum, outbuf, DEC_BLOCKSIZE, 1, ignore_garbage);
                    break;
                case ENC_BASE32:
                    decoded_len = base32_decode_block(inbuf, sum, outbuf, DEC_BLOCKSIZE, base32_chars, ignore_garbage);
                    break;
                case ENC_BASE32HEX:
                    decoded_len = base32_decode_block(inbuf, sum, outbuf, DEC_BLOCKSIZE, base32hex_chars, ignore_garbage);
                    break;
                case ENC_BASE16:
                    decoded_len = base16_decode_block(inbuf, sum, outbuf, DEC_BLOCKSIZE, ignore_garbage);
                    break;
                case ENC_BASE2MSBF:
                    decoded_len = base2_decode_block(inbuf, sum, outbuf, DEC_BLOCKSIZE, 1, ignore_garbage);
                    break;
                case ENC_BASE2LSBF:
                    decoded_len = base2_decode_block(inbuf, sum, outbuf, DEC_BLOCKSIZE, 0, ignore_garbage);
                    break;
                case ENC_Z85:
                    decoded_len = z85_decode_block(inbuf, sum, outbuf, DEC_BLOCKSIZE, ignore_garbage);
                    break;
                default:
                    free(inbuf);
                    free(outbuf);
                    exit_with_error("unknown encoding type", NULL);
            }

            if (fwrite(outbuf, 1, decoded_len, out) < decoded_len) {
                write_error();
            }
        }
    } while (!feof(in) && !ferror(in));

    if (ferror(in)) {
        free(inbuf);
        free(outbuf);
        exit_with_error("read error", NULL);
    }

    free(inbuf);
    free(outbuf);

    if (fclose(in) != 0) {
        if (strcmp(infile, "-") == 0) {
            exit_with_error("closing standard input", NULL);
        } else {
            exit_with_error(infile, strerror(errno));
        }
    }

    exit(EXIT_SUCCESS);
}


void wrap_write(const char *buffer, size_t len, size_t wrap_column, size_t *current_column, FILE *out) {
    if (wrap_column == 0) {
        if (fwrite(buffer, 1, len, out) < len) {
            write_error();
        }
        *current_column += len;
    } else {
        for (size_t i = 0; i < len; i++) {
            if (*current_column >= wrap_column) {
                if (fputc('\n', out) == EOF) {
                    write_error();
                }
                *current_column = 0;
            }
            if (fputc(buffer[i], out) == EOF) {
                write_error();
            }
            (*current_column)++;
        }
    }
}

void write_error(void) {
    exit_with_error("write error", NULL);
}

void exit_with_error(const char *message, const char *arg) {
    if (arg) {
        fprintf(stderr, "%s: %s: %s\n", PROGRAM_NAME, arg, message);
    } else {
        fprintf(stderr, "%s: %s\n", PROGRAM_NAME, message);
    }
    exit(EXIT_FAILURE);
}

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
    } else {
        printf("Usage: %s [OPTION]... [FILE]\n", PROGRAM_NAME);
        printf("basenc encode or decode FILE, or standard input, to standard output.\n\n");
        printf("With no FILE, or when FILE is -, read standard input.\n\n");
        printf("Mandatory arguments to long options are mandatory for short options too.\n");
        printf("      --base64          same as 'base64' program (RFC4648 section 4)\n");
        printf("      --base64url       file- and url-safe base64 (RFC4648 section 5)\n");
        printf("      --base32          same as 'base32' program (RFC4648 section 6)\n");
        printf("      --base32hex       extended hex alphabet base32 (RFC4648 section 7)\n");
        printf("      --base16          hex encoding (RFC4648 section 8)\n");
        printf("      --base2msbf       bit string with most significant bit (msb) first\n");
        printf("      --base2lsbf       bit string with least significant bit (lsb) first\n");
        printf("  -d, --decode          decode data\n");
        printf("  -i, --ignore-garbage  when decoding, ignore non-alphabet characters\n");
        printf("  -w, --wrap=COLS       wrap encoded lines after COLS character (default 76).\n");
        printf("                          Use 0 to disable line wrapping\n");
        printf("      --z85             ascii85-like encoding (ZeroMQ spec:32/Z85);\n");
        printf("                        when encoding, input length must be a multiple of 4;\n");
        printf("                        when decoding, input length must be a multiple of 5\n");
        printf("      --help     display this help and exit\n");
        printf("      --version  output version information and exit\n\n");
        printf("When decoding, the input may contain newlines in addition to the bytes of\n");
        printf("the formal alphabet.  Use --ignore-garbage to attempt to recover\n");
        printf("from any other non-alphabet bytes in the encoded stream.\n");
    }
    exit(status);
}

void version(void) {
    printf("%s (Windows compatible) 1.0\n", PROGRAM_NAME);
    exit(EXIT_SUCCESS);
}

static void remove_suffix(char* name, const char* suffix) {
    char* np = name + strlen(name);
    const char* sp = suffix + strlen(suffix);

    while (np > name && sp > suffix)
        if (*--np != *--sp) return;

    if (np > name) *np = '\0';
}

int parse_arguments(int argc, char **argv, params_t *params) {
    int i;
    int encoding_set = 0;

    // Set default values
    params->decode = 0;
    params->ignore_garbage = 0;
    params->wrap_column = 76;
    params->encoding_type = ENC_NONE;
    params->input_file = "-";

    if (argc > 0) {
        PROGRAM_NAME = argv[0];
        const char *slash = strrchr(PROGRAM_NAME, '/');
        if (slash) {
            PROGRAM_NAME = slash + 1;
        }
        #ifdef _WIN32
        const char *backslash = strrchr(PROGRAM_NAME, '\\');
        if (backslash) {
            PROGRAM_NAME = backslash + 1;
        }
        #endif
    }
    remove_suffix(PROGRAM_NAME, ".exe");

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(EXIT_SUCCESS);
        } else if (strcmp(argv[i], "--version") == 0) {
            version();
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--decode") == 0) {
            params->decode = 1;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--ignore-garbage") == 0) {
            params->ignore_garbage = 1;
        } else if (strcmp(argv[i], "-w") == 0) {
            if (i + 1 < argc) {
                char *endptr;
                long val = strtol(argv[++i], &endptr, 10);
                if (*endptr != '\0' || val < 0) {
                    fprintf(stderr, "%s: invalid wrap size: '%s'\n", PROGRAM_NAME, argv[i]);
                    return -1;
                }
                params->wrap_column = (int)val;
            } else {
                fprintf(stderr, "%s: option requires an argument -- 'w'\n", PROGRAM_NAME);
                return -1;
            }
        } else if (strncmp(argv[i], "--wrap=", 7) == 0) {
            char *endptr;
            long val = strtol(argv[i] + 7, &endptr, 10);
            if (*endptr != '\0' || val < 0) {
                fprintf(stderr, "%s: invalid wrap size: '%s'\n", PROGRAM_NAME, argv[i] + 7);
                return -1;
            }
            params->wrap_column = (int)val;
        } else if (strcmp(argv[i], "--base64") == 0) {
            if (encoding_set && params->encoding_type != ENC_BASE64) {
                fprintf(stderr, "%s: multiple encoding types specified\n", PROGRAM_NAME);
                return -1;
            }
            params->encoding_type = ENC_BASE64;
            encoding_set = 1;
        } else if (strcmp(argv[i], "--base64url") == 0) {
            if (encoding_set && params->encoding_type != ENC_BASE64URL) {
                fprintf(stderr, "%s: multiple encoding types specified\n", PROGRAM_NAME);
                return -1;
            }
            params->encoding_type = ENC_BASE64URL;
            encoding_set = 1;
        } else if (strcmp(argv[i], "--base32") == 0) {
            if (encoding_set && params->encoding_type != ENC_BASE32) {
                fprintf(stderr, "%s: multiple encoding types specified\n", PROGRAM_NAME);
                return -1;
            }
            params->encoding_type = ENC_BASE32;
            encoding_set = 1;
        } else if (strcmp(argv[i], "--base32hex") == 0) {
            if (encoding_set && params->encoding_type != ENC_BASE32HEX) {
                fprintf(stderr, "%s: multiple encoding types specified\n", PROGRAM_NAME);
                return -1;
            }
            params->encoding_type = ENC_BASE32HEX;
            encoding_set = 1;
        } else if (strcmp(argv[i], "--base16") == 0) {
            if (encoding_set && params->encoding_type != ENC_BASE16) {
                fprintf(stderr, "%s: multiple encoding types specified\n", PROGRAM_NAME);
                return -1;
            }
            params->encoding_type = ENC_BASE16;
            encoding_set = 1;
        } else if (strcmp(argv[i], "--base2msbf") == 0) {
            if (encoding_set && params->encoding_type != ENC_BASE2MSBF) {
                fprintf(stderr, "%s: multiple encoding types specified\n", PROGRAM_NAME);
                return -1;
            }
            params->encoding_type = ENC_BASE2MSBF;
            encoding_set = 1;
        } else if (strcmp(argv[i], "--base2lsbf") == 0) {
            if (encoding_set && params->encoding_type != ENC_BASE2LSBF) {
                fprintf(stderr, "%s: multiple encoding types specified\n", PROGRAM_NAME);
                return -1;
            }
            params->encoding_type = ENC_BASE2LSBF;
            encoding_set = 1;
        } else if (strcmp(argv[i], "--z85") == 0) {
            if (encoding_set && params->encoding_type != ENC_Z85) {
                fprintf(stderr, "%s: multiple encoding types specified\n", PROGRAM_NAME);
                return -1;
            }
            params->encoding_type = ENC_Z85;
            encoding_set = 1;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            if (argv[i][1] != '-') {
                size_t j;
                for (j = 1; argv[i][j] != '\0'; j++) {
                    switch (argv[i][j]) {
                        case 'd':
                            params->decode = 1;
                            break;
                        case 'i':
                            params->ignore_garbage = 1;
                            break;
                        case 'w':
                            if (argv[i][j+1] != '\0') {
                                char *endptr;
                                long val = strtol(&argv[i][j+1], &endptr, 10);
                                if (*endptr != '\0' || val < 0) {
                                    fprintf(stderr, "%s: invalid wrap size: '%s'\n", PROGRAM_NAME, &argv[i][j+1]);
                                    return -1;
                                }
                                params->wrap_column = (int)val;
                                j = strlen(argv[i]); 
                            } else if (i + 1 < argc) {
                                char *endptr;
                                long val = strtol(argv[++i], &endptr, 10);
                                if (*endptr != '\0' || val < 0) {
                                    fprintf(stderr, "%s: invalid wrap size: '%s'\n", PROGRAM_NAME, argv[i]);
                                    return -1;
                                }
                                params->wrap_column = (int)val;
                                j = strlen(argv[i-1]);
                            } else {
                                fprintf(stderr, "%s: option requires an argument -- 'w'\n", PROGRAM_NAME);
                                return -1;
                            }
                            break;
                        default:
                            fprintf(stderr, "%s: invalid option -- '%c'\n", PROGRAM_NAME, argv[i][j]);
                            return -1;
                    }
                }
            } else {
                fprintf(stderr, "%s: unrecognized option '%s'\n", PROGRAM_NAME, argv[i]);
                return -1;
            }
        } else if (strcmp(argv[i], "-") == 0 || argv[i][0] != '-') {
            if (strcmp(params->input_file, "-") != 0) {
                fprintf(stderr, "%s: extra operand '%s'\n", PROGRAM_NAME, argv[i]);
                return -1;
            }
            params->input_file = argv[i];
        } else {
            fprintf(stderr, "%s: unrecognized option '%s'\n", PROGRAM_NAME, argv[i]);
            return -1;
        }
    }

    if (params->encoding_type == ENC_NONE) {
        fprintf(stderr, "%s: missing encoding type\n", PROGRAM_NAME);
        fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    params_t params;
    FILE *input_stream;

    if (parse_arguments(argc, argv, &params) != 0) {
        return EXIT_FAILURE;
    }

    if (strcmp(params.input_file, "-") == 0) {
        input_stream = stdin;
        SET_BINARY_MODE(stdin);
    } else {
        input_stream = fopen(params.input_file, "rb");
        if (!input_stream) {
            exit_with_error(params.input_file, strerror(errno));
        }
    }

    SET_BINARY_MODE(stdout);

    if (params.decode) {
        do_decode(input_stream, params.input_file, stdout, params.ignore_garbage, params.encoding_type);
    } else {
        do_encode(input_stream, params.input_file, stdout, params.wrap_column, params.encoding_type);
    }
    return EXIT_SUCCESS;
}
