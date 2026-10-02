#include <fstream>

#include "controller.h"
#include <vector>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <queue>
#include <fstream>
#include <string>

sc_lv<34> Controller::create_header(int dest, int src, int pkt_type) {
    sc_lv<34> h;
    h.range(33, 32) = FLIT_HEADER;
    h.range(31, 28) = dest;
    h.range(27, 24) = src;
    h.range(23, 20) = pkt_type;
    h.range(19, 0)  = 0;
    return h;
}

sc_lv<34> Controller::create_data_flit(float val, bool is_tail) {
    sc_lv<34> f;
    f.range(33, 32) = is_tail ? FLIT_TAIL : FLIT_BODY;
    unsigned int ival;
    std::memcpy(&ival, &val, sizeof(float)); 
    f.range(31, 0) = ival;
    return f;
}

void Controller::send_parameter_stream(uint32_t start_addr, int dest_pe, int total_count, int pkt_type) {
    std::cout << "@ " << sc_time_stamp() << " [CTRL] -> Sending Header to PE " << dest_pe << " (Type: " << pkt_type << ")\n";

    // 1. SEND THE HEADER FIRST (To open the route before the flood)
    flit_tx.write(create_header(dest_pe, my_id, pkt_type));
    req_tx.write(1);
    wait();

    int wait_header = 0;
    while(ack_tx.read() == 0) {
        wait_header++;
        if(wait_header % 5000 == 0) {
            std::cout << "@ " << sc_time_stamp() << " [WARNING] Controller stuck waiting for Header ACK_TX to go HIGH..." << std::endl;
        }
        wait();
    }
    req_tx.write(0); // Pause

    std::cout << "@ " << sc_time_stamp() << " [CTRL] -> Header ACKed. Requesting DMA...\n";

    // 2. REQUEST TO DMA (Pulse)
    ctrl_addr.write(start_addr);
    ctrl_len.write(total_count);
    ctrl_read_req.write(1);
    wait();
    ctrl_read_req.write(0);

    // 3. SYNCHRONOUS FIFO BUFFER (Absorb DMA + Inject into NoC)
    // This "fifo" abstractly represents your global on-chip SRAM
    std::queue<float> fifo;
    int dma_read_count = 0;
    int noc_send_count = 0;
    bool flit_in_transit = false;

    // The controller is always ready to accept data into its global SRAM
    data_out_ready.write(1);

    while (noc_send_count < total_count) {
        // A. Read from DMA
        if (data_out_valid.read() == 1 && dma_read_count < total_count) {
            fifo.push(data_out.read());
            dma_read_count++;

            // --- METRIC ADDED ---
            if (fifo.size() > max_sram_fifo_size) {
                max_sram_fifo_size = fifo.size();
            }
        }

        if (dma_read_count == 1 && pkt_type == PKT_TYPE_WEIGHT) {
        std::cout << "[PROBE 2] Controller received first weight from DMA: " << fifo.front() << std::endl;
        }

        // B. If the router accepted the flit in the previous cycle
        if (flit_in_transit) {
            if (ack_tx.read() == 1) { 
                fifo.pop();
                noc_send_count++;
                flit_in_transit = false;
                req_tx.write(0);
            }
        }

        // C. Inject a new flit if the line is free and the buffer is not empty
        if (!flit_in_transit && !fifo.empty()) {
            float val = fifo.front();
            bool is_tail = (noc_send_count == total_count - 1);
            
            flit_tx.write(create_data_flit(val, is_tail));
            req_tx.write(1);
            flit_in_transit = true;
        }

        wait(); // Advance the clock by one cycle

        if (noc_send_count > 0 && noc_send_count % 500000 == 0 && !flit_in_transit) {
            std::cout << "@ " << sc_time_stamp() << " [CTRL] Sent to NoC: " 
                      << noc_send_count << "/" << total_count 
                      << " | FIFO size: " << fifo.size() << "\n";
        }
    }
    
    // --- MARQUEUR 3 ---
    std::cout << "@ " << sc_time_stamp() << " [CTRL] -> Finished payload injection. Cleaning up...\n";

    // 4. CLEANUP
    req_tx.write(0);
    data_out_ready.write(0); // On n'accepte plus de données
    
    // On attend juste quelques cycles pour laisser le Tail Flit 
    // se propager tranquillement dans le réseau jusqu'au PE.
    for(int i = 0; i < 5; i++) wait();
    
}

