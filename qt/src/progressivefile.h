#pragma once
#include <QString>
#include <cstdint>
#include <cstddef>
namespace ProgressiveFile {
struct Info { uint64_t device, inode, size; };
bool absolute(const QString &path);
intptr_t open(const QString &path);
Info inspect(intptr_t file, uint64_t available);
int64_t read(intptr_t file, uint64_t offset, void *data, size_t count);
void close(intptr_t file);
}
