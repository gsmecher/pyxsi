# This Dockerfile captures a Ubuntu 22.04 suitable for Vivado. This is one of
# the LTS releases that shows up in the Vivado release notes (for 2023.2)

FROM ubuntu:22.04
MAINTAINER "Graeme Smecher <gsmecher@threespeedlogic.com>"

ENV DEBIAN_FRONTEND noninteractive
ENV LD_LIBRARY_PATH=.:${XILINX_VIVADO}/lib/lnx64.o:${XILINX_VIVADO}/lib/lnx64.o/Ubuntu

WORKDIR /cosim

RUN apt-get update -qq								\
	&& apt-get install -y --no-install-recommends				\
			ca-certificates						\
			make build-essential g++-10				\
			python3 python3-dev python3-pip python3-wheel		\
			python3-numpy python3-scipy python3-matplotlib		\
			python3-pytest python3-pytest-forked python3-py		\
			libfmt-dev pybind11-dev libboost-dev			\
			libjansson-dev libgetdata-dev				\
			libtinfo5 locales					\
			valgrind						\
		&& rm -rf /var/lib/apt/lists/* /var/cache/apt/*

# make /bin/sh symlink to bash instead of dash:
RUN echo "dash dash/sh boolean false" | debconf-set-selections
RUN DEBIAN_FRONTEND=noninteractive dpkg-reconfigure dash

# Generate en_US.UTF-8 (required for Vivado)
RUN sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen && locale-gen
