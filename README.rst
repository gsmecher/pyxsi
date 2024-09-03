C/RTL Cosimulation with Vivado and Python
=========================================

:tags: Vivado, xsim, XSI
:category: Vivado
:slug: vivado-cosimulation-with-xsi
:summary: A method to cosimulate C and RTL code under Python control

.. contents:: Table of Contents

Summary
~~~~~~~

Xilinx's xsim simulator (UG900_) includes the Xilinx Simulator Interface (XSI),
a way to embed a RTL simulation kernel inside a C/C++ program.  This page
further embeds XSI kernels within a Python session, creating a layered
simulation environment where Python, C/C++, and RTL coexist.  Testbenches can
be coded in a mixture of the three languages, removing the hard boundaries that
surround an RTL-only testbench environment.

Motivation
~~~~~~~~~~

RTL testbenches are a good way to verify low-level design elements, but RTL
testbenches alone are not good enough.

First, **I want to verify a combination of C and RTL code**, where the
interface between C and RTL is complex and malleable.  Driving a wedge between
C code and RTL code so I can extract test vectors via file I/O feels like a
monumental distraction.

Second, **I need to post-process test data in Python.** For signal-processing
analysis, my go-to environment is Python/Scipy/Matplotlib.  For data analysis,
there is nothing in C or RTL that offer the same broad reach and high
productivity.

