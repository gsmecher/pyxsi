#!/usr/bin/env -S python3 -m pytest

import os
import pyxsi
import pytest

@pytest.fixture
def xsi(request):
    """Indirect fixture: parametrize with a dict of {so, ...}."""
    sim = pyxsi.XSI(request.param["so"])

    # Apply SystemVerilog type overrides if needed (signed/unsigned annotations
    # are apparently unavailable in XSI/IKI, so user annotation is handy)
    if request.param.get("sv_types"):
        sim.sim_type(
                u_in=pyxsi.Unsigned,
                u_out=pyxsi.Unsigned,
                u_inc=pyxsi.Unsigned,
                s_in=pyxsi.Signed,
                s_out=pyxsi.Signed)

    yield sim

    # Prevent stale design .so from blocking the next fixture - particularly
    # when an exception has occurred (mumble mumble exception traceback
    # reference-count cycles)
    sim.sim_close()


def clock(sim, n=1):
    # 100 MHz clock
    for _ in range(n):
        sim.clk = 1
        sim.sim_run(seconds=5e-9)
        sim.clk = 0
        sim.sim_run(seconds=5e-9)


# ---------------------------------------------------------------------------
# Design parameter sets
# ---------------------------------------------------------------------------

VHDL        = dict(so="xsim.dir/dut/xsimk.so")
VERILOG     = dict(so="xsim.dir/dut_v/xsimk.so", sv_types=True)
VHDL_WIDE   = dict(so="xsim.dir/dut_wide/xsimk.so")
VERILOG_WIDE= dict(so="xsim.dir/dut_wide_v/xsimk.so", sv_types=True)
VERILOG_RAW = dict(so="xsim.dir/dut_v/xsimk.so")



# ---------------------------------------------------------------------------
# Unified tests (VHDL auto-detects types, sv uses sim_type overrides)
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("xsi", [VHDL, VERILOG], indirect=True, ids=["vhdl", "sv"])
def test_passthrough(xsi):
    """Attr write -> clock -> attr read roundtrip via u_in->u_out, verify u_inc."""
    for val in [0, 1, 0xBEEF, 0xFFFF]:
        xsi.u_in = val
        clock(xsi)
        assert xsi.u_out == val
        assert xsi.u_inc == (val + 1) & 0xFFFF


@pytest.mark.parametrize("xsi", [VHDL_WIDE, VERILOG_WIDE], indirect=True, ids=["vhdl", "sv"])
def test_passthrough_wide(xsi):
    """64-bit passthrough + increment with values > 2^32."""
    for val in [0, 1, 2**32 + 1, 2**64 - 1]:
        xsi.u_in = val
        clock(xsi)
        assert xsi.u_out == val
        assert xsi.u_inc == (val + 1) & (2**64 - 1)


@pytest.mark.parametrize("xsi", [VHDL, VERILOG], indirect=True, ids=["vhdl", "sv"])
def test_signed(xsi):
    """Signed integer roundtrip (negative values)."""
    for val in [-1, -7, -32768]:
        xsi.s_in = val
        clock(xsi)
        assert xsi.s_out == val


@pytest.mark.parametrize("xsi", [VHDL, VERILOG], indirect=True, ids=["vhdl", "sv"])
def test_hierarchy(xsi):
    """sim_list_signals() non-empty; dict path matches attr access."""
    signals = xsi.sim_list_signals()
    assert len(signals) > 0, "No signals found in hierarchy"

    # Apply same type override to hierarchical path so values compare equal
    xsi.sim_type("/dut/u_out", pyxsi.Unsigned)
    xsi.u_in = 42
    clock(xsi)
    assert xsi["/dut/u_out"] == xsi.u_out


@pytest.mark.parametrize("xsi", [VHDL, VERILOG], indirect=True, ids=["vhdl", "sv"])
def test_signals_readonly(xsi):
    """Dict read of internal register; write raises."""
    xsi.sim_type("/dut/reg_u", pyxsi.Unsigned)
    xsi.u_in = 99
    clock(xsi)
    assert xsi["/dut/reg_u"] == 99

    with pytest.raises(RuntimeError):
        xsi["/dut/reg_u"] = 5


@pytest.mark.parametrize("xsi", [VHDL, VERILOG], indirect=True, ids=["vhdl", "sv"])
def test_watch(xsi):
    """sim_watch() + sim_get_changes(): detected on change, not reported when unchanged."""
    xsi.sim_watch("/dut/reg_u")

    # Setup: latch a value, drain initial changes
    xsi.u_in = 10
    clock(xsi, 2)
    xsi.sim_get_changes()

    # Change input -- reg_u should change
    xsi.u_in = 20
    clock(xsi)
    changes = xsi.sim_get_changes()
    assert "/dut/reg_u" in changes

    # Same input -- no change
    clock(xsi)
    changes = xsi.sim_get_changes()
    assert "/dut/reg_u" not in changes


