#include "pe.h"
#include <cfloat>
#include <cmath>

// ============================================================================
// 1. INITIALISATION & SNAKE MAPPING
// ============================================================================
void PE::init(int id) {
    my_id = id;
    weights_ready = false;
    biases_ready = false;
    input_ready = false;

    // Definition of the next packet destination (Snake Mapping)
    switch(my_id) {
        case 1:  destination_id = 2;  break; // Conv1 -> Pool1
        case 2:  destination_id = 3;  break; // Pool1 -> Conv2
        case 3:  destination_id = 7;  break; // Conv2 -> Pool2 (Going down!)
        case 7:  destination_id = 6;  break; // Pool2 -> Conv3 (Going left)
        case 6:  destination_id = 5;  break; // Conv3 -> Conv4
        case 5:  destination_id = 4;  break; // Conv4 -> Conv5
        case 4:  destination_id = 8;  break; // Conv5 -> Pool5 (Going down!)
        case 8:  destination_id = 9;  break; // Pool5 -> FC6 (Going right)
        case 9:  destination_id = 10; break; // FC6 -> FC7
        case 10: destination_id = 11; break; // FC7 -> FC8
        case 11: destination_id = 0;  break; // FC8 -> Controller (End of network)
        default: destination_id = 0;  break; // Inactive PEs (12, 13, 14, 15)
    }
}

// ============================================================================
// 2. FINITE STATE MACHINE (FSM)
// ============================================================================
void PE::pe_main_thread() {
    // Initialization
    weight_memory.clear();
    bias_memory.clear();
    input_fm.clear();
    output_fm.clear();
    wait();

    while(true) {
        // Trigger condition: We have our weights, biases and an image to process.
        // Note: For Pool1, Pool2 and Pool5, weights_ready and biases_ready will
        // intentionally remain 'false' (managed below) because they have no weights.
        
        bool is_pooling = (my_id == 2 || my_id == 7 || my_id == 8);
        bool can_compute = input_ready && (is_pooling || (weights_ready && biases_ready));

        if (can_compute) {
            //std::cout << "[PE 1] Conditions met! Starting mathematical computation.\n";
            
            // 1. Execute mathematical computation (Blocking for PE)
            execute_layer_computation();

            // Increment by one "macro" compute cycle
            compute_cycles_counter++; 
            
            // 2. Encapsulate result in a packet for the Core
            Packet* p = new Packet();
            p->source_id = my_id;
            p->dest_id = destination_id;
            p->packet_type = PKT_TYPE_FM; // This is a Feature Map
            p->datas = output_fm;
            
            // Place it in the queue for the Core's tx_thread to send
            output_queue.push(p);

            // 3. Reset for the next image
            input_ready = false;
            input_fm.clear();
            output_fm.clear();
            // NOTE: Do NOT clear weights and biases, keep them in SRAM cache!
        }
        
        wait(); // Wait for the next clock cycle
    }
}

// ============================================================================
// 3. ARCHITECTURAL ROUTING (Execute Layer)
// ============================================================================
void PE::execute_layer_computation() {
    std::vector<float> padded;

    switch(my_id) {
        case 1: // Conv1
            pad_input_image(input_fm, padded);
            convolution(padded, weight_memory, bias_memory, output_fm, 3, 227, 227, 64, 11, 4);
            for(auto& val : output_fm) if(val < 0) val = 0; // ReLU
            break;
            
        case 2: // Pool1
            max_pooling(input_fm, output_fm, 64, 55, 55, 3, 2);
            break;
            
        case 3: // Conv2
            pad_tensor(input_fm, padded, 64, 27, 27, 2);
            convolution(padded, weight_memory, bias_memory, output_fm, 64, 31, 31, 192, 5, 1);
            for(auto& val : output_fm) if(val < 0) val = 0;
            break;
            
        case 7: // Pool2
            max_pooling(input_fm, output_fm, 192, 27, 27, 3, 2);
            break;
            
        case 6: // Conv3
            pad_tensor(input_fm, padded, 192, 13, 13, 1);
            convolution(padded, weight_memory, bias_memory, output_fm, 192, 15, 15, 384, 3, 1);
            for(auto& val : output_fm) if(val < 0) val = 0;
            break;
            
        case 5: // Conv4
            pad_tensor(input_fm, padded, 384, 13, 13, 1);
            convolution(padded, weight_memory, bias_memory, output_fm, 384, 15, 15, 256, 3, 1);
            for(auto& val : output_fm) if(val < 0) val = 0;
            break;
            
        case 4: // Conv5
            pad_tensor(input_fm, padded, 256, 13, 13, 1);
            convolution(padded, weight_memory, bias_memory, output_fm, 256, 15, 15, 256, 3, 1);
            for(auto& val : output_fm) if(val < 0) val = 0;
            break;
            
        case 8: // Pool5
            max_pooling(input_fm, output_fm, 256, 13, 13, 3, 2);
            break;
            
        case 9: // FC6
            fully_connected(input_fm, weight_memory, bias_memory, output_fm, 9216, 4096);
            for(auto& val : output_fm) if(val < 0) val = 0;
            break;
            
        case 10: // FC7
            fully_connected(input_fm, weight_memory, bias_memory, output_fm, 4096, 4096);
            for(auto& val : output_fm) if(val < 0) val = 0;
            break;
            
        case 11: // FC8 (Note: No ReLU on the last layer)
            fully_connected(input_fm, weight_memory, bias_memory, output_fm, 4096, 1000);
            break;
    }
}

