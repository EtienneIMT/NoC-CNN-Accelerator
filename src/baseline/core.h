#ifndef CORE_H
#define CORE_H

#include "systemc.h"
#include "pe.h"

SC_MODULE( Core ) {
    sc_in  < bool >  rst;
    sc_in  < bool >  clk;
    // receive
    sc_in  < sc_lv<34> > flit_rx;
    sc_in  < bool > req_rx;      
    sc_out < bool > ack_rx;      
    // transmit
    sc_out < sc_lv<34> > flit_tx;
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
                    sc_lv<34> h;
                    h.range(33, 32) = 2; // Type 2 = Header
                    h.range(31, 28) = p->dest_id;
                    h.range(27, 24) = p->source_id;
                    h.range(23, 20) = p->packet_type; // Our control plane!
                    h.range(19, 0)  = 0;              // The remaining bits are set to 0
                    
                    flit_tx.write(h);
                    req_tx.write(1); // Valid
                    wait();
                    while(ack_tx.read() == 0) wait(); // Wait for ready
                    
                    // --- BODY / TAIL TRANSMISSION ---
                    for (size_t i = 0; i < p->datas.size(); i++) {
                        sc_lv<34> f;
                        if (i == p->datas.size() - 1) f.range(33, 32) = 1; // Tail
                        else f.range(33, 32) = 0; // Body
                        
                        // Safe use of std::memcpy instead of union punning
                        unsigned int ival;
                        float fval = p->datas[i];
                        std::memcpy(&ival, &fval, sizeof(float));
                        f.range(31, 0) = ival;

                        flit_tx.write(f);
                        req_tx.write(1); // Always valid (burst)
                        wait();
                        while(ack_tx.read() == 0) wait(); // Wait for Ready
                    }
                    req_tx.write(0); // End of packet

                    delete p; // Free the packet memory
                }
            } else {
                wait(); // The queue is empty, so we wait for the next cycle
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
            } while (req_rx.read() == 0 || flit_rx.read().range(33, 32).to_uint() != 2);
            
            // If we exit the loop, we have captured a valid header
            sc_lv<34> header_flit = flit_rx.read();
            p = new Packet();
            p->dest_id     = header_flit.range(31, 28).to_uint(); 
            p->source_id   = header_flit.range(27, 24).to_uint(); 
            p->packet_type = header_flit.range(23, 20).to_uint();
            
            //std::cout << "[Core 1] HEADER received! Packet type: " << p->packet_type << "\n";

            // --- STEP 2: READ THE PAYLOAD (RECEIVE mode) ---
            // Stay in this loop until the packet is complete
            bool packet_finished = false;
            
            while (!packet_finished) {
                wait(); // Move to the next clock cycle to read the next flit
                
                if (req_rx.read() == 1) {
                    sc_lv<34> f = flit_rx.read();
                    unsigned int type = f.range(33, 32).to_uint();
                    
                    if (type == 0 || type == 1) { // Body or Tail
                        unsigned int ival = f.range(31, 0).to_uint();
                        float fval;
                        std::memcpy(&fval, &ival, sizeof(float));
                        
                        p->datas.push_back(fval);

                        // CORE probe
                        //if (p->datas.size() == 1) std::cout << "  [Core 1-Probe] First BODY flit received!\n";
                        //if (p->datas.size() % 5000 == 0) std::cout << "  [Core 1-Probe] " << p->datas.size() << " flits received...\n";
                        
                        if (type == 1) { // TAIL detected!
                            //std::cout << "[Core 1] TAIL received! Transferring " << p->datas.size() << " values to the PE.\n";
                            
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