# This Dockerfile captures a Ubuntu 20.04 suitable for Vivado. This is one of
# the LTS releases that shows up in the Vivado release notes (for 2021.1,
# anyway)

FROM ubuntu:20.04
MAINTAINER "Graeme Smecher <gsmecher@threespeedlogic.com>"

ENV DEBIAN_FRONTEND noninteractive
ENV XILINX_VIVADO=/opt/xilinx/Vivado/2021.1
ENV LD_LIBRARY_PATH=.:${XILINX_VIVADO}/lib/lnx64.o:${XILINX_VIVADO}/lib/lnx64.o/Ubuntu

WORKDIR /cosim

RUN apt-get update -qq								\
	&& apt-get install -y --no-install-recommends				\
			ca-certificates						\
			make build-essential g++-10				\
			python3.9 python3.9-dev python3-pip			\
			libfmt-dev pybind11-dev libboost-dev			\
			libjansson-dev libgetdata-dev				\
			libtinfo5 locales					\
			valgrind						\
		&& rm -rf /var/lib/apt/lists/* /var/cache/apt/*

# As packaged in Ubuntu, python3.9 is incompatible with python3-numpy.
# Use pip for almost everything in Python-land.
RUN python3.9 -m pip install setuptools wheel
RUN python3.9 -m pip install pytest pytest-xdist pytest-check pytest-html
RUN python3.9 -m pip install numpy scipy matplotlib

# make /bin/sh symlink to bash instead of dash:
RUN echo "dash dash/sh boolean false" | debconf-set-selections
RUN DEBIAN_FRONTEND=noninteractive dpkg-reconfigure dash

# Generate en_US.UTF-8 (required for Vivado)
RUN sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen && locale-gen
