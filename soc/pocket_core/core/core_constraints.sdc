#
# user core constraints
#
# Clock groups: every clock domain in the design must appear in a -group, or STA
# times its CDC paths as synchronous and the report drowns in impossible negative
# slack (the pre-v0.7.0 -1680ns numbers were exactly this: the SoC ALTPLL clocks
# were missing). All RTL crossings between groups are 2-FF MultiReg / AsyncFIFO
# (verified in the 2026-07 audit), so -asynchronous is safe.
#
# mf_pllbase general[2]/[3] were removed: that PLL only has two outputs (12.288 MHz
# pixel clock + its 90deg twin), the extra groups matched nothing.
# The two SoC ALTPLL outputs (sys 25 MHz, sys_ps 180deg -> dram_clk pin only, no
# registers) share one group.
#

set_clock_groups -asynchronous \
 -group { bridge_spiclk } \
 -group { clk_74a } \
 -group { clk_74b } \
 -group { ic|mp1|mf_pllbase_inst|altera_pll_i|general[0].gpll~PLL_OUTPUT_COUNTER|divclk } \
 -group { ic|mp1|mf_pllbase_inst|altera_pll_i|general[1].gpll~PLL_OUTPUT_COUNTER|divclk } \
 -group { ic|soc|ALTPLL|auto_generated|generic_pll1~PLL_OUTPUT_COUNTER|divclk \
          ic|soc|ALTPLL|auto_generated|generic_pll2~PLL_OUTPUT_COUNTER|divclk }
