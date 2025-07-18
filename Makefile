ifeq ($(XILINX_VIVADO),)
 $(error Please source a Vivado settings.sh script before running this!)
endif

# See if we're inside docker. If not, wrap ourselves in docker and try again.
ifeq ($(wildcard /.dockerenv),)
include Makefile.docker-boilerplate
else
# For the remainder of this Makefile, we're running within Docker and can focus
# on building documentation instead of the build environment.

default: pyxsi.so

.PHONY: rtl test clean

VPATH=src

# Vivado 2025.1+ changed the name of the shared library - it's either
# librdi_simulator_kernel.so or libxv_simulator_kernel.so
SIMENGINE_SO := $(wildcard $(XILINX_VIVADO)/lib/lnx64.o/lib*_simulator_kernel.so)

CXX=g++
CXXFLAGS=-Wall -Werror -g -fPIC -std=c++20		\
	 $(shell python3 -m pybind11 --includes)	\
	-I$(XILINX_VIVADO)/data/xsim/include -Isrc	\
	-DSIMENGINE_SO=\"$(SIMENGINE_SO)\"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

pyxsi.so: pybind.o xsi_loader.o
	$(CXX) $(CXXFLAGS) -shared -o $@ $^ -ldl

rtl:
	. $(XILINX_VIVADO)/settings64.sh && \
		xelab work.widget -prj rtl/widget.prj -debug all -dll -s widget && \
		xelab work.widget -prj rtl/widget.prj -debug all -dll -generic_top WIDTH=64 -s widget64 && \
		xelab work.counter_verilog -prj rtl/counter.prj -debug all -dll -s counter_verilog  && \
		xelab work.counter_wide_verilog -prj rtl/counter.prj -debug all -dll -s counter_wide_verilog

test: pyxsi.so
	LD_LIBRARY_PATH=$(XILINX_VIVADO)/lib/lnx64.o py/test.py -v -s

clean:
	-rm *.o *.so
	-rm -rf xsim.dir py/__pycache__ *.jou *.log xelab.pb *.wdb

endif
