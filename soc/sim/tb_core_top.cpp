// Full-system testbench: the REAL core_top (bridge FSM, pak FSM, save window,
// loader/unloader) + the REAL SoC (VexiiRiscv running the real bootloader and
// a real SDK game) driven by a scripted model of the Pocket host.
//
// The host model implements the APF bridge protocol exactly as core_bridge_cmd
// expects it (see that file): host commands at 0xF8xx00xx ('CM' -> poll 'OK'),
// target commands at 0xF8xx10xx (core posts 'cm'+cmd, host acks 'bu', serves
// the data over plain bridge reads/writes, completes 'ok'+result), datatable
// at 0xF8xx2xxx. Data slots live in an in-memory fake FS; every transaction is
// logged with cycle timestamps. Scenario: boot -> pick savetest.bin -> watch
// the openfile/write flows -> game exits -> picker -> re-pick -> second pass
// verifies persistence. Exit code 0 = all assertions held.
//
// SPDX-License-Identifier: BSD-2-Clause
#include "Vcore_top.h"
#include "Vcore_top_core_top.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <map>
#include <vector>
#include <sys/resource.h>
#include <unistd.h>

static Vcore_top    *top;
static VerilatedVcdC *vcd;
static uint64_t      cyc;         // clk_74a posedge count
static bool          trace_on;

static void tick() {              // one full clk_74a cycle
    top->clk_74a = 0; top->clk_74b = 0;
    top->eval(); if (trace_on) vcd->dump(cyc*2*6734);   // ~6.734ns half period
    top->clk_74a = 1; top->clk_74b = 1;
    top->eval(); if (trace_on) vcd->dump(cyc*2*6734 + 6734);
    cyc++;
    // loader-boundary probe: log the first halfwords the data_loader hands
    // to the SoC (address/data pairs, clk_core domain, sampled per 74a tick)
    static int probe_n = 0; static uint8_t prev_en = 0;
    uint8_t en = top->core_top->pak_wr_en;
    if (en && !prev_en && probe_n < 24) {
        printf("[LDR] #%02d addr=0x%07X data=0x%04X @%lu\n", probe_n,
               (unsigned)top->core_top->pak_wr_addr,
               (unsigned)top->core_top->pak_wr_data, (unsigned long)cyc);
        probe_n++;
    }
    prev_en = en;
    // UART decode on dbg_tx (115200 8N1 @74.25MHz = 645.65 cyc/bit, LSB first)
    static bool uidle = true; static uint64_t u0; static int ubits; static uint32_t usr;
    int rx = top->dbg_tx & 1;
    if (uidle) {
        if (!rx) { uidle = false; u0 = cyc; ubits = 0; usr = 0; }
    } else {
        uint64_t d = cyc - u0;
        if (ubits < 8) {
            if (d >= 323u + 646u * (unsigned)(ubits + 1)) { usr |= (uint32_t)rx << ubits; ubits++; }
        } else if (d >= 323u + 646u * 9u) {
            putchar((int)(usr & 0xFF)); fflush(stdout); uidle = true;
            extern std::string uart_tail;
            uart_tail.push_back((char)(usr & 0xFF));
            if (uart_tail.size() > 120)
                uart_tail.erase(0, uart_tail.size() - 120);
        }
    }
}
std::string uart_tail;
static void ticks(int n) { while (n--) tick(); }

// ---------------------------------------------------------------- bridge ops
// endian_little = 0 on hardware (apf_top leaves it undriven): 32-bit bridge
// words carry the byte at addr+0 in bits [31:24].
static void bwrite(uint32_t addr, uint32_t data) {
    top->bridge_addr = addr; top->bridge_wr_data = data;
    top->bridge_wr = 1; tick(); top->bridge_wr = 0;
    // Pacing: data_loader's mem side drains ONE halfword per ~4 cycles of the
    // 12.375 MHz clock (~24 clk_74a cycles), through a 4-deep CDC fifo with
    // overflow checking OFF — writing faster than ~1 word/48 cycles silently
    // DROPS words (fifo overrun; found when the game's every-3rd-word arrived).
    // The real host is slower still. Command-region writes have no fifo.
    ticks(((addr >> 28) == 0x1 || (addr >> 28) == 0x2) ? 64 : 16);
}
static uint32_t bread(uint32_t addr, int settle = 8) {
    // 0x2xxxxxxx reads go through data_unloader: TWO halfword round trips
    // (addr fifo -> 12.375MHz BRAM (2-cycle) -> data fifo -> assemble), and
    // bridge_rd_data holds the PREVIOUS read's word until BOTH halves arrive
    // — sampling early returns stale data (cost us a 'path=Sa flags=3 size=3'
    // openfile ghost). Real host timing is generous; be generous.
    if ((addr >> 28) == 0x2) settle = 160;
    top->bridge_addr = addr;
    top->bridge_rd = 1; tick(); top->bridge_rd = 0;
    ticks(settle);
    return top->bridge_rd_data;
}