@pytest.mark.parametrize("xsi", [VHDL, VERILOG], indirect=True, ids=["vhdl", "sv"])
def test_watch_stop(xsi):
    """sim_watch(stop=True) causes sim_run() to return early on signal change."""
    xsi.sim_watch("/dut/reg_u", stop=True)

    xsi.u_in = 10
    xsi.clk = 1
    xsi.sim_run(seconds=10e-6)

    changes = xsi.sim_get_changes()
    assert "/dut/reg_u" in changes

    # Simulation should still be usable
    xsi.clk = 0
    xsi.sim_run(seconds=5e-9)

    xsi.u_in = 20
    xsi.clk = 1
    xsi.sim_run(seconds=10e-6)

    changes = xsi.sim_get_changes()
    assert "/dut/reg_u" in changes


@pytest.mark.parametrize("so", [VHDL["so"], VERILOG["so"]], ids=["vhdl", "sv"])
def test_wdb(so, request):
    """wdb= kwarg produces a non-empty .wdb file on disk."""
    wdbfile = request.node.name + ".wdb"
    if os.path.exists(wdbfile):
        os.remove(wdbfile)

    sim = pyxsi.XSI(so, wdb=wdbfile)
    sim.u_in = 0
    clock(sim)
    sim.u_in = 0xA5
    clock(sim)
    del sim

    assert os.path.exists(wdbfile), f"WDB file {wdbfile} was not created"
    assert os.path.getsize(wdbfile) > 0, f"WDB file {wdbfile} is empty"


@pytest.mark.parametrize("xsi", [VHDL, VERILOG], indirect=True, ids=["vhdl", "sv"])
def test_time_precision(xsi):
    """sim_precision() returns the kernel time precision in seconds."""
    prec = xsi.sim_precision()
    # xsim defaults to 1ps
    assert prec == 1e-12


# ---------------------------------------------------------------------------
# Type-error tests
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("xsi", [VHDL, VERILOG], indirect=True, ids=["vhdl", "sv"])
def test_type_errors(xsi):
    """Type-mismatched writes raise TypeError/ValueError."""
    # Integer-typed signal rejects non-int
    with pytest.raises(TypeError):
        xsi.u_in = [1, 2, 3]


@pytest.mark.parametrize("xsi", [VHDL], indirect=True, ids=["vhdl"])
def test_type_error_logic(xsi):
    """std_logic rejects out-of-range int (VHDL auto-detects clk as logic)."""
    with pytest.raises(ValueError):
        xsi.clk = 5


# ---------------------------------------------------------------------------
# VHDL-specific: verify auto-detection without explicit overrides
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("xsi", [VHDL], indirect=True, ids=["vhdl"])
def test_vhdl_auto_types(xsi):
    """VHDL auto-detects: unsigned->int, signed->int, std_logic->str, slv->str."""
    # unsigned -> int
    xsi.u_in = 42
    clock(xsi)
    u = xsi.u_out
    assert isinstance(u, int) and u == 42

    # signed -> int (negative roundtrip)
    xsi.s_in = -7
    clock(xsi)
    s = xsi.s_out
    assert isinstance(s, int) and s == -7

    # std_logic -> str
    clk_val = xsi.clk
    assert isinstance(clk_val, str)

    # std_logic_vector -> str
    xsi.slv_in = f"{'1010' * 4}"
    clock(xsi)
    slv = xsi.slv_out
    assert isinstance(slv, str)
    assert int(slv, 2) == int("1010" * 4, 2)


# ---------------------------------------------------------------------------
# sv-specific: verify default (untyped) bit-string behavior
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("xsi", [VERILOG_RAW], indirect=True, ids=["sv"])
def test_verilog_default_types(xsi):
    """Without sim_type overrides, sv signals are bit strings."""
    xsi.u_in = f"{42:016b}"
    clock(xsi)
    assert isinstance(xsi.u_out, str)
    assert int(xsi.u_out, 2) == 42


# ---------------------------------------------------------------------------
# Assertion tests
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("xsi", [VHDL, VERILOG], indirect=True, ids=["vhdl", "sv"])
def test_assert(xsi):
    """Severity-failure assertion raises RuntimeError in Python."""
    # doom=0: assertion should not fire
    xsi.doom = 0
    clock(xsi)

    # doom=1: assertion fires on next rising edge
    xsi.doom = 1
    with pytest.raises(RuntimeError, match="DOOM"):
        clock(xsi)


@pytest.mark.parametrize("xsi", [VHDL, VERILOG], indirect=True, ids=["vhdl", "sv"])
def test_vcd(xsi, request):
    """sim_vcd() produces a VCD file containing signal transitions."""
    vcdfile = request.node.name + ".vcd"
    if os.path.exists(vcdfile):
        os.remove(vcdfile)

    xsi.sim_vcd(vcdfile)
    xsi.u_in = 0
    clock(xsi)
    xsi.u_in = 0xA5
    clock(xsi)
    xsi.sim_vcd_close()

    assert os.path.exists(vcdfile), f"VCD file {vcdfile} was not created"
    contents = open(vcdfile).read()
    assert "$var" in contents, "VCD file missing $var declarations"
    assert "$end" in contents, "VCD file missing $end markers"


if __name__ == "__main__":
    import sys
    pytest.main(sys.argv)
