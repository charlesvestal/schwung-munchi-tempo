/* SampleManager.h -- TEMPO's sample memory, with the SD card replaced.
 *
 * sampleManager keeps the firmware's model: one 10 s record buffer plus a
 * fixed 1,920,000-byte region per sample slot, 48 kHz 16-bit stereo, sizes
 * counted in BYTES (SampleInfo::numSamples is a byte count, as the
 * firmware's PARSE_HEADER stored it). What changed, and why:
 *
 *   - Files are read at start-up by LoadCard() (munchi_tempo.cpp calls it
 *     before audio starts) and written by a worker thread, never by the SPI
 *     loop. A WAV is parsed by chunk, and 8/24/32-bit, float, mono and other
 *     sample rates are converted to the 48 kHz 16-bit stereo the players
 *     read; the firmware assumed that format and read the "data" chunk raw.
 *   - Each (engine, slot) has its OWN region. The firmware's copyRamToRam()
 *     placed a saved or copied sample at buff_end + 1.92 MB * (dst - 1) for
 *     either engine, which is where boot had loaded some other slot; and it
 *     copied numSamples * 4 bytes, four times the sample. Neither mattered in
 *     64 MB of SDRAM; both would corrupt or overrun a heap block.
 *   - Folders are matched case-insensitively (FAT was; the factory card
 *     ships chromatic/, slice/, buffer/).
 *
 * samplePlayer, below, is the firmware's, verbatim.
 */
#pragma once
#include "daisy.h"
#include "daisysp.h"
#include "SampleInfo.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#define MAX_SLOTS 14

class sampleManager {
    public:
    static constexpr size_t kRegionBytes = 1920000; // 10 s, 48 kHz, stereo int16
    static constexpr size_t kPad = 64;

    sampleManager() {}
    ~sampleManager() { StopWorker(); free(alloc_); }

    /** Allocates the pool. The pages are only touched as samples land. */
    bool Init(float sr) {
        (void)sr;
        // samplePlayer reads one frame past either end of a sample (the
        // firmware's interpolation does); the pad keeps that inside the block
        alloc_ = static_cast<uint8_t *>(calloc((1 + 2 * MAX_SLOTS) * kRegionBytes + 2 * kPad, 1));
        if (!alloc_)
            return false;
        pool_ = alloc_ + kPad;
        sdram_buff_ptr_ = pool_;
        sdram_buff_end_ = pool_ + kRegionBytes;
        sdram_rec_ptr_ = sdram_buff_ptr_;
        for (size_t eng = 0; eng < 2; ++eng) {
            for (size_t i = 0; i < 15; ++i) {
                loadedSamples[eng][i].start = nullptr;
                loadedSamples[eng][i].numSamples = 0;
                loadedSamples[eng][i].name.clear();
                loadedSamples[eng][i].fullPath.clear();
                loadedSamples[eng][i].sampleRate = 0;
                loadedSamples[eng][i].status = SampleStatus::EMPTY;
            }
        }
        bufferFilled = false;
        return true;
    }

    /** loadFileInfo() + loadFileData(), synchronously. */
    void LoadCard(const std::string &card_dir);
    void StartWorker();
    void StopWorker();
    /** True while the worker still has files to write. */
    bool Busy();

    bool checkLoaded() { return true; }

