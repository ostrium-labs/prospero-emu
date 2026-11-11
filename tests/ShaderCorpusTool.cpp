// SPDX-License-Identifier: GPL-2.0
//
// Replays a shader corpus captured with --graphics-debug-dump.
//
// Usage:
//   shader_corpus_tool <manifest.json|corpus-dir> [--out <dir>] [--frontend-only]
//
// For each manifest it re-decodes and re-translates the guest shader, compares
// the decoded dump with the capture, and - when resource materialization
// succeeds - recompiles and compares the SPIR-V and IR dumps.

#include "common/emulatorConfig.h"
#include "common/logging/log.h"
#include "common/subsystems.h"
#include "common/threads.h"
#include "graphics/shader/recompiler/ShaderRecompiler.h"
#include "graphics/shader/recompiler/ir/passes/ResourceMaterialization.h"
#include "graphics/shader/shader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace Libs::Graphics;
namespace fs = std::filesystem;

struct Manifest {
	std::string            path;
	ShaderType             stage = ShaderType::Unknown;
	uint64_t               hash = 0;
	uint32_t               wave_size = 64;
	uint32_t               user_data_base = 0;
	std::vector<uint32_t>  user_data;
	std::vector<uint32_t>  back_code;
	std::vector<uint32_t>  static_state;
	uint32_t               push_data_start_dword = 0;
	uint64_t               indirect_args_address = 0;
	std::vector<uint32_t>  code;
	std::string            code_file;
	std::string            spirv_file;
	std::string            ir_file;
	std::string            decoded_file;
	bool                   standalone_replay = false;
	ShaderComputeInputInfo compute;
};

bool ReadFile(const fs::path& path, std::vector<uint8_t>& data) {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return false;
	}
	file.seekg(0, std::ios::end);
	const auto size = file.tellg();
	if (size < 0) {
		return false;
	}
	file.seekg(0, std::ios::beg);
	data.resize(static_cast<size_t>(size));
	if (!data.empty()) {
		file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
	}
	return file.good() || file.eof();
}

std::string ReadText(const fs::path& path) {
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return {};
	}
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool WriteFile(const fs::path& path, const void* data, size_t size) {
	std::error_code error;
	if (!path.parent_path().empty()) {
		fs::create_directories(path.parent_path(), error);
	}
	std::ofstream file(path, std::ios::binary);
	if (!file) {
		return false;
	}
	file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
	return file.good();
}

bool ParseU32(std::string_view text, uint32_t& value) {
	const auto* begin = text.data();
	const auto* end   = text.data() + text.size();
	if (text.starts_with("0x") || text.starts_with("0X")) {
		begin += 2;
	}
	const auto result = std::from_chars(begin, end, value, 16);
	return result.ec == std::errc {} && result.ptr == end;
}

bool ParseU64(std::string_view text, uint64_t& value) {
	const auto* begin = text.data();
	const auto* end   = text.data() + text.size();
	if (text.starts_with("0x") || text.starts_with("0X")) {
		begin += 2;
	}
	const auto result = std::from_chars(begin, end, value, 16);
	return result.ec == std::errc {} && result.ptr == end;
}

bool ParseWords(const nlohmann::json& json, std::vector<uint32_t>& words) {
	if (!json.is_array()) {
		return false;
	}
	words.clear();
	words.reserve(json.size());
	for (const auto& item: json) {
		if (!item.is_string()) {
			return false;
		}
		uint32_t word = 0;
		if (!ParseU32(item.get<std::string>(), word)) {
			return false;
		}
		words.push_back(word);
	}
	return true;
}

