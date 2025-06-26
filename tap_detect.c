#include <stdio.h>   // For memory allocation (malloc, free), random numbers (rand, srand)
#include "tap_detect.h"

// --- Static Buffers for DSP Operations ---
// These buffers are allocated in static memory (e.g., .data or .bss section) at compile time.
// They consume memory constantly but avoid runtime dynamic allocation overhead.

static int coeff_cd1[MAX_CD1_LEN]                = { 0 };
static int analysis_sig[MAX_SIG_LEN_SIZE]        = { 0 };

typedef enum
{
    TAP_STATE_IDLE = 0,
    TAP_STATE_WAITING_FOR_SECOND
} tap_detection_state_e;

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
    // n = 0
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

    // n = sig_len - 1
    if ((inp_sig[(sig_len - 1)] >= min_threshold) && (inp_sig[(sig_len - 1)] <= max_threshold) && (inp_sig[(sig_len - 1)] > inp_sig[(sig_len - 2)]))
    {
        *num_peaks_out += 1;
    }
}

static int32_t cooldown_block_cnt = 100;
static int32_t block_cnt_since_last_tap = 0;
static int32_t current_block_cnt = 0;
static int32_t last_tap_detected_block_cnt = 0;
static tap_detection_state_e current_tap_state = TAP_STATE_IDLE;
/*tap_detection_result_e tap_detect_status(const int *mic1_sig, const int *mic2_sig, int audio_sig_len)
{
    tap_detection_result_e result = TAP_NONE;
    current_block_cnt++;
    // weighted average mic signals for analysis
    for (int n = 0; n < audio_sig_len; n++)
    {
        analysis_sig[n] = (mic1_sig[n] + mic2_sig[n]) >> 1; // simple averaging
        //analysis_sig[n] = (3 * mic1_sig[n] + mic2_sig[n]) >> 2; // weighted averaging: 0.75 * mic1, 0.25 * mic2
        //analysis_sig[n] = (mic1_sig[n] + (3 * mic2_sig[n])) >> 2; // weighted averaging: 0.25 * mic1, 0.75 * mic2
    }

    // Wavelet transform - use detail coefficients of level 1. Should cover 12000:24000Hz for mic signals sampled at 48kHz
    int cd_len = 0;
    tap_detect_haar_dwt_l1(&analysis_sig[0], audio_sig_len, &coeff_cd1[0], &cd_len);

    // Check if there are any peaks
    int num_peaks_detected = 0;
    if (cooldown_block_cnt == 0)
    {
        tap_detect_find_peaks(&coeff_cd1[0], cd_len, TRANSIENT_THRESHOLD_MIN_FXP, TRANSIENT_THRESHOLD_MAX_FXP, &num_peaks_detected);
    }
    else
    {
        // cooldown, no peak detection
        cooldown_block_cnt--;
    }

    if (num_peaks_detected > 0)
    {
        cooldown_block_cnt = 60;

        if (block_cnt_since_last_tap <= 130)
        {
            result = TAP_DOUBLE;
        }
        else
        {
            result = TAP_SINGLE;
        }
        block_cnt_since_last_tap = 0;
    }
    block_cnt_since_last_tap++;


    return result;// = (num_peaks_detected > 0) ? (TAP_DOUBLE) : (TAP_NONE);
}
*/

// --- Main Tap Detection Logic ---
// current_block_number must be supplied by the caller and should increment with each processed block.
// **MINIMAL CHANGES START HERE:** New static variables to manage tap state
static bool     first_tap_pending = false; // True if a first tap was detected and we are waiting for a second
static uint32_t first_tap_block_time = 0;  // Stores the block number when the first tap was detected

tap_detection_result_e tap_detect_status(const int *mic1_sig, const int *mic2_sig, int audio_sig_len)
{
    tap_detection_result_e result = TAP_NONE; // Default result for this block
    current_block_cnt++;                      // Increment global block counter for time reference

    /* --- Signal Processing --- */
    for (int n = 0; n < audio_sig_len; n++)
    {
        analysis_sig[n] = (mic1_sig[n] + mic2_sig[n]) >> 1;
    }

    int cd_len = 0;
    tap_detect_haar_dwt_l1(&analysis_sig[0], audio_sig_len, &coeff_cd1[0], &cd_len);

    /* --- Peak Detection with Cooldown/Debounce --- */
    int num_peaks_this_block = 0; // Counter for raw peaks in current block

    if (cooldown_block_cnt == 0) // Only look for peaks if not in cooldown
    {
        tap_detect_find_peaks(&coeff_cd1[0], cd_len, TRANSIENT_THRESHOLD_MIN_FXP, TRANSIENT_THRESHOLD_MAX_FXP, &num_peaks_this_block);
    }
    else
    {
        cooldown_block_cnt--; // Decrement cooldown timer
    }

    // Determine if a *new, distinct* tap event has occurred based on peak and cooldown
    bool is_new_distinct_tap = (num_peaks_this_block > 0);
    if (is_new_distinct_tap)
    {
        cooldown_block_cnt = 40; // Reset cooldown for next peak detection
    }

    /* --- Tap Sequence Logic (Minimal Changes Applied Here) --- */

    if (is_new_distinct_tap) // Logic when a NEW, DEBOUNCED tap is detected in this block
    {
        if (first_tap_pending)
        {
            // We were waiting for a second tap. This is it!
            uint32_t blocks_since_first_tap = current_block_cnt - first_tap_block_time;

            if (blocks_since_first_tap <= 130)
            {
                // It's a **VALID DOUBLE TAP!**
                result = TAP_DOUBLE;
                // Reset state to IDLE for next sequence
                first_tap_pending = false;
                first_tap_block_time = 0;
            }
            else
            {
                // This second tap arrived too late.
                // The *previous* tap (the one that set first_tap_pending) has now effectively timed out as a single tap.
                result = TAP_SINGLE; // Report the *previous* tap as a single tap
                // Now, this *current* tap becomes the start of a new potential sequence.
                first_tap_pending = true;
                first_tap_block_time = current_block_cnt; // Record time for this new first tap
            }
        }
        else // first_tap_pending is false: This is the very first logical tap in a new sequence
        {
            first_tap_pending = true;
            first_tap_block_time = current_block_cnt; // Mark its occurrence time
            // No result returned yet, as we are waiting for a potential second tap or a timeout for this one.
        }
    }
    else // No new, distinct tap occurred in this block. Check for single tap timeout.
    {
        // If a first tap is pending AND its time window for a second tap has expired
        if (first_tap_pending && ((current_block_cnt - first_tap_block_time) > 130))
        {
            // **SINGLE TAP concluded by timeout!**
            result = TAP_SINGLE;
            // Reset state to IDLE for next sequence
            first_tap_pending = false;
            first_tap_block_time = 0;
        }
    }

    return result;
}
