#ifndef TF2_SDK_DATAMAP_HPP
#define TF2_SDK_DATAMAP_HPP

#include <cstddef>
#include <cstdint>

struct inputdata_t;

enum fieldtype_t {
  FIELD_VOID = 0,
  FIELD_FLOAT,
  FIELD_STRING,
  FIELD_VECTOR,
  FIELD_QUATERNION,
  FIELD_INTEGER,
  FIELD_BOOLEAN,
  FIELD_SHORT,
  FIELD_CHARACTER,
  FIELD_COLOR32,
  FIELD_EMBEDDED,
  FIELD_CUSTOM,
  FIELD_CLASSPTR,
  FIELD_EHANDLE,
  FIELD_EDICT,
  FIELD_POSITION_VECTOR,
  FIELD_TIME,
  FIELD_TICK,
  FIELD_MODELNAME,
  FIELD_SOUNDNAME,
  FIELD_INPUT,
  FIELD_FUNCTION,
  FIELD_VMATRIX,
  FIELD_VMATRIX_WORLDSPACE,
  FIELD_MATRIX3X4_WORLDSPACE,
  FIELD_INTERVAL,
  FIELD_MODELINDEX,
  FIELD_MATERIALINDEX,
  FIELD_VECTOR2D,
  FIELD_TYPECOUNT
};

inline constexpr short FTYPEDESC_GLOBAL = 0x0001;
inline constexpr short FTYPEDESC_SAVE = 0x0002;
inline constexpr short FTYPEDESC_KEY = 0x0004;
inline constexpr short FTYPEDESC_INPUT = 0x0008;
inline constexpr short FTYPEDESC_OUTPUT = 0x0010;
inline constexpr short FTYPEDESC_FUNCTIONTABLE = 0x0020;
inline constexpr short FTYPEDESC_PTR = 0x0040;
inline constexpr short FTYPEDESC_OVERRIDE = 0x0080;
inline constexpr short FTYPEDESC_INSENDTABLE = 0x0100;
inline constexpr short FTYPEDESC_PRIVATE = 0x0200;
inline constexpr short FTYPEDESC_NOERRORCHECK = 0x0400;

enum {
  TD_OFFSET_NORMAL = 0,
  TD_OFFSET_PACKED = 1,
  TD_OFFSET_COUNT = 2
};

class ISaveRestoreOps;
struct datamap_t;

struct typedescription_t {
  fieldtype_t fieldType;
  const char* fieldName;
  int fieldOffset[TD_OFFSET_COUNT];
  unsigned short fieldSize;
  short flags;
  const char* externalName;
  ISaveRestoreOps* pSaveRestoreOps;
  // GCC pointer-to-member is 16 bytes; TF2's typedescription_t stores CBaseEntity::* here.
  std::uint8_t inputFunc[16];
  datamap_t* td;
  int fieldSizeInBytes;
  typedescription_t* override_field;
  int override_count;
  float fieldTolerance;
};

struct datamap_t {
  typedescription_t* dataDesc;
  int dataNumFields;
  const char* dataClassName;
  datamap_t* baseMap;
  bool chains_validated;
  bool packed_offsets_computed;
  int packed_size;
};

static_assert(offsetof(datamap_t, dataNumFields) == 8);
static_assert(offsetof(datamap_t, dataClassName) == 16);
static_assert(offsetof(datamap_t, baseMap) == 24);
static_assert(offsetof(datamap_t, chains_validated) == 32);
static_assert(offsetof(datamap_t, packed_size) == 36);

static_assert(sizeof(typedescription_t) == 96, "Linux TF2 typedescription_t is 96 bytes (16-byte PMF)");
static_assert(offsetof(typedescription_t, fieldSizeInBytes) == 72);
static_assert(offsetof(typedescription_t, fieldTolerance) == 92);

#endif
