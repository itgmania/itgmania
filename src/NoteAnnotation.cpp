#include "NoteAnnotation.h"

#include <cstdint>
#include <cstring>
#include <sstream>

#include "mbedtls/base64.h"

int base64_encode(const std::vector<uint8_t>& input, std::string& output) {
  size_t out_len = 0;
  // First call with null output to get required buffer size
  mbedtls_base64_encode(nullptr, 0, &out_len, input.data(), input.size());

  output.resize(out_len, '\0');
  int encode_result = mbedtls_base64_encode(
      reinterpret_cast<unsigned char*>(output.data()), out_len, &out_len,
      input.data(), input.size());
  output.resize(out_len);  // trim null terminator mbedtls adds
  return encode_result;
}

int base64_decode(const std::string& input, std::vector<uint8_t>& output) {
  size_t out_len = 0;
  // First call to get required buffer size
  mbedtls_base64_decode(
      nullptr, 0, &out_len,
      reinterpret_cast<const unsigned char*>(input.data()), input.size());

  output.resize(out_len);
  int decode_result = mbedtls_base64_decode(
      output.data(), output.size(), &out_len,
      reinterpret_cast<const unsigned char*>(input.data()), input.size());
  output.resize(out_len);
  return decode_result;
}

static void MakeUrlSafe(std::string& s) {
  for (char& c : s) {
    if (c == '+') {
      c = '-';
    } else if (c == '/') {
      c = '_';
    }
  }
}

static void MakeStandardBase64(std::string& s) {
  for (char& c : s) {
    if (c == '-') {
      c = '+';
    } else if (c == '_') {
      c = '/';
    }
  }
}

/**
 * Serializes a NoteAnnotation into an 11-byte representation
 *
 * bytes 1-4: beat
 * bytes 5-9: whereTheFeetAre
 * bytes 10-11: tech bitmask
 */
void SerializeAnnotation(const NoteAnnotation& ann, std::vector<uint8_t>& out) {
  size_t offset = out.size();
  out.resize(offset + 11);

  // 4 bytes: beat (float)
  std::memcpy(&out[offset], &ann.beat_, 4);
  offset += 4;

  // 5 bytes: whereTheFeetAre (int8_t each, -1 = no position)
  for (int i = 0; i < 5; i++) {
    out[offset++] = static_cast<int8_t>(ann.where_the_feet_are_[i]);
  }

  // 2 bytes: tech bitmask
  uint16_t tech_mask = 0;
  for (TechCountsCategory cat : ann.tech_) {
    if (cat < NUM_TechCountsCategory) {
      tech_mask |= (1u << cat);
    }
  }
  out[offset++] = tech_mask & 0xFF;
  out[offset++] = (tech_mask >> 8) & 0xFF;
}

NoteAnnotation DeserializeAnnotation(const uint8_t* buf) {
  NoteAnnotation ann;
  size_t offset = 0;

  // 4 bytes: beat
  std::memcpy(&ann.beat_, &buf[offset], 4);
  offset += 4;

  // 5 bytes: whereTheFeetAre
  ann.where_the_feet_are_.resize(5);
  for (int i = 0; i < 5; i++) {
    ann.where_the_feet_are_[i] = static_cast<int8_t>(buf[offset++]);
  }

  // 2 bytes: tech bitmask
  uint16_t tech_mask = buf[offset] | (buf[offset + 1] << 8);
  ann.tech_.clear();
  for (int i = 0; i < NUM_TechCountsCategory; i++) {
    if (tech_mask & (1u << i)) {
      ann.tech_.push_back(static_cast<TechCountsCategory>(i));
    }
  }

  return ann;
}

void NoteAnnotation::FromString(
    const std::string& input, std::vector<NoteAnnotation>& out) {
  std::string input_copy = input;
  MakeStandardBase64(input_copy);
  std::vector<uint8_t> raw;
  out.clear();
  int decode_result = base64_decode(input_copy, raw);

  if (decode_result != 0 || raw.size() % 11 != 0) {
    LOG->Warn(
        "Could not decode NoteAnnotations string. decode_result=%d, raw.size() "
        "mod 11 = %zu",
        decode_result, raw.size() % 11);
    return;
  }

  size_t annotation_count = raw.size() / 11;
  out.reserve(annotation_count);

  for (size_t i = 0; i < annotation_count; i++) {
    out.push_back(DeserializeAnnotation(&raw[i * 11]));
  }
}

std::string NoteAnnotation::ToString(std::vector<NoteAnnotation>& annotations) {
  std::vector<uint8_t> raw;
  raw.reserve(annotations.size() * 11);
  for (const auto& ann : annotations) {
    SerializeAnnotation(ann, raw);
  }
  std::string output;
  int encode_result = base64_encode(raw, output);
  if (encode_result != 0) {
    LOG->Warn(
        "Could not encode Note Annotations. encode_result=%d", encode_result);
    return "";
  }

  MakeUrlSafe(output);
  return output;
}

const std::string& NoteAnnotationCache::GetCompressed() const {
  return compressed_;
}

const std::vector<NoteAnnotation>& NoteAnnotationCache::GetNoteAnnotations()
    const {
  if (decompressed_.empty() && !compressed_.empty()) {
    NoteAnnotation::FromString(compressed_, decompressed_);
  }
  return decompressed_;
}

void NoteAnnotationCache::Compress() {
  if (decompressed_.size() > 0) {
    compressed_ = NoteAnnotation::ToString(decompressed_);
  } else {
    compressed_ = "";
  }
  decompressed_.clear();
}
