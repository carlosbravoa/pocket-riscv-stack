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
        }
    }
}
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

// -------------------------------------------------------------- diag watch
bool     fm_mode = false;
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
    FILE *g = fopen(game_path, "rb");
    if (!g) { printf("[TB] cannot open %s\n", game_path); return 2; }
    FakeFile gamef;
    for (int c; (c = fgetc(g)) != EOF; ) gamef.bytes.push_back((uint8_t)c);
    fclose(g);
    fs["<game>"] = gamef;
    slot_file[2] = "<game>";
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
    bwrite(0xF800200C, gsize);                    // index1*2+1 = word 3: game size
    host_cmd(0x008A, /*id*/2, /*size*/gsize);     // triggers core_top repick reset
    printf("[TB] game picked (dataslot_update id=2 size=%u) @%lu\n",
           gsize, (unsigned long)cyc);

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
    {
        auto &f1 = fs["Assets/riscv_stack/common/simtest.sav"];
        CHECK(f1.bytes.size() == 8192 + 4, "file sized cap+4 (got %zu)", f1.bytes.size());
        if (f1.bytes.size() >= 24) {
            uint32_t w0 = f1.bytes[0] | (f1.bytes[1]<<8) | (f1.bytes[2]<<16) | ((uint32_t)f1.bytes[3]<<24);
            CHECK(w0 == 0x53494D31, "committed magic little-endian in file (got 0x%08X)", w0);
            uint32_t w5 = f1.bytes[20] | (f1.bytes[21]<<8) | (f1.bytes[22]<<16) | ((uint32_t)f1.bytes[23]<<24);
            CHECK(w5 == 0xA5000005, "pattern word 5 (got 0x%08X)", w5);
        }
    }

    // game exited -> hardware reset -> bootloader shows picker (skip_autoload).
    // Give it time, then re-pick the same game for pass 2.
    run(3'000'000);
    host_cmd(0x008A, 2, gsize);
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
        CHECK(verified, "pass 2 verified the pattern read back from the fake SD");
        CHECK(second_ok, "second save file opened");
        CHECK(fs.count("Assets/riscv_stack/common/simtest2.sav") == 1, "simtest2.sav exists (fallback dir)");
        auto &f1 = fs["Assets/riscv_stack/common/simtest.sav"];
        if (f1.bytes.size() >= 8) {
            uint32_t w1 = f1.bytes[4] | (f1.bytes[5]<<8) | (f1.bytes[6]<<16) | ((uint32_t)f1.bytes[7]<<24);
            CHECK(w1 == (0xA5000001 ^ 0xFF), "rebind-back commit landed (word1=0x%08X)", w1);
        }
    }

out:
    if (trace_on) vcd->close();
    printf("\n[TB] ======== %s (%d failures, %lu cycles) ========\n",
           fails ? "FAILED" : "PASSED", fails, (unsigned long)cyc);
    // dump diag history
    for (uint32_t d : diag_log) printf("[TB] diag history: 0x%08X\n", d);
    return fails ? 1 : 0;
}
