#!/usr/bin/env -S python3 -m pytest --forked

import pyxsi
import random
import pytest

# VHDL uses 1ps timestep by default. This results in a 100 MHz clock.
HALF_PERIOD = 5000


@pytest.mark.parametrize("language", ["VHDL", "Verilog"])
def test_counting(language):
    if language == "VHDL":
        xsi = pyxsi.XSI(
            "xsim.dir/widget/xsimk.so", tracefile="widget.wdb"
        )
    else:
        xsi = pyxsi.XSI(
            "xsim.dir/counter_verilog/xsimk.so",
            tracefile="counter_verilog.wdb",
        )

    (old_a, old_b) = ("0", "0")

    for n in range(65535 + 2):
        xsi.set_value("clk", 1)
        xsi.run(HALF_PERIOD)
        xsi.set_value("clk", 0)
        xsi.run(HALF_PERIOD)

        xsi.set_value("a", n & 0xffff)
        xsi.set_value("b", n & 0xffff)

        a = xsi.get_value("a")
        b = xsi.get_value("b")
        sum = xsi.get_value("sum")
        product = xsi.get_value("product")

        print(f"old_a {old_a} old_b {old_b} sum {sum} product {product}")

        assert (int(old_a, 2) + int(old_b, 2)) % 2**16 == int(sum, 2)
        assert (int(old_a, 2) * int(old_b, 2)) % 2**32 == int(product, 2)

        (old_a, old_b) = (a, b)


@pytest.mark.parametrize("language", ["VHDL", "Verilog"])
def test_counting_wide(language):
    if language == "VHDL":
        xsi = pyxsi.XSI(
            "xsim.dir/widget64/xsimk.so", tracefile="widget64.wdb"
        )
    else:
        xsi = pyxsi.XSI(
            "xsim.dir/counter_wide_verilog/xsimk.so",
            tracefile="counter_wide_verilog.wdb",
        )

    (old_a, old_b) = ("0", "0")

    for n in range(2**32, (2**32) + 10):
        xsi.set_value("clk", 1)
        xsi.run(HALF_PERIOD)
        xsi.set_value("clk", 0)
        xsi.run(HALF_PERIOD)

        xsi.set_value("a", f"{(n+1) & 0xffffffffffffff:064b}")
        xsi.set_value("b", f"{(n+2) & 0xffffffffffffff:064b}")

        a = xsi.get_value("a")
        b = xsi.get_value("b")
        sum = xsi.get_value("sum")
        product = xsi.get_value("product")

        print(f"old_a {old_a} old_b {old_b} sum {sum} product {product}")
        print(
            f"old_a {int(old_a, 2)} old_b {int(old_b, 2)} sum {int(sum,2)} product {int(product,2)}"
        )

        assert (int(old_a, 2) + int(old_b, 2)) % 2**64 == int(sum, 2)
        assert (int(old_a, 2) * int(old_b, 2)) % 2**128 == int(product, 2)

        (old_a, old_b) = (a, b)


@pytest.mark.parametrize("language", ["VHDL", "Verilog"])
def test_hier_signal(language):
    if language == "VHDL":
        xsi = pyxsi.XSI(
            "xsim.dir/widget/xsimk.so"
        )
        prefix = "widget"
    else:
        xsi = pyxsi.XSI(
            "xsim.dir/counter_verilog/xsimk.so",
        )
        prefix = "counter_verilog"

    sum_path = f"/{prefix}/sum"
    product_path = f"/{prefix}/product"

    # Verify hierarchy database is populated
    signals = xsi.list_signals()
    assert len(signals) > 0, "No signals found in hierarchy"

    for n in range(10):
        xsi.set_value("clk", 1)
        xsi.run(HALF_PERIOD)
        xsi.set_value("clk", 0)
        xsi.run(HALF_PERIOD)

        xsi.set_value("a", (n+1) & 0xffff)
        xsi.set_value("b", (n+2) & 0xffff)

    # After clocking, the internal regs should match the output ports
    sum_port = xsi.get_value("sum")
    product_port = xsi.get_value("product")

    sum_hier = xsi.get_value(sum_path)
    product_hier = xsi.get_value(product_path)

    print(f"sum port={sum_port} hier={sum_hier}")
    print(f"product port={product_port} hier={product_hier}")

    assert sum_port == sum_hier, f"sum mismatch: port={sum_port} hier={sum_hier}"
    assert product_port == product_hier, f"product mismatch: port={product_port} hier={product_hier}"


@pytest.mark.parametrize("language", ["VHDL", "Verilog"])
def test_random(language):
    if language == "VHDL":
        xsi = pyxsi.XSI(
            "xsim.dir/widget/xsimk.so", tracefile="random_vhdl.wdb"
        )
    else:
        xsi = pyxsi.XSI(
            "xsim.dir/counter_verilog/xsimk.so",
            tracefile="random_verilog.wdb",
        )

    (old_a, old_b) = ("0", "0")

    for n in range(65535):
        xsi.set_value("clk", 1)
        xsi.run(HALF_PERIOD)
        xsi.set_value("clk", 0)
        xsi.run(HALF_PERIOD)

        xsi.set_value("a", random.randint(0, 65535))
        xsi.set_value("b", random.randint(0, 65535))

        a = xsi.get_value("a")
        b = xsi.get_value("b")
        sum = xsi.get_value("sum")
        product = xsi.get_value("product")

        print(f"old_a {old_a} old_b {old_b} sum {sum} product {product}")

        assert (int(old_a, 2) + int(old_b, 2)) % 2**16 == int(sum, 2)
        assert (int(old_a, 2) * int(old_b, 2)) % 2**32 == int(product, 2)

        (old_a, old_b) = (a, b)


if __name__ == "__main__":
    import pytest
    import sys

    pytest.main(sys.argv)
