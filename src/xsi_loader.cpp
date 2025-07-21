#include <iostream>
#include <fmt/format.h>
#include "xsi_loader.h"

using namespace Xsi;

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
}
