#include <stdio.h>    // For console I/O (printf, fprintf)
#include <stdlib.h>   // For memory allocation (malloc, free), random numbers (rand, srand)
#include <stdint.h>   // For fixed-size integer types (uint32_t, int16_t, int32_t, int64_t)
#include <string.h>   // For strncpy
#include <math.h>     // For round()
#include <limits.h>   // For INT16_MAX, INT16_MIN, INT32_MAX, INT32_MIN
#include <time.h>     // For time() to seed random number generator

#include "tap_detect.h" // Include the custom tap detection header

// --- Fixed-Point Configuration ---
// We'll use Q2.29 fixed-point format for 32-bit processing.
// This means: 1 sign bit, 2 integer bits, 29 fractional bits.
// The value range for a Q2.29 fixed_point_t (int32_t) is approximately -4.0 to +3.999...
// This provides sufficient headroom for audio processing (audio typically normalized to -1.0 to 1.0).
#define Q_FORMAT Q_BITS // Using Q_BITS from tap_detect.h for consistency (29)
#define FIXED_POINT_ONE Q_ONE // Using Q_ONE from tap_detect.h for consistency (1 << 29)

// Define the fixed_point_t type as a signed 32-bit integer for processing
typedef int32_t fixed_point_t;

// --- Fixed-Point Utility Functions ---
// Convert a float to fixed-point (Q_FORMAT)
fixed_point_t float_to_fixed_point(float f) {
    // Clamp the float value to prevent overflow for Q2.29 range [-4.0, 3.999...]
    if (f >= 4.0f) f = 3.999999f;
    if (f < -4.0f) f = -4.0f;
    return (fixed_point_t)round(f * FIXED_POINT_ONE);
}

// Convert fixed-point (Q_FORMAT) to float
float fixed_point_to_float(fixed_point_t q) {
    return (float)q / FIXED_POINT_ONE;
}

// Fixed-point multiplication (Q_FORMAT * Q_FORMAT -> Q_FORMAT)
fixed_point_t fixed_point_mul(fixed_point_t a, fixed_point_t b) {
    // Multiply two 32-bit numbers, result can be up to 64-bit.
    int64_t result_64 = (int64_t)a * b;
    // Right-shift by Q_FORMAT to bring it back to Q_FORMAT.
    // This implicitly truncates fractional bits beyond Q_FORMAT.
    return (fixed_point_t)(result_64 >> Q_FORMAT);
}

// Fixed-point addition (Q_FORMAT + Q_FORMAT -> Q_FORMAT) with saturation
fixed_point_t fixed_point_add(fixed_point_t a, fixed_point_t b) {
    // Use a 64-bit temporary variable to detect overflow before casting back to 32-bit.
    int64_t sum = (int64_t)a + b;

    // Saturate the sum to the valid range of fixed_point_t (int32_t)
    if (sum > INT32_MAX) {
        return INT32_MAX;
    } else if (sum < INT32_MIN) {
        return INT32_MIN;
    }
    return (fixed_point_t)sum;
}

// --- WAV Header Structure ---
// Defines the standard RIFF WAV file header for 16-bit PCM mono audio.
typedef struct {
    char     riff[4];        // "RIFF" chunk ID
    uint32_t overall_size;   // Size of the entire file in bytes minus 8 bytes
    char     wave[4];        // "WAVE" format
    char     fmt_chunk_marker[4]; // "fmt " subchunk 1 ID
    uint32_t fmt_chunk_size; // Size of the fmt subchunk (16 for PCM)
    uint16_t audio_format;   // Audio format (1 for PCM)
    uint16_t num_channels;   // Number of channels (1 for mono, 2 for stereo)
    uint32_t sample_rate;    // Sample rate in Hz
    uint32_t byte_rate;      // Byte rate = sample_rate * num_channels * bits_per_sample/8
    uint16_t block_align;    // Block align = num_channels * bits_per_sample/8
    uint16_t bits_per_sample;// Bits per sample (16 for 16-bit PCM)
    char     data_chunk_marker[4]; // "data" subchunk 2 ID
    uint32_t data_size;      // Size of the data section in bytes
} WavHeader;

/**
 * @brief Reads 16-bit PCM mono audio data from a WAV file into a dynamically allocated fixed-point array (Q2.29).
 * @param filepath The path to the input WAV file.
 * @param samplerate_out Pointer to a uint32_t to store the sample rate read from the header.
 * @param num_samples_out Pointer to a long to store the total number of audio samples read.
 * @return A pointer to a newly allocated int32_t array containing the fixed-point audio data,
 * or NULL if an error occurs. The caller is responsible for freeing this memory.
 * Assumptions: Input WAV is 16-bit PCM, mono.
 */