// ------------------------------------------------------------- host command
static void host_cmd(uint16_t cmd, uint32_t p0 = 0, uint32_t p1 = 0) {
    bwrite(0xF8000020, p0);
    bwrite(0xF8000024, p1);
    bwrite(0xF8000000, 0x434D0000u | cmd);       // 'CM'
    for (int i = 0; i < 1000; i++) {
        uint32_t s = bread(0xF8000000, 8);
        if ((s >> 16) == 0x4F4B) return;         // 'OK'
    }
    printf("[TB] FATAL: host cmd %04X never completed\n", cmd);
    exit(2);
}

// --------------------------------------------------------------- fake SD FS
struct FakeFile { std::vector<uint8_t> bytes; };
static std::map<std::string, FakeFile> fs;      // path -> content
// Directory model (matches hardware: openfile creates FILES, never parent
// dirs — a missing dir yields result 3 even with create-if-missing set).
// Saves/riscv_stack is NOT preinstalled here so the sim exercises the
// assets-dir fallback ladder; the family zip ships the real directory.
static std::vector<std::string> fs_dirs = { "Assets/riscv_stack/common" };
static bool dir_exists(const std::string &path) {
    size_t sl = path.rfind('/');
    if (sl == std::string::npos) return true;    // root-level file
    std::string d = path.substr(0, sl);
    for (auto &e : fs_dirs) if (e == d) return true;
    return false;
}
static std::string slot_file[4];                 // slot id -> bound path
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; \
    printf("[TB] FAIL @%lucyc: ", (unsigned long)cyc); printf(__VA_ARGS__); printf("\n"); } \
    else { printf("[TB] ok   @%lucyc: ", (unsigned long)cyc); printf(__VA_ARGS__); printf("\n"); } } while (0)

// ------------------------------------------------- target command servicing
// Polls target_0 once; if the core posted a command, serve it fully.
static int diag_watch(uint32_t *out);            // fwd

