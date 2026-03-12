#include <optional>
#include <algorithm>
#include <cmath>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "xsi_loader.h"

namespace py = pybind11;
using namespace std;

// ---------------------------------------------------------------------------
// Type tag sentinels: pyxsi.Signed, pyxsi.Unsigned, pyxsi.Vector
// These are empty classes used as type tags for sim_type().
// ---------------------------------------------------------------------------
struct TagSigned {};
struct TagUnsigned {};
struct TagVector {};

// ---------------------------------------------------------------------------
// Helper: convert a bit string to a Python object based on SignalType.
// For int-typed signals (SIGNED_INT, UNSIGNED_INT), attempts to parse as
// integer. If the bit string contains non-0/1 characters (X, U, Z, etc.),
// falls back to returning the raw bit string.
// ---------------------------------------------------------------------------
static py::object bits_to_python(const std::string &bits, Xsi::SignalType type) {
	switch(type) {
		case Xsi::SignalType::SIGNED_INT:
		case Xsi::SignalType::UNSIGNED_INT: {
			// Check for non-numeric bits -- fall back to str
			if(!std::all_of(bits.begin(), bits.end(),
					[](char c){ return c == '0' || c == '1'; }))
				return py::str(bits);

			// Use PyLong_FromString for arbitrary-width int
			std::string prefixed = "0b" + bits;
			py::object val = py::reinterpret_steal<py::object>(
				PyLong_FromString(prefixed.c_str(), nullptr, 0));
			if(!val)
				throw py::error_already_set();

			if(type == Xsi::SignalType::SIGNED_INT && !bits.empty() && bits[0] == '1') {
				// Two's complement: subtract 2^width
				std::string pow_str = "0b1" + std::string(bits.size(), '0');
				py::object pow = py::reinterpret_steal<py::object>(
					PyLong_FromString(pow_str.c_str(), nullptr, 0));
				if(!pow)
					throw py::error_already_set();
				val = val - pow;
			}
			return val;
		}

		case Xsi::SignalType::LOGIC:
		case Xsi::SignalType::VECTOR:
		default:
			return py::str(bits);
	}
}

// ---------------------------------------------------------------------------
// Helper: convert a Python value to arguments for set_signal_value,
// based on the signal's type and width.
// ---------------------------------------------------------------------------
static void python_to_signal(Xsi::Loader &loader, const std::string &name,
		py::handle value, Xsi::SignalType type) {
	switch(type) {
		case Xsi::SignalType::LOGIC: {
			// std_logic: accept int 0/1 or single-char str
			if(py::isinstance<py::int_>(value)) {
				long v = value.cast<long>();
				if(v != 0 && v != 1)
					throw py::value_error(
						"std_logic accepts int 0 or 1, got " + std::to_string(v));
				loader.set_signal_value(name, static_cast<uint64_t>(v));
			} else if(py::isinstance<py::str>(value)) {
				loader.set_signal_value(name, value.cast<std::string>());
			} else {
				throw py::type_error(
					"std_logic accepts int (0/1) or str, got "
					+ std::string(py::str(value.get_type())));
			}
			return;
		}

		case Xsi::SignalType::SIGNED_INT:
		case Xsi::SignalType::UNSIGNED_INT: {
			if(!py::isinstance<py::int_>(value))
				throw py::type_error(
					"Integer-typed signal requires int, got "
					+ std::string(py::str(value.get_type())));

			int width = loader.get_signal_width(name);
			py::object py_val = py::reinterpret_borrow<py::object>(value);

			// For signed values, convert to unsigned representation
			if(type == Xsi::SignalType::SIGNED_INT) {
				// mask = 2^width - 1 (all-ones bit string of given width)
				std::string mask_str = "0b" + std::string(width, '1');
				py::object mask = py::reinterpret_steal<py::object>(
					PyLong_FromString(mask_str.c_str(), nullptr, 0));
				if(!mask)
					throw py::error_already_set();
				py_val = py_val & mask;
			}

			// format(val, '0{width}b')
			py::str fmt_spec(std::string(1, '0') + std::to_string(width) + "b");
			py::object bit_str = py::module_::import("builtins")
				.attr("format")(py_val, fmt_spec);
			loader.set_signal_value(name, bit_str.cast<std::string>());
			return;
		}

		case Xsi::SignalType::VECTOR:
		default: {
			if(py::isinstance<py::str>(value)) {
				loader.set_signal_value(name, value.cast<std::string>());
			} else if(py::isinstance<py::int_>(value)) {
				// Accept int for bit-string signals: format as binary
				int width = loader.get_signal_width(name);
				py::str fmt_spec(std::string(1, '0') + std::to_string(width) + "b");
				py::object bit_str = py::module_::import("builtins")
					.attr("format")(value, fmt_spec);
				loader.set_signal_value(name, bit_str.cast<std::string>());
			} else {
				throw py::type_error(
					"Vector signal requires str or int, got "
					+ std::string(py::str(value.get_type())));
			}
			return;
		}
	}
}

