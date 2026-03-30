# Multi-stage build for NestIQ Hostel Management System
# Stage 1: Build the C++ application
FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libboost-all-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY crow_all.h .
COPY main.cpp .
COPY templates/ templates/

# Build with g++ directly using Boost
RUN g++ -std=c++17 -O2 \
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

EXPOSE 10000

CMD ["./main"]
