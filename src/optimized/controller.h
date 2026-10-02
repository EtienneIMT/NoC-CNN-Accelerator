#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <systemc.h>
#include "memory_map.h"
#include <stdint.h>
#include <queue>
#include <vector>
#include <cstdint>

#define FLIT_BODY 0
#define FLIT_TAIL 1
#define FLIT_HEADER 2
#define PKT_TYPE_WEIGHT 0
#define PKT_TYPE_BIAS 1
#define PKT_TYPE_FM 2

SC_MODULE(Controller) {
    // --- Clock and Reset ---
    sc_in<bool> clk;
    sc_in<bool> rst;

    // --- DMA signals ---
    sc_out<uint32_t> ctrl_addr;
    sc_out<uint32_t>      ctrl_len;
    sc_out<bool>     ctrl_read_req;
    sc_out<bool>     ctrl_write_req;
    sc_in <bool>     ctrl_done;

    sc_in<float>     data_out;
    sc_in<bool>      data_out_valid;
    sc_out<bool>     data_out_ready;

    sc_out<float>    data_in;
    sc_out<bool>     data_in_valid;
    sc_in<bool>      data_in_ready;

    // =======================================================
    // NEW HARDWARE INTERFACES (Dual-NIC)
    // =======================================================
    
    // --- NoC Port 0 (Connection to Router 0 - Top line) ---
    sc_out<sc_lv<130>> flit_tx_0;
    sc_out<bool>       req_tx_0;
    sc_in<bool>        ack_tx_0;

    // --- NoC Port 4 (Connection to Router 4 - Middle line) ---
    sc_out<sc_lv<130>> flit_tx_4;
    sc_out<bool>       req_tx_4;
    sc_in<bool>        ack_tx_4;

    // The receive port remains unique (results return to Router 0)
    sc_in<sc_lv<130>>  flit_rx;
    sc_in<bool>        req_rx;
    sc_out<bool>       ack_rx;

    // --- Internal variables ---
    int my_id;
    int max_sram_fifo_size;

    // =======================================================
    // INTERNAL BUFFER MEMORIES (Dispatcher FIFOs)
    // =======================================================
    std::queue<sc_lv<130>> flit_queue_0;
    std::queue<sc_lv<130>> flit_queue_4;

    // --- Process declarations (SystemC threads) ---
    void master_thread();  // The Producer (reads DMA, creates flits, fills queues)
    void tx_thread_0();    // Consumer 0 (drains queue 0 to Router 0)
    void tx_thread_4();    // Consumer 4 (drains queue 4 to Router 4)

    // --- Utility functions ---
    sc_lv<130> create_header(int dest, int src, int pkt_type);
    sc_lv<130> create_data_flit(float v0, float v1, float v2, float v3, bool is_tail);
    void send_parameter_stream(uint32_t start_addr, int dest_pe, int total_count, int pkt_type);

    SC_CTOR(Controller) {
        my_id = 0;
        max_sram_fifo_size = 0;

        SC_THREAD(master_thread);
        sensitive << clk.pos();

        SC_THREAD(tx_thread_0);
        sensitive << clk.pos();

        SC_THREAD(tx_thread_4);
        sensitive << clk.pos();
    }
};

#endif