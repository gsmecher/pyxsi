#include <optional>
#include <iostream>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "xsi_loader.h"

namespace py = pybind11;
using namespace std;

class XSI {
	public:
		XSI(
				const std::string &design_so,
				const std::string &simengine_so="libxv_simulator_kernel.so",
				const std::optional<std::string> &tracefile=std::nullopt,
				const std::optional<std::string> &logfile=std::nullopt)
			:
				design_so(design_so),
				simengine_so(simengine_so),
				tracefile(tracefile),
				logfile(logfile)
		{
			loader = std::make_unique<Xsi::Loader>(design_so, simengine_so);
			if(!loader)
				throw std::runtime_error("Unable to create simulator kernel!");

			memset(&info, 0, sizeof(info));

			if(tracefile)
				info.wdbFileName = (char *)this->tracefile->c_str();
			if(logfile)
				info.logFileName = (char *)this->logfile->c_str();

			loader->open(&info);

			loader->init_hierarchy();

			if(tracefile)
				loader->trace_all();

			// Initialize all input ports to 0
			for(int i = 0; i < loader->num_ports(); i++) {
				if(loader->get_port_direction(i) == xsiInputPort) {
					int length = loader->get_port_length(i);
					loader->set_signal_value(
						loader->get_port_name(i), std::string(length, '0'));
				}
			}
		}

		virtual ~XSI() = default;

		void restart() {
			loader->restart();
		}

		const int get_status() {
			return loader->get_status();
		}

		const std::string get_error_info() {
			return loader->get_error_info();
		}

		void run(int const& duration) {
			loader->run(duration);
		}

		const int get_port_count() const {
			return loader->num_ports();
		}

		const std::string get_port_name(int index) const {
			return loader->get_str_port(index, xsiNameTopPort);
		}

		std::string get_value(const std::string &name) {
			return loader->get_signal_value(name);
		}

		void set_value_str(const std::string &name, const std::string &value) {
			loader->set_signal_value(name, value);
		}

		void set_value_int(const std::string &name, int64_t value) {
			loader->set_signal_value(name, static_cast<uint64_t>(value));
		}

		std::vector<std::string> list_signals() {
			return loader->list_signals();
		}

	private:
		std::unique_ptr<Xsi::Loader> loader;
		s_xsi_setup_info info;

		const std::string design_so;
		const std::string simengine_so;
		const std::optional<std::string> tracefile;
		const std::optional<std::string> logfile;
};

PYBIND11_MODULE(pyxsi, m) {
	py::class_<XSI>(m, "XSI")
		.def(py::init<std::string const&, std::string const&, std::optional<std::string> const&, std::optional<std::string> const&>(),
				py::arg("design_so"),
				py::arg("simengine_so")=SIMENGINE_SO, /* see Makefile */
				py::arg("tracefile")=std::nullopt,
				py::arg("logfile")=std::nullopt)

		.def("get_value", &XSI::get_value)
		.def("set_value", &XSI::set_value_str)
		.def("set_value", &XSI::set_value_int)
		.def("get_port_count", &XSI::get_port_count)
		.def("get_port_name", &XSI::get_port_name)
		.def("restart", &XSI::restart)
		.def("get_status", &XSI::get_status)
		.def("get_error_info", &XSI::get_error_info)
		.def("list_signals", &XSI::list_signals)
		.def("run", &XSI::run, py::arg("duration")=0,
			py::call_guard<py::gil_scoped_release>());
}
