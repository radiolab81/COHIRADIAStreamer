#ifndef BINARYSTRUCTURES_H
#define BINARYSTRUCTURES_H

#include <cstdint>

// --- BINÄR-STRUKTUREN ---
#pragma pack(push, 1)
struct ChunkHeader { char id[4]; uint32_t size; };
struct RiffHeader { char chunkId[4]; uint32_t chunkSize; char format[4]; };
struct FmtStruct { uint16_t audioFormat; uint16_t numChannels; uint32_t sampleRate; uint32_t byteRate; uint16_t blockAlign; uint16_t bitsPerSample; };
struct AuxiContent { uint8_t padding[68]; char filename[96]; };
#pragma pack(pop)

#endif // BINARYSTRUCTURES_H