fixed_point_t* read_wav_data_fx(const char* filepath, uint32_t* samplerate_out, long* num_samples_out) {
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        fprintf(stderr, "Error: Could not open WAV file %s\n", filepath);
        return NULL;
    }

    WavHeader header;
    if (fread(&header, 1, sizeof(WavHeader), file) != sizeof(WavHeader)) {
        fprintf(stderr, "Error: Could not read full WAV header from %s\n", filepath);
        fclose(file);
        return NULL;
    }

    // Validate WAV format
    if (strncmp(header.riff, "RIFF", 4) != 0 || strncmp(header.wave, "WAVE", 4) != 0 ||
        strncmp(header.fmt_chunk_marker, "fmt ", 4) != 0 || strncmp(header.data_chunk_marker, "data", 4) != 0 ||
        header.audio_format != 1 || header.num_channels != 1 || header.bits_per_sample != 16) {
        fprintf(stderr, "Error: Unsupported WAV format. Requires 16-bit PCM mono. %s\n", filepath);
        fclose(file);
        return NULL;
    }

    *samplerate_out = header.sample_rate;
    *num_samples_out = header.data_size / (header.bits_per_sample / 8);

    fixed_point_t* audio_data_fx = (fixed_point_t*)malloc(*num_samples_out * sizeof(fixed_point_t));
    if (!audio_data_fx) {
        fprintf(stderr, "Error: Memory allocation failed for fixed-point audio data.\n");
        fclose(file);
        return NULL;
    }

    int16_t sample_int;
    for (long i = 0; i < *num_samples_out; ++i) {
        if (fread(&sample_int, sizeof(int16_t), 1, file) != 1) {
             fprintf(stderr, "Error: Could not read sample %ld from WAV file.\n", i);
             free(audio_data_fx);
             fclose(file);
             return NULL;
        }
        // Convert int16_t sample (Q0.15) to Q2.29 fixed-point.
        // Shift left by (Q_FORMAT - 15) = (29 - 15) = 14 bits.
        audio_data_fx[i] = ((fixed_point_t)sample_int << (Q_FORMAT - 15));
    }

    fclose(file);
    return audio_data_fx;
}

/**
 * @brief Writes a fixed-point (Q2.29) audio array to a 16-bit PCM mono WAV file.
 * @param filepath The path to the output WAV file.
 * @param audio_data_fx Pointer to the fixed-point audio data (normalized to [-Q_ONE, Q_ONE]).
 * @param num_samples The number of samples in the audio_data_fx array.
 * @param samplerate The sample rate of the audio in Hz.
 * Assumptions: Output WAV will be 16-bit PCM, mono.
 */
void write_wav_data_fx(const char* filepath, const fixed_point_t* audio_data_fx, long num_samples, uint32_t samplerate) {
    FILE* file = fopen(filepath, "wb");
    if (!file) {
        fprintf(stderr, "Error: Could not open file for writing %s\n", filepath);
        return;
    }

    WavHeader header;
    // Fill in RIFF, WAVE, fmt, and data chunk markers
    strncpy(header.riff, "RIFF", 4);
    strncpy(header.wave, "WAVE", 4);
    strncpy(header.fmt_chunk_marker, "fmt ", 4);
    strncpy(header.data_chunk_marker, "data", 4);

    // Set format parameters
    header.audio_format = 1;      // PCM
    header.num_channels = 1;      // Mono
    header.sample_rate = samplerate;
    header.bits_per_sample = 16;  // 16 bits per sample
    header.byte_rate = header.sample_rate * header.num_channels * (header.bits_per_sample / 8);
    header.block_align = header.num_channels * (header.bits_per_sample / 8);
    header.fmt_chunk_size = 16;   // Size of the fmt subchunk for PCM
    header.data_size = num_samples * header.num_channels * (header.bits_per_sample / 8);
    header.overall_size = header.data_size + 36; // 36 bytes = size of header without data_size

    // Write the WAV header to the file
    fwrite(&header, 1, sizeof(WavHeader), file);

    // Convert fixed-point samples back to 16-bit integers and write them
    int16_t sample_int;
    for (long i = 0; i < num_samples; ++i) {
        // Convert Q2.29 fixed-point to 16-bit signed int (Q0.15)
        // Shift right by (Q_FORMAT - 15) = (29 - 15) = 14 bits.
        int32_t temp_val = audio_data_fx[i] >> (Q_FORMAT - 15);

        // Clip to [-32768, 32767] range of int16_t.
        if (temp_val > INT16_MAX) sample_int = INT16_MAX;
        else if (temp_val < INT16_MIN) sample_int = INT16_MIN;
        else sample_int = (int16_t)temp_val;

        fwrite(&sample_int, sizeof(int16_t), 1, file);
    }

    fclose(file);
}

