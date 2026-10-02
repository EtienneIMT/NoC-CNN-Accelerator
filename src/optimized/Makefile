SYSTEMC_HOME ?= /usr/local/systemc-2.3.3
LIB_DIR = $(SYSTEMC_HOME)/lib-linux64
INC_DIR = $(SYSTEMC_HOME)/include
LIB = -lsystemc-2.3.3
RPATH = -Wl,-rpath,$(SYSTEMC_HOME)/lib-linux64

O = run
C = *.cpp

# Default target
all: $(O)

# Compile and link
$(O): $(C)
	g++ -I . -I  $(INC_DIR) -L . -L $(LIB_DIR) -o $(O) $(C) $(LIB) $(RPATH) -g

# Run with cat.txt
cat: $(O)
	IMAGE_FILE_NAME=cat.txt ./run

# Run with dog.txt
dog: $(O)
	IMAGE_FILE_NAME=dog.txt ./run

# Clean up compiled files
clean:
	rm -rf $(O)