#include "Module.h"

#include "Asserts.h"
#include "InputStream.h"
#include "JsonUtils.h"
#include "OutputStream.h"
#include "Sequence.h"
#include "WmdFileTypes.h"

BEGIN_NAMESPACE(AudioTools)

//------------------------------------------------------------------------------------------------------------------------------------------
// Read the module from a json document
//------------------------------------------------------------------------------------------------------------------------------------------
void Module::readFromJson(const rapidjson::Document& doc) THROWS {
    // Read basic module properties
    if (!doc.IsObject())
        throw "Module root must be a json object!";
    
    maxActiveSequences = JsonUtils::clampedGetOrDefault<uint8_t>(doc, "maxActiveSequences", 1);
    maxActiveTracks = JsonUtils::clampedGetOrDefault<uint8_t>(doc, "maxActiveTracks", 1);
    maxGatesPerSeq = JsonUtils::clampedGetOrDefault<uint8_t>(doc, "maxGatesPerSeq", 1);
    maxItersPerSeq = JsonUtils::clampedGetOrDefault<uint8_t>(doc, "maxItersPerSeq", 1);
    maxCallbacks = JsonUtils::clampedGetOrDefault<uint8_t>(doc, "maxCallbacks", 1);
    
    // Read the PSX sound driver patch group
    if (const auto patchGroupIter = doc.FindMember("psxPatchGroup"); patchGroupIter != doc.MemberEnd()) {
        const rapidjson::Value& patchGroupObj = patchGroupIter->value;
        psxPatchGroup.readFromJson(patchGroupObj);
    }
    
    // Read all sequences
    sequences.clear();

    if (const rapidjson::Value* const pSequencesArray = JsonUtils::tryGetArray(doc, "sequences")) {
        for (rapidjson::SizeType i = 0; i < pSequencesArray->Size(); ++i) {
            const rapidjson::Value& sequenceObj = (*pSequencesArray)[i];
            Sequence& sequence = sequences.emplace_back();
            sequence.readFromJson(sequenceObj);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Write the entire module to a json document
//------------------------------------------------------------------------------------------------------------------------------------------
void Module::writeToJson(rapidjson::Document& doc) const noexcept {
    // Add basic module properties
    ASSERT(doc.IsObject());
    rapidjson::Document::AllocatorType& jsonAlloc = doc.GetAllocator();

    doc.AddMember("maxActiveSequences", maxActiveSequences, jsonAlloc);
    doc.AddMember("maxActiveTracks", maxActiveTracks, jsonAlloc);
    doc.AddMember("maxGatesPerSeq", maxGatesPerSeq, jsonAlloc);
    doc.AddMember("maxItersPerSeq", maxItersPerSeq, jsonAlloc);
    doc.AddMember("maxCallbacks", maxCallbacks, jsonAlloc);

    // Write the PSX driver patch group
    {
        rapidjson::Value patchGroupObj(rapidjson::kObjectType);
        psxPatchGroup.writeToJson(patchGroupObj, jsonAlloc);
        doc.AddMember("psxPatchGroup", patchGroupObj, jsonAlloc);
    }
    
    // Write all the sequences
    {
        rapidjson::Value sequencesArray(rapidjson::kArrayType);
        
        for (const Sequence& sequence : sequences) {
            rapidjson::Value sequenceObj(rapidjson::kObjectType);
            sequence.writeToJson(sequenceObj, jsonAlloc);
            sequencesArray.PushBack(sequenceObj, jsonAlloc);
        }
        
        doc.AddMember("sequences", sequencesArray, jsonAlloc);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Read the entire module from the specified .WMD file
//------------------------------------------------------------------------------------------------------------------------------------------
void Module::readFromWmd(InputStream& in) THROWS {
    // Read the header for the module file, verify correct and save it's basic info
    WmdModuleHdr moduleHdr = {};
    in.read(moduleHdr);
    moduleHdr.endianCorrect();

    if (moduleHdr.moduleId != WMD_MODULE_ID)
        throw "Bad .WMD file ID!";

    if (moduleHdr.moduleVersion != WMD_VERSION)
        throw "Bad .WMD file version!";
    
    this->maxActiveSequences = moduleHdr.maxActiveSequences;
    this->maxActiveTracks = moduleHdr.maxActiveTracks;
    this->maxGatesPerSeq = moduleHdr.maxGatesPerSeq;
    this->maxItersPerSeq = moduleHdr.maxItersPerSeq;
    this->maxCallbacks = moduleHdr.maxCallbacks;

    // Read all patch groups
    for (uint32_t patchGrpIdx = 0; patchGrpIdx < moduleHdr.numPatchGroups; ++patchGrpIdx) {
        // Read the header for the patch group
        WmdPatchGroupHdr patchGroupHdr = {};
        in.read(patchGroupHdr);
        patchGroupHdr.endianCorrect();

        // If it's a PlayStation format patch group read it, otherwise skip
        if (patchGroupHdr.driverId == WmdSoundDriverId::PSX) {
            psxPatchGroup = {};
            psxPatchGroup.readFromWmd(in, patchGroupHdr);
        } else {
            std::printf("Warning: skipping unsupported format patch group with driver id '%u'!\n", (unsigned) patchGroupHdr.driverId);
            skipReadingWmdPatchGroup(in, patchGroupHdr);
        }
    }

    // Read all sequences
    sequences.clear();
    
    for (uint32_t seqIdx = 0; seqIdx < moduleHdr.numSequences; ++seqIdx) {
        sequences.emplace_back().readFromWmd(in);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Write the entire module to the specified .WMD file.
//
//  Notes:
//      (1) Just writes the actual .WMD data, does not pad out to 2,048 byte multiples like the .WMD files on the PSX Doom disc.
//      (2) If some parts of the module are invalidly configured, writing may fail.
//          The output from this process should always be a valid .WMD file.
//------------------------------------------------------------------------------------------------------------------------------------------
void Module::writeToWmd(OutputStream& out) const THROWS {
    // Make up the module header, endian correct and serialize it
    {
        WmdModuleHdr hdr = {};
        hdr.moduleId = WMD_MODULE_ID;
        hdr.moduleVersion = WMD_VERSION;

        if (sequences.size() > UINT16_MAX)
            throw "Too many sequences for a .WMD file!";

        hdr.numSequences = (uint16_t) sequences.size();
        hdr.numPatchGroups = 1; // Just the PSX patch group to be written
        hdr.maxActiveSequences = maxActiveSequences;
        hdr.maxActiveTracks = maxActiveTracks;
        hdr.maxGatesPerSeq = maxGatesPerSeq;
        hdr.maxItersPerSeq = maxItersPerSeq;
        hdr.maxCallbacks = maxCallbacks;

        hdr.endianCorrect();
        out.write(hdr);
    }

    // Write the PSX driver patch group
    psxPatchGroup.writeToWmd(out);

    // Write all the sequences
    for (const Sequence& sequence : sequences) {
        sequence.writeToWmd(out);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Skip the data in a .WMD file for a patch group of an unknown type
//------------------------------------------------------------------------------------------------------------------------------------------
void Module::skipReadingWmdPatchGroup(InputStream& in, const WmdPatchGroupHdr& patchGroupHdr) THROWS {
    if (patchGroupHdr.loadFlags & LOAD_PATCHES) {
        in.skipBytes((size_t) patchGroupHdr.numPatches * patchGroupHdr.patchSize);
    }

    if (patchGroupHdr.loadFlags & LOAD_PATCH_VOICES) {
        in.skipBytes((size_t) patchGroupHdr.numPatchVoices * patchGroupHdr.patchVoiceSize);
    }

    if (patchGroupHdr.loadFlags & LOAD_PATCH_SAMPLES) {
        in.skipBytes((size_t) patchGroupHdr.numPatchSamples * patchGroupHdr.patchSampleSize);
    }

    if (patchGroupHdr.loadFlags & LOAD_DRUM_PATCHES) {
        in.skipBytes((size_t) patchGroupHdr.numDrumPatches * patchGroupHdr.drumPatchSize);
    }

    if (patchGroupHdr.loadFlags & LOAD_EXTRA_DATA) {
        in.skipBytes(patchGroupHdr.extraDataSize);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Reads a variable length quantity from track data, similar to how certain data is encoded in the MIDI standard.
// Returns number of bytes read and the output value, which may be up to 5 bytes.
//------------------------------------------------------------------------------------------------------------------------------------------
uint32_t Module::readVarLenQuant(InputStream& in, uint32_t& valueOut) THROWS {
    // Grab the first byte in the quantity
    uint8_t curByte = in.read<uint8_t>();
    uint32_t numBytesRead = 1;

    // The top bit set on each byte means there is another byte to follow.
    // Each byte can therefore only encode 7 bits, so we need 5 of them to encode 32-bits:
    uint32_t decodedVal = curByte & 0x7Fu;

    while (curByte & 0x80) {
        // Make room for more data
        decodedVal <<= 7;

        // Read the next byte and incorporate into the value
        curByte = in.read<uint8_t>();
        numBytesRead++;
        decodedVal |= (uint32_t) curByte & 0x7Fu;

        // Sanity check, there should only be at most 5 bytes!
        if (numBytesRead > 5)
            throw "Read VLQ: too many bytes! Quantity encoding is not valid!";
    }

    valueOut = decodedVal;
    return numBytesRead;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Write a variable length quantity to track data, similar to how certain data is encoded in the MIDI standard.
// Returns number of bytes written, which may be up to 5 bytes.
//------------------------------------------------------------------------------------------------------------------------------------------
uint32_t Module::writeVarLenQuant(OutputStream& out, const uint32_t valueIn) THROWS {
    // Encode the given value into a stack buffer, writing a '0x80' bit flag whenever more bytes follow
    uint8_t encodedBytes[8];
    uint8_t* pEncodedByte = encodedBytes;

    {
        *pEncodedByte = (uint8_t)(valueIn & 0x7F);
        pEncodedByte++;
        uint32_t bitsLeftToEncode = valueIn >> 7;

        while (bitsLeftToEncode != 0) {
            *pEncodedByte = (uint8_t)(bitsLeftToEncode | 0x80);
            bitsLeftToEncode >>= 7;
            pEncodedByte++;
        }
    }
    
    // Write the encoded value to the given output stream.
    // Note that the ordering here gets reversed, so it winds up being read in the correct order.
    uint32_t numBytesWritten = 0;

    do {
        pEncodedByte--;
        out.write(*pEncodedByte);
        numBytesWritten++;
    } while (*pEncodedByte & 0x80);

    return numBytesWritten;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Tells how many bytes would be used to encode a 32-bit value using variable length (MIDI style) encoding.
// The returned answer will be between 1-5 bytes.
//------------------------------------------------------------------------------------------------------------------------------------------
uint32_t Module::getVarLenQuantLen(const uint32_t valueIn) noexcept {
    uint32_t numEncodedBytes = 1;
    uint32_t bitsLeftToEncode = valueIn >> 7;

    while (bitsLeftToEncode != 0) {
        numEncodedBytes++;
        bitsLeftToEncode >>= 7;
    }

    return numEncodedBytes;
}

END_NAMESPACE(AudioTools)
