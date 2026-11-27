# TENSOR-CORE toolchain image (every version pinned). Built by docker/run.sh.

ARG UBUNTU=ubuntu:24.04@sha256:562111b88caa79c71391c0c4d3c2114a7d5c35915c20561fd893cb1e07929e6b

# ---------------------------------------------------------------- apt base
FROM ${UBUNTU} AS apt-base
ARG APT_SNAPSHOT=20261120T000000Z
ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 LC_ALL=C.UTF-8
# The bare image has no CA certificates, so fetch them once from the live archive,
# then freeze apt to a dated snapshot of the Ubuntu archive. Packages are GPG-checked either way.
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates \
 && echo "APT::Snapshot \"${APT_SNAPSHOT}\";" > /etc/apt/apt.conf.d/50snapshot \
 && apt-get update \
 && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------- OpenSTA (built from source)
FROM apt-base AS sta-build
ARG CUDD_COMMIT=f54f533303640afd5dbe47a05ebeabb3066f2a25
ARG OPENSTA_COMMIT=39f9dec68a07d658b38d7d99a493479e077c1c30
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential git cmake swig bison flex libfl-dev tcl-dev libeigen3-dev zlib1g-dev \
      autoconf automake libtool \
 && rm -rf /var/lib/apt/lists/*
RUN git clone https://github.com/cuddorg/cudd.git /src/cudd \
 && cd /src/cudd && git checkout ${CUDD_COMMIT} \
 && autoreconf -fi && ./configure --prefix=/opt/cudd && make -j"$(nproc)" && make install
RUN git clone https://github.com/The-OpenROAD-Project/OpenSTA.git /src/OpenSTA \
 && cd /src/OpenSTA && git checkout ${OPENSTA_COMMIT} \
 && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCUDD_DIR=/opt/cudd \
          -DBUILD_TESTS=OFF -DUSE_TCL_READLINE=OFF -DCMAKE_INSTALL_PREFIX=/opt/sta \
 && cmake --build build -j"$(nproc)" && cmake --install build

# ---------------------------------------------------------------- RISC-V GCC (xPack)
FROM apt-base AS gcc-fetch
ARG XPACK_VER=15.2.0-1
ARG XPACK_SHA256=aaaa8060c914851a3e5ee1ba82cc3d6f80972f90638a05c6e823a37557a33758
RUN apt-get update && apt-get install -y --no-install-recommends curl && rm -rf /var/lib/apt/lists/*
RUN f=xpack-riscv-none-elf-gcc-${XPACK_VER}-linux-x64.tar.gz \
 && curl -fsSL -o /tmp/$f https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v${XPACK_VER}/$f \
 && echo "${XPACK_SHA256}  /tmp/$f" | sha256sum -c - \
 && mkdir -p /opt/riscv && tar -xzf /tmp/$f -C /opt/riscv --strip-components=1 \
 && rm -rf /opt/riscv/share/doc
# The docs call the compiler riscv32-unknown-elf-gcc; expose every tool under that prefix too.
RUN cd /opt/riscv/bin && for t in riscv-none-elf-*; do ln -s "$t" "riscv32-unknown-elf-${t#riscv-none-elf-}"; done

# ---------------------------------------------------------------- sky130 liberty (typical corner)
FROM apt-base AS pdk-fetch
ARG CIEL_VER=3.3.0
ARG PDK_COMMIT=2fdf40d5736413bb321c31418462aefc17b919b8
ARG LIB_SHA256=8e78e14442062dba34d414fca6490b2f6b96038d4510d1438ca44fee31487135
RUN apt-get update && apt-get install -y --no-install-recommends python3 python3-venv && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /tmp/ciel && /tmp/ciel/bin/pip install -q ciel==${CIEL_VER} \
 && /tmp/ciel/bin/ciel fetch --pdk-family sky130 --pdk-root /tmp/pdk -l sky130_fd_sc_hd ${PDK_COMMIT} \
 && mkdir -p /opt/sky130 \
 && cp /tmp/pdk/ciel/sky130/versions/${PDK_COMMIT}/sky130A/libs.ref/sky130_fd_sc_hd/lib/sky130_fd_sc_hd__tt_025C_1v80.lib /opt/sky130/ \
 && echo "${LIB_SHA256}  /opt/sky130/sky130_fd_sc_hd__tt_025C_1v80.lib" | sha256sum -c -

# ---------------------------------------------------------------- kit image
FROM apt-base AS kit
RUN apt-get update && apt-get install -y --no-install-recommends \
      verilator=5.020-1 yosys=0.33-5build2 gtkwave=3.3.116-1build2 \
      build-essential make git curl ca-certificates xz-utils \
      python3 python3-venv tcl8.6 zlib1g zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*
COPY --from=sta-build /opt/sta   /opt/sta
COPY --from=gcc-fetch /opt/riscv /opt/riscv
COPY --from=pdk-fetch /opt/sky130 /opt/sky130
COPY docker/requirements-kit.txt /opt/requirements-kit.txt
RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --no-cache-dir -r /opt/requirements-kit.txt
ENV PATH=/opt/venv/bin:/opt/riscv/bin:/opt/sta/bin:$PATH \
    SKY130_LIB=/opt/sky130/sky130_fd_sc_hd__tt_025C_1v80.lib \
    TC_IMAGE=kit
WORKDIR /work
