import glob
import os
from _pyxsi import Signed, Unsigned, Vector, _XSI


XILINX_VIVADO = os.environ.get("XILINX_VIVADO", None)


class XSI(_XSI):
    def __init__(self, design_so, *, simengine_so=None, tracefile=None, logfile=None):

        if simengine_so is None:
            if not XILINX_VIVADO:
                raise RuntimeError("XILINX_VIVADO not set -- source a Vivado settings.sh first")

            if not (hits := glob.glob(os.path.join(XILINX_VIVADO, "lib/lnx64.o/lib*_simulator_kernel.so"))):
                raise RuntimeError(f"No simulator kernel library found in {vivado}/lib/lnx64.o/")

            simengine_so = hits[0]

        shim_so = os.path.join(os.path.dirname(os.path.abspath(__file__)), "xsi_shim.so")

        super().__init__(
            design_so,
            simengine_so,
            tracefile=tracefile,
            logfile=logfile,
            shim_so=shim_so,
        )