static void serve_target_once() {
    uint32_t t0 = bread(0xF8001000, 8);
    if ((t0 >> 16) != 0x636D) return;            // 'cm'
    uint16_t cmd = t0 & 0xFFFF;
    uint32_t p_id  = bread(0xF8001020, 8);
    uint32_t p_off = bread(0xF8001024, 8);
    uint32_t p_bad = bread(0xF8001028, 8);
    uint32_t p_len = bread(0xF800102C, 8);
    uint64_t t_cmd = cyc;
    bwrite(0xF8001000, 0x62750000u | cmd);       // 'bu' = ack
    uint16_t result = 0;

    if (cmd == 0x0140) {                          // ready to run
        printf("[HOST] ready-to-run @%lu\n", (unsigned long)cyc);
    } else if (cmd == 0x0180) {                   // dataslot READ -> bridge writes
        uint16_t id = p_id & 0xFFFF;
        printf("[HOST] read  slot=%u off=%u len=%u -> 0x%08X @%lu\n",
               id, p_off, p_len, p_bad, (unsigned long)cyc);
        auto it = fs.find(slot_file[id & 3]);
        if (it == fs.end()) { result = 2; }
        else {
            auto &b = it->second.bytes;
            for (uint32_t i = 0; i < p_len; i += 4) {
                uint32_t w = 0;
                for (int k = 0; k < 4; k++) {
                    uint32_t src = p_off + i + k;
                    uint8_t byte = (src < b.size()) ? b[src] : 0;
                    w |= (uint32_t)byte << (24 - 8*k);   // addr+0 in [31:24]
                }
                bwrite(p_bad + i, w);
            }
        }
    } else if (cmd == 0x0184) {                   // dataslot WRITE <- bridge reads
        uint16_t id = p_id & 0xFFFF;
        printf("[HOST] write slot=%u off=%u len=%u <- 0x%08X @%lu\n",
               id, p_off, p_len, p_bad, (unsigned long)cyc);
        auto it = fs.find(slot_file[id & 3]);
        if (it == fs.end()) { result = 2; }
        else {
            auto &b = it->second.bytes;
            if (p_off + p_len > b.size()) {
                printf("[HOST]   write beyond EOF (file %zu bytes) -> err\n", b.size());
                result = 5;                       // cannot extend
            } else {
                for (uint32_t i = 0; i < p_len; i += 4) {
                    uint32_t w = bread(p_bad + i);
                    for (int k = 0; k < 4 && i + k < p_len; k++)
                        b[p_off + i + k] = (w >> (24 - 8*k)) & 0xFF;
                }
            }
        }
    } else if (cmd == 0x0190) {                   // dataslot GETFILE (host writes struct)
        uint16_t id = p_id & 0xFFFF;
        uint32_t ptr = p_off;                     // param 1 = resp struct pointer
        std::string path = slot_file[id & 3];
        if (path.empty() || path == "<game>") path = "/Assets/riscv_stack/common/savetest.bin";
        printf("[HOST] getfile slot=%u ptr=0x%08X -> '%s' @%lu\n",
               id, ptr, path.c_str(), (unsigned long)cyc);
        char pbuf[256] = {0};
        strncpy(pbuf, path.c_str(), 255);
        for (int i = 0; i < 256; i += 4) {
            uint32_t w = ((uint32_t)(uint8_t)pbuf[i]   << 24) |
                         ((uint32_t)(uint8_t)pbuf[i+1] << 16) |
                         ((uint32_t)(uint8_t)pbuf[i+2] <<  8) |
                          (uint32_t)(uint8_t)pbuf[i+3];
            bwrite(ptr + i, w);
        }
        result = 0;
    } else if (cmd == 0x0192) {                   // dataslot OPENFILE
        uint16_t id = p_id & 0xFFFF;
        uint32_t ptr = p_off;                     // param 1 = struct pointer
        char path[257] = {0};
        for (int i = 0; i < 256; i += 4) {
            uint32_t w = bread(ptr + i);
            path[i+0] = (w >> 24) & 0xFF; path[i+1] = (w >> 16) & 0xFF;
            path[i+2] = (w >>  8) & 0xFF; path[i+3] = w & 0xFF;
        }
        uint32_t flags = bread(ptr + 0x100);
        uint32_t size  = bread(ptr + 0x104);
        // flags/size are u32 in file byte order: un-reverse ([31:24]=byte0=LSB)
        flags = __builtin_bswap32(flags);
        size  = __builtin_bswap32(size);
        printf("[HOST] openfile slot=%u ptr=0x%08X path='%s' flags=0x%X size=%u @%lu\n",
               id, ptr, path, flags, size, (unsigned long)cyc);
        bool sane = path[0] && strlen(path) < 256;
        for (const char *c = path; *c; c++)
            if ((unsigned char)*c < 0x20 || (unsigned char)*c > 0x7E) sane = false;
        if (!sane) {
            printf("[HOST]   GARBLED PATH -> result 4 (this would likely wedge real firmware!)\n");
            result = 4;
        } else if (!fs.count(path)) {
            if (!dir_exists(path)) {
                printf("[HOST]   parent dir missing -> result 3 (hardware-observed)\n");
                result = 3;
            } else if (flags & 1) {               // create
                fs[path].bytes.assign(size, 0xEE);// junk fill: model "undefined"
                slot_file[id & 3] = path;
                result = 1;                       // created
            } else result = 3;                    // not found
        } else {
            if (flags & 2) fs[path].bytes.resize(size, 0xEE);
            slot_file[id & 3] = path;
            result = 0;                           // opened
        }
    } else {
        printf("[HOST] UNKNOWN target cmd %04X\n", cmd);
        result = 0xFFFF & 5;
    }
    bwrite(0xF8001000, 0x6F6B0000u | result);     // 'ok'
    printf("[HOST]   done result=%u (%lu cyc cmd->done)\n",
           result, (unsigned long)(cyc - t_cmd));
}

// ------------------------------------------------ nonvolatile save modeling
// The real host loads the nonvolatile slot's file into its bridge window at
// core start and reads the window back into the file at quit/power-off/sleep
// (creating it on first flush). Size comes from datatable word 5 if the core
// published one. Derived filename: slot 0's file + ".sav" (parameters bit2).
static std::string nv_savename() { return "Saves/riscv_stack/savetest.sav"; }

