
#include "source/extensions/common/http_wasm/bytecode_util.h"

#if !defined(_MSC_VER)
#include <cxxabi.h>
#endif

#include <cstring>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HttpWasm {

bool BytecodeUtil::checkWasmHeader(std::string_view bytecode) {
  // Wasm file header is 8 bytes (magic number + version).
  static const uint8_t wasm_magic_number[4] = {0x00, 0x61, 0x73, 0x6d};
  return (bytecode.size() < 8 || ::memcmp(bytecode.data(), wasm_magic_number, 4) == 0);
}

bool BytecodeUtil::getCustomSection(std::string_view bytecode, std::string_view name,
                                    std::string_view& ret) {
  // Check Wasm header.
  if (!checkWasmHeader(bytecode)) {
    return false;
  }

  // Skip the Wasm header.
  const char* pos = bytecode.data() + 8;
  const char* end = bytecode.data() + bytecode.size();
  while (pos < end) {
    if (pos + 1 > end) {
      return false;
    }
    const auto section_type = *pos++;
    uint32_t section_len = 0;
    if (!parseVarint(pos, end, section_len) || pos + section_len > end) {
      return false;
    }
    if (section_type == 0) {
      // Custom section.
      const char* section_end = pos + section_len;
      uint32_t section_name_len = 0;
      if (!BytecodeUtil::parseVarint(pos, section_end, section_name_len) ||
          pos + section_name_len > section_end) {
        return false;
      }
      if (section_name_len == name.size() && ::memcmp(pos, name.data(), section_name_len) == 0) {
        pos += section_name_len;
        ret = {pos, static_cast<size_t>(section_end - pos)};
        return true;
      }
      pos = section_end;
    } else {
      // Skip other sections.
      pos += section_len;
    }
  }
  return true;
}

bool BytecodeUtil::getFunctionNameIndex(std::string_view bytecode,
                                        std::unordered_map<uint32_t, std::string>& ret) {
  std::string_view name_section = {};
  if (!BytecodeUtil::getCustomSection(bytecode, "name", name_section)) {
    return false;
  };
  if (!name_section.empty()) {
    const char* pos = name_section.data();
    const char* end = name_section.data() + name_section.size();
    while (pos < end) {
      const auto subsection_id = *pos++;
      uint32_t subsection_size = 0;
      if (!parseVarint(pos, end, subsection_size) || pos + subsection_size > end) {
        return false;
      }

      if (subsection_id != 1) {
        // Skip other subsctions.
        pos += subsection_size;
      } else {
        // Enters function name subsection.
        const auto* const start = pos;
        uint32_t namemap_vector_size = 0;
        if (!parseVarint(pos, end, namemap_vector_size) || pos + namemap_vector_size > end) {
          return false;
        }
        for (uint32_t i = 0; i < namemap_vector_size; i++) {
          uint32_t func_index = 0;
          if (!parseVarint(pos, end, func_index)) {
            return false;
          }

          uint32_t func_name_size = 0;
          if (!parseVarint(pos, end, func_name_size) || pos + func_name_size > end) {
            return false;
          }
          auto func_name = std::string(pos, func_name_size);
#if !defined(_MSC_VER)
          int status;
          char* data = abi::__cxa_demangle(func_name.c_str(), nullptr, nullptr, &status);
          if (data != nullptr) {
            func_name = std::string(data);
            ::free(data);
          }
#endif
          ret.insert({func_index, func_name});
          pos += func_name_size;
        }
        if (start + subsection_size != pos) {
          return false;
        }
      }
    }
  }
  return true;
}

bool BytecodeUtil::getStrippedSource(std::string_view bytecode, std::string& ret) {
  // Check Wasm header.
  if (!checkWasmHeader(bytecode)) {
    return false;
  }

  // Skip the Wasm header.
  const char* pos = bytecode.data() + 8;
  const char* end = bytecode.data() + bytecode.size();
  while (pos < end) {
    const auto* const section_start = pos;
    if (pos + 1 > end) {
      return false;
    }
    const auto section_type = *pos++;
    uint32_t section_len = 0;
    if (!parseVarint(pos, end, section_len) || pos + section_len > end) {
      return false;
    }
    if (section_type == 0 /* custom section */) {
      const auto* const section_data_start = pos;
      uint32_t section_name_len = 0;
      if (!parseVarint(pos, end, section_name_len) || pos + section_name_len > end) {
        return false;
      }
      auto section_name = std::string_view(pos, section_name_len);
      if (section_name.find("precompiled_") != std::string::npos) {
        // If this is the first "precompiled_" section, then save everything
        // before it, otherwise skip it.
        if (ret.empty()) {
          const char* start = bytecode.data();
          ret.append(start, section_start);
        }
      }
      pos = section_data_start + section_len;
    } else {
      pos += section_len;
      // Save this section if we already saw a custom "precompiled_" section.
      if (!ret.empty()) {
        ret.append(section_start, pos);
      }
    }
  }
  if (ret.empty()) {
    // Copy the original source code if it is empty.
    ret = std::string(bytecode);
  }
  return true;
}

bool BytecodeUtil::parseVarint(const char*& pos, const char* end, uint32_t& ret) {
  uint32_t shift = 0;
  uint32_t total = 0;
  uint32_t v;
  char b;
  while (pos < end) {
    if (pos + 1 > end) {
      // overread
      return false;
    }
    b = *pos++;
    v = (b & 0x7f);
    if (shift == 28 && v > 3) {
      // overflow
      return false;
    }
    total += v << shift;
    if ((b & 0x80) == 0) {
      ret = total;
      return true;
    }
    shift += 7;
    if (shift > 28) {
      // overflow
      return false;
    }
  }
  return false;
}

} // namespace HttpWasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
