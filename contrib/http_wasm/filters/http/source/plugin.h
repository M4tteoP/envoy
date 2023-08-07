#pragma once

#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/wasm/v3/wasm.pb.validate.h"
#include "envoy/local_info/local_info.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HttpWasm {

//clang-format off
using EnvironmentVariableMap = std::unordered_map<std::string, std::string>;
struct SanitizationConfig {
  std::vector<std::string> argument_list;
  bool is_allowlist;
};
using AllowedCapabilitiesMap = std::unordered_map<std::string, SanitizationConfig>;
// clang-format on

class Context;
class WasmConfig {
public:
  WasmConfig(const envoy::extensions::wasm::v3::PluginConfig& config);
  const envoy::extensions::wasm::v3::PluginConfig& config() { return config_; }
  AllowedCapabilitiesMap& allowedCapabilities() { return allowed_capabilities_; }
  EnvironmentVariableMap& environmentVariables() { return envs_; }

private:
  const envoy::extensions::wasm::v3::PluginConfig config_;
  AllowedCapabilitiesMap allowed_capabilities_{};
  EnvironmentVariableMap envs_;
};

using WasmConfigPtr = std::unique_ptr<WasmConfig>;

// Plugin contains the information for a filter/service.
class Plugin {
public:
  Plugin(const envoy::extensions::wasm::v3::PluginConfig& config,
         envoy::config::core::v3::TrafficDirection direction,
         const LocalInfo::LocalInfo& local_info,
         const envoy::config::core::v3::Metadata* listener_metadata)
      : name_(std::string(config.name())), root_id_(std::string(config.root_id())),
        vm_id_(std::string(config.vm_config().vm_id())),
        engine_(std::string(config.vm_config().runtime())),
        plugin_configuration_(MessageUtil::anyToBytes(config.configuration())),
        fail_open_(config.fail_open()), direction_(direction), local_info_(local_info),
        listener_metadata_(listener_metadata), wasm_config_(std::make_unique<WasmConfig>(config)),
        key_(root_id_ + "||" + plugin_configuration_ + "||" +
             std::string(createPluginKey(config, direction, listener_metadata))),
        log_prefix_(makeLogPrefix()) {}

  envoy::config::core::v3::TrafficDirection& direction() { return direction_; }
  const LocalInfo::LocalInfo& localInfo() { return local_info_; }
  const envoy::config::core::v3::Metadata* listenerMetadata() { return listener_metadata_; }
  WasmConfig& wasmConfig() { return *wasm_config_; }
  const std::string name_;
  // TODO: keep only id_
  const std::string root_id_;
  const std::string vm_id_;
  const std::string engine_;
  const std::string plugin_configuration_;
  const bool fail_open_;

  const std::string& key() const { return key_; }
  const std::string& log_prefix() const { return log_prefix_; }

private:
  static std::string createPluginKey(const envoy::extensions::wasm::v3::PluginConfig& config,
                                     envoy::config::core::v3::TrafficDirection direction,
                                     const envoy::config::core::v3::Metadata* listener_metadata) {
    return config.name() + "||" + envoy::config::core::v3::TrafficDirection_Name(direction) +
           (listener_metadata ? "||" + std::to_string(MessageUtil::hash(*listener_metadata)) : "");
  }

private:
  envoy::config::core::v3::TrafficDirection direction_;
  const LocalInfo::LocalInfo& local_info_;
  const envoy::config::core::v3::Metadata* listener_metadata_;
  WasmConfigPtr wasm_config_;

private:
  std::string makeLogPrefix() const;

  const std::string key_;
  const std::string log_prefix_;
};

using PluginSharedPtr = std::shared_ptr<Plugin>;
} // namespace HttpWasm
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
