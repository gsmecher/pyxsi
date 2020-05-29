#include <iostream>
#include "xsi_loader.h"

using namespace Xsi;

namespace {
    const char* std_logic_literal[]={"U", "X", "0", "1", "Z", "W", "L", "H", "-"};
    inline void issue_not_open_error() { std::cerr << "Design is not open. Cannot execute any XSI method.\n"; }
}




std::string Loader::convert_std_logic_to_str(const char* value_buffer, int width) {
    std::string res;
    for (int i = 0; i < width; i++) {
        if(value_buffer[i] >=0 && value_buffer[i] <=8) {
          res += std_logic_literal[value_buffer[i]];
        } else {
            res +="I";
        }
    }
    return res;
}

Loader::Loader(const std::string& design_libname, const std::string& simkernel_libname) :
    _design_libname(design_libname),
    _simkernel_libname(simkernel_libname),
    _design_handle(NULL),
    _xsi_open(NULL),
    _xsi_close(NULL),
    _xsi_run(NULL),
    _xsi_get_value(NULL),
    _xsi_put_value(NULL),
    _xsi_get_status(NULL),
    _xsi_get_error_info(NULL),
    _xsi_restart(NULL),
    _xsi_get_port_number(NULL),
    _xsi_get_int_property_design(NULL),
    _xsi_get_int_property_port(NULL),
    _xsi_get_str_property_port(NULL),
    _xsi_trace_all(NULL)

{

    if (!initialize()) {
        throw LoaderException("Failed to Load up XSI.");
    }
   
}

Loader::~Loader()
{
    close();
}

bool
Loader::isopen() const
{

    return (_design_handle != NULL);
}

void
Loader::open(p_xsi_setup_info setup_info)
{
    _design_handle = _xsi_open(setup_info);
    if(isopen()) {
        // Set number of ports
        XSI_INT32 num_ports = Loader::get_int_property_design(xsiNumTopPorts);
        // Allocate buffer for printing
        for(XSI_INT32 i=0; i<num_ports; ++i) {
            XSI_INT32 port_value_size = get_int_property_port(i, xsiHDLValueSize);
            _xsi_value_buffer.push_back(new char[port_value_size]);
        }
    } else {
        std::cerr << "Loading of design failed\n";
    }

}

void
Loader::close()
{
    int size = _xsi_value_buffer.size();
    // Free up buffer for printing
    for(XSI_INT32 i=0; i<size; ++i) {
        delete[] _xsi_value_buffer[i];
    }

    if (_design_handle) {
        _xsi_close(_design_handle);
        _design_handle = NULL;
    }
}

void
Loader::run(XSI_INT64 step)
{
    if(!isopen()) {
        issue_not_open_error();
        return;
    }
    _xsi_run(_design_handle, step);
}

void
Loader::restart()
{
    if(!isopen()) {
        issue_not_open_error();
        return;
    }
    _xsi_restart(_design_handle);
}

int
Loader::get_value(int port_number, void* value)
{
    _xsi_get_value(_design_handle, port_number, value);
    return get_status();
}

int
Loader::get_port_number(const char* port_name)
{
    return _xsi_get_port_number(_design_handle, port_name);
}

void
Loader::put_value(int port_number, const void* value)
{
    _xsi_put_value(_design_handle, port_number, const_cast<void*>(value));
}

int
Loader::get_status()
{
    return _xsi_get_status(_design_handle);
}

const char*
Loader::get_error_info()
{
    return _xsi_get_error_info(_design_handle);
}

void
Loader::trace_all()
{
    _xsi_trace_all(_design_handle);
}

int 
Loader::get_int_property_design(int property_type)
{
    return _xsi_get_int_property_design(_design_handle, property_type);
}

int 
Loader::get_int_property_port(int port_number, int property_type)
{
    return _xsi_get_int_property_port(_design_handle, port_number, property_type);
}

const char* 
Loader::get_str_property_port(int port_number, int property_type)
{
    return _xsi_get_str_property_port(_design_handle, port_number, property_type);
}

