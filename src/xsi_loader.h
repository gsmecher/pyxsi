#pragma once

#include "xsi.h"
#include <dlfcn.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <atomic>

namespace Xsi {

	// Signal type classification for type-aware Python access.
	enum class SignalType { UNCLASSIFIED = -1, VECTOR, SIGNED_INT, UNSIGNED_INT, LOGIC };

	// Mirror structs for opaque IKI/xsim data structures.
	struct ScopeInfo {
		uint32_t name_string_id;          // 0x00
		uint32_t parent_scope_id;         // 0x04: 0xFFFFFFFF = root
		uint32_t _unknown_08;             // 0x08
		uint32_t child_scope_count;       // 0x0c
		uint32_t first_child_scope;       // 0x10
		uint32_t first_child_obj;         // 0x14
		uint32_t _unknown_18;             // 0x18
		uint32_t _unknown_1c;             // 0x1c
		uint32_t scope_common_info_index; // 0x20
	};
	static_assert(sizeof(ScopeInfo) == 36);

	struct ScopeCommonInfo {
		uint32_t _unknown_00;             // 0x00
		uint32_t _unknown_04;             // 0x04
		uint32_t scope_type;              // 0x08: enum (<=8 = Verilog, 0x14-0x17 = SystemC)
		uint32_t num_objects;             // 0x0c
		uint32_t _unknown_10;             // 0x10
		uint32_t _unknown_14;             // 0x14
		uint32_t _unknown_18;             // 0x18
		uint32_t _unknown_1c;             // 0x1c
		uint32_t _unknown_20;             // 0x20
	};
	static_assert(sizeof(ScopeCommonInfo) == 36);

	// Partial mirror of ISIMK::TraceDispatcher's header region.
	// The TraceDispatcher is embedded at offset 0x2C0 within the UAS
	// object; we only access the first 0x40 bytes.
	struct TraceDispatcherHeader {
		void     *vptr;                   // 0x00: vtable pointer
		void    **features_begin;         // 0x08: std::vector<TraceFeature*>::begin
		void    **features_end;           // 0x10: std::vector<TraceFeature*>::end
		void    **features_capacity;      // 0x18: std::vector<TraceFeature*>::capacity_end
		int32_t  *index_map_begin;        // 0x20: parallel index vector begin
		int32_t  *index_map_end;          // 0x28: parallel index vector end
		int32_t  *index_map_capacity;     // 0x30: parallel index vector capacity_end
		int32_t   next_index;             // 0x38: next featureIndex to assign
		uint32_t  _pad_3c;                // 0x3c
	};
	static_assert(offsetof(TraceDispatcherHeader, next_index) == 0x38);

	struct HdlValueObject {
		int64_t  dp_byte_offset; // 0x00: byte offset into *GlobalDP
		int64_t  trace_offset;   // 0x08: SystemC trace handle
		int32_t  storage_type;   // 0x10: controls pointer deref depth and copy strategy
		int32_t  value_format;   // 0x14: ValueFormat enum (0-2=Verilog/VHDL, 4-6=indirect, 8=SystemC)
		int32_t  start_index;    // 0x18: bit/byte offset within data region
		int32_t  length;         // 0x1c: width in bits (Verilog) or bytes (VHDL)

		// Everything below this does not affect POD layout
		static constexpr int32_t FORMAT_VERILOG = 0;
		static constexpr int32_t FORMAT_VHDL    = 2;

		// VHDL std_logic encoding (one byte per bit)
		static constexpr unsigned char SLV_U=0, SLV_X=1, SLV_0=2, SLV_1=3,
			SLV_Z=4, SLV_W=5, SLV_L=6, SLV_H=7, SLV_DASH=8;

		static char slv_to_char(unsigned char slv) {
			switch(slv) {
				case SLV_0: return '0'; case SLV_1: return '1';
				case SLV_U: return 'U'; case SLV_X: return 'X';
				case SLV_Z: return 'Z'; case SLV_W: return 'W';
				case SLV_L: return 'L'; case SLV_H: return 'H';
				case SLV_DASH: return '-';
				default: throw std::runtime_error("Unexpected logic value!");
			}
		}

		static unsigned char char_to_slv(char ch) {
			switch(ch) {
				case '0': return SLV_0; case '1': return SLV_1;
				case 'U': return SLV_U; case 'X': return SLV_X;
				case 'Z': return SLV_Z; case 'W': return SLV_W;
				case 'L': return SLV_L; case 'H': return SLV_H;
				case '-': return SLV_DASH;
				default: throw std::runtime_error(
					std::string("Unexpected logic value '") + ch + "'");
			}
		}

