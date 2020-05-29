#include <pybind11/pybind11.h>
#include <unordered_map>
#include <algorithm>
#include "xsi_loader.h"
#include <iostream>

namespace py = pybind11;
using namespace std;

const char SLV_U=0;
const char SLV_X=1;
const char SLV_0=2;
const char SLV_1=3;
const char SLV_Z=4;
const char SLV_W=5;
const char SLV_L=6;
const char SLV_H=7;
const char SLV_DASH=8;

class XSI {
	public:
		XSI(
				const std::string &design_so,
				const std::string &simengine_so="librdi_simulator_kernel.so",
				const std::string &tracefile=std::string())
			:
				design_so(design_so),
				simengine_so(simengine_so),
       				tracefile(tracefile) {

			loader = new Xsi::Loader(design_so, simengine_so);
			memset(&info, 0, sizeof(info));

			if(!this->tracefile.empty())
				info.wdbFileName = (char *)this->tracefile.c_str();

			loader->open(&info);

			/* Keep a local mapping from port name (which we care about)
			 * to port index (which xsim cares about) */
			for(int i=0; i<loader->num_ports(); i++)
				port_map[loader->get_str_property_port(i, xsiNameTopPort)] = i;
		}
		~XSI() { delete loader; }

		void trace_all(void) { loader->trace_all(); }

		const int get_port_number(const std::string &name) { return port_map[name]; }

		const std::string get_port_value(const std::string port_name) {
			int port = port_map[port_name];
			std::string s(loader->get_int_property_port(port, xsiHDLValueSize), SLV_0);
			loader->get_value(port, &s.front());

			std::replace(s.begin(), s.end(), SLV_U, 'U');
			std::replace(s.begin(), s.end(), SLV_X, 'X');
			std::replace(s.begin(), s.end(), SLV_0, '0');
			std::replace(s.begin(), s.end(), SLV_1, '1');
			std::replace(s.begin(), s.end(), SLV_Z, 'Z');
			std::replace(s.begin(), s.end(), SLV_W, 'W');
			std::replace(s.begin(), s.end(), SLV_L, 'L');
			std::replace(s.begin(), s.end(), SLV_H, 'H');
			std::replace(s.begin(), s.end(), SLV_DASH, '-');

			return s;
		}

		void set_port_value(const std::string port_name, std::string value) {
			int port = port_map[port_name];
			std::string s = value;

			std::replace(s.begin(), s.end(), 'U', SLV_U);
			std::replace(s.begin(), s.end(), 'X', SLV_X);
			std::replace(s.begin(), s.end(), '0', SLV_0);
			std::replace(s.begin(), s.end(), '1', SLV_1);
			std::replace(s.begin(), s.end(), 'Z', SLV_Z);
			std::replace(s.begin(), s.end(), 'W', SLV_W);
			std::replace(s.begin(), s.end(), 'L', SLV_L);
			std::replace(s.begin(), s.end(), 'H', SLV_H);
			std::replace(s.begin(), s.end(), '-', SLV_DASH);

			loader->put_value(port, &s.front());
		}

		void run(int duration) { loader->run(duration); }

	private:
		Xsi::Loader *loader;
		s_xsi_setup_info info;

		std::string simengine_so;
		std::string design_so;
		std::string tracefile;

		std::unordered_map<std::string, int> port_map;
};


PYBIND11_MODULE(pyxsi, m) {
	py::class_<XSI>(m, "XSI")
		.def(py::init<const std::string &, const std::string &, const std::string &>(),
				py::arg("design_so"),
				py::arg("simengine_so")="librdi_simulator_kernel.so",
				py::arg("tracefile")="")
		.def("get_port_number", &XSI::get_port_number)
		.def("get_port_value", &XSI::get_port_value)
		.def("set_port_value", &XSI::set_port_value)
		.def("trace_all", &XSI::trace_all)
		.def("run", &XSI::run, py::arg("duration")=0);
}

