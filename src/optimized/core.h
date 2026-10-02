#ifndef CORE_H
#define CORE_H

#include "systemc.h"
#include "pe.h"

SC_MODULE( Core ) {
    sc_in  < bool >  rst;
    sc_in  < bool >  clk;
    // receive
    sc_in  < sc_lv<130> > flit_rx;
    sc_in  < bool > req_rx;      
    sc_out < bool > ack_rx;      
    // transmit
    sc_out < sc_lv<130> > flit_tx;
    sc_out < bool > req_tx;      
    sc_in  < bool > ack_tx;      

    PE pe;
    int core_id; // Variable name synchronized with the constructor

    void init(int id) {
        core_id = id;
        pe.init(id);
    }

    void tx_thread() {
        req_tx.write(0);
        wait();

        while(true) {
            // The Core checks whether the PE has queued a packet ready to be sent on the network
            if (!pe.output_queue.empty()) {
                Packet* p = pe.output_queue.front();
                pe.output_queue.pop();

                if (p != NULL) {
                    // --- HEADER TRANSMISSION ---
                    sc_lv<130> h;
                    h.range(129, 128) = 2; // Type 2 = Header
                    h.range(127, 32)  = 0; // Padding
                    h.range(31, 28) = p->dest_id;
                    h.range(27, 24) = p->source_id;
                    h.range(23, 20) = p->packet_type; // Our control plane!
                    h.range(19, 0)  = 0;              // The remaining bits are set to 0
                    
                    flit_tx.write(h);
                    req_tx.write(1); // Valid
                    wait();
                    while(ack_tx.read() == 0) wait(); // Wait for ready
                    
                    // --- BODY / TAIL TRANSMISSION ---
                    // --- BODY / TAIL TRANSMISSION ---
                    for (size_t i = 0; i < p->datas.size(); i += 4) { // Advance by 4 each iteration
                        sc_lv<130> f;
                        
                        // If this is the last group of up to 4 values, mark as Tail
                        if (i + 4 >= p->datas.size()) f.range(129, 128) = 1; // Tail
                        else f.range(129, 128) = 0; // Body
                        
                        // Extract the 4 values (zero-pad if beyond size)
                        float v0 = (i < p->datas.size())     ? p->datas[i]   : 0.0f;
                        float v1 = (i+1 < p->datas.size())   ? p->datas[i+1] : 0.0f;
                        float v2 = (i+2 < p->datas.size())   ? p->datas[i+2] : 0.0f;
                        float v3 = (i+3 < p->datas.size())   ? p->datas[i+3] : 0.0f;

                        // Safe conversion
                        unsigned int i0, i1, i2, i3;
                        std::memcpy(&i0, &v0, sizeof(float));
                        std::memcpy(&i1, &v1, sizeof(float));
                        std::memcpy(&i2, &v2, sizeof(float));
                        std::memcpy(&i3, &v3, sizeof(float));

                        // Place into the 128 bits of data
                        f.range(31, 0)   = i0;
                        f.range(63, 32)  = i1;
                        f.range(95, 64)  = i2;
                        f.range(127, 96) = i3;

                        flit_tx.write(f);
                        req_tx.write(1); // Always valid (burst)
                        wait();
                        while(ack_tx.read() == 0) wait(); // Wait for Ready
                    }
                    req_tx.write(0); // End of packet

                    delete p; // Free the packet memory
                }
                } else {
                wait(); // The queue is empty, wait for the next cycle
            }
        }
    }

void rx_thread() {
        // Initialization at reset
        ack_rx.write(0);
        Packet* p = NULL;
        wait(); // Wait for the end of the hardware reset

        while(true) {
            // --- STEP 1: WAIT FOR THE HEADER (IDLE mode) ---
            ack_rx.write(1); // Signal that we are ready to receive a new packet
            
            do {
                wait(); // Advance one clock cycle each time
            // NOTE: The type bits are now at 129,128
            } while (req_rx.read() == 0 || flit_rx.read().range(129, 128).to_uint() != 2);
            
            // If we exit the loop, we have captured a valid header
            sc_lv<130> header_flit = flit_rx.read();
            p = new Packet();
            // The header indices remain at the lower bits of the flit (thanks to the padding we added)
            p->dest_id     = header_flit.range(31, 28).to_uint(); 
            p->source_id   = header_flit.range(27, 24).to_uint(); 
            p->packet_type = header_flit.range(23, 20).to_uint();
            
            // --- STEP 2: READ THE PAYLOAD (RECEIVE mode) ---
            // Stay in this loop until the packet is complete
            bool packet_finished = false;
            
            while (!packet_finished) {
                wait(); // Move to the next clock cycle to read the next flit
                
                if (req_rx.read() == 1) {
                    sc_lv<130> f = flit_rx.read();
                    // NOTE: Type bits at 129,128
                    unsigned int type = f.range(129, 128).to_uint();
                    
                    if (type == 0 || type == 1) { // Body or Tail
                        
                        // Extract the four 32-bit blocks
                        unsigned int i0 = f.range(31, 0).to_uint();
                        unsigned int i1 = f.range(63, 32).to_uint();
                        unsigned int i2 = f.range(95, 64).to_uint();
                        unsigned int i3 = f.range(127, 96).to_uint();
                        
                        float v0, v1, v2, v3;
                        std::memcpy(&v0, &i0, sizeof(float));
                        std::memcpy(&v1, &i1, sizeof(float));
                        std::memcpy(&v2, &i2, sizeof(float));
                        std::memcpy(&v3, &i3, sizeof(float));
                        
                        // Add the 4 floats at once into the packet memory
                        p->datas.push_back(v0);
                        p->datas.push_back(v1);
                        p->datas.push_back(v2);
                        p->datas.push_back(v3);

                        if (type == 1) { // TAIL detected!
                            
                            // Direct transfer to the PE memories
                            if (p->packet_type == 0) {
                                pe.weight_memory = p->datas;
                                pe.weights_ready = true;
                            } else if (p->packet_type == 1) {
                                pe.bias_memory = p->datas;
                                pe.biases_ready = true;
                            } else if (p->packet_type == 2) {
                                pe.input_fm = p->datas;
                                pe.input_ready = true;
                            }
                            
                            // Cleanup and state change
                            delete p;
                            p = NULL;
                            packet_finished = true; // Exit RECEIVE mode and return to IDLE
                        }
                    }
                }
            }
        }
    }

    SC_HAS_PROCESS(Core);
    Core(sc_module_name name, int id) : sc_module(name), core_id(id), pe("internal_pe") {
        SC_THREAD(tx_thread);
        sensitive << clk.pos();
        SC_THREAD(rx_thread);
        sensitive << clk.pos();

        // Connect the Core clock and reset to the internal PE
        pe.clk(clk);
        pe.rst(rst);
    }
};

#endif