// --- Main application function ---
// This main function demonstrates how to process a WAV file frame by frame for tap detection.
// Compile: gcc audio_processor.c tap_detect.c -o tap_detector -lm
// Run: ./tap_detector input_audio.wav
int main(int argc, char *argv[]) {
    // Seed the random number generator for the dummy tap detector
    srand(time(NULL));

    // Check command line arguments
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_wav_file>\n", argv[0]);
        return 1;
    }
    const char* input_wav_filepath = argv[1];
    const char* output_binary_wav_filepath = "tap_detection_output.wav"; // Output file name

    uint32_t samplerate;
    long total_num_samples;
    // Read the entire WAV file into a dynamically allocated buffer.
    fixed_point_t* full_audio_data = read_wav_data_fx(input_wav_filepath, &samplerate, &total_num_samples);
    if (!full_audio_data) {
        fprintf(stderr, "Failed to load audio from %s. Exiting.\n", input_wav_filepath);
        return 1;
    }

    // Dynamically allocate a buffer for the binary tap detection output signal.
    // This buffer will be filled with Q_ONE (1.0), Q_ONE/2 (0.5), or 0 based on detection.
    fixed_point_t* tap_detection_output_fx = (fixed_point_t*)calloc(total_num_samples, sizeof(fixed_point_t));
    if (!tap_detection_output_fx) {
        fprintf(stderr, "Error: Memory allocation failed for tap_detection_output_fx buffer.\n");
        free(full_audio_data);
        return 1;
    }

    printf("Processing WAV file: %s (Samplerate: %u Hz, Total Samples: %ld)\n",
           input_wav_filepath, samplerate, total_num_samples);
    printf("--- Tap Detection Log by Frame ---\n");
    // MAX_AUDIO_FRAME_SIZE should be defined in tap_detect.h, assuming a value for now if not present.
    // If MAX_AUDIO_FRAME_SIZE is not defined in tap_detect.h, please add it there.
    // For this example, let's assume a default if it's not available yet.
    #ifndef MAX_AUDIO_FRAME_SIZE
    #define MAX_AUDIO_FRAME_SIZE 256 // Default frame size if not defined in tap_detect.h
    #endif
    printf("Frame Size: %d samples\n", MAX_AUDIO_FRAME_SIZE);
    printf("----------------------------------\n");
    printf("Frame | Start Time (s) | Tap Detected?\n");
    printf("----------------------------------\n");

    long frame_count = 0;
    // Iterate through the full audio data in chunks (frames)
    for (long current_sample_idx = 0; current_sample_idx < total_num_samples; current_sample_idx += MAX_AUDIO_FRAME_SIZE) {
        long current_frame_len = MAX_AUDIO_FRAME_SIZE;
        // Adjust frame length for the last chunk if it's smaller than MAX_AUDIO_FRAME_SIZE
        if (current_sample_idx + current_frame_len > total_num_samples) {
            current_frame_len = total_num_samples - current_sample_idx;
        }

        // Check if the remaining frame is too small
        if (current_frame_len < 2) { // Need at least 2 samples for DWT in a real scenario
            break;
        }

        // Call the tap detection function for the current frame
        tap_detection_result_e tap_detected_in_this_frame = tap_detect_status(
                                                                &full_audio_data[current_sample_idx],
                                                                &full_audio_data[current_sample_idx], // Dummy: second arg (processed_frame_out) not used by dummy
                                                                current_frame_len);

        // Log the result for the current frame
        printf("%5ld | %14.3f | %d\n",
               frame_count++,
               (float)current_sample_idx / samplerate,
               tap_detected_in_this_frame);

        // Fill the output binary WAV buffer for this frame based on the enum result
        fixed_point_t mapped_fill_value = 0; // Default to NO_TAP (0)
        if (tap_detected_in_this_frame == TAP_SINGLE) {
            mapped_fill_value = 1 << 16; // Represents 0.5 (half full scale)
        } else if (tap_detected_in_this_frame == TAP_DOUBLE) {
            mapped_fill_value = 1 << 31; // Represents 1.0 (full scale)
        }
        // You can use different mapping for other enum values if you extend it
        // else if (tap_detected_in_this_frame == TRIPLE_TAP) { ... }

        for (long i = 0; i < current_frame_len; ++i) {
            tap_detection_output_fx[current_sample_idx + i] = mapped_fill_value;
        }
    }
    printf("----------------------------------\n");

    // --- Write the binary tap detection output to a WAV file ---
    write_wav_data_fx(output_binary_wav_filepath, tap_detection_output_fx, total_num_samples, samplerate);
    printf("Binary tap detection output saved to: %s\n", output_binary_wav_filepath);

    // Free all dynamically allocated buffers
    free(full_audio_data);
    free(tap_detection_output_fx);
    printf("Processing complete.\n");

    return 0;
}
