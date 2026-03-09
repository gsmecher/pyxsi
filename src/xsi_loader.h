#pragma once

#include "xsi.h"
#include <dlfcn.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <cstdint>
#include <cstring>

namespace Xsi {
	class Loader {
		public:
			Loader(const std::string& dll_name, const std::string& simkernel_libname);
			~Loader() {
				close();
				dlclose(design);
				dlclose(simkernel);
			}

			bool isopen() const { return !!_design_handle; }
			void open(p_xsi_setup_info setup_info){
				_design_handle = _xsi_open(setup_info);
				if(isopen()) {
					_num_ports = get_int(xsiNumTopPorts);
				} else
					throw std::runtime_error("Failed to open design!");
			}

			void close() {
				if(_dbg && _dbgManagerDtor) {
					_dbgManagerDtor(_dbg);
					free(_dbg);
					_dbg = nullptr;
				}

				if (_design_handle) {
					_xsi_close(_design_handle);
					_design_handle = NULL;
				}
			}

			void run(XSI_INT64 step) {
				if(!isopen())
					throw std::runtime_error("Design not open! Can't execute XSI method.");
				_xsi_run(_design_handle, step);
			}

			void restart() {
				if(!isopen())
					throw std::runtime_error("Design not open! Can't execute XSI method.");
				_xsi_restart(_design_handle);
			}

			void put_value(int port_number, const void* value){
				_xsi_put_value(_design_handle, port_number, const_cast<void*>(value));
			}

			int get_value(int port_number, void* value) {
				_xsi_get_value(_design_handle, port_number, value);
				return get_status();
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

			// Hierarchy — call init_hierarchy() after open(), before using signals
			void init_hierarchy();
			std::string get_signal_value(const std::string &name);
			void set_signal_value(const std::string &name, const std::string &value);
			void set_signal_value(const std::string &name, uint64_t value);
			std::vector<std::string> list_signals();

		private:
			void *design, *simkernel;

			std::string _design_libname;
			std::string _simkernel_libname;
			xsiHandle _design_handle;
			int _num_ports = 0;

			// XSI function pointers (resolved from design/simkernel .so)
			t_fp_xsi_open _xsi_open;
			t_fp_xsi_close _xsi_close;
			t_fp_xsi_run _xsi_run;
			t_fp_xsi_get_value _xsi_get_value;
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
			using fn_isPortVHDL = bool(*)(void*, int);
			using fn_getObjectInfo = void*(*)(const void *dbg, unsigned id);
			using fn_getObjectLongName = std::string*(*)(std::string *result, const void *dbg, unsigned id);
			using fn_getCommonObjectInfo = void*(*)(const void *dbg, const void *objInfo);
			using fn_setHdlValueObject = void(*)(const void *dbg, void *hdlObj, const void *objInfo);
			using fn_hasDbgImage = bool(*)(const void *dbg);
			using fn_readDbgFile = void(*)(void *dbg, const char *path);
			using fn_dbgManagerCtor = void(*)(void *dbg, const std::string &path);
			using fn_dbgManagerDtor = void(*)(void *dbg);
			using fn_getScopeInfo = void*(*)(const void *dbg, unsigned id);
			using fn_getScopeCommonInfo = void*(*)(const void *dbg, const void *scopeInfo);
			using fn_getValue = void(*)(void *uas, const void *hdlObj,
				unsigned char *buf, unsigned *outSize,
				unsigned offset, unsigned count,
				void *p1, void *p2, void *p3, void *p4, void *p5, bool *p6);

			fn_isPortVHDL _isPortVHDL = nullptr;
			fn_getObjectInfo _getObjectInfo = nullptr;
			fn_getObjectLongName _getObjectLongName = nullptr;
			fn_getCommonObjectInfo _getCommonObjectInfo = nullptr;
			fn_setHdlValueObject _setHdlValueObject = nullptr;
			fn_hasDbgImage _hasDbgImage = nullptr;
			fn_readDbgFile _readDbgFile = nullptr;
			fn_dbgManagerCtor _dbgManagerCtor = nullptr;
			fn_dbgManagerDtor _dbgManagerDtor = nullptr;
			fn_getScopeInfo _getScopeInfo = nullptr;
			fn_getScopeCommonInfo _getScopeCommonInfo = nullptr;
			fn_getValue _getValue = nullptr;

			int resolve_port(const std::string &name);
			void enumerate_scope(unsigned scope_id);
			std::string _read_hier_signal(unsigned id);

			void *_dbg = nullptr;
			void *_uas = nullptr;

			std::unordered_map<std::string, unsigned> _name_to_id;
			std::unordered_map<std::string, std::string> _port_to_hier;
	};
}
