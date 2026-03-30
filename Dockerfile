# Multi-stage build for NestIQ Hostel Management System
# Stage 1: Build the C++ application
FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libboost-all-dev \
    libasio-dev \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY crow_all.h .
COPY sqlite3.h .
COPY sqlite3.c .
COPY main.cpp .
COPY templates/ templates/

# Build with g++ directly (compile sqlite3.c as C, link with main.cpp)
RUN gcc -c -O2 sqlite3.c -o sqlite3.o && \
    g++ -std=c++17 -O2 \
    -I. \
    main.cpp sqlite3.o \
    -o main \
    -lpthread -ldl

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