		bool is_vhdl() const { return value_format == FORMAT_VHDL; }

		std::string decode(const unsigned char *data) const {
			std::string s(length, '0');
			if(is_vhdl()) {
				for(int32_t n = 0; n < length; n++)
					s[n] = slv_to_char(data[n]);
			} else {
				struct logicval { unsigned aVal; unsigned bVal; };
				auto *lv = reinterpret_cast<const logicval*>(data);
				for(int32_t n = 0; n < length; n++) {
					int aVal = (lv[n/32].aVal >> (n&31)) & 1u;
					int bVal = (lv[n/32].bVal >> (n&31)) & 1u;
					switch((bVal << 1) | aVal) {
						case 0b00: s[length-1-n] = '0'; break;
						case 0b01: s[length-1-n] = '1'; break;
						case 0b10: s[length-1-n] = 'Z'; break;
						case 0b11: s[length-1-n] = 'X'; break;
					}
				}
			}
			return s;
		}

		std::vector<unsigned char> encode(const std::string &value) const {
			if(is_vhdl()) {
				std::vector<unsigned char> buf(value.size());
				for(size_t n = 0; n < value.size(); n++)
					buf[n] = char_to_slv(value[n]);
				return buf;
			} else {
				auto logicval = std::vector<s_xsi_vlog_logicval>(
					(value.size()+31)/32, {0, 0});
				for(size_t n = 0; n < value.size(); n++) {
					switch(value[value.size()-1-n]) {
						case '0': break;
						case '1': logicval[n/32].aVal |= 1u<<(n&31u); break;
						case 'Z': logicval[n/32].bVal |= 1u<<(n&31u); break;
						case 'X': logicval[n/32].aVal |= 1u<<(n&31u);
						          logicval[n/32].bVal |= 1u<<(n&31u); break;
					}
				}
				std::vector<unsigned char> buf(
					reinterpret_cast<unsigned char*>(logicval.data()),
					reinterpret_cast<unsigned char*>(logicval.data())
						+ logicval.size() * sizeof(s_xsi_vlog_logicval));
				return buf;
			}
		}

		std::vector<unsigned char> encode(uint64_t value) const {
			int width = length;
			if(width < 64)
				value &= (1ULL << width) - 1;
			if(is_vhdl()) {
				std::vector<unsigned char> buf(width);
				for(int i = 0; i < width; i++)
					buf[width - 1 - i] = (value >> i) & 1 ? SLV_1 : SLV_0;
				return buf;
			} else {
				auto logicval = std::vector<s_xsi_vlog_logicval>(
					(width+31)/32, {0, 0});
				for(int i = 0; i < width && i < 64; i++)
					if((value >> i) & 1)
						logicval[i/32].aVal |= 1u << (i & 31u);
				std::vector<unsigned char> buf(
					reinterpret_cast<unsigned char*>(logicval.data()),
					reinterpret_cast<unsigned char*>(logicval.data())
						+ logicval.size() * sizeof(s_xsi_vlog_logicval));
				return buf;
			}
		}
	};
	static_assert(sizeof(HdlValueObject) == 0x20);

	// SignalInfo: cached HdlValueObject + type classification.
	struct SignalInfo {
		HdlValueObject hdl;
		SignalType type = SignalType::UNCLASSIFIED;
	};

	// WatchCookie: per-signal cookie passed to activateTrace and
	// received back by our traceVlogNet/traceVhdlNet callbacks.
	struct WatchCookie {
		std::atomic<bool> dirty{false};
		std::string name;       // hierarchical signal name (for reporting)
		unsigned obj_id;        // hierarchy database object id (for reading value)
		// When non-null, the callback sets *interruptFlag |= 0x04 to
		// make the event loop return, causing run() to complete early.
		volatile unsigned char *interruptFlag = nullptr;
	};

	// WatchFeature: a fabricated ISIMK::TraceFeature-compatible object
	// registered into the TraceDispatcher to receive per-signal change
	// notifications via activateTrace.  Called from deep inside the IKI
	// simulator: must be pure C/C++, no Python, no exceptions, no
	// allocations.
	struct WatchFeature {
		void **vptr;              // +0x00: points to our static vtable
		void  *ptr_08;            // +0x08: unused (NULL)
		uint32_t traceFlagBit;    // +0x10: 1 << position in features vector
		int32_t  featureIndex;    // +0x14: index into per-signal callback arrays
		void *dbgMgr;             // +0x18: DbgManager* (unused by us)
		void *traceTypeMgr;       // +0x20: TraceTypeManager* (unused by us)
	};

