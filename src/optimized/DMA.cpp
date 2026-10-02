#include "DMA.h"

// ====================================================================
// READ PROCESS: DRAM -> DMA -> Controller
// ====================================================================
void DMA::master_read_thread() {
    // Initialization on reset
    arvalid.write(0);
    rready.write(0);
    data_out_valid.write(0);
    ctrl_done.write(0);
    wait();

    while(true) {
        // 1. Wait for a command from the Controller
        if (ctrl_read_req.read() == 1) {
            uint32_t remaining_len = ctrl_len.read();
            uint32_t current_addr = ctrl_addr.read();

            // 2. Split the total transfer into AXI bursts (max 256)
            while (remaining_len > 0) {
                uint32_t current_burst_len = (remaining_len > 256) ? 256 : remaining_len;

                // --- AR PHASE (Address Read) ---
                araddr.write(current_addr);
                arlen.write(current_burst_len - 1); // AXI specification: len = beats - 1
                arvalid.write(1);

                do { wait(); } while (arready.read() == 0); // Handshake
                arvalid.write(0); // Deassert valid as soon as ready is seen

                // --- R PHASE (Read Data) ---
                // Keep the burst_len loop, but increment by 4 each time
                for (uint32_t i = 0; i < current_burst_len; i += 4) {
                    
                    // Process 4 values in a single "time cycle"
                    for (int b = 0; b < 4; b++) {
                        if ((i + b) < current_burst_len) {
                            rready.write(1);
                            do { wait(); } while (rvalid.read() == 0); // (The wait() here will not be a problem because the DRAM is instantaneous without the delays we removed)
                            
                            float read_val = rdata.read(); 
                            rready.write(0);

                            data_out.write(read_val);
                            data_out_valid.write(1);
                            do { wait(); } while (data_out_ready.read() == 0); 
                            data_out_valid.write(0);
                        }
                    }
                    // Global time advances by one cycle only AFTER transferring 4 floats!
                    // wait(); to check whether wait was added in the baseline version
                }

                // Update for the next burst
                remaining_len -= current_burst_len;
                current_addr += (current_burst_len * 4); // +4 bytes per float
                dram_bytes_read += (current_burst_len * 4);
            }

            // 3. Inform the Controller that the entire transfer is complete
            ctrl_done.write(1);
            wait();
            ctrl_done.write(0);

        } else {
            wait(); // Wait for the next cycle if there is no request
        }
    }
}

// ====================================================================
// WRITE PROCESS: Controller -> DMA -> DRAM
// ====================================================================
void DMA::master_write_thread() {
    // Initialization on reset
    awvalid.write(0);
    wvalid.write(0);
    wlast.write(0);
    bready.write(0);
    data_in_ready.write(0);
    wait();

    while(true) {
        if (ctrl_write_req.read() == 1) {
            uint32_t remaining_len = ctrl_len.read();
            uint32_t current_addr = ctrl_addr.read();

            while (remaining_len > 0) {
                uint32_t current_burst_len = (remaining_len > 256) ? 256 : remaining_len;

                // --- AW PHASE (Address Write) ---
                awaddr.write(current_addr);
                awlen.write(current_burst_len - 1);
                awvalid.write(1);

                do { wait(); } while (awready.read() == 0);
                awvalid.write(0);

                // --- W PHASE (Write Data) ---
            // Increment by 4 each time to simulate the 128-bit bus
            for (uint32_t i = 0; i < current_burst_len; i += 4) {
                
                // Process 4 floats in the same time window
                for (int b = 0; b < 4; b++) {
                    if ((i + b) < current_burst_len) {
                        
                        // 1. Accept data from the Controller
                        data_in_ready.write(1);
                        do { wait(); } while (data_in_valid.read() == 0);
                        float write_val = data_in.read();
                        data_in_ready.write(0);

                        // 2. Place the data on the AXI bus
                        wdata.write(write_val);
                        wvalid.write(1);
                        wlast.write(((i + b) == current_burst_len - 1) ? 1 : 0);

                        do { wait(); } while (wready.read() == 0);
                        wvalid.write(0);
                    }
                }
                
                // The SystemC kernel advances by one cycle ONLY after 4 floats!
                // wait(); to check whether wait was added in the baseline version
            }

                // --- B PHASE (Write Response) ---
                // Wait for the DRAM to confirm that the burst write completed successfully
                bready.write(1);
                do { wait(); } while (bvalid.read() == 0);
                bready.write(0);

                remaining_len -= current_burst_len;
                current_addr += (current_burst_len * 4);
                dram_bytes_written += (current_burst_len * 4);
            }
            
            // The Controller will know it is done when the DRAM has acknowledged all B responses
            // We can reuse ctrl_done or create a dedicated one (ctrl_write_done).
            // For simplicity, we assume read_req and write_req are not asserted at the same time
            // in our basic Controller.
        }
        wait();
    }
}