static void nv_load() {
    // HARDWARE-OBSERVED (v0.17.7): with a deferload Game slot the derived
    // save name is unresolvable at core start, so the host loads NOTHING
    // into the window — ever. It only binds the slot handle (reads and the
    // quit-flush work). The HAL restores the window itself via dataslot_read.
    uint32_t n = 32768;
    printf("[HOST] nv-load: window untouched (0xFF), slot bound to %s @%lu\n",
           nv_savename().c_str(), (unsigned long)cyc);
    for (uint32_t i = 0; i < n; i += 4)
        bwrite(0x20000000 + i, 0xFFFFFFFFu);   // bit5 init pattern only
    slot_file[3] = nv_savename();              // slot handle now bound
    // and the datatable gets the bound file's size (position 2 -> word 5)
    auto it = fs.find(nv_savename());
    bwrite(0xF8002014, it == fs.end() ? 0 : (uint32_t)it->second.bytes.size());
}

static void nv_flush() {
    // datatable reads have 2-cycle BRAM latency behind an address register
    // that tracks EVERY bridge address: read twice, keep the second
    bread(0xF8002014, 8);
    uint32_t sz = bread(0xF8002014, 8);        // datatable word 5 = save size
    if (sz == 0 || sz > 32768) sz = 32768;
    auto &b = fs[nv_savename()].bytes;
    b.assign(sz, 0);
    for (uint32_t i = 0; i < sz; i += 4) {
        uint32_t w = bread(0x20000000 + i);
        for (int k = 0; k < 4 && i + k < sz; k++)
            b[i + k] = (w >> (24 - 8*k)) & 0xFF;
    }
    printf("[HOST] nv-flush -> %s (%u bytes) @%lu\n", nv_savename().c_str(),
           sz, (unsigned long)cyc);
}

// -------------------------------------------------------------- diag watch
bool     fm_mode = false;
static bool portlib_mode = false;
uint64_t audio_nz_cyc = 0;
int16_t  audio_nz_val = 0;
static uint32_t last_diag = 0;
static std::vector<uint32_t> diag_log;
static void poll_diag() {
    uint32_t d = top->core_top->soc_diag;         // SoC diag GPIO (public scope)
    if (d != last_diag) {
        last_diag = d;
        diag_log.push_back(d);
        printf("[DIAG] 0x%08X @%lu\n", d, (unsigned long)cyc);
    }
#ifdef FM_PROBE
    // FM probe: first nonzero mixed sample proves the whole audio chain
    extern bool fm_mode; extern uint64_t audio_nz_cyc; extern int16_t audio_nz_val;
    if (fm_mode && !audio_nz_cyc) {
        int16_t l = (int16_t)top->core_top->audio_mix_l;
        if (l != 0) { audio_nz_cyc = cyc; audio_nz_val = l;
            printf("[AUD] first nonzero mix sample %d @%lu\n", l, (unsigned long)cyc); }
    }
#endif
    static uint64_t next_hb = 0;                  // heartbeat: where is everyone?
    if (cyc >= next_hb) {
        next_hb = cyc + 2'000'000;
        printf("[HB] @%lu diag=0x%08X pak_state=%u repick=%u skip=%u\n",
               (unsigned long)cyc, d,
               (unsigned)top->core_top->pak_state,
               (unsigned)(top->core_top->game_repick_rst != 0),
               (unsigned)top->core_top->skip_autoload);
    }
}

// Run N cycles while servicing target commands + watching diag.
static void run(uint64_t n) {
    uint64_t end = cyc + n;
    while (cyc < end) {
        serve_target_once();                      // ~50 cycles per poll
        poll_diag();
    }
}
static bool wait_diag(uint32_t code, uint64_t timeout) {
    uint64_t end = cyc + timeout;
    while (cyc < end) {
        serve_target_once();
        poll_diag();
        if (!diag_log.empty() && diag_log.back() == code) return true;
    }
    return false;
}

