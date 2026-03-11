library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dut is
generic (WIDTH : natural := 16);
port (
    clk     : in  std_logic := '0';
    doom    : in  std_logic := '0';
    u_in    : in  unsigned(WIDTH-1 downto 0) := (others => '0');
    u_out   : out unsigned(WIDTH-1 downto 0) := (others => '0');
    u_inc   : out unsigned(WIDTH-1 downto 0) := (others => '0');
    s_in    : in  signed(WIDTH-1 downto 0) := (others => '0');
    s_out   : out signed(WIDTH-1 downto 0) := (others => '0');
    slv_in  : in  std_logic_vector(WIDTH-1 downto 0) := (others => '0');
    slv_out : out std_logic_vector(WIDTH-1 downto 0) := (others => '0'));
end dut;

architecture behav of dut is
    signal reg_u   : unsigned(WIDTH-1 downto 0) := (others => '0');
    signal reg_s   : signed(WIDTH-1 downto 0) := (others => '0');
    signal reg_slv : std_logic_vector(WIDTH-1 downto 0) := (others => '0');
begin
    process(clk) begin
        if rising_edge(clk) then
            reg_u   <= u_in;
            reg_s   <= s_in;
            reg_slv <= slv_in;
            assert doom='0'
                report "DOOM!"
                severity failure;
        end if;
    end process;
    u_out   <= reg_u;
    u_inc   <= reg_u + 1;
    s_out   <= reg_s;
    slv_out <= reg_slv;
end behav;
