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
				const std::string &simengine_so="librdi_simulator_kernel.so",
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

		void close() {
			loader->close();
		}

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
			return loader->get_str_property_port(index, xsiNameTopPort);
		}

		const Language get_language() const {
			return language;
		}

		const std::string get_port_value(std::string const& port_name) {
			auto const& [port, length, direction] = port_map.at(port_name);

			switch(language) {
				case Language::VERILOG: {
					auto logicval = std::vector<s_xsi_vlog_logicval>(roundup_int_div(length, 32));
					loader->get_value(port, logicval.data());
					std::string ret = logic_val_to_string(logicval.data(), length);
					return ret;
				}

				case Language::VHDL: {
					/* Create a vector of chars and receive the value into it
					* (get_value does not allocate space) */
					std::string s(length, '0');
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
					auto logicval = std::vector<s_xsi_vlog_logicval>(roundup_int_div(length, 32));
					string_to_logic_val(value, logicval.data());
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

		const size_t roundup_int_div(const size_t dividend, const size_t divisor) {
			return (dividend + divisor - 1) / divisor;
		}

		void clear_bit(XSI_UINT32 &container, size_t ind) {
			container = container & ~((XSI_UINT32)1 << ind);
		}

		void set_bit(XSI_UINT32 &container, size_t ind) {
			container = container | ((XSI_UINT32)1 << ind);
		}

		bool test_bit(XSI_UINT32 &container, size_t ind) {
			return ((container & ((XSI_UINT32)1 << ind)) > 0 ? true : false);
		}

		void set_logic_val_at_ind(s_xsi_vlog_logicval &logicval, size_t ind, char val) {
			switch(val) {
				case '0':
					clear_bit((logicval.aVal), ind);
					clear_bit((logicval.bVal), ind);
					break;
				case '1':
					set_bit((logicval.aVal), ind);
					clear_bit((logicval.bVal), ind);
					break;
				case 'X':
					set_bit((logicval.aVal), ind);
					set_bit((logicval.bVal), ind);
					break;
				case 'Z':
					clear_bit((logicval.aVal), ind);
					set_bit((logicval.bVal), ind);
					break;
				default:
					throw std::runtime_error("Unrecognized value for set_logic_val_at_ind: "+val);
			}
		}

		void string_to_logic_val(std::string str, p_xsi_vlog_logicval value) {
			size_t str_len = str.length();
			size_t num_words = roundup_int_div(str_len, 32);
			memset(value, 0, sizeof(s_xsi_vlog_logicval)*num_words);
			for(size_t i = 0; i < str_len; i++) {
				size_t array_ind = i / 32;
				size_t bit_ind = i % 32;
				set_logic_val_at_ind(value[array_ind], bit_ind, str[str_len-i-1]);
			}
		}

		std::string logic_val_to_string(p_xsi_vlog_logicval value, const size_t n_bits) {
			std::string ret(n_bits, '?');
			for(size_t i = 0; i < n_bits; i++) {
				size_t array_ind = i / 32;
				size_t bit_ind = i % 32;
				bool is_set_aVal = test_bit(value[array_ind].aVal, bit_ind);
				bool is_set_bVal = test_bit(value[array_ind].bVal, bit_ind);
				if(!is_set_aVal && !is_set_bVal) {
					ret[n_bits-i-1] = '0';
				} else if(is_set_aVal && !is_set_bVal) {
					ret[n_bits-i-1] = '1';
				} else if(!is_set_aVal && is_set_bVal) {
					ret[n_bits-i-1] = 'X';
				} else {
					ret[n_bits-i-1] = 'Z';
				}
			}
			return ret;
		}
};

PYBIND11_MODULE(pyxsi, m) {
	py::enum_<Language>(m, "Language")
		.value("VHDL", Language::VHDL)
		.value("VERILOG", Language::VERILOG)
		.export_values();

	py::class_<XSI>(m, "XSI")
		.def(py::init<std::string const&, std::string const&, Language const&, std::optional<std::string> const&, std::optional<std::string> const&>(),
				py::arg("design_so"),
				py::arg("simengine_so")="librdi_simulator_kernel.so",
				py::arg("language")=Language::VHDL,
				py::arg("tracefile")=std::nullopt,
				py::arg("logfile")=std::nullopt)

		.def("get_port_value", &XSI::get_port_value)
		.def("set_port_value", &XSI::set_port_value)
		.def("get_port_count", &XSI::get_port_count)
		.def("get_port_name", &XSI::get_port_name)
		.def("close", &XSI::close)
		.def("restart", &XSI::restart)
		.def("get_status", &XSI::get_status)
		.def("get_error_info", &XSI::get_error_info)
		.def("get_language", &XSI::get_language)
		.def("run", &XSI::run, py::arg("duration")=0);
}

