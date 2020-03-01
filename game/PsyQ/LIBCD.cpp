//------------------------------------------------------------------------------------------------------------------------------------------
// Module containing a partial reimplementation of the PSY-Q 'LIBCD' library.
// These functions are not neccesarily faithful to the original code, and are reworked to make the game run in it's new environment.
//------------------------------------------------------------------------------------------------------------------------------------------

#include "LIBCD.h"

#include "LIBAPI.h"
#include "LIBC2.h"
#include "LIBETC.h"
#include "PcPsx/Finally.h"

BEGIN_THIRD_PARTY_INCLUDES

#include <device/cdrom/cdrom.h>
#include <disc/disc.h>

END_THIRD_PARTY_INCLUDES

// N.B: must be done LAST due to MIPS register macros
#include "PsxVm/PsxVm.h"

// CD-ROM constants
static constexpr int32_t CD_SECTORS_PER_SEC = 75;       // The number of CD sectors per second of audio
static constexpr int32_t CD_LEAD_SECTORS    = 150;      // How many sectors are assigned to the lead in track, which has the TOC for the disc

// This is the 'readcnt' cdrom amount that Avocado will read data on
static constexpr int AVOCADO_DATA_READ_CNT = 1150;

// Callbacks for when a command completes and when a data sector is ready
static CdlCB gpLIBCD_CD_cbsync;
static CdlCB gpLIBCD_CD_cbready;

// Result bytes for the most recent CD command
static uint8_t gLastCdCmdResult[8];

// These are local to this module: forward declare here
void LIBCD_EVENT_def_cbsync(const CdlStatus status, const uint8_t pResult[8]) noexcept;
void LIBCD_EVENT_def_cbready(const CdlStatus status, const uint8_t pResult[8]) noexcept;

