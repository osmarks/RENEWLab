/*
 Copyright (c) 2018-2021, Rice University
 RENEW OPEN SOURCE LICENSE: http://renew-wireless.org/license
 
----------------------------------------------------------
 Handles received samples from massive-mimo base station 
----------------------------------------------------------
*/

#include "include/receiver.h"
#include "include/ClientRadioSet.h"
#include "include/comms-lib.h"
#include "include/logger.h"
#include "include/macros.h"
#include "include/utils.h"

#include <SoapySDR/Time.hpp>
#include <atomic>
#include <random>
#include <unistd.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

Receiver::Receiver(
    Config* config, moodycamel::ConcurrentQueue<Event_data>* in_queue)
    : config_(config)
    , message_queue_(in_queue)
{
    /* initialize random seed: */
    srand(time(NULL));

    MLPD_TRACE("Receiver Construction - CL present: %d, BS Present: %d\n",
        config_->client_present(), config_->bs_present());

    try {
        this->clientRadioSet_
            = config_->client_present() ? new ClientRadioSet(config_) : nullptr;
        this->base_radio_set_
            = config_->bs_present() ? new BaseRadioSet(config_) : nullptr;
    } catch (std::exception& e) {
        throw ReceiverException("Invalid Radio Setup");
    }

    MLPD_TRACE("Receiver Construction -- number radios %zu\n",
        config_->num_bs_sdrs_all());

    if (((this->base_radio_set_ != nullptr)
            && (this->base_radio_set_->getRadioNotFound()))
        || ((this->clientRadioSet_ != nullptr)
               && (this->clientRadioSet_->getRadioNotFound()))) {
        if (this->base_radio_set_ != nullptr) {
            MLPD_WARN("Invalid Base Radio Setup: %d\n",
                this->base_radio_set_ == nullptr);
            this->base_radio_set_->radioStop();
            delete this->base_radio_set_;
        }
        if (this->clientRadioSet_ != nullptr) {
            MLPD_WARN("Invalid Client Radio Setup: %d\n",
                this->clientRadioSet_ == nullptr);
            this->clientRadioSet_->radioStop();
            delete this->clientRadioSet_;
        }
        throw ReceiverException("Invalid Radio Setup");
    }
    MLPD_TRACE("Construction complete\n");
}

Receiver::~Receiver()
{
    MLPD_TRACE("Radio Set cleanup, Base: %d, Client: %d\n",
        this->base_radio_set_ == nullptr, this->clientRadioSet_ == nullptr);
    if (this->base_radio_set_ != nullptr) {
        this->base_radio_set_->radioStop();
        delete this->base_radio_set_;
    }
    if (this->clientRadioSet_ != nullptr) {
        this->clientRadioSet_->radioStop();
        delete this->clientRadioSet_;
    }
}

std::vector<pthread_t> Receiver::startClientThreads(
    SampleBuffer* rx_buffer, unsigned in_core_id)
{
    std::vector<pthread_t> client_threads;
    if (config_->client_present() == true) {
        client_threads.resize(config_->num_cl_sdrs());
        for (unsigned int i = 0; i < config_->num_cl_sdrs(); i++) {
            pthread_t cl_thread_;
            // record the thread id
            ReceiverContext* context = new ReceiverContext;
            context->ptr = this;
            context->core_id = in_core_id;
            context->tid = i;
            context->buffer = rx_buffer;
            // start socket thread
            if (pthread_create(
                    &cl_thread_, NULL, Receiver::clientTxRx_launch, context)
                != 0) {
                MLPD_ERROR("Socket client thread create failed in start client "
                           "threads");
                throw std::runtime_error("Socket client thread create failed "
                                         "in start client threads");
            }
            client_threads[i] = cl_thread_;
        }
    }
    return client_threads;
}

std::vector<pthread_t> Receiver::startRecvThreads(
    SampleBuffer* rx_buffer, size_t n_rx_threads, unsigned in_core_id)
{
    assert(rx_buffer[0].buffer.size() != 0);
    thread_num_ = n_rx_threads;
    std::vector<pthread_t> created_threads;
    created_threads.resize(this->thread_num_);
    for (size_t i = 0; i < this->thread_num_; i++) {
        // record the thread id
        ReceiverContext* context = new ReceiverContext;
        context->ptr = this;
        context->core_id = in_core_id;
        context->tid = i;
        context->buffer = rx_buffer;
        // start socket thread
        if (pthread_create(&created_threads.at(i), NULL,
                Receiver::loopRecv_launch, context)
            != 0) {
            MLPD_ERROR("Socket recv thread create failed");
            throw std::runtime_error("Socket recv thread create failed");
        }
    }
    sleep(1);
    pthread_cond_broadcast(&cond);
    go();
    return created_threads;
}

void Receiver::completeRecvThreads(const std::vector<pthread_t>& recv_thread)
{
    for (std::vector<pthread_t>::const_iterator it = recv_thread.begin();
         it != recv_thread.end(); ++it) {
        pthread_join(*it, NULL);
    }
}

