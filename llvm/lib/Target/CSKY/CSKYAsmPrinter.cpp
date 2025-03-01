//===-- CSKYAsmPrinter.cpp - CSKY LLVM assembly writer --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to the CSKY assembly language.
//
//===----------------------------------------------------------------------===//
#include "CSKYAsmPrinter.h"
#include "CSKY.h"
#include "CSKYConstantPoolValue.h"
#include "CSKYTargetMachine.h"
#include "MCTargetDesc/CSKYInstPrinter.h"
#include "MCTargetDesc/CSKYMCExpr.h"
#include "TargetInfo/CSKYTargetInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "csky-asm-printer"

STATISTIC(CSKYNumInstrsCompressed,
          "Number of C-SKY Compressed instructions emitted");

CSKYAsmPrinter::CSKYAsmPrinter(llvm::TargetMachine &TM,
                               std::unique_ptr<llvm::MCStreamer> Streamer)
    : AsmPrinter(TM, std::move(Streamer)), MCInstLowering(OutContext, *this) {}

bool CSKYAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &MF.getSubtarget<CSKYSubtarget>();
  return AsmPrinter::runOnMachineFunction(MF);
}

#define GEN_COMPRESS_INSTR
#include "CSKYGenCompressInstEmitter.inc"
void CSKYAsmPrinter::EmitToStreamer(MCStreamer &S, const MCInst &Inst) {
  MCInst CInst;
  bool Res = compressInst(CInst, Inst, *Subtarget, OutStreamer->getContext());
  if (Res)
    ++CSKYNumInstrsCompressed;
  AsmPrinter::EmitToStreamer(*OutStreamer, Res ? CInst : Inst);
}

// Simple pseudo-instructions have their lowering (with expansion to real
// instructions) auto-generated.
#include "CSKYGenMCPseudoLowering.inc"

void CSKYAsmPrinter::emitInstruction(const MachineInstr *MI) {
  // Do any auto-generated pseudo lowerings.
  if (emitPseudoExpansionLowering(*OutStreamer, MI))
    return;

  MCInst TmpInst;
  MCInstLowering.Lower(MI, TmpInst);
  EmitToStreamer(*OutStreamer, TmpInst);
}

// Convert a CSKY-specific constant pool modifier into the associated
// MCSymbolRefExpr variant kind.
static CSKYMCExpr::VariantKind
getModifierVariantKind(CSKYCP::CSKYCPModifier Modifier) {
  switch (Modifier) {
  case CSKYCP::NO_MOD:
    return CSKYMCExpr::VK_CSKY_None;
  case CSKYCP::ADDR:
    return CSKYMCExpr::VK_CSKY_ADDR;
  case CSKYCP::GOT:
    return CSKYMCExpr::VK_CSKY_GOT;
  case CSKYCP::GOTOFF:
    return CSKYMCExpr::VK_CSKY_GOTOFF;
  case CSKYCP::PLT:
    return CSKYMCExpr::VK_CSKY_PLT;
  case CSKYCP::TLSGD:
    return CSKYMCExpr::VK_CSKY_TLSGD;
  case CSKYCP::TLSLE:
    return CSKYMCExpr::VK_CSKY_TLSLE;
  case CSKYCP::TLSIE:
    return CSKYMCExpr::VK_CSKY_TLSIE;
  }
  llvm_unreachable("Invalid CSKYCPModifier!");
}

void CSKYAsmPrinter::emitMachineConstantPoolValue(
    MachineConstantPoolValue *MCPV) {
  int Size = getDataLayout().getTypeAllocSize(MCPV->getType());
  CSKYConstantPoolValue *CCPV = static_cast<CSKYConstantPoolValue *>(MCPV);
  MCSymbol *MCSym;

  if (CCPV->isBlockAddress()) {
    const BlockAddress *BA =
        cast<CSKYConstantPoolConstant>(CCPV)->getBlockAddress();
    MCSym = GetBlockAddressSymbol(BA);
  } else if (CCPV->isGlobalValue()) {
    const GlobalValue *GV = cast<CSKYConstantPoolConstant>(CCPV)->getGV();
    MCSym = getSymbol(GV);
  } else if (CCPV->isMachineBasicBlock()) {
    const MachineBasicBlock *MBB = cast<CSKYConstantPoolMBB>(CCPV)->getMBB();
    MCSym = MBB->getSymbol();
  } else if (CCPV->isJT()) {
    signed JTI = cast<CSKYConstantPoolJT>(CCPV)->getJTI();
    MCSym = GetJTISymbol(JTI);
  } else {
    assert(CCPV->isExtSymbol() && "unrecognized constant pool value");
    StringRef Sym = cast<CSKYConstantPoolSymbol>(CCPV)->getSymbol();
    MCSym = GetExternalSymbolSymbol(Sym);
  }
  // Create an MCSymbol for the reference.
  const MCExpr *Expr =
      MCSymbolRefExpr::create(MCSym, MCSymbolRefExpr::VK_None, OutContext);

  if (CCPV->getPCAdjustment()) {

    MCSymbol *PCLabel = OutContext.getOrCreateSymbol(
        Twine(MAI->getPrivateGlobalPrefix()) + "PC" +
        Twine(getFunctionNumber()) + "_" + Twine(CCPV->getLabelID()));

    const MCExpr *PCRelExpr = MCSymbolRefExpr::create(PCLabel, OutContext);
    if (CCPV->mustAddCurrentAddress()) {
      // We want "(<expr> - .)", but MC doesn't have a concept of the '.'
      // label, so just emit a local label end reference that instead.
      MCSymbol *DotSym = OutContext.createTempSymbol();
      OutStreamer->emitLabel(DotSym);
      const MCExpr *DotExpr = MCSymbolRefExpr::create(DotSym, OutContext);
      PCRelExpr = MCBinaryExpr::createSub(PCRelExpr, DotExpr, OutContext);
    }
    Expr = MCBinaryExpr::createSub(Expr, PCRelExpr, OutContext);
  }

  // Create an MCSymbol for the reference.
  Expr = CSKYMCExpr::create(Expr, getModifierVariantKind(CCPV->getModifier()),
                            OutContext);

  OutStreamer->emitValue(Expr, Size);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeCSKYAsmPrinter() {
  RegisterAsmPrinter<CSKYAsmPrinter> X(getTheCSKYTarget());
}
