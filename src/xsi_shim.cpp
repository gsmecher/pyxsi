// xsi_shim.cpp -- thin C-linkage wrappers loaded into the Vivado dlmopen
// namespace.  All allocations and std::string operations happen on the
// "far side" of the namespace boundary, avoiding cross-heap corruption.
//
// The host resolves these extern "C" symbols via dlsym on the shim handle.
// ScopeInfo and ScopeCommonInfo are opaque types (void*) -- the shim only
// forwards pointers, never dereferences them.

#include <cstdlib>
#include <cstring>
#include <string>

using fn_dbgManagerCtor     = void(*)(void *dbg, const std::string &path);
using fn_dbgManagerDtor     = void(*)(void *dbg);
using fn_readDbgFile        = void(*)(void *dbg, const char *path);
using fn_hasDbgImage        = bool(*)(const void *dbg);
using fn_getObjectLongName  = std::string*(*)(std::string *result, const void *dbg, unsigned id);
using fn_getScopeInfo       = const void*(*)(const void *dbg, unsigned scope_id);
using fn_getScopeCommonInfo = const void*(*)(const void *dbg, const void *scope_info);

static fn_dbgManagerCtor     s_dbgManagerCtor;
static fn_dbgManagerDtor     s_dbgManagerDtor;
static fn_readDbgFile        s_readDbgFile;
static fn_hasDbgImage        s_hasDbgImage;
static fn_getObjectLongName  s_getObjectLongName;
static fn_getScopeInfo       s_getScopeInfo;
static fn_getScopeCommonInfo s_getScopeCommonInfo;

static constexpr size_t DBG_ALLOC_SIZE = 4096;

extern "C" {
	// Provide function pointers that the shim will call.  The host resolves
	// these from simkernel via dlsym and passes them in.
	void shim_init(fn_dbgManagerCtor     ctor,
			fn_dbgManagerDtor     dtor,
			fn_readDbgFile        readDbg,
			fn_hasDbgImage        hasImg,
			fn_getObjectLongName  getName,
			fn_getScopeInfo       getSI,
			fn_getScopeCommonInfo getSCI) {
		s_dbgManagerCtor     = ctor;
		s_dbgManagerDtor     = dtor;
		s_readDbgFile        = readDbg;
		s_hasDbgImage        = hasImg;
		s_getObjectLongName  = getName;
		s_getScopeInfo       = getSI;
		s_getScopeCommonInfo = getSCI;
	}

	// Allocate + construct a DbgManager, read the debug image, validate.
	// Returns opaque handle, or NULL on failure.
	void *shim_dbg_create(const char *dbg_path) {
		void *buf = calloc(1, DBG_ALLOC_SIZE);
		if(!buf) return nullptr;

		std::string empty_str;
		s_dbgManagerCtor(buf, empty_str);
		s_readDbgFile(buf, dbg_path);

		if(!s_hasDbgImage(buf) || !s_getScopeInfo(buf, 1)) {
			s_dbgManagerDtor(buf);
			free(buf);
			return nullptr;
		}
		return buf;
	}

	// Destroy + free a DbgManager handle.
	void shim_dbg_destroy(void *dbg) {
		if(dbg) {
			s_dbgManagerDtor(dbg);
			free(dbg);
		}
	}

	// Get the long name for an object.  Returns a strdup'd string
	// (allocated with malloc on the Vivado side), or nullptr on
	// empty/error.  Caller must release with shim_free().
	char *shim_get_object_name(const void *dbg, unsigned obj_id) {
		std::string name;
		s_getObjectLongName(&name, dbg, obj_id);
		if(name.empty()) return nullptr;
		return strdup(name.c_str());
	}

	void shim_free(void *p) { free(p); }
}
