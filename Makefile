default: pyxsi.so

.PHONY: rtl test clean

XILINX_VIVADO := /opt/xilinx/Vivado/2019.2

VPATH=src

CXXFLAGS=-fPIC -std=c++17 -I/usr/include/python3.8 -I$(XILINX_VIVADO)/data/xsim/include -Isrc

%.o: %.cpp
	g++ $(CXXFLAGS) -c -o $@ $<

pyxsi.so: pybind.o xsi_loader.o
	g++ $(CXXFLAGS) -shared -o $@ $^ -ldl

rtl:
	xelab work.widget -prj rtl/widget.prj -debug all -dll -s widget
	xelab work.assert_test -prj rtl/assert_test.prj -debug all -dll -s assert_test

test: pyxsi.so
	LD_LIBRARY_PATH=$(XILINX_VIVADO)/lib/lnx64.o python3.8 -m pytest py/test.py -v

clean:
	-rm *.o *.so
	-rm -rf xsim.dir py/__pycache__ *.jou *.log xelab.pb *.wdb
