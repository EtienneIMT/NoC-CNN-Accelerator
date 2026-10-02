#ifndef DMA_H
#define DMA_H

#include "systemc.h"
#include <stdint.h>

SC_MODULE( DMA ) {
    sc_in<bool> clk;
    sc_in<bool> rst;

    // ==========================================
    // COMMAND INTERFACE (From/To the Controller)
    // ==========================================
    sc_in < uint32_t > ctrl_addr;       // Requested start address
    sc_in < uint32_t > ctrl_len;        // TOTAL number of floats to transfer
    sc_in < bool >     ctrl_read_req;   // Pulse to start a read DRAM -> SRAM
    sc_in < bool >     ctrl_write_req;  // Pulse to start a write SRAM -> DRAM
    sc_out< bool >     ctrl_done;       // Pulse when the whole transfer is finished

    // ==========================================
    // READ DATA INTERFACE (DMA -> Controller/SRAM)
    // Operates with a Valid/Ready handshake like in Lab 3
    // ==========================================
    sc_out< float >    data_out;
    sc_out< bool >     data_out_valid;
    sc_in < bool >     data_out_ready;

    // ==========================================
    // WRITE DATA INTERFACE (Controller/SRAM -> DMA)
    // ==========================================
    sc_in < float >    data_in;
    sc_in < bool >     data_in_valid;
    sc_out< bool >     data_in_ready;

    // ==========================================
    // AXI4 MASTER PORTS (To DRAM)
    // ==========================================
    // Read Channels (AR, R)
    sc_out< uint32_t > araddr;
    sc_out< uint32_t > arlen;
    sc_out< bool >     arvalid;
    sc_in < bool >     arready;

    sc_in < float >    rdata;
    sc_in < bool >     rlast;
    sc_in < bool >     rvalid;
    sc_out< bool >     rready;

    // Write Channels (AW, W, B)
    sc_out< uint32_t > awaddr;
    sc_out< uint32_t > awlen;
    sc_out< bool >     awvalid;
    sc_in < bool >     awready;

    sc_out< float >    wdata;
    sc_out< bool >     wlast;
    sc_out< bool >     wvalid;
    sc_in < bool >     wready;

    sc_in < bool >     bvalid;
    sc_out< bool >     bready;

    // ==========================================
    // PROCESS
    // ==========================================
    void master_read_thread();
    void master_write_thread();

    public:
        unsigned long long dram_bytes_read;
        unsigned long long dram_bytes_written;

    SC_CTOR( DMA ) {
        dram_bytes_read = 0;
        dram_bytes_written = 0;

        SC_THREAD(master_read_thread);
        sensitive << clk.pos();
        dont_initialize();

        SC_THREAD(master_write_thread);
        sensitive << clk.pos();
        dont_initialize();
    }
};

#endif