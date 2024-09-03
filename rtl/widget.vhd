library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity widget is
generic (WIDTH : natural := 16);
port (
    clk, rst : in std_logic := '0';
    a, b     : in unsigned(WIDTH-1 downto 0):= (others => '0');
    sum      : out unsigned(WIDTH-1 downto 0) := (others => '0');
    product  : out unsigned(2*WIDTH-1 downto 0) := (others => '0'));
end widget;

architecture behav of widget is
begin
    process(clk) begin
        if rising_edge(clk) then
            if rst = '1' then
                sum <= (others => '0');
                product <= (others => '0');
            else
                sum <= a + b;
                product <= a * b;
            end if;
        end if;
    end process;
end behav;
