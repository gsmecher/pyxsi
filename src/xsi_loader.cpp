#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#define FMT_HEADER_ONLY

#include <cxxabi.h>
#include <cstdlib>
#include <set>
#include <fmt/format.h>
#include "xsi_loader.h"

using namespace Xsi;

// Track loaded design .so paths to prevent double-loading (which causes
// corruption of xsim's global state and mysterious segfaults).
static std::set<std::string> _loaded_designs;

// Process-wide dlmopen namespace for Vivado libraries. Created once
// (glibc never reclaims namespace slots, so we must reuse). Simkernel
// stays loaded for the process lifetime; design .so is loaded/unloaded
// per Loader instance.
static Lmid_t _vivado_lmid = LM_ID_NEWLM;  // LM_ID_NEWLM = not yet created
static void *_simkernel_persistent = nullptr;

static std::string canonical_path(const std::string &path) {
	char *rp = realpath(path.c_str(), nullptr);
	if(!rp)
		return path;  // fall back to original if realpath fails
	std::string result(rp);
	free(rp);
	return result;
}

// ---------------------------------------------------------------------------
// Offsets into opaque IKI/xsim data structures.
// These were determined empirically and may change across Vivado versions.
// ---------------------------------------------------------------------------

// XSIHost: offset to UserAccessService*
constexpr size_t iki_XSIHost_uas_offset = 0x470;

// TraceDispatcher is embedded at this offset within the UAS object
constexpr size_t iki_UAS_traceDispatcher_offset = 0x2C0;

static void assert_demangles(const char *mangled, const char *expected_prefix) {
	int status = 0;
	char *demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
	if(status != 0 || !demangled)
		throw std::runtime_error(fmt::format(
			"Failed to demangle '{}' (status={})", mangled, status));
	bool ok = std::string_view(demangled).starts_with(expected_prefix);
	std::string actual(demangled);
	free(demangled);
	if(!ok)
		throw std::runtime_error(fmt::format(
			"Symbol mismatch: '{}' demangled to '{}', expected prefix '{}'",
			mangled, actual, expected_prefix));
}

// ---------------------------------------------------------------------------
// WatchFeature: static vtable and callbacks
// ---------------------------------------------------------------------------
// These are called from deep inside the IKI simulator during run().
// They must not allocate, throw, or touch Python.

// The IKI trace dispatch calls traceVlogNet/traceVhdlNet as virtual methods.
// The old_value_passthrough parameter (r8) is chained through successive
// callbacks via rax — each callback MUST return it.

static void *watch_traceVlogNet(void *self, WatchCookie *cookie,
		unsigned a3, void *new_value, void *old_value_passthrough,
		unsigned start_bit, unsigned bit_count, unsigned mode) {
	cookie->dirty.store(true, std::memory_order_relaxed);
	if(cookie->interruptFlag)
		*cookie->interruptFlag |= 0x04;
	return old_value_passthrough;
}

static void *watch_traceVhdlNet(void *self, WatchCookie *cookie,
		unsigned a3, void *a4, void *old_value_passthrough, unsigned a6) {
	cookie->dirty.store(true, std::memory_order_relaxed);
	if(cookie->interruptFlag)
		*cookie->interruptFlag |= 0x04;
	return old_value_passthrough;
}

static void watch_noop_void(void*) {}
static bool watch_needs_index(void*) { return true; }

// Slots [6] and [7] base implementations return an argument:
//   [6]: mov %r8,%rax; ret  (returns 5th arg)
//   [7]: mov %rcx,%rax; ret (returns 4th arg)
static void* watch_passthru_r8(void*, void*, void*, void*, void *r8) { return r8; }
static void* watch_passthru_rcx(void*, void*, void*, void *rcx) { return rcx; }

