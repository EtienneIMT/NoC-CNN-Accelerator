#ifndef ROUTER_H
#define ROUTER_H

#include "systemc.h"

#define DEPTH 2 // Physical depth of our SRAM per port

SC_MODULE( Router ) {
    sc_in  < bool >  rst;
    sc_in  < bool >  clk;

    sc_out < sc_lv<130> >  out_flit[5];
    sc_out < bool >  out_req[5];
    sc_in  < bool >  in_ack[5];

    sc_in  < sc_lv<130> >  in_flit[5];
    sc_in  < bool >  in_req[5];
    sc_out < bool >  out_ack[5];

    // --- RING BUFFERS ---
    sc_lv<130> in_buffer[5][DEPTH];
    int in_rd_ptr[5]; // Read pointer
    int in_wr_ptr[5]; // Write pointer
    int in_count[5];  // Current number of elements

    sc_lv<130> out_buffer[5][DEPTH];
    int out_rd_ptr[5];
    int out_wr_ptr[5];
    int out_count[5];
    
    // Added: current output port assigned to each input port (-1 means idle)
    int in_port_state[5];
    // Added: which input port has locked each output port (-1 means idle)
    int out_port_lock[5]; 
    int router_id;

    void init(int id) {
        router_id = id;
    }

    // XY routing strategy
    int get_xy_route(int current_id, int dest_id) {
        int cx = current_id % 4;
        int cy = current_id / 4;
        int dx = dest_id % 4;
        int dy = dest_id / 4;

        if (dx > cx) return 2; // East
        if (dx < cx) return 3; // West
        if (dy > cy) return 1; // South
        if (dy < cy) return 0; // North
        return 4; // Local (Core)
    }

    // --- FIFO HARDWARE LOGIC ---

    // Input buffer functions
    bool in_empty(int p) { return in_count[p] == 0; }
    bool in_full(int p)  { return in_count[p] == DEPTH; }
    sc_lv<130> in_front(int p) { return in_buffer[p][in_rd_ptr[p]]; }

    void in_push(int p, sc_lv<130> val) {
        in_buffer[p][in_wr_ptr[p]] = val;
        in_wr_ptr[p] = (in_wr_ptr[p] + 1) % DEPTH; // Modulo (%) makes the pointer wrap around
        in_count[p]++;
    }

    void in_pop(int p) {
        in_rd_ptr[p] = (in_rd_ptr[p] + 1) % DEPTH;
        in_count[p]--;
    }

    // Output buffer functions
    bool out_empty(int p) { return out_count[p] == 0; }
    bool out_full(int p)  { return out_count[p] == DEPTH; }
    sc_lv<130> out_front(int p) { return out_buffer[p][out_rd_ptr[p]]; }

    void out_push(int p, sc_lv<130> val) {
        out_buffer[p][out_wr_ptr[p]] = val;
        out_wr_ptr[p] = (out_wr_ptr[p] + 1) % DEPTH;
        out_count[p]++;
    }

    void out_pop(int p) {
        out_rd_ptr[p] = (out_rd_ptr[p] + 1) % DEPTH;
        out_count[p]--;
    }

    void rx_thread_0() { rx_logic(0); }
    void rx_thread_1() { rx_logic(1); }
    void rx_thread_2() { rx_logic(2); }
    void rx_thread_3() { rx_logic(3); }
    void rx_thread_4() { rx_logic(4); }

    void rx_logic(int p) {
        out_ack[p].write(0); // Initialization at reset
        
        while(true) {
            // 1. Do I have space in my ring buffer?
            bool ready = !in_full(p);
            out_ack[p].write(ready); // out_ack becomes our "Ready" signal
            
            // 2. Wait for the clock edge (time passes)
            wait(); 
            
            // 3. After waking up, check whether a transfer occurred
            if (ready && in_req[p].read() == 1) {
                sc_lv<130> f = in_flit[p].read();
                in_push(p, f); // Save the flit physically
            }
        }
    }

    // Move data from in_q to out_q according to the routing rules (includes lock arbitration)
    void route_thread() {
        while(true) {
            // -------------------------------------------------------------
            // STEP 1: Routing Computation (RC) - What do the inputs want?
            // -------------------------------------------------------------
            int requests[5]; 
            for(int i=0; i<5; i++) {
                requests[i] = -1; // -1 = No request for this port
                
                if (!in_empty(i)) {
                    sc_lv<130> f = in_front(i);
                    int type = f.range(129, 128).to_uint();

                    if (in_port_state[i] == -1) { // Port is free (new packet)
                        if (type == 2) { // Header
                            int dest_id = f.range(31, 28).to_uint();
                            requests[i] = get_xy_route(router_id, dest_id);
                        } else {
                            // Anti-bug: orphan Body or Tail flit -> drop it
                            in_pop(i); 
                        }
                    } else {
                        // Packet already in transmission, keep the request
                        requests[i] = in_port_state[i];
                    }
                }
            }

            // -------------------------------------------------------------
            // STEP 2: Arbiter (SA) - Who gets access to the outputs?
            // -------------------------------------------------------------
            for(int out_port=0; out_port<5; out_port++) {
                // If the output port is not already locked by an in-flight packet
                if (out_port_lock[out_port] == -1) {
                    // Look for a requester. (Fixed priority: port 0 wins against port 4)
                    for(int in_port=0; in_port<5; in_port++) {
                        if (requests[in_port] == out_port) {
                            // Winner found! Grant it the output port.
                            out_port_lock[out_port] = in_port;
                            in_port_state[in_port] = out_port;
                            break; // Stop arbitration for this output port
                        }
                    }
                }
            }

            // -------------------------------------------------------------
            // STEP 3: Crossbar Switch (XB) - Physical transfer
            // -------------------------------------------------------------
            for (int in_port=0; in_port<5; in_port++) {
                if (!in_empty(in_port) && in_port_state[in_port] != -1) {
                    int target_out = in_port_state[in_port];
                    
                    // If we hold the lock AND the output has space
                    if (out_port_lock[target_out] == in_port && !out_full(target_out)) {
                        
                        sc_lv<130> f = in_front(in_port);
                        int type = f.range(129, 128).to_uint();

                        // Transfer!
                        out_push(target_out, f); 
                        in_pop(in_port);         

                        // If this is a Tail flit, the transmission is done; release the lock
                        if (type == 1) {
                            out_port_lock[target_out] = -1;
                            in_port_state[in_port] = -1;
                        }
                    }
                }
            }

            // End of the combinational stage, wait for the next clock tick
            wait(); 
        }
    }

    void tx_thread_0() { tx_logic(0); }
    void tx_thread_1() { tx_logic(1); }
    void tx_thread_2() { tx_logic(2); }
    void tx_thread_3() { tx_logic(3); }
    void tx_thread_4() { tx_logic(4); }

    void tx_logic(int p) {
        out_req[p].write(0); // Initialization
        
        while(true) {
            if (!out_empty(p)) {
                // Prepare the data on the bus
                sc_lv<130> f = out_front(p);
                out_flit[p].write(f);
                out_req[p].write(1); // out_req becomes our "Valid" signal
                
                // Wait for the clock edge
                wait(); 
                
                // Receiver not ready? Sleep until it is
                while(in_ack[p].read() == 0) {
                    wait(); 
                }
                // Transfer succeeded, remove the flit from our buffer
                out_pop(p); 
            } else {
                // Nothing to send
                out_req[p].write(0);
                wait(); 
            }
        }
    }

    SC_HAS_PROCESS(Router);
    Router(sc_module_name name) : sc_module(name) {
        for(int i=0; i<5; i++) {
            in_port_state[i] = -1;
            out_port_lock[i] = -1;

            // Memory pointer initialization
            in_rd_ptr[i] = 0;
            in_wr_ptr[i] = 0;
            in_count[i] = 0;
            
            out_rd_ptr[i] = 0;
            out_wr_ptr[i] = 0;
            out_count[i] = 0;
        }
        
        SC_THREAD(rx_thread_0); sensitive << clk.pos();
        SC_THREAD(rx_thread_1); sensitive << clk.pos();
        SC_THREAD(rx_thread_2); sensitive << clk.pos();
        SC_THREAD(rx_thread_3); sensitive << clk.pos();
        SC_THREAD(rx_thread_4); sensitive << clk.pos();

        SC_THREAD(route_thread); sensitive << clk.pos();

        SC_THREAD(tx_thread_0); sensitive << clk.pos();
        SC_THREAD(tx_thread_1); sensitive << clk.pos();
        SC_THREAD(tx_thread_2); sensitive << clk.pos();
        SC_THREAD(tx_thread_3); sensitive << clk.pos();
        SC_THREAD(tx_thread_4); sensitive << clk.pos();
    }
};

#endif