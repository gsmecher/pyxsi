`timescale 1 ns/1 ps

module counter_verilog (clk, rst, a, b, sum, product);
input rst, clk;
input [15:0] a;
input [15:0] b;
output [15:0] sum;
output [31:0] product;

reg  [15:0] reg_sum;
reg [31:0] reg_product;

assign sum = reg_sum;
assign product = reg_product;

always @ (posedge clk)
  begin
     if (rst) begin
       reg_sum <= 16'h0;
       reg_product <= 32'h0;
     end 
     else begin
       reg_sum <= a+b;
       reg_product <= a*b;
     end
  end
endmodule

module counter_wide_verilog (clk, rst, a, b, sum, product);
input rst, clk;
input [63:0] a;
input [63:0] b;
output [63:0] sum;
output [127:0] product;

reg  [63:0] reg_sum;
reg [127:0] reg_product;

assign sum = reg_sum;
assign product = reg_product;

always @ (posedge clk)
  begin
     if (rst) begin
       reg_sum <= 64'h0;
       reg_product <= 128'h0;
     end 
     else begin
       reg_sum <= a+b;
       reg_product <= a*b;
     end
  end
endmodule