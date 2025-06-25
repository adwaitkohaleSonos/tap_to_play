#include <stdio.h>    // For console I/O (printf, fprintf)
#include <stdlib.h>   // For memory allocation (malloc, free), random numbers (rand, srand)
#include <stdint.h>   // For fixed-size integer types (uint32_t, int16_t, int32_t, int64_t)
#include <string.h>   // For memcpy (to copy frames)
#include <math.h>     // For fabsf
#include "tap_detect.h"

// Define M_PI if it's not defined by default (some compilers might require _USE_MATH_DEFINES)
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif



// --- REMOVED: Minimum floor for transient normalization ---
// This parameter is no longer needed with absolute thresholding.
// #define MIN_TRANSIENT_NORMALIZATION_FLOOR_F 0.005f


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
 * @brief Reads 16-bit PCM mono audio data from a WAV file into a dynamically allocated fixed-point array.
 * @param filepath The path to the input WAV file.
 * @param samplerate_out Pointer to a uint32_t to store the sample rate read from the header.
 * @param num_samples_out Pointer to a long to store the total number of audio samples read.
 * @return A pointer to a newly allocated int32_t array containing the fixed-point audio data,
 * or NULL if an error occurs. The caller is responsible for freeing this memory.
 * Assumptions: Input WAV is 16-bit PCM, mono.
 */
int32_t* read_wav_data_fx(const char* filepath, uint32_t* samplerate_out, long* num_samples_out) {
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

    int32_t* audio_data_fx = (int32_t*)malloc(*num_samples_out * sizeof(int32_t));
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
        // Convert int16_t sample to Q2.29 fixed-point.
        // int16_t ranges from -32768 to +32767. Q2.29 has 29 fractional bits.
        // To convert Q0.15 (16-bit int) to Q2.29, we shift left by (29 - 15) = 14 bits.
        audio_data_fx[i] = ((int32_t)sample_int << (Q_BITS - 15));
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
void write_wav_data_fx(const char* filepath, const int32_t* audio_data_fx, long num_samples, uint32_t samplerate) {
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
        // Convert Q2.29 to Q0.15 (16-bit signed int) by shifting right by (29-15) = 14 bits.
        // Then clip to [-32768, 32767] range of int16_t.
        sample_int = (int16_t)(audio_data_fx[i] >> (Q_BITS - 15));
        // Manual clipping to int16_t min/max for robustness, though FX_CLIP should handle overall.
        if (sample_int > 32767) sample_int = 32767;
        if (sample_int < -32768) sample_int = -32768;

        fwrite(&sample_int, sizeof(int16_t), 1, file);
    }

    fclose(file);
}

// --- Main application function ---
// This main function demonstrates how to process a WAV file frame by frame for tap detection.
// Compile: gcc your_file.c -o tap_detector -lm
// Run: ./tap_detector input_audio.wav
int main(int argc, char *argv[]) {
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
    // In a true embedded system, this data would come from an ADC stream.
    int32_t* full_audio_data = read_wav_data_fx(input_wav_filepath, &samplerate, &total_num_samples);
    if (!full_audio_data) {
        fprintf(stderr, "Failed to load audio from %s. Exiting.\n", input_wav_filepath);
        return 1;
    }

    // Dynamically allocate a buffer for the binary tap detection output signal.
    // This buffer will be filled with Q_ONE (1.0) or 0 based on detection.
    int32_t* tap_detection_output_fx = (int32_t*)calloc(total_num_samples, sizeof(int32_t));
    if (!tap_detection_output_fx) {
        fprintf(stderr, "Error: Memory allocation failed for tap_detection_output_fx buffer.\n");
        free(full_audio_data);
        return 1;
    }

    printf("Processing WAV file: %s (Samplerate: %u Hz, Total Samples: %ld)\n",
           input_wav_filepath, samplerate, total_num_samples);
    printf("--- Tap Detection Log by Frame ---\n");
    printf("Frame Size: %d samples\n", MAX_AUDIO_FRAME_SIZE);
    // Removed normalization floor log as it's no longer used.
    printf("----------------------------------\n");
    printf("Frame | Start Time (s) | Tap Detected?\n");
    printf("----------------------------------\n");

    // Parameters for tap detection (tune these for your specific taps and noise)
    // This is now an ABSOLUTE threshold. Tune it carefully based on expected tap amplitude.
    // A value of 0.1 means 10%% of full scale.
    float absolute_transient_threshold = 0.03f;
    float min_peak_distance_s = 0.03f;       // Minimum separation in seconds

    long frame_count = 0;
    // Iterate through the full audio data in chunks (frames)
    for (long current_sample_idx = 0; current_sample_idx < total_num_samples; current_sample_idx += MAX_AUDIO_FRAME_SIZE) {
        long current_frame_len = MAX_AUDIO_FRAME_SIZE;
        // Adjust frame length for the last chunk if it's smaller than MAX_AUDIO_FRAME_SIZE
        if (current_sample_idx + current_frame_len > total_num_samples) {
            current_frame_len = total_num_samples - current_sample_idx;
        }

        // Check if the remaining frame is too small for DWT (needs at least 2 samples).
        if (current_frame_len < 2) {
            break;
        }

        // Call the tap detection function for the current frame
        /*int tap_detected_in_this_frame = detect_single_tap_binary_for_frame(
            &full_audio_data[current_sample_idx], // Pointer to the start of the current frame
            current_frame_len,                   // Actual length of the current frame
            samplerate,
            absolute_transient_threshold, // Pass the absolute threshold
            min_peak_distance_s
        );*/
        bool tap_detected_in_this_frame = tap_detect_status(&full_audio_data[current_sample_idx],
                                                            &full_audio_data[current_sample_idx],
                                                            current_frame_len);

        // Log the result for the current frame
        printf("%5ld | %14.3f | %s\n",
               frame_count++,
               (float)current_sample_idx / samplerate,
               (tap_detected_in_this_frame) ? "YES" : "NO");

        // Fill the output binary WAV buffer for this frame
        int32_t fill_value = tap_detected_in_this_frame ? Q_ONE : 0;
        for (long i = 0; i < current_frame_len; ++i) {
            tap_detection_output_fx[current_sample_idx + i] = fill_value;
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
