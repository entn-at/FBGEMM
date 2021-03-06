#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

#include <cpuinfo.h>

namespace fbgemm {

// Copied from gemmlowp
//
// A structure to hold quantization parameters 'scale' and 'zero_point'.
// The meaning of these values is as the constants in the quantization equation
//
//   real_value = scale * (quantized_value - zero_point)
//
// In other words, 'zero_point' is the quantized value that corresponds
// to the real value 0, and 'scale' is the difference of real values
// corresponding to consecutive quantized values.
struct TensorQuantizationParams {
  float scale;
  std::int32_t zero_point;
  int precision;
  float Min() const;
  float Max() const;
};

TensorQuantizationParams ChooseQuantizationParams(
    float min,
    float max,
    std::int32_t qmin,
    std::int32_t qmax,
    bool preserve_sparsity = false,
    bool force_scale_power_of_two = false);

// Parameters when we scale from int32 intermediate matrix multiplication
// results to 8-bit integers
struct RequantizationParams {
  // For floating-point requantization
  float real_multiplier;

  // For fixed-point requantization
  std::int32_t multiplier;
  int right_shift;

  TensorQuantizationParams target_qparams;
};

void ChooseRequantizationMultiplier(
    float real_multiplier,
    std::int32_t* quantized_multiplier,
    int* right_shift,
    int requantization_multiplier_precision = 32);

////////////////////////////////////////////////////////////////////////////////
// Utility functions

/// Clamp src in T1 to the desired precision and convert it to T2
template <typename T1, typename T2 = std::uint8_t>
T2 clamp(T1 src, int precision, bool is_signed = false)
// TODO: T26263653 fix signed-integer-overflow undefined behavior
#if defined(__has_feature)
#if __has_feature(__address_sanitizer__)
    __attribute__((__no_sanitize__("signed-integer-overflow")))
#endif
#endif
{
  std::int32_t min = is_signed ? -(1LL << (precision - 1)) : 0;
  std::int32_t max =
      is_signed ? ((1LL << (precision - 1)) - 1) : (1LL << precision) - 1;

  // Make sure T1 and T2 can represent the precision
  assert(min >= std::numeric_limits<T1>::lowest());
  assert(min >= std::numeric_limits<T2>::lowest());
  assert(max <= std::numeric_limits<T1>::max());
  assert(max <= std::numeric_limits<T2>::max());

  return std::min<T1>(std::max<T1>(src, min), max);
}

/// Quantize src using zero_point and scale, clamp to the specified precision,
/// and convert it to type T
template <typename T>
T Quantize(
    float src,
    std::int32_t zero_point,
    float scale,
    int result_precision,
    bool result_is_signed = std::is_signed<T>::value) {
  const float transformed_val = zero_point + src / scale;
  return clamp<std::int64_t, T>(
      static_cast<std::int64_t>(std::nearbyint(transformed_val)),
      result_precision,
      result_is_signed);
}

template <typename T>
T Quantize(float src, const TensorQuantizationParams& qparams) {
  return Quantize<T>(
      src, qparams.zero_point, qparams.scale, qparams.precision);
}

template <typename T>
void Quantize(
    const float* src,
    T* dst,
    int len,
    const TensorQuantizationParams& qparams);

template <typename T>
float Dequantize(T src, const TensorQuantizationParams& qparams) {
  return qparams.scale * (src - qparams.zero_point);
}

template <typename T>
void Dequantize(
    const T* src,
    float* dst,
    int len,
    const TensorQuantizationParams& qparams) {
  for (std::size_t i = 0; i < len; i++) {
    dst[i] = Dequantize(src[i], qparams);
  }
}

/**
 * Find the min and max value in a float matrix.
 */
void FindMinMax(const float* m, float* min, float* max, int len);

////////////////////////////////////////////////////////////////////////////////
// Requantization (pure fixed-point)

std::int64_t
SaturatingRoundingMulWithShift(std::int32_t a, std::int32_t b, int right_shift);

template <typename T>
T Requantize(
    std::int32_t src, // int32 input before requantization
    std::int32_t zero_point,
    std::int32_t multiplier,
    int right_shift,
    int result_precision,
    bool result_is_signed = false) {
  std::int64_t quantized_down =
      zero_point + SaturatingRoundingMulWithShift(src, multiplier, right_shift);
  return clamp<std::int64_t, T>(
      quantized_down, result_precision, result_is_signed);
}

template <typename T>
T RequantizeFixedPoint(
    std::int32_t src, // int32 input before requantization
    const RequantizationParams& params) {
  return Requantize<T>(
      src,
      params.target_qparams.zero_point,
      params.multiplier,
      params.right_shift,
      params.target_qparams.precision);
}

void RequantizeFixedPointAvx2(
    const std::int32_t* src,
    std::uint8_t* dst,
    int len,
    const RequantizationParams& params);

template <typename T>
void RequantizeFixedPoint(
    const std::int32_t* src,
    T* dst,
    int len,
    const RequantizationParams& params);

////////////////////////////////////////////////////////////////////////////////
// Requantization (with floats)

template <typename T>
T Requantize(
    std::int32_t src, // int32 input before requantization
    std::int32_t zero_point,
    float multiplier,
    int result_precision,
    bool result_is_signed = false) {
  long quantized_down = zero_point + std::lrintf(src * multiplier);
  return clamp<long, T>(quantized_down, result_precision, result_is_signed);
}

template <typename T>
T Requantize(
    std::int32_t src, // int32 input before requantization
    const RequantizationParams& params) {
  return Requantize<T>(
      src,
      params.target_qparams.zero_point,
      params.real_multiplier,
      params.target_qparams.precision);
}

void RequantizeAvx2(
    const std::int32_t* src,
    std::uint8_t* dst,
    int len,
    const RequantizationParams& params);

template <typename T>
void Requantize(
    const std::int32_t* src,
    T* dst,
    int len,
    const RequantizationParams& params);

} // namespace fbgemm
