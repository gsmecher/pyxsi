#define FMT_HEADER_ONLY

#include <cxxabi.h>
#include <fmt/format.h>
#include "xsi_loader.h"

using namespace Xsi;

static constexpr unsigned char SLV_U=0, SLV_X=1, SLV_0=2, SLV_1=3,
	SLV_Z=4, SLV_W=5, SLV_L=6, SLV_H=7, SLV_DASH=8;

static char slv_to_char(unsigned char slv) {
	switch(slv) {
		case SLV_0: return '0';
		case SLV_1: return '1';
		case SLV_U: return 'U';
		case SLV_X: return 'X';
		case SLV_Z: return 'Z';
		case SLV_W: return 'W';
		case SLV_L: return 'L';
		case SLV_H: return 'H';
		case SLV_DASH: return '-';
		default: throw std::runtime_error("Unexpected logic value!");
	}
}

static unsigned char char_to_slv(char ch) {
	switch(ch) {
		case '0': return SLV_0;
		case '1': return SLV_1;
		case 'U': return SLV_U;
		case 'X': return SLV_X;
		case 'Z': return SLV_Z;
		case 'W': return SLV_W;
		case 'L': return SLV_L;
		case 'H': return SLV_H;
		case '-': return SLV_DASH;
		default: throw std::runtime_error(fmt::format(
			"Unexpected logic value '{}'", ch));
	}
}

static std::string decode_value(const unsigned char *data, size_t bit_width, bool is_vhdl) {
	std::string s(bit_width, '0');

	if(is_vhdl) {
		for(size_t n = 0; n < bit_width; n++)
			s[n] = slv_to_char(data[n]);
	} else {
		struct logicval { unsigned aVal; unsigned bVal; };
		auto *lv = reinterpret_cast<const logicval*>(data);

		for(size_t n = 0; n < bit_width; n++) {
			int aVal = (lv[n/32].aVal >> (n&31)) & 1u;
			int bVal = (lv[n/32].bVal >> (n&31)) & 1u;

			switch((bVal << 1) | aVal) {
				case 0b00: s[bit_width-1-n] = '0'; break;
				case 0b01: s[bit_width-1-n] = '1'; break;
				case 0b10: s[bit_width-1-n] = 'Z'; break;
				case 0b11: s[bit_width-1-n] = 'X'; break;
			}
		}
	}
	return s;
}

static std::vector<unsigned char> encode_value(const std::string &value, bool is_vhdl) {
	size_t length = value.size();

	if(is_vhdl) {
		std::vector<unsigned char> buf(length);
		for(size_t n = 0; n < length; n++)
			buf[n] = char_to_slv(value[n]);
		return buf;
	} else {
		auto logicval = std::vector<s_xsi_vlog_logicval>((length+31)/32, {0, 0});
		for(size_t n = 0; n < length; n++) {
			switch(value[length-1-n]) {
				/* Per UG900 Table 64 */
				case '0':
					break; /* nop, already 0 */
				case '1':
					logicval[n/32].aVal |= 1u<<(n&31u);
					break;
				case 'Z':
					logicval[n/32].bVal |= 1u<<(n&31u);
					break;
				case 'X':
					logicval[n/32].aVal |= 1u<<(n&31u);
					logicval[n/32].bVal |= 1u<<(n&31u);
					break;
			}
		}
		std::vector<unsigned char> buf(
			reinterpret_cast<unsigned char*>(logicval.data()),
			reinterpret_cast<unsigned char*>(logicval.data()) + logicval.size() * sizeof(s_xsi_vlog_logicval));
		return buf;
	}
}

// ---------------------------------------------------------------------------
// Offsets into opaque IKI/xsim data structures.
// These were determined empirically and may change across Vivado versions.
// ---------------------------------------------------------------------------

// XSIHost: offset to UserAccessService*
constexpr size_t iki_XSIHost_uas_offset = 0x470;

// ScopeInfo field offsets
constexpr size_t iki_ScopeInfo_child_scope_count = 0x0c;
constexpr size_t iki_ScopeInfo_first_child_scope = 0x10;
constexpr size_t iki_ScopeInfo_first_child_obj   = 0x14;

// ScopeCommonInfo field offsets
constexpr size_t iki_ScopeCommonInfo_obj_count = 0x0c;

// HdlValueObject layout
constexpr size_t iki_HdlValueObject_size        = 0x20;
constexpr size_t iki_HdlValueObject_format       = 0x14;
constexpr size_t iki_HdlValueObject_bit_width    = 0x1c;
constexpr unsigned iki_HdlValueFormat_VHDL       = 2;

// DbgManager object allocation size (upper bound)
constexpr size_t iki_DbgManager_alloc_size = 4096;

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

