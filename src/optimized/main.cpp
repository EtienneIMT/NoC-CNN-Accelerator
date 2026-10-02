#include "systemc.h"
#include "DRAM.h"
#include "DMA.h"
#include "controller.h"
#include "core.h"
#include "router.h"
#include <cstdio>
#include <stdint.h>

int sc_main(int argc, char* argv[])
{
    // =======================
    //   SIGNAL DECLARATIONS
    // =======================
    sc_clock clk("clk", 10, SC_NS);
    sc_signal<bool> rst;

    // --- Signals Controller <-> DMA ---
    sc_signal< uint32_t > ctrl_addr;
    sc_signal< uint32_t > ctrl_len;
    sc_signal< bool >     ctrl_read_req;
    sc_signal< bool >     ctrl_write_req;
    sc_signal< bool >     ctrl_done;

    sc_signal< float >    ctrl_data_out;
    sc_signal< bool >     ctrl_data_out_valid;
    sc_signal< bool >     ctrl_data_out_ready;

    sc_signal< float >    ctrl_data_in;
    sc_signal< bool >     ctrl_data_in_valid;
    sc_signal< bool >     ctrl_data_in_ready;

    // --- Signals AXI4 Bus (DMA <-> DRAM) ---
    // Read Address Channel (AR)
    sc_signal< uint32_t > axi_araddr;
    sc_signal< uint32_t > axi_arlen;
    sc_signal< bool >     axi_arvalid;
    sc_signal< bool >     axi_arready;
    // Read Data Channel (R)
    sc_signal< float >    axi_rdata;
    sc_signal< bool >     axi_rlast;
    sc_signal< bool >     axi_rvalid;
    sc_signal< bool >     axi_rready;
    // Write Address Channel (AW)
    sc_signal< uint32_t > axi_awaddr;
    sc_signal< uint32_t > axi_awlen;
    sc_signal< bool >     axi_awvalid;
    sc_signal< bool >     axi_awready;
    // Write Data Channel (W)
    sc_signal< float >    axi_wdata;
    sc_signal< bool >     axi_wlast;
    sc_signal< bool >     axi_wvalid;
    sc_signal< bool >     axi_wready;
    // Write Response Channel (B)
    sc_signal< bool >     axi_bvalid;
    sc_signal< bool >     axi_bready;

    // =======================================================
    // NEW INJECTION SIGNALS (Dual-Port Controller)
    // =======================================================
    sc_signal< sc_lv<130> > ctrl_flit_tx_0, ctrl_flit_tx_4, ctrl_flit_rx;
    sc_signal< bool >       ctrl_req_tx_0, ctrl_req_tx_4, ctrl_req_rx;
    sc_signal< bool >       ctrl_ack_tx_0, ctrl_ack_tx_4, ctrl_ack_rx;

    // --- Local Signals (Port 4): Cores <-> Routers ---
    // (Note: Index 0 is ignored here because Controller is wired manually)
    sc_signal< sc_lv<130> > c2r_flit[16];
    sc_signal< bool >      c2r_req[16];
    sc_signal< bool >      c2r_ack[16];

    sc_signal< sc_lv<130> > r2c_flit[16];
    sc_signal< bool >      r2c_req[16];
    sc_signal< bool >      r2c_ack[16];

    // Physical Mesh Signals (Ports 0 to 3): Router <-> Router
    sc_signal< sc_lv<130> > r2r_flit[16][4];
    sc_signal< bool >      r2r_req[16][4];
    sc_signal< bool >      r2r_ack[16][4];

    // Dummy Signals (Mesh Boundaries)
    sc_signal< sc_lv<130> > dummy_out_flit[16][4];
    sc_signal< bool >      dummy_out_req[16][4];
    sc_signal< bool >      dummy_in_ack[16][4];
    sc_signal< sc_lv<130> > dummy_in_flit[16][4];
    sc_signal< bool >      dummy_in_req[16][4];
    sc_signal< bool >      dummy_out_ack[16][4];

    // =======================
    //   INSTANTIATION
    // =======================
    DRAM dram("DRAM");
    DMA dma("DMA");
    Controller ctrl("Controller");
    
    Core* cores[16]; // The pointer cores[0] will remain NULL (empty)
    Router* routers[16];

    for(int i=0; i<16; i++) {
        char rname[20], cname[20];
        sprintf(rname, "router_%d", i);
        
        routers[i] = new Router(rname);
        routers[i]->init(i);

        // Instantiate Cores only for IDs 1 to 15
        if (i > 0) {
            sprintf(cname, "core_%d", i);
            cores[i] = new Core(cname, i);
            cores[i]->init(i); 
        } else {
            cores[0] = NULL;
        }
    }

    // =======================
    //   WIRING
    // =======================
    // 1. Wiring DRAM (AXI4 Slave)
    dram.clk(clk); dram.rst(rst);
    dram.araddr(axi_araddr); dram.arlen(axi_arlen); dram.arvalid(axi_arvalid); dram.arready(axi_arready);
    dram.rdata(axi_rdata); dram.rlast(axi_rlast); dram.rvalid(axi_rvalid); dram.rready(axi_rready);
    dram.awaddr(axi_awaddr); dram.awlen(axi_awlen); dram.awvalid(axi_awvalid); dram.awready(axi_awready);
    dram.wdata(axi_wdata); dram.wlast(axi_wlast); dram.wvalid(axi_wvalid); dram.wready(axi_wready);
    dram.bvalid(axi_bvalid); dram.bready(axi_bready);

    // 2. Wiring DMA (AXI4 Master & Controller Interface)
    dma.clk(clk); dma.rst(rst);
    dma.araddr(axi_araddr); dma.arlen(axi_arlen); dma.arvalid(axi_arvalid); dma.arready(axi_arready);
    dma.rdata(axi_rdata); dma.rlast(axi_rlast); dma.rvalid(axi_rvalid); dma.rready(axi_rready);
    dma.awaddr(axi_awaddr); dma.awlen(axi_awlen); dma.awvalid(axi_awvalid); dma.awready(axi_awready);
    dma.wdata(axi_wdata); dma.wlast(axi_wlast); dma.wvalid(axi_wvalid); dma.wready(axi_wready);
    dma.bvalid(axi_bvalid); dma.bready(axi_bready);
    
    dma.ctrl_addr(ctrl_addr); dma.ctrl_len(ctrl_len);
    dma.ctrl_read_req(ctrl_read_req); dma.ctrl_write_req(ctrl_write_req); dma.ctrl_done(ctrl_done);
    dma.data_out(ctrl_data_out); dma.data_out_valid(ctrl_data_out_valid); dma.data_out_ready(ctrl_data_out_ready);
    dma.data_in(ctrl_data_in); dma.data_in_valid(ctrl_data_in_valid); dma.data_in_ready(ctrl_data_in_ready);

    // 3. Wiring Controller (Dual-NIC Setup)
    ctrl.clk(clk); ctrl.rst(rst);
    ctrl.ctrl_addr(ctrl_addr); ctrl.ctrl_len(ctrl_len);
    ctrl.ctrl_read_req(ctrl_read_req); ctrl.ctrl_write_req(ctrl_write_req); ctrl.ctrl_done(ctrl_done);
    ctrl.data_out(ctrl_data_out); ctrl.data_out_valid(ctrl_data_out_valid); ctrl.data_out_ready(ctrl_data_out_ready);
    ctrl.data_in(ctrl_data_in); ctrl.data_in_valid(ctrl_data_in_valid); ctrl.data_in_ready(ctrl_data_in_ready);

    // --- Câblage des deux ports matériels du contrôleur ---
    ctrl.flit_tx_0(ctrl_flit_tx_0); ctrl.req_tx_0(ctrl_req_tx_0); ctrl.ack_tx_0(ctrl_ack_tx_0);
    ctrl.flit_tx_4(ctrl_flit_tx_4); ctrl.req_tx_4(ctrl_req_tx_4); ctrl.ack_tx_4(ctrl_ack_tx_4);
    ctrl.flit_rx(ctrl_flit_rx);     ctrl.req_rx(ctrl_req_rx);     ctrl.ack_rx(ctrl_ack_rx);

    // 4. Mesh Wiring
    for(int i=0; i<16; i++) {
        routers[i]->clk(clk);
        routers[i]->rst(rst);

        // --- Wiring of Local Port (Port 4) ---
        if (i == 0) {
            // Le Routeur 0 écoute le Port 0 du Controller
            routers[0]->in_flit[4](ctrl_flit_tx_0); 
            routers[0]->in_req[4](ctrl_req_tx_0); 
            routers[0]->out_ack[4](ctrl_ack_tx_0);

            // Le Routeur 0 renvoie les données (Résultats) au Controller
            routers[0]->out_flit[4](ctrl_flit_rx); 
            routers[0]->out_req[4](ctrl_req_rx); 
            routers[0]->in_ack[4](ctrl_ack_rx);
        } else {
            // Nodes 1 to 15 connect the Cores to the Routers
            cores[i]->clk(clk); cores[i]->rst(rst);
            
            cores[i]->flit_tx(c2r_flit[i]); cores[i]->req_tx(c2r_req[i]); cores[i]->ack_tx(c2r_ack[i]);
            routers[i]->in_flit[4](c2r_flit[i]); routers[i]->in_req[4](c2r_req[i]); routers[i]->out_ack[4](c2r_ack[i]);

            routers[i]->out_flit[4](r2c_flit[i]); routers[i]->out_req[4](r2c_req[i]); routers[i]->in_ack[4](r2c_ack[i]);
            cores[i]->flit_rx(r2c_flit[i]); cores[i]->req_rx(r2c_req[i]); cores[i]->ack_rx(r2c_ack[i]);
        }

        // --- Wiring of the 4 physical directions ---
        int x = i % 4;
        int y = i / 4;

        // [Port 0: North]
        if (y > 0) { 
            int north = i - 4;
            routers[i]->out_flit[0](r2r_flit[i][0]); routers[i]->out_req[0](r2r_req[i][0]); routers[i]->in_ack[0](r2r_ack[i][0]);
            routers[north]->in_flit[1](r2r_flit[i][0]); routers[north]->in_req[1](r2r_req[i][0]); routers[north]->out_ack[1](r2r_ack[i][0]);
        } else { 
            routers[i]->out_flit[0](dummy_out_flit[i][0]); routers[i]->out_req[0](dummy_out_req[i][0]); routers[i]->in_ack[0](dummy_in_ack[i][0]);
            routers[i]->in_flit[0](dummy_in_flit[i][0]); routers[i]->in_req[0](dummy_in_req[i][0]); routers[i]->out_ack[0](dummy_out_ack[i][0]);
        }

        // [Port 1: South]
        if (y < 3) { 
            int south = i + 4;
            routers[i]->out_flit[1](r2r_flit[i][1]); routers[i]->out_req[1](r2r_req[i][1]); routers[i]->in_ack[1](r2r_ack[i][1]);
            routers[south]->in_flit[0](r2r_flit[i][1]); routers[south]->in_req[0](r2r_req[i][1]); routers[south]->out_ack[0](r2r_ack[i][1]);
        } else {
            routers[i]->out_flit[1](dummy_out_flit[i][1]); routers[i]->out_req[1](dummy_out_req[i][1]); routers[i]->in_ack[1](dummy_in_ack[i][1]);
            routers[i]->in_flit[1](dummy_in_flit[i][1]); routers[i]->in_req[1](dummy_in_req[i][1]); routers[i]->out_ack[1](dummy_out_ack[i][1]);
        }

        // [Port 2: East]
        if (x < 3) { 
            int east = i + 1;
            routers[i]->out_flit[2](r2r_flit[i][2]); routers[i]->out_req[2](r2r_req[i][2]); routers[i]->in_ack[2](r2r_ack[i][2]);
            routers[east]->in_flit[3](r2r_flit[i][2]); routers[east]->in_req[3](r2r_req[i][2]); routers[east]->out_ack[3](r2r_ack[i][2]);
        } else {
            routers[i]->out_flit[2](dummy_out_flit[i][2]); routers[i]->out_req[2](dummy_out_req[i][2]); routers[i]->in_ack[2](dummy_in_ack[i][2]);
            routers[i]->in_flit[2](dummy_in_flit[i][2]); routers[i]->in_req[2](dummy_in_req[i][2]); routers[i]->out_ack[2](dummy_out_ack[i][2]);
        }

        // [Port 3: West]
        if (x > 0) { 
            int west = i - 1;
            routers[i]->out_flit[3](r2r_flit[i][3]); routers[i]->out_req[3](r2r_req[i][3]); routers[i]->in_ack[3](r2r_ack[i][3]);
            routers[west]->in_flit[2](r2r_flit[i][3]); routers[west]->in_req[2](r2r_req[i][3]); routers[west]->out_ack[2](r2r_ack[i][3]);
        } else {
            routers[i]->out_flit[3](dummy_out_flit[i][3]); routers[i]->out_req[3](dummy_out_req[i][3]); routers[i]->in_ack[3](dummy_in_ack[i][3]);
            
            // ==============================================================
            // Port 4 of Controller comes in from the West of Router 4
            // ==============================================================
            if (i == 4) {
                routers[i]->in_flit[3](ctrl_flit_tx_4);
                routers[i]->in_req[3](ctrl_req_tx_4);
                routers[i]->out_ack[3](ctrl_ack_tx_4);
            } else {
                routers[i]->in_flit[3](dummy_in_flit[i][3]); 
                routers[i]->in_req[3](dummy_in_req[i][3]); 
                routers[i]->out_ack[3](dummy_out_ack[i][3]);
            }
        }
    }

    // =======================
    //   START SIMULATION
    // =======================
    rst.write(1);
    sc_start(20, SC_NS); 
    rst.write(0);        
    sc_start(20, SC_NS); 

    sc_start(); // The simulation will run until the Controller calls sc_stop()

    // ========================================================
    // POST-SIMULATION : MEASURES AND EXPORT CSV
    // ========================================================
    unsigned long long total_macs = 0;
    unsigned long long total_pe_cycles = 0;

    for (int i = 1; i < 16; i++) {
        if (cores[i] != NULL) {
            total_pe_cycles += cores[i]->pe.compute_cycles_counter;
            total_macs += cores[i]->pe.mac_operations_counter;
        }
    }

    double baseline_mac_units = 4.0; 
    double compute_cycles = total_macs / baseline_mac_units;

    double total_time_ns = sc_time_stamp().to_double();
    double noc_sim_cycles = total_time_ns / 10.0;

    double total_sim_cycles = noc_sim_cycles + compute_cycles;
    
    double pe_utilization = (compute_cycles / (total_sim_cycles * 8.0)) * 100.0;

    double global_sram_kb = (ctrl.max_sram_fifo_size * 4.0) / 1024.0;
    double max_local_sram_mb = 2.0;
    double dram_access_mb = ((150528.0 + 60965376.0 + 10624.0 + 1000.0) * 4.0) / (1024.0 * 1024.0);

    const char* env_file = getenv("IMAGE_FILE_NAME");
    std::string current_image = (env_file != NULL) ? env_file : "cat.txt";
    
    std::ofstream report_file("hardware_metrics.csv", std::ios_base::app);
    if (report_file.is_open()) {
        report_file.seekp(0, std::ios::end);
        if (report_file.tellp() == 0) {
            report_file << "Architecture,Image,Total_Cycles,Global_SRAM_KB,Local_SRAM_Max_MB,DRAM_Access_MB,PE_Utilization_Pct,Total_MAC_Ops\n";
        }
        
        report_file << "Optimized," 
                    << current_image << "," 
                    << total_sim_cycles << "," 
                    << global_sram_kb << ","
                    << max_local_sram_mb << ","
                    << dram_access_mb << ","
                    << pe_utilization << ","
                    << total_macs << "\n";
                    
        report_file.close();
        std::cout << "\n[Info] Metrics successfully collected from 16 PEs and exported to hardware_metrics.csv\n";
    }

    for (int i = 0; i < 16; i++) {
        delete routers[i];
        if (i > 0) delete cores[i];
    }

    return 0;
}