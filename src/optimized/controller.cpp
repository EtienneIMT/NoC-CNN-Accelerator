#include "controller.h"
#include "memory_map.h"
#include <stdint.h>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>

// --- Helper: Create Header Flit ---
sc_lv<130> Controller::create_header(int dest, int src, int pkt_type) {
    sc_lv<130> h;
    h.range(129, 128) = FLIT_HEADER;
    h.range(127, 32)  = 0; // Padding
    h.range(31, 28)   = dest;
    h.range(27, 24)   = src;
    h.range(23, 20)   = pkt_type;
    h.range(19, 0)    = 0;
    return h;
}

// --- Helper: Create Data Flit (Body or Tail) ---
sc_lv<130> Controller::create_data_flit(float v0, float v1, float v2, float v3, bool is_tail) {
    sc_lv<130> f;
    f.range(129, 128) = is_tail ? FLIT_TAIL : FLIT_BODY;
    
    unsigned int i0, i1, i2, i3;
    std::memcpy(&i0, &v0, sizeof(float)); 
    std::memcpy(&i1, &v1, sizeof(float)); 
    std::memcpy(&i2, &v2, sizeof(float)); 
    std::memcpy(&i3, &v3, sizeof(float)); 
    
    f.range(31, 0)   = i0;
    f.range(63, 32)  = i1;
    f.range(95, 64)  = i2;
    f.range(127, 96) = i3;
    
    return f;
}

// ====================================================================
// TX THREAD 0 : Dedicated to Router 0 (Top Row PEs)
// ====================================================================
void Controller::tx_thread_0() {
    req_tx_0.write(0);
    flit_tx_0.write(0);
    bool flit_in_transit = false;
    wait();

    while(true) {
        if (flit_in_transit && ack_tx_0.read() == 1) { 
            req_tx_0.write(0);
            flit_in_transit = false;
        }

        if (!flit_in_transit && !flit_queue_0.empty()) {
            sc_lv<130> f = flit_queue_0.front();
            flit_queue_0.pop();

            flit_tx_0.write(f);
            req_tx_0.write(1); 
            flit_in_transit = true;
        }
        wait(); 
    }
}

// ====================================================================
// TX THREAD 4 : Dedicated to Router 4 (Middle Row PEs)
// ====================================================================
void Controller::tx_thread_4() {
    req_tx_4.write(0);
    flit_tx_4.write(0);
    bool flit_in_transit = false;
    wait();

    while(true) {
        if (flit_in_transit && ack_tx_4.read() == 1) { 
            req_tx_4.write(0);
            flit_in_transit = false;
        }

        if (!flit_in_transit && !flit_queue_4.empty()) {
            sc_lv<130> f = flit_queue_4.front();
            flit_queue_4.pop();

            flit_tx_4.write(f);
            req_tx_4.write(1); 
            flit_in_transit = true;
        }
        wait(); 
    }
}

