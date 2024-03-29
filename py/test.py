#!/usr/bin/python3.8

import pyxsi
import random

# VHDL uses 1ps timestep by default. This results in a 100 MHz clock.
HALF_PERIOD = 5000


def test_counting():
    xsi = pyxsi.XSI("xsim.dir/widget/xsimk.so", tracefile="counting.wdb")

    (old_a, old_b) = (f"{0:016b}", f"{0:016b}")

    for n in range(65535 + 2):
        xsi.set_port_value("clk", "1")
        xsi.run(HALF_PERIOD)
        xsi.set_port_value("clk", "0")
        xsi.run(HALF_PERIOD)

        xsi.set_port_value("a", f"{n & 0xffff:016b}")
        xsi.set_port_value("b", f"{n & 0xffff:016b}")

        a = xsi.get_port_value("a")
        b = xsi.get_port_value("b")
        sum = xsi.get_port_value("sum")
        product = xsi.get_port_value("product")

        print(f"old_a {old_a} old_b {old_b} sum {sum} product {product}")

        assert (int(old_a, 2) + int(old_b, 2)) % 2 ** 16 == int(sum, 2)
        assert (int(old_a, 2) * int(old_b, 2)) % 2 ** 32 == int(product, 2)

        (old_a, old_b) = (a, b)


def test_random():
    xsi = pyxsi.XSI("xsim.dir/widget/xsimk.so", tracefile="random.wdb")

    (old_a, old_b) = (f"{0:016b}", f"{0:016b}")

    for n in range(65535):
        xsi.set_port_value("clk", "1")
        xsi.run(HALF_PERIOD)
        xsi.set_port_value("clk", "0")
        xsi.run(HALF_PERIOD)

        xsi.set_port_value("a", f"{random.randint(0, 65535):016b}")
        xsi.set_port_value("b", f"{random.randint(0, 65535):016b}")

        a = xsi.get_port_value("a")
        b = xsi.get_port_value("b")
        sum = xsi.get_port_value("sum")
        product = xsi.get_port_value("product")

        print(f"old_a {old_a} old_b {old_b} sum {sum} product {product}")

        assert (int(old_a, 2) + int(old_b, 2)) % 2 ** 16 == int(sum, 2)
        assert (int(old_a, 2) * int(old_b, 2)) % 2 ** 32 == int(product, 2)

        (old_a, old_b) = (a, b)


if __name__ == "__main__":
    import pytest
    import sys

    pytest.main(sys.argv)