Loader::Loader(const std::string& design_libname, const std::string& simkernel_libname) :
	_design_libname(design_libname),
	_simkernel_libname(simkernel_libname),
	_design_handle(NULL)
{
	if(!(design = dlopen(design_libname.c_str(), RTLD_LAZY|RTLD_GLOBAL)))
		throw std::runtime_error(fmt::format("Unable to load design library {}", design_libname));

	if(!(simkernel = dlopen(simkernel_libname.c_str(), RTLD_LAZY|RTLD_GLOBAL)))
		throw std::runtime_error(fmt::format("Unable to load simulator library {}", simkernel_libname));

#define xstr(s) str(s)
#define str(s) #s
#define RESOLVE(library_handle, x) ({					\
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
	RESOLVE(simkernel, xsi_get_value);
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
#define RESOLVE_MANGLED(member, mangled, demangled_prefix) ({		\
	dlerror();							\
	if(!(member = (decltype(member))dlsym(simkernel, mangled)))	\
		throw std::runtime_error(fmt::format(			\
			"Unable to resolve {}: {}",			\
			demangled_prefix, dlerror()));			\
	assert_demangles(mangled, demangled_prefix);			\
	})

	RESOLVE_MANGLED(_isPortVHDL,
		"_ZN5ISIMK7XSIHost21isPortValueFormatVHDLEi",
		"ISIMK::XSIHost::isPortValueFormatVHDL(int)");

	RESOLVE_MANGLED(_getObjectInfo,
		"_ZNK5ISIMK10DbgManager13getObjectInfoEj",
		"ISIMK::DbgManager::getObjectInfo(unsigned int)");

	RESOLVE_MANGLED(_getObjectLongName,
		"_ZNK5ISIMK10DbgManager17getObjectLongNameB5cxx11Ej",
		"ISIMK::DbgManager::getObjectLongName");

	RESOLVE_MANGLED(_getCommonObjectInfo,
		"_ZNK5ISIMK10DbgManager19getCommonObjectInfoEPKN13ISimHierarchy10ObjectInfoE",
		"ISIMK::DbgManager::getCommonObjectInfo(ISimHierarchy::ObjectInfo const*)");

	RESOLVE_MANGLED(_setHdlValueObject,
		"_ZNK5ISIMK10DbgManager17setHdlValueObjectERN4ISIM14HdlValueObjectEPKN13ISimHierarchy10ObjectInfoE",
		"ISIMK::DbgManager::setHdlValueObject(ISIM::HdlValueObject&, ISimHierarchy::ObjectInfo const*)");

	RESOLVE_MANGLED(_hasDbgImage,
		"_ZNK5ISIMK10DbgManager11hasDbgImageEv",
		"ISIMK::DbgManager::hasDbgImage()");

	RESOLVE_MANGLED(_readDbgFile,
		"_ZN5ISIMK10DbgManager11readDbgFileEPKc",
		"ISIMK::DbgManager::readDbgFile(char const*)");

	RESOLVE_MANGLED(_dbgManagerCtor,
		"_ZN5ISIMK10DbgManagerC1ERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE",
		"ISIMK::DbgManager::DbgManager(std::");

	RESOLVE_MANGLED(_dbgManagerDtor,
		"_ZN5ISIMK10DbgManagerD1Ev",
		"ISIMK::DbgManager::~DbgManager()");

	RESOLVE_MANGLED(_getScopeInfo,
		"_ZNK5ISIMK10DbgManager12getScopeInfoEj",
		"ISIMK::DbgManager::getScopeInfo(unsigned int)");

	RESOLVE_MANGLED(_getScopeCommonInfo,
		"_ZNK5ISIMK10DbgManager18getScopeCommonInfoEPKN13ISimHierarchy9ScopeInfoE",
		"ISIMK::DbgManager::getScopeCommonInfo(ISimHierarchy::ScopeInfo const*)");

	RESOLVE_MANGLED(_getValue,
		"_ZN5ISIMK17UserAccessService8getValueERKN4ISIM14HdlValueObjectEPhPjjjPSt6vectorIiSaIiEEPS7_ISt4pairIS5_iESaISC_EES5_S5_S5_Pb",
		"ISIMK::UserAccessService::getValue(ISIM::HdlValueObject const&,");
}

void Loader::init_hierarchy() {
	if(!(_uas = *(void**)((char*)_design_handle + iki_XSIHost_uas_offset)))
		throw std::runtime_error("Hierarchy init: UserAccessService pointer is null");

	// xsim.dbg is placed next to xsimk.so by xelab.
	auto slash = _design_libname.rfind('/');
	if(slash == std::string::npos)
		throw std::runtime_error("Design library path must contain a directory component.");
	std::string dbg_path = _design_libname.substr(0, slash + 1) + "xsim.dbg";

	void *buf = calloc(1, iki_DbgManager_alloc_size);
	std::string empty_str;
	_dbgManagerCtor(buf, empty_str);
	_readDbgFile(buf, dbg_path.c_str());

	if(!_hasDbgImage(buf) || !_getScopeInfo(buf, 1)) {
		_dbgManagerDtor(buf);
		free(buf);
		throw std::runtime_error(
			"Could not load xsim.dbg (was the design compiled with -debug?)");
	}
	_dbg = buf;

	enumerate_scope(1);

	if(_name_to_id.empty())
		throw std::runtime_error("No signals found in hierarchy database.");
}

