// SPDX-License-Identifier: GPL-3.0-only
// Verify firmware mappings, NOR analysis and representative OSOS functions.
// @category OSOS

import com.google.gson.*;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import java.nio.file.*;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HexFormat;

public class VerifyAnalysis extends GhidraScript {
  @Override
  public void run() throws Exception {
    String path = currentProgram.getDomainFile().getPathname().substring(1);
    JsonObject manifest =
        JsonParser.parseString(Files.readString(Path.of(getScriptArgs()[0]))).getAsJsonObject();
    JsonObject spec = null;
    for (JsonElement element : manifest.getAsJsonArray("programs")) {
      JsonObject candidate = element.getAsJsonObject();
      if (candidate.get("path").getAsString().equals(path)) spec = candidate;
    }
    if (spec == null) throw new IllegalStateException("Unlisted program " + path);
    if (!currentProgram.getName().equals(currentProgram.getDomainFile().getName()))
      throw new IllegalStateException("Program name differs from project filename: " + path);
    verifyMemory(spec);
    boolean nor = path.contains("/NOR/");
    if (nor) verifyNorCode();
    long[] samples =
        path.equals("2.0.4/osos.elf")
            ? new long[] {0x0817298cL, 0x0826d8e0L, 0x220071b0L, 0x220046e0L}
            : new long[] {spec.get("entry").getAsLong() & ~1L};
    if (spec.has("verify_functions")) {
      samples =
          spec.getAsJsonArray("verify_functions").asList().stream()
              .mapToLong(JsonElement::getAsLong)
              .toArray();
    }
    if (nor) {
      ArrayList<Long> entries = new ArrayList<>();
      for (long address : samples) {
        if (getFunctionAt(toAddr(address)) == null)
          throw new IllegalStateException(
              "Missing NOR entry function " + Long.toHexString(address));
      }
      for (Function function : currentProgram.getFunctionManager().getFunctions(true))
        entries.add(function.getEntryPoint().getOffset());
      samples = entries.stream().mapToLong(Long::longValue).toArray();
      if (samples.length == 0) throw new IllegalStateException("No NOR functions");
    }
    DecompInterface decompiler = new DecompInterface();
    try {
      if (!decompiler.openProgram(currentProgram))
        throw new IllegalStateException(decompiler.getLastMessage());
      for (long address : samples) verifyFunction(decompiler, address, nor);
    } finally {
      decompiler.dispose();
    }
    println("VERIFIED " + path + ": memory regions and " + samples.length + " decompilations");
  }

  private void verifyNorCode() throws Exception {
    for (var instruction : currentProgram.getListing().getInstructions(true)) {
      if (!currentProgram.getMemory().getBlock(instruction.getAddress()).isExecute())
        throw new IllegalStateException(
            "Instruction outside executable memory: " + instruction.getAddress());
      for (var reference : instruction.getReferencesFrom()) {
        var target = reference.getToAddress();
        if (reference.getReferenceType().isCall()
            && currentProgram.getMemory().getExecuteSet().contains(target)
            && getFunctionAt(target) == null)
          throw new IllegalStateException("Missing called function at " + target);
      }
    }
    var errors = currentProgram.getBookmarkManager().getBookmarksIterator("Error");
    if (errors.hasNext()) {
      var error = errors.next();
      throw new IllegalStateException(
          "Analysis error at " + error.getAddress() + ": " + error.getComment());
    }
  }

  private void verifyMemory(JsonObject spec) throws Exception {
    if (currentProgram.getMemory().getBlocks().length != spec.getAsJsonArray("regions").size()) {
      throw new IllegalStateException("Unexpected memory blocks");
    }
    for (JsonElement element : spec.getAsJsonArray("regions")) {
      JsonObject region = element.getAsJsonObject();
      MemoryBlock block = currentProgram.getMemory().getBlock(region.get("name").getAsString());
      int flags = region.get("flags").getAsInt();
      long initialized = region.get("initialized").getAsLong();
      if (block == null
          || block.getStart().getOffset() != region.get("address").getAsLong()
          || block.getSize() != region.get("size").getAsLong()
          || block.isInitialized() != (initialized != 0)
          || block.isRead() != ((flags & 4) != 0)
          || block.isWrite() != ((flags & 2) != 0)
          || block.isExecute() != ((flags & 1) != 0)) {
        throw new IllegalStateException("Incorrect region " + region.get("name"));
      }
      MessageDigest digest = MessageDigest.getInstance("SHA-256");
      for (long offset = 0; offset < initialized; ) {
        byte[] bytes = new byte[(int) Math.min(32768, initialized - offset)];
        if (block.getBytes(block.getStart().add(offset), bytes) != bytes.length)
          throw new IllegalStateException("Short memory read");
        digest.update(bytes);
        offset += bytes.length;
      }
      if (!HexFormat.of().formatHex(digest.digest()).equals(region.get("sha256").getAsString())) {
        throw new IllegalStateException("Memory digest mismatch: " + block.getName());
      }
    }
  }

  private void verifyFunction(DecompInterface decompiler, long address, boolean strict)
      throws Exception {
    Function function = getFunctionAt(toAddr(address));
    if (function == null)
      throw new IllegalStateException("Missing function at " + Long.toHexString(address));
    if (strict && getInstructionAt(function.getEntryPoint()) == null)
      throw new IllegalStateException("Function is not disassembled: " + function.getName());
    DecompileResults result = decompiler.decompileFunction(function, 30, monitor);
    if (!result.decompileCompleted() || result.getDecompiledFunction() == null) {
      throw new IllegalStateException(
          "Decompilation failed: " + function.getName() + ": " + result.getErrorMessage());
    }
    String code = result.getDecompiledFunction().getC();
    if (strict && code.contains("halt_baddata"))
      throw new IllegalStateException("Truncated control flow: " + function.getName());
  }
}
