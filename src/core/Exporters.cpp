#include "core/Exporters.h"

#include <QByteArray>
#include <QSaveFile>
#include <QtEndian>

#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace td {
namespace {

bool writeAtomic(const QString& path, const QByteArray& data, QString* error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

void putFloat(char*& out, float value) {
    quint32 bits;
    std::memcpy(&bits, &value, sizeof bits);
    qToLittleEndian<quint32>(bits, out);
    out += 4;
}

template <typename T>
void appendLe(QByteArray& out, T value) {
    char bytes[sizeof(T)];
    qToLittleEndian<T>(value, bytes);
    out.append(bytes, sizeof(T));
}

void appendNumber(std::string& out, float value) {
    // to_chars is locale independent, and shortest form keeps the file small.
    char buffer[32];
    const auto end = std::to_chars(buffer, buffer + sizeof buffer, value).ptr;
    out.append(buffer, end);
}

void appendNumber(std::string& out, uint32_t value) {
    char buffer[16];
    const auto end = std::to_chars(buffer, buffer + sizeof buffer, value).ptr;
    out.append(buffer, end);
}

std::string xmlEscape(const QString& text) {
    std::string out;
    for (char c : text.toUtf8().toStdString()) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

quint32 crc32Of(const QByteArray& data) {
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int bit = 0; bit < 8; ++bit) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    quint32 crc = 0xFFFFFFFFu;
    for (unsigned char byte : data) crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

struct ZipEntry {
    QByteArray name;
    QByteArray content;
};

// Writes a deflated ZIP, which is the container format of a 3MF file.
bool buildZip(const std::vector<ZipEntry>& entries, QByteArray* zip, QString* error) {
    const quint16 kVersion = 20;
    const quint16 kDeflate = 8;
    const quint16 kDosDate = 0x21;  // 1980-01-01: identical input gives identical files
    QByteArray central;
    QByteArray out;

    for (const ZipEntry& entry : entries) {
        // qCompress returns a 4-byte size, then a zlib stream with a 2-byte
        // header and 4-byte checksum around the raw deflate data ZIP needs.
        const QByteArray compressed = qCompress(entry.content, 6);
        if (compressed.size() < 10) {
            *error = QStringLiteral("compression failed");
            return false;
        }
        const QByteArray deflated = compressed.mid(6, compressed.size() - 10);
        const quint32 crc = crc32Of(entry.content);
        const qint64 offset = out.size();
        if (entry.content.size() >= std::numeric_limits<quint32>::max() || offset + deflated.size() >= std::numeric_limits<quint32>::max()) {
            *error = QStringLiteral("the model is too large for a 3MF file");
            return false;
        }

        const auto common = [&](QByteArray& b) {
            appendLe<quint16>(b, kVersion);
            appendLe<quint16>(b, 0);  // flags
            appendLe<quint16>(b, kDeflate);
            appendLe<quint16>(b, 0);  // time
            appendLe<quint16>(b, kDosDate);
            appendLe<quint32>(b, crc);
            appendLe<quint32>(b, static_cast<quint32>(deflated.size()));
            appendLe<quint32>(b, static_cast<quint32>(entry.content.size()));
            appendLe<quint16>(b, static_cast<quint16>(entry.name.size()));
            appendLe<quint16>(b, 0);  // extra length
        };

        appendLe<quint32>(out, 0x04034b50);
        common(out);
        out.append(entry.name);
        out.append(deflated);

        appendLe<quint32>(central, 0x02014b50);
        appendLe<quint16>(central, kVersion);
        common(central);
        appendLe<quint16>(central, 0);  // comment length
        appendLe<quint16>(central, 0);  // disk number
        appendLe<quint16>(central, 0);  // internal attributes
        appendLe<quint32>(central, 0);  // external attributes
        appendLe<quint32>(central, static_cast<quint32>(offset));
        central.append(entry.name);
    }

    const qint64 centralOffset = out.size();
    out.append(central);
    appendLe<quint32>(out, 0x06054b50);
    appendLe<quint16>(out, 0);
    appendLe<quint16>(out, 0);
    appendLe<quint16>(out, static_cast<quint16>(entries.size()));
    appendLe<quint16>(out, static_cast<quint16>(entries.size()));
    appendLe<quint32>(out, static_cast<quint32>(central.size()));
    appendLe<quint32>(out, static_cast<quint32>(centralOffset));
    appendLe<quint16>(out, 0);
    *zip = std::move(out);
    return true;
}

}  // namespace

bool exportStl(const Mesh& mesh, const QString& path, QString* error) {
    if (mesh.empty()) {
        *error = QStringLiteral("there is nothing to export");
        return false;
    }
    const size_t triangles = mesh.triangleCount();
    QByteArray data(static_cast<qsizetype>(84 + triangles * 50), '\0');
    // The header must not begin with "solid", or readers may assume ASCII STL.
    const char header[] = "Cubra binary STL";
    std::memcpy(data.data(), header, sizeof header - 1);
    qToLittleEndian<quint32>(static_cast<quint32>(triangles), data.data() + 80);

    char* out = data.data() + 84;
    for (size_t t = 0; t < triangles; ++t) {
        const float* a = &mesh.positions[mesh.indices[t * 3] * 3];
        const float* b = &mesh.positions[mesh.indices[t * 3 + 1] * 3];
        const float* c = &mesh.positions[mesh.indices[t * 3 + 2] * 3];
        const float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        const float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (length > 0) {
            nx /= length;
            ny /= length;
            nz /= length;
        }
        putFloat(out, nx);
        putFloat(out, ny);
        putFloat(out, nz);
        for (const float* vertex : {a, b, c})
            for (int axis = 0; axis < 3; ++axis) putFloat(out, vertex[axis]);
        out += 2;  // attribute byte count stays zero
    }
    return writeAtomic(path, data, error);
}

bool export3mf(const Mesh& mesh, const QString& path, const QString& objectName, QString* error) {
    if (mesh.empty()) {
        *error = QStringLiteral("there is nothing to export");
        return false;
    }

    std::string model;
    model.reserve(mesh.vertexCount() * 48 + mesh.triangleCount() * 48 + 1024);
    model +=
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<model unit=\"millimeter\" xml:lang=\"en-US\" "
        "xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n"
        "<resources>\n<object id=\"1\" type=\"model\" name=\"";
    model += xmlEscape(objectName);
    model += "\">\n<mesh>\n<vertices>\n";
    for (size_t v = 0; v < mesh.vertexCount(); ++v) {
        model += "<vertex x=\"";
        appendNumber(model, mesh.positions[v * 3]);
        model += "\" y=\"";
        appendNumber(model, mesh.positions[v * 3 + 1]);
        model += "\" z=\"";
        appendNumber(model, mesh.positions[v * 3 + 2]);
        model += "\"/>\n";
    }
    model += "</vertices>\n<triangles>\n";
    for (size_t t = 0; t < mesh.triangleCount(); ++t) {
        model += "<triangle v1=\"";
        appendNumber(model, mesh.indices[t * 3]);
        model += "\" v2=\"";
        appendNumber(model, mesh.indices[t * 3 + 1]);
        model += "\" v3=\"";
        appendNumber(model, mesh.indices[t * 3 + 2]);
        model += "\"/>\n";
    }
    model += "</triangles>\n</mesh>\n</object>\n</resources>\n<build>\n<item objectid=\"1\"/>\n</build>\n</model>\n";

    const QByteArray contentTypes =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>"
        "</Types>\n";
    const QByteArray relationships =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" "
        "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>"
        "</Relationships>\n";

    QByteArray zip;
    if (!buildZip({{"[Content_Types].xml", contentTypes},
                   {"_rels/.rels", relationships},
                   {"3D/3dmodel.model", QByteArray::fromStdString(model)}},
                  &zip, error))
        return false;
    return writeAtomic(path, zip, error);
}

}  // namespace td