void Controller::master_thread() {
    // Initial reset
    ctrl_addr.write(0);
    ctrl_len.write(0);
    ctrl_read_req.write(0);
    ctrl_write_req.write(0);
    data_out_ready.write(0);
    data_in_valid.write(0);
    data_in.write(0);
    req_tx.write(0);
    ack_rx.write(0);
    wait();
    while (rst.read() == 1) wait();
    wait();

    // Configuration: {dest_pe, W_addr, W_size, B_addr, B_size}
    // Reuses your HW4 topology but with the physical memory map
    struct LayerCfg {
        int dest; uint32_t w_addr; int w_size; uint32_t b_addr; int b_size;
    };
    
    LayerCfg cfg[8] = {
        {1,  MEM_ADDR_W_L1, SIZE_W_L1, MEM_ADDR_B_L1, SIZE_B_L1},
        {3,  MEM_ADDR_W_L2, SIZE_W_L2, MEM_ADDR_B_L2, SIZE_B_L2},
        {6,  MEM_ADDR_W_L3, SIZE_W_L3, MEM_ADDR_B_L3, SIZE_B_L3},
        {5,  MEM_ADDR_W_L4, SIZE_W_L4, MEM_ADDR_B_L4, SIZE_B_L4},
        {4,  MEM_ADDR_W_L5, SIZE_W_L5, MEM_ADDR_B_L5, SIZE_B_L5},
        {9,  MEM_ADDR_W_L6, SIZE_W_L6, MEM_ADDR_B_L6, SIZE_B_L6},
        {10, MEM_ADDR_W_L7, SIZE_W_L7, MEM_ADDR_B_L7, SIZE_B_L7},
        {11, MEM_ADDR_W_L8, SIZE_W_L8, MEM_ADDR_B_L8, SIZE_B_L8}
    };

    std::cout << "@ " << sc_time_stamp() << " [Controller] Start sending weights and biases via DMA...\n";
    for (int i = 0; i < 8; ++i) {
        send_parameter_stream(cfg[i].w_addr, cfg[i].dest, cfg[i].w_size, PKT_TYPE_WEIGHT);
        send_parameter_stream(cfg[i].b_addr, cfg[i].dest, cfg[i].b_size, PKT_TYPE_BIAS);
    }

    std::cout << "@ " << sc_time_stamp() << " [Controller] Sending image to PE 1 via DMA...\n";
    send_parameter_stream(MEM_ADDR_IMAGE, 1, SIZE_IMAGE, PKT_TYPE_FM);

    std::cout << "@ " << sc_time_stamp() << " [Controller] Inference triggered. Waiting for final result...\n";

    // Receive the result from NoC
    std::vector<float> linear_out;
    ack_rx.write(1);

    while(linear_out.size() < 1000) {
        wait();
        if (req_rx.read() == 1) {
            sc_lv<34> f = flit_rx.read();
            int type = f.range(33, 32).to_uint();
            if (type == FLIT_BODY || type == FLIT_TAIL) {
                unsigned int ival = f.range(31, 0).to_uint();
                float fval;
                std::memcpy(&fval, &ival, sizeof(float));
                linear_out.push_back(fval);
            }
        }
    }
    ack_rx.write(0);

    std::cout << "@ " << sc_time_stamp() << " [Controller] Writing 1000 results back to DRAM...\n";
    
    ctrl_addr.write(MEM_ADDR_RESULT_OUT);
    ctrl_len.write(1000);
    ctrl_write_req.write(1);
    wait();
    ctrl_write_req.write(0);

    for (int i = 0; i < 1000; ++i) {
        data_in.write(linear_out[i]);
        data_in_valid.write(1);
        
        do { wait(); } while(data_in_ready.read() == 0); // Wait for the DMA to accept
        
        data_in_valid.write(0);
    }

    std::cout << "@ " << sc_time_stamp() << " [Controller] Inference and Write-Back completed!\n";

    // ========================================================
    // PHASE 4: DISPLAY THE RESULT (Strict TA Format)
    // ========================================================
    // Softmax & Display
    double max_val = linear_out[0];
    for (size_t i = 1; i < linear_out.size(); ++i) {
        if (linear_out[i] > max_val) max_val = linear_out[i];
    }
    double sum = 0.0;
    std::vector<double> softmax_out(1000, 0.0);
    for (size_t i = 0; i < linear_out.size(); ++i) {
        softmax_out[i] = std::exp(linear_out[i] - max_val);
        sum += softmax_out[i];
    }

    std::vector<std::pair<double, int>> probabilities;
    for (size_t i = 0; i < 1000; ++i) {
        probabilities.push_back({softmax_out[i] / sum, i});
    }

    // Sort in descending order
    std::sort(probabilities.rbegin(), probabilities.rend());

    // --- READ THE CLASSES FILE ---
    // Since you created the symlink ./data -> ../../00_TESTBED/data,
    // this relative path will work fine.
    std::ifstream class_file("./data/imagenet_classes.txt");

    if(!class_file.is_open()) {
        std::cerr << "Error opening file: ./data/imagenet_classes.txt\n";
        sc_stop();
        return;
    }
    
    std::vector<std::string> class_name;
    std::string class_name_element;
    while (std::getline(class_file, class_name_element)) {
        class_name.push_back(class_name_element);
    }

    // --- FORMATTED PRINT
    std::cout << "\nTop 100 classes:\n";
    std::cout << "=================================================\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << std::right << std::setw(5) << "idx"
              << " | " << std::setw(8) << "val"
              << " | " << std::setw(11) << "possibility" 
              << " | " << "class name\n";
    std::cout << "-------------------------------------------------\n";

    for (int i = 0; i < 100; ++i) {
        int original_idx = probabilities[i].second;
        double prob = probabilities[i].first * 100.0;
        float linear_val = linear_out[original_idx];
        
        std::cout << std::right << std::setw(5) << original_idx
                  << " | " << std::setw(8) << linear_val
                  << " | " << std::setw(11) << prob
                  << " | " << class_name[original_idx] << "\n";
    }
    
    std::cout << "=================================================\n";

    sc_stop();
}