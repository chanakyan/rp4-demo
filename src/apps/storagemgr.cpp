// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 Rajeshkumar Venugopal / Third Buyer Advisory
//
// storagemgr — SD card file access + FLAC decode
//
// Subscribes to:
//   /pps/cmd/play  → decode FLAC from SD, publish PCM to /pps/audio/pcm
//   /pps/cmd/ls    → list files, publish to /pps/storage/listing
//   /pps/cmd/stop  → cancel playback
//
// Publishes:
//   /pps/audio/pcm       → capability ID for decoded PCM
//   /pps/audio/status    → title, sample_rate, channels, frames
//   /pps/storage/status  → mounted, decoding state
//   /pps/storage/listing → directory contents

import std;
import qnx.kernel;
import rp4.hal;

using namespace qnx;
using namespace qnx::kernel;
using namespace qnx::memory;
using namespace qnx::pps;
using namespace rp4::hal;

// ─── FatFs (C linkage) ─────────────────────────────────────────────────────

extern "C" {
#include "ff.h"
}

// ─── dr_flac (C linkage, custom allocator) ─────────────────────────────────

extern "C" {
    void* rp4_malloc(std::size_t sz)            { return std::malloc(sz); }
    void* rp4_realloc(void* p, std::size_t sz)  { return std::realloc(p, sz); }
    void  rp4_free(void* p)                     { std::free(p); }
}

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DRFLAC_MALLOC(sz)     rp4_malloc(sz)
#define DRFLAC_REALLOC(p,sz)  rp4_realloc(p, sz)
#define DRFLAC_FREE(p)        rp4_free(p)
#include "dr_flac.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DRMP3_MALLOC(sz)      rp4_malloc(sz)
#define DRMP3_REALLOC(p,sz)   rp4_realloc(p, sz)
#define DRMP3_FREE(p)         rp4_free(p)
#include "dr_mp3.h"

#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#define DRWAV_MALLOC(sz)      rp4_malloc(sz)
#define DRWAV_REALLOC(p,sz)   rp4_realloc(p, sz)
#define DRWAV_FREE(p)         rp4_free(p)
#include "dr_wav.h"

// ─── State ─────────────────────────────────────────────────────────────────

static FATFS g_fs;

// ─── dr_flac callbacks for FatFs ───────────────────────────────────────────

struct FlacFile { FIL fil; };

static auto flac_read(void* ud, void* buf, std::size_t n) -> std::size_t {
    UINT rd = 0;
    f_read(&static_cast<FlacFile*>(ud)->fil, buf, static_cast<UINT>(n), &rd);
    return static_cast<std::size_t>(rd);
}

static auto flac_seek(void* ud, int offset, drflac_seek_origin origin) -> drflac_bool32 {
    auto& fil = static_cast<FlacFile*>(ud)->fil;
    FSIZE_t pos = (origin == DRFLAC_SEEK_CUR) ?
        f_tell(&fil) + offset : static_cast<FSIZE_t>(offset);
    return f_lseek(&fil, pos) == FR_OK;
}

static auto flac_tell(void* ud, drflac_int64* cursor) -> drflac_bool32 {
    *cursor = static_cast<drflac_int64>(f_tell(&static_cast<FlacFile*>(ud)->fil));
    return DRFLAC_TRUE;
}

// ─── Entry ─────────────────────────────────────────────────────────────────

