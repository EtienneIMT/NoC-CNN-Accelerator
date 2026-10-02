#ifndef PE_H
#define PE_H

#include "systemc.h"
#include <vector>
#include <queue>

// --- CONTROL PLANE CONSTANTS ---
#define PKT_TYPE_WEIGHT 0
#define PKT_TYPE_BIAS   1
#define PKT_TYPE_FM     2 // Feature Map (Image data or intermediate tensors)

// FINITE STATE MACHINE (FSM)
enum PE_State {
    PE_IDLE,
    PE_LOAD_SRAM,
    PE_COMPUTE,
    PE_SEND_NOC
};

// --- PACKET STRUCTURE ---
struct Packet {
    int source_id;
    int dest_id;
    int packet_type;        // 0=Weight, 1=Bias, 2=Feature Map
    std::vector<float> datas; // Inference data in floating point (float)
};

SC_MODULE( PE ) {
    public:
        unsigned long long compute_cycles_counter;
        unsigned long long mac_operations_counter;

    sc_in<bool> clk;
    sc_in<bool> rst;

    // --- HARDWARE REGISTERS (Problem statement specifications) ---
    int my_id;              // Unique PE ID on the NoC grid (0 to 15)
    int destination_id;     // Register containing the target of the next hop (Snake Mapping)

    // FSM REGISTERS & PARALLELISM
    PE_State state;
    int mac_units;                               // Instanciation de 4 MACs
    unsigned long long compute_cycles_remaining; // Timer matériel pour l'état COMPUTE

    // --- LOCAL SRAM (Internal PE storage vectors) ---
    std::vector<float> weight_memory;
    std::vector<float> bias_memory;
    std::vector<float> input_fm;
    std::vector<float> output_fm;

    // --- CONTROL FLAGS (Hardware indicators for the FSM) ---
    bool weights_ready;
    bool biases_ready;
    bool input_ready;

    // --- OUTPUT QUEUE ---
    // Queue read by the Core's tx_thread to inject data on the NoC
    std::queue<Packet*> output_queue;

    // --- METHODS AND THREAD PROCESSES ---
    void pe_main_thread();               // Main FSM thread
    void init(int id);                   // Initialization function (ID and Routing)
    void execute_layer_computation();    // Routing logic based on ID

    // --- ALEXNET ALGORITHMS ---
    void convolution(const std::vector<float>& input, const std::vector<float>& weights, const std::vector<float>& bias, std::vector<float>& output, int C_in, int H_in, int W_in, int C_out, int K, int stride);
    void max_pooling(const std::vector<float>& input, std::vector<float>& output, int C, int H_in, int W_in, int K, int stride);
    void fully_connected(const std::vector<float>& input, const std::vector<float>& weights, const std::vector<float>& bias, std::vector<float>& output, int in_features, int out_features);
    
    // Tensor adjustment utility functions (Padding)
    void pad_input_image(const std::vector<float>& input, std::vector<float>& output);
    void pad_tensor(const std::vector<float>& input, std::vector<float>& output, int C, int H_in, int W_in, int pad);

    SC_CTOR( PE ) {
        // HARDWARE INITIALIZATION
        state = PE_IDLE;
        mac_units = 4; // The accelerator now has 4 MACs!
        compute_cycles_remaining = 0;

        SC_THREAD( pe_main_thread );
        sensitive << clk.pos();
        dont_initialize();
        
        // Default initialization of indicators
        weights_ready = false;
        biases_ready  = false;
        input_ready   = false;
    }
};

#endif