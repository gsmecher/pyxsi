#include <unordered_map>
#include <algorithm>
#include <pybind11/pybind11.h>
#include <fmt/format.h>

#include "xsi_loader.h"

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

enum class PortDirection {INPUT, OUTPUT, INOUT};

class XSI {
	public:
		XSI(
				const std::string &design_so,
				const std::string &simengine_so="librdi_simulator_kernel.so",
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

			if(tracefile)
				loader->trace_all();

			/* Keep a local mapping from port name (which we care about)
			 * to a tuple specifying (port index, length). */
			for(int i=0; i<loader->num_ports(); i++) {
				int length = loader->get_int_property_port(i, xsiHDLValueSize);
				std::string port = loader->get_str_property_port(i, xsiNameTopPort);
				PortDirection d;

				switch(loader->get_int_property_port(i, xsiDirectionTopPort)) {
					case xsiInputPort: d = PortDirection::INPUT; break;
					case xsiOutputPort: d = PortDirection::OUTPUT; break;
					case xsiInoutPort: d = PortDirection::INOUT; break;
					default: throw runtime_error(fmt::format("Unexpected port direction for '{}'.", port));
				}

				port_map[port] = std::make_tuple(i, length, d);
			}

			/* Initialize all input ports to 0 */
			for(const auto& [port, entry] : port_map) {
				const auto& [id, length, direction] = entry;
				if(direction == PortDirection::INPUT)
					set_port_value(port, std::string(length, '0'));
			}
		}

		virtual ~XSI() = default;

		void run(int const& duration) {
			loader->run(duration);
		}

		const std::string get_port_value(std::string const& port_name) const {
			auto [port, length, direction] = port_map.at(port_name);

			/* Create a vector of chars and receive the value into it
			 * (get_value does not allocate space) */
			std::string s(length, '0');
			loader->get_value(port, s.data());

			/* Transform into a string we can sanely deal with */
			std::transform(std::begin(s), std::end(s), std::begin(s), [](auto const& ch) {
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
					default: return 'X';
				}});
			return s;
		}

		void set_port_value(std::string const& port_name, std::string value) {
			auto [port, length, direction] = port_map.at(port_name);

			if(length != value.length())
				throw std::invalid_argument("Length of vector didn't match length of port!");

			std::transform(std::begin(value), std::end(value), std::begin(value), [](auto const& ch) {
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
					default: return SLV_X;
				}});

			loader->put_value(port, value.data());
		}

	private:
		std::unique_ptr<Xsi::Loader> loader;
		s_xsi_setup_info info;

		const std::string design_so;
		const std::string simengine_so;
		const std::optional<std::string> tracefile;
		const std::optional<std::string> logfile;

		/* (id, length, direction) tuple */
		std::unordered_map<std::string, std::tuple<int, size_t, PortDirection>> port_map;
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
		.def("run", &XSI::run, py::arg("duration")=0);
}

