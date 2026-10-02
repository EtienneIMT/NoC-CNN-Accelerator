#include "DRAM.h"
#include <sstream>

using namespace std;

// ====================================================================
// 1. MEMORY INITIALIZATION (Only once at the beginning)
// ====================================================================
void DRAM::load_layer(int layer_idx, bool is_bias, uint32_t base_addr) {
    std::stringstream ss;
    ss << layer_idx;
    string prefix = (layer_idx <= 5) ? "conv" : "fc";
    string type = is_bias ? "_bias.txt" : "_weight.txt";
    string filename = DATA_PATH + prefix + ss.str() + type;

    ifstream file(filename.c_str());
    if (!file.is_open()) {
        cerr << "[DRAM] Error: Cannot open " << filename << endl;
        return;
    }

    float value;
    uint32_t current_addr = base_addr;
    while (file >> value) {
        memory[current_addr] = value;
        current_addr += 4; // Advance by 4 bytes per float
    }
    file.close();
    // cout << "[DRAM] Loaded " << filename << " at 0x" << std::hex << base_addr << std::dec << endl;
}

void DRAM::load_data_from_files() {
    cout << "[DRAM] Loading data into memory map..." << endl;
    
    // 1. Dynamically retrieve the image filename
    const char* env_file = getenv("IMAGE_FILE_NAME");
    string image_name = (env_file != NULL) ? env_file : "cat.txt";

    // 2. Load the correct file
    string img_file = DATA_PATH + image_name; 
    ifstream file(img_file.c_str());
    
    if (!file.is_open()) {
        cerr << "[DRAM] Error: Cannot open " << img_file << endl;
    } else {
        float val;
        uint32_t addr = MEM_ADDR_IMAGE;
        while(file >> val) {
            memory[addr] = val;
            addr += 4;
        }
        file.close();
        cout << "[DRAM] Successfully loaded " << image_name << endl;
    }

    // Load weights and biases using the exact addresses defined in memory_map.h
    // Layer 1 (Conv1)
    load_layer(1, false, MEM_ADDR_W_L1); 
    load_layer(1, true,  MEM_ADDR_B_L1);
    
    // Layer 2 (Conv2)
    load_layer(2, false, MEM_ADDR_W_L2); 
    load_layer(2, true,  MEM_ADDR_B_L2);
    
    // Layer 3 (Conv3)
    load_layer(3, false, MEM_ADDR_W_L3); 
    load_layer(3, true,  MEM_ADDR_B_L3);
    
    // Layer 4 (Conv4)
    load_layer(4, false, MEM_ADDR_W_L4); 
    load_layer(4, true,  MEM_ADDR_B_L4);
    
    // Layer 5 (Conv5)
    load_layer(5, false, MEM_ADDR_W_L5); 
    load_layer(5, true,  MEM_ADDR_B_L5);
    
    // Layer 6 (FC6)
    load_layer(6, false, MEM_ADDR_W_L6); 
    load_layer(6, true,  MEM_ADDR_B_L6);
    
    // Layer 7 (FC7)
    load_layer(7, false, MEM_ADDR_W_L7); 
    load_layer(7, true,  MEM_ADDR_B_L7);
    
    // Layer 8 (FC8)
    load_layer(8, false, MEM_ADDR_W_L8); 
    load_layer(8, true,  MEM_ADDR_B_L8);

    cout << "[DRAM] Initialization complete." << endl;
    cout << "[PROBE 1] DRAM Content at L1 Weight Start (0x01000000) = " << memory[MEM_ADDR_W_L1] << endl;
}

// ====================================================================
// 2. READ PROCESS (AXI4 AR & R Channels)
// ====================================================================
void DRAM::read_process() {
    // Initialisation au Reset
    arready.write(0);
    rvalid.write(0);
    rlast.write(0);
    rdata.write(0);
    wait();

    while(true) {
        // Step 1: Wait for an address request (ARVALID == 1)
        while (arvalid.read() == 0) {
            wait();
        }

        // Retrieve the address and burst length
        uint32_t current_addr = araddr.read();
        uint32_t burst_len = arlen.read(); // AXI spec: burst_len = nb_transferts - 1

        // Handshake: Accept the address
        arready.write(1);
        wait();
        arready.write(0);

        // Step 2: Send the data (R Channel)
        for (uint32_t i = 0; i <= burst_len; ++i) {
            // Put the data on the bus and assert RVALID
            rdata.write(memory[current_addr]);
            rvalid.write(1);
            rlast.write((i == burst_len) ? 1 : 0); // Lever RLAST sur le dernier beat

            // Wait until the master is ready to receive
            do {
                wait();
            } while (rready.read() == 0);

            // Increment the address for the next beat (4 bytes = 1 float)
            current_addr += 4; 
        }

        // End of burst, deassert the signals
        rvalid.write(0);
        rlast.write(0);
    }
}

// ====================================================================
// 3. WRITE PROCESS (AXI4 AW, W & B Channels)
// ====================================================================
void DRAM::write_process() {
    // Initialisation au Reset
    awready.write(0);
    wready.write(0);
    bvalid.write(0);
    wait();

    while(true) {
        // Step 1: Wait for a write address request
        while (awvalid.read() == 0) {
            wait();
        }

        uint32_t current_addr = awaddr.read();
        uint32_t burst_len = awlen.read();

        awready.write(1);
        wait();
        awready.write(0);

        // Step 2: Receive the data
        for (uint32_t i = 0; i <= burst_len; ++i) {
            wready.write(1); // We are ready to receive
            
            // Wait until the master sends valid data
            do {
                wait();
            } while (wvalid.read() == 0);

            // Write to memory
            memory[current_addr] = wdata.read();
            
            // Note: Strictly speaking, we should verify wlast.read() on the last iteration
            current_addr += 4;
            wready.write(0); 
            // An extra wait() could be added if we want to simulate a slower DRAM
        }

        // Step 3: Send the B response (Transaction complete)
        bvalid.write(1);
        do {
            wait();
        } while (bready.read() == 0);
        bvalid.write(0);
    }
}