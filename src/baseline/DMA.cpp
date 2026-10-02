#include "DMA.h"

// ====================================================================
// READ PROCESS: DRAM -> DMA -> Controller
// ====================================================================
void DMA::master_read_thread() {
    // Initialization at Reset
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

            // 2. Split the total transfer into AXI bursts (Max 256)
            while (remaining_len > 0) {
                uint32_t current_burst_len = (remaining_len > 256) ? 256 : remaining_len;

                // --- AR PHASE (Address Read) ---
                araddr.write(current_addr);
                arlen.write(current_burst_len - 1); // AXI specification: len = beats - 1
                arvalid.write(1);

                do { wait(); } while (arready.read() == 0); // Handshake
                arvalid.write(0); // Deassert valid as soon as ready is seen

                // --- R PHASE (Read Data) ---
                for (uint32_t i = 0; i < current_burst_len; ++i) {
                    // Tell the DRAM we are ready
                    rready.write(1);
                    do { wait(); } while (rvalid.read() == 0);
                    
                    float read_val = rdata.read(); // Capture the data
                    rready.write(0);

                    // Transfer the data to the Controller (via our Data Out interface)
                    data_out.write(read_val);
                    data_out_valid.write(1);
                    do { wait(); } while (data_out_ready.read() == 0); // Attente que le Controller prenne la donnée
                    data_out_valid.write(0);
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
            wait(); // Wait for the next cycle if no request
        }
    }
}

// ====================================================================
// WRITE PROCESS: Controller -> DMA -> DRAM
// ====================================================================
void DMA::master_write_thread() {
    // Initialization at Reset
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
                for (uint32_t i = 0; i < current_burst_len; ++i) {
                    // Request the data from the Controller
                    data_in_ready.write(1);
                    do { wait(); } while (data_in_valid.read() == 0);
                    float val_to_write = data_in.read();
                    data_in_ready.write(0);

                    // Push the data to the DRAM
                    wdata.write(val_to_write);
                    wvalid.write(1);
                    wlast.write((i == current_burst_len - 1) ? 1 : 0);

                    do { wait(); } while (wready.read() == 0);
                    wvalid.write(0);
                    wlast.write(0);
                }

                    // --- B PHASE (Write Response) ---
                    // Wait for the DRAM to confirm the burst write succeeded
                bready.write(1);
                do { wait(); } while (bvalid.read() == 0);
                bready.write(0);

                remaining_len -= current_burst_len;
                current_addr += (current_burst_len * 4);
                dram_bytes_written += (current_burst_len * 4);
            }
            
                // The Controller will know it's finished when the DRAM has acknowledged all B responses
                // We could reuse ctrl_done or create a specific signal (ctrl_write_done).
                // For simplicity, we assume read_req and write_req are not asserted at the same time
                // on our basic Controller.
        }
        wait();
    }
}