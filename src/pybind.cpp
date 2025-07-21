#include <unordered_map>
#include <algorithm>
#include <optional>
#include <iostream>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#define FMT_HEADER_ONLY
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
enum class Language {VHDL, VERILOG};

class XSI {
	public:
		XSI(
				const std::string &design_so,
				const std::string &simengine_so="libxv_simulator_kernel.so",
				const Language language=Language::VHDL,
				const std::optional<std::string> &tracefile=std::nullopt,
				const std::optional<std::string> &logfile=std::nullopt)
			:
				design_so(design_so),
				simengine_so(simengine_so),
				language(language),
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
				int length = loader->get_int_port(i, xsiHDLValueSize);
				std::string port = loader->get_str_port(i, xsiNameTopPort);
				PortDirection d;

				switch(loader->get_int_port(i, xsiDirectionTopPort)) {
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

		const Language get_language() const {
			return language;
		}

		const std::string get_port_value(std::string const& port_name) {
			auto const& [port, length, direction] = port_map.at(port_name);
			std::string s(length, '0');

			switch(language) {
				case Language::VERILOG: {
					auto logicval = std::vector<s_xsi_vlog_logicval>((length+31)/32, {0, 0});
					loader->get_value(port, logicval.data());

					for(size_t n=0; n<length; n++) {
						int aVal = (logicval[n/32].aVal >> (n&31)) & 1u;
						int bVal = (logicval[n/32].bVal >> (n&31)) & 1u;

						switch((bVal << 1) | aVal) {
							case 0b00: s[length-1-n] = '0'; break;
							case 0b01: s[length-1-n] = '1'; break;
							case 0b10: s[length-1-n] = 'Z'; break;
							case 0b11: s[length-1-n] = 'X'; break;
						}
					}
					return s;
				}

				case Language::VHDL: {
					loader->get_value(port, s.data());
					/* Transform into a string we can sanely deal with */
					std::transform(std::begin(s), std::end(s), std::begin(s), [](auto const& ch) {
						switch(ch) {
							case SLV_0: return '0';
							case SLV_1: return '1';
							case SLV_U: return 'U';
							case SLV_X: return 'X';
							case SLV_Z: return 'Z';
							case SLV_W: return 'W';
							case SLV_L: return 'L';
							case SLV_H: return 'H';
							case SLV_DASH: return '-';
							default: throw std::runtime_error("Unexpected logic value!");
						}});
					return s;
				}

				default:
					throw std::runtime_error("Unknown top-level language!");
			}
		}

		void set_port_value(std::string const& port_name, std::string value) {
			auto const& [port, length, direction] = port_map.at(port_name);

			if(length != value.length())
				throw std::invalid_argument("Length of vector didn't match length of port!");

			switch(language) {
				case Language::VERILOG: {
					/* Verilog simulator uses a pair of bit fields to encode 4-state variables */
					auto logicval = std::vector<s_xsi_vlog_logicval>((length+31)/32, {0, 0});
					for(size_t n=0; n<length; n++) {
						switch(value[length-1-n]) {
							/* Per UG900 Table 64 */
							case '0':
								break; /* nop, already 0 */
							case '1':
								logicval[n/32].aVal |= 1u<<(n&31u);
								break;
							case 'Z':
								logicval[n/32].bVal |= 1u<<(n&31u);
								break;
							case 'X':
								logicval[n/32].aVal |= 1u<<(n&31u);
								logicval[n/32].bVal |= 1u<<(n&31u);
								break;
						}
					}
					loader->put_value(port, logicval.data());
					break;
				}

				case Language::VHDL: {
					std::transform(std::begin(value), std::end(value), std::begin(value), [](auto const& ch) {
					switch(ch) {
						case '0': return SLV_0;
						case '1': return SLV_1;
						case 'U': return SLV_U;
						case 'X': return SLV_X;
						case 'Z': return SLV_Z;
						case 'W': return SLV_W;
						case 'L': return SLV_L;
						case 'H': return SLV_H;
						case '-': return SLV_DASH;
						default: throw std::runtime_error("Unexpected logic value!");
					}});
					loader->put_value(port, value.data());
					break;
				}
				default:
					throw std::runtime_error("Unknown top-level language!");
			}
		}

	private:
		std::unique_ptr<Xsi::Loader> loader;
		s_xsi_setup_info info;

		const std::string design_so;
		const std::string simengine_so;
		const Language language;
		const std::optional<std::string> tracefile;
		const std::optional<std::string> logfile;

		/* (id, length, direction) tuple */
		std::unordered_map<std::string, std::tuple<int, size_t, PortDirection>> port_map;
};

PYBIND11_MODULE(pyxsi, m) {
	py::enum_<Language>(m, "Language")
		.value("VHDL", Language::VHDL)
		.value("VERILOG", Language::VERILOG)
		.export_values();

	py::class_<XSI>(m, "XSI")
		.def(py::init<std::string const&, std::string const&, Language const&, std::optional<std::string> const&, std::optional<std::string> const&>(),
				py::arg("design_so"),
				py::arg("simengine_so")=SIMENGINE_SO, /* see Makefile */
				py::arg("language")=Language::VHDL,
				py::arg("tracefile")=std::nullopt,
				py::arg("logfile")=std::nullopt)

		.def("get_port_value", &XSI::get_port_value)
		.def("set_port_value", &XSI::set_port_value)
		.def("get_port_count", &XSI::get_port_count)
		.def("get_port_name", &XSI::get_port_name)
		.def("restart", &XSI::restart)
		.def("get_status", &XSI::get_status)
		.def("get_error_info", &XSI::get_error_info)
		.def("get_language", &XSI::get_language)
		.def("run", &XSI::run, py::arg("duration")=0);
}