// ---------------------------------------------------------------------------
// Helper: look up the effective SignalType for a name, checking overrides
// first, then falling back to the loader's auto-classification.
// ---------------------------------------------------------------------------
static Xsi::SignalType effective_type(
		Xsi::Loader &loader, const std::string &name,
		const std::unordered_map<std::string, Xsi::SignalType> &overrides) {
	auto it = overrides.find(name);
	if(it != overrides.end())
		return it->second;
	return loader.get_signal_type(name);
}

// ---------------------------------------------------------------------------
// Helper: read a signal value as a type-aware Python object.
// ---------------------------------------------------------------------------
static py::object read_signal(
		Xsi::Loader &loader, const std::string &name,
		const std::unordered_map<std::string, Xsi::SignalType> &overrides) {
	auto type = effective_type(loader, name, overrides);
	auto bits = loader.get_signal_value(name);
	return bits_to_python(bits, type);
}

class _XSI {
	public:
		_XSI(
				const std::string &design_so,
				const std::string &simengine_so="libxv_simulator_kernel.so",
				const std::optional<std::string> &wdb=std::nullopt,
				const std::optional<std::string> &logfile=std::nullopt,
				const std::string &shim_so="xsi_shim.so")
			:
				design_so(design_so),
				simengine_so(simengine_so),
				wdb(wdb),
				logfile(logfile)
		{
			loader = std::make_unique<Xsi::Loader>(design_so, simengine_so, shim_so);
			if(!loader)
				throw std::runtime_error("Unable to create simulator kernel!");

			memset(&info, 0, sizeof(info));

			if(wdb)
				info.wdbFileName = (char *)this->wdb->c_str();
			if(logfile)
				info.logFileName = (char *)this->logfile->c_str();

			loader->open(&info);

			loader->init_hierarchy();

			if(wdb)
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

		virtual ~_XSI() = default;

		void sim_close() {
			loader->close();
		}

		void sim_restart() {
			loader->restart();
		}

		void sim_run(std::optional<int64_t> steps, std::optional<double> seconds) {
			if(steps && seconds)
				throw py::value_error(
					"sim_run() accepts steps= or seconds=, not both");
			if(seconds) {
				double prec = sim_precision();
				loader->run(static_cast<int64_t>(*seconds / prec));
			} else {
				loader->run(steps.value_or(0));
			}
		}

		std::vector<std::string> sim_list_signals() {
			return loader->list_signals();
		}

		void sim_watch(const std::string &name, bool stop = false) {
			loader->watch(name, stop);
		}

		std::vector<std::string> sim_get_changes() {
			return loader->get_changes();
		}

		double sim_precision() {
			int exponent = loader->get_int(xsiTimePrecisionKernel);
			return std::pow(10.0, exponent);
		}

		void sim_vcd(const std::string &filename, int depth = 0) {
			loader->vcd_dumpfile(filename);
			loader->vcd_dumpvars(depth);
		}
		void sim_vcd_on()    { loader->vcd_dumpon(); }
		void sim_vcd_off()   { loader->vcd_dumpoff(); }
		void sim_vcd_flush() { loader->vcd_dumpflush(); }
		void sim_vcd_close() { loader->vcd_close(); }

		// Override the auto-detected type for a signal.
		// Accepts either positional (name, type) or keyword arguments
		// (signal_name=type, ...) for bulk assignment.
		void sim_type(py::args args, py::kwargs kwargs) {
			if(args.size() == 2) {
				_set_type(args[0].cast<std::string>(), args[1]);
			} else if(args.size() != 0) {
				throw py::type_error(
					"sim_type() accepts (name, type) or keyword arguments");
			}
			for(auto &[key, val] : kwargs)
				_set_type(key.cast<std::string>(), val);
		}

	private:
		void _set_type(const std::string &name, py::handle type) {
			if(type.is(py::type::of<TagSigned>()))
				_type_overrides[name] = Xsi::SignalType::SIGNED_INT;
			else if(type.is(py::type::of<TagUnsigned>()))
				_type_overrides[name] = Xsi::SignalType::UNSIGNED_INT;
			else if(type.is(py::type::of<TagVector>()))
				_type_overrides[name] = Xsi::SignalType::VECTOR;
			else
				throw py::type_error(
					"Expected pyxsi.Signed, pyxsi.Unsigned, or pyxsi.Vector, got "
					+ std::string(py::str(type)));
		}
	public:

		// __getattr__: resolve bare name as a port, return type-aware value.
		// Only called when normal attribute lookup fails (i.e. sim_* methods
		// are found first via the type's MRO).
		py::object getattr(const std::string &name) {
			try {
				return read_signal(*loader, name, _type_overrides);
			} catch(const std::runtime_error &) {
				throw py::attribute_error(
					"XSI object has no attribute or port '" + name + "'");
			}
		}

		// __setattr__: resolve bare name as a writable port, write type-aware value.
		// Raises AttributeError if name is not a port.
		void setattr(const std::string &name, py::handle value) {
			if(!loader->is_port(name))
				throw py::attribute_error(
					"XSI object has no writable port '" + name + "'");
			auto type = effective_type(*loader, name, _type_overrides);
			python_to_signal(*loader, name, value, type);
		}

		// __getitem__: resolve any name (bare or hierarchical), return type-aware value.
		py::object getitem(const std::string &name) {
			return read_signal(*loader, name, _type_overrides);
		}

		// __setitem__: resolve name, write if writable port, else raise TypeError.
		void setitem(const std::string &name, py::handle value) {
			auto type = effective_type(*loader, name, _type_overrides);
			python_to_signal(*loader, name, value, type);
		}

	private:
		std::unique_ptr<Xsi::Loader> loader;
		s_xsi_setup_info info;
		std::unordered_map<std::string, Xsi::SignalType> _type_overrides;

		const std::string design_so;
		const std::string simengine_so;
		const std::optional<std::string> wdb;
		const std::optional<std::string> logfile;
};

PYBIND11_MODULE(_pyxsi, m) {
	py::class_<TagSigned>(m, "Signed");
	py::class_<TagUnsigned>(m, "Unsigned");
	py::class_<TagVector>(m, "Vector");

	py::class_<_XSI>(m, "_XSI")
		.def(py::init<std::string const&, std::string const&, std::optional<std::string> const&, std::optional<std::string> const&, std::string const&>(),
				py::arg("design_so"),
				py::arg("simengine_so"),
				py::arg("wdb")=std::nullopt,
				py::arg("logfile")=std::nullopt,
				py::arg("shim_so")="xsi_shim.so")

		.def("sim_run", &_XSI::sim_run,
			py::kw_only(),
			py::arg("steps")=py::none(), py::arg("seconds")=py::none(),
			py::call_guard<py::gil_scoped_release>())
		.def("sim_close", &_XSI::sim_close)
		.def("sim_restart", &_XSI::sim_restart)
		.def("sim_list_signals", &_XSI::sim_list_signals)
		.def("sim_watch", &_XSI::sim_watch, py::arg("name"), py::arg("stop")=false)
		.def("sim_get_changes", &_XSI::sim_get_changes)
		.def("sim_precision", &_XSI::sim_precision)
		.def("sim_type", &_XSI::sim_type)
		.def("sim_vcd", &_XSI::sim_vcd, py::arg("filename"), py::arg("depth")=0)
		.def("sim_vcd_on", &_XSI::sim_vcd_on)
		.def("sim_vcd_off", &_XSI::sim_vcd_off)
		.def("sim_vcd_flush", &_XSI::sim_vcd_flush)
		.def("sim_vcd_close", &_XSI::sim_vcd_close)

		.def("__getattr__", &_XSI::getattr)
		.def("__setattr__", &_XSI::setattr)
		.def("__getitem__", &_XSI::getitem)
		.def("__setitem__", &_XSI::setitem);
}
