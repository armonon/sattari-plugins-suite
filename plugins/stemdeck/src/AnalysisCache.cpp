#include "AnalysisCache.h"
#include <sqlite3.h>
#include <vector>

namespace
{
constexpr int kSchemaVersion = 1;

// ── Blob (de)serialisation for the analysis vectors ──────────────────────────
juce::MemoryBlock packDoubles (const std::vector<double>& v)
{
    juce::MemoryBlock mb;
    if (! v.empty()) mb.append (v.data(), v.size() * sizeof (double));
    return mb;
}
std::vector<double> unpackDoubles (const void* data, int bytes)
{
    std::vector<double> v;
    if (data != nullptr && bytes >= (int) sizeof (double))
    {
        const int n = bytes / (int) sizeof (double);
        v.resize ((size_t) n);
        std::memcpy (v.data(), data, (size_t) n * sizeof (double));
    }
    return v;
}
juce::MemoryBlock packCurve (const std::vector<std::pair<double, float>>& v)
{
    juce::MemoryBlock mb;
    for (const auto& p : v) { mb.append (&p.first, sizeof (double)); mb.append (&p.second, sizeof (float)); }
    return mb;
}
std::vector<std::pair<double, float>> unpackCurve (const void* data, int bytes)
{
    std::vector<std::pair<double, float>> v;
    constexpr int rec = (int) (sizeof (double) + sizeof (float));   // 12
    if (data != nullptr && bytes >= rec)
    {
        const auto* p = static_cast<const char*> (data);
        for (int i = 0; i + rec <= bytes; i += rec)
        {
            double t; float b;
            std::memcpy (&t, p + i, sizeof (double));
            std::memcpy (&b, p + i + sizeof (double), sizeof (float));
            v.emplace_back (t, b);
        }
    }
    return v;
}
} // namespace

struct AnalysisCache::Impl
{
    sqlite3* db = nullptr;
    juce::CriticalSection lock;