bool LoadManifest(const fs::path& path, Manifest& manifest) {
	std::ifstream file(path);
	if (!file) {
		std::fprintf(stderr, "%s: cannot open\n", path.c_str());
		return false;
	}

	const auto text = ReadText(path);
	const auto json = nlohmann::json::parse(text, nullptr, false);
	if (json.is_discarded()) {
		std::fprintf(stderr, "%s: invalid JSON\n", path.c_str());
		return false;
	}

	if (!json.is_object() || json.value("format", 0) != 1) {
		std::fprintf(stderr, "%s: unsupported manifest format\n", path.c_str());
		return false;
	}

	manifest.path = path.string();
	manifest.code_file = json.value("code_file", std::string {});
	manifest.spirv_file = json.value("spirv_file", std::string {});
	manifest.ir_file = json.value("ir_file", std::string {});
	manifest.decoded_file = json.value("decoded_file", std::string {});
	manifest.standalone_replay = json.value("standalone_replay", false);
	manifest.wave_size = json.value("wave_size", 64u);
	manifest.user_data_base = json.value("user_data_base", 0u);
	manifest.push_data_start_dword = json.value("push_data_start_dword", 0u);

	const auto stage = json.value("stage", std::string {});
	if (stage == "Compute") {
		manifest.stage = ShaderType::Compute;
	} else {
		std::fprintf(stderr, "%s: stage %s is not replayable yet\n", path.c_str(), stage.c_str());
		return false;
	}

	if (!ParseU64(json.value("shader_hash", std::string {}), manifest.hash) ||
	    !ParseWords(json.value("user_data", nlohmann::json::array()), manifest.user_data) ||
	    !ParseWords(json.value("back_code", nlohmann::json::array()), manifest.back_code) ||
	    !ParseWords(json.value("static_state", nlohmann::json::array()), manifest.static_state)) {
		std::fprintf(stderr, "%s: invalid hash or word array\n", path.c_str());
		return false;
	}

	if (!manifest.standalone_replay) {
		std::fprintf(stderr, "%s: not standalone-replayable\n", path.c_str());
		return false;
	}

	if (const auto it = json.find("compute_input"); it != json.end() && it->is_object()) {
		const auto& input = *it;
		const auto read3 = [&input](const char* key, uint32_t (&out)[3]) {
			if (!input.contains(key) || !input[key].is_array() || input[key].size() != 3) {
				return;
			}
			for (size_t i = 0; i < 3; i++) {
				out[i] = input[key][i].get<uint32_t>();
			}
		};
		read3("threads_num", manifest.compute.threads_num);
		read3("dispatch_threads_num", manifest.compute.dispatch_threads_num);
		read3("workgroup_counts", manifest.compute.workgroup_counts);
		manifest.compute.lds_size_dwords = input.value("lds_size_dwords", 0u);
		manifest.compute.scratch_size_dwords = input.value("scratch_size_dwords", 0u);
		manifest.compute.host_subgroup_size = input.value("host_subgroup_size", 64u);
		manifest.compute.wave_size = input.value("wave_size", manifest.wave_size);
		manifest.compute.async_compute = input.value("async_compute", false);
		manifest.compute.float_mode = input.value("float_mode", 0xc0);
		manifest.compute.dispatch_thread_dimensions =
		    input.value("dispatch_thread_dimensions", false);
		manifest.compute.lds_storage = input.value("lds_storage", false);
		manifest.compute.thread_ids_num = input.value("thread_ids_num", 0);
		manifest.compute.workgroup_register = input.value("workgroup_register", 0);
		manifest.compute.tg_size_en = input.value("tg_size_en", false);
		if (const auto address = input.find("indirect_args_address");
		    address != input.end() && address->is_string()) {
			ParseU64(address->get<std::string>(), manifest.indirect_args_address);
		}
		manifest.compute.indirect_args_address = manifest.indirect_args_address;
	}

	const auto root = path.parent_path();
	std::vector<uint8_t> bytes;
	if (!ReadFile(root / manifest.code_file, bytes)) {
		std::fprintf(stderr, "%s: cannot read %s\n", path.c_str(), manifest.code_file.c_str());
		return false;
	}
	if (bytes.size() % sizeof(uint32_t) != 0) {
		std::fprintf(stderr, "%s: code file size is not a multiple of 4\n", path.c_str());
		return false;
	}
	manifest.code.resize(bytes.size() / sizeof(uint32_t));
	std::memcpy(manifest.code.data(), bytes.data(), bytes.size());
	return true;
}

bool NoSpecializationMemory(void*, uint64_t, std::span<uint32_t>) {
	return false;
}

struct ReplayResult {
	bool decoded_matches = false;
	bool compiled = false;
	bool spirv_matches = false;
	bool ir_matches = false;
	std::vector<uint32_t> spirv;
	std::string decoded;
	std::string ir;
};

ReplayResult Replay(const Manifest& manifest) {
	ReplayResult result;

	ShaderRecompiler::CompileOptions options;
	options.stage = manifest.stage;
	options.wave_size = manifest.wave_size;
	options.user_data_base = manifest.user_data_base;
	options.shader_hash = manifest.hash;
	options.user_data = manifest.user_data;
	options.back_code = manifest.back_code;
	options.dump_ir = true;
	options.input_info.compute = &manifest.compute;

	auto translated = ShaderRecompiler::TranslateProgram(manifest.code, options);
	result.decoded = std::move(translated.decoded_dump);

	const auto root = fs::path(manifest.path).parent_path();
	const auto captured_decoded = ReadText(root / manifest.decoded_file);
	result.decoded_matches = !captured_decoded.empty() && captured_decoded == result.decoded;

	auto plan = ShaderRecompiler::IR::ExtractResourcePlan(translated.program);
	ShaderRecompiler::IR::ResourceSnapshot resources;
	ShaderRecompiler::IR::ResourceSpecialization specialization;
	const ShaderRecompiler::IR::SrtRuntime runtime {
	    .user_data = options.user_data,
	    .shader_base = reinterpret_cast<uint64_t>(manifest.code.data()),
	    .read_specialization_memory = NoSpecializationMemory,
	    .workgroup_counts = manifest.compute.workgroup_counts,
	    .indirect_args_address = manifest.indirect_args_address,
	};
	if (!ShaderRecompiler::IR::MaterializeResources(plan, runtime, resources, specialization)) {
		return result;
	}

	auto compiled = ShaderRecompiler::CompileProgram(std::move(translated), options, specialization,
	                                                 manifest.push_data_start_dword);
	result.compiled = true;
	result.spirv = std::move(compiled.spirv);
	result.ir = std::move(compiled.ir_dump);

	std::vector<uint8_t> captured_spirv;
	if (ReadFile(root / manifest.spirv_file, captured_spirv)) {
		result.spirv_matches =
		    captured_spirv.size() == result.spirv.size() * sizeof(uint32_t) &&
		    std::memcmp(captured_spirv.data(), result.spirv.data(), captured_spirv.size()) == 0;
	}
	const auto captured_ir = ReadText(root / manifest.ir_file);
	result.ir_matches = !captured_ir.empty() && captured_ir == result.ir;
	return result;
}

