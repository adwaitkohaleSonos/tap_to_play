#include <stdio.h>   // For memory allocation (malloc, free), random numbers (rand, srand)
#include "tap_detect.h"

// --- Static Buffers for DSP Operations ---
// These buffers are allocated in static memory (e.g., .data or .bss section) at compile time.
// They consume memory constantly but avoid runtime dynamic allocation overhead.

static int coeff_cd1[MAX_CD1_LEN]                = { 0 };
static int normalized_transient_sig[MAX_CD1_LEN] = { 0 };
static int analysis_sig[MAX_SIG_LEN_SIZE]        = { 0 };

static void tap_detect_haar_dwt_l1(const int *inp_sig, int sig_len, int *coeff_cd1, int *cd_len_out)
{
    *cd_len_out = sig_len >> 1;
    for (int n = 0; n < *cd_len_out; n++)
    {
        coeff_cd1[n] = inp_sig[(n << 1) + 1] - inp_sig[(n << 1)];
    }
}

// --- Peak Detection Logic (Simplified for Embedded, Static Memory) ---
static void tap_detect_find_peaks(int *inp_sig, int sig_len, int min_threshold, int max_threshold, int *num_peaks_out)
{
    /* n = 0 */
    if ((inp_sig[0] >= min_threshold) && (inp_sig[0] <= max_threshold) && (inp_sig[0] > inp_sig[1]))
    {
        *num_peaks_out += 1;
    }
    for (int n = 1; n < (sig_len - 1); n++)
    {
        if ((inp_sig[n] >= min_threshold) && (inp_sig[n] <= max_threshold) && (inp_sig[n] > inp_sig[n - 1]) && (inp_sig[n] > inp_sig[n + 1]))
        {
            *num_peaks_out += 1;
        }
    }

    /* n = sig_len - 1 */
    if ((inp_sig[(sig_len - 1)] >= min_threshold) && (inp_sig[(sig_len - 1)] < max_threshold) && (inp_sig[(sig_len - 1)] > inp_sig[(sig_len - 2)]))
    {
        *num_peaks_out += 1;
    }
}

// --- Single Tap Detection Logic (Binary Output, Static Memory) ---
bool tap_detect_status(const int *mic1_sig, const int *mic2_sig, int audio_sig_len)
{
    bool result = false;
    int ndx;
    /* weighted average mic signals for analysis */
    for (int n = 0; n < audio_sig_len; n++)
    {
        analysis_sig[n] = (mic1_sig[n] + mic2_sig[n]) >> 1; /* simple averaging */
        //analysis_sig[n] = (3 * mic1_sig[n] + mic2_sig[n]) >> 2; /* weighted averaging: 0.75 * mic1, 0.25 * mic2 */
        //analysis_sig[n] = (mic1_sig[n] + (3 * mic2_sig[n])) >> 2; /* weighted averaging: 0.25 * mic1, 0.75 * mic2 */
    }

    /* Wavelet transform - use detail coefficients of level 1. Should cover 12000:24000Hz for mic signals sampled at 48kHz */
    int cd_len = 0;
    tap_detect_haar_dwt_l1(&analysis_sig[0], audio_sig_len, &coeff_cd1[0], &cd_len);

    /* Check if there are any peaks */
    int num_peaks_detected = 0;
    tap_detect_find_peaks(&coeff_cd1[0], cd_len, TRANSIENT_THRESHOLD_MIN_FXP, TRANSIENT_THRESHOLD_MAX_FXP, &num_peaks_detected);
    if (num_peaks_detected > 0)
    {
        result = true;
    }
    return result;
}
