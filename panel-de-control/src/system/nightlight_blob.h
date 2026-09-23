#pragma once

// Night Light's registry values, read and written without the registry (SEGURIDAD.md 2.5).
// Windows keeps Night Light as Bond CompactBinary v1 inside a CloudStore envelope; there is no
// public API. The format is documented by kvnxiao/win-nightlight-cli
// (docs/nightlight-registry-format.md, MIT), and every function here is pure and tested against
// the blobs of the machine it was written on.
//
// The codec keeps every field it does not understand, byte for byte, and writing is refused
// unless decoding and encoding again gives back exactly what was read: a blob Windows wrote in a
// way this does not know is left alone.

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace panel::cb {

// Bond type codes (bond/core/bond_const_enum.h).
enum Type : uint8_t {
  kStop = 0,
  kStopBase = 1,
  kBool = 2,
  kUInt8 = 3,
  kUInt16 = 4,
  kUInt32 = 5,
  kUInt64 = 6,
  kFloat = 7,
  kDouble = 8,
  kString = 9,
  kStruct = 10,
  kList = 11,
  kSet = 12,
  kInt8 = 14,
  kInt16 = 15,
  kInt32 = 16,
  kInt64 = 17,
  kWString = 18,
};

// One field of a struct: its id, its type and its value's bytes exactly as they were (for a
// struct, everything up to and including its STOP). A base-class STOP inside a struct is kept
// as a field of type kStopBase with no value, so it comes back where it was.
struct Field {
  uint16_t id = 0;
  uint8_t type = kStop;
  std::vector<uint8_t> value;
};

using Struct = std::vector<Field>;

// A struct's fields from `bytes`, which start with its first field header and end with its
// STOP. Nullopt when anything is not well formed or there are bytes after the STOP.
std::optional<Struct> ParseStruct(std::span<const uint8_t> bytes);
std::vector<uint8_t> EncodeStruct(const Struct& fields);

// A marshaled blob: the 4-byte "CB" v1 header, then a struct.
std::optional<Struct> ParseMarshaled(std::span<const uint8_t> bytes);
std::vector<uint8_t> EncodeMarshaled(const Struct& fields);

const Field* Find(const Struct& fields, uint16_t id);

}  // namespace panel::cb

namespace panel {

// What the panel reads from the two values.
struct NightLightData {
  bool on = false;          // state: field 0 present
  bool scheduled = false;   // settings: field 0 true
  bool sunsetToSunrise = false;
  int fromMinute = 0;       // the schedule's start, or sunset in sunset-to-sunrise mode
  int toMinute = 0;
};

// The payload inside a CloudStore envelope, or nullopt when the envelope is not the one known.
std::optional<std::vector<uint8_t>> EnvelopePayload(std::span<const uint8_t> blob);

// Reads the state value's blob: whether Night Light is on. Nullopt when it is not understood.
std::optional<bool> ReadNightLightState(std::span<const uint8_t> blob);

// Reads the settings value's blob into `data`'s schedule fields. False when not understood.
bool ReadNightLightSettings(std::span<const uint8_t> blob, NightLightData& data);

// True when decoding the blob and encoding it again gives exactly the same bytes: the condition
// for ever writing a blob built from it.
bool RoundTrips(std::span<const uint8_t> blob);

// The state blob with Night Light on or off and the two timestamps moved to now (`unixSeconds`
// for the envelope, `filetime` for the last transition), everything else kept. Nullopt when the
// blob does not round-trip.
std::optional<std::vector<uint8_t>> WithNightLight(std::span<const uint8_t> stateBlob, bool on,
                                                   uint64_t unixSeconds, uint64_t filetime);

}  // namespace panel
