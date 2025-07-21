#pragma once

#include "xsi.h"
#include <dlfcn.h>

#include <string>
#include <vector>
#include <stdexcept>

namespace Xsi {
	class Loader {
		public:
			Loader(const std::string& dll_name, const std::string& simkernel_libname);
			~Loader() {
				close();
				dlclose(design);
				dlclose(simkernel);
			}

			// Initialize and close
			bool isopen() const { return !!_design_handle; }
			void open(p_xsi_setup_info setup_info){
				_design_handle = _xsi_open(setup_info);
				if(isopen()) {
					// Set number of ports
					XSI_INT32 num_ports = get_int(xsiNumTopPorts);
					// Allocate buffer for printing
					for(XSI_INT32 i=0; i<num_ports; ++i) {
						XSI_INT32 port_value_size = get_int_port(i, xsiHDLValueSize);
						_xsi_value_buffer.push_back(new char[port_value_size]);
					}
				} else
					throw std::runtime_error("Failed to open design!");
			}

			void close() {
				int size = _xsi_value_buffer.size();
				for(XSI_INT32 i=0; i<size; ++i)
					delete[] _xsi_value_buffer[i];

				if (_design_handle) {
					_xsi_close(_design_handle);
					_design_handle = NULL;
				}
			}

			// Control simulation
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

			// Put value
			void put_value(int port_number, const void* value){
				_xsi_put_value(_design_handle, port_number, const_cast<void*>(value));
			}

			// Read values
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

		public:
			int num_ports() { return _xsi_value_buffer.size(); };

		private:
			void *design, *simkernel;

			// Handles for the shared library and design
			std::string _design_libname;
			std::string _simkernel_libname;
			xsiHandle _design_handle;

			// Addresses of the XSI functions
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

			// Buffer for printing value for each of the ports
			std::vector<char*> _xsi_value_buffer;
	};
}
