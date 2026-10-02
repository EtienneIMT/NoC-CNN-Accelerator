#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include <stdint.h>

// ------------------------------------------------------------------
// SYSTEMC MEMORY MAP FOR DRAM (Addresses are in Bytes)
// Note: Each float takes 4 Bytes.
// ------------------------------------------------------------------

// --- BASE ADDRESSES ---
const uint32_t MEM_ADDR_IMAGE       = 0x00000000; // Start of memory

// Weights - Spaced widely apart to avoid overlaps
const uint32_t MEM_ADDR_W_L1        = 0x01000000; 
const uint32_t MEM_ADDR_W_L2        = 0x02000000; 
const uint32_t MEM_ADDR_W_L3        = 0x03000000; 
const uint32_t MEM_ADDR_W_L4        = 0x04000000; 
const uint32_t MEM_ADDR_W_L5        = 0x05000000; 
const uint32_t MEM_ADDR_W_L6        = 0x10000000; // FC6 layer (Very large, >150MB)
const uint32_t MEM_ADDR_W_L7        = 0x20000000; // FC7 layer (~67MB)
const uint32_t MEM_ADDR_W_L8        = 0x30000000; // FC8 layer (~16MB)

// Biases
const uint32_t MEM_ADDR_B_L1        = 0x40000000; 
const uint32_t MEM_ADDR_B_L2        = 0x40010000; 
const uint32_t MEM_ADDR_B_L3        = 0x40020000; 
const uint32_t MEM_ADDR_B_L4        = 0x40030000; 
const uint32_t MEM_ADDR_B_L5        = 0x40040000; 
const uint32_t MEM_ADDR_B_L6        = 0x40050000; 
const uint32_t MEM_ADDR_B_L7        = 0x40060000; 
const uint32_t MEM_ADDR_B_L8        = 0x40070000; 

// Space to write final results (The 1000 probabilities)
const uint32_t MEM_ADDR_RESULT_OUT  = 0x80000000; 


// --- DATA SIZES (in number of floats) ---
// Useful for configuring AXI4 Burst lengths (ARLEN / AWLEN)
const int SIZE_IMAGE = 150528;

const int SIZE_W_L1 = 23232;    const int SIZE_B_L1 = 64;
const int SIZE_W_L2 = 307200;   const int SIZE_B_L2 = 192;
const int SIZE_W_L3 = 663552;   const int SIZE_B_L3 = 384;
const int SIZE_W_L4 = 884736;   const int SIZE_B_L4 = 256;
const int SIZE_W_L5 = 589824;   const int SIZE_B_L5 = 256;
const int SIZE_W_L6 = 37748736; const int SIZE_B_L6 = 4096;
const int SIZE_W_L7 = 16777216; const int SIZE_B_L7 = 4096;
const int SIZE_W_L8 = 4096000;  const int SIZE_B_L8 = 1000;

#endif // MEMORY_MAP_H