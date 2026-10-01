/* tempo_samples.cpp -- sampleManager's card I/O (see tempo/SampleManager.h).
 *
 * The firmware's loadFileInfo() + loadFileData() + FileStreamingManager,
 * replaced: files are read once before audio starts, and written by a worker
 * thread off the SPI loop.
 */
#include "tempo/SampleManager.h"
#include <dirent.h>
#include <sched.h>
#include <stdio.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *kDirs[3] = {"chromatic", "slice", "buffer"};

static std::string find_dir(const std::string &card, const char *want)
{
    if(DIR *d = opendir(card.c_str()))
    {
        while(struct dirent *e = readdir(d))
            if(!strcasecmp(e->d_name, want))
            {
                std::string n = e->d_name;
                closedir(d);
                return n;
            }
        closedir(d);
    }
    return want;
}

static std::string read_file(const std::string &p)
{
    std::string s;
    if(FILE *f = fopen(p.c_str(), "rb"))
    {
        char   buf[65536];
        size_t n;
        while((n = fread(buf, 1, sizeof(buf), f)) > 0)
            s.append(buf, n);
        fclose(f);
    }
    return s;
}

/* A WAV into `dst` as 48 kHz 16-bit stereo, at most `cap` bytes. Returns the
 * byte count, 0 if the file is not a usable WAV. */
static size_t load_wav(const std::string &path, uint8_t *dst, size_t cap)
{
    std::string s = read_file(path);
    if(s.size() < 44 || memcmp(s.data(), "RIFF", 4) || memcmp(s.data() + 8, "WAVE", 4))
        return 0;
    const uint8_t *b = (const uint8_t *)s.data();
    auto rd16 = [&](size_t o) { return (int)(b[o] | (b[o + 1] << 8)); };
    auto rd32 = [&](size_t o) {
        return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16)
               | ((uint32_t)b[o + 3] << 24);
    };
    int      fmt = 1, ch = 2, bits = 16;
    uint32_t rate = 48000;
    size_t   off = 12, data = 0, data_len = 0;
    while(off + 8 <= s.size())
    {
        uint32_t sz = rd32(off + 4);
        if(!memcmp(b + off, "fmt ", 4) && off + 24 <= s.size())
        {
            fmt  = rd16(off + 8);
            ch   = rd16(off + 10);
            rate = rd32(off + 12);
            bits = rd16(off + 22);
            if(fmt == 0xFFFE && off + 34 <= s.size())
                fmt = rd16(off + 32);
        }
        else if(!memcmp(b + off, "data", 4))
        {
            data     = off + 8;
            data_len = std::min((size_t)sz, s.size() - data);
            break;
        }
        off += 8 + sz + (sz & 1);
    }
    if(!data || ch < 1 || bits < 8 || !(fmt == 1 || fmt == 3) || rate < 1000)
        return 0;

    const size_t bps = bits / 8, frames = data_len / (bps * ch);
    auto sample = [&](size_t frame, int c) -> float {
        const uint8_t *p = b + data + (frame * ch + (c < ch ? c : 0)) * bps;
        if(fmt == 3 && bits == 32)
        {
            float v;
            memcpy(&v, p, 4);
            return v;
        }
        if(fmt == 3 && bits == 64)
        {
            double v;
            memcpy(&v, p, 8);
            return (float)v;
        }
        switch(bits)
        {
            case 8: return (p[0] - 128) / 128.f;
            case 16: return (int16_t)rd16(p - b) / 32768.f;
            case 24:
                return ((int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16
                                  | (uint32_t)p[2] << 24)
                        >> 8)
                       / 8388608.f;
            case 32: return (int32_t)rd32(p - b) / 2147483648.f;
        }
        return 0.f;
    };

    int16_t     *out    = (int16_t *)dst;
    const size_t max_fr = cap / 4;
    size_t       n      = 0;
    if(rate == 48000 && fmt == 1 && bits == 16 && ch == 2)
    {
        // the firmware's own format: copied as-is
        n = std::min(frames, max_fr);
        memcpy(dst, b + data, n * 4);
        return n * 4;
    }
    // linear interpolation to 48 kHz; adequate for a sampler that plays at
    // arbitrary speeds anyway
    const double step = rate / 48000.0;
    for(double pos = 0; n < max_fr; pos += step, n++)
    {
        size_t i = (size_t)pos;
        if(i >= frames)
            break;
        size_t j = i + 1 < frames ? i + 1 : i;
        float  t = (float)(pos - i);
        for(int c = 0; c < 2; c++)
        {
            float v    = sample(i, c) + (sample(j, c) - sample(i, c)) * t;
            out[n * 2 + c] = (int16_t)f2s16(v);
        }
    }
    return n * 4;
}

std::string sampleManager::SlotPath(size_t slot, size_t engine)
{
    if(engine == 0)
        return card_dir_ + "/" + dir_names_[0] + "/chroma_a" + std::to_string(slot) + ".wav";
    return card_dir_ + "/" + dir_names_[1] + "/slice_a" + std::to_string(slot) + ".wav";
}