Finally, **I want my test environment to resemble my deployment environment.**
My design is an amalgam of C and RTL when it's deployed; why should the testing
environment be different?  (Here, ASIC designers will be tearing out their
hair. It's OK, I understand.) Combining C and RTL reduces the amount of hassle
associated with testing, which allows me to focus on solving problems and not
fighting tools.

Compounding the issue further: many excellent test environments (cocotb_,
VUnit_, UVVM_) do not work with Xilinx's simulator.  As a result, my options
for higher-level testing constructs focused on an RTL world dwindle from
"frustrating" to "squalid".  This is largely Xilinx's fault: although it is
slowly improving, xsim's support for SystemVerilog and VHDL-2008 constructs
lags other simulators [1]_.

Demonstration
~~~~~~~~~~~~~

The following sections show a start-to-finish build, from checking out the
source code to completing a test.

Check Out the Source Code
-------------------------

Source code is available at https://github.com/gsmecher/pyxsi.

.. code-block:: bash

   $ git clone https://github.com/gsmecher/pyxsi.git
   Cloning into 'pyxsi'...
   remote: Enumerating objects: 14, done.
   remote: Counting objects: 100% (14/14), done.
   remote: Compressing objects: 100% (12/12), done.
   remote: Total 14 (delta 0), reused 14 (delta 0), pack-reused 0
   Receiving objects: 100% (14/14), 6.72 KiB | 6.72 MiB/s, done.

Prepare the Build
-----------------

You should modify the top-level Makefile (setting XILINX_BASE) to match your
Vivado installation.  The example here uses Vivado 2023.2, though it is
reasonably portable.

Next, prepare your Docker environment:

.. code-block:: bash

   $ cd pyxsi
   pyxsi$ make dockerenv

This step prepares a portable build environment using Docker.  You only need to
run this step once.

Build the RTL
-------------

Now set up Xilinx's environment variables and build the RTL into a simulation
library.

There is a Docker wrapper in this command (and subsequent commands). You may
need to modify the top-level Makefile to point to the right Vivado installation
path.

.. code-block:: bash

   pyxsi$ make rtl
   Moving inside Docker...
   make: Entering directory '/root'
   . /opt/xilinx/Vivado/2023.2/settings64.sh && \
        xelab work.widget -prj rtl/widget.prj -debug all -dll -s widget && \
        xelab work.counter_verilog -prj rtl/counter.prj -debug all -dll -s counter_verilog  && \
        xelab work.counter_wide_verilog -prj rtl/counter.prj -debug all -dll -s counter_wide_verilog
   Vivado Simulator v2023.2
   Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
   Copyright 2022-2023 Advanced Micro Devices, Inc. All Rights Reserved.
   Running: /opt/xilinx/Vivado/2023.2/bin/unwrapped/lnx64.o/xelab work.widget -prj rtl/widget.prj -debug all -dll -s widget
   Multi-threading is on. Using 14 slave threads.
   Determining compilation order of HDL files.
   INFO: [VRFC 10-163] Analyzing VHDL file "/root/rtl/widget.vhd" into library work
   INFO: [VRFC 10-3107] analyzing entity 'widget'
   Starting static elaboration
   Completed static elaboration
   Starting simulation data flow analysis
   Completed simulation data flow analysis
   Time Resolution for simulation is 1ps
   Compiling package std.standard
   Compiling package std.textio
   Compiling package ieee.std_logic_1164
   Compiling package ieee.numeric_std
   Compiling architecture behav of entity work.widget
   Built XSI simulation shared library xsim.dir/widget/xsimk.so
   Vivado Simulator v2023.2
   Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
   Copyright 2022-2023 Advanced Micro Devices, Inc. All Rights Reserved.
   Running: /opt/xilinx/Vivado/2023.2/bin/unwrapped/lnx64.o/xelab work.counter_verilog -prj rtl/counter.prj -debug all -dll -s counter_verilog
   Multi-threading is on. Using 14 slave threads.
   INFO: [VRFC 10-2263] Analyzing Verilog file "/root/rtl/counter.v" into library work
   INFO: [VRFC 10-311] analyzing module counter_verilog
   INFO: [VRFC 10-311] analyzing module counter_wide_verilog
   Starting static elaboration
   Pass Through NonSizing Optimizer
   Completed static elaboration
   Starting simulation data flow analysis
   Completed simulation data flow analysis
   Time Resolution for simulation is 1ps
   Compiling module work.counter_verilog
   Built XSI simulation shared library xsim.dir/counter_verilog/xsimk.so
   Vivado Simulator v2023.2
   Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
   Copyright 2022-2023 Advanced Micro Devices, Inc. All Rights Reserved.
   Running: /opt/xilinx/Vivado/2023.2/bin/unwrapped/lnx64.o/xelab work.counter_wide_verilog -prj rtl/counter.prj -debug all -dll -s counter_wide_verilog
   Multi-threading is on. Using 14 slave threads.
   INFO: [VRFC 10-2263] Analyzing Verilog file "/root/rtl/counter.v" into library work
   INFO: [VRFC 10-311] analyzing module counter_verilog
   INFO: [VRFC 10-311] analyzing module counter_wide_verilog
   Starting static elaboration
   Pass Through NonSizing Optimizer
   Completed static elaboration
   Starting simulation data flow analysis
   Completed simulation data flow analysis
   Time Resolution for simulation is 1ps
   Compiling module work.counter_wide_verilog
   Built XSI simulation shared library xsim.dir/counter_wide_verilog/xsimk.so
   make: Leaving directory '/root'

Build the C++ Code
------------------

Now build the C++ code:

.. code-block:: bash

   pyxsi$ make
   make: Entering directory '/root'
   g++-10 -Wall -Werror -g -fPIC -std=c++20 -I/usr/include/python3.9 -I/opt/xilinx/Vivado/2021.2/data/xsim/include -Isrc -c -o pybind.o src/pybind.cpp
   g++-10 -Wall -Werror -g -fPIC -std=c++20 -I/usr/include/python3.9 -I/opt/xilinx/Vivado/2021.2/data/xsim/include -Isrc -c -o xsi_loader.o src/xsi_loader.cpp
   g++-10 -Wall -Werror -g -fPIC -std=c++20 -I/usr/include/python3.9 -I/opt/xilinx/Vivado/2021.2/data/xsim/include -Isrc -shared -o pyxsi.so pybind.o xsi_loader.o -lfmt -ldl -static-libstdc++
   make: Leaving directory '/root'

Execute Tests
-------------

Finally, tests are discovered and executed using Python's pytest_ environment.

.. code-block:: bash

   pyxsi$ make test
   Moving inside Docker...
   make: Entering directory '/root'
   LD_LIBRARY_PATH=/opt/xilinx/Vivado/2023.2/lib/lnx64.o \
        python3.10 -m pytest py/test.py -v
   ============================= test session starts ==============================
   platform linux -- Python 3.10.12, pytest-8.3.2, pluggy-1.5.0 -- /usr/bin/python3.10
   cachedir: .pytest_cache
   metadata: {'Python': '3.10.12', 'Platform': 'Linux-6.7.12-amd64-x86_64-with-glibc2.35', 'Packages': {'pytest': '8.3.2', 'pluggy': '1.5.0'}, 'Plugins': {'html': '4.1.1', 'check': '2.4.1', 'metadata': '3.1.1', 'xdist': '2.5.0', 'forked': '1.4.0'}}
   rootdir: /root
   plugins: html-4.1.1, check-2.4.1, metadata-3.1.1, xdist-2.5.0, forked-1.4.0
   collecting ... collected 4 items

   py/test.py::test_counting[VHDL] PASSED                                   [ 25%]
   py/test.py::test_counting[VERILOG] PASSED                                [ 50%]
   py/test.py::test_counting_wide_verilog PASSED                            [ 75%]
   py/test.py::test_random PASSED                                           [100%]

   ============================== 4 passed in 5.09s ===============================
   make: Leaving directory '/root'

Conclusions
~~~~~~~~~~~

This is only a skeletal example, with just enough scaffolding to build on.

.. _UG900: https://www.xilinx.com/support/documentation/sw_manuals/xilinx2021_2/ug900-vivado-logic-simulation.pdf
.. _cocotb: https://github.com/cocotb/cocotb
.. _VUnit: https://vunit.github.io/
.. _UVVM: https://github.com/UVVM/UVVM
.. _pytest: https://docs.pytest.org/en/latest/

.. [1] I use xsim because none of the open-source simulators can combine VHDL
   and Verilog, or simulate encrypted IP.  Commercial simulators don't make
   sense for a small instrumentation consultancy (mine, anyway).