    void saveSlot(size_t slot, size_t engine) {
        copyRamToRam(15, slot, engine, engine);
        validateSlot(15, slot, engine, engine);
        WriteFileToCard(slot, engine);
    }
    void deleteSlot(size_t slot, size_t engine) {
        if (slot < 1 || slot > MAX_SLOTS)
            return;
        Job j;
        j.unlink = true;
        j.path = SlotPath(slot, engine);
        Push(j);
        invalidateSlot(slot, engine);
    }
    void copySlot(size_t src, size_t dst, size_t src_engine, size_t dst_engine) {
        copyRamToRam(src, dst, src_engine, dst_engine);
        validateSlot(src, dst, src_engine, dst_engine);
        WriteFileToCard(dst, dst_engine);
    }
    bool isValidSample(size_t slot, size_t engine) {
        if (slot == 15) {
            return bufferFilled;
        }
        if (slot < 1 || slot > MAX_SLOTS)
            return false;
        return loadedSamples[engine][slot - 1].numSamples > 0;
    }
    void *getNextSample(size_t slot, size_t engine) {
        if (slot == 14) {
            return loadedSamples[0][14].start;
        }
        else {
            return loadedSamples[engine][slot].start;
        }
    }
    size_t getNextSampleSize(size_t slot, size_t engine) {
        if (slot == 14) {
            return loadedSamples[0][14].numSamples / 4;
        }
        else {
            return loadedSamples[engine][slot].numSamples / 4; //Stereo sample frame size
        }
    }
    void startRecording() {
        sdram_rec_ptr_ = sdram_buff_ptr_;
    }
    void stopRecording() {
        int16_t* start = reinterpret_cast<int16_t*>(sdram_buff_ptr_);
        int16_t* end   = reinterpret_cast<int16_t*>(sdram_rec_ptr_);
        size_t buffSize = (end - start) * 2; // bytes
        loadedSamples[0][14].numSamples = buffSize;
        loadedSamples[0][14].start = sdram_buff_ptr_;
        bufferFilled = true;
    }
    bool stereoWrite(int16_t l, int16_t r) {
        int16_t* dst = reinterpret_cast<int16_t*>(sdram_rec_ptr_);
        if (reinterpret_cast<uint8_t*>(dst + 2) > sdram_buff_end_) {
            return false;
        }
        dst[0] = l;
        dst[1] = r;
        sdram_rec_ptr_ = reinterpret_cast<uint8_t*>(dst + 2);
        return true;
    }
    /** Fraction of the record buffer used (the screen draws it). */
    float recordFill() const {
        return float(sdram_rec_ptr_ - sdram_buff_ptr_) / float(kRegionBytes);
    }

    SampleInfo loadedSamples[2][15];

    private:
    struct Job {
        bool unlink = false;
        std::string path;
        const uint8_t *data = nullptr; // the slot's region
        size_t bytes = 0;
    };
    uint8_t *Region(size_t slot, size_t engine) {
        return sdram_buff_end_ + kRegionBytes * (engine * MAX_SLOTS + (slot - 1));
    }
    std::string SlotPath(size_t slot, size_t engine);
    void WriteFileToCard(size_t dst, size_t dst_engine) {
        if (dst < 1 || dst > MAX_SLOTS)
            return;
        SampleInfo &si = loadedSamples[dst_engine][dst - 1];
        Job j;
        j.path = SlotPath(dst, dst_engine);
        // the worker reads the region itself: nothing is copied or allocated
        // here, on the SPI loop. Only another save to this same slot rewrites
        // the region, and that queues its own write behind this one.
        j.data = static_cast<const uint8_t *>(si.start);
        j.bytes = si.numSamples;
        Push(j);
    }
    void validateSlot(size_t src, size_t dst, size_t src_engine, size_t dst_engine) {
        if (dst < 1 || dst > MAX_SLOTS)
            return;
        if (src == 15) {
            loadedSamples[dst_engine][dst - 1].numSamples = loadedSamples[0][14].numSamples;
        }
        else {
            loadedSamples[dst_engine][dst - 1].numSamples = loadedSamples[src_engine][src - 1].numSamples;
        }
        loadedSamples[dst_engine][dst - 1].start = Region(dst, dst_engine);
        if (dst_engine == 0) {
            loadedSamples[0][dst - 1].name = "chroma_a" + std::to_string(dst) + ".wav";
        }
        else {
            loadedSamples[1][dst - 1].name = "slice_a" + std::to_string(dst) + ".wav";
        }
    }
    void invalidateSlot(uint8_t slot, size_t engine) {
        loadedSamples[engine][slot - 1].start = nullptr;
        loadedSamples[engine][slot - 1].numSamples = 0;
    }
    void copyRamToRam(size_t src, size_t dst, size_t src_engine, size_t dst_engine) {
        if (dst < 1 || dst > MAX_SLOTS)
            return;
        const SampleInfo &s = src == 15 ? loadedSamples[0][14] : loadedSamples[src_engine][src - 1];
        if (!s.start)
            return;
        size_t n = s.numSamples < kRegionBytes ? s.numSamples : kRegionBytes;
        memmove(Region(dst, dst_engine), s.start, n);
    }
    void Push(Job &j) {
        std::lock_guard<std::mutex> lk(mu_);
        jobs_.push_back(std::move(j));
        cv_.notify_one();
    }
    void WorkerMain();