void sampleManager::LoadCard(const std::string &card_dir)
{
    card_dir_ = card_dir;
    for(int i = 0; i < 3; i++)
    {
        dir_names_[i] = find_dir(card_dir_, kDirs[i]);
        mkdir((card_dir_ + "/" + dir_names_[i]).c_str(), 0775);
    }
    // loadFileInfo(): chroma_aN.wav / slice_aN.wav, N = 1..14
    for(int eng = 0; eng < 2; eng++)
    {
        const char       *prefix = eng == 0 ? "chroma_a" : "slice_a";
        const std::string dir    = card_dir_ + "/" + dir_names_[eng];
        if(DIR *d = opendir(dir.c_str()))
        {
            while(struct dirent *e = readdir(d))
            {
                const char *n = e->d_name;
                size_t      l = strlen(n);
                if(n[0] == '.' || strncasecmp(n, prefix, strlen(prefix)) || l < 5
                   || strcasecmp(n + l - 4, ".wav"))
                    continue;
                int slot = atoi(n + strlen(prefix));
                if(slot < 1 || slot > MAX_SLOTS)
                    continue;
                SampleInfo &si = loadedSamples[eng][slot - 1];
                si.name        = n;
                si.fullPath    = dir + "/" + n;
                si.start       = Region(slot, eng);
                si.numSamples  = load_wav(si.fullPath, Region(slot, eng), kRegionBytes);
                si.status      = si.numSamples ? SampleStatus::LOADED : SampleStatus::FAILED;
                if(!si.numSamples)
                    si.start = nullptr;
            }
            closedir(d);
        }
    }
    // /Buffer/buffer.wav goes into the record buffer
    SampleInfo       &bi  = loadedSamples[0][14];
    const std::string bdir = card_dir_ + "/" + dir_names_[2];
    if(DIR *d = opendir(bdir.c_str()))
    {
        while(struct dirent *e = readdir(d))
            if(!strcasecmp(e->d_name, "buffer.wav"))
            {
                bi.name       = e->d_name;
                bi.fullPath   = bdir + "/" + e->d_name;
                bi.numSamples = load_wav(bi.fullPath, sdram_buff_ptr_, kRegionBytes);
                bi.status     = bi.numSamples ? SampleStatus::LOADED : SampleStatus::FAILED;
                bi.start      = sdram_buff_ptr_;
                bufferFilled  = true;
            }
        closedir(d);
    }
}

static void write_wav(const std::string &path, const uint8_t *data, size_t bytes)
{
    std::string tmp = path + ".tmp";
    FILE       *f   = fopen(tmp.c_str(), "wb");
    if(!f)
        return;
    uint8_t h[44];
    auto    w32 = [&](int o, uint32_t v) {
        h[o] = v & 255, h[o + 1] = (v >> 8) & 255, h[o + 2] = (v >> 16) & 255,
        h[o + 3] = (v >> 24) & 255;
    };
    auto w16 = [&](int o, uint16_t v) { h[o] = v & 255, h[o + 1] = (v >> 8) & 255; };
    memcpy(h, "RIFF", 4);
    w32(4, 36 + bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    w32(16, 16);
    w16(20, 1);
    w16(22, 2);
    w32(24, 48000);
    w32(28, 48000 * 4);
    w16(32, 4);
    w16(34, 16);
    memcpy(h + 36, "data", 4);
    w32(40, bytes);
    fwrite(h, 1, 44, f);
    fwrite(data, 1, bytes, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    rename(tmp.c_str(), path.c_str());
}

void sampleManager::WorkerMain()
{
#ifdef __linux__
    struct sched_param sp = {};
    sched_setscheduler(0, SCHED_OTHER, &sp);
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    CPU_SET(1, &set);
    CPU_SET(2, &set);
    sched_setaffinity(0, sizeof(set), &set);
#endif
    std::unique_lock<std::mutex> lk(mu_);
    while(true)
    {
        cv_.wait(lk, [&] { return !jobs_.empty() || !run_; });
        if(jobs_.empty())
            return;
        Job j    = jobs_.front();
        jobs_.pop_front();
        writing_ = true;
        lk.unlock();
        if(j.unlink)
            unlink(j.path.c_str());
        else if(j.data && j.bytes)
            write_wav(j.path, j.data, j.bytes);
        lk.lock();
        writing_ = false;
    }
}

void sampleManager::StartWorker()
{
    run_    = true;
    worker_ = std::thread([this] { WorkerMain(); });
}

void sampleManager::StopWorker()
{
    {
        std::lock_guard<std::mutex> lk(mu_);
        if(!run_)
            return;
        run_ = false;
        cv_.notify_one();
    }
    if(worker_.joinable())
        worker_.join(); // drains the queue first
}

bool sampleManager::Busy()
{
    std::lock_guard<std::mutex> lk(mu_);
    return writing_ || !jobs_.empty();
}