//------------------------------------------------------------------------------------------------------------------------------------------
// Call a libcd callback ('sync' or 'ready' callbacks)
//------------------------------------------------------------------------------------------------------------------------------------------
static void invokeCallback(
    const CdlCB callback,
    const CdlStatus status,
    const uint8_t resultBytes[8]
) noexcept {
    if (callback) {
        callback(status, resultBytes);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Read a byte from the cdrom's result fifo buffer
//------------------------------------------------------------------------------------------------------------------------------------------
static uint8_t readCdCmdResultByte() noexcept {
    device::cdrom::CDROM& cdrom = *PsxVm::gpCdrom;
    fifo<uint8_t, 16>& cmdResultFifo = cdrom.CDROM_response;
    uint8_t resultByte = 0;

    if (!cmdResultFifo.empty()) {
        resultByte = cmdResultFifo.get();

        if (cmdResultFifo.empty()) {
            cdrom.status.responseFifoEmpty = 0;
        }
    } else {
        cdrom.status.responseFifoEmpty = 0;
    }

    return resultByte;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Step the CD-ROM and invoke 'data ready' callbacks if a new sector was read
//------------------------------------------------------------------------------------------------------------------------------------------
static void stepCdromWithCallbacks() noexcept {
    // Determine if a new sector will be read
    device::cdrom::CDROM& cdrom = *PsxVm::gpCdrom;

    const bool bWillReadNewSector = (
        (cdrom.stat.read || cdrom.stat.play) &&
        (cdrom.readcnt == AVOCADO_DATA_READ_CNT)
    );

    // Clear the data buffers if a new sector is about to be read
    if (bWillReadNewSector) {
        cdrom.rawSector.clear();
        cdrom.dataBuffer.clear();
        cdrom.dataBufferPointer = 0;
        cdrom.status.dataFifoEmpty = 0;
    }

    // Advance the cdrom emulation
    cdrom.step();

    // If we read a new sector then setup the data buffer fifo and let the callback know
    if (bWillReadNewSector) {
        ASSERT(!cdrom.rawSector.empty());
        cdrom.dataBuffer = cdrom.rawSector;
        cdrom.dataBufferPointer = 0;
        cdrom.status.dataFifoEmpty = 1;
        
        invokeCallback(gpLIBCD_CD_cbready, CdlDataReady, gLastCdCmdResult);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Handle executing a command to the cdrom drive.
// Note: only a subset of the available commands are supported, just the ones needed for DOOM.
//------------------------------------------------------------------------------------------------------------------------------------------
static bool handleCdCmd(const CdlCmd cmd, const uint8_t* const pArgs, uint8_t resultBytesOut[8]) noexcept {    
    device::cdrom::CDROM& cdrom = *PsxVm::gpCdrom;

    // Save the result globally and for the caller on exit
    uint8_t cmdResult[8] = {};
    static_assert(sizeof(cmdResult) == sizeof(gLastCdCmdResult));

    const auto exitActions = finally([&](){
        std::memcpy(gLastCdCmdResult, cmdResult, sizeof(cmdResult));

        if (resultBytesOut) {
            std::memcpy(resultBytesOut, cmdResult, sizeof(cmdResult));
        }
    });

    // Handle the command    
    fifo<uint8_t, 16>& cdparams = cdrom.CDROM_params;
    
    switch (cmd) {
        case CdlPause:
            cdrom.handleCommand(cmd);
            cmdResult[0] = readCdCmdResultByte();   // Status bits
            invokeCallback(gpLIBCD_CD_cbsync, CdlComplete, cmdResult);
            return true;

        case CdlSetloc:
            cdparams.add(pArgs[0]);     // CdlLOC.minute
            cdparams.add(pArgs[1]);     // CdlLOC.second
            cdparams.add(pArgs[2]);     // CdlLOC.sector
            cdrom.handleCommand(cmd);
            cmdResult[0] = readCdCmdResultByte();   // Status bits

            // Goto the desired sector immediately and invoke the callback
            cdrom.readSector = cdrom.seekSector;
            invokeCallback(gpLIBCD_CD_cbsync, CdlComplete, cmdResult);
            return true;

        case CdlSeekP:
            if (pArgs) {
                // Set the current read/seek position if specified
                disc::Position pos;
                pos.mm = bcd::toBinary(pArgs[0]);   // CdlLOC.minute
                pos.ss = bcd::toBinary(pArgs[1]);   // CdlLOC.second
                pos.ff = bcd::toBinary(pArgs[2]);   // CdlLOC.sector

                const int sector = pos.toLba();
                cdrom.seekSector = sector;
                cdrom.readSector = sector;
            }

            cdrom.handleCommand(cmd);
            cmdResult[0] = readCdCmdResultByte();   // Status bits
            cmdResult[1] = readCdCmdResultByte();   // Location: minute (BCD)
            cmdResult[2] = readCdCmdResultByte();   // Location: second (BCD)
            invokeCallback(gpLIBCD_CD_cbsync, CdlComplete, cmdResult);
            return true;

        case CdlSetmode:
            cdparams.add(pArgs[0]);                 // Mode
            cdrom.handleCommand(cmd);
            cmdResult[0] = readCdCmdResultByte();   // Status bits
            invokeCallback(gpLIBCD_CD_cbsync, CdlComplete, cmdResult);
            return true;

        case CdlReadN:
        case CdlReadS:
            // Set the location to read and issue the read command
            if (pArgs) {
                cdparams.add(pArgs[0]);     // CdlLOC.minute
                cdparams.add(pArgs[1]);     // CdlLOC.second
                cdparams.add(pArgs[2]);     // CdlLOC.sector
            }

            cdrom.handleCommand(cmd);
            cmdResult[0] = readCdCmdResultByte();   // Status bits
            invokeCallback(gpLIBCD_CD_cbsync, CdlComplete, cmdResult);

            // Set the read location immediately
            {
                const uint32_t minute = bcd::toBinary(pArgs[0]);
                const uint32_t second = bcd::toBinary(pArgs[1]);
                const uint32_t sector = bcd::toBinary(pArgs[2]);

                cdrom.seekSector = sector + (second * 75) + (minute * 60 * 75);
                cdrom.readSector = cdrom.seekSector;
            }
            
            return true;

        case CdlPlay:
            if (pArgs) {
                cdparams.add(pArgs[0]);     // CdlLOC.track
            }

            cdrom.handleCommand(cmd);
            cmdResult[0] = readCdCmdResultByte();   // Status bits
            return true;

        default:
            ASSERT_FAIL("Unhandled or unknown cd command!");
            break;
    }

    return false;
}

void LIBCD_CdInit() noexcept {
loc_80054B00:
    sp -= 0x18;
    sw(s0, sp + 0x10);
    s0 = 4;                                             // Result = 00000004
    sw(ra, sp + 0x14);
loc_80054B10:
    a0 = 1;                                             // Result = 00000001
    LIBCD_CdReset();
    v1 = 1;                                             // Result = 00000001
    s0--;
    if (v0 != v1) goto loc_80054B5C;

    LIBCD_CdSyncCallback(LIBCD_EVENT_def_cbsync);
    LIBCD_CdReadyCallback(LIBCD_EVENT_def_cbready);

    v0 = 1;                                             // Result = 00000001
    goto loc_80054B7C;
loc_80054B5C:
    v0 = -1;                                            // Result = FFFFFFFF
    if (s0 != v0) goto loc_80054B10;
    a0 = 0x80010000;                                    // Result = 80010000
    a0 += 0x1F54;                                       // Result = STR_LIBCD_CdInit_Failed_Err[0] (80011F54)
    LIBC2_printf();
    v0 = 0;                                             // Result = 00000000
loc_80054B7C:
    ra = lw(sp + 0x14);
    s0 = lw(sp + 0x10);
    sp += 0x18;
    return;
}

void LIBCD_EVENT_def_cbsync([[maybe_unused]] const CdlStatus status, [[maybe_unused]] const uint8_t pResult[8]) noexcept {
    // TODO: remove/replace this
    a0 = 0xF0000003;
    a1 = 0x20;
    LIBAPI_DeliverEvent();
}

void LIBCD_EVENT_def_cbready([[maybe_unused]] const CdlStatus status, [[maybe_unused]] const uint8_t pResult[8]) noexcept {
    // TODO: remove/replace this
    a0 = 0xF0000003;
    a1 = 0x40;
    LIBAPI_DeliverEvent();
}

void LIBCD_CdReset() noexcept {
loc_80054C28:
    sp -= 0x18;
    sw(s0, sp + 0x10);
    sw(ra, sp + 0x14);
    s0 = a0;
    LIBCD_CD_init();
    {
        const bool bJump = (v0 != 0);
        v0 = 0;                                         // Result = 00000000
        if (bJump) goto loc_80054C64;
    }
    v0 = 1;                                             // Result = 00000001
    if (s0 != v0) goto loc_80054C64;
    LIBCD_CD_initvol();
    {
        const bool bJump = (v0 != 0);
        v0 = 0;                                         // Result = 00000000
        if (bJump) goto loc_80054C64;
    }
    v0 = 1;                                             // Result = 00000001
loc_80054C64:
    ra = lw(sp + 0x14);
    s0 = lw(sp + 0x10);
    sp += 0x18;
    return;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Cancel the current cd command which is in-flight
//------------------------------------------------------------------------------------------------------------------------------------------
void LIBCD_CdFlush() noexcept {
    // This doesn't need to do anything for this LIBCD reimplementation.
    // All commands are executed synchronously!
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Check for a cd command's completion or wait for it to complete.
// If 'mode' is '0' then that indicates 'wait for completion'.
//------------------------------------------------------------------------------------------------------------------------------------------
CdlStatus LIBCD_CdSync([[maybe_unused]] const int32_t mode, uint8_t pResult[8]) noexcept {
    // Just step the CDROM a little in case this is being polled
    stepCdromWithCallbacks();

    // Give the caller the result of the last cd operation if required
    if (pResult) {
        std::memcpy(pResult, gLastCdCmdResult, sizeof(gLastCdCmdResult));
    }

    return CdlComplete;
}

void _thunk_LIBCD_CdSync() noexcept {
    v0 = LIBCD_CdSync(a0, vmAddrToPtr<uint8_t>(a1));
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Wait for cdrom data to be ready or check if it is ready.
// Mode '0' means block until data is ready, otherwise we simply return the current status.
//------------------------------------------------------------------------------------------------------------------------------------------
CdlStatus LIBCD_CdReady(const int32_t mode, uint8_t pResult[8]) noexcept {
    device::cdrom::CDROM& cdrom = *PsxVm::gpCdrom;

    const auto exitActions = finally([&](){
        // Copy the last command result on exit if the caller wants it
        if (pResult) {
            std::memcpy(pResult, gLastCdCmdResult, sizeof(gLastCdCmdResult));
        }
    });

    if (mode == 0) {
        // Block until there is data: see if there is some
        if (!cdrom.isBufferEmpty()) {
            return CdlDataReady;
        } else {
            // No data: make a read of some happen immediately
            cdrom.readcnt = AVOCADO_DATA_READ_CNT;
            stepCdromWithCallbacks();
            ASSERT(!cdrom.isBufferEmpty());

            return CdlDataReady;
        }
    }
    else {
        // Just querying whether there is data or not.
        // Emulate the CD a little in case this is being polled in a loop and return the status.
        stepCdromWithCallbacks();
        return (!cdrom.isBufferEmpty()) ? CdlDataReady : CdlNoIntr;
    }
}

void _thunk_LIBCD_CdReady() noexcept {
    v0 = LIBCD_CdReady(a0, vmAddrToPtr<uint8_t>(a1));
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Set the callback for when a cd command completes and return the previous one
//------------------------------------------------------------------------------------------------------------------------------------------
CdlCB LIBCD_CdSyncCallback(const CdlCB syncCallback) noexcept {
    const CdlCB oldCallback = gpLIBCD_CD_cbsync;
    gpLIBCD_CD_cbsync = syncCallback;
    return oldCallback;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Set the callback for when a cd data sector is ready and return the previous one
//------------------------------------------------------------------------------------------------------------------------------------------
CdlCB LIBCD_CdReadyCallback(const CdlCB readyCallback) noexcept {
    const CdlCB oldCallback = gpLIBCD_CD_cbready;
    gpLIBCD_CD_cbready = readyCallback;
    return oldCallback;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Issue a command to the CDROM system, possibly with arguments and return the result in the given (optional) buffer.
// Returns 'true' if successful.
//
// In the original PsyQ SDK some of the commands would block, while others would not block.
// In this version however ALL commands are executed SYNCHRONOUSLY.
//------------------------------------------------------------------------------------------------------------------------------------------
bool LIBCD_CdControl(const CdlCmd cmd, const uint8_t* const pArgs, uint8_t pResult[8]) noexcept {
    return handleCdCmd(cmd, pArgs, pResult);
}

void _thunk_LIBCD_CdControl() noexcept {
    v0 = LIBCD_CdControl((CdlCmd) a0, vmAddrToPtr<const uint8_t>(a1), vmAddrToPtr<uint8_t>(a2));
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Issue a command to the CDROM system and return 'true' if successful.
//
// In the original PsyQ SDK this was similar to 'CdControl' except it was faster (though less safe) by bypassing some of the handshaking
// with the cdrom drive. In this reimplementation however it functions the same as the normal 'CdControl'.
// Note: all commands are also executed SYNCHRONOUSLY.
//------------------------------------------------------------------------------------------------------------------------------------------
bool LIBCD_CdControlF(const CdlCmd cmd, const uint8_t* const pArgs) noexcept {
    return handleCdCmd(cmd, pArgs, nullptr);
}

void _thunk_LIBCD_CdControlF() noexcept {
    v0 = LIBCD_CdControlF((CdlCmd) a0, vmAddrToPtr<const uint8_t>(a1));
}

void LIBCD_CdMix() noexcept {
loc_800550F0:
    sp -= 0x18;
    sw(ra, sp + 0x10);
    LIBCD_CD_vol();
    v0 = 1;                                             // Result = 00000001
    ra = lw(sp + 0x10);
    sp += 0x18;
    return;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Reads the requested number of 32-bit words from the CDROM's sector data buffer.
// Returns 'true' if successful, which will be always.
//------------------------------------------------------------------------------------------------------------------------------------------
bool LIBCD_CdGetSector(void* const pDst, const int32_t readSizeInWords) noexcept {
    device::cdrom::CDROM& cdrom = *PsxVm::gpCdrom;
    
    // Figure out the size in bytes we want (request is for words)
    const int32_t readSizeInBytes = readSizeInWords * sizeof(uint32_t);
    uint8_t* const pDstBytes = (uint8_t*) pDst;

    // The previous PSYQ code wrote '0x80' 'to 0x1F801803' (The 'CD Request' register).
    // This means that we want data and if the data FIFO is at the end then we should reset it.
    if (cdrom.isBufferEmpty()) {
        cdrom.dataBuffer = cdrom.rawSector;
        cdrom.dataBufferPointer = 0;
        cdrom.status.dataFifoEmpty = 1;
    }

    // From Avocado, figure out the offset of the data in the sector buffer. 12 bytes are for sector header.
    //  Mode 0 - 2048 byte sectors
    //  Mode 1 - 2340 byte sectors (no sync bytes also)
    //
    const int32_t dataStart = (cdrom.mode.sectorSize == 0) ? 24 : 12;

    // Read the bytes: note that reading past the end of the buffer on the real hardware would do the following:
    //  "The PSX will repeat the byte at index [800h-8] or [924h-4] as padding value."
    // See: 
    //  https://problemkaputt.de/psx-spx.htm#cdromcontrollerioports
    //
    const int32_t sectorSize = (cdrom.mode.sectorSize == 0) ? 2048 : 2340;
    const int32_t sectorBytesLeft = (cdrom.dataBufferPointer < sectorSize) ? sectorSize - cdrom.dataBufferPointer : 0;

    const uint8_t* const pSrcBytes = cdrom.dataBuffer.data() + cdrom.dataBufferPointer + dataStart;

    if (readSizeInBytes <= sectorBytesLeft) {
        // Usual case: there is enough data in the cdrom buffer: just do a memcpy and move along the pointer
        std::memcpy(pDstBytes, pSrcBytes, readSizeInBytes);
        cdrom.dataBufferPointer += readSizeInBytes;
    }
    else {
        // Note enough bytes in the FIFO to complete the read, will read what we can then use the repeated byte value for the rest
        std::memcpy(pDstBytes, pSrcBytes, sectorBytesLeft);
        cdrom.dataBufferPointer = sectorSize;

        const std::uint8_t fillByte = (cdrom.mode.sectorSize == 0) ?
            cdrom.dataBuffer[dataStart + sectorSize - 8] :
            cdrom.dataBuffer[dataStart + sectorSize - 4];

        const int32_t bytesToFill = readSizeInBytes - sectorBytesLeft;
        std::memset(pDstBytes + sectorBytesLeft, fillByte, bytesToFill);
    }

    // Set this flag if the CDROM data FIFO has been drained
    if (cdrom.dataBufferPointer >= sectorSize) {
        cdrom.status.dataFifoEmpty = 0;
    }

    // No emulated command was issued for this read, clear the params and reponse FIFOs
    cdrom.CDROM_params.clear();
    cdrom.CDROM_response.clear();

    return true;    // According to the PsyQ docs this always returns '1' or 'true' for success (never fails)
}

void _thunk_LIBCD_CdGetSector() noexcept {
    v0 = LIBCD_CdGetSector(vmAddrToPtr<void>(a0), a1);
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Convert from a CD position in terms of an absolute sector to a binary coded decimal CD position in terms of minutes,
// seconds and sector number. Note that the given sector number is assumed to NOT include the lead in track, so that will be added to the
// returned BCD minute, second and sector number.
//------------------------------------------------------------------------------------------------------------------------------------------
CdlLOC& LIBCD_CdIntToPos(const int32_t sectorNum, CdlLOC& pos) noexcept {
    const int32_t totalSeconds = (sectorNum + CD_LEAD_SECTORS) / CD_SECTORS_PER_SEC;
    const int32_t sector = (sectorNum + CD_LEAD_SECTORS) % CD_SECTORS_PER_SEC;
    const int32_t minute = totalSeconds / 60;
    const int32_t second = totalSeconds % 60;

    // For more about this particular way of doing decimal to BCD conversion, see:
    // https://stackoverflow.com/questions/45655484/bcd-to-decimal-and-decimal-to-bcd
    pos.second = (uint8_t) second + (uint8_t)(second / 10) * 6;
    pos.sector = (uint8_t) sector + (uint8_t)(sector / 10) * 6;
    pos.minute = (uint8_t) minute + (uint8_t)(minute / 10) * 6;
    return pos;
}

void _thunk_LIBCD_CdIntToPos() noexcept {
    v0 = LIBCD_CdIntToPos(a0, *vmAddrToPtr<CdlLOC>(a1));
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Convert from a CD position in terms of seconds, minutes to an absolute sector number.
// Note: the hidden 'lead in' track is discounted from the returned absolute sector number.
//------------------------------------------------------------------------------------------------------------------------------------------
int32_t LIBCD_CdPosToInt(const CdlLOC& pos) noexcept {
    // Convert minute, second and sector counts from BCD to decimal
    const uint32_t minute = (uint32_t)(pos.minute >> 4) * 10 + (pos.minute & 0xF);
    const uint32_t second = (uint32_t)(pos.second >> 4) * 10 + (pos.second & 0xF);
    const uint32_t sector = (uint32_t)(pos.sector >> 4) * 10 + (pos.sector & 0xF);

    // Figure out the absolute sector number and exclude the hidden lead in track which contains the TOC
    return (minute * 60 + second) * CD_SECTORS_PER_SEC + sector - CD_LEAD_SECTORS;
}

void _thunk_LIBCD_CdPosToInt() noexcept {
    v0 = LIBCD_CdPosToInt(*vmAddrToPtr<CdlLOC>(a0));
}

void LIBCD_CD_vol() noexcept {
loc_8005621C:
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74B4);                               // Load from: 800774B4
    v0 = 2;                                             // Result = 00000002
    sb(v0, v1);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74BC);                               // Load from: 800774BC
    v0 = lbu(a0);
    sb(v0, v1);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74C0);                               // Load from: 800774C0
    v0 = lbu(a0 + 0x1);
    sb(v0, v1);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74B4);                               // Load from: 800774B4
    v0 = 3;                                             // Result = 00000003
    sb(v0, v1);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74B8);                               // Load from: 800774B8
    v0 = lbu(a0 + 0x2);
    sb(v0, v1);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74BC);                               // Load from: 800774BC
    v0 = lbu(a0 + 0x3);
    sb(v0, v1);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74C0);                               // Load from: 800774C0
    v0 = 0x20;                                          // Result = 00000020
    sb(v0, v1);
    v0 = 0;                                             // Result = 00000000
    return;
}

void LIBCD_CD_init() noexcept {
    sp -= 0x18;

    a0 = 0x80012168;            // Result = STR_Sys_CD_init_Msg[0] (80012168)
    a1 = 0x800774F8;            // Result = 800774F8
    LIBC2_printf();

    v1 = 0x800774D0;            // Result = 800774D0
    v0 = 9;
    a0 = -1;

    gpLIBCD_CD_cbready = nullptr;
    gpLIBCD_CD_cbsync = nullptr;

    sw(0, 0x80077204);          // Store to: gLIBCD_CD_status (80077204)

    do {
        sw(0, v1);
        v0--;
        v1 += 4;
    } while (v0 != a0);

    v1 = lw(0x800774B4);                // Load from: 800774B4
    v0 = 1;
    sb(v0, v1);
    v0 = lw(0x800774C0);                // Load from: 800774C0
    v0 = lbu(v0);
    v0 &= 7;
    a0 = 1;
    v1 = 7;

    while (v0 != 0) {
        v0 = lw(0x800774B4);            // Load from: 800774B4
        sb(a0, v0);
        v0 = lw(0x800774C0);            // Load from: 800774C0
        sb(v1, v0);
        v0 = lw(0x800774BC);            // Load from: 800774BC
        sb(v1, v0);
        v0 = lw(0x800774C0);            // Load from: 800774C0
        v0 = lbu(v0);
        v0 &= 7;
    }

    sb(0, 0x800774CF);              // Store to: 800774CF
    v1 = lbu(0x800774CF);           // Load from: 800774CF
    v0 = 0x800774CE;                // Result = 800774CE
    sb(v1, v0);                     // Store to: 800774CE
    v1 = lw(0x800774B4);            // Load from: 800774B4
    v0 = 2;
    sb(v0, 0x800774CD);             // Store to: 800774CD
    sb(0, v1);
    v0 = lw(0x800774C0);            // Load from: 800774C0
    sb(0, v0);
    v1 = lw(0x800774C4);            // Load from: 800774C4
    v0 = 0x132C;
    sw(v0, v1);

    v0 = 0;
    sp += 0x18;
}

void LIBCD_CD_initvol() noexcept {
loc_80056664:
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74C8);                               // Load from: 800774C8
    v0 = lhu(v1 + 0x1B8);
    sp -= 8;
    if (v0 != 0) goto loc_800566A0;
    v0 = lhu(v1 + 0x1BA);
    {
        const bool bJump = (v0 != 0);
        v0 = 0x3FFF;                                    // Result = 00003FFF
        if (bJump) goto loc_800566A4;
    }
    sh(v0, v1 + 0x180);
    sh(v0, v1 + 0x182);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74C8);                               // Load from: 800774C8
loc_800566A0:
    v0 = 0x3FFF;                                        // Result = 00003FFF
loc_800566A4:
    sh(v0, v1 + 0x1B0);
    sh(v0, v1 + 0x1B2);
    v0 = 0xC001;                                        // Result = 0000C001
    sh(v0, v1 + 0x1AA);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74B4);                               // Load from: 800774B4
    v0 = 0x80;                                          // Result = 00000080
    sb(v0, sp + 0x2);
    sb(v0, sp);
    v0 = 2;                                             // Result = 00000002
    sb(0, sp + 0x3);
    sb(0, sp + 0x1);
    sb(v0, v1);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74BC);                               // Load from: 800774BC
    v0 = lbu(sp);
    sb(v0, v1);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74C0);                               // Load from: 800774C0
    v0 = lbu(sp + 0x1);
    sb(v0, v1);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74B4);                               // Load from: 800774B4
    v0 = 3;                                             // Result = 00000003
    sb(v0, v1);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74B8);                               // Load from: 800774B8
    v0 = lbu(sp + 0x2);
    sb(v0, v1);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74BC);                               // Load from: 800774BC
    v0 = lbu(sp + 0x3);
    sb(v0, v1);
    v1 = 0x80070000;                                    // Result = 80070000
    v1 = lw(v1 + 0x74C0);                               // Load from: 800774C0
    v0 = 0x20;                                          // Result = 00000020
    sb(v0, v1);
    v0 = 0;                                             // Result = 00000000
    sp += 8;
    return;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Retrieve the table of contents for the disc (tack positions) and return the number of tracks.
// If there is an error then '0' or less will be returned.
// The 0th track points past the last track on the disc.
//------------------------------------------------------------------------------------------------------------------------------------------
int32_t LIBCD_CdGetToc(CdlLOC trackLocs[CdlMAXTOC]) noexcept {    
    // Get the disc: if there is none then we can't provide a TOC
    device::cdrom::CDROM& cdrom = *PsxVm::gpCdrom;
    disc::Disc* const pDisc = cdrom.disc.get();

    if (!pDisc)
        return 0;
    
    // Retrieve the info for each track.
    // Note that if the 'empty' disc is being used then the count will be '0', which is regarded as an error when returned.
    const int32_t numTracks = (int32_t) pDisc->getTrackCount();

    for (int32_t trackNum = 1; trackNum <= numTracks; ++trackNum) {
        const disc::Position discPos = pDisc->getTrackStart(trackNum);
        
        CdlLOC& loc = trackLocs[trackNum];
        loc.minute = bcd::toBcd((uint8_t) discPos.mm);
        loc.second = bcd::toBcd((uint8_t) discPos.ss);
        loc.sector = 0;     // Sector number is not provided by the underlying CD-ROM command to query track location (0x14)
        loc.track = 0;      // Should always be '0' in this PsyQ version - unused
    }

    // The first track entry (0th index) should point to the end of the disk
    const disc::Position discEndPos = pDisc->getDiskSize();

    trackLocs[0].minute = bcd::toBcd((uint8_t) discEndPos.mm);
    trackLocs[0].second = bcd::toBcd((uint8_t) discEndPos.ss);
    trackLocs[0].sector = 0;
    trackLocs[0].track = 0;

    return numTracks;
}