void Receiver::go()
{
    if (this->base_radio_set_ != NULL) {
        this->base_radio_set_->radioStart(); // hardware trigger
    }
}

void* Receiver::loopRecv_launch(void* in_context)
{
    ReceiverContext* context = (ReceiverContext*)in_context;
    auto me = context->ptr;
    auto tid = context->tid;
    auto core_id = context->core_id;
    auto buffer = context->buffer;
    delete context;
    me->loopRecv(tid, core_id, buffer);
    return 0;
}

void Receiver::loopRecv(int tid, int core_id, SampleBuffer* rx_buffer)
{
    if (config_->core_alloc() == true) {
        MLPD_INFO("Pinning rx thread %d to core %d\n", tid, core_id + tid);
        if (pin_to_core(core_id + tid) != 0) {
            MLPD_ERROR(
                "Pin rx thread %d to core %d failed\n", tid, core_id + tid);
            throw std::runtime_error("Pin rx thread to core failed");
        }
    }

    // Use mutex to sychronize data receiving across threads
    if (config_->internal_measurement()
        || ((config_->num_cl_sdrs() > 0) && (config_->num_bs_sdrs_all() > 0))) {
        pthread_mutex_lock(&mutex);
        MLPD_INFO("Recv Thread %d: waiting for release\n", tid);
        pthread_cond_wait(&cond, &mutex);
        pthread_mutex_unlock(&mutex); // unlocking for all other threads
    }

    // use token to speed up
    moodycamel::ProducerToken local_ptok(*message_queue_);

    const size_t num_channels = config_->bs_channel().length();
    size_t packageLength = sizeof(Package) + config_->getPackageDataLength();
    int buffer_chunk_size = rx_buffer[0].buffer.size() / packageLength;

    // handle two channels at each radio
    // this is assuming buffer_chunk_size is at least 2
    std::atomic_int* pkg_buf_inuse = rx_buffer[tid].pkg_buf_inuse;
    char* buffer = rx_buffer[tid].buffer.data();

    size_t num_radios = config_->num_bs_sdrs_all(); //config_->n_bs_sdrs()[0]
    std::vector<size_t> radio_ids_in_thread;
    if (config_->internal_measurement() && config_->ref_node_enable()) {
        if (tid == 0)
            radio_ids_in_thread.push_back(config_->cal_ref_sdr_id());
        else
            // FIXME: Does this work in multi-cell case?
            for (size_t it = 0; it < config_->num_bs_sdrs_all(); it++)
                if (it != config_->cal_ref_sdr_id())
                    radio_ids_in_thread.push_back(it);
    } else {
        size_t radio_start = (tid * num_radios) / thread_num_;
        size_t radio_end = ((tid + 1) * num_radios) / thread_num_;
        for (size_t it = radio_start; it < radio_end; it++)
            radio_ids_in_thread.push_back(it);
    }
    MLPD_INFO(
        "Receiver thread %d has %zu radios\n", tid, radio_ids_in_thread.size());
    MLPD_TRACE(
        " -- %d - radio start: %zu, end: %zu, total radios %zu, thread: %d\n",
        tid, radio_ids_in_thread.front(), radio_ids_in_thread.back(),
        num_radios, thread_num_);

    // prepare BS beacon in host buffer
    std::vector<void*> beaconbuff(2);
    void* zeroes_memory
        = calloc(config_->samps_per_slot(), sizeof(int16_t) * 2);

    if (zeroes_memory == NULL) {
        throw std::runtime_error("Memory allocation error");
    }

    MLPD_SYMBOL(
        "Process %d -- Loop Rx Allocated memory at: %p, approx size: %lu\n",
        tid, zeroes_memory, (sizeof(int16_t) * 2) * config_->samps_per_slot());
    beaconbuff.at(0u) = config_->beacon_ci16().data();
    beaconbuff.at(1u) = zeroes_memory;

    long long rxTimeBs(0);
    long long txTimeBs(0);

    // read rx_offset to align the FPGA time of the BS
    // by performing dummy readStream()
    std::vector<std::complex<int16_t>> samp_buffer0(
        config_->samps_per_slot() * config_->slot_per_frame(), 0);
    std::vector<std::complex<int16_t>> samp_buffer1(
        config_->samps_per_slot() * config_->slot_per_frame(), 0);
    std::vector<void*> samp_buffer(2);
    samp_buffer[0] = samp_buffer0.data();
    if (num_channels == 2)
        samp_buffer[1] = samp_buffer1.data();

    FILE* fp = nullptr;
    if (config_->dl_data_slot_present() == true) {
        std::printf("Opening DL time-domain data for radio %d to %s\n", tid,
            config_->dl_tx_td_data_files().at(tid).c_str());
        fp = std::fopen(config_->dl_tx_td_data_files().at(tid).c_str(), "rb");
    }

    std::vector<void*> txbuff(2);
    for (size_t ch = 0; ch < config_->bs_sdr_ch(); ch++) {
        txbuff.at(ch)
            = std::calloc(config_->samps_per_slot(), sizeof(int16_t) * 2);
    }
    size_t slot_byte_size = config_->samps_per_slot() * sizeof(int16_t) * 2;
    if (config_->dl_slot_per_frame() > 0) {
        size_t txIndex = tid * config_->bs_sdr_ch();
        for (size_t ch = 0; ch < config_->bs_sdr_ch(); ch++) {
            std::memcpy(txbuff.at(ch),
                config_->dl_txdata_time_dom().at(txIndex + ch).data(),
                slot_byte_size);
        }
        MLPD_INFO("%zu downlink slots will be sent per frame...\n",
            config_->dl_slot_per_frame());
    }

    int cell = 0;
    // for UHD device, the first pilot should not have an END_BURST flag
    int flagsTxUlData;
    size_t frameTimeLen
        = config_->samps_per_slot() * config_->cl_frames().at(0).size();
    size_t txFrameDelta
        = std::ceil(TIME_DELTA / (1e3 * frameTimeLen / config_->rate()));
    size_t txTimeDelta = txFrameDelta * frameTimeLen;

    if (kUseUHD == true) {
        // For multi-USRP BS perform dummy radioRx to avoid initial late packets
        int bs_sync_ret = -1;
        MLPD_INFO("Sync BS host and FPGA timestamp for thread %d\n", tid);
        for (auto& it : radio_ids_in_thread) {
            // Find cell this USRP belongs to..,
            for (size_t i = 0; i <= config_->num_cells(); i++) {
                if (it < config_->n_bs_sdrs_agg().at(i)) {
                    cell = i - 1;
                    break;
                }
            }
            size_t radio_id = it - config_->n_bs_sdrs_agg().at(cell);
            bs_sync_ret = -1;
            while (bs_sync_ret < 0) {
                bs_sync_ret = this->base_radio_set_->radioRx(radio_id, cell,
                    samp_buffer.data(), config_->samps_per_slot(), rxTimeBs);
            }
        }
    }

    int cursor = 0;
    size_t frame_id = 0;
    size_t slot_id = 0;
    size_t ant_id = 0;
    cell = 0;
    MLPD_INFO("Start BS main recv loop in thread %d\n", tid);
    while (config_->running() == true) {

        // Global updates of frame and slot IDs for USRPs
        if (kUseUHD == true || config_->bs_hw_framer() == false) {
            if (slot_id == config_->slot_per_frame()) {
                slot_id = 0;
                frame_id++;
            }
        }

        // Receive data
        for (auto& it : radio_ids_in_thread) {
            Package* pkg[num_channels];
            void* samp[num_channels];

            // Find cell this board belongs to...
            for (size_t i = 0; i <= config_->num_cells(); i++) {
                if (it < config_->n_bs_sdrs_agg().at(i)) {
                    cell = i - 1;
                    break;
                }
            }

            size_t radio_id = it - config_->n_bs_sdrs_agg().at(cell);

            size_t num_packets = config_->internal_measurement()
                    && radio_id == config_->cal_ref_sdr_id()
                    && config_->ref_node_enable()
                ? 1
                : num_channels; // receive only on one channel at the ref antenna

            // Set buffer status(es) to full; fail if full already
            for (size_t ch = 0; ch < num_packets; ++ch) {
                int bit = 1 << (cursor + ch) % sizeof(std::atomic_int);
                int offs = (cursor + ch) / sizeof(std::atomic_int);
                int old = std::atomic_fetch_or(
                    &pkg_buf_inuse[offs], bit); // now full
                // if buffer was full, exit
                if ((old & bit) != 0) {
                    MLPD_ERROR("thread %d buffer full\n", tid);
                    throw std::runtime_error("Thread %d buffer full\n");
                }
                // Reserved until marked empty by consumer
            }

            // Receive data into buffers
            size_t packageLength
                = sizeof(Package) + config_->getPackageDataLength();
            for (size_t ch = 0; ch < num_packets; ++ch) {
                pkg[ch] = (Package*)(buffer + (cursor + ch) * packageLength);
                samp[ch] = pkg[ch]->data;
            }
            if (num_packets != num_channels)
                samp[num_channels - 1]
                    = std::vector<char>(packageLength).data();

            assert(this->base_radio_set_ != NULL);
            ant_id = radio_id * num_channels;

            if (kUseUHD == false && config_->bs_hw_framer() == true) {
                long long frameTime;
                if (this->base_radio_set_->radioRx(
                        radio_id, cell, samp, frameTime)
                    < 0) {
                    config_->running(false);
                    break;
                }

                frame_id = (size_t)(frameTime >> 32);
                slot_id = (size_t)((frameTime >> 16) & 0xFFFF);

                if (config_->internal_measurement()
                    && config_->ref_node_enable()) {
                    if (radio_id == config_->cal_ref_sdr_id()) {
                        ant_id = slot_id < radio_id * num_channels
                            ? slot_id
                            : slot_id - num_channels;
                        slot_id = 0; // downlink reciprocal pilot
                    } else {
                        if (radio_id >= config_->cal_ref_sdr_id())
                            ant_id -= num_channels;
                        slot_id = 1; // uplink reciprocal pilot
                    }
                } else if (config_->internal_measurement()
                    && !config_->ref_node_enable()) {
                    // Mapping (compress schedule to eliminate Gs)
                    size_t adv
                        = int(slot_id / (config_->guard_mult() * num_channels));
                    slot_id = slot_id - ((config_->guard_mult() - 1) * 2 * adv);
                }
            } else {
                int rx_len = config_->samps_per_slot();
                int r;

                // only write received pilot or data into samp
                // otherwise use samp_buffer as a dummy buffer
                if (config_->isPilot(frame_id, slot_id)
                    || config_->isUlData(frame_id, slot_id))
                    r = this->base_radio_set_->radioRx(
                        radio_id, cell, samp, rxTimeBs);
                else
                    r = this->base_radio_set_->radioRx(
                        radio_id, cell, samp_buffer.data(), rxTimeBs);

                if (r < 0) {
                    config_->running(false);
                    break;
                }
                if (r != rx_len) {
                    std::cerr << "BAD Receive(" << r << "/" << rx_len
                              << ") at Time " << rxTimeBs << ", frame count "
                              << frame_id << std::endl;
                }

                // schedule all TX slot
                if (slot_id == 0) {
                    // Schedule next beacon in BEACON_INTERVAL frames
                    // FIXME?? From EACH cell or only one cell?
                    txTimeBs = rxTimeBs
                        + config_->samps_per_slot() * config_->slot_per_frame()
                            * BEACON_INTERVAL;
                    int r_tx = this->base_radio_set_->radioTx(radio_id, cell,
                        beaconbuff.data(), kStreamEndBurst, txTimeBs);
                    if (r_tx != (int)config_->samps_per_slot())
                        std::cerr << "BAD Transmit(" << r_tx << "/"
                                  << config_->samps_per_slot() << ") at Time "
                                  << txTimeBs << ", frame count " << frame_id
                                  << std::endl;

                    // schedule downlink slots
                    if (config_->dl_data_slot_present() == true) {
                        for (size_t s = 0; s < config_->dl_slot_per_frame();
                             s++) {
                            txTimeBs = rxTimeBs + txTimeDelta
                                + config_->dl_slots().at(radio_id).at(s)
                                    * config_->samps_per_slot()
                                - config_->tx_advance();
                            for (size_t ch = 0; ch < config_->bs_sdr_ch();
                                 ch++) {
                                size_t read_num = std::fread(txbuff.at(ch),
                                    2 * sizeof(int16_t),
                                    config_->samps_per_slot(), fp);
                                if (read_num != config_->samps_per_slot()) {
                                    MLPD_WARN(
                                        "BAD downlink Data Read: %zu/%zu\n",
                                        read_num, config_->samps_per_slot());
                                }
                            }
                            if (kUseUHD
                                && s < (config_->dl_slot_per_frame() - 1))
                                flagsTxUlData = kStreamContinuous; // HAS_TIME
                            else
                                flagsTxUlData
                                    = kStreamEndBurst; // HAS_TIME & END_BURST, fixme
                            r = clientRadioSet_->radioTx(tid, txbuff.data(),
                                config_->samps_per_slot(), flagsTxUlData,
                                txTimeBs);
                            if (r < (int)config_->samps_per_slot()) {
                                MLPD_WARN("BAD Write: %d/%zu\n", r,
                                    config_->samps_per_slot());
                            }
                        } // end for
                        if (frame_id % config_->dl_data_frame_num() == 0)
                            std::fseek(fp, 0, SEEK_SET);
                    } // end if config_->dul_data_slot_present()
                }
            }

#if DEBUG_PRINT
            for (size_t ch = 0; ch < num_packets; ++ch) {
                printf("receive thread %d, frame %zu, slot %zu, cell %zu, ant "
                       "%zu samples: %d %d %d %d %d %d %d %d ...\n",
                    tid, frame_id, slot_id, cell, ant_id + ch, pkg[ch]->data[1],
                    pkg[ch]->data[2], pkg[ch]->data[3], pkg[ch]->data[4],
                    pkg[ch]->data[5], pkg[ch]->data[6], pkg[ch]->data[7],
                    pkg[ch]->data[8]);
            }
#endif

            for (size_t ch = 0; ch < num_packets; ++ch) {
                // new (pkg[ch]) Package(frame_id, slot_id, 0, ant_id + ch);
                new (pkg[ch]) Package(frame_id, slot_id, cell, ant_id + ch);
                // push kEventRxSymbol event into the queue
                Event_data package_message;
                package_message.event_type = kEventRxSymbol;
                package_message.ant_id = ant_id + ch;
                // data records the position of this packet in the buffer & tid of this socket
                // (so that task thread could know which buffer it should visit)
                package_message.data = cursor + tid * buffer_chunk_size;
                if (message_queue_->enqueue(local_ptok, package_message)
                    == false) {
                    MLPD_ERROR("socket message enqueue failed\n");
                    throw std::runtime_error("socket message enqueue failed");
                }
                cursor++;
                cursor %= buffer_chunk_size;
            }
        }

        // for UHD device update slot_id on host
        if (kUseUHD == true) {
            slot_id++;
        }
    }
    MLPD_SYMBOL(
        "Process %d -- Loop Rx Freed memory at: %p\n", tid, zeroes_memory);
    free(zeroes_memory);
}

