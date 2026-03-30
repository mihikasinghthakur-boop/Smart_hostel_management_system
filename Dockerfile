# Multi-stage build for NestIQ Hostel Management System
# Stage 1: Build the C++ application
FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libasio-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY crow_all.h .
COPY main.cpp .
COPY CMakeLists.txt .
COPY templates/ templates/

# Build with g++ directly (simpler than vcpkg for Linux)
RUN g++ -std=c++17 -O2 \
    -DCROW_USE_BOOST=0 \
    -DASIO_STANDALONE \
    -I. \
    main.cpp \
    -o main \
    -lpthread

# Stage 2: Minimal runtime image
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/main .
COPY templates/ templates/

EXPOSE 18080

CMD ["./main"]