	class Loader {
		public:
			Loader(const std::string& dll_name, const std::string& simkernel_libname,
					const std::string& shim_libname);
			~Loader();

			bool isopen() const { return !!_design_handle; }
			void open(p_xsi_setup_info setup_info){
				_design_handle = _xsi_open(setup_info);
				if(isopen()) {
					_num_ports = get_int(xsiNumTopPorts);
				} else
					throw std::runtime_error("Failed to open design!");
			}

			void close();

			void run(XSI_INT64 step) {
				if(!isopen())
					throw std::runtime_error("Design not open! Can't execute XSI method.");
				// Clear the interrupt flag before entering the event loop,
				// in case a previous watch(stop=True) triggered it.
				if(_interruptFlag)
					*_interruptFlag &= ~0x04;
				_xsi_run(_design_handle, step);
				// xsi_get_status() is unreliable for VHDL severity-failure
				// assertions: xsim maps internal status 0xa to xsiNormal.
				// xsi_get_error_info() drains a per-run message queue and
				// returns null when empty, so it's a reliable indicator.
				const char *err = _xsi_get_error_info(_design_handle);
				if(err && err[0])
					throw std::runtime_error(std::string(err));
			}

			void restart() {
				if(!isopen())
					throw std::runtime_error("Design not open! Can't execute XSI method.");
				_xsi_restart(_design_handle);
			}

			int get_port_number(const char* port_name){
				return _xsi_get_port_number(_design_handle, port_name);
			}

			int get_int(int property_type){
				return _xsi_get_int(_design_handle, property_type);
			}

			int get_int_port(int port_number, int property_type){
				return _xsi_get_int_port(_design_handle, port_number, property_type);
			}

			const char* get_str_port(int port_number, int property_type){
				return _xsi_get_str_port(_design_handle, port_number, property_type);
			}

			int get_status() { return _xsi_get_status(_design_handle); }

			const char* get_error_info(){ return _xsi_get_error_info(_design_handle); }
			void trace_all() { _xsi_trace_all(_design_handle); }

			int num_ports() const { return _num_ports; }

			int get_port_direction(int port_number) {
				return get_int_port(port_number, xsiDirectionTopPort);
			}

			int get_port_length(int port_number) {
				return get_int_port(port_number, xsiHDLValueSize);
			}

			const char* get_port_name(int port_number) {
				return get_str_port(port_number, xsiNameTopPort);
			}

			// Hierarchical naming and typing
			void init_hierarchy();
			std::string get_signal_value(const std::string &name);
			void set_signal_value(const std::string &name, const std::string &value);
			void set_signal_value(const std::string &name, uint64_t value);
			std::vector<std::string> list_signals();
			SignalType get_signal_type(const std::string &name);
			int get_signal_width(const std::string &name);

			// Watch API
			void watch(const std::string &name, bool stop = false);
			std::vector<std::string> get_changes();

			// VCD dump
			void vcd_dumpfile(const std::string &filename) {
				_vcdDumpFile(_uas, "", filename.c_str());
			}
			void vcd_dumpvars(int depth = 0) {
				_vcdDumpVarScope(_uas, 1, depth);
			}
			void vcd_dumpon()              { _vcdDumpOn(_uas); }
			void vcd_dumpoff()             { _vcdDumpOff(_uas); }
			void vcd_dumpall()             { _vcdDumpAll(_uas); }
			void vcd_dumpflush()           { _vcdDumpFlush(_uas); }
			void vcd_dumplimit(long long b){ _vcdDumpLimit(_uas, b); }
			void vcd_close()               { _vcdClose(_uas); }

			bool is_port(const std::string &name);

		private:
			void *design = nullptr;

			std::string _design_libname;
			std::string _canonical_design_path;
			xsiHandle _design_handle;
			int _num_ports = 0;

			// XSI function pointers
			t_fp_xsi_open _xsi_open;
			t_fp_xsi_close _xsi_close;
			t_fp_xsi_run _xsi_run;
			t_fp_xsi_put_value _xsi_put_value;
			t_fp_xsi_get_status _xsi_get_status;
			t_fp_xsi_get_error_info _xsi_get_error_info;
			t_fp_xsi_restart _xsi_restart;
			t_fp_xsi_get_port_number _xsi_get_port_number;
			t_fp_xsi_get_int _xsi_get_int;
			t_fp_xsi_get_int_port _xsi_get_int_port;
			t_fp_xsi_get_str_port _xsi_get_str_port;
			t_fp_xsi_trace_all _xsi_trace_all;

