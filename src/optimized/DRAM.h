#ifndef DRAM_H
#define DRAM_H

#include "systemc.h"
#include "memory_map.h" // Ton fichier d'adresses
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <string>

SC_MODULE( DRAM ) {
    // --- Global Ports ---
    sc_in<bool> clk;
    sc_in<bool> rst;
 
    // ==========================================
    // AXI4 SLAVE PORTS
    // ==========================================
    
    // 1. Read Address Channel (AR)
    sc_in < uint32_t > araddr;
    sc_in < uint32_t > arlen;   // Number of transfers in the burst (-1)
    sc_in < bool >     arvalid;
    sc_out< bool >     arready;

    // 2. Read Data Channel (R)
    sc_out< float >    rdata;   // Using float to simplify connection with computations
    sc_out< bool >     rlast;   // Indicates the last data of the burst
    sc_out< bool >     rvalid;
    sc_in < bool >     rready;

    // 3. Write Address Channel (AW)
    sc_in < uint32_t > awaddr;
    sc_in < uint32_t > awlen;
    sc_in < bool >     awvalid;
    sc_out< bool >     awready;

    // 4. Write Data Channel (W)
    sc_in < float >    wdata;
    sc_in < bool >     wlast;
    sc_in < bool >     wvalid;
    sc_out< bool >     wready;

    // 5. Write Response Channel (B)
    sc_out< bool >     bvalid;
    sc_in < bool >     bready;

    // ==========================================
    // INTERNAL STORAGE
    // ==========================================
    // Use unordered_map to store only used addresses (sparse memory)
    std::unordered_map<uint32_t, float> memory;
    std::string DATA_PATH;

    // ==========================================
    // PROCESSES AND FUNCTIONS
    // ==========================================
    void read_process();
    void write_process();
    void load_data_from_files(); // Initial data loading
    void load_layer(int layer_idx, bool is_bias, uint32_t base_addr);

    SC_CTOR( DRAM )
    {
        DATA_PATH = "./data/";

        // Load all txt files BEFORE the start of the simulation
        load_data_from_files();

        SC_THREAD(read_process);
        sensitive << clk.pos();
        dont_initialize(); // Wait for reset

        SC_THREAD(write_process);
        sensitive << clk.pos();
        dont_initialize();
    }
};

#endif