int Process(const fs::path& path, const fs::path& out_dir, bool frontend_only, bool capture) {
	Manifest manifest;
	if (!LoadManifest(path, manifest)) {
		return 1;
	}

	auto result = Replay(manifest);
	const auto stem = path.stem().string();
	const auto root = path.parent_path();

	if (capture) {
		const auto decoded_path =
		    manifest.decoded_file.empty() ? root / (stem + ".decoded.txt")
		                                  : root / manifest.decoded_file;
		const auto spirv_path =
		    manifest.spirv_file.empty() ? root / (stem + ".spv") : root / manifest.spirv_file;
		const auto ir_path = manifest.ir_file.empty() ? root / (stem + ".ir.txt")
		                                              : root / manifest.ir_file;
		if (!result.decoded.empty()) {
			WriteFile(decoded_path, result.decoded.data(), result.decoded.size());
		}
		if (!result.spirv.empty()) {
			WriteFile(spirv_path, result.spirv.data(), result.spirv.size() * sizeof(uint32_t));
		}
		if (!result.ir.empty()) {
			WriteFile(ir_path, result.ir.data(), result.ir.size());
		}
		std::printf("%s: captured (%s)\n", path.c_str(), result.compiled ? "full" : "frontend");
		return result.decoded.empty() ? 1 : 0;
	}

	if (!out_dir.empty()) {
		if (!result.spirv.empty()) {
			WriteFile(out_dir / (stem + ".spv"), result.spirv.data(),
			          result.spirv.size() * sizeof(uint32_t));
		}
		if (!result.decoded.empty()) {
			WriteFile(out_dir / (stem + ".decoded.txt"), result.decoded.data(), result.decoded.size());
		}
		if (!result.ir.empty()) {
			WriteFile(out_dir / (stem + ".ir.txt"), result.ir.data(), result.ir.size());
		}
	}

	int failures = 0;
	if (!result.decoded_matches) {
		std::fprintf(stderr, "%s: decoded dump differs\n", path.c_str());
		failures++;
	}
	if (!frontend_only) {
		if (!result.compiled) {
			std::fprintf(stderr, "%s: resource materialization failed; frontend-only\n",
			             path.c_str());
			failures++;
		} else {
			if (!result.spirv_matches) {
				std::fprintf(stderr, "%s: SPIR-V differs\n", path.c_str());
				failures++;
			}
			if (!result.ir_matches) {
				std::fprintf(stderr, "%s: IR dump differs\n", path.c_str());
				failures++;
			}
		}
	}

	if (failures == 0) {
		std::printf("%s: ok (%s)\n", path.c_str(), result.compiled ? "full" : "frontend");
	}
	return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
	using namespace Libs::Graphics;

	if (argc < 2) {
		std::fprintf(stderr,
		             "usage: %s <manifest.json|corpus-dir> [--out <dir>] [--frontend-only] "
		             "[--capture]\n",
		             argv[0]);
		return 2;
	}

	fs::path input = argv[1];
	fs::path out_dir;
	bool     frontend_only = false;
	bool     capture = false;
	for (int i = 2; i < argc; i++) {
		if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
			out_dir = argv[++i];
		} else if (std::strcmp(argv[i], "--frontend-only") == 0) {
			frontend_only = true;
		} else if (std::strcmp(argv[i], "--capture") == 0) {
			capture = true;
		} else {
			std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
			return 2;
		}
	}

	static Common::Subsystems subsystems;
	Common::InitializeThreads();
	subsystems.Initialize<Config::Lifecycle>();
	Config::ConfigOptions options;
	options.printf_direction = Config::LogDirection::Silent;
	Config::Load(options);
	subsystems.Initialize<Log::Lifecycle>();
	ShaderInit();

	std::vector<fs::path> manifests;
	if (fs::is_directory(input)) {
		for (const auto& entry: fs::directory_iterator(input)) {
			if (entry.is_regular_file() && entry.path().extension() == ".json") {
				manifests.push_back(entry.path());
			}
		}
		std::sort(manifests.begin(), manifests.end());
	} else {
		manifests.push_back(input);
	}

	if (manifests.empty()) {
		std::fprintf(stderr, "no manifests found in %s\n", input.c_str());
		return 2;
	}

	int failures = 0;
	for (const auto& manifest: manifests) {
		failures += Process(manifest, out_dir, frontend_only, capture);
	}

	std::printf("replayed=%zu failures=%d\n", manifests.size(), failures);
	return failures == 0 ? 0 : 1;
}