    explicit Impl (const juce::File& dbFile)
    {
        dbFile.getParentDirectory().createDirectory();
        if (sqlite3_open_v2 (dbFile.getFullPathName().toRawUTF8(), &db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
        {
            if (db) { sqlite3_close (db); db = nullptr; }
            return;
        }
        exec ("PRAGMA journal_mode=WAL;");
        exec ("PRAGMA synchronous=NORMAL;");
        exec ("CREATE TABLE IF NOT EXISTS analysis ("
              "hash TEXT PRIMARY KEY, version INTEGER, sr REAL, dur REAL, rms REAL, bpm REAL, "
              "downbeat REAL, bpb INTEGER, key_pc INTEGER, key_min INTEGER, key_conf REAL, "
              "grid BLOB, transients BLOB, tempo BLOB, created INTEGER);");
    }
    ~Impl() { if (db) sqlite3_close (db); }

    void exec (const char* sql) { if (db) sqlite3_exec (db, sql, nullptr, nullptr, nullptr); }
};

AnalysisCache::AnalysisCache() : AnalysisCache (defaultDbFile()) {}
AnalysisCache::AnalysisCache (const juce::File& dbFile) : impl (std::make_unique<Impl> (dbFile)) {}
AnalysisCache::~AnalysisCache() = default;

bool AnalysisCache::isOpen() const noexcept { return impl && impl->db != nullptr; }

juce::File AnalysisCache::defaultDbFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Sattari").getChildFile ("StemDeck").getChildFile ("analysis-cache.db");
}

juce::String AnalysisCache::hashFile (const juce::File& f)
{
    if (! f.existsAsFile()) return {};
    const juce::int64 size = f.getSize();
    juce::MemoryBlock mb;
    mb.append (&size, sizeof (size));

    juce::FileInputStream in (f);
    if (in.openedOk())
    {
        constexpr int chunk = 256 * 1024;
        auto readChunk = [&] (juce::int64 pos, int len)
        {
            if (len <= 0) return;
            in.setPosition (pos);
            juce::HeapBlock<char> buf ((size_t) len);
            const int got = in.read (buf, len);
            if (got > 0) mb.append (buf, (size_t) got);
        };
        readChunk (0, (int) juce::jmin<juce::int64> (chunk, size));
        if (size > (juce::int64) chunk * 2) readChunk (size / 2, chunk);
        if (size > chunk) readChunk (juce::jmax<juce::int64> (0, size - chunk), chunk);
    }

    // FNV-1a 64-bit over (size + sampled bytes). Self-contained — avoids pulling
    // in juce_cryptography just for a cache key. Collisions are astronomically
    // unlikely given the size prefix + content windows.
    uint64_t h = 1469598103934665603ULL;
    const auto* p = static_cast<const uint8_t*> (mb.getData());
    for (size_t i = 0; i < mb.getSize(); ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return juce::String::toHexString ((juce::int64) h).paddedLeft ('0', 16);
}

bool AnalysisCache::lookup (const juce::String& hash, TrackAnalysis& out) const
{
    if (! isOpen() || hash.isEmpty()) return false;
    const juce::ScopedLock sl (impl->lock);

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2 (impl->db,
            "SELECT version,sr,dur,rms,bpm,downbeat,bpb,key_pc,key_min,key_conf,grid,transients,tempo "
            "FROM analysis WHERE hash=?;", -1, &st, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_text (st, 1, hash.toRawUTF8(), -1, SQLITE_TRANSIENT);
    bool hit = false;
    if (sqlite3_step (st) == SQLITE_ROW && sqlite3_column_int (st, 0) == kSchemaVersion)
    {
        out.sampleRate       = sqlite3_column_double (st, 1);
        out.durationSeconds  = sqlite3_column_double (st, 2);
        out.rmsDb            = (float) sqlite3_column_double (st, 3);
        out.detectedBpm      = (float) sqlite3_column_double (st, 4);
        out.downbeatSample   = sqlite3_column_double (st, 5);
        out.beatsPerBar      = sqlite3_column_int (st, 6);
        out.keyPitchClass    = sqlite3_column_int (st, 7);
        out.keyIsMinor       = sqlite3_column_int (st, 8) != 0;
        out.keyConfidence    = (float) sqlite3_column_double (st, 9);
        out.beatGridSamples        = unpackDoubles (sqlite3_column_blob (st, 10), sqlite3_column_bytes (st, 10));
        out.transientAnchorSamples = unpackDoubles (sqlite3_column_blob (st, 11), sqlite3_column_bytes (st, 11));
        out.tempoCurve             = unpackCurve   (sqlite3_column_blob (st, 12), sqlite3_column_bytes (st, 12));
        hit = true;
    }
    sqlite3_finalize (st);
    return hit;
}

void AnalysisCache::store (const juce::String& hash, const TrackAnalysis& a)
{
    if (! isOpen() || hash.isEmpty()) return;
    const juce::ScopedLock sl (impl->lock);

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2 (impl->db,
            "INSERT OR REPLACE INTO analysis "
            "(hash,version,sr,dur,rms,bpm,downbeat,bpb,key_pc,key_min,key_conf,grid,transients,tempo,created) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);", -1, &st, nullptr) != SQLITE_OK)
        return;

    const auto grid  = packDoubles (a.beatGridSamples);
    const auto trans = packDoubles (a.transientAnchorSamples);
    const auto curve = packCurve  (a.tempoCurve);

    sqlite3_bind_text   (st, 1,  hash.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int    (st, 2,  kSchemaVersion);
    sqlite3_bind_double (st, 3,  a.sampleRate);
    sqlite3_bind_double (st, 4,  a.durationSeconds);
    sqlite3_bind_double (st, 5,  a.rmsDb);
    sqlite3_bind_double (st, 6,  a.detectedBpm);
    sqlite3_bind_double (st, 7,  a.downbeatSample);
    sqlite3_bind_int    (st, 8,  a.beatsPerBar);
    sqlite3_bind_int    (st, 9,  a.keyPitchClass);
    sqlite3_bind_int    (st, 10, a.keyIsMinor ? 1 : 0);
    sqlite3_bind_double (st, 11, a.keyConfidence);
    sqlite3_bind_blob   (st, 12, grid.getData(),  (int) grid.getSize(),  SQLITE_TRANSIENT);
    sqlite3_bind_blob   (st, 13, trans.getData(), (int) trans.getSize(), SQLITE_TRANSIENT);
    sqlite3_bind_blob   (st, 14, curve.getData(), (int) curve.getSize(), SQLITE_TRANSIENT);
    sqlite3_bind_int64  (st, 15, (sqlite3_int64) juce::Time::currentTimeMillis());

    sqlite3_step (st);
    sqlite3_finalize (st);
}

int64_t AnalysisCache::count() const
{
    if (! isOpen()) return 0;
    const juce::ScopedLock sl (impl->lock);
    sqlite3_stmt* st = nullptr;
    int64_t n = 0;
    if (sqlite3_prepare_v2 (impl->db, "SELECT COUNT(*) FROM analysis;", -1, &st, nullptr) == SQLITE_OK)
    {
        if (sqlite3_step (st) == SQLITE_ROW) n = sqlite3_column_int64 (st, 0);
        sqlite3_finalize (st);
    }
    return n;
}
