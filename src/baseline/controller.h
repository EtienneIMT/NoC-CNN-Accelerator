#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "systemc.h"
#include <stdint.h>
#include "memory_map.h"

// --- MACROS FOR THE CONTROL PLANE ---
#define PKT_TYPE_WEIGHT 0
#define PKT_TYPE_BIAS   1
#define PKT_TYPE_FM     2 // Feature Map (Image)

#define FLIT_HEADER 2
#define FLIT_BODY   0
#define FLIT_TAIL   1

SC_MODULE( Controller ) {
    sc_in  < bool >  rst;
    sc_in  < bool >  clk;
    
    // --- DMA CONTROL INTERFACE ---
    sc_out< uint32_t > ctrl_addr;
    sc_out< uint32_t > ctrl_len;
    sc_out< bool >     ctrl_read_req;
    sc_out< bool >     ctrl_write_req;
    sc_in < bool >     ctrl_done;

    // --- DATA INTERFACE FROM DMA (Read) ---
    sc_in < float >    data_out;
    sc_in < bool >     data_out_valid;
    sc_out< bool >     data_out_ready;

    // --- DATA INTERFACE TO DMA (Write results) ---
    sc_out< float >    data_in;
    sc_out< bool >     data_in_valid;
    sc_in < bool >     data_in_ready;
    
    // --- NoC Interface (to Router 0) ---
    sc_out < sc_lv<34> > flit_tx;
    sc_out < bool > req_tx;
    sc_in  < bool > ack_tx;

    sc_in  < sc_lv<34> > flit_rx;
    sc_in  < bool > req_rx;
    sc_out < bool > ack_rx;

    // --- Internal variables ---
    int my_id;
    
    // Method to generate an "intelligent" Header Flit
    sc_lv<34> create_header(int dest, int src, int pkt_type);

    // Method to convert a float into a flit (Body/Tail)
    sc_lv<34> create_data_flit(float val, bool is_tail);

    // Function to automate ROM read -> NoC injection
    void send_parameter_stream(uint32_t start_addr, int dest_pe, int total_count, int pkt_type);

    void master_thread();

    public:
        // --- Hardware Metrics ---
        int max_sram_fifo_size;
        unsigned long long total_mac_operations;
        unsigned long long total_pe_compute_cycles;

    SC_CTOR( Controller ) {
        my_id = 0; // By convention the Controller is attached to Router 0
        max_sram_fifo_size = 0;
        total_mac_operations = 0;
        total_pe_compute_cycles = 0;
        
        SC_THREAD( master_thread );
        sensitive << clk.pos();
        dont_initialize();
    }
};

#endif