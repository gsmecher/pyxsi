library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity assert_test is port (
    clk : in std_logic := '0';
    doom : in std_logic := '0');
end assert_test;

architecture behav of assert_test is
begin
    process(clk) begin
        if rising_edge(clk) then
            assert doom='0'
	    	report "DOOM!"
	    	severity failure;
        end if;
    end process;
end behav;