// Matches TraceFeature vtable layout:
//   [0] dtor_complete  [1] dtor_deleting  [2] needsIndex
//   [3] traceVlogNet   [4] traceVhdlNet   [5] unknown (noop)
//   [6] traceScObject  [7] unknown        [8] finalize  [9] restart
static void *s_watchVtable[] = {
	/* offset_to_top = 0, typeinfo = NULL  (two entries before vptr target) */
	nullptr, nullptr,
	/* vptr points here: */
	(void*)watch_noop_void,        // [0] dtor_complete
	(void*)watch_noop_void,        // [1] dtor_deleting
	(void*)watch_needs_index,      // [2] needsIndex -> true (enables per-signal cookies)
	(void*)watch_traceVlogNet,     // [3] traceVlogNet (returns old_value_passthrough)
	(void*)watch_traceVhdlNet,     // [4] traceVhdlNet (returns old_value_passthrough)
	(void*)watch_noop_void,        // [5] unknown (noop)
	(void*)watch_passthru_r8,      // [6] traceScObject (base: returns 5th arg)
	(void*)watch_passthru_rcx,     // [7] unknown (base: returns 4th arg)
	(void*)watch_noop_void,        // [8] finalize
	(void*)watch_noop_void,        // [9] restart
};

// ---------------------------------------------------------------------------

