#include "PseudoCppPrinter_InstPrint.h"

#include "CpuGpr.h"
#include "CpuInstruction.h"
#include "PseudoCppPrinter.h"
#include <cstdlib>

using namespace PseudoCppPrinter;

//------------------------------------------------------------------------------------------------------------------------------------------
// Print in either hex or decimal, depending on how big the number is.
// The number is printed without padding.
//------------------------------------------------------------------------------------------------------------------------------------------
static void printHexOrDecInt32Literal(const int32_t val, std::ostream& out) {
    if (std::abs(val) < 10) {
        out << val;
    } else {
        printHexCppInt32Literal(val, false, out);
    }
}

void PseudoCppPrinter::printInst_addiu(std::ostream& out, const CpuInstruction& inst) {
    const int16_t i16 = (int16_t) inst.immediateVal;
    const int32_t i32 = i16;

    out << getGprCppMacroName(inst.regT);

    if (i32 == 0) {
        // If the immediate is zero, then it is basically a move or assign
        out << " = ";

        if (inst.regS == CpuGpr::ZERO) {
            out << "0";
        } else {
            out << getGprCppMacroName(inst.regS);
        }
    }
    else if (inst.regS == CpuGpr::ZERO) {
        // If the register is zero then we are just assigning an integer literal
        out << " = ";
        printHexOrDecInt32Literal(i32, out);
    }
    else if (inst.regT == inst.regS) {
        // If the source and dest reg are the same then we can use one of '+=', '-=', '++' or '--':
        if (i32 < 0) {
            if (i32 == -1) {
                out << "--";
            } else {
                out << " -= ";
                printHexOrDecInt32Literal(-i32, out);
            }
        } else {
            if (i32 == 1) {
                out << "++";
            } else {
                out << " += ";
                printHexOrDecInt32Literal(i32, out);
            }
        }
    } else {
        // Regular add or subtract
        if (i32 < 0) {
            out << " = ";
            out << getGprCppMacroName(inst.regS);
            out << " - ";
            printHexOrDecInt32Literal(-i32, out);
        } else {
            out << " = ";
            out << getGprCppMacroName(inst.regS);
            out << " + ";
            printHexOrDecInt32Literal(i32, out);
        }
    }
}

void PseudoCppPrinter::printInst_addu(std::ostream& out, const CpuInstruction& inst) {
    out << getGprCppMacroName(inst.regD);

    if (inst.regS == CpuGpr::ZERO && inst.regT == CpuGpr::ZERO) {
        // Zero assign
        out << " = 0";
    }
    else if (inst.regS == CpuGpr::ZERO) {
        // Move instruction
        out << " = ";
        out << getGprCppMacroName(inst.regT);
    }
    else if (inst.regT == CpuGpr::ZERO) {
        // Move instruction
        out << " = ";
        out << getGprCppMacroName(inst.regS);
    }
    else if (inst.regS == inst.regD) {
        // One source reg same as dest: can use '+=' shorthand
        out << " += ";
        out << getGprCppMacroName(inst.regT);
    }
    else if (inst.regT == inst.regD) {
        // One source reg same as dest: can use '+=' shorthand
        out << " += ";
        out << getGprCppMacroName(inst.regS);
    }
    else {
        // Regular add
        out << " = ";
        out << getGprCppMacroName(inst.regS);
        out << " + ";
        out << getGprCppMacroName(inst.regT);
    }
}
