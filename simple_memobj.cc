/*
 * Copyright (c) 2017 Jason Lowe-Power
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "learning_gem5/part2/simple_memobj.hh"

#include "base/trace.hh"
#include "debug/SimpleMemobj.hh"
#include <cmath>
#include <algorithm>
#include <string>
#include <cassert>
#include <vector>
namespace gem5
{

SimpleMemobj::SimpleMemobj(const SimpleMemobjParams &params) :
    SimObject(params),
    instPort(params.name + ".inst_port", this),
    dataPort(params.name + ".data_port", this),
    memPort(params.name + ".mem_side", this),
    blocked(false)
{
}

Port &
SimpleMemobj::getPort(const std::string &if_name, PortID idx)
{
    panic_if(idx != InvalidPortID, "This object doesn't support vector ports");

    // This is the name from the Python SimObject declaration (SimpleMemobj.py)
    if (if_name == "mem_side") {
        return memPort;
    } else if (if_name == "inst_port") {
        return instPort;
    } else if (if_name == "data_port") {
        return dataPort;
    } else {
        // pass it along to our super class
        return SimObject::getPort(if_name, idx);
    }
}

void
SimpleMemobj::CPUSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the memobj is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingResp(pkt)) {
        blockedPacket = pkt;
    }
}

AddrRangeList
SimpleMemobj::CPUSidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

void
SimpleMemobj::CPUSidePort::trySendRetry()
{
    if (needRetry && blockedPacket == nullptr) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(SimpleMemobj, "Sending retry req for %d\n", id);
        sendRetryReq();
    }
}

void
SimpleMemobj::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    // Just forward to the memobj.
    return owner->handleFunctional(pkt);
}

bool
SimpleMemobj::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    // Just forward to the memobj.
    if (!owner->handleRequest(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
}

void
SimpleMemobj::CPUSidePort::recvRespRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);
}

void
SimpleMemobj::MemSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the memobj is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }
}

bool
SimpleMemobj::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    // Just forward to the memobj.
    return owner->handleResponse(pkt);
}

void
SimpleMemobj::MemSidePort::recvReqRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);
}

void
SimpleMemobj::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

bool
SimpleMemobj::handleRequest(PacketPtr pkt)
{
    if (blocked) {
        // There is currently an outstanding request. Stall.
        return false;
    }

    DPRINTF(SimpleMemobj, "Got request for addr %#x\n", pkt->getAddr());
    if (pkt->isWrite()) {
        // If it is a write, encode the data using ECC
        uint64_t addr = pkt->getAddr();
        uint8_t* data = pkt->getPtr<uint8_t>();
        unsigned size = pkt->getSize();
        DPRINTF(SimpleMemobj, "handleRequest pkt: ");
        for(size_t i=0; i<size; ++i)
        {
            DPRINTF(SimpleMemobj, "%u ", data[i]);
        }
        encodeECC(pkt);
        DPRINTF(EccMemobj, "parity bits in address %llu is %u\n", addr, hammingMap[addr]);
        flipRandomBit(data, size);
    }

    // This memobj is now blocked waiting for the response to this packet.
    blocked = true;
    // Simply forward to the memory port
    memPort.sendPacket(pkt);

    return true;
}

bool
SimpleMemobj::handleResponse(PacketPtr pkt)
{
    assert(blocked);
    DPRINTF(SimpleMemobj, "Got response for addr %#x\n", pkt->getAddr());

    // The packet is now done. We're about to put it in the port, no need for
    // this object to continue to stall.
    // We need to free the resource before sending the packet in case the CPU
    // tries to send another request immediately (e.g., in the same callchain).
    blocked = false;

    if (pkt->isRead()) {
        // If it is a read, decode and check the data using ECC
        uint64_t addr = pkt->getAddr();
        uint8_t* data = pkt->getPtr<uint8_t>();
        if(SimpleMemobj.find(addr) != SimpleMemobj.end())
        {            
            unsigned size = pkt->getSize();
            DPRINTF(SimpleMemobj, "handleResponse pkt:");
            for(size_t i= 0; i<size; i++ ){
                DPRINTF(SimpleMemobj, "%u ", data[i]);
            }
            DPRINTF(SimpleMemobj, "parity bits in address %llu is %u\n", addr, hammingMap[addr]);
            decodeECC(pkt);
        }
    }
    // Simply forward to the memory port
    if (pkt->req->isInstFetch()) {
        instPort.sendPacket(pkt);
    } else {
        dataPort.sendPacket(pkt);
    }

    // For each of the cpu ports, if it needs to send a retry, it should do it
    // now since this memory object may be unblocked now.
    instPort.trySendRetry();
    dataPort.trySendRetry();

    return true;
}

void
SimpleMemobj::handleFunctional(PacketPtr pkt)
{
    // Just pass this on to the memory side to handle for now.
    memPort.sendFunctional(pkt);
}

AddrRangeList
SimpleMemobj::getAddrRanges() const
{
    DPRINTF(SimpleMemobj, "Sending new ranges\n");
    // Just use the same ranges as whatever is on the memory side.
    return memPort.getAddrRanges();
}

void
SimpleMemobj::sendRangeChange()
{
    instPort.sendRangeChange();
    dataPort.sendRangeChange();
}

void SimpleMemobj::encodeECC(PacketPtr pkt)
{
    uint64_t addr = pkt->getAddr();
    uint8_t* data = pkt->getPtr<uint8_t>();
    unsigned size = pkt->getSize();
    int binLength = calBitsLen(size);
    char* binaryString = new char[binLength + 1];
    //convert to binary
    for (size_t x = 0; x < size; x++) {
        uint8_t* n = data + x;
        for (int i = 0; i < 8; i++) {
            binaryString[binLength - 1 - 8 * (size - x - 1) - i] = ((*n >> i) & 1) ? '1' : '0';
        }
    }
    binaryString[binLength] = '\0';
    
    std::vector<int> encode(binaryString.begin(), binaryString.end());

    // Determine Hamming code length
    int n = encode.size();
    int r = 0;
    while (pow(2, r) < r + n + 1) {
        ++r;
    }

    // Reverse the input code
    std::reverse(encode.begin(), encode.end());

    // Initialize array for Hamming code
    std::vector<int> arr;
    int j = 0;
    int codePos = 0;
    for (int i = 0; i < r + n; ++i) {
        if (i + 1 == pow(2, j)) {
            arr.push_back(0);
            ++j;
        } else {
            arr.push_back(encode[codePos] - '0');
            ++codePos;
        }
    }

    // Calculate Hamming code
    j = 0;
    std::vector<int> parityBits;
    for (int parity = 0; parity < arr.size(); ++parity) {
        if (parity + 1 == pow(2, j)) {
            std::vector<int> Xor;
            int Starti = pow(2, j) - 1;
            int index = Starti;
            while (index < arr.size()) {
                std::vector<int> block(arr.begin() + index, arr.begin() + index + pow(2, j));
                Xor.insert(Xor.end(), block.begin(), block.end());
                index += pow(2, j + 1);
            }
            for (int z = 1; z < Xor.size(); ++z) {
                arr[Starti] = arr[Starti] ^ Xor[z];
            }
            parityBits.push_back(arr[Starti]);
            ++j;
        }
    }

    // Reverse the Hamming code
    std::reverse(arr.begin(), arr.end());

    // 存入map中
    hammingMap[addr] = parityBits;

    return;
}

// 隨機翻轉位元的函數
void flipRandomBit(uint8_t* data, unsigned size)
{
    // 通過隨機數生成器生成隨機位元的索引
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, size - 1);
    int index = dis(gen);

    // 翻轉選定的位元
    data[index] ^= 0x01;
}

void SimpleMemobj::decodeECC(PacketPtr pkt)
{
    uint64_t addr = pkt->getAddr();
    uint8_t* data = pkt->getPtr<uint8_t>();
    unsigned size = pkt->getSize();
    int binLength = calBitsLen(size);
    char* binaryString = new char[binLength + 1];
    //convert to binary
    for (size_t x = 0; x < size; x++) {
        uint8_t* n = data + x;
        for (int i = 0; i < 8; i++) {
            binaryString[binLength - 1 - 8 * (size - x - 1) - i] = ((*n >> i) & 1) ? '1' : '0';
        }
    }
    binaryString[binLength] = '\0';
    
    std::vector<int> decode(binaryString.begin(), binaryString.end());

    int n = decode.size();

    if (n <= 2)
        std::cout << "Encoding Signal error, check the length!" << std::endl;

  
    auto max2r = [](int N) {
        int r = N - 1;
        r |= r >> 1;
        r |= r >> 2;
        r |= r >> 4;
        r |= r >> 8;
        r |= r >> 16;
        return (r + 1 == N) ? N : (r + 1) >> 1;
    };

    int rmax = max2r(n);
    int r = (int) (log2(rmax)) + 1;
    int k = n - r;

    std::vector<int> sum(r, 0);
    int temp = 1;
    for (auto i = 0; i < r; i++) {
        for (auto j = 0; j < n; j++) {
            if (temp & (j + 1)) {
                sum[i] = sum[i] + x[j];
            }
        }

        if (sum[i] & 1)
            sum[i] = 1;
        else
            sum[i] = 0;
        temp = temp << 1;
    }

    //binary to decimal
    auto binary2decimal = [&sum](int r) {
        auto pos = 0;
        for (auto i = 0; i < r; i++) {
            auto temp = sum[i] << i;
            pos = pos | temp;
        }
        return pos;
    };
    //correct bit
    auto pos = binary2decimal(r);
    if (pos)
        x[pos - 1] ^= 1;

    std::vector<int> x_decoding(k, 0);

    auto is2n = [](int N) { if ((N & (N - 1)) == 0 && N > 0) return 1; else return 0; };

    auto j = 0;
    for (auto i = 1; i <= n; i++) {
        if (is2n(i))
            continue;
        x_decoding[j] = x[i - 1];
        j++;
    }

    return x_decoding;
}




} // namespace gem5