void* Receiver::clientTxRx_launch(void* in_context)
{
    ReceiverContext* context = (ReceiverContext*)in_context;
    auto me = context->ptr;
    auto tid = context->tid;
    auto core_id = context->core_id;
    auto buffer = context->buffer;
    delete context;
    if (me->config_->hw_framer())
        me->clientTxRx(tid);
    else
        me->clientSyncTxRx(tid, core_id, buffer);
    return 0;
}

void Receiver::clientTxRx(int tid)
{
    int tx_slots = config_->cl_ul_slots().at(tid).size();
    int rxSyms = config_->cl_dl_slots().at(tid).size();
    int txStartSym = config_->cl_ul_slots().at(tid).empty()
        ? 0
        : config_->cl_ul_slots().at(tid).at(0);

    double frameTime = config_->samps_per_slot()
        * config_->cl_frames().at(0).size() * 1e3
        / config_->rate(); // miliseconds
    unsigned txFrameDelta = (unsigned)(std::ceil(TIME_DELTA / frameTime));
    int NUM_SAMPS = config_->samps_per_slot();

    if (config_->core_alloc() == true) {
        int core = tid + 1 + config_->bs_rx_thread_num()
            + config_->task_thread_num();
        MLPD_INFO("Pinning client TxRx thread %d to core %d\n", tid, core);
        if (pin_to_core(core) != 0) {
            MLPD_ERROR(
                "Pin client TxRx thread %d to core %d failed in client txrx\n",
                tid, core);
            throw std::runtime_error(
                "Pin client TxRx thread to core failed in client txr");
        }
    }

    std::vector<std::complex<int16_t>> buffs(NUM_SAMPS, 0);
    std::vector<void*> rxbuff(2);
    rxbuff[0] = buffs.data();
    rxbuff[1] = buffs.data();

    std::vector<void*> txbuff(2);
    if (tx_slots > 0) {
        size_t txIndex = tid * config_->cl_sdr_ch();
        txbuff[0] = config_->txdata_time_dom().at(txIndex).data();
        if (config_->cl_sdr_ch() == 2)
            txbuff[1] = config_->txdata_time_dom().at(txIndex + 1).data();
        std::cout << tx_slots << " uplink slots will be sent per frame..."
                  << std::endl;
    }

    int all_trigs = 0;
    struct timespec tv, tv2;
    clock_gettime(CLOCK_MONOTONIC, &tv);

    assert(clientRadioSet_ != NULL);
    while (config_->running() == true) {
        clock_gettime(CLOCK_MONOTONIC, &tv2);
        double diff
            = ((tv2.tv_sec - tv.tv_sec) * 1e9 + (tv2.tv_nsec - tv.tv_nsec))
            / 1e9;
        if ((config_->frame_mode() != "free_running") && (diff > 2)) {
            int total_trigs = clientRadioSet_->triggers(tid);
            std::cout << "new triggers: " << total_trigs - all_trigs
                      << ", total: " << total_trigs << std::endl;
            all_trigs = total_trigs;
            tv = tv2;
        }
        // receiver loop
        long long rxTime(0);
        long long txTime(0);
        long long firstRxTime(0);
        bool receiveErrors = false;
        for (int i = 0; i < rxSyms; i++) {
            int r = clientRadioSet_->radioRx(
                tid, rxbuff.data(), NUM_SAMPS, rxTime);
            if (r == NUM_SAMPS) {
                if (i == 0)
                    firstRxTime = rxTime;
            } else {
                std::cerr << "waiting for receive frames... " << std::endl;
                receiveErrors = true;
                break;
            }
        }
        if (receiveErrors == false) {
            // transmit loop
            txTime = firstRxTime & 0xFFFFFFFF00000000;
            txTime += ((long long)txFrameDelta << 32);
            txTime += ((long long)txStartSym << 16);
            //printf("rxTime %llx, txTime %llx \n", firstRxTime, txTime);
            for (int i = 0; i < tx_slots; i++) {
                int r = clientRadioSet_->radioTx(
                    tid, txbuff.data(), NUM_SAMPS, 1, txTime);
                if (r == NUM_SAMPS) {
                    txTime += 0x10000;
                }
            }
        } // end receiveErrors == false)
    } // end while config_->running() == true)
}

