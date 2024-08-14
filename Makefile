XILINX_VIVADO ?= /opt/xilinx/Vivado/2023.2

# See if we're inside docker. If not, wrap ourselves in docker and try again.
ifeq ($(wildcard /.dockerenv),)
include Makefile.docker-boilerplate
else
# For the remainder of this Makefile, we're running within Docker and can focus
# on building documentation instead of the build environment.

default: pyxsi.so

.PHONY: rtl test clean

VPATH=src

CXX=g++-10
CXXFLAGS=-Wall -Werror -g -fPIC -std=c++20 -I/usr/include/python3.10 -I$(XILINX_VIVADO)/data/xsim/include -Isrc

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

pyxsi.so: pybind.o xsi_loader.o
	$(CXX) $(CXXFLAGS) -shared -o $@ $^ -lfmt -ldl

rtl:
	. $(XILINX_VIVADO)/settings64.sh && \
		xelab work.widget -prj rtl/widget.prj -debug all -dll -s widget && \
		xelab work.counter_verilog -prj rtl/counter.prj -debug all -dll -s counter_verilog

test: pyxsi.so
	LD_LIBRARY_PATH=$(XILINX_VIVADO)/lib/lnx64.o \
		python3.10 -m pytest py/test.py -v

clean:
	-rm *.o *.so
	-rm -rf xsim.dir py/__pycache__ *.jou *.log xelab.pb *.wdb

endif