Loader::Loader(const std::string& design_libname, const std::string& simkernel_libname,
		const std::string& shim_libname) :
	_design_libname(design_libname),
	_canonical_design_path(canonical_path(design_libname)),
	_design_handle(NULL)
{
	if(_loaded_designs.count(_canonical_design_path))
		throw std::runtime_error(fmt::format(
			"Design library {} is already loaded by another XSI instance. "
			"Destroy the existing instance before creating a new one.",
			design_libname));
	_loaded_designs.insert(_canonical_design_path);

	try {
		// Load simkernel into an isolated linker namespace (persistent,
		// because glibc never reclaims dlmopen namespace slots).
		if(!_simkernel_persistent) {
			_simkernel_persistent = dlmopen(LM_ID_NEWLM,
				simkernel_libname.c_str(), RTLD_LAZY);
			if(!_simkernel_persistent)
				throw std::runtime_error(fmt::format(
					"Unable to load simulator library {}: {}",
					simkernel_libname, dlerror()));
			dlinfo(_simkernel_persistent, RTLD_DI_LMID, &_vivado_lmid);
		}
		void *simkernel = _simkernel_persistent;

		// Load design into the same namespace.
		if(!(design = dlmopen(_vivado_lmid, design_libname.c_str(), RTLD_LAZY)))
			throw std::runtime_error(fmt::format("Unable to load design library {}: {}",
				design_libname, dlerror()));

#define xstr(s) str(s)
#define str(s) #s
#define RESOLVE(library_handle, x) ({						\
		dlerror();							\
		if(!(_##x = (decltype(_##x))dlsym(library_handle, str(x))))	\
			throw std::runtime_error(fmt::format(			\
				"Unable to resolve symbol "			\
				str(x)						\
				": {}", dlerror()));				\
		})

		// XSI public API
		RESOLVE(design, xsi_open);
		RESOLVE(simkernel, xsi_close);
		RESOLVE(simkernel, xsi_run);
		RESOLVE(simkernel, xsi_put_value);
		RESOLVE(simkernel, xsi_get_status);
		RESOLVE(simkernel, xsi_get_error_info);
		RESOLVE(simkernel, xsi_restart);
		RESOLVE(simkernel, xsi_get_port_number);
		RESOLVE(simkernel, xsi_get_int);
		RESOLVE(simkernel, xsi_get_int_port);
		RESOLVE(simkernel, xsi_get_str_port);
		RESOLVE(simkernel, xsi_trace_all);

// Mangled C++ symbols from the simulator kernel
#define RESOLVE_MANGLED(member, mangled, demangled_prefix) ({			\
		dlerror();							\
		if(!(member = (decltype(member))dlsym(simkernel, mangled)))	\
			throw std::runtime_error(fmt::format(			\
				"Unable to resolve {}: {}",			\
				demangled_prefix, dlerror()));			\
		assert_demangles(mangled, demangled_prefix);			\
		})

		RESOLVE_MANGLED(_getObjectInfo,
			"_ZNK5ISIMK10DbgManager13getObjectInfoEj",
			"ISIMK::DbgManager::getObjectInfo(unsigned int)");

		RESOLVE_MANGLED(_setHdlValueObject,
			"_ZNK5ISIMK10DbgManager17setHdlValueObjectERN4ISIM14HdlValueObjectEPKN13ISimHierarchy10ObjectInfoE",
			"ISIMK::DbgManager::setHdlValueObject(ISIM::HdlValueObject&, ISimHierarchy::ObjectInfo const*)");

		RESOLVE_MANGLED(_getScopeInfo,
			"_ZNK5ISIMK10DbgManager12getScopeInfoEj",
			"ISIMK::DbgManager::getScopeInfo(unsigned int)");

		RESOLVE_MANGLED(_getScopeCommonInfo,
			"_ZNK5ISIMK10DbgManager18getScopeCommonInfoEPKN13ISimHierarchy9ScopeInfoE",
			"ISIMK::DbgManager::getScopeCommonInfo(ISimHierarchy::ScopeInfo const*)");

		// Type classification -- optional, graceful fallback to VECTOR.
		_getCommonObjectInfo = (fn_getCommonObjectInfo)dlsym(simkernel,
			"_ZNK5ISIMK10DbgManager19getCommonObjectInfoEPKN13ISimHierarchy10ObjectInfoE");

		RESOLVE_MANGLED(_activateTrace,
			"_ZN5ISIMK15TraceDispatcher13activateTraceERKN4ISIM14HdlValueObjectERNS_12TraceFeatureEbbPv",
			"ISIMK::TraceDispatcher::activateTrace(ISIM::HdlValueObject const&,");

		// Resolve GlobalDesignProperties to locate the interrupt flag byte.
		// interruptSimulation() sets bit 2 (0x04) at GlobalDesignProperties->0x62f;
		// the event loop polls this and returns when set.
		void **gdp = (void**)dlsym(simkernel,
			"_ZN5ISIMK22GlobalDesignPropertiesE");
		if(gdp && *gdp)
			_interruptFlag = (volatile unsigned char*)(*gdp) + 0x62f;

		// Resolve GlobalDP -- the base pointer for all signal data.
		// Signal values live at (char*)(*GlobalDP) + hdlValueObject.offset.
		_globalDP = (void**)dlsym(simkernel, "_ZN5ISIMK8GlobalDPE");
		if(!_globalDP)
			throw std::runtime_error("Unable to resolve ISIMK::GlobalDP");

		// Load xsi_shim.so into the Vivado namespace. The shim provides
		// C-linkage wrappers for operations that involve std::string or
		// heap allocation, keeping those on the far side of the boundary.
		if(!(_shim = dlmopen(_vivado_lmid, shim_libname.c_str(), RTLD_LAZY)))
			throw std::runtime_error(fmt::format(
				"Unable to load xsi_shim.so: {}", dlerror()));

		// Resolve shim entry points
		_shim_dbg_create = (fn_shim_dbg_create)dlsym(_shim, "shim_dbg_create");
		_shim_dbg_destroy = (fn_shim_dbg_destroy)dlsym(_shim, "shim_dbg_destroy");
		_shim_get_object_name = (fn_shim_get_object_name)dlsym(
			_shim, "shim_get_object_name");
		_shim_free = (fn_shim_free)dlsym(_shim, "shim_free");
		if(!_shim_dbg_create || !_shim_dbg_destroy || !_shim_get_object_name || !_shim_free)
			throw std::runtime_error("Unable to resolve xsi_shim.so symbols");

		// Initialize the shim with simkernel function pointers.
		// These are resolved here because the shim cannot dlsym
		// simkernel (it doesn't have the handle).
		using fn_shim_init = void(*)(void*, void*, void*, void*, void*, void*, void*);
		auto shim_init = (fn_shim_init)dlsym(_shim, "shim_init");
		if(!shim_init)
			throw std::runtime_error("Unable to resolve shim_init");

		// Resolve the simkernel symbols the shim needs
		void *p_ctor = dlsym(simkernel,
			"_ZN5ISIMK10DbgManagerC1ERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE");
		void *p_dtor = dlsym(simkernel,
			"_ZN5ISIMK10DbgManagerD1Ev");
		void *p_readDbg = dlsym(simkernel,
			"_ZN5ISIMK10DbgManager11readDbgFileEPKc");
		void *p_hasImg = dlsym(simkernel,
			"_ZNK5ISIMK10DbgManager11hasDbgImageEv");
		void *p_getName = dlsym(simkernel,
			"_ZNK5ISIMK10DbgManager17getObjectLongNameB5cxx11Ej");
		void *p_getSI = dlsym(simkernel,
			"_ZNK5ISIMK10DbgManager12getScopeInfoEj");
		void *p_getSCI = dlsym(simkernel,
			"_ZNK5ISIMK10DbgManager18getScopeCommonInfoEPKN13ISimHierarchy9ScopeInfoE");
		if(!p_ctor || !p_dtor || !p_readDbg || !p_hasImg
		   || !p_getName || !p_getSI || !p_getSCI)
			throw std::runtime_error(
				"Unable to resolve simkernel symbols for shim");

		shim_init(p_ctor, p_dtor, p_readDbg, p_hasImg,
			  p_getName, p_getSI, p_getSCI);

	} catch(...) {
		if(design) { dlclose(design); design = nullptr; }
		if(_shim) { dlclose(_shim); _shim = nullptr; }
		// simkernel is process-global -- do not dlclose
		_loaded_designs.erase(_canonical_design_path);
		throw;
	}
}