void Receiver::clientSyncTxRx(int tid, int core_id, SampleBuffer* rx_buffer)
{
    if (config_->core_alloc() == true) {
        int core = tid + core_id;

        MLPD_INFO("Pinning client synctxrx thread %d to core %d\n", tid, core);
        if (pin_to_core(core) != 0) {
            MLPD_ERROR(
                "Pin client synctxrx thread %d to core %d failed\n", tid, core);
            throw std::runtime_error(
                "Failed to Pin client synctxrx thread to core");
        }
    }

    size_t frameTimeLen
        = config_->samps_per_slot() * config_->cl_frames().at(0).size();
    size_t txFrameDelta
        = std::ceil(TIME_DELTA / (1e3 * frameTimeLen / config_->rate()));
    size_t txTimeDelta = txFrameDelta * frameTimeLen;

    MLPD_INFO("Scheduling TX: %zu Frames (%lf ms) in the future!\n",
        txFrameDelta, ((1e3 * txTimeDelta) / config_->rate()));

    int NUM_SAMPS = config_->samps_per_slot();
    int SYNC_NUM_SAMPS = config_->samps_per_slot() * config_->slot_per_frame();

    std::vector<std::complex<int16_t>> syncbuff0(SYNC_NUM_SAMPS, 0);
    std::vector<std::complex<int16_t>> syncbuff1(SYNC_NUM_SAMPS, 0);
    std::vector<void*> syncrxbuff(2);
    syncrxbuff.at(0) = syncbuff0.data();

    std::vector<void*> pilotbuffA(2);
    std::vector<void*> pilotbuffB(2);

    std::vector<void*> zeros(2);
    for (auto& memory : zeros) {
        memory = calloc(NUM_SAMPS, sizeof(int16_t) * 2);
        MLPD_SYMBOL("Process %d -- Client Sync Tx Rx Allocated memory at %p "
                    "approx size: %lu\n",
            tid, memory, (NUM_SAMPS * sizeof(int16_t) * 2));
        if (memory == NULL) {
            throw std::runtime_error("Error allocating memory");
        }
    }

    pilotbuffA.at(0) = config_->pilot_cf32().data();
    if (config_->cl_sdr_ch() == 2) {
        pilotbuffA.at(1) = zeros.at(0);
        pilotbuffB.at(1) = config_->pilot_ci16().data();
        pilotbuffB.at(0) = zeros.at(1);
        syncrxbuff.at(1) = syncbuff1.data();
    }

    std::vector<void*> txbuff(2);
    for (size_t ch = 0; ch < config_->cl_sdr_ch(); ch++) {
        txbuff.at(ch)
            = std::calloc(config_->samps_per_slot(), sizeof(int16_t) * 2);
    }
    size_t slot_byte_size = config_->samps_per_slot() * sizeof(int16_t) * 2;
    size_t tx_slots = config_->cl_ul_slots().at(tid).size();
    if (tx_slots > 0) {
        size_t txIndex = tid * config_->cl_sdr_ch();
        for (size_t ch = 0; ch < config_->cl_sdr_ch(); ch++) {
            std::memcpy(txbuff.at(ch),
                config_->txdata_time_dom().at(txIndex + ch).data(),
                slot_byte_size);
        }
        MLPD_INFO("%zu uplink slots will be sent per frame...\n", tx_slots);
    }

    FILE* fp = nullptr;
    if (config_->ul_data_slot_present() == true) {
        std::printf("Opening UL time-domain data for radio %d to %s\n", tid,
            config_->ul_tx_td_data_files().at(tid).c_str());
        fp = std::fopen(config_->ul_tx_td_data_files().at(tid).c_str(), "rb");
    }

    size_t packageLength = sizeof(Package) + config_->getPackageDataLength();
    size_t ant_id = tid * config_->cl_sdr_ch();
    int buffer_chunk_size = 0;
    // use token to speed up
    moodycamel::ProducerToken local_ptok(*message_queue_);
    char* buffer;
    std::atomic_int* pkg_buf_inuse;
    if (config_->dl_slot_per_frame() > 0) {

        buffer_chunk_size = rx_buffer[0].buffer.size() / packageLength;
        // handle two channels at each radio
        // this is assuming buffer_chunk_size is at least 2
        pkg_buf_inuse
            = rx_buffer[tid + config_->bs_rx_thread_num()].pkg_buf_inuse;
        buffer = rx_buffer[tid + config_->bs_rx_thread_num()].buffer.data();
    }

    long long rxTime(0);
    long long txTime(0);
    int sync_index(-1);
    int rx_offset = 0;

    // For USRP clients skip UHD_INIT_TIME_SEC to avoid late packets
    if (kUseUHD == true) {
        int cl_sync_ret = -1;
        sleep(UHD_INIT_TIME_SEC);
        while (cl_sync_ret < 0) {
            cl_sync_ret = clientRadioSet_->radioRx(
                tid, syncrxbuff.data(), SYNC_NUM_SAMPS, rxTime);
        }
    }

    // Keep reading one frame worth of data until a beacon is found
    // Perform initial beacon detection once every BEACON_INTERVAL frames
    while ((config_->running() == true) && (sync_index < 0)) {
        int r;
        for (int find_beacon_retry = 0; find_beacon_retry < BEACON_INTERVAL;
             find_beacon_retry++) {
            r = clientRadioSet_->radioRx(
                tid, syncrxbuff.data(), SYNC_NUM_SAMPS, rxTime);
            if (r != SYNC_NUM_SAMPS) {
                MLPD_WARN("BAD SYNC Receive( %d / %d ) at Time %lld\n", r,
                    SYNC_NUM_SAMPS, rxTime);
            }
        }
        // convert data to complex float for sync detection
        std::vector<std::complex<float>> sync_buff_float(SYNC_NUM_SAMPS, 0);
        for (int i = 0; i < SYNC_NUM_SAMPS; i++) {
            sync_buff_float[i] = (std::complex<float>(
                syncbuff0[i].real() / 32768.0, syncbuff0[i].imag() / 32768.0));
        }
        //TODO syncbuff0 is sloppy here since we recevied into syncrxbuff.data(), r bytes.
#if defined(__x86_64__)
        sync_index
            = CommsLib::find_beacon_avx(sync_buff_float, config_->gold_cf32());
#else
        sync_index = CommsLib::find_beacon(sync_buff_float);
#endif

        if (sync_index >= 0) {
            MLPD_INFO("Beacon detected at Time %lld, sync_index: %d\n", rxTime,
                sync_index);
            rx_offset = sync_index - config_->beacon_size() - config_->prefix();
        }
    }

    // Read rx_offset to align with the begining of a frame
    assert((rx_offset >= 0) && (rx_offset <= SYNC_NUM_SAMPS));
    if (config_->running() == true) {
        MLPD_INFO("Start main client txrx loop... tid=%d\n", tid);
        if (rx_offset > 0) {
            int rx_data;
            rx_data = clientRadioSet_->radioRx(
                tid, syncrxbuff.data(), rx_offset, rxTime);
            if (rx_data != rx_offset) {
                MLPD_WARN("Rx data: %d : %d failed to align sync read\n",
                    rx_data, rx_offset);
            }
        }
    }

    // Main client read/write loop.
    size_t frame_id = 0;
    size_t slot_id = 0;
    size_t buffer_offset = 0;
    bool resync = false;
    bool resync_enable = (config_->frame_mode() == "continuous_resync");
    size_t resync_retry_cnt(0);
    size_t resync_retry_max(100);
    size_t resync_success(0);
    int rx_len = config_->samps_per_slot();
    int r = 0;
    rx_offset = 0;

    // for UHD device, the first pilot should not have an END_BURST flag
    int flags = (((kUseUHD == true) && (config_->cl_sdr_ch() == 2)))
        ? kStreamContinuous
        : kStreamEndBurst;
    int flagsTxUlData;

    while (config_->running() == true) {
        if (config_->max_frame() > 0 && frame_id >= config_->max_frame()) {
            config_->running(false);
            break;
        }

        r = clientRadioSet_->radioRx(
            tid, syncrxbuff.data(), NUM_SAMPS + rx_offset, rxTime);
        if (r < 0) {
            config_->running(false);
            break;
        }

        // resync every X=1000 frames:
        // TODO: X should be a function of sample rate and max CFO
        if (((frame_id / 1000) > 0) && ((frame_id % 1000) == 0)) {
            resync = resync_enable;
            MLPD_TRACE("Enable resyncing at frame %zu\n", frame_id);
        }
        rx_offset = 0;
        if (resync == true) {
            //Need to bound the beacon detection to the last 'r not the size of the memory (vector)
            //TODO: Use SIMD for faster conversion
            // convert data to complex float for sync detection
            std::vector<std::complex<float>> radio_rx_data(SYNC_NUM_SAMPS, 0);
            for (int i = 0; i < SYNC_NUM_SAMPS; i++) {
                radio_rx_data[i]
                    = (std::complex<float>(syncbuff0[i].real() / 32768.0,
                        syncbuff0[i].imag() / 32768.0));
            }
#if defined(__x86_64__)
            sync_index = CommsLib::find_beacon_avx(
                radio_rx_data, config_->gold_cf32());
#else
            sync_index = CommsLib::find_beacon(radio_rx_data);
#endif
            if (sync_index >= 0) {
                rx_offset
                    = sync_index - config_->beacon_size() - config_->prefix();
                rxTime += rx_offset;
                resync = false;
                resync_retry_cnt = 0;
                resync_success++;
                MLPD_INFO("Re-syncing with offset: %d, after %zu "
                          "tries, index: %d, tid %d\n",
                    rx_offset, resync_retry_cnt + 1, sync_index, tid);
            } else {
                resync_retry_cnt++;
            }
        }
        if ((resync == true) && (resync_retry_cnt > resync_retry_max)) {

            MLPD_WARN("Exceeded resync retry limit (%zu) for client "
                      "%d reached after %zu resync successes at "
                      "frame: %zu.  Stopping!\n",
                resync_retry_max, tid, resync_success, frame_id);
            resync = false;
            resync_retry_cnt = 0;
            config_->running(false);
            break;
        }
        // schedule all TX slot
        // config_->tx_advance() needs calibration based on SDR model and sampling rate
        txTime = rxTime + txTimeDelta
            + config_->cl_pilot_slots().at(tid).at(0) * NUM_SAMPS
            - config_->tx_advance();

        r = clientRadioSet_->radioTx(
            tid, pilotbuffA.data(), NUM_SAMPS, flags, txTime);
        if (r < NUM_SAMPS) {
            MLPD_WARN("BAD Write: %d/%d\n", r, NUM_SAMPS);
        }
        if (config_->cl_sdr_ch() == 2) {
            txTime = rxTime + txTimeDelta
                + config_->cl_pilot_slots().at(tid).at(1) * NUM_SAMPS
                - config_->tx_advance();

            r = clientRadioSet_->radioTx(
                tid, pilotbuffB.data(), NUM_SAMPS, kStreamEndBurst, txTime);
            if (r < NUM_SAMPS) {
                MLPD_WARN("BAD Write: %d/%d\n", r, NUM_SAMPS);
            }
        }
        if (config_->ul_data_slot_present() == true) {
            for (size_t s = 0; s < tx_slots; s++) {
                txTime = rxTime + txTimeDelta
                    + config_->cl_ul_slots().at(tid).at(s) * NUM_SAMPS
                    - config_->tx_advance();
                for (size_t ch = 0; ch < config_->cl_sdr_ch(); ch++) {
                    size_t read_num = std::fread(txbuff.at(ch),
                        2 * sizeof(int16_t), config_->samps_per_slot(), fp);
                    if (read_num != config_->samps_per_slot()) {
                        MLPD_WARN("BAD Uplink Data Read: %zu/%zu\n", read_num,
                            config_->samps_per_slot());
                    }
                }
                if (kUseUHD && s < (tx_slots - 1))
                    flagsTxUlData = kStreamContinuous; // HAS_TIME
                else
                    flagsTxUlData
                        = kStreamEndBurst; // HAS_TIME & END_BURST, fixme
                r = clientRadioSet_->radioTx(
                    tid, txbuff.data(), NUM_SAMPS, flagsTxUlData, txTime);
                if (r < NUM_SAMPS) {
                    MLPD_WARN("BAD Write: %d/%d\n", r, NUM_SAMPS);
                }
            } // end for
            if (frame_id % config_->ul_data_frame_num() == 0)
                std::fseek(fp, 0, SEEK_SET);
        } // end if config_->ul_data_slot_present()

        slot_id++;

        for (; slot_id < config_->slot_per_frame(); slot_id++) {
            if (config_->isDlData(frame_id, slot_id)) {
                // Set buffer status(es) to full; fail if full already
                for (size_t ch = 0; ch < config_->cl_sdr_ch(); ++ch) {
                    int bit = 1
                        << (buffer_offset + ch) % sizeof(std::atomic_int);
                    int offs = (buffer_offset + ch) / sizeof(std::atomic_int);
                    int old = std::atomic_fetch_or(
                        &pkg_buf_inuse[offs], bit); // now full
                    // if buffer was full, exit
                    if ((old & bit) != 0) {
                        MLPD_ERROR("thread %d buffer full\n", tid);
                        throw std::runtime_error("Thread %d buffer full\n");
                    }
                    // Reserved until marked empty by consumer
                }

                // Receive data into buffers
                Package* pkg[config_->cl_sdr_ch()];
                void* samp[config_->cl_sdr_ch()];
                for (size_t ch = 0; ch < config_->cl_sdr_ch(); ++ch) {
                    pkg[ch] = (Package*)(buffer
                        + (buffer_offset + ch) * packageLength);
                    samp[ch] = pkg[ch]->data;
                }

                if ((r = this->clientRadioSet_->radioRx(
                         tid, samp, rx_len, rxTime))
                    < 0) {
                    config_->running(false);
                    break;
                }
                for (size_t ch = 0; ch < config_->cl_sdr_ch(); ++ch) {
                    new (pkg[ch]) Package(frame_id, slot_id, 0, ant_id + ch);
                    // push kEventRxSymbol event into the queue
                    Event_data package_message;
                    package_message.event_type = kEventRxSymbol;
                    package_message.ant_id = ant_id + ch;
                    // data records the position of this packet in the buffer & tid of this socket
                    // (so that task thread could know which buffer it should visit)
                    package_message.data
                        = buffer_offset + tid * buffer_chunk_size;
                    if (message_queue_->enqueue(local_ptok, package_message)
                        == false) {
                        MLPD_ERROR("socket message enqueue failed\n");
                        throw std::runtime_error(
                            "socket message enqueue failed");
                    }
                    buffer_offset++;
                    buffer_offset %= buffer_chunk_size;
                }
            } else {
                r = clientRadioSet_->radioRx(
                    tid, syncrxbuff.data(), rx_len, rxTime);
                if (r < 0) {
                    config_->running(false);
                    break;
                }
            }
            if (r != rx_len) {
                MLPD_WARN("BAD Receive(%d/%d) at Time %lld, frame count %zu\n",
                    r, rx_len, rxTime, frame_id);
            }
        } // end for
        frame_id++;
        slot_id = 0;
    } // end while
    if (config_->ul_data_slot_present() == true || fp != nullptr) {
        std::fclose(fp);
    }

    for (size_t ch = 0; ch < config_->cl_sdr_ch(); ch++) {
        std::free(txbuff.at(ch));
    }

    for (auto memory : zeros) {
        MLPD_SYMBOL("Process %d -- Client Sync Tx Rx Freed memory at %p\n", tid,
            memory);
        free(memory);
    }
    zeros.clear();
}
