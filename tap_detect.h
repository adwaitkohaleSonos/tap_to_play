#ifndef TAP_DETECT_H
#define TAP_DETECT_H
#include <stdint.h>
#include <stdbool.h>

// IMPORTANT: This defines the maximum FRAME SIZE (chunk of audio) your system will process at once.
// All internal algorithm buffers are sized based on this.
// A typical frame size might be 512 or 1024 samples.
#define MAX_AUDIO_FRAME_SIZE 192

// Max length of cD1 coefficients for one frame is MAX_AUDIO_FRAME_SIZE / 2.
// Add 1 for safety with integer division/array indexing.
#define MAX_CD1_LEN (MAX_AUDIO_FRAME_SIZE / 2 + 1)

// Max number of peaks. In worst case, almost every other sample can be a local max.
// Use MAX_CD1_LEN as a generous upper bound for simplicity.
#define MAX_DETECTED_PEAKS MAX_CD1_LEN

// --- Fixed-Point Configuration ---
// Q_BITS defines the number of fractional bits.
// For Q2.29 format in a 32-bit integer: 1 sign bit, 2 integer bits, 29 fractional bits.
// This provides sufficient range for audio (values from -2 to +1.999...) and precision.
#define Q_BITS 29
#define Q_ONE (1L << Q_BITS) // Represents the fixed-point value 1.0 (e.g., 2^29)

// Conversion macros between float and fixed-point
// FLOAT_TO_Q is primarily used for setting parameters or initial test values.
#define FLOAT_TO_Q(f)   ((int32_t)((f) * Q_ONE))
// Q_TO_FLOAT is used for converting fixed-point to float for human-readable output (e.g., times).
#define Q_TO_FLOAT(q)   (((float)(q)) / Q_ONE)

// Fixed-point multiplication: (a * b) / 2^Q_BITS
// Uses int64_t for intermediate product to prevent overflow before shifting.
#define FX_MULT(a, b)   ((int32_t)(((int64_t)(a) * (b)) >> Q_BITS))

// Fixed-point absolute value
#define FX_ABS(a)       ((a) < 0 ? -(a) : (a))

// Fixed-point clipping to [-Q_ONE, Q_ONE]
#define FX_CLIP(val, min_val, max_val) ((val) > (max_val) ? (max_val) : ((val) < (min_val) ? (min_val) : (val)))

// Fixed-point maximum value (new utility macro)
#define FX_MAX(a, b)    ((a) > (b) ? (a) : (b))

#define TRANSIENT_THRESHOLD_MIN_FLOAT (0.0075f)
#define TRANSIENT_THRESHOLD_MAX_FLOAT (0.0150f)
#define TRANSIENT_THRESHOLD_MIN_FXP   (int)(16106127)
#define TRANSIENT_THRESHOLD_MAX_FXP   (int)(16106127 << 1)
#define MAX_SIG_LEN_SIZE          (192) /* set based on current DMA config: 192 samples at 48KHz. */
//#define MAX_CD1_LEN               ((MAX_SIG_LEN_SIZE >> 1) + 1) /* Detail coefficients for level 1 are calculated by folding signals in half. */
#define ABS(_x)                   ((_x < 0) ? (0 - _x) : (_x))

#define TAP_INTERVAL_MS           (300) /* measured in recordings. */
#define TAP_INTERVAL_SAMPLES      (TAP_INTERVAL_MS * 48) /* 48KHz sampling rate */
#define TAP_INTERVAL_BLOCKS       ((TAP_INTERVAL_SAMPLES + MAX_AUDIO_FRAME_SIZE - 1)/MAX_AUDIO_FRAME_SIZE)

typedef enum
{
    TAP_NONE = 0,
    TAP_SINGLE = 1 << 8,
    TAP_DOUBLE = 1 << 16
} tap_detection_result_e;

tap_detection_result_e tap_detect_status(const int *mic1_sig, const int *mic2_sig, int audio_sig_len);


#endif // !TAP_DETECT_H