void Loader::close() {
	if(_dbg && _shim_dbg_destroy) {
		_shim_dbg_destroy(_dbg);
		_dbg = nullptr;
	}
	if(_design_handle) {
		_xsi_close(_design_handle);
		_design_handle = NULL;
	}
	if(design) { dlclose(design); design = nullptr; }
	if(_shim) { dlclose(_shim); _shim = nullptr; }
	if(!_canonical_design_path.empty()) {
		_loaded_designs.erase(_canonical_design_path);
		_canonical_design_path.clear();
	}
}

Loader::~Loader() {
	try { close(); } catch(...) {}
}

void Loader::init_hierarchy() {
	if(!(_uas = *(void**)((char*)_design_handle + iki_XSIHost_uas_offset)))
		throw std::runtime_error("Hierarchy init: UserAccessService pointer is null");

	_traceDispatcher = (TraceDispatcherHeader*)((char*)_uas + iki_UAS_traceDispatcher_offset);

	// xsim.dbg is placed next to xsimk.so by xelab.
	auto slash = _design_libname.rfind('/');
	if(slash == std::string::npos)
		throw std::runtime_error("Design library path must contain a directory component.");
	std::string dbg_path = _design_libname.substr(0, slash + 1) + "xsim.dbg";

	// shim_dbg_create allocates, constructs, and validates the DbgManager
	// entirely on the far side of the dlmopen boundary.
	_dbg = _shim_dbg_create(dbg_path.c_str());
	if(!_dbg)
		throw std::runtime_error(
			"Could not load xsim.dbg (was the design compiled with -debug?)");

	enumerate_scope(1);

	if(_name_to_id.empty())
		throw std::runtime_error("No signals found in hierarchy database.");
}

void Loader::enumerate_scope(unsigned scope_id) {
	const ScopeInfo *scope = _getScopeInfo(_dbg, scope_id);
	if(!scope)
		return;

	const ScopeCommonInfo *common = _getScopeCommonInfo(_dbg, scope);
	unsigned obj_count = common ? common->num_objects : 0;

	for(unsigned i = 0; i < obj_count; ++i) {
		unsigned obj_id = scope->first_child_obj + i;
		void *objInfo = _getObjectInfo(_dbg, obj_id);
		if(!objInfo)
			continue;

		// Get the signal name via the shim (C-linkage, no std::string
		// crossing the namespace boundary).
		char *raw = _shim_get_object_name(_dbg, obj_id);
		if(!raw)
			continue;
		std::string name(raw);
		_shim_free(raw);

		_name_to_id[name] = obj_id;

		if(scope_id == 1) {
			auto last_slash = name.rfind('/');
			std::string leaf = (last_slash != std::string::npos)
				? name.substr(last_slash + 1) : name;
			_port_to_hier[leaf] = name;
		}
	}

	for(unsigned i = 0; i < scope->child_scope_count; ++i)
		enumerate_scope(scope->first_child_scope + i);
}

std::pair<std::string, unsigned> Loader::_resolve(const std::string &name) {
	std::string resolved = name;
	if(name.find('/') == std::string::npos) {
		auto pit = _port_to_hier.find(name);
		if(pit != _port_to_hier.end())
			resolved = pit->second;
	}

	auto it = _name_to_id.find(resolved);
	if(it == _name_to_id.end())
		throw std::runtime_error(fmt::format(
			"Signal '{}' not found in hierarchy.", name));
	return {resolved, it->second};
}