// ------------------------------------------------------------------- main
int main(int argc, char **argv) {
    // Big verilated eval chains overflow the default 8 MB stack (SIGSEGV with
    // a garbage backtrace). Raise our own limit and re-exec once.
    struct rlimit rl;
    if (!getenv("SIM_RESTACKED") && getrlimit(RLIMIT_STACK, &rl) == 0) {
        rlim_t want = 512ul << 20;
        if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < want) want = rl.rlim_max;
        if (rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur < want) {
            rl.rlim_cur = want;
            if (setrlimit(RLIMIT_STACK, &rl) == 0) {
                setenv("SIM_RESTACKED", "1", 1);
                execv("/proc/self/exe", argv);
            }
        }
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);   // don't lose evidence to buffering
    Verilated::commandArgs(argc, argv);
    top = new Vcore_top;
    trace_on = false;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--trace")) trace_on = true;
    if (trace_on) {
        Verilated::traceEverOn(true);
        vcd = new VerilatedVcdC;
        top->trace(vcd, 99);
        vcd->open("core_top.vcd");
    }

    // load the game binary
    const char *game_path = "../../sdk/savetest/savetest.bin";
    for (int i = 1; i < argc - 1; i++) if (!strcmp(argv[i], "--game")) game_path = argv[i+1];
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--fm")) fm_mode = true;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--portlib")) portlib_mode = true;
    for (int i = 1; i < argc - 1; i++) if (!strcmp(argv[i], "--pak")) {
        FILE *pf = fopen(argv[i+1], "rb");
        if (!pf) { printf("[TB] cannot open pak %s\n", argv[i+1]); return 2; }
        FakeFile pakf;
        for (int c; (c = fgetc(pf)) != EOF; ) pakf.bytes.push_back((uint8_t)c);
        fclose(pf);
        fs["<pak>"] = pakf;
        slot_file[1] = "<pak>";
        printf("[TB] pak: %s (%zu bytes)\n", argv[i+1], fs["<pak>"].bytes.size());
    }
    FILE *g = fopen(game_path, "rb");
    if (!g) { printf("[TB] cannot open %s\n", game_path); return 2; }
    FakeFile gamef;
    for (int c; (c = fgetc(g)) != EOF; ) gamef.bytes.push_back((uint8_t)c);
    fclose(g);
    fs["<game>"] = gamef;
    slot_file[0] = "<game>";
    printf("[TB] game: %s (%zu bytes)\n", game_path, fs["<game>"].bytes.size());

    top->cont1_key = 0; top->cont2_key = 0;
    top->bridge_endian_little = 0;
    top->dbg_rx = 1;                  // UART RX idles HIGH (0 = break = IRQ storm)

    // ---- Pocket boot sequence (host side) ----
    ticks(100);
    host_cmd(0x0010);                             // reset enter
    ticks(50);
    host_cmd(0x0011);                             // reset exit -> SoC boots
    printf("[TB] reset released @%lu\n", (unsigned long)cyc);

    // bootloader: sdram init + memtest + picker. Let it reach the pick loop.
    run(3'000'000);

    // ---- pick the game: datatable size + dataslot_update(id=2) ----
    uint32_t gsize = (uint32_t)fs["<game>"].bytes.size();
    bwrite(0xF8002004, gsize);                    // Game = position 0 -> word 1
    nv_load();                                    // host loads the nonvolatile save
    host_cmd(0x008A, /*id*/0, /*size*/gsize);     // triggers core_top repick reset
    printf("[TB] game picked (dataslot_update id=2 size=%u) @%lu\n",
           gsize, (unsigned long)cyc);

    if (slot_file[1] == "<pak>") {
        // post the Pak slot's size in the datatable (position 1 -> word 3)
        bwrite(0xF800200C, (uint32_t)fs["<pak>"].bytes.size());
    }

    if (portlib_mode) {
        // ---- portlib scenario: pakfstest reports via 0x9AC0xxxx diags ----
        if (!wait_diag(0x9AC000F0, 120'000'000)) {
            printf("[TB] FAIL: pakfstest never completed (last diag 0x%08X)\n", last_diag);
            fails++;
        } else {
            for (uint32_t d : diag_log)
                CHECK(d != 0x9AC00BAD, "portlib step 0x%08X", d);
        }
        goto out;
    }

    if (getenv("RVSTACK_TYRIAN")) {
        // ---- Tyrian demo-hang hunt at RTL fidelity ----------------------
        // pak streams over the real bridge (~142M cycles); warm-boot menu
        // input is scheduled off the UART mount message; beacons arrive on
        // diag (0xBEAC000n). If stage 3 sticks, the heartbeat's CPU
        // read-address probe names the memory the core is hammering.
        printf("[TB] TYRIAN scenario: waiting for pak mount over UART...\n");
        uint64_t t_mount = 0;
        while (!t_mount && cyc < 400'000'000) {
            serve_target_once(); poll_diag();
            if (uart_tail.find("pak mounted") != std::string::npos)
                t_mount = cyc;
        }
        if (!t_mount) { printf("[TB] FAIL: pak never mounted\n"); fails++; goto out; }
        printf("[TB] mounted @%lu — scheduling menu input\n", (unsigned long)t_mount);
        struct { uint64_t at; uint16_t bit; } script[] = {
            { t_mount +  60'000'000, 1u << 15 },   // START: leave title
            { t_mount + 120'000'000, 1u << 15 },   // START: (safety re-press)
            { t_mount + 170'000'000, 1u << 1  },   // DOWN
            { t_mount + 200'000'000, 1u << 1  },   // DOWN
            { t_mount + 230'000'000, 1u << 15 },   // START: select Demo
        };
        size_t si = 0;
        uint64_t press_end = 0, hb = 0;
        uint32_t beac = 0, ar_last = 0;
        uint64_t beac_t = 0;
        while (cyc < t_mount + 500'000'000) {
            serve_target_once(); poll_diag();
            if ((last_diag >> 16) == 0xBEAC && (last_diag & 0xFF) != beac) {
                beac = last_diag & 0xFF; beac_t = cyc;
                printf("[TB] beacon stage %u @%lu\n", beac, (unsigned long)cyc);
                if (beac >= 6) { printf("[TB] demo level load COMPLETED — no repro\n"); break; }
            }
            if (si < 5 && cyc >= script[si].at) {
                top->cont1_key = script[si].bit;
                press_end = cyc + 5'000'000;
                printf("[TB] press 0x%04X @%lu\n", script[si].bit, (unsigned long)cyc);
                si++;
            }
            if (press_end && cyc >= press_end) { top->cont1_key = 0; press_end = 0; }
            if (top->rootp) { /* AR probe sampled in heartbeat below */ }
            if (cyc >= hb) {
                hb = cyc + 20'000'000;
                printf("[TB-HB] @%lu beacon=%u last_diag=0x%08X\n",
                       (unsigned long)cyc, beac, last_diag);
            }
            // stall detector: stage 3 with no progress for 80M cycles
            if (beac == 3 && cyc - beac_t > 80'000'000) {
                printf("[TB] *** REPRODUCED: stage 3 stalled >80M cycles ***\n");
                fails++;
                break;
            }
        }
        goto out;
    }

    if (getenv("RVSTACK_MIDRESET")) {
        // ---- reproduce the field wedge: re-pick DURING heavy activity ----
        // savetest pass 1 is busy (window writes, dataslot traffic). Fire the
        // host's game re-pick mid-flight; the SoC must reset and reboot to
        // the picker (diag 0xB0070001) then relaunch. Field: core never
        // came back (black, dead to reload, recovered only by full relaunch).
        if (!wait_diag(0xD1A60101, 60'000'000) &&
            !wait_diag(0xD1A60100, 1'000'000)) {
            printf("[TB] FAIL: never reached save_open result\n");
            fails++; goto out;
        }
        printf("[TB] === firing MID-ACTIVITY re-pick @%lu ===\n", (unsigned long)cyc);
        diag_log.clear();
        host_cmd(0x008A, 0, gsize);
        if (!wait_diag(0xB0070001, 40'000'000)) {
            printf("[TB] REPRODUCED: SoC never rebooted after mid-activity re-pick (last diag 0x%08X)\n", last_diag);
            fails++;
        } else {
            printf("[TB] reboot OK; waiting for game relaunch...\n");
            if (!wait_diag(0xD1A60001, 40'000'000)) {
                printf("[TB] FAIL: bootloader up but game never relaunched (last diag 0x%08X)\n", last_diag);
                fails++;
            } else
                printf("[TB] mid-activity re-pick fully recovered.\n");
        }
        goto out;
    }

    if (getenv("RVSTACK_BENCH")) {
        // ---- fbbench: wait for done, then just dump the diag history ----
        if (!wait_diag(0xFB0000F0, 120'000'000)) {
            printf("[TB] FAIL: fbbench never completed (last diag 0x%08X)\n", last_diag);
            fails++;
        } else {
            static const char *names[8] = {0, "memcpy64K", "flush76800",
                "fb_present", "copy+present", "avg-of-10", "BLIT64K", "blit-present"};
            for (uint32_t d : diag_log)
                if ((d >> 24) == 0xFB && ((d >> 16) & 0xFF) >= 1 && ((d >> 16) & 0xFF) <= 7)
                    printf("[BENCH] %-13s %u us\n",
                           names[(d >> 16) & 0xFF], (d & 0xFFFF) * 16);
        }
        goto out;
    }

    if (fm_mode) {
        // ---- FM scenario: fmtest patches ch0 + keys middle C; we assert the
        // debug word ([15]=nz [14]=valid [13:10]=kon) AND an audible mix.
        if (!wait_diag(0xF3D000F0, 90'000'000)) {
            printf("[TB] FAIL: fmtest never completed (last diag 0x%08X)\n", last_diag);
            fails++;
        } else {
            uint32_t caps = 0, dbg = 0;
            for (uint32_t d : diag_log) {
                if ((d & 0xFFFFFF00) == 0xF3D00100) caps = d & 0xFF;
                if ((d & 0xFFFFFC00) == 0xF3D00400) dbg  = d & 0xFF;
            }
            CHECK(caps & 0x10, "sys_caps reports HAL_FEAT_FM (feat low byte 0x%02X)", caps);
            CHECK(dbg & 0x80, "OPL3 produced a NONZERO sample (dbg 0x%02X)", dbg);
            CHECK(dbg & 0x40, "OPL3 sample_valid ticking (dbg 0x%02X)", dbg);
            CHECK((dbg >> 2) & 0x1, "channel 0 kon visible on led (dbg 0x%02X)", dbg);
#ifdef FM_PROBE
            CHECK(audio_nz_cyc != 0, "nonzero sample reached the i2s mix (val=%d)", audio_nz_val);
#endif
        }
        goto out;
    }

    // ---- pass 1: boot -> load game -> save_open(created) -> commit -> exit
    if (!wait_diag(0xD1A600F0, 60'000'000)) {
        printf("[TB] FAIL: pass 1 never completed (last diag 0x%08X)\n", last_diag);
        fails++;
        goto out;
    }
    printf("[TB] === pass 1 complete ===\n");
    // persistence is the HOST's act (quit/sleep): nothing on "SD" yet is fine

    // game exited -> hardware reset -> bootloader shows picker (skip_autoload).
    // Give it time, then re-pick the same game for pass 2.
    run(3'000'000);
    // host behavior between sessions: flush the nonvolatile slot, then (for
    // the re-pick of the same game) reload it — worst case also power-cycle
    nv_flush();
    nv_load();
    host_cmd(0x008A, 0, gsize);
    printf("[TB] re-picked for pass 2 @%lu\n", (unsigned long)cyc);

    if (!wait_diag(0xD1A600F1, 80'000'000)) {
        printf("[TB] FAIL: pass 2 never completed (last diag 0x%08X)\n", last_diag);
        fails++;
        goto out;
    }
    printf("[TB] === pass 2 complete ===\n");
    {
        bool verified = false, second_ok = false;
        for (uint32_t d : diag_log) {
            if (d == 0xD1A600DD) verified = true;
            if (d == 0xD1A60200 || d == 0xD1A60201) second_ok = true;
        }
        CHECK(verified, "pass 2 verified the pattern loaded back from the .sav");
        CHECK(second_ok, "second save file opened");
        CHECK(fs.count(nv_savename()) == 1, "host created the derived .sav at flush");
        // final host flush, then check both TOC files landed in the image
        nv_flush();
        auto &b = fs[nv_savename()].bytes;
        uint32_t magic = b.size() >= 4 ?
            (b[0] | (b[1]<<8) | (b[2]<<16) | ((uint32_t)b[3]<<24)) : 0;
        CHECK(magic == 0x56535652, "window TOC magic in .sav (got 0x%08X)", magic);
    }

out:
    if (trace_on) vcd->close();
    printf("\n[TB] ======== %s (%d failures, %lu cycles) ========\n",
           fails ? "FAILED" : "PASSED", fails, (unsigned long)cyc);
    // dump diag history
    for (uint32_t d : diag_log) printf("[TB] diag history: 0x%08X\n", d);
    return fails ? 1 : 0;
}
