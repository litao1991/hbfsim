#include "internal.h"

#include <cmath>
#include <fstream>
#include <limits>

namespace hbfsim {
namespace {

MappingPolicy parse_mapping(const std::string& value) {
  const auto normalized = detail::lower(value);
  if (normalized == "linear") return MappingPolicy::Linear;
  if (normalized == "fine_stripe" || normalized == "fine-stripe") return MappingPolicy::FineStripe;
  if (normalized == "burst_stripe" || normalized == "burst-stripe") return MappingPolicy::BurstStripe;
  if (normalized == "host_managed" || normalized == "host-managed") return MappingPolicy::HostManaged;
  throw std::runtime_error("unknown mapping policy: " + value);
}

InitializationMode parse_initialization(const std::string& value) {
  const auto normalized = detail::lower(value);
  if (normalized == "empty") return InitializationMode::Empty;
  if (normalized == "image_loaded" || normalized == "image-loaded")
    return InitializationMode::ImageLoaded;
  if (normalized == "preconditioned")
    return InitializationMode::Preconditioned;
  throw std::runtime_error("unknown initialization mode: " + value);
}

bool parse_bool(const std::string& value) {
  const auto normalized = detail::lower(detail::trim(value));
  if (normalized == "true" || normalized == "1" || normalized == "yes")
    return true;
  if (normalized == "false" || normalized == "0" || normalized == "no")
    return false;
  throw std::runtime_error("invalid boolean: " + value);
}

double parse_probability(const std::string& value) {
  std::size_t used = 0;
  const double result = std::stod(detail::trim(value), &used);
  if (used != detail::trim(value).size() || !std::isfinite(result))
    throw std::runtime_error("invalid probability: " + value);
  return result;
}

double parse_ratio(const std::string& value) {
  const auto normalized = detail::trim(value);
  if (!normalized.empty() && normalized.back() == '%')
    return parse_probability(normalized.substr(0, normalized.size() - 1)) /
           100.0;
  return parse_probability(normalized);
}

HostGcVictimPolicy parse_host_gc_policy(const std::string& value) {
  const auto normalized = detail::lower(detail::trim(value));
  if (normalized == "invalid_ratio" || normalized == "invalid-ratio")
    return HostGcVictimPolicy::InvalidRatio;
  if (normalized == "greedy") return HostGcVictimPolicy::Greedy;
  throw std::runtime_error("unknown Host GC victim policy: " + value);
}

SimTime parse_duration_ns(const std::string& value) {
  const auto normalized = detail::lower(detail::trim(value));
  std::size_t used = 0;
  const long double number = std::stold(normalized, &used);
  const auto unit = detail::trim(normalized.substr(used));
  long double multiplier = 1.0L;
  if (unit.empty() || unit == "ns") multiplier = 1.0L;
  else if (unit == "us") multiplier = 1'000.0L;
  else if (unit == "ms") multiplier = 1'000'000.0L;
  else if (unit == "s") multiplier = 1'000'000'000.0L;
  else throw std::runtime_error("unknown duration unit: " + value);
  const auto result = number * multiplier;
  const auto exclusive_limit = std::ldexp(
      1.0L, std::numeric_limits<SimTime>::digits);
  if (!std::isfinite(result) || result < 0.0L ||
      result >= exclusive_limit)
    throw std::runtime_error("invalid duration: " + value);
  return static_cast<SimTime>(std::llround(result));
}

}  // namespace

std::uint64_t parse_size(const std::string& raw_value) {
  const auto value = detail::lower(detail::trim(raw_value));
  std::size_t used = 0;
  const double number = std::stod(value, &used);
  const auto unit = detail::trim(value.substr(used));
  std::uint64_t multiplier = 1;
  if (unit.empty() || unit == "b") multiplier = 1;
  else if (unit == "kib" || unit == "kb") multiplier = 1024ULL;
  else if (unit == "mib" || unit == "mb") multiplier = 1024ULL * 1024ULL;
  else if (unit == "gib" || unit == "gb") multiplier = 1024ULL * 1024ULL * 1024ULL;
  else if (unit == "tib" || unit == "tb") multiplier = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
  else throw std::runtime_error("unknown size unit: " + raw_value);
  return static_cast<std::uint64_t>(std::llround(number * static_cast<double>(multiplier)));
}

double parse_bandwidth_bytes_per_ns(const std::string& raw_value) {
  const auto value = detail::lower(detail::trim(raw_value));
  std::size_t used = 0;
  const double number = std::stod(value, &used);
  const auto unit = detail::trim(value.substr(used));
  double bytes_per_second = 0.0;
  if (unit == "bps" || unit == "b/s") bytes_per_second = number;
  else if (unit == "kbps" || unit == "kb/s") bytes_per_second = number * 1e3;
  else if (unit == "mbps" || unit == "mb/s") bytes_per_second = number * 1e6;
  else if (unit == "gbps" || unit == "gb/s") bytes_per_second = number * 1e9;
  else if (unit == "tbps" || unit == "tb/s") bytes_per_second = number * 1e12;
  else throw std::runtime_error("unknown bandwidth unit: " + raw_value);
  return bytes_per_second / 1e9;
}

std::string to_string(OpType op) {
  switch (op) {
    case OpType::Read: return "READ";
    case OpType::Write: return "WRITE";
    case OpType::Erase: return "ERASE";
    case OpType::Refresh: return "REFRESH";
    case OpType::Invalidate: return "INVALIDATE";
  }
  return "UNKNOWN";
}

std::string to_string(TransactionSource source) {
  switch (source) {
    case TransactionSource::User: return "USER";
    case TransactionSource::Mapping: return "MAPPING";
    case TransactionSource::Maintenance: return "MAINTENANCE";
    case TransactionSource::Refresh: return "REFRESH";
    case TransactionSource::GarbageCollection: return "GC";
    case TransactionSource::Recovery: return "RECOVERY";
  }
  return "UNKNOWN";
}

OpType parse_op(const std::string& value) {
  const auto normalized = detail::lower(detail::trim(value));
  if (normalized == "r" || normalized == "read") return OpType::Read;
  if (normalized == "w" || normalized == "write") return OpType::Write;
  if (normalized == "e" || normalized == "erase") return OpType::Erase;
  if (normalized == "refresh") return OpType::Refresh;
  if (normalized == "i" || normalized == "invalidate" ||
      normalized == "trim" || normalized == "discard")
    return OpType::Invalidate;
  throw std::runtime_error("unknown operation: " + value);
}

Config Config::from_yaml_file(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open config: " + path);
  std::map<std::string, std::string> values;
  std::vector<std::pair<int, std::string>> parents;
  std::string line;
  while (std::getline(input, line)) {
    if (const auto comment = line.find('#'); comment != std::string::npos) line.erase(comment);
    if (detail::trim(line).empty() || detail::trim(line).starts_with("-")) continue;
    const int indent = static_cast<int>(line.find_first_not_of(" \t"));
    const auto content = detail::trim(line);
    const auto colon = content.find(':');
    if (colon == std::string::npos) continue;
    const auto key = detail::trim(content.substr(0, colon));
    const auto value = detail::unquote(content.substr(colon + 1));
    while (!parents.empty() && parents.back().first >= indent) parents.pop_back();
    std::string full_key;
    for (const auto& [_, parent] : parents) full_key += parent + ".";
    full_key += key;
    if (value.empty()) parents.emplace_back(indent, key);
    else values[full_key] = value;
  }

  Config config;
  const auto integer = [](const std::string& v) { return detail::parse_u64(v); };
  const auto time = [](const std::string& v) { return parse_duration_ns(v); };
  detail::assign_if(values, "device.stacks", config.stacks, integer);
  detail::assign_if(values, "nand.dies_per_stack", config.dies_per_stack, integer);
  detail::assign_if(values, "nand.planes_per_die", config.planes_per_die, integer);
  detail::assign_if(values, "nand.blocks_per_plane", config.blocks_per_plane, integer);
  detail::assign_if(values, "nand.pages_per_block", config.pages_per_block, integer);
  detail::assign_if(values, "nand.page_size", config.page_size, parse_size);
  detail::assign_if(values, "nand.timing.read_ns", config.read_ns, time);
  detail::assign_if(values, "nand.timing.program_ns", config.program_ns, time);
  detail::assign_if(values, "nand.timing.erase_ns", config.erase_ns, time);
  detail::assign_if(values, "nand.timing.t_ccs_ns", config.t_ccs_ns, time);
  detail::assign_if(values, "nand.timing.t_adl_ns", config.t_adl_ns, time);
  detail::assign_if(values, "nand.timing.t_whr_ns", config.t_whr_ns, time);
  detail::assign_if(values, "nand.timing.suspend_ns", config.suspend_ns, time);
  detail::assign_if(values, "nand.timing.resume_ns", config.resume_ns, time);
  detail::assign_if(values, "nand.timing.multi_plane_setup_ns", config.multi_plane_setup_ns, time);
  detail::assign_if(values, "nand.timing.cache_program_setup_ns", config.cache_program_setup_ns, time);
  detail::assign_if(values, "nand.timing.read_retry_ns", config.read_retry_ns, time);
  detail::assign_if(values, "nand.parallelism.max_active_planes_per_die", config.max_active_planes_per_die, integer);
  detail::assign_if(values, "nand.parallelism.max_active_planes_per_stack", config.max_active_planes_per_stack, integer);
  detail::assign_if(values, "internal_fabric.ports_per_stack", config.ports_per_stack, integer);
  detail::assign_if(values, "internal_fabric.fixed_latency_ns", config.internal_fixed_latency_ns, time);
  detail::assign_if(values, "internal_fabric.aggregate_bandwidth", config.internal_bw_bytes_per_ns, parse_bandwidth_bytes_per_ns);
  detail::assign_if(values, "internal_fabric.port_bandwidth", config.internal_port_bw_bytes_per_ns, parse_bandwidth_bytes_per_ns);
  detail::assign_if(values, "host_interface.channels_per_stack", config.host_channels_per_stack, integer);
  detail::assign_if(values, "host_interface.fixed_latency_ns", config.host_fixed_latency_ns, time);
  detail::assign_if(values, "host_interface.bandwidth_per_channel", config.host_bw_bytes_per_ns, parse_bandwidth_bytes_per_ns);
  detail::assign_if(values, "host_interface.full_duplex", config.host_full_duplex, parse_bool);
  if (const auto it = values.find("initialization.mode"); it != values.end())
    config.initialization_mode = parse_initialization(it->second);
  detail::assign_if(values, "mapping.burst_size", config.burst_size, parse_size);
  if (const auto it = values.find("mapping.policy"); it != values.end()) config.mapping_policy = parse_mapping(it->second);
  detail::assign_if(values, "scheduler.write_starvation_us", config.write_starvation_ns,
                    [](const std::string& v) { return detail::parse_u64(v) * 1'000ULL; });
  detail::assign_if(values, "scheduler.source_aging_ns", config.source_aging_ns, time);
  detail::assign_if(values, "scheduler.max_consecutive_reads", config.max_consecutive_reads, integer);
  detail::assign_if(values, "host_management.auto_recovery", config.auto_recovery_enabled, parse_bool);
  detail::assign_if(values, "host_management.max_recovery_attempts", config.max_recovery_attempts, integer);
  detail::assign_if(values, "host_gc.enabled", config.host_gc_enabled, parse_bool);
  detail::assign_if(values, "host_gc.low_watermark", config.host_gc_low_watermark, parse_ratio);
  detail::assign_if(values, "host_gc.high_watermark", config.host_gc_high_watermark, parse_ratio);
  detail::assign_if(values, "host_gc.overprovisioning_ratio", config.host_gc_overprovisioning_ratio, parse_ratio);
  if (const auto it = values.find("host_gc.victim_policy");
      it != values.end())
    config.host_gc_victim_policy = parse_host_gc_policy(it->second);
  detail::assign_if(values, "refresh.enabled", config.automatic_refresh_enabled, parse_bool);
  detail::assign_if(values, "refresh.retention_time_ns", config.retention_time_ns, time);
  detail::assign_if(values, "refresh.guard_time_ns", config.refresh_guard_time_ns, time);
  detail::assign_if(values, "refresh.max_concurrent_jobs", config.max_concurrent_refresh_jobs, integer);
  detail::assign_if(values, "copy_engine.max_inflight_reads", config.copy_max_inflight_reads, integer);
  detail::assign_if(values, "copy_engine.max_inflight_programs", config.copy_max_inflight_programs, integer);
  detail::assign_if(values, "copy_engine.copy_buffer_size", config.copy_buffer_size, parse_size);
  detail::assign_if(values, "copy_engine.prefetch_window_pages", config.copy_prefetch_window_pages, integer);
  detail::assign_if(values, "nand.strict_media_validation", config.strict_media_validation, parse_bool);
  detail::assign_if(values, "nand.features.suspend_resume", config.suspend_resume_enabled, parse_bool);
  detail::assign_if(values, "nand.features.multi_plane", config.multi_plane_enabled, parse_bool);
  detail::assign_if(values, "nand.features.max_multi_plane_width", config.max_multi_plane_width, integer);
  detail::assign_if(values, "nand.features.cache_program", config.cache_program_enabled, parse_bool);
  detail::assign_if(values, "nand.reliability.program_failure_rate", config.program_failure_rate, parse_probability);
  detail::assign_if(values, "nand.reliability.program_failure_budget", config.program_failure_budget, integer);
  detail::assign_if(values, "nand.reliability.raw_bit_error_rate", config.raw_bit_error_rate, parse_probability);
  detail::assign_if(values, "nand.reliability.retry_ber_multiplier", config.retry_ber_multiplier, parse_probability);
  detail::assign_if(values, "nand.reliability.ecc_correctable_bits", config.ecc_correctable_bits, integer);
  detail::assign_if(values, "nand.reliability.max_read_retries", config.max_read_retries, integer);
  detail::assign_if(values, "nand.reliability.random_seed", config.random_seed, integer);
  detail::assign_if(values, "simulation.max_requests", config.max_requests, integer);
  detail::assign_if(values, "simulation.warmup_requests", config.warmup_requests, integer);
  if (const auto it = values.find("statistics.output_dir"); it != values.end()) config.output_dir = it->second;
  config.validate();
  return config;
}

void Config::validate() const {
  if (page_size == 0 || stacks == 0 || dies_per_stack == 0 ||
      planes_per_die == 0 || blocks_per_plane == 0 || pages_per_block == 0 ||
      host_channels_per_stack == 0 || ports_per_stack == 0 ||
      max_active_planes_per_die == 0 || max_active_planes_per_stack == 0 ||
      max_multi_plane_width == 0 ||
      source_aging_ns == 0 || max_recovery_attempts == 0 ||
      host_bw_bytes_per_ns <= 0.0 || internal_bw_bytes_per_ns <= 0.0 ||
      internal_port_bw_bytes_per_ns <= 0.0)
    throw std::runtime_error("invalid zero-valued HBF topology or link");
  if (copy_max_inflight_reads == 0 || copy_max_inflight_programs == 0 ||
      copy_prefetch_window_pages == 0 || copy_buffer_size < page_size)
    throw std::runtime_error(
        "copy_engine limits must be non-zero and its buffer must fit a page");
  const auto max_u32 = std::numeric_limits<std::uint32_t>::max();
  const std::uint64_t planes_per_stack =
      static_cast<std::uint64_t>(dies_per_stack) * planes_per_die;
  const std::uint64_t total_planes =
      static_cast<std::uint64_t>(stacks) * planes_per_stack;
  if (total_planes > max_u32)
    throw std::runtime_error("configured plane count exceeds simulator address range");
  if (max_active_planes_per_die > planes_per_die ||
      max_active_planes_per_stack > planes_per_stack)
    throw std::runtime_error("active-plane limit exceeds configured topology");
  if (multi_plane_enabled && max_multi_plane_width > planes_per_die)
    throw std::runtime_error("multi-plane width exceeds planes per die");
  if (program_failure_rate < 0.0 || program_failure_rate > 1.0 ||
      raw_bit_error_rate < 0.0 || raw_bit_error_rate > 1.0 ||
      retry_ber_multiplier < 0.0 || retry_ber_multiplier > 1.0)
    throw std::runtime_error("reliability probabilities must be in [0,1]");
  if (host_gc_low_watermark < 0.0 || host_gc_low_watermark >= 1.0 ||
      host_gc_high_watermark <= host_gc_low_watermark ||
      host_gc_high_watermark > 1.0 ||
      host_gc_overprovisioning_ratio < 0.0 ||
      host_gc_overprovisioning_ratio >= 1.0)
    throw std::runtime_error(
        "Host GC requires 0 <= low < high <= 1 and 0 <= OP < 1");
  if (host_gc_enabled && mapping_policy != MappingPolicy::HostManaged)
    throw std::runtime_error(
        "automatic Host GC requires mapping.policy: host_managed");
  if (automatic_refresh_enabled &&
      (mapping_policy != MappingPolicy::HostManaged ||
       retention_time_ns == 0 ||
       refresh_guard_time_ns >= retention_time_ns ||
       max_concurrent_refresh_jobs == 0))
    throw std::runtime_error(
        "automatic Refresh requires host_managed mapping, positive retention "
        "and concurrency, and guard < retention");
  const auto max_u64 = std::numeric_limits<std::uint64_t>::max();
  if (planes_per_stack > max_u64 / blocks_per_plane ||
      planes_per_stack * blocks_per_plane > max_u64 / pages_per_block)
    throw std::runtime_error("configured NAND capacity exceeds simulator address range");
  const std::uint64_t pages_per_stack =
      planes_per_stack * blocks_per_plane * pages_per_block;
  if (static_cast<std::uint64_t>(stacks) > max_u64 / pages_per_stack)
    throw std::runtime_error("configured NAND capacity exceeds simulator address range");
  if (mapping_policy == MappingPolicy::BurstStripe) {
    if (burst_size == 0 || burst_size % page_size != 0)
      throw std::runtime_error(
          "mapping.burst_size must be a non-zero multiple of nand.page_size");
    const std::uint64_t pages_per_burst = burst_size / page_size;
    if (pages_per_stack % pages_per_burst != 0)
      throw std::runtime_error(
          "burst_stripe requires each stack capacity to contain a whole number of bursts");
  }
}

}  // namespace hbfsim