			// Hierarchy function pointers (resolved from simkernel .so; may be null)
			using fn_getObjectInfo = void*(*)(const void *dbg, unsigned id);
			using fn_setHdlValueObject = void(*)(const void *dbg, HdlValueObject *hdlObj, const void *objInfo);
			using fn_getScopeInfo = const ScopeInfo*(*)(const void *dbg, unsigned id);
			using fn_getScopeCommonInfo = const ScopeCommonInfo*(*)(const void *dbg, const ScopeInfo *scopeInfo);
			using fn_getCommonObjectInfo = const void*(*)(const void *dbg, const void *objInfo);
			fn_getObjectInfo _getObjectInfo = nullptr;
			fn_setHdlValueObject _setHdlValueObject = nullptr;
			fn_getCommonObjectInfo _getCommonObjectInfo = nullptr;
			fn_getScopeInfo _getScopeInfo = nullptr;
			fn_getScopeCommonInfo _getScopeCommonInfo = nullptr;

			using fn_activateTrace = void(*)(void *traceDispatcher,
				const HdlValueObject *hdlObj, void *traceFeature,
				bool activate, bool, void *cookie);
			fn_activateTrace _activateTrace = nullptr;

			// VCD function pointers (resolved from simkernel)
			using fn_vcdDumpFile      = void(*)(void *uas, const char *filename, const char *second);
			using fn_vcdDumpVarScope  = void(*)(void *uas, int scope_id, int depth);
			using fn_vcdDumpVarObject = void(*)(void *uas, int obj_id);
			using fn_vcdDumpOn        = void(*)(void *uas);
			using fn_vcdDumpOff       = void(*)(void *uas);
			using fn_vcdDumpAll       = void(*)(void *uas);
			using fn_vcdDumpFlush     = void(*)(void *uas);
			using fn_vcdDumpLimit     = void(*)(void *uas, long long bytes);
			using fn_vcdClose         = void(*)(void *uas);

			fn_vcdDumpFile      _vcdDumpFile = nullptr;
			fn_vcdDumpVarScope  _vcdDumpVarScope = nullptr;
			fn_vcdDumpVarObject _vcdDumpVarObject = nullptr;
			fn_vcdDumpOn        _vcdDumpOn = nullptr;
			fn_vcdDumpOff       _vcdDumpOff = nullptr;
			fn_vcdDumpAll       _vcdDumpAll = nullptr;
			fn_vcdDumpFlush     _vcdDumpFlush = nullptr;
			fn_vcdDumpLimit     _vcdDumpLimit = nullptr;
			fn_vcdClose         _vcdClose = nullptr;

			// Shim function pointers -- C-linkage wrappers loaded into
			// the Vivado namespace. All std::string and heap operations
			// happen on the far side of the dlmopen boundary.
			void *_shim = nullptr;
			using fn_shim_dbg_create = void*(*)(const char*);
			using fn_shim_dbg_destroy = void(*)(void*);
			using fn_shim_get_object_name = char*(*)(const void*, unsigned);
			using fn_shim_free = void(*)(void*);
			fn_shim_dbg_create _shim_dbg_create = nullptr;
			fn_shim_dbg_destroy _shim_dbg_destroy = nullptr;
			fn_shim_get_object_name _shim_get_object_name = nullptr;
			fn_shim_free _shim_free = nullptr;

			// Points to GlobalDesignProperties->0x62f (the event-loop
			// interrupt flag byte). Setting bit 2 (0x04) causes the
			// event loop to return, ending the current run() early.
			volatile unsigned char *_interruptFlag = nullptr;

			int resolve_port(const std::string &name);
			// Resolve a signal name (bare or hierarchical) to
			// {hierarchical name, obj_id}.
			std::pair<std::string, unsigned> _resolve(const std::string &name);
			void enumerate_scope(unsigned scope_id);
			std::string _read_hier_signal(unsigned id);
			SignalInfo& _get_signal_info(unsigned id);
			SignalType _classify_type(unsigned obj_id, const HdlValueObject &hdl);
			void init_watch();

			void *_dbg = nullptr;
			void *_uas = nullptr;
			TraceDispatcherHeader *_traceDispatcher = nullptr;
			void **_globalDP = nullptr;   // &ISIMK::GlobalDP (points to char*)

			std::unordered_map<std::string, unsigned> _name_to_id;
			std::unordered_map<std::string, std::string> _port_to_hier;
			std::unordered_map<unsigned, SignalInfo> _signalCache;

			// Watch state
			WatchFeature _watchFeature{};
			bool _watchRegistered = false;
			std::unordered_map<std::string, std::unique_ptr<WatchCookie>> _watchCookies;
	};
}
