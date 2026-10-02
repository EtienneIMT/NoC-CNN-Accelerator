FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    wget \
    tar \
    && rm -rf /var/lib/apt/lists/*

# Download and build SystemC 2.3.3
WORKDIR /opt
RUN wget -q https://accellera.org/images/downloads/standards/systemc/systemc-2.3.3.tar.gz && \
    tar -xzf systemc-2.3.3.tar.gz && \
    cd systemc-2.3.3 && \
    mkdir objdir && cd objdir && \
    ../configure --prefix=/usr/local/systemc-2.3.3 && \
    make -j$(nproc) && make install && \
    rm -rf /opt/systemc-2.3.3.tar.gz

# Set up environment variables
ENV SYSTEMC_HOME=/usr/local/systemc-2.3.3
ENV LD_LIBRARY_PATH=$SYSTEMC_HOME/lib-linux64:$LD_LIBRARY_PATH

# Set up working directory
WORKDIR /app
COPY . /app/

# The default command will be to compile the baseline as a test
CMD ["/bin/bash", "-c", "cd src/baseline && make cat"]