auto storagemgr_main(Kernel& k) -> void {
    auto& pps = k.pps();

    if (f_mount(&g_fs, "", 1) != FR_OK) {
        (void)pps.publish("/pps/storage/status", "mounted", "false");
        uart::puts("storagemgr: SD mount failed\n");
        return;
    }
    (void)pps.publish("/pps/storage/status", "mounted", "true");
    uart::puts("storagemgr: SD mounted\n");

    constexpr auto pid = ProcessId{12};

    // ─── Detect format by magic bytes (not extension) ───────────────────
    // NativeDSD formats: DSF, DFF, DSD-WV (WavPack DSD), FLAC, AIFF, WAV, MP3
    enum class AudioFmt { unknown, dsf, dff, flac, mp3, wav, aiff, wvpk, ape, ogg, m4a };

    auto detect_format = [](FlacFile& ff) -> AudioFmt {
        std::array<std::uint8_t, 16> magic{};
        UINT rd = 0;
        f_lseek(&ff.fil, 0);
        f_read(&ff.fil, magic.data(), 16, &rd);
        f_lseek(&ff.fil, 0);  // rewind for decoder

        if (rd >= 4 && magic[0]=='D' && magic[1]=='S' && magic[2]=='D' && magic[3]==' ')
            return AudioFmt::dsf;
        if (rd >= 16 && magic[0]=='F' && magic[1]=='R' && magic[2]=='M' && magic[3]=='8'
            && magic[12]=='D' && magic[13]=='S' && magic[14]=='D' && magic[15]==' ')
            return AudioFmt::dff;
        if (rd >= 4 && magic[0]=='f' && magic[1]=='L' && magic[2]=='a' && magic[3]=='C')
            return AudioFmt::flac;
        if (rd >= 4 && magic[0]=='R' && magic[1]=='I' && magic[2]=='F' && magic[3]=='F')
            return AudioFmt::wav;
        if (rd >= 3 && magic[0]=='I' && magic[1]=='D' && magic[2]=='3')
            return AudioFmt::mp3;
        if (rd >= 2 && magic[0]==0xFF && (magic[1] & 0xE0) == 0xE0)
            return AudioFmt::mp3;  // MP3 sync word
        if (rd >= 12 && magic[0]=='F' && magic[1]=='O' && magic[2]=='R' && magic[3]=='M'
            && magic[8]=='A' && magic[9]=='I' && magic[10]=='F' && magic[11]=='F')
            return AudioFmt::aiff;
        if (rd >= 4 && magic[0]=='w' && magic[1]=='v' && magic[2]=='p' && magic[3]=='k')
            return AudioFmt::wvpk;  // WavPack (can carry DSD)
        if (rd >= 4 && magic[0]=='M' && magic[1]=='A' && magic[2]=='C' && magic[3]==' ')
            return AudioFmt::ape;   // Monkey's Audio
        if (rd >= 4 && magic[0]=='O' && magic[1]=='g' && magic[2]=='g' && magic[3]=='S')
            return AudioFmt::ogg;   // Ogg Vorbis/FLAC/Opus
        // M4A/AAC/ALAC: "ftyp" at offset 4
        if (rd >= 8 && magic[4]=='f' && magic[5]=='t' && magic[6]=='y' && magic[7]=='p')
            return AudioFmt::m4a;

        return AudioFmt::unknown;
    };

    // ─── Metadata container ─────────────────────────────────────────────
    struct TrackMeta {
        std::array<char, 128> artist{};
        std::array<char, 128> album{};
        std::array<char, 128> title{};
        std::uint32_t track_number = 0;
        bool has_cover = false;
        const std::uint8_t* cover_data = nullptr;
        std::size_t cover_size = 0;
    };

    auto publish_meta = [&](const TrackMeta& m) {
        if (m.artist[0]) (void)pps.publish("/pps/audio/status", "artist", m.artist.data());
        if (m.album[0])  (void)pps.publish("/pps/audio/status", "album", m.album.data());
        if (m.title[0])  (void)pps.publish("/pps/audio/status", "title", m.title.data());
        if (m.track_number > 0)
            (void)pps.publish("/pps/audio/status", "track", std::to_string(m.track_number));
        if (m.has_cover && m.cover_data && m.cover_size > 0) {
            // Store cover art in memfs for browser to fetch
            (void)pps.publish("/pps/cmd/store", "value",
                std::string("cover.jpg 0 ") + std::to_string(m.cover_size));
            (void)pps.publish("/pps/audio/status", "cover", "true");
        }
    };

    // ─── FLAC Vorbis comment parser ───────────────────────────────────────
    // Callback for drflac_open_with_metadata — extracts tags + cover art
    struct FlacMetaCtx {
        TrackMeta meta;
        Pps* pps_ref;
    };

    auto copy_tag_value = [](const char* comment, drflac_uint32 len,
                             const char* key, std::array<char, 128>& out) {
        auto klen = 0u;
        while (key[klen]) ++klen;
        if (len <= klen + 1) return;
        // Case-insensitive prefix match
        for (std::uint32_t i = 0; i < klen; ++i) {
            char a = comment[i];
            char b = key[i];
            if (a >= 'a' && a <= 'z') a -= 32;
            if (b >= 'a' && b <= 'z') b -= 32;
            if (a != b) return;
        }
        if (comment[klen] != '=') return;
        auto vlen = std::min(static_cast<std::size_t>(len - klen - 1), out.size() - 1);
        for (std::size_t i = 0; i < vlen; ++i) out[i] = comment[klen + 1 + i];
        out[vlen] = '\0';
    };

    // ─── DSD-over-PCM (DoP) packing ─────────────────────────────────────
    // SACD DSD is 1-bit at 2.8224 MHz. PCM5122 accepts DoP: DSD bytes
    // packed into 24-bit PCM frames with alternating markers 0x05/0xFA.
    // Output: 176.4 kHz 24-bit stereo PCM (int32 with top 8 bits = marker).
    auto pack_dop = [](const std::uint8_t* dsd, std::size_t dsd_bytes,
                       std::int32_t* out, std::size_t* out_frames) {
        // DSD is interleaved L/R bytes. Each byte = 8 DSD samples.
        // DoP frame: marker (8 bits) + DSD byte (16 bits) = 24-bit PCM sample.
        // Two DSD bytes per stereo frame (one L, one R).
        std::size_t frames = dsd_bytes / 2;  // 2 bytes per stereo frame
        *out_frames = frames;
        bool marker_toggle = false;
        for (std::size_t i = 0; i < frames; ++i) {
            auto marker = marker_toggle ? 0xFA : 0x05;
            marker_toggle = !marker_toggle;
            // Left channel: marker in top byte, DSD in middle byte
            out[i * 2]     = (marker << 16) | (dsd[i * 2] << 8);
            // Right channel
            out[i * 2 + 1] = (marker << 16) | (dsd[i * 2 + 1] << 8);
        }
    };

    // ─── DSF file header parser ──────────────────────────────────────────
    // DSF = DSD Stream File. Header: "DSD " magic, then metadata chunk,
    // then "data" chunk with raw DSD bytes.
    struct DsfHeader {
        std::uint32_t sample_rate;
        std::uint32_t channels;
        std::uint64_t data_offset;
        std::uint64_t data_size;
        bool valid;
    };

    auto parse_dsf = [](FlacFile& ff) -> DsfHeader {
        DsfHeader h{0, 0, 0, 0, false};
        std::array<std::uint8_t, 28> hdr{};
        UINT rd = 0;
        f_read(&ff.fil, hdr.data(), 28, &rd);
        if (rd < 28) return h;
        // "DSD " magic
        if (hdr[0] != 'D' || hdr[1] != 'S' || hdr[2] != 'D' || hdr[3] != ' ')
            return h;
        // Skip to fmt chunk (offset 28 in DSD chunk points to it)
        // Read fmt chunk
        std::array<std::uint8_t, 52> fmt{};
        f_read(&ff.fil, fmt.data(), 52, &rd);
        if (rd < 52) return h;
        // "fmt " magic at offset 0
        if (fmt[0] != 'f' || fmt[1] != 'm' || fmt[2] != 't' || fmt[3] != ' ')
            return h;
        // Format version at offset 8 (should be 1)
        // Channel count at offset 16 (uint32 LE)
        h.channels = fmt[16] | (fmt[17] << 8) | (fmt[18] << 16) | (fmt[19] << 24);
        // Sample rate at offset 20 (uint32 LE) — DSD rate (2822400 or 5644800)
        h.sample_rate = fmt[20] | (fmt[21] << 8) | (fmt[22] << 16) | (fmt[23] << 24);
        // Data offset from DSD chunk: skip to data chunk
        // Sample count at offset 28 (uint64 LE)
        // Block size per channel at offset 36 (uint32 LE)
        // Seek to data chunk (after fmt)
        f_lseek(&ff.fil, 28 + 52);  // past DSD header + fmt
        std::array<std::uint8_t, 12> data_hdr{};
        f_read(&ff.fil, data_hdr.data(), 12, &rd);
        if (rd < 12) return h;
        if (data_hdr[0] != 'd' || data_hdr[1] != 'a' || data_hdr[2] != 't' || data_hdr[3] != 'a')
            return h;
        // Data size at offset 4 (uint64 LE) — includes 12-byte header
        h.data_size = data_hdr[4] | (data_hdr[5] << 8) |
            (static_cast<std::uint64_t>(data_hdr[6]) << 16) |
            (static_cast<std::uint64_t>(data_hdr[7]) << 24);
        h.data_size -= 12;  // subtract data chunk header
        h.data_offset = 28 + 52 + 12;
        h.valid = true;
        return h;
    };

    // ─── Publish decoded PCM to PPS ───────────────────────────────────────
    auto publish_pcm = [&](std::string_view title, std::int16_t* pcm,
                           std::size_t samples, std::uint32_t sr, std::uint32_t ch) {
        if (!pcm) return;
        (void)pps.publish("/pps/audio/status", "title", std::string(title));
        (void)pps.publish("/pps/audio/status", "sample_rate", std::to_string(sr));
        (void)pps.publish("/pps/audio/status", "channels", std::to_string(ch));

        auto bytes = samples * sizeof(std::int16_t);
        (void)pps.publish("/pps/cmd/store", "value",
            std::string(title) + " 0 " + std::to_string(bytes));
        (void)pps.publish("/pps/storage/status", "state", "decoded");
    };

    // play <path> — supports .flac, .mp3, .wav
    (void)pps.subscribe("/pps/cmd/play", pid, [&](const Notification& n) {
        (void)pps.publish("/pps/storage/status", "decoding", n.value);

        std::array<char, 256> pathbuf{};
        auto plen = std::min(n.value.size(), pathbuf.size() - 1);
        for (std::size_t i = 0; i < plen; ++i) pathbuf[i] = n.value[i];

        FlacFile ff{};
        if (f_open(&ff.fil, pathbuf.data(), FA_READ) != FR_OK) {
            (void)pps.publish("/pps/storage/status", "error", "file not found");
            return;
        }

        auto fmt = detect_format(ff);

        // Publish magic + format to PPS — UI shows both
        constexpr const char* fmt_names[] = {
            "unknown", "DSF (DSD)", "DFF (DSDIFF)", "FLAC", "MP3", "WAV",
            "AIFF", "WavPack", "APE", "Ogg", "M4A/AAC/ALAC"
        };
        constexpr const char* fmt_magic[] = {
            "??", "44 53 44 20", "46 52 4D 38", "66 4C 61 43", "49 44 33",
            "52 49 46 46", "46 4F 52 4D", "77 76 70 6B", "4D 41 43 20",
            "4F 67 67 53", "66 74 79 70"
        };
        auto fi = static_cast<int>(fmt);
        (void)pps.publish("/pps/audio/status", "format", fmt_names[fi]);
        (void)pps.publish("/pps/audio/status", "magic", fmt_magic[fi]);

        if (fmt == AudioFmt::flac) {
            // Open with metadata callback to extract tags + cover
            FlacMetaCtx mctx{};
            mctx.pps_ref = &pps;

            auto meta_cb = [](void* ud, drflac_metadata* m) {
                auto& ctx = *static_cast<FlacMetaCtx*>(ud);
                if (m->type == DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) {
                    drflac_vorbis_comment_iterator iter;
                    drflac_init_vorbis_comment_iterator(&iter,
                        m->data.vorbis_comment.commentCount,
                        m->data.vorbis_comment.pComments);
                    drflac_uint32 len = 0;
                    const char* comment;
                    while ((comment = drflac_next_vorbis_comment(&iter, &len)) != nullptr) {
                        // Parse ARTIST=, ALBUM=, TITLE=, TRACKNUMBER=
                        auto try_tag = [&](const char* key, std::array<char, 128>& out) {
                            auto klen = 0u;
                            while (key[klen]) ++klen;
                            if (len <= klen + 1) return;
                            bool match = true;
                            for (std::uint32_t i = 0; i < klen && match; ++i) {
                                char a = comment[i], b = key[i];
                                if (a >= 'a' && a <= 'z') a -= 32;
                                if (b >= 'a' && b <= 'z') b -= 32;
                                if (a != b) match = false;
                            }
                            if (match && comment[klen] == '=') {
                                auto vlen = std::min(static_cast<std::size_t>(len - klen - 1),
                                                     out.size() - 1);
                                for (std::size_t i = 0; i < vlen; ++i)
                                    out[i] = comment[klen + 1 + i];
                                out[vlen] = '\0';
                            }
                        };
                        try_tag("ARTIST", ctx.meta.artist);
                        try_tag("ALBUM", ctx.meta.album);
                        try_tag("TITLE", ctx.meta.title);
                    }
                }
                if (m->type == DRFLAC_METADATA_BLOCK_TYPE_PICTURE) {
                    if (m->data.picture.pPictureData && m->data.picture.pictureDataSize > 0) {
                        ctx.meta.has_cover = true;
                        ctx.meta.cover_data = m->data.picture.pPictureData;
                        ctx.meta.cover_size = m->data.picture.pictureDataSize;
                    }
                }
            };

            auto* flac = drflac_open_with_metadata(flac_read, flac_seek, flac_tell,
                meta_cb, &mctx, nullptr);
            if (!flac) { f_close(&ff.fil); return; }
            auto total = static_cast<std::size_t>(flac->totalPCMFrameCount);
            auto ch = flac->channels;
            auto samples = total * ch;
            auto* pcm = static_cast<std::int16_t*>(rp4_malloc(samples * sizeof(std::int16_t)));
            if (pcm) drflac_read_pcm_frames_s16(flac, total, pcm);

            // Publish metadata — title falls back to filename if no tag
            if (!mctx.meta.title[0]) {
                auto tlen = std::min(n.value.size(), mctx.meta.title.size() - 1);
                for (std::size_t i = 0; i < tlen; ++i) mctx.meta.title[i] = n.value[i];
                mctx.meta.title[tlen] = '\0';
            }
            publish_meta(mctx.meta);
            publish_pcm(n.value, pcm, samples, flac->sampleRate, ch);
            drflac_close(flac);

        } else if (fmt == AudioFmt::mp3) {
            // Read entire file into memory for dr_mp3
            auto fsize = f_size(&ff.fil);
            auto* fbuf = static_cast<std::uint8_t*>(rp4_malloc(fsize));
            if (fbuf) {
                UINT rd = 0;
                f_read(&ff.fil, fbuf, static_cast<UINT>(fsize), &rd);
                drmp3_uint64 frames = 0;
                drmp3_config cfg{};
                auto* pcm = drmp3_open_memory_and_read_pcm_frames_s16(
                    fbuf, fsize, &cfg, &frames, nullptr);
                rp4_free(fbuf);
                if (pcm) {
                    publish_pcm(n.value, pcm, static_cast<std::size_t>(frames * cfg.channels),
                                cfg.sampleRate, cfg.channels);
                }
            }

        } else if (fmt == AudioFmt::wav) {
            // Read entire file into memory for dr_wav
            auto fsize = f_size(&ff.fil);
            auto* fbuf = static_cast<std::uint8_t*>(rp4_malloc(fsize));
            if (fbuf) {
                UINT rd = 0;
                f_read(&ff.fil, fbuf, static_cast<UINT>(fsize), &rd);
                drwav_uint64 frames = 0;
                unsigned int ch = 0, sr = 0;
                auto* pcm = drwav_open_memory_and_read_pcm_frames_s16(
                    fbuf, fsize, &ch, &sr, &frames, nullptr);
                rp4_free(fbuf);
                if (pcm) {
                    publish_pcm(n.value, pcm, static_cast<std::size_t>(frames * ch), sr, ch);
                }
            }

        } else if (fmt == AudioFmt::dsf || fmt == AudioFmt::dff) {
            // SACD DSD → DoP (DSD-over-PCM)
            auto dsf = parse_dsf(ff);
            if (!dsf.valid) {
                (void)pps.publish("/pps/storage/status", "error", "not valid DSF");
                f_close(&ff.fil);
                return;
            }

            // Read raw DSD data
            auto* dsd = static_cast<std::uint8_t*>(rp4_malloc(dsf.data_size));
            if (dsd) {
                UINT rd = 0;
                f_lseek(&ff.fil, dsf.data_offset);
                f_read(&ff.fil, dsd, static_cast<UINT>(dsf.data_size), &rd);

                // Pack as DoP — output is 32-bit samples (24-bit DoP in int32)
                std::size_t dop_frames = 0;
                auto* dop = static_cast<std::int32_t*>(
                    rp4_malloc(dsf.data_size * 2 * sizeof(std::int32_t)));
                if (dop) {
                    pack_dop(dsd, dsf.data_size, dop, &dop_frames);

                    (void)pps.publish("/pps/audio/status", "title", n.value);
                    (void)pps.publish("/pps/audio/status", "format", "DSD-over-PCM (DoP)");
                    (void)pps.publish("/pps/audio/status", "sample_rate",
                        std::to_string(dsf.sample_rate));
                    (void)pps.publish("/pps/audio/status", "channels",
                        std::to_string(dsf.channels));
                    (void)pps.publish("/pps/audio/status", "bits_per_sample", "DSD/1-bit");

                    auto bytes = dop_frames * dsf.channels * sizeof(std::int32_t);
                    (void)pps.publish("/pps/cmd/store", "value",
                        n.value + " 0 " + std::to_string(bytes));
                    (void)pps.publish("/pps/storage/status", "state", "decoded (DoP)");
                    rp4_free(dop);
                }
                rp4_free(dsd);
            }

        } else if (fmt == AudioFmt::aiff) {
            // AIFF — dr_wav handles it (same as WAV path)
            auto fsize = f_size(&ff.fil);
            auto* fbuf = static_cast<std::uint8_t*>(rp4_malloc(fsize));
            if (fbuf) {
                UINT rd = 0;
                f_read(&ff.fil, fbuf, static_cast<UINT>(fsize), &rd);
                drwav_uint64 frames = 0;
                unsigned int ch = 0, sr = 0;
                auto* pcm = drwav_open_memory_and_read_pcm_frames_s16(
                    fbuf, fsize, &ch, &sr, &frames, nullptr);
                rp4_free(fbuf);
                if (pcm) {
                    publish_pcm(n.value, pcm, static_cast<std::size_t>(frames * ch), sr, ch);
                }
            }

        } else if (fmt == AudioFmt::wvpk || fmt == AudioFmt::ape ||
                   fmt == AudioFmt::ogg || fmt == AudioFmt::m4a) {
            // Detected but decoder not yet integrated
            (void)pps.publish("/pps/storage/status", "error",
                "format detected but decoder not yet linked");

        } else {
            (void)pps.publish("/pps/storage/status", "error", "unsupported format");
        }

        f_close(&ff.fil);
    });

    // ls
    (void)pps.subscribe("/pps/cmd/ls", pid, [&](const Notification&) {
        DIR dir;
        FILINFO fno;
        if (f_opendir(&dir, "/") == FR_OK) {
            std::string listing;
            while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
                listing += fno.fname;
                listing += '\n';
            }
            f_closedir(&dir);
            (void)pps.publish("/pps/storage/listing", "files", listing);
        }
    });

    // stop
    (void)pps.subscribe("/pps/cmd/stop", pid, [&](const Notification&) {
        (void)pps.publish("/pps/storage/status", "state", "idle");
    });
}

int main() {
    Kernel k;
    k.init();
    std::println("=== rp4 storagemgr (host build) ===");
    return 0;
}