    uint8_t *alloc_ = nullptr, *pool_ = nullptr;
    uint8_t *sdram_buff_ptr_ = nullptr; //Start of RAM buffer for playback
    uint8_t *sdram_rec_ptr_ = nullptr;  //For writing new samples to RAM buffer
    uint8_t *sdram_buff_end_ = nullptr;
    bool bufferFilled;
    std::string card_dir_, dir_names_[3];

    std::thread worker_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    bool run_ = false, writing_ = false;
};

class samplePlayer {
    public:

    samplePlayer() {}
    ~samplePlayer() {}

    static constexpr size_t kMaxCrossfadeLengthLoop = 256;
    static constexpr size_t kMaxCrossfadeLengthClick = 256;

    void Init(float sr) {
        playbackSampleRate_ = sr;
        reverse = false;
        loop = true;
        globalFrequency = 1.f;
    }

    void setStartPoint(float val) {
        size_t offset = static_cast<size_t>(static_cast<float>(num_samples_) * val);
        start_point_ = offset;
        calculateCrossfade();
    }

    void setEndPoint(float val) {
        size_t reduction = static_cast<size_t>(static_cast<float>(num_samples_) * (1.f - val));
        end_point_ = num_samples_ - reduction;
        if (end_point_ < 1) {
            end_point_ = 1; // Ensure at least 1 to avoid indexing issues
        }
        calculateCrossfade();
    }

    void setFrequency(float freq) {
        cur_key_ = freq;
        updateFrequency();
    }

    void setGlobalFrequency(float freq) {
        globalFrequency = freq;
        updateFrequency();
    }

    void updateFrequency() {
        constexpr float middleC = 440.f;
        tuningWord = (cur_key_ * globalFrequency) / middleC;
        if (reverse) {
            tuningWord = -tuningWord;
        }
        calculateCrossfade();
    }

    void setReverse(bool rev) {
        if (reverse != rev) {
            reverse = rev;
            calculateCrossfade();
        }
        reverse = rev;
    }

    void setLoop(bool l) {
        loop = l;
    }

    void resetPlayer(bool still_running) {
        crossfade_counter_ = 0.f;
        if ((still_running && loop) || (still_running && !loop && isVoiceResettable())) {
            crossfading_ = true;
        }
        else {
            crossfading_ = false;
            if (!reverse) {
                phaseAccumulator = static_cast<float>(start_point_);
            }
            else {
                phaseAccumulator = static_cast<float>(end_point_);
            }
        }
        click_crossfading_ = false;
        click_crossfade_counter_ = 0.f;
    }

