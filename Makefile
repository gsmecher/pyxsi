ifeq ($(XILINX_VIVADO),)
 $(error Please source a Vivado settings.sh script before running this!)
endif

# See if we're inside docker. If not, wrap ourselves in docker and try again.
ifeq ($(wildcard /.dockerenv),)
include Makefile.docker-boilerplate
else
# For the remainder of this Makefile, we're running within Docker and can focus
# on building documentation instead of the build environment.

default: _pyxsi.so

.PHONY: rtl test clean ipython3 python3

VPATH=src
CXX=g++
CXXFLAGS=-Wall -Werror -g -fPIC -std=c++20		\
	 $(shell python3 -m pybind11 --includes)	\
	-I$(XILINX_VIVADO)/data/xsim/include -Isrc

SHIMFLAGS=-Wall -Werror -g -fPIC -std=c++20

# Extract Vivado version (works for both /opt/xilinx/2025.2/Vivado and
# /opt/xilinx/Vivado/2024.1 layout conventions)
VIVADO_VERSION=$(shell echo $(XILINX_VIVADO) | grep -oP '\d{4}\.\d+')
VIVADO_LIBDIR=$(XILINX_VIVADO)/lib/lnx64.o

LIBDIR=lib/$(VIVADO_VERSION)
LIBDIR_STAMP=$(LIBDIR)/.stamp

%.o: %.cpp xsi_loader.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

xsi_shim.so: src/xsi_shim.cpp
	$(CXX) $(SHIMFLAGS) -shared -o $@ $<

$(LIBDIR_STAMP):
	bin/provision-libs.sh $(VIVADO_LIBDIR) $(LIBDIR)
	touch $@

_pyxsi.so: pybind.o xsi_loader.o xsi_shim.so $(LIBDIR_STAMP)
	$(CXX) $(CXXFLAGS) -shared -o $@ pybind.o xsi_loader.o -ldl

rtl:
	. $(XILINX_VIVADO)/settings64.sh && \
		xelab work.dut -prj rtl/dut.prj -debug all -dll -s dut && \
		xelab work.dut -prj rtl/dut.prj -debug all -dll -generic_top WIDTH=64 -s dut_wide && \
		xelab work.dut -prj rtl/dut_v.prj -debug all -dll -s dut_v && \
		xelab work.dut -prj rtl/dut_v.prj -debug all -dll -generic_top WIDTH=64 -s dut_wide_v

ipython3 python3: _pyxsi.so
	LD_LIBRARY_PATH=$(CURDIR)/$(LIBDIR) $@

test: _pyxsi.so
	LD_LIBRARY_PATH=$(CURDIR)/$(LIBDIR) py/test.py -v -s

clean:
	-rm *.o *.so
	-rm -rf lib xsim.dir __pycache__ py/__pycache__ *.jou *.log xelab.pb *.wdb

endif