std::string Loader::get_signal_value(const std::string &name) {
	return _read_hier_signal(_resolve(name).second);
}

SignalType Loader::_classify_type(unsigned obj_id, const HdlValueObject &hdl) {
	if(!_getCommonObjectInfo)
		return SignalType::VECTOR;

	void *objInfo = _getObjectInfo(_dbg, obj_id);
	if(!objInfo)
		return SignalType::VECTOR;

	const void *commonObjInfo = _getCommonObjectInfo(_dbg, objInfo);
	if(!commonObjInfo)
		return SignalType::VECTOR;

	// Verilog/SystemVerilog: signedness is erased at elaboration time
	// in xsim's IKI database. Neither objInfo, commonObjInfo, nor the
	// SystemVerilog parser preserves the signed/unsigned qualifier.
	// Verified by annotating dut.v ports with explicit signed/unsigned
	// and dumping both structs under both verilog and sv parser modes
	// -- all multi-bit signals produce identical type_id values.
	// VPI (which does expose vpiSigned) is not accessible through XSI.
	if(!hdl.is_vhdl())
		return SignalType::VECTOR;

	const uint32_t *words = (const uint32_t*)commonObjInfo;

	// commonObjInfo word[5] (+0x14) is a type_id that distinguishes VHDL types.
	// Observed values (Vivado 2025.2):
	//   0 = std_logic / std_logic_vector (enumeration-based)
	//   2 = integer (VHDL integer type)
	//   3 = unsigned (ieee.numeric_std.unsigned)
	//   4 = signed (ieee.numeric_std.signed)  [expected, unverified]
	uint32_t type_id = words[5];

	switch(type_id) {
		case 0:
			// Enumeration type: single-bit = std_logic, multi-bit = std_logic_vector
			return (hdl.length == 1)
				? SignalType::LOGIC
				: SignalType::VECTOR;
		case 2:
			// VHDL integer -- signed by language definition
			return SignalType::SIGNED_INT;
		case 3:
			return SignalType::UNSIGNED_INT;
		case 4:
			// Expected for ieee.numeric_std.signed (not yet verified)
			return SignalType::SIGNED_INT;
		default:
			return SignalType::VECTOR;
	}
}

SignalInfo& Loader::_get_signal_info(unsigned id) {
	auto it = _signalCache.find(id);
	if(it != _signalCache.end())
		return it->second;

	void *objInfo = _getObjectInfo(_dbg, id);
	if(!objInfo)
		throw std::runtime_error(fmt::format(
			"getObjectInfo returned null for object id {}", id));

	SignalInfo info{};
	_setHdlValueObject(_dbg, &info.hdl, objInfo);

	if(info.hdl.length == 0)
		info.hdl.length = 1;

	auto [ins, _] = _signalCache.emplace(id, info);
	return ins->second;
}

std::string Loader::_read_hier_signal(unsigned id) {
	const auto &info = _get_signal_info(id);

	// Direct access: data lives at *GlobalDP + dp_byte_offset.
	// For plain Verilog/VHDL signals (storage_type 0-2), start_index
	// is 0 and we can read directly. Other storage types would need
	// pointer dereference chains - not yet supported.
	const unsigned char *base = (const unsigned char*)(*_globalDP) + info.hdl.dp_byte_offset;
	return info.hdl.decode(base + info.hdl.start_index);
}

int Loader::resolve_port(const std::string &name) {
	std::string bare = name;
	auto last_slash = name.rfind('/');
	if(last_slash != std::string::npos)
		bare = name.substr(last_slash + 1);

	int port_idx = get_port_number(bare.c_str());
	if(port_idx < 0)
		throw std::runtime_error(fmt::format(
			"Signal '{}' is not a writable top-level port.", name));
	return port_idx;
}

bool Loader::is_port(const std::string &name) {
	std::string bare = name;
	auto last_slash = name.rfind('/');
	if(last_slash != std::string::npos)
		bare = name.substr(last_slash + 1);
	return get_port_number(bare.c_str()) >= 0;
}

void Loader::set_signal_value(const std::string &name, const std::string &value) {
	int port_idx = resolve_port(name);

	int length = get_port_length(port_idx);
	if(length != (int)value.length())
		throw std::invalid_argument(fmt::format(
			"Value length {} doesn't match port width {}.",
			value.length(), length));

	const auto &info = _get_signal_info(_resolve(name).second);
	auto buf = info.hdl.encode(value);
	_xsi_put_value(_design_handle, port_idx, buf.data());
}