void Loader::enumerate_scope(unsigned scope_id) {
	void *scopeInfo = _getScopeInfo(_dbg, scope_id);
	if(!scopeInfo)
		return;

	unsigned child_scope_count = *(unsigned*)((char*)scopeInfo + iki_ScopeInfo_child_scope_count);
	unsigned first_child_scope = *(unsigned*)((char*)scopeInfo + iki_ScopeInfo_first_child_scope);
	unsigned first_child_obj   = *(unsigned*)((char*)scopeInfo + iki_ScopeInfo_first_child_obj);

	void *scopeCommon = _getScopeCommonInfo(_dbg, scopeInfo);
	unsigned obj_count = 0;
	if(scopeCommon)
		obj_count = *(unsigned*)((char*)scopeCommon + iki_ScopeCommonInfo_obj_count);

	for(unsigned i = 0; i < obj_count; ++i) {
		unsigned obj_id = first_child_obj + i;
		void *objInfo = _getObjectInfo(_dbg, obj_id);
		if(!objInfo)
			continue;

		std::string name;
		_getObjectLongName(&name, _dbg, obj_id);
		if(!name.empty()) {
			_name_to_id[name] = obj_id;

			if(scope_id == 1) {
				auto last_slash = name.rfind('/');
				std::string leaf = (last_slash != std::string::npos)
					? name.substr(last_slash + 1) : name;
				_port_to_hier[leaf] = name;
			}
		}
	}

	for(unsigned i = 0; i < child_scope_count; ++i)
		enumerate_scope(first_child_scope + i);
}

std::string Loader::get_signal_value(const std::string &name) {
	// Resolve bare port names to their hierarchical path
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

	return _read_hier_signal(it->second);
}

std::string Loader::_read_hier_signal(unsigned id) {
	void *objInfo = _getObjectInfo(_dbg, id);
	if(!objInfo)
		throw std::runtime_error(fmt::format(
			"getObjectInfo returned null for object id {}", id));

	alignas(8) unsigned char hdlObj[iki_HdlValueObject_size] = {};
	_setHdlValueObject(_dbg, hdlObj, objInfo);

	unsigned format = *(unsigned*)(hdlObj + iki_HdlValueObject_format);
	bool is_vhdl = (format == iki_HdlValueFormat_VHDL);
	unsigned bit_width = *(unsigned*)(hdlObj + iki_HdlValueObject_bit_width);
	if(bit_width == 0)
		bit_width = 1;

	size_t buf_size;
	if(is_vhdl)
		buf_size = bit_width;
	else
		buf_size = ((bit_width + 31) / 32) * 8;

	std::vector<unsigned char> buf(buf_size, 0);

	_getValue(_uas, hdlObj, buf.data(), nullptr, 0, 0,
		nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

	return decode_value(buf.data(), bit_width, is_vhdl);
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

void Loader::set_signal_value(const std::string &name, const std::string &value) {
	int port_idx = resolve_port(name);

	int length = get_port_length(port_idx);
	if(length != (int)value.length())
		throw std::invalid_argument(fmt::format(
			"Value length {} doesn't match port width {}.",
			value.length(), length));

	bool is_vhdl = _isPortVHDL(_design_handle, port_idx);
	auto buf = encode_value(value, is_vhdl);
	put_value(port_idx, buf.data());
}

void Loader::set_signal_value(const std::string &name, uint64_t value) {
	int port_idx = resolve_port(name);
	int width = get_port_length(port_idx);

	if(width < 64)
		value &= (1ULL << width) - 1;

	bool is_vhdl = _isPortVHDL(_design_handle, port_idx);

	if(is_vhdl) {
		std::vector<unsigned char> buf(width);
		for(int i = 0; i < width; i++)
			buf[width - 1 - i] = (value >> i) & 1 ? SLV_1 : SLV_0;
		put_value(port_idx, buf.data());
	} else {
		auto logicval = std::vector<s_xsi_vlog_logicval>((width+31)/32, {0, 0});
		for(int i = 0; i < width && i < 64; i++)
			if((value >> i) & 1)
				logicval[i/32].aVal |= 1u << (i & 31u);
		put_value(port_idx, logicval.data());
	}
}

std::vector<std::string> Loader::list_signals() {
	std::vector<std::string> names;
	names.reserve(_name_to_id.size());
	for(const auto &[name, id] : _name_to_id)
		names.push_back(name);
	return names;
}
