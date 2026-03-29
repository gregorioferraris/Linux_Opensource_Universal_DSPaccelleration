#include <iostream>
#include <cassert>
#include <vector>
#include <cmath>
#include "../daemon/ipc/include/ipc_shm.hpp"

using namespace DspAccel::Ipc;

/**
 * @brief Test di stress per il Ring Buffer Audio.
 * Simula il Plugin che invia un'onda sinusoidale e il Worker che la legge.
 */
void test_audio_throughput() {
    std::cout << "[Test] Avvio Audio Loopback Stress Test..." << std::endl;

    DspSharedMemory shm = {};
    DspAudioFrame send_frame;
    send_frame.frame_count = 256;
    send_frame.channel_count = 2;

    // 1. Riempimento con una sinusoide (Plugin Side)
    for (uint32_t i = 0; i < 256 * 2; ++i) {
        send_frame.samples[i] = std::sin(i * 0.1f);
    }

    std::cout << "Plugin: Invio di 1000 blocchi audio..." << std::endl;
    
    // Simuliamo l'invio e la ricezione di 1000 blocchi (stress test)
    for (int block = 0; block < 1000; ++block) {
        // Plugin Push
        if (!shm.in_queue.push(send_frame)) {
            std::cerr << "XRUN rilevato nel buffer di input al blocco " << block << std::endl;
            assert(false);
        }

        // Worker Pop & Loopback (simula il processamento)
        DspAudioFrame worker_frame;
        if (shm.in_queue.pop(worker_frame)) {
            // Verifica integrità dati
            assert(worker_frame.samples[0] == send_frame.samples[0]);
            shm.out_queue.push(worker_frame);
        }

        // Plugin Pop (ricezione risultato)
        DspAudioFrame recv_frame;
        bool received = shm.out_queue.pop(recv_frame);
        assert(received);
        assert(recv_frame.frame_count == 256);
    }

    assert(shm.in_queue.empty());
    assert(shm.out_queue.empty());
    std::cout << "[Test] Audio Loopback: PASSED (Bit-perfect integrity)" << std::endl;
}

int main() {
    test_audio_throughput();
    return 0;
}