void
Loader::display_value(int port_number)
{
    if(port_number >= _xsi_value_buffer.size() || port_number < 0) {
        std::cerr << "The port number " << port_number << " is invalid. Valid port number is in the range 0 to " << _xsi_value_buffer.size() << std::endl;
        return ;
    }
    std::string str;
    int status = get_value(port_number, _xsi_value_buffer[port_number]);
    if(status == xsiNormal) {
        XSI_INT32 port_value_size = get_int_property_port(port_number, xsiHDLValueSize);
        str = convert_std_logic_to_str(_xsi_value_buffer[port_number], port_value_size);
        std::cout << "Port " << get_str_property_port(port_number, xsiNameTopPort) << " : " << str << std::endl;
    } else {
        std::cerr << "Could not print value as design is in error state. Status: " << status << " Error: " << get_error_info() << std::endl;
    }
    return;
}

void
Loader::display_port_values()
{
     XSI_INT32 count = num_ports();

     for(XSI_INT32 i=0; i<count; ++i) {
         display_value(i);
     }
}

bool
Loader::initialize()
{
   
    // Load ISIM design shared library
    if (!_design_lib.load(_design_libname)) {
        std::cerr << "Could not load XSI simulation shared library (" << _design_libname <<"): " << _design_lib.error() << std::endl;
        return false;
    }

   
    if (!_simkernel_lib.load(_simkernel_libname)) {
        std::cerr << "Could not load simulaiton kernel library (" << _simkernel_libname << ") :" << _simkernel_lib.error() << "\n";
        return false;
    }

    // Get function pointer for getting an ISIM design handle
    _xsi_open = (t_fp_xsi_open) _design_lib.getfunction("xsi_open");
    if (!_xsi_open) {
        return false;
    }


    // Get function pointer for running ISIM simulation
    _xsi_run = (t_fp_xsi_run) _simkernel_lib.getfunction("xsi_run");
    if (!_xsi_run) {
        return false;
    }

    // Get function pointer for terminating ISIM simulation
    _xsi_close = (t_fp_xsi_close) _simkernel_lib.getfunction("xsi_close");
    if (!_xsi_close) {
        return false;
    }

    // Get function pointer for running ISIM simulation
    _xsi_restart = (t_fp_xsi_restart) _simkernel_lib.getfunction("xsi_restart");
    if (!_xsi_restart) {
        return false;
    }

    // Get function pointer for reading data from ISIM
    _xsi_get_value = (t_fp_xsi_get_value) _simkernel_lib.getfunction("xsi_get_value");
    if (!_xsi_get_value) {
        return false;
    }

    // Get function pointer for reading data from ISIM
    _xsi_get_port_number = (t_fp_xsi_get_port_number) _simkernel_lib.getfunction("xsi_get_port_number");
    if (!_xsi_get_port_number) {
        return false;
    }
    // Get function pointer for passing data to ISIM
    _xsi_put_value = (t_fp_xsi_put_value) _simkernel_lib.getfunction("xsi_put_value");
    if (!_xsi_put_value) {
        return false;
    }

    // Get function pointer for checking error status
    _xsi_get_status = (t_fp_xsi_get_status) _simkernel_lib.getfunction("xsi_get_status");
    if (!_xsi_get_status) {
        return false;
    }

    // Get function pointer for getting error message
    _xsi_get_error_info = (t_fp_xsi_get_error_info) _simkernel_lib.getfunction("xsi_get_error_info");
    if (!_xsi_get_error_info) {
        return false;
    }

    // Get function pointer for getting error message
    _xsi_get_int_property_design = (t_fp_xsi_get_int) _simkernel_lib.getfunction("xsi_get_int");
    if (!_xsi_get_int_property_design) {
        return false;
    }

    // Get function pointer for getting error message
    _xsi_get_int_property_port = (t_fp_xsi_get_int_port) _simkernel_lib.getfunction("xsi_get_int_port");
    if (!_xsi_get_int_property_port) {
        return false;
    }

    // Get function pointer for getting error message
    _xsi_get_str_property_port = (t_fp_xsi_get_str_port) _simkernel_lib.getfunction("xsi_get_str_port");
    if (!_xsi_get_str_property_port) {
        return false;
    }

    // Get function pointer for tracing all signals to WDB
    _xsi_trace_all = (t_fp_xsi_trace_all) _simkernel_lib.getfunction("xsi_trace_all");
    if (!_xsi_trace_all) {
        return false;
    }

    return true;
}


