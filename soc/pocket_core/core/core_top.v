//
// User core top-level
//
// Instantiated by the real top-level: apf_top
//

`default_nettype none

module core_top (

//
// physical connections
//

///////////////////////////////////////////////////
// clock inputs 74.25mhz. not phase aligned, so treat these domains as asynchronous

input   wire            clk_74a, // mainclk1
input   wire            clk_74b, // mainclk1 

///////////////////////////////////////////////////
// cartridge interface
// switches between 3.3v and 5v mechanically
// output enable for multibit translators controlled by pic32

// GBA AD[15:8]
inout   wire    [7:0]   cart_tran_bank2,
output  wire            cart_tran_bank2_dir,

// GBA AD[7:0]
inout   wire    [7:0]   cart_tran_bank3,
output  wire            cart_tran_bank3_dir,

// GBA A[23:16]
inout   wire    [7:0]   cart_tran_bank1,
output  wire            cart_tran_bank1_dir,

// GBA [7] PHI#
// GBA [6] WR#
// GBA [5] RD#
// GBA [4] CS1#/CS#
//     [3:0] unwired
inout   wire    [7:4]   cart_tran_bank0,
output  wire            cart_tran_bank0_dir,

// GBA CS2#/RES#
inout   wire            cart_tran_pin30,
output  wire            cart_tran_pin30_dir,
// when GBC cart is inserted, this signal when low or weak will pull GBC /RES low with a special circuit
// the goal is that when unconfigured, the FPGA weak pullups won't interfere.
// thus, if GBC cart is inserted, FPGA must drive this high in order to let the level translators
// and general IO drive this pin.
output  wire            cart_pin30_pwroff_reset,

// GBA IRQ/DRQ
inout   wire            cart_tran_pin31,
output  wire            cart_tran_pin31_dir,

// infrared
input   wire            port_ir_rx,
output  wire            port_ir_tx,
output  wire            port_ir_rx_disable, 

// GBA link port
inout   wire            port_tran_si,
output  wire            port_tran_si_dir,
inout   wire            port_tran_so,
output  wire            port_tran_so_dir,
inout   wire            port_tran_sck,
output  wire            port_tran_sck_dir,
inout   wire            port_tran_sd,
output  wire            port_tran_sd_dir,
 
///////////////////////////////////////////////////
// cellular psram 0 and 1, two chips (64mbit x2 dual die per chip)

output  wire    [21:16] cram0_a,
inout   wire    [15:0]  cram0_dq,
input   wire            cram0_wait,
output  wire            cram0_clk,
output  wire            cram0_adv_n,
output  wire            cram0_cre,
output  wire            cram0_ce0_n,
output  wire            cram0_ce1_n,
output  wire            cram0_oe_n,
output  wire            cram0_we_n,
output  wire            cram0_ub_n,
output  wire            cram0_lb_n,

output  wire    [21:16] cram1_a,
inout   wire    [15:0]  cram1_dq,
input   wire            cram1_wait,
output  wire            cram1_clk,
output  wire            cram1_adv_n,
output  wire            cram1_cre,
output  wire            cram1_ce0_n,
output  wire            cram1_ce1_n,
output  wire            cram1_oe_n,
output  wire            cram1_we_n,
output  wire            cram1_ub_n,
output  wire            cram1_lb_n,

///////////////////////////////////////////////////
// sdram, 512mbit 16bit

output  wire    [12:0]  dram_a,
output  wire    [1:0]   dram_ba,
inout   wire    [15:0]  dram_dq,
output  wire    [1:0]   dram_dqm,
output  wire            dram_clk,
output  wire            dram_cke,
output  wire            dram_ras_n,
output  wire            dram_cas_n,
output  wire            dram_we_n,

///////////////////////////////////////////////////
// sram, 1mbit 16bit

output  wire    [16:0]  sram_a,
inout   wire    [15:0]  sram_dq,
output  wire            sram_oe_n,
output  wire            sram_we_n,
output  wire            sram_ub_n,
output  wire            sram_lb_n,

///////////////////////////////////////////////////
// vblank driven by dock for sync in a certain mode

input   wire            vblank,

///////////////////////////////////////////////////
// i/o to 6515D breakout usb uart

output  wire            dbg_tx,
input   wire            dbg_rx,

///////////////////////////////////////////////////
// i/o pads near jtag connector user can solder to

output  wire            user1,
input   wire            user2,

///////////////////////////////////////////////////
// RFU internal i2c bus 

inout   wire            aux_sda,
output  wire            aux_scl,

///////////////////////////////////////////////////
// RFU, do not use
output  wire            vpll_feed,


//
// logical connections
//

///////////////////////////////////////////////////
// video, audio output to scaler
output  wire    [23:0]  video_rgb,
output  wire            video_rgb_clock,
output  wire            video_rgb_clock_90,
output  wire            video_de,
output  wire            video_skip,
output  wire            video_vs,
output  wire            video_hs,
    
output  wire            audio_mclk,
input   wire            audio_adc,
output  wire            audio_dac,
output  wire            audio_lrck,

///////////////////////////////////////////////////
// bridge bus connection
// synchronous to clk_74a
output  wire            bridge_endian_little,
input   wire    [31:0]  bridge_addr,
input   wire            bridge_rd,
output  reg     [31:0]  bridge_rd_data,
input   wire            bridge_wr,
input   wire    [31:0]  bridge_wr_data,

///////////////////////////////////////////////////
// controller data
// 
// key bitmap:
//   [0]    dpad_up
//   [1]    dpad_down
//   [2]    dpad_left
//   [3]    dpad_right
//   [4]    face_a
//   [5]    face_b
//   [6]    face_x
//   [7]    face_y
//   [8]    trig_l1
//   [9]    trig_r1
//   [10]   trig_l2
//   [11]   trig_r2
//   [12]   trig_l3
//   [13]   trig_r3
//   [14]   face_select
//   [15]   face_start
//   [31:28] type
// joy values - unsigned
//   [ 7: 0] lstick_x
//   [15: 8] lstick_y
//   [23:16] rstick_x
//   [31:24] rstick_y
// trigger values - unsigned
//   [ 7: 0] ltrig
//   [15: 8] rtrig
//
input   wire    [31:0]  cont1_key,
input   wire    [31:0]  cont2_key,
input   wire    [31:0]  cont3_key,
input   wire    [31:0]  cont4_key,
input   wire    [31:0]  cont1_joy,
input   wire    [31:0]  cont2_joy,
input   wire    [31:0]  cont3_joy,
input   wire    [31:0]  cont4_joy,
input   wire    [15:0]  cont1_trig,
input   wire    [15:0]  cont2_trig,
input   wire    [15:0]  cont3_trig,
input   wire    [15:0]  cont4_trig
    
);

// not using the IR port, so turn off both the LED, and
// disable the receive circuit to save power
assign port_ir_tx = 0;
assign port_ir_rx_disable = 1;

// bridge endianness
assign bridge_endian_little = 0;

// cart is unused, so set all level translators accordingly
// directions are 0:IN, 1:OUT
assign cart_tran_bank3 = 8'hzz;
assign cart_tran_bank3_dir = 1'b0;
assign cart_tran_bank2 = 8'hzz;
assign cart_tran_bank2_dir = 1'b0;
assign cart_tran_bank1 = 8'hzz;
assign cart_tran_bank1_dir = 1'b0;
assign cart_tran_bank0 = 4'hf;
assign cart_tran_bank0_dir = 1'b1;
assign cart_tran_pin30 = 1'b0;      // reset or cs2, we let the hw control it by itself
assign cart_tran_pin30_dir = 1'bz;
assign cart_pin30_pwroff_reset = 1'b0;  // hardware can control this
assign cart_tran_pin31 = 1'bz;      // input
assign cart_tran_pin31_dir = 1'b0;  // input

// link port is unused, set to input only to be safe
// each bit may be bidirectional in some applications
assign port_tran_so = 1'bz;
assign port_tran_so_dir = 1'b0;     // SO is output only
assign port_tran_si = 1'bz;
assign port_tran_si_dir = 1'b0;     // SI is input only
assign port_tran_sck = 1'bz;
assign port_tran_sck_dir = 1'b0;    // clock direction can change
assign port_tran_sd = 1'bz;
assign port_tran_sd_dir = 1'b0;     // SD is input and not used

// tie off the rest of the pins we are not using
assign cram0_a = 'h0;
assign cram0_dq = {16{1'bZ}};
assign cram0_clk = 0;
assign cram0_adv_n = 1;
assign cram0_cre = 0;
assign cram0_ce0_n = 1;
assign cram0_ce1_n = 1;
assign cram0_oe_n = 1;
assign cram0_we_n = 1;
assign cram0_ub_n = 1;
assign cram0_lb_n = 1;

assign cram1_a = 'h0;
assign cram1_dq = {16{1'bZ}};
assign cram1_clk = 0;
assign cram1_adv_n = 1;
assign cram1_cre = 0;
assign cram1_ce0_n = 1;
assign cram1_ce1_n = 1;
assign cram1_oe_n = 1;
assign cram1_we_n = 1;
assign cram1_ub_n = 1;
assign cram1_lb_n = 1;

// dram_* is driven by the SoC's LiteDRAM GENSDRPHY (see the pocket_platform
// instance below) — Stage 4 external SDRAM. Not tied off here.

assign sram_a = 'h0;
assign sram_dq = {16{1'bZ}};
assign sram_oe_n  = 1;
assign sram_we_n  = 1;
assign sram_ub_n  = 1;
assign sram_lb_n  = 1;

// dbg_tx/dbg_rx (DevKey/breakout USB-UART) carry the SoC's LiteX console UART
// (115200 8N1) — wired at the pocket_platform instance below.
wire soc_serial_tx;
assign dbg_tx = soc_serial_tx;
assign user1 = 1'bZ;
assign aux_scl = 1'bZ;
assign vpll_feed = 1'bZ;


// for bridge write data, we just broadcast it to all bus devices
// for bridge read data, we have to mux it
// add your own devices here
always @(*) begin
    casex(bridge_addr)
    default: begin
        bridge_rd_data <= 0;
    end
    32'h2xxxxxxx: begin
        // save read-back (data_unloader serves the nonvolatile slot content)
        bridge_rd_data <= save_bridge_rd_data;
    end
    32'hF8xxxxxx: begin
        bridge_rd_data <= cmd_bridge_rd_data;
    end
    endcase
end


//
// host/target command handler
//
    wire            reset_n;                // driven by host commands, can be used as core-wide reset
    wire    [31:0]  cmd_bridge_rd_data;
    
// bridge host commands
// synchronous to clk_74a
    wire            status_boot_done = pll_core_locked_s; 
    wire            status_setup_done = pll_core_locked_s; // rising edge triggers a target command
    wire            status_running = reset_n; // we are running as soon as reset_n goes high

    wire            dataslot_requestread;
    wire    [15:0]  dataslot_requestread_id;
    wire            dataslot_requestread_ack = 1;
    wire            dataslot_requestread_ok = 1;

    wire            dataslot_requestwrite;
    wire    [15:0]  dataslot_requestwrite_id;
    wire    [31:0]  dataslot_requestwrite_size;
    wire            dataslot_requestwrite_ack = 1;
    wire            dataslot_requestwrite_ok = 1;

    wire            dataslot_update;
    wire    [15:0]  dataslot_update_id;
    wire    [31:0]  dataslot_update_size;
    
    wire            dataslot_allcomplete;

    wire     [31:0] rtc_epoch_seconds;
    wire     [31:0] rtc_date_bcd;
    wire     [31:0] rtc_time_bcd;
    wire            rtc_valid;

    wire            savestate_supported;
    wire    [31:0]  savestate_addr;
    wire    [31:0]  savestate_size;
    wire    [31:0]  savestate_maxloadsize;

    wire            savestate_start;
    wire            savestate_start_ack;
    wire            savestate_start_busy;
    wire            savestate_start_ok;
    wire            savestate_start_err;

    wire            savestate_load;
    wire            savestate_load_ack;
    wire            savestate_load_busy;
    wire            savestate_load_ok;
    wire            savestate_load_err;
    
    wire            osnotify_inmenu;

// bridge target commands
// synchronous to clk_74a

    reg             target_dataslot_read;       
    reg             target_dataslot_write;
    reg             target_dataslot_getfile;    // require additional param/resp structs to be mapped
    reg             target_dataslot_openfile;   // require additional param/resp structs to be mapped
    
    wire            target_dataslot_ack;        
    wire            target_dataslot_done;
    wire    [2:0]   target_dataslot_err;

    reg     [15:0]  target_dataslot_id;
    reg     [31:0]  target_dataslot_slotoffset;
    reg     [31:0]  target_dataslot_bridgeaddr;
    reg     [31:0]  target_dataslot_length;
    
    wire    [31:0]  target_buffer_param_struct; // to be mapped/implemented when using some Target commands
    wire    [31:0]  target_buffer_resp_struct;  // to be mapped/implemented when using some Target commands
    
// bridge data slot access
// synchronous to clk_74a

    wire    [9:0]   datatable_addr;
    wire            datatable_wren;
    wire    [31:0]  datatable_data;
    wire    [31:0]  datatable_q;

core_bridge_cmd icb (

    .clk                ( clk_74a ),
    .reset_n            ( reset_n ),

    .bridge_endian_little   ( bridge_endian_little ),
    .bridge_addr            ( bridge_addr ),
    .bridge_rd              ( bridge_rd ),
    .bridge_rd_data         ( cmd_bridge_rd_data ),
    .bridge_wr              ( bridge_wr ),
    .bridge_wr_data         ( bridge_wr_data ),
    
    .status_boot_done       ( status_boot_done ),
    .status_setup_done      ( status_setup_done ),
    .status_running         ( status_running ),

    .dataslot_requestread       ( dataslot_requestread ),
    .dataslot_requestread_id    ( dataslot_requestread_id ),
    .dataslot_requestread_ack   ( dataslot_requestread_ack ),
    .dataslot_requestread_ok    ( dataslot_requestread_ok ),

    .dataslot_requestwrite      ( dataslot_requestwrite ),
    .dataslot_requestwrite_id   ( dataslot_requestwrite_id ),
    .dataslot_requestwrite_size ( dataslot_requestwrite_size ),
    .dataslot_requestwrite_ack  ( dataslot_requestwrite_ack ),
    .dataslot_requestwrite_ok   ( dataslot_requestwrite_ok ),

    .dataslot_update            ( dataslot_update ),
    .dataslot_update_id         ( dataslot_update_id ),
    .dataslot_update_size       ( dataslot_update_size ),
    
    .dataslot_allcomplete   ( dataslot_allcomplete ),

    .rtc_epoch_seconds      ( rtc_epoch_seconds ),
    .rtc_date_bcd           ( rtc_date_bcd ),
    .rtc_time_bcd           ( rtc_time_bcd ),
    .rtc_valid              ( rtc_valid ),
    
    .savestate_supported    ( savestate_supported ),
    .savestate_addr         ( savestate_addr ),
    .savestate_size         ( savestate_size ),
    .savestate_maxloadsize  ( savestate_maxloadsize ),

    .savestate_start        ( savestate_start ),
    .savestate_start_ack    ( savestate_start_ack ),
    .savestate_start_busy   ( savestate_start_busy ),
    .savestate_start_ok     ( savestate_start_ok ),
    .savestate_start_err    ( savestate_start_err ),

    .savestate_load         ( savestate_load ),
    .savestate_load_ack     ( savestate_load_ack ),
    .savestate_load_busy    ( savestate_load_busy ),
    .savestate_load_ok      ( savestate_load_ok ),
    .savestate_load_err     ( savestate_load_err ),

    .osnotify_inmenu        ( osnotify_inmenu ),
    
    .target_dataslot_read       ( target_dataslot_read ),
    .target_dataslot_write      ( target_dataslot_write ),
    .target_dataslot_getfile    ( target_dataslot_getfile ),
    .target_dataslot_openfile   ( target_dataslot_openfile ),
    
    .target_dataslot_ack        ( target_dataslot_ack ),
    .target_dataslot_done       ( target_dataslot_done ),
    .target_dataslot_err        ( target_dataslot_err ),

    .target_dataslot_id         ( target_dataslot_id ),
    .target_dataslot_slotoffset ( target_dataslot_slotoffset ),
    .target_dataslot_bridgeaddr ( target_dataslot_bridgeaddr ),
    .target_dataslot_length     ( target_dataslot_length ),

    .target_buffer_param_struct ( target_buffer_param_struct ),
    .target_buffer_resp_struct  ( target_buffer_resp_struct ),
    
    .datatable_addr         ( datatable_addr ),
    .datatable_wren         ( datatable_wren ),
    .datatable_data         ( datatable_data ),
    .datatable_q            ( datatable_q )

);



////////////////////////////////////////////////////////////////////////////////////////

//
// Our own RISC-V SoC (LiteX + VexRiscv). Runs from ROM, draws pixels into a
// framebuffer BRAM (uncached) which we scan out below; also drives a 32-bit diag
// word. The framebuffer read port is clocked by our pixel clock (vclk): fb_radr
// selects a 32-bit word (4 packed 8bpp RGB332 pixels), fb_rdat returns it one
// pixel-clock later.
//
    wire [31:0] soc_diag;
    wire        soc_video_de, soc_video_hs, soc_video_vs;
    wire  [7:0] soc_video_r, soc_video_g, soc_video_b;
    // Auto-reboot on Game re-pick, and game exit. A dataslot_update for the
    // Game slot (id 2) pulses the SoC reset (~14 ms) and clears skip_autoload:
    // the bootloader then boots and loads the newly picked game. A game calling
    // sys_exit() (toggle from the SoC) also pulses the reset but SETS
    // skip_autoload — this flag lives outside the SoC reset domain, so after
    // the reboot the bootloader sees it and shows the picker instead of
    // relaunching the same game. Pak picks (id 1) do NOT reset.
    wire soc_exit;
    wire soc_exit_s;
    synch_3 se_s0 ( soc_exit, soc_exit_s, clk_74a );
    reg        exit_prev = 0;
    reg        skip_autoload = 0;
    reg [19:0] game_repick_rst = 0;
    always @(posedge clk_74a) begin
        exit_prev <= soc_exit_s;
        if (dataslot_update && dataslot_update_id == 16'd2) begin
            skip_autoload   <= 0;
            game_repick_rst <= 20'hFFFFF;
        end else if (soc_exit_s != exit_prev) begin
            skip_autoload   <= 1;
            game_repick_rst <= 20'hFFFFF;
        end else if (|game_repick_rst)
            game_repick_rst <= game_repick_rst - 1'b1;
    end

    pocket_platform soc (
        .clk74a    ( clk_74a        ),
        // APF host/menu reset (active-low) -> SoC PLL reset (active-high): a menu
        // "reset core" now truly resets the CPU and the framebuffer DMA.
        .rst       ( ~reset_n | (|game_repick_rst) ),
        .diag      ( soc_diag       ),
        // APF controllers ([15:0] buttons, [31:28] type), clk_74a domain; the SoC
        // synchronizes internally. joy = analog sticks (unsigned, 128-centered).
        .cont1     ( cont1_key      ),
        .cont2     ( cont2_key      ),
        .joy1      ( cont1_joy      ),
        .joy2      ( cont2_joy      ),
        // exit/boot-skip (game exit protocol) + save memory access handshake.
        .exit      ( soc_exit       ),
        .boot_skip ( skip_autoload  ),
        .save_adr  ( soc_save_adr   ),
        .save_wdat ( soc_save_wdat  ),
        .save_wr   ( soc_save_wr    ),
        .save_rd   ( soc_save_rd    ),
        .save_rdat ( save_rdat      ),
        .save_ack  ( save_ack       ),
        // Feature ID: what THIS flavor implements (HAL_FEAT_* bits):
        // PALETTE|PCM|PAD2|PAK|FM|SAVE = 0x3F.
        .hwfeat    ( 32'h0000003F ),
        // OPL3 register bus (this flavor's raison d'etre): {A[1:0],D[7:0]} + toggle.
        .opl_cmd   ( soc_opl_cmd    ),
        .opl_wr    ( soc_opl_wr     ),
        .opl_dbg   ( {opl_dbg_nz, opl_dbg_valid, opl_dbg_led, opl_dbg_wrcount} ),
        // 48 kHz stereo sample pair (vid/12.288 domain) -> sound_i2s above.
        .audio_l   ( soc_audio_l    ),
        .audio_r   ( soc_audio_r    ),
        // pak: deferred data-slot pull. Loader words in (12.288 domain), command
        // request out (toggle + params), status/size in (clk_74a, SoC syncs).
        .loader_en   ( pak_wr_en      ),
        .loader_addr ( pak_wr_addr    ),
        .loader_data ( pak_wr_data    ),
        .pak_req     ( soc_pak_req    ),
        .pak_id      ( soc_pak_id     ),
        .pak_dtaddr  ( soc_pak_dtaddr ),
        .pak_offset  ( soc_pak_offset ),
        .pak_length  ( soc_pak_length ),
        .pak_busy    ( pak_busy       ),
        .pak_err     ( pak_err        ),
        .pak_size    ( pak_size_74    ),
        .serial_rx ( dbg_rx         ),
        .serial_tx ( soc_serial_tx  ),
        .vclk      ( clk_core_12288 ),
        // Video stream from LiteX VideoFramebuffer (DRAM scanout), in the vclk domain.
        .video_de    ( soc_video_de ),
        .video_hsync ( soc_video_hs ),
        .video_vsync ( soc_video_vs ),
        .video_r     ( soc_video_r  ),
        .video_g     ( soc_video_g  ),
        .video_b     ( soc_video_b  ),
        // External SDRAM (LiteDRAM GENSDRPHY) -> Pocket dram_* pins.
        .dram_clk    ( dram_clk    ),
        .sdram_a     ( dram_a      ),
        .sdram_ba    ( dram_ba     ),
        .sdram_dq    ( dram_dq     ),
        .sdram_dm    ( dram_dqm    ),
        .sdram_ras_n ( dram_ras_n  ),
        .sdram_cas_n ( dram_cas_n  ),
        .sdram_we_n  ( dram_we_n   ),
        .sdram_cke   ( dram_cke    )
    );

// Video: forward the SoC's LiteX-generated video stream to the APF scaler. The
// SoC's VideoTimingGenerator runs in the clk_core_12288 (vclk) domain and produces
// the 320x240 active timing video.json expects; the framebuffer is DMA'd from DRAM.
assign video_rgb_clock    = clk_core_12288;
assign video_rgb_clock_90 = clk_core_12288_90deg;
assign video_rgb  = { soc_video_r, soc_video_g, soc_video_b };
assign video_de   = soc_video_de;
assign video_hs   = soc_video_hs;
assign video_vs   = soc_video_vs;
assign video_skip = 1'b0;




//
// pak loading (deferred data slot -> SoC DRAM)
//
// The slot is marked deferload in data.json: the host only posts id+size at pick
// time. The SoC's HAL pulls the content after boot (DRAM must be initialized by
// firmware first) by driving target_dataslot_read through the small FSM below;
// the host then bridge-writes the data to 0x1xxxxxxx, which data_loader CDCs to
// the 12.288 MHz domain for the SoC to DMA into main_ram.
//

// File content path: bridge 0x1xxxxxxx -> 16-bit words in the 12.288 domain.
wire        pak_wr_en;
wire [27:0] pak_wr_addr;
wire [15:0] pak_wr_data;

data_loader #(
    .ADDRESS_MASK_UPPER_4 ( 4'h1 ),
    .OUTPUT_WORD_SIZE     ( 2    )
) pak_loader (
    .clk_74a              ( clk_74a ),
    .clk_memory           ( clk_core_12288 ),
    .bridge_wr            ( bridge_wr ),
    .bridge_endian_little ( bridge_endian_little ),
    .bridge_addr          ( bridge_addr ),
    .bridge_wr_data       ( bridge_wr_data ),
    .write_en             ( pak_wr_en ),
    .write_addr           ( pak_wr_addr ),
    .write_data           ( pak_wr_data )
);

// Slot size: the host posts sizes in the data table at slot_index*2+1; the SoC
// selects which entry to read (assets index 0 -> 1, game index 1 -> 3).
// Registered BRAM port (2-cycle latency) -> just latch q continuously.
wire [9:0] soc_pak_dtaddr;
wire [9:0] soc_pak_dtaddr_s;
synch_3 #(.WIDTH(10)) soc_s13 ( soc_pak_dtaddr, soc_pak_dtaddr_s, clk_74a );
assign datatable_addr = soc_pak_dtaddr_s;
assign datatable_wren = 1'b0;
assign datatable_data = 32'h0;
reg [31:0] pak_size_74;
always @(posedge clk_74a) pak_size_74 <= datatable_q;

// Command FSM (clk_74a): the SoC toggles soc_pak_req with offset/length set;
// protocol per APF docs + hardware-learned guards: wait ack, drop read, wait
// done — but accept a done without ack after a ~16-cycle stale-done guard, and
// abort via ~226 ms watchdog (err=7) so a wedged host command can't hang us.
wire        soc_pak_req_s;
wire [31:0] soc_pak_offset_s, soc_pak_length_s;
wire [15:0] soc_pak_id_s;
wire        soc_pak_req;
wire [31:0] soc_pak_offset, soc_pak_length;
wire [15:0] soc_pak_id;
synch_3                soc_s10 ( soc_pak_req,    soc_pak_req_s,    clk_74a );
synch_3 #(.WIDTH(32))  soc_s11 ( soc_pak_offset, soc_pak_offset_s, clk_74a );
synch_3 #(.WIDTH(32))  soc_s12 ( soc_pak_length, soc_pak_length_s, clk_74a );
synch_3 #(.WIDTH(16))  soc_s14 ( soc_pak_id,     soc_pak_id_s,     clk_74a );

reg        pak_prev_req;
reg        pak_busy = 0;
reg  [2:0] pak_err = 0;
reg  [1:0] pak_state = 0;
reg [24:0] pak_wd;
reg  [4:0] pak_guard;
localparam PAK_IDLE = 0, PAK_WAIT_ACK = 1, PAK_WAIT_DONE = 2;

always @(posedge clk_74a) begin
    target_dataslot_write    <= 0;
    target_dataslot_getfile  <= 0;
    target_dataslot_openfile <= 0;
    pak_prev_req <= soc_pak_req_s;

    case (pak_state)
    PAK_IDLE: begin
        target_dataslot_read <= 0;
        if (soc_pak_req_s != pak_prev_req) begin
            target_dataslot_id         <= soc_pak_id_s;
            target_dataslot_slotoffset <= soc_pak_offset_s;
            target_dataslot_bridgeaddr <= 32'h10000000;
            target_dataslot_length     <= soc_pak_length_s;
            target_dataslot_read       <= 1;
            pak_busy  <= 1;
            pak_err   <= 0;
            pak_wd    <= 0;
            pak_guard <= 0;
            pak_state <= PAK_WAIT_ACK;
        end
    end
    PAK_WAIT_ACK: begin
        pak_wd <= pak_wd + 1'b1;
        if (~&pak_guard) pak_guard <= pak_guard + 1'b1;
        if (target_dataslot_ack) begin
            target_dataslot_read <= 0;
            pak_state <= PAK_WAIT_DONE;
        end else if (&pak_guard && target_dataslot_done) begin
            // done with no ack (stale-done guard expired): treat as completion
            target_dataslot_read <= 0;
            pak_err   <= target_dataslot_err;
            pak_busy  <= 0;
            pak_state <= PAK_IDLE;
        end else if (pak_wd[24]) begin
            target_dataslot_read <= 0;
            pak_err   <= 3'h7;              // watchdog abort
            pak_busy  <= 0;
            pak_state <= PAK_IDLE;
        end
    end
    PAK_WAIT_DONE: begin
        pak_wd <= pak_wd + 1'b1;
        if (target_dataslot_done) begin
            pak_err   <= target_dataslot_err;
            pak_busy  <= 0;
            pak_state <= PAK_IDLE;
        end else if (pak_wd[24]) begin
            pak_err   <= 3'h7;
            pak_busy  <= 0;
            pak_state <= PAK_IDLE;
        end
    end
    default: pak_state <= PAK_IDLE;
    endcase
end

//
// save memory (4 KB, nonvolatile data slot id 3 at 0x2xxxxxxx)
//
// Lives OUTSIDE the SoC on purpose: the host streams the .sav in while the
// core may still be in reset (this BRAM survives SoC reboots), and reads it
// back through data_unloader when the user exits the core. The SoC reads/
// writes it through a toggle handshake in the 12.288 domain.
//

wire        save_wr_en;
wire [27:0] save_wr_addr;
wire [15:0] save_wr_data;
data_loader #(
    .ADDRESS_MASK_UPPER_4 ( 4'h2 ),
    .OUTPUT_WORD_SIZE     ( 2    )
) save_loader (
    .clk_74a              ( clk_74a ),
    .clk_memory           ( clk_core_12288 ),
    .bridge_wr            ( bridge_wr ),
    .bridge_endian_little ( bridge_endian_little ),
    .bridge_addr          ( bridge_addr ),
    .bridge_wr_data       ( bridge_wr_data ),
    .write_en             ( save_wr_en ),
    .write_addr           ( save_wr_addr ),
    .write_data           ( save_wr_data )
);

wire        save_rd_en;
wire [27:0] save_rd_addr;
reg  [15:0] save_rd_data;
wire [31:0] save_bridge_rd_data;
data_unloader #(
    .ADDRESS_MASK_UPPER_4 ( 4'h2 ),
    .INPUT_WORD_SIZE      ( 2    ),
    .READ_MEM_CLOCK_DELAY ( 2    )    // registered BRAM read = data 1 cycle late
) save_unloader (
    .clk_74a              ( clk_74a ),
    .clk_memory           ( clk_core_12288 ),
    .bridge_rd            ( bridge_rd ),
    .bridge_endian_little ( bridge_endian_little ),
    .bridge_addr          ( bridge_addr ),
    .bridge_rd_data       ( save_bridge_rd_data ),
    .read_en              ( save_rd_en ),
    .read_addr            ( save_rd_addr ),
    .read_data            ( save_rd_data )
);

// Simple-dual-port RAM (1 write + 1 read side, both muxed) — the only shape
// that reliably infers as M10K here. Dual-write-port coding blew up into 33k
// registers / 197% ALMs (twice). Muxing is safe: host load, host read-back
// and SoC access don't meaningfully overlap (load = boot, read-back = exit).
(* ramstyle = "M10K" *) reg [15:0] save_mem [0:2047];
reg [15:0] save_q;

wire        soc_save_wr, soc_save_rd;
wire [10:0] soc_save_adr;
wire [15:0] soc_save_wdat;
wire        soc_save_wr_s, soc_save_rd_s;
wire [10:0] soc_save_adr_s;
wire [15:0] soc_save_wdat_s;
synch_3                sv_s0 ( soc_save_wr,   soc_save_wr_s,   clk_core_12288 );
synch_3                sv_s1 ( soc_save_rd,   soc_save_rd_s,   clk_core_12288 );
synch_3 #(.WIDTH(11))  sv_s2 ( soc_save_adr,  soc_save_adr_s,  clk_core_12288 );
synch_3 #(.WIDTH(16))  sv_s3 ( soc_save_wdat, soc_save_wdat_s, clk_core_12288 );

reg  save_prev_wr = 0, save_prev_rd = 0;
reg  save_ack  = 0;
reg  [15:0] save_rdat = 0;
reg  soc_rd_pend = 0, soc_rd_pend_d = 0;
wire save_b_we = (soc_save_wr_s != save_prev_wr);

wire        save_we    = save_wr_en | save_b_we;      // loader wins
wire [10:0] save_wadr  = save_wr_en ? save_wr_addr[11:1] : soc_save_adr_s;
wire [15:0] save_wdatm = save_wr_en ? save_wr_data       : soc_save_wdat_s;
wire [10:0] save_radr  = soc_rd_pend ? soc_save_adr_s : save_rd_addr[11:1];

always @(posedge clk_core_12288) begin
    if (save_we)
        save_mem[save_wadr] <= save_wdatm;
    save_q <= save_mem[save_radr];
end
always @(*) save_rd_data = save_q;   // unloader samples 2 cycles after addr

// SoC handshake: a read borrows the read port for 2 cycles (addr mux -> q).
always @(posedge clk_core_12288) begin
    save_prev_wr  <= soc_save_wr_s;
    save_prev_rd  <= soc_save_rd_s;
    soc_rd_pend_d <= soc_rd_pend;
    if (save_b_we)
        save_ack <= ~save_ack;
    if (soc_save_rd_s != save_prev_rd)
        soc_rd_pend <= 1;
    else if (soc_rd_pend && soc_rd_pend_d) begin
        save_rdat     <= save_q;
        save_ack      <= ~save_ack;
        soc_rd_pend   <= 0;
        soc_rd_pend_d <= 0;
    end
end

//
// audio: the SoC streams 48 kHz signed 16-bit stereo samples (registered in the
// 12.288 MHz domain); sound_i2s (agg23, proven on Pocket) serializes to the APF
// DAC. clk_audio = the same 12.288 clock the SoC registers the samples in.
//

wire [15:0] soc_audio_l, soc_audio_r;

//
// OPL3 (FM synthesis, opl3_fpga by Greg Taylor, LGPL) — the fork's raison
// d'etre. Runs entirely in the 12.288 MHz domain (sample rate 12.288e6/248 =
// 49548 Hz, 0.34% below the authentic chip — inaudible). The CPU writes the
// classic two-port register interface through a toggle handshake from the SoC;
// FM output is mixed into the PCM stream below. The CPU never synthesizes FM.
//

// register write handshake: SoC pushes {A[1:0], D[7:0]} + toggle
wire       soc_opl_wr;
wire [9:0] soc_opl_cmd;                    // [9:8]=A, [7:0]=D
wire       soc_opl_wr_s;
wire [9:0] soc_opl_cmd_s;
synch_3               op_s0 ( soc_opl_wr,  soc_opl_wr_s,  clk_core_12288 );
synch_3 #(.WIDTH(10)) op_s1 ( soc_opl_cmd, soc_opl_cmd_s, clk_core_12288 );

reg       opl_prev_wr = 0;
reg       opl_cs_n = 1, opl_wr_n = 1;
reg [1:0] opl_a = 0;
reg [7:0] opl_d = 0;
reg [2:0] opl_hold = 0;
always @(posedge clk_core_12288) begin
    opl_prev_wr <= soc_opl_wr_s;
    if (soc_opl_wr_s != opl_prev_wr) begin
        opl_a    <= soc_opl_cmd_s[9:8];
        opl_d    <= soc_opl_cmd_s[7:0];
        opl_cs_n <= 0;
        opl_wr_n <= 0;
        opl_hold <= 3'd4;                  // host_if captures on the wr edge
    end else if (|opl_hold) begin
        opl_hold <= opl_hold - 1'b1;
        if (opl_hold == 1) begin
            opl_cs_n <= 1;
            opl_wr_n <= 1;
        end
    end
end

wire signed [23:0] opl_sample_l, opl_sample_r;
wire               opl_sample_valid;

// FM chain diagnostics (read back by fmdemo): [15]=nonzero-sample seen,
// [14]=sample_valid seen, [13:0]=register-write toggles observed. One look
// at this value tells which link of CPU->FSM->OPL3->samples is dead.
reg [9:0]  opl_dbg_wrcount = 0;
reg        opl_dbg_valid = 0, opl_dbg_nz = 0;
wire [3:0] opl_dbg_led;   // kon state of channels 0-3 (regfile write proof)
always @(posedge clk_core_12288) begin
    if (soc_opl_wr_s != opl_prev_wr)
        opl_dbg_wrcount <= opl_dbg_wrcount + 1'b1;
    if (opl_sample_valid)
        opl_dbg_valid <= 1;
    if (opl_sample_valid && (opl_sample_l != 0 || opl_sample_r != 0))
        opl_dbg_nz <= 1;
end

opl3 opl3 (
    .clk          ( clk_core_12288 ),
    .clk_host     ( clk_core_12288 ),
    .clk_dac      ( 1'b0 ),
    // NOTE: must be a signal DECLARED ABOVE this line — connecting the (later-
    // declared) pll_core_locked here bound an implicit undriven net, holding
    // the OPL3 in permanent reset and sweeping the entire synth from the fit.
    .ic_n         ( reset_n ),
    .cs_n         ( opl_cs_n ),
    .rd_n         ( 1'b1 ),
    .wr_n         ( opl_wr_n ),
    .address      ( opl_a ),
    .din          ( opl_d ),
    .dout         ( ),
    .sample_valid ( opl_sample_valid ),
    .sample_l     ( opl_sample_l ),
    .sample_r     ( opl_sample_r ),
    .led          ( opl_dbg_led ),
    .irq_n        ( )
);

// mix: PCM (SoC, 12.288 domain) + FM (same domain), saturated to 16 bits
reg signed [15:0] opl_l = 0, opl_r = 0;
always @(posedge clk_core_12288)
    if (opl_sample_valid) begin
        opl_l <= opl_sample_l[23:8];
        opl_r <= opl_sample_r[23:8];
    end

wire signed [16:0] mix_l = $signed(soc_audio_l) + opl_l;
wire signed [16:0] mix_r = $signed(soc_audio_r) + opl_r;
wire [15:0] audio_mix_l = (mix_l > 17'sd32767)  ? 16'h7FFF :
                          (mix_l < -17'sd32768) ? 16'h8000 : mix_l[15:0];
wire [15:0] audio_mix_r = (mix_r > 17'sd32767)  ? 16'h7FFF :
                          (mix_r < -17'sd32768) ? 16'h8000 : mix_r[15:0];

sound_i2s #(
    .CHANNEL_WIDTH ( 16 ),
    .SIGNED_INPUT  ( 1  )
) sound_i2s (
    .clk_74a    ( clk_74a         ),
    .clk_audio  ( clk_core_12288  ),
    .audio_l    ( audio_mix_l     ),
    .audio_r    ( audio_mix_r     ),
    .audio_mclk ( audio_mclk      ),
    .audio_lrck ( audio_lrck      ),
    .audio_dac  ( audio_dac       )
);


///////////////////////////////////////////////


    wire    clk_core_12288;
    wire    clk_core_12288_90deg;
    
    wire    pll_core_locked;
    wire    pll_core_locked_s;
synch_3 s01(pll_core_locked, pll_core_locked_s, clk_74a);

mf_pllbase mp1 (
    .refclk         ( clk_74a ),
    .rst            ( 0 ),
    
    .outclk_0       ( clk_core_12288 ),
    .outclk_1       ( clk_core_12288_90deg ),
    
    .locked         ( pll_core_locked )
);


    
endmodule
