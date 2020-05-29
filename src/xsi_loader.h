#ifndef _XSI_LOADER_H_
#define _XSI_LOADER_H_

#include "xsi.h"
#include "xsi_shared_lib.h"

#include <string>
#include <vector>
#include <exception>

namespace Xsi {

class LoaderException : public std::exception {
public:
    LoaderException(const std::string& msg) : _msg("ISim engine error: " + msg) { }

    virtual ~LoaderException() throw() { }

    virtual const char * what() const throw() { return _msg.c_str(); }
private:
    std::string _msg;
};

class Loader {

public:
    Loader(const std::string& dll_name, const std::string& simkernel_libname);
    ~Loader();

    // Initialize and close
    bool isopen() const;
    void open(p_xsi_setup_info setup_info);
    void close();

    // Control simulation
    void run(XSI_INT64 step);
    void restart();

    // Put value
    void put_value(int port_number, const void* value);

    // Read values
    int get_value(int port_number, void* value);
    int get_port_number(const char* port_name);
    int get_int_property_design(int property_type);
    int get_int_property_port(int port_number, int property_type);
    const char* get_str_property_port(int port_number, int property_type);
    int get_status();
    const char* get_error_info();
    void trace_all();


public:
    // Utility functions
    int num_ports() { return _xsi_value_buffer.size(); };
    void display_value(int port_number);
    void display_port_values();
    static std::string convert_std_logic_to_str(const char* value_buffer, int width);
    
private:
    bool initialize();

private:
    // Handles for the shared library and design
    Xsi::SharedLibrary _design_lib;
    Xsi::SharedLibrary _simkernel_lib;
    std::string _design_libname;
    std::string _simkernel_libname;
    xsiHandle _design_handle;

private:
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
    t_fp_xsi_get_int _xsi_get_int_property_design;
    t_fp_xsi_get_int_port _xsi_get_int_property_port;
    t_fp_xsi_get_str_port _xsi_get_str_property_port;
    t_fp_xsi_trace_all _xsi_trace_all;

private:

    // Buffer for printing value for each of the ports
    std::vector<char*> _xsi_value_buffer;


}; // class Loader

} // namespace Xsi

#endif // _XSI_LOADER_H_