    void PopStereoSamps(float* left, float* right) {

        if (sample_memory_ == nullptr) {
            // if we get here, there's a bug
            *left = 0.f;
            *right = 0.f;
            return;
        }

        int16_t* pcm_samples = static_cast<int16_t*>(sample_memory_);
        size_t idx = static_cast<size_t>(phaseAccumulator);
        size_t base, next_base;
        float frac;
    
        if (!reverse) {
            base = idx * 2;
            next_base = base + 2;
            if (idx >= end_point_) {
                if (loop) {
                    base = next_base = start_point_ * 2;
                    phaseAccumulator = static_cast<float>(start_point_);
                }
                else if (!crossfading_) {
                    *left = 0.f;
                    *right = 0.f;
                    return;
                }
            }
            frac = phaseAccumulator - static_cast<float>(idx);
        }
        else {
            base = idx * 2;
            next_base = base - 2;
            if (idx <= start_point_) {
                if (loop) {
                    base = next_base = (end_point_ - 1) * 2;
                    phaseAccumulator = static_cast<float>(end_point_ - 1);
                }
                if (!crossfading_) {
                    *left = 0.f;
                    *right = 0.f;
                    return;
                }
            }
            frac = 1.f - (phaseAccumulator - static_cast<float>(idx));
        }

        float crossfadeEnv = 1.f - crossfade_counter_ / static_cast<float>(kMaxCrossfadeLengthLoop);
        float clickEnv = 1.f - click_crossfade_counter_ / static_cast<float>(kMaxCrossfadeLengthClick);
    
        // Interpolate left channel
        int16_t l0 = pcm_samples[base];
        int16_t l1 = pcm_samples[next_base];
        float l_interp = static_cast<float>(l0) + (static_cast<float>(l1 - l0) * frac);
        *left = s162f(static_cast<int16_t>(l_interp)) * crossfadeEnv * clickEnv;
    
        // Interpolate right channel
        int16_t r0 = pcm_samples[base + 1];
        int16_t r1 = pcm_samples[next_base + 1];
        float r_interp = static_cast<float>(r0) + (static_cast<float>(r1 - r0) * frac);
        *right = s162f(static_cast<int16_t>(r_interp)) * crossfadeEnv * clickEnv;

        if (should_crossfade_  && loop) {
            if (phaseAccumulator > crossfade_start_loop_ && !reverse && !crossfading_ || phaseAccumulator < crossfade_start_loop_ && reverse && !crossfading_) {
                crossfading_ = true;
                crossfade_counter_ = 0.f;
            }
        }
        else if (!loop) {
            if (phaseAccumulator > crossfade_start_click_ && !reverse && !click_crossfading_ || phaseAccumulator < crossfade_start_click_ && reverse && !click_crossfading_) {
                click_crossfading_ = true;
                click_crossfade_counter_ = 0.f;
            }
        }
        if (crossfading_) {
            crossfade_counter_ += fabsf(tuningWord);
            float crossfade_pos;
            if (!reverse) {
                crossfade_pos = static_cast<float>(start_point_) + crossfade_counter_;
            }
            else {
                crossfade_pos = static_cast<float>(end_point_) - crossfade_counter_;
            }
            idx = static_cast<size_t>(crossfade_pos);
            base = idx * 2;
            next_base = base + (!reverse ? 2 : -2);
            frac = crossfade_pos - static_cast<float>(idx);
            if (reverse) {
                frac = 1.f - frac;
            }
            l0 = pcm_samples[base];
            l1 = pcm_samples[next_base];
            l_interp = static_cast<float>(l0) + (static_cast<float>(l1 - l0) * frac);
            *left += s162f(static_cast<int16_t>(l_interp)) * (1.f - crossfadeEnv);

            r0 = pcm_samples[base + 1];
            r1 = pcm_samples[next_base + 1];
            r_interp = static_cast<float>(r0) + (static_cast<float>(r1 - r0) * frac);
            *right += s162f(static_cast<int16_t>(r_interp)) * (1.f - crossfadeEnv);
            if (crossfade_counter_ > static_cast<float>(kMaxCrossfadeLengthLoop)) {
                crossfading_ = false;
                crossfade_counter_ = 0;
                phaseAccumulator = crossfade_pos;
            }
        }
        if (click_crossfading_) {
            click_crossfade_counter_ += fabsf(tuningWord);
            if (click_crossfade_counter_ > static_cast<float>(kMaxCrossfadeLengthClick)) {
                click_crossfading_ = false;
                click_crossfade_counter_ = 0.f;
            }
        }
    
        phaseAccumulator += tuningWord;
    }

    void setSample(void *addr, size_t ns) {
        sample_memory_ = addr;
        num_samples_ = ns;
        start_point_ = 0;
        end_point_ = ns;
        calculateCrossfade();
    }

    void calculateCrossfade() {
        window_size_ = end_point_ - start_point_;
        if (window_size_ > 4800) {
            should_crossfade_ = true;
        }
        else {
            should_crossfade_ = false;
        }

        if (!reverse) {
            crossfade_start_loop_ = static_cast<float>(end_point_ - kMaxCrossfadeLengthLoop);
            crossfade_start_click_ = static_cast<float>(end_point_ - kMaxCrossfadeLengthClick);
        }
        else {
            crossfade_start_loop_ = static_cast<float>(start_point_ + kMaxCrossfadeLengthLoop);
            crossfade_start_click_ = static_cast<float>(start_point_ + kMaxCrossfadeLengthClick);
        }
    }

    bool isVoiceResettable() {
        if (phaseAccumulator < crossfade_start_loop_ && !reverse) {
            return true;
        }
        if (phaseAccumulator > crossfade_start_loop_ && reverse) {
            return true;
        }
        return false;
    }

    void *sample_memory_;
    size_t num_samples_;
    size_t start_point_;
    size_t end_point_;
    float cur_key_;
    float phaseAccumulator;
    float tuningWord;
    float playbackSampleRate_;
    float globalFrequency;
    bool reverse;
    bool loop;

    size_t window_size_;
    bool crossfading_, should_crossfade_;
    float crossfade_start_loop_, crossfade_start_click_;
    bool click_crossfading_;
    float crossfade_counter_;
    float click_crossfade_counter_;

    private:
};