void Loader::set_signal_value(const std::string &name, uint64_t value) {
	int port_idx = resolve_port(name);
	const auto &info = _get_signal_info(_resolve(name).second);
	auto buf = info.hdl.encode(value);
	_xsi_put_value(_design_handle, port_idx, buf.data());
}

SignalType Loader::get_signal_type(const std::string &name) {
	auto [resolved, obj_id] = _resolve(name);
	auto &info = _get_signal_info(obj_id);
	if(info.type == SignalType::UNCLASSIFIED)
		info.type = _classify_type(obj_id, info.hdl);
	return info.type;
}

int Loader::get_signal_width(const std::string &name) {
	return _get_signal_info(_resolve(name).second).hdl.length;
}

std::vector<std::string> Loader::list_signals() {
	std::vector<std::string> names;
	names.reserve(_name_to_id.size());
	for(const auto &[name, id] : _name_to_id)
		names.push_back(name);
	return names;
}

// ---------------------------------------------------------------------------
// Watch API
// ---------------------------------------------------------------------------

void Loader::init_watch() {
	if(_watchRegistered)
		return;

	if(!_traceDispatcher)
		throw std::runtime_error("Trace infrastructure not available.");

	// Point vptr at our static vtable (skip the two preamble entries)
	_watchFeature.vptr = &s_watchVtable[2];
	_watchFeature.ptr_08 = nullptr;
	_watchFeature.dbgMgr = _dbg;
	_watchFeature.traceTypeMgr = nullptr;

	// Register our WatchFeature into the TraceDispatcher's features vector.
	// We append (rather than replacing a slot) so we don't clobber WDB tracing.
	auto *disp = _traceDispatcher;

	size_t feat_count = disp->features_end - disp->features_begin;

	// Set traceFlagBit = 1 << feat_count (our position in the vector)
	_watchFeature.traceFlagBit = 1u << feat_count;

	// Claim the next featureIndex from the counter
	_watchFeature.featureIndex = disp->next_index;
	disp->next_index += 1;

	// Append our WatchFeature pointer to the features vector.
	// The vector typically has capacity for 5+ entries.
	if(disp->features_end >= disp->features_capacity)
		throw std::runtime_error("TraceDispatcher features vector has no spare capacity.");

	*disp->features_end = &_watchFeature;
	disp->features_end += 1;

	// Append our featureIndex to the parallel indexMap vector
	if(disp->index_map_end >= disp->index_map_capacity)
		throw std::runtime_error("TraceDispatcher indexMap vector has no spare capacity.");

	*disp->index_map_end = _watchFeature.featureIndex;
	disp->index_map_end += 1;

	// Verify registration
	if(disp->features_begin[feat_count] != &_watchFeature)
		throw std::runtime_error("WatchFeature registration verification failed.");

	_watchRegistered = true;
}

void Loader::watch(const std::string &name, bool stop) {
	auto [resolved, obj_id] = _resolve(name);

	// If already watching, update the stop flag
	auto existing = _watchCookies.find(resolved);
	if(existing != _watchCookies.end()) {
		existing->second->interruptFlag = stop ? _interruptFlag : nullptr;
		return;
	}

	// Ensure our feature is registered in the dispatcher
	init_watch();

	// Allocate a per-signal cookie
	auto cookie = std::make_unique<WatchCookie>();
	cookie->name = resolved;
	cookie->obj_id = obj_id;
	cookie->interruptFlag = stop ? _interruptFlag : nullptr;

	// Get (or build and cache) the SignalInfo for this signal
	const auto &info = _get_signal_info(obj_id);

	// Activate per-signal tracing via activateTrace
	_activateTrace(_traceDispatcher, &info.hdl, &_watchFeature,
		/*activate=*/true, /*unknown=*/false, cookie.get());

	_watchCookies[resolved] = std::move(cookie);
}

std::vector<std::string> Loader::get_changes() {
	std::vector<std::string> changed;

	for(auto &[name, cookie] : _watchCookies) {
		if(cookie->dirty.exchange(false, std::memory_order_relaxed))
			changed.push_back(name);
	}

	return changed;
}
