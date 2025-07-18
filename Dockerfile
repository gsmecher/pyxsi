# This Dockerfile captures a Ubuntu 24.04 suitable for Vivado. This is one of
# the LTS releases that shows up in the Vivado release notes.

FROM ubuntu:24.04
MAINTAINER "Graeme Smecher <gsmecher@threespeedlogic.com>"

ENV DEBIAN_FRONTEND noninteractive
ENV LD_LIBRARY_PATH=.:${XILINX_VIVADO}/lib/lnx64.o:${XILINX_VIVADO}/lib/lnx64.o/Ubuntu

WORKDIR /root

RUN apt-get update -qq								\
	&& apt-get install -y --no-install-recommends				\
			ca-certificates						\
			make build-essential g++				\
			python3 python3-dev					\
			python3-pytest python3-pytest-forked			\
			libfmt-dev pybind11-dev python3-pybind11		\
			locales wget valgrind					\
		&& rm -rf /var/lib/apt/lists/* /var/cache/apt/*

# Install libtinfo5, which recent Vivado still seems to rely on
RUN wget http://security.ubuntu.com/ubuntu/pool/universe/n/ncurses/libtinfo5_6.3-2ubuntu0.1_amd64.deb && \
	apt install ./libtinfo5_6.3-2ubuntu0.1_amd64.deb

# make /bin/sh symlink to bash instead of dash
RUN dpkg-divert --remove --no-rename /bin/sh
RUN ln -sf bash /bin/sh
RUN dpkg-divert --add --local --no-rename /bin/sh

# Generate en_US.UTF-8 (required for Vivado)
RUN sed -i '/en_US.UTF-8/s/^# //g' /etc/locale.gen && locale-gen
