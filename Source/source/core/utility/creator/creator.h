#pragma once
#include <cstdint>
#include <string>

namespace creator {
	auto init() -> bool;
	auto shutdown() -> void;
	auto notify_datamodel_lost() -> void;
	auto install_console_handler() -> void;
	auto is_available() -> bool;

	auto create_instance(const char* class_name, std::uint64_t parent, std::uint32_t timeout_ms = 500) -> std::uint64_t;
	auto set_parent(std::uint64_t instance, std::uint64_t parent, std::uint32_t timeout_ms = 500) -> bool;
	auto destroy(std::uint64_t instance, std::uint32_t timeout_ms = 500) -> bool;
	auto call(std::uint64_t fn, std::uint64_t arg1, std::uint64_t arg2, std::uint64_t arg3, std::uint32_t timeout_ms = 500) -> bool;

	auto last_error() -> const char*;

	auto debug_scan_job_slots(std::uint32_t duration_ms = 1500) -> std::string;
}