// ====================================================================
// THE DISPATCHER (Producer): Reads DMA and queues flits
// ====================================================================
void Controller::send_parameter_stream(uint32_t start_addr, int dest_pe, int total_count, int pkt_type) {
    
    int target_port = (dest_pe <= 3) ? 0 : 4;
    std::queue<sc_lv<130>>* target_queue = (target_port == 0) ? &flit_queue_0 : &flit_queue_4;
    
    std::cout << "@ " << sc_time_stamp() << " [Dispatcher] Fetching " << total_count 
              << " items via DMA -> Routing to Port " << target_port << " (Target PE: " << dest_pe << ")\n";

    // 1. Send Header to the selected software queue
    target_queue->push(create_header(dest_pe, my_id, pkt_type));

    // 2. Pulse DMA Read Request
    ctrl_addr.write(start_addr);
    ctrl_len.write(total_count);
    ctrl_read_req.write(1);
    wait();
    ctrl_read_req.write(0);

    // 3. DMA Fetching Loop
    std::queue<float> temp_fifo;
    int dma_read_count = 0;
    int noc_send_count = 0;

    data_out_ready.write(1);

    while (noc_send_count < total_count) {
        // A. Read float from DMA
        if (data_out_valid.read() == 1 && dma_read_count < total_count) {
            temp_fifo.push(data_out.read());
            dma_read_count++;
            if (temp_fifo.size() > max_sram_fifo_size) max_sram_fifo_size = temp_fifo.size();
        }

        // B. Pack 4 floats into 1 Flit and push to queue
        if (temp_fifo.size() >= 4 || (dma_read_count == total_count && !temp_fifo.empty())) {
            float v[4] = {0.0f, 0.0f, 0.0f, 0.0f}; 
            int current_payload = 0;
            
            while (current_payload < 4 && !temp_fifo.empty()) {
                v[current_payload] = temp_fifo.front();
                temp_fifo.pop();
                current_payload++;
            }

            bool is_tail = ((noc_send_count + current_payload) >= total_count);
            target_queue->push(create_data_flit(v[0], v[1], v[2], v[3], is_tail));
            noc_send_count += current_payload;
        }

        // C. Backpressure mechanism (Limit software queue size)
        while (target_queue->size() > 64) {
            wait(); 
        }

        wait(); 
    }
    
    data_out_ready.write(0); 

    // Allow hardware queues to drain safely
    for(int i = 0; i < 5; i++) wait();
}

// ====================================================================
// MASTER THREAD (Main Application Flow)
// ====================================================================
void Controller::master_thread() {
    ctrl_addr.write(0);
    ctrl_len.write(0);
    ctrl_read_req.write(0);
    ctrl_write_req.write(0);
    data_out_ready.write(0);
    data_in_valid.write(0);
    data_in.write(0);
    ack_rx.write(0);
    wait();
    
    while (rst.read() == 1) wait();
    wait();

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

    std::cout << "@ " << sc_time_stamp() << " [Controller] Start dual-port DMA injection...\n";
    for (int i = 0; i < 8; ++i) {
        send_parameter_stream(cfg[i].w_addr, cfg[i].dest, cfg[i].w_size, PKT_TYPE_WEIGHT);
        send_parameter_stream(cfg[i].b_addr, cfg[i].dest, cfg[i].b_size, PKT_TYPE_BIAS);
    }

    std::cout << "@ " << sc_time_stamp() << " [Controller] Sending image to PE 1 via DMA...\n";
    send_parameter_stream(MEM_ADDR_IMAGE, 1, SIZE_IMAGE, PKT_TYPE_FM);

    std::cout << "@ " << sc_time_stamp() << " [Controller] Inference triggered. Waiting for final result...\n";

    std::vector<float> linear_out;
    ack_rx.write(1);

    while(linear_out.size() < 1000) {
        wait();
        if (req_rx.read() == 1) {
            sc_lv<130> f = flit_rx.read();
            int type = f.range(129, 128).to_uint();
            if (type == 0 || type == 1) { 
                unsigned int i0 = f.range(31, 0).to_uint();
                unsigned int i1 = f.range(63, 32).to_uint();
                unsigned int i2 = f.range(95, 64).to_uint();
                unsigned int i3 = f.range(127, 96).to_uint();
                
                float v0, v1, v2, v3;
                std::memcpy(&v0, &i0, sizeof(float));
                std::memcpy(&v1, &i1, sizeof(float));
                std::memcpy(&v2, &i2, sizeof(float));
                std::memcpy(&v3, &i3, sizeof(float));
                
                linear_out.push_back(v0);
                linear_out.push_back(v1);
                linear_out.push_back(v2);
                linear_out.push_back(v3);
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
        
        do { wait(); } while(data_in_ready.read() == 0); 
        
        data_in_valid.write(0);
    }

    std::cout << "@ " << sc_time_stamp() << " [Controller] Inference and Write-Back completed!\n";

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

    std::sort(probabilities.rbegin(), probabilities.rend());

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
