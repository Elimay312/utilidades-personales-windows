#include "system/nightlight_blob.h"

#include <algorithm>

namespace panel::cb {
namespace {

constexpr uint8_t kMarshaled[] = {0x43, 0x42, 0x01, 0x00};  // "CB", version 1

bool ReadVarint(std::span<const uint8_t> bytes, size_t& pos, uint64_t& out) {
  out = 0;
  for (int shift = 0; shift < 64; shift += 7) {
    if (pos >= bytes.size()) return false;
    const uint8_t byte = bytes[pos++];
    out |= static_cast<uint64_t>(byte & 0x7F) << shift;
    if ((byte & 0x80) == 0) return true;
  }
  return false;
}

void WriteVarint(uint64_t value, std::vector<uint8_t>& out) {
  do {
    uint8_t byte = value & 0x7F;
    value >>= 7;
    if (value != 0) byte |= 0x80;
    out.push_back(byte);
  } while (value != 0);
}

// A field header: 3 bits of id, or a marker that the id follows in one or two bytes, and 5
// bits of type.
bool ReadHeader(std::span<const uint8_t> bytes, size_t& pos, uint16_t& id, uint8_t& type) {
  if (pos >= bytes.size()) return false;
  const uint8_t byte = bytes[pos++];
  type = byte & 0x1F;
  const uint8_t small = byte >> 5;
  if (small <= 5) {
    id = small;
  } else if (small == 6) {
    if (pos >= bytes.size()) return false;
    id = bytes[pos++];
  } else {
    if (pos + 1 >= bytes.size()) return false;
    id = static_cast<uint16_t>(bytes[pos] | (bytes[pos + 1] << 8));
    pos += 2;
  }
  return true;
}

void WriteHeader(uint16_t id, uint8_t type, std::vector<uint8_t>& out) {
  if (id <= 5) {
    out.push_back(static_cast<uint8_t>((id << 5) | type));
  } else if (id <= 0xFF) {
    out.push_back(static_cast<uint8_t>(0xC0 | type));
    out.push_back(static_cast<uint8_t>(id));
  } else {
    out.push_back(static_cast<uint8_t>(0xE0 | type));
    out.push_back(static_cast<uint8_t>(id & 0xFF));
    out.push_back(static_cast<uint8_t>(id >> 8));
  }
}

bool SkipValue(std::span<const uint8_t> bytes, size_t& pos, uint8_t type, int depth);

// Past a struct's fields and its STOP.
bool SkipStruct(std::span<const uint8_t> bytes, size_t& pos, int depth) {
  if (depth > 16) return false;  // nothing Windows writes nests like this
  for (;;) {
    if (pos >= bytes.size()) return false;
    if (bytes[pos] == kStop) {
      ++pos;
      return true;
    }
    if (bytes[pos] == kStopBase) {
      ++pos;
      continue;
    }
    uint16_t id = 0;
    uint8_t type = 0;
    if (!ReadHeader(bytes, pos, id, type) || !SkipValue(bytes, pos, type, depth + 1)) return false;
  }
}

bool SkipValue(std::span<const uint8_t> bytes, size_t& pos, uint8_t type, int depth) {
  uint64_t count = 0;
  switch (type) {
    case kBool:
    case kUInt8:
    case kInt8:
      if (pos >= bytes.size()) return false;
      ++pos;
      return true;
    case kUInt16:
    case kUInt32:
    case kUInt64:
    case kInt16:
    case kInt32:
    case kInt64:
      return ReadVarint(bytes, pos, count);
    case kFloat:
      pos += 4;
      return pos <= bytes.size();
    case kDouble:
      pos += 8;
      return pos <= bytes.size();
    case kString:
    case kWString:
      if (!ReadVarint(bytes, pos, count)) return false;
      if (count > bytes.size()) return false;
      pos += static_cast<size_t>(count) * (type == kWString ? 2 : 1);
      return pos <= bytes.size();
    case kStruct:
      return SkipStruct(bytes, pos, depth);
    case kList:
    case kSet: {
      // CompactBinary v1: the element type in a byte, then the count.
      if (pos >= bytes.size()) return false;
      const uint8_t element = bytes[pos++] & 0x1F;
      if (!ReadVarint(bytes, pos, count) || count > bytes.size()) return false;
      for (uint64_t i = 0; i < count; ++i) {
        if (!SkipValue(bytes, pos, element, depth + 1)) return false;
      }
      return true;
    }
    default:
      return false;  // maps and anything else: not something Night Light has
  }
}

}  // namespace

std::optional<Struct> ParseStruct(std::span<const uint8_t> bytes) {
  Struct fields;
  size_t pos = 0;
  for (;;) {
    if (pos >= bytes.size()) return std::nullopt;
    if (bytes[pos] == kStop) {
      ++pos;
      if (pos != bytes.size()) return std::nullopt;  // bytes after the STOP: not ours to guess
      return fields;
    }
    if (bytes[pos] == kStopBase) {
      ++pos;
      fields.push_back(Field{0, kStopBase, {}});
      continue;
    }
    Field field;
    if (!ReadHeader(bytes, pos, field.id, field.type)) return std::nullopt;
    const size_t start = pos;
    if (!SkipValue(bytes, pos, field.type, 0)) return std::nullopt;
    field.value.assign(bytes.begin() + static_cast<std::ptrdiff_t>(start),
                       bytes.begin() + static_cast<std::ptrdiff_t>(pos));
    fields.push_back(std::move(field));
  }
}

std::vector<uint8_t> EncodeStruct(const Struct& fields) {
  std::vector<uint8_t> out;
  for (const Field& field : fields) {
    if (field.type == kStopBase) {
      out.push_back(kStopBase);
      continue;
    }
    WriteHeader(field.id, field.type, out);
    out.insert(out.end(), field.value.begin(), field.value.end());
  }
  out.push_back(kStop);
  return out;
}

std::optional<Struct> ParseMarshaled(std::span<const uint8_t> bytes) {
  if (bytes.size() < sizeof(kMarshaled) || !std::equal(std::begin(kMarshaled), std::end(kMarshaled), bytes.begin())) {
    return std::nullopt;
  }
  return ParseStruct(bytes.subspan(sizeof(kMarshaled)));
}

std::vector<uint8_t> EncodeMarshaled(const Struct& fields) {
  std::vector<uint8_t> out(std::begin(kMarshaled), std::end(kMarshaled));
  const std::vector<uint8_t> body = EncodeStruct(fields);
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

const Field* Find(const Struct& fields, uint16_t id) {
  for (const Field& field : fields) {
    if (field.type != kStopBase && field.id == id) return &field;
  }
  return nullptr;
}

}  // namespace panel::cb

namespace panel {
namespace {

using cb::Field;
using cb::Struct;

Field* FindMutable(Struct& fields, uint16_t id) {
  for (Field& field : fields) {
    if (field.type != cb::kStopBase && field.id == id) return &field;
  }
  return nullptr;
}

// The envelope (docs/nightlight-registry-format.md): field 1 is the payload container; in it,
// field 0 the Unix timestamp and field 1 a struct whose field 1 is the payload as list<int8>.
struct Envelope {
  Struct outer;
  Struct container;
  Struct wrapper;
  std::vector<uint8_t> payload;
};

std::optional<Envelope> OpenEnvelope(std::span<const uint8_t> blob) {
  Envelope envelope;
  std::optional<Struct> outer = cb::ParseMarshaled(blob);
  if (!outer) return std::nullopt;
  envelope.outer = std::move(*outer);
  const Field* container = cb::Find(envelope.outer, 1);
  if (container == nullptr || container->type != cb::kStruct) return std::nullopt;
  std::optional<Struct> containerFields = cb::ParseStruct(container->value);
  if (!containerFields) return std::nullopt;
  envelope.container = std::move(*containerFields);
  const Field* wrapper = cb::Find(envelope.container, 1);
  if (wrapper == nullptr || wrapper->type != cb::kStruct) return std::nullopt;
  std::optional<Struct> wrapperFields = cb::ParseStruct(wrapper->value);
  if (!wrapperFields) return std::nullopt;
  envelope.wrapper = std::move(*wrapperFields);
  const Field* list = cb::Find(envelope.wrapper, 1);
  if (list == nullptr || list->type != cb::kList || list->value.empty() || (list->value[0] & 0x1F) != cb::kInt8) {
    return std::nullopt;
  }
  // The list's own bytes: the element type, the count, then the payload.
  size_t pos = 1;
  uint64_t count = 0;
  for (int shift = 0; pos < list->value.size(); shift += 7) {
    const uint8_t byte = list->value[pos++];
    count |= static_cast<uint64_t>(byte & 0x7F) << shift;
    if ((byte & 0x80) == 0) break;
  }
  if (pos + count != list->value.size()) return std::nullopt;
  envelope.payload.assign(list->value.begin() + static_cast<std::ptrdiff_t>(pos), list->value.end());
  return envelope;
}

std::vector<uint8_t> Varint(uint64_t value) {
  std::vector<uint8_t> out;
  do {
    uint8_t byte = value & 0x7F;
    value >>= 7;
    if (value != 0) byte |= 0x80;
    out.push_back(byte);
  } while (value != 0);
  return out;
}

std::vector<uint8_t> CloseEnvelope(Envelope envelope, const std::vector<uint8_t>& payload,
                                   std::optional<uint64_t> unixSeconds) {
  Field* list = FindMutable(envelope.wrapper, 1);
  list->value.assign(1, static_cast<uint8_t>(cb::kInt8));
  const std::vector<uint8_t> count = Varint(payload.size());
  list->value.insert(list->value.end(), count.begin(), count.end());
  list->value.insert(list->value.end(), payload.begin(), payload.end());
  FindMutable(envelope.container, 1)->value = cb::EncodeStruct(envelope.wrapper);
  if (unixSeconds) {
    if (Field* stamp = FindMutable(envelope.container, 0); stamp != nullptr && stamp->type == cb::kUInt64) {
      stamp->value = Varint(*unixSeconds);
    }
  }
  FindMutable(envelope.outer, 1)->value = cb::EncodeStruct(envelope.container);
  return cb::EncodeMarshaled(envelope.outer);
}

// A TimeBlock: field 0 the hour and field 1 the minute, as int8, each left out when zero.
int MinuteOf(const Field* block) {
  if (block == nullptr || block->type != cb::kStruct) return 0;
  const std::optional<Struct> fields = cb::ParseStruct(block->value);
  if (!fields) return 0;
  const auto part = [&fields](uint16_t id) {
    const Field* field = cb::Find(*fields, id);
    return field != nullptr && field->type == cb::kInt8 && !field->value.empty() ? field->value[0] : 0;
  };
  return std::min(part(0), 23) * 60 + std::min(part(1), 59);
}

}  // namespace

std::optional<std::vector<uint8_t>> EnvelopePayload(std::span<const uint8_t> blob) {
  std::optional<Envelope> envelope = OpenEnvelope(blob);
  if (!envelope) return std::nullopt;
  return envelope->payload;
}

std::optional<bool> ReadNightLightState(std::span<const uint8_t> blob) {
  const std::optional<std::vector<uint8_t>> payload = EnvelopePayload(blob);
  if (!payload) return std::nullopt;
  const std::optional<Struct> state = cb::ParseMarshaled(*payload);
  if (!state) return std::nullopt;
  // Presence is the signal, not the value (the format document, "Enabled State Semantics").
  return cb::Find(*state, 0) != nullptr;
}

bool ReadNightLightSettings(std::span<const uint8_t> blob, NightLightData& data) {
  const std::optional<std::vector<uint8_t>> payload = EnvelopePayload(blob);
  if (!payload) return false;
  const std::optional<Struct> settings = cb::ParseMarshaled(*payload);
  if (!settings) return false;
  const Field* enabled = cb::Find(*settings, 0);
  data.scheduled = enabled != nullptr && enabled->type == cb::kBool && !enabled->value.empty() && enabled->value[0] != 0;
  data.sunsetToSunrise = data.scheduled && cb::Find(*settings, 10) == nullptr;
  data.fromMinute = MinuteOf(cb::Find(*settings, data.sunsetToSunrise ? 50 : 20));
  data.toMinute = MinuteOf(cb::Find(*settings, data.sunsetToSunrise ? 60 : 30));
  return true;
}

bool RoundTrips(std::span<const uint8_t> blob) {
  const std::optional<Envelope> envelope = OpenEnvelope(blob);
  if (!envelope) return false;
  const std::optional<Struct> inner = cb::ParseMarshaled(envelope->payload);
  if (!inner || cb::EncodeMarshaled(*inner) != envelope->payload) return false;
  const std::vector<uint8_t> again = CloseEnvelope(*envelope, envelope->payload, std::nullopt);
  return std::equal(again.begin(), again.end(), blob.begin(), blob.end());
}

std::optional<std::vector<uint8_t>> WithNightLight(std::span<const uint8_t> stateBlob, bool on,
                                                   uint64_t unixSeconds, uint64_t filetime) {
  if (!RoundTrips(stateBlob)) return std::nullopt;
  std::optional<Envelope> envelope = OpenEnvelope(stateBlob);
  std::optional<Struct> state = cb::ParseMarshaled(envelope->payload);

  // Field 0, int32: present means on, and its value is always 0. Nothing else is touched but
  // field 20, the time of the last change, which Windows moves too.
  std::erase_if(*state, [](const Field& field) { return field.type != cb::kStopBase && field.id == 0; });
  if (on) state->insert(state->begin(), Field{0, cb::kInt32, {0x00}});
  if (Field* changed = FindMutable(*state, 20); changed != nullptr && changed->type == cb::kUInt64) {
    changed->value = Varint(filetime);
  }
  return CloseEnvelope(*envelope, cb::EncodeMarshaled(*state), unixSeconds);
}

}  // namespace panel
