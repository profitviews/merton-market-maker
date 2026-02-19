# Build args for easy retargeting (e.g. Python/Ubuntu updates).
ARG UBUNTU_VERSION=22.04
ARG PYTHON_VERSION=3.9
ARG QUANTLIB_VERSION=1.33
ARG CLANG_P2996_BRANCH=p2996

# --- STAGE 1: Build the Experimental Compiler ---
	FROM ubuntu:${UBUNTU_VERSION} AS builder

	RUN apt-get update && apt-get install -y \
		git cmake ninja-build build-essential python3 lld \
		libedit-dev libncurses-dev
	
	WORKDIR /llvm-project
	ARG CLANG_P2996_BRANCH
	RUN git clone --depth 1 --branch ${CLANG_P2996_BRANCH} https://github.com/bloomberg/clang-p2996.git .
	
	RUN cmake -S llvm -B build -G Ninja \
		-DCMAKE_BUILD_TYPE=Release \
		-DLLVM_ENABLE_PROJECTS="clang" \
		-DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
		-DLLVM_TARGETS_TO_BUILD="X86" \
		-DLLVM_USE_LINKER=lld \
		-DCMAKE_INSTALL_PREFIX=/opt/clang-p2996
	
	RUN ninja -C build install
	
	# --- STAGE 2: Python + QuantLib (P2996 Clang, libc++, QL_USE_STD_SHARED_PTR) ---
	FROM ubuntu:${UBUNTU_VERSION}

	ARG DEBIAN_FRONTEND=noninteractive
	ARG PYTHON_VERSION
	ARG QUANTLIB_VERSION
	ENV TZ=UTC
	ENV PYTHON_VERSION=${PYTHON_VERSION}
	ENV QUANTLIB_VERSION=${QUANTLIB_VERSION}

	RUN apt-get update && apt-get install -y software-properties-common && \
		add-apt-repository universe && \
		add-apt-repository ppa:deadsnakes/ppa && \
		apt-get update && apt-get install -y \
		build-essential cmake ninja-build pkg-config \
		git wget curl ca-certificates \
		python${PYTHON_VERSION} python${PYTHON_VERSION}-dev python${PYTHON_VERSION}-distutils python3-pip \
		libedit2 libboost-all-dev && \
		rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --no-cache-dir pybind11 pytest && \
	python${PYTHON_VERSION} -m pip install --no-cache-dir pybind11 pytest

	# Install just (recipe runner for justfile)
	RUN curl -sSf https://just.systems/install.sh | bash -s -- --to /usr/local/bin

	COPY --from=builder /opt/clang-p2996 /opt/clang-p2996
	ENV PATH="/opt/clang-p2996/bin:$PATH"
	ENV CXX="/opt/clang-p2996/bin/clang++"
	ENV CC="/opt/clang-p2996/bin/clang"

	# Build QuantLib from source (PIC static, std=c++14, QL_USE_STD_SHARED_PTR)
	# and install to /opt/ql_install so CMake can find it when building the module.
	RUN set -e && cd /tmp && \
		wget -q https://github.com/lballabio/QuantLib/releases/download/v${QUANTLIB_VERSION}/QuantLib-${QUANTLIB_VERSION}.tar.gz && \
		tar xzf QuantLib-${QUANTLIB_VERSION}.tar.gz && cd QuantLib-${QUANTLIB_VERSION} && \
		sed -i 's/boost::numeric_cast<Integer>(/static_cast<Integer>(/g' ql/time/daycounters/yearfractiontodate.cpp && \
		CPPFLAGS="-D_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION -DQL_USE_STD_SHARED_PTR" \
		CXXFLAGS="-O3 -fPIC -ffunction-sections -fdata-sections -std=c++14 -stdlib=libc++" LDFLAGS="-stdlib=libc++" \
		./configure --prefix=/opt/ql_install --with-pic --disable-shared --enable-static \
			--disable-test-suite --disable-examples && \
		make -j$(nproc) && make install && \
		cd / && rm -rf /tmp/QuantLib-${QUANTLIB_VERSION} /tmp/QuantLib-${QUANTLIB_VERSION}.tar.gz

	WORKDIR /workspace