// ============================================================================
// 4. MATHEMATICAL ALGORITHMS
// ============================================================================

void PE::pad_input_image(const std::vector<float>& input, std::vector<float>& output) {
    // Allocate and zero-initialize output tensor for asymmetric padding (224 -> 227)
    output.assign(3 * 227 * 227, 0.0f); 
    for (int c = 0; c < 3; ++c) {
        for (int y = 0; y < 224; ++y) {
            for (int x = 0; x < 224; ++x) {
                int in_idx = c * (224 * 224) + y * 224 + x;
                int out_idx = c * (227 * 227) + (y + 2) * 227 + (x + 2);
                output[out_idx] = input[in_idx];
            }
        }
    }
}

void PE::pad_tensor(const std::vector<float>& input, std::vector<float>& output, int C, int H_in, int W_in, int pad) {
    int H_out = H_in + 2 * pad;
    int W_out = W_in + 2 * pad;
    output.assign(C * H_out * W_out, 0.0f);
    for (int c = 0; c < C; ++c) {
        for (int y = 0; y < H_in; ++y) {
            for (int x = 0; x < W_in; ++x) {
                int in_idx  = c * (H_in * W_in) + y * W_in + x;
                int out_idx = c * (H_out * W_out) + (y + pad) * W_out + (x + pad);
                output[out_idx] = input[in_idx];
            }
        }
    }
}

void PE::convolution(const std::vector<float>& input, const std::vector<float>& weights, const std::vector<float>& bias, std::vector<float>& output, int C_in, int H_in, int W_in, int C_out, int K, int stride) {
    int H_out = (H_in - K) / stride + 1;
    int W_out = (W_in - K) / stride + 1;
    output.assign(C_out * H_out * W_out, 0.0f);

    // 6-nested-loop convolution (Output Channels, H, W, Input Channels, K_H, K_W)
    for (int f = 0; f < C_out; ++f) {                 
        for (int y = 0; y < H_out; ++y) {             
            for (int x = 0; x < W_out; ++x) {         
                float sum = bias[f];                 
                for (int c = 0; c < C_in; ++c) {      
                    for (int ky = 0; ky < K; ++ky) {  
                        for (int kx = 0; kx < K; ++kx) { 
                            int in_y = y * stride + ky;
                            int in_x = x * stride + kx;
                            int in_idx = c * (H_in * W_in) + in_y * W_in + in_x;
                            int w_idx = f * (C_in * K * K) + c * (K * K) + ky * K + kx;
                            sum += input[in_idx] * weights[w_idx];
                        }
                    }
                }
                int out_idx = f * (H_out * W_out) + y * W_out + x;
                output[out_idx] = sum;
            }
        }
    }

    // Calculate the number of MAC operations for this layer
    unsigned long long total_macs = (unsigned long long)C_out * H_out * W_out * C_in * K * K;
    mac_operations_counter += total_macs; // For your CSV
}

void PE::max_pooling(const std::vector<float>& input, std::vector<float>& output, int C, int H_in, int W_in, int K, int stride) {
    int H_out = (H_in - K) / stride + 1;
    int W_out = (W_in - K) / stride + 1;
    output.assign(C * H_out * W_out, 0.0f);

    for (int c = 0; c < C; ++c) {                     
        for (int y = 0; y < H_out; ++y) {             
            for (int x = 0; x < W_out; ++x) {         
                float max_val = -FLT_MAX; 
                for (int ky = 0; ky < K; ++ky) {      
                    for (int kx = 0; kx < K; ++kx) {
                        int in_y = y * stride + ky;
                        int in_x = x * stride + kx;
                        int in_idx = c * (H_in * W_in) + in_y * W_in + in_x;
                        if (input[in_idx] > max_val) {
                            max_val = input[in_idx];
                        }
                    }
                }
                int out_idx = c * (H_out * W_out) + y * W_out + x;
                output[out_idx] = max_val;
            }
        }
    }
}

void PE::fully_connected(const std::vector<float>& input, const std::vector<float>& weights, const std::vector<float>& bias, std::vector<float>& output, int in_features, int out_features) {
    output.assign(out_features, 0.0f);
    for (int o = 0; o < out_features; ++o) {
        float sum = bias[o]; 
        for (int i = 0; i < in_features; ++i) {
            int w_idx = o * in_features + i;
            sum += input[i] * weights[w_idx];
        }
        output[o] = sum;
    }

    // Calculate the number of MAC operations for this layer
    unsigned long long layer_macs = (unsigned long long)out_features * in_features;
    mac_operations_counter += layer_macs; // For your CSV
}