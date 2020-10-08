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
				const std::string &tracefile="xsim.wdb",
				const std::string &logfile="xsim.log")
			:
				design_so(design_so),
				simengine_so(simengine_so),
				tracefile(tracefile),
				logfile(logfile)
		{
                    cout << "Got tracefile '" << tracefile << "', logfile '" << logfile << "'" << endl << std::flush;
			loader = new Xsi::Loader(design_so, simengine_so);
			memset(&info, 0, sizeof(info));

			info.wdbFileName = (char *)this->tracefile.c_str();
			info.logFileName = (char *)this->logfile.c_str();

			loader->open(&info);

			/* Keep a local mapping from port name (which we care about)
			 * to a tuple specifying (port index, length). */
			for(int i=0; i<loader->num_ports(); i++)
				port_map[loader->get_str_property_port(i, xsiNameTopPort)] = std::make_tuple(
						i,
						loader->get_int_property_port(i, xsiHDLValueSize));
		}
		~XSI() { delete loader; }

		void trace_all(void) { loader->trace_all(); }
		void run(int duration) {
			loader->run(duration);
			if(loader->get_status() != xsiNormal)
				throw std::runtime_error(loader->get_error_info());
		}

		const std::string get_port_value(const std::string port_name) {
			auto [port, length] = port_map.at(port_name);

			/* Create a vector of chars and receive the value into it
			 * (get_value does not allocate space) */
			std::vector<char> value(length);
			if(loader->get_value(port, value.data()) != xsiNormal)
				throw std::runtime_error(loader->get_error_info());

			/* Transform into a string we can sanely deal with */
			std::string s(value.data(), length);
			std::transform(std::begin(s), std::end(s), std::begin(s), [](auto ch) {
				switch(ch) {
					case SLV_U: return 'U';
					case SLV_X: return 'X';
					case SLV_0: return '0';
					case SLV_1: return '1';
					case SLV_Z: return 'Z';
					case SLV_W: return 'W';
					case SLV_L: return 'L';
					case SLV_H: return 'H';
					case SLV_DASH: return '-';
					default: throw std::runtime_error("Unexpected value from XSI!");
				}});
			return s;
		}

		void set_port_value(const std::string port_name, std::string value) {
			auto [port, length] = port_map.at(port_name);

			if(length != value.length())
				throw std::invalid_argument("Length of vector didn't match length of port!");

			std::transform(std::begin(value), std::end(value), std::begin(value), [](auto ch) {
				switch(ch) {
					case 'U': return SLV_U;
					case 'X': return SLV_X;
					case '0': return SLV_0;
					case '1': return SLV_1;
					case 'Z': return SLV_Z;
					case 'W': return SLV_W;
					case 'L': return SLV_L;
					case 'H': return SLV_H;
					case '-': return SLV_DASH;
					default: throw std::runtime_error("Unexpected value for XSI!");
				}});
			loader->put_value(port, value.data());
		}

	private:
		Xsi::Loader *loader;
		s_xsi_setup_info info;

		const std::string design_so;
		const std::string simengine_so;
		const std::string tracefile;
		const std::string logfile;

		std::unordered_map<std::string, std::tuple<int, size_t>> port_map; /* (id, length) tuple */
};

PYBIND11_MODULE(pyxsi, m) {
	py::class_<XSI>(m, "XSI")
		.def(py::init<const std::string &, const std::string &, const std::string &, const std::string &>(),
				py::arg("design_so"),
				py::arg("simengine_so")="librdi_simulator_kernel.so",
				py::arg("tracefile")="xsim.wdb",
                                py::arg("logfile")="xsim.log")

		.def("get_port_value", &XSI::get_port_value)
		.def("set_port_value", &XSI::set_port_value)
		.def("trace_all", &XSI::trace_all)
		.def("run", &XSI::run, py::arg("duration")=0);
}

