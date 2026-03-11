`timescale 1 ns/1 ps

module dut #(parameter WIDTH = 16) (clk, doom, u_in, u_out, u_inc, s_in, s_out, slv_in, slv_out);
input clk;
input doom;
input  [WIDTH-1:0] u_in, s_in, slv_in;
output [WIDTH-1:0] u_out, u_inc, s_out, slv_out;

reg [WIDTH-1:0] reg_u, reg_s, reg_slv;

assign u_out   = reg_u;
assign u_inc   = reg_u + 1;
assign s_out   = reg_s;
assign slv_out = reg_slv;

always @(posedge clk) begin
    reg_u   <= u_in;
    reg_s   <= s_in;
    reg_slv <= slv_in;
    assert (!doom) else $fatal(1, "DOOM!");
end
endmodule
