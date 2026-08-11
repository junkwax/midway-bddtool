#include "Core/zip_writer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <string>
#include <vector>

namespace {

struct ZipEntry {
    std::string name;
    unsigned long crc;
    unsigned long size;
    unsigned long offset;
    unsigned short dos_time;
    unsigned short dos_date;
};

unsigned long crc32_update(unsigned long crc, const unsigned char *buf, size_t len)
{
    static unsigned long table[256];
    static bool built = false;
    if (!built) {
        for (unsigned long n = 0; n < 256; n++) {
            unsigned long c = n;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        built = true;
    }
    crc ^= 0xFFFFFFFFUL;
    while (len--)
        crc = table[(crc ^ *buf++) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFUL;
}

void dos_stamp(unsigned short *dos_time, unsigned short *dos_date)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (!t) {
        *dos_time = 0;
        *dos_date = 0x21;   /* 1980-01-01 */
        return;
    }
    *dos_time = (unsigned short)((t->tm_hour << 11) | (t->tm_min << 5) | (t->tm_sec >> 1));
    *dos_date = (unsigned short)(((t->tm_year - 80) << 9) | ((t->tm_mon + 1) << 5) | t->tm_mday);
}

} // namespace

struct ZipWriter {
    FILE *f = nullptr;
    std::vector<ZipEntry> entries;
    bool ok = true;
    std::string path;
};

static void put16(ZipWriter *z, unsigned v)
{
    unsigned char b[2] = { (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF) };
    if (fwrite(b, 1, 2, z->f) != 2) z->ok = false;
}

static void put32(ZipWriter *z, unsigned long v)
{
    unsigned char b[4] = { (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF),
                           (unsigned char)((v >> 16) & 0xFF), (unsigned char)((v >> 24) & 0xFF) };
    if (fwrite(b, 1, 4, z->f) != 4) z->ok = false;
}

ZipWriter *zip_writer_open(const char *path)
{
    if (!path || !path[0]) return nullptr;
    FILE *f = fopen(path, "wb");
    if (!f) return nullptr;
    ZipWriter *z = new (std::nothrow) ZipWriter();
    if (!z) {
        fclose(f);
        return nullptr;
    }
    z->f = f;
    z->path = path;
    return z;
}

bool zip_writer_add_memory(ZipWriter *z, const void *data, size_t len, const char *name_in_zip)
{
    if (!z || !z->ok || !name_in_zip || !name_in_zip[0]) return false;

    ZipEntry e;
    e.name = name_in_zip;
    e.size = (unsigned long)len;
    e.crc = crc32_update(0, (const unsigned char *)data, len);
    long off = ftell(z->f);
    if (off < 0) { z->ok = false; return false; }
    e.offset = (unsigned long)off;
    dos_stamp(&e.dos_time, &e.dos_date);

    put32(z, 0x04034B50UL);          /* local file header */
    put16(z, 20);                    /* version needed */
    put16(z, 0);                     /* flags */
    put16(z, 0);                     /* method 0 = stored */
    put16(z, e.dos_time);
    put16(z, e.dos_date);
    put32(z, e.crc);
    put32(z, e.size);                /* compressed == uncompressed */
    put32(z, e.size);
    put16(z, (unsigned)e.name.size());
    put16(z, 0);                     /* extra length */
    if (fwrite(e.name.data(), 1, e.name.size(), z->f) != e.name.size()) z->ok = false;
    if (len && fwrite(data, 1, len, z->f) != len) z->ok = false;

    if (!z->ok) return false;
    z->entries.push_back(e);
    return true;
}

bool zip_writer_add_file(ZipWriter *z, const char *src_path, const char *name_in_zip)
{
    if (!z || !z->ok || !src_path || !src_path[0]) return false;
    FILE *in = fopen(src_path, "rb");
    if (!in) return false;

    std::vector<unsigned char> buf;
    unsigned char chunk[8192];
    size_t n;
    bool read_ok = true;
    while ((n = fread(chunk, 1, sizeof chunk, in)) > 0) {
        try {
            buf.insert(buf.end(), chunk, chunk + n);
        } catch (const std::bad_alloc &) {
            read_ok = false;
            break;
        }
    }
    if (ferror(in)) read_ok = false;
    fclose(in);
    if (!read_ok) return false;

    return zip_writer_add_memory(z, buf.empty() ? (const void *)"" : (const void *)buf.data(),
                                 buf.size(), name_in_zip);
}

bool zip_writer_close(ZipWriter *z)
{
    if (!z) return false;
    bool ok = z->ok;

    long dir_start = ftell(z->f);
    if (dir_start < 0) ok = false;

    for (size_t i = 0; ok && i < z->entries.size(); i++) {
        const ZipEntry &e = z->entries[i];
        put32(z, 0x02014B50UL);      /* central directory header */
        put16(z, 20);                /* version made by */
        put16(z, 20);                /* version needed */
        put16(z, 0);
        put16(z, 0);                 /* stored */
        put16(z, e.dos_time);
        put16(z, e.dos_date);
        put32(z, e.crc);
        put32(z, e.size);
        put32(z, e.size);
        put16(z, (unsigned)e.name.size());
        put16(z, 0);                 /* extra */
        put16(z, 0);                 /* comment */
        put16(z, 0);                 /* disk number */
        put16(z, 0);                 /* internal attrs */
        put32(z, 0);                 /* external attrs */
        put32(z, e.offset);
        if (fwrite(e.name.data(), 1, e.name.size(), z->f) != e.name.size()) z->ok = false;
        ok = ok && z->ok;
    }

    long dir_end = ftell(z->f);
    if (dir_end < 0) ok = false;

    if (ok) {
        put32(z, 0x06054B50UL);      /* end of central directory */
        put16(z, 0);
        put16(z, 0);
        put16(z, (unsigned)z->entries.size());
        put16(z, (unsigned)z->entries.size());
        put32(z, (unsigned long)(dir_end - dir_start));
        put32(z, (unsigned long)dir_start);
        put16(z, 0);                 /* comment length */
        ok = z->ok;
    }

    if (fclose(z->f) != 0) ok = false;
    std::string path = z->path;
    delete z;
    if (!ok) remove(path.c_str());
    return ok;
}
