// SPDX-License-Identifier: GPL-3.0-only
// Initialize mapped firmware before running auto-analysis.
// @category OSOS

import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.disassemble.Disassembler;
import ghidra.program.model.listing.Program;
import java.math.BigInteger;
import java.nio.file.*;
import java.util.HashSet;
import java.util.Set;

public class AnalyzePrepared extends GhidraScript {
  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    String path = args[1] + "/" + currentProgram.getName();
    JsonObject manifest =
        JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject();
    if (!currentProgram.getLanguageID().toString().equals(manifest.get("language").getAsString()))
      throw new IllegalStateException("Processor language differs from manifest");
    JsonObject spec = findProgram(manifest, path);
    normalizeMemory(spec);
    currentProgram
        .getOptions(Program.DISASSEMBLER_PROPERTIES)
        .setBoolean(Disassembler.RESTRICT_DISASSEMBLY_TO_EXECUTE_MEMORY_PROPERTY, true);
    setInstructionModes(spec);
    for (JsonElement element : spec.getAsJsonArray("entry_points")) {
      var address = toAddr(element.getAsJsonObject().get("address").getAsLong());
      if (!disassemble(address) && getInstructionAt(address) == null)
        throw new IllegalStateException("Cannot disassemble entry " + address);
      if (getFunctionAt(address) == null && createFunction(address, null) == null)
        throw new IllegalStateException("Cannot create entry function " + address);
      currentProgram.getSymbolTable().addExternalEntryPoint(address);
    }
    analyzeAll(currentProgram);
    println("PREPARED " + path);
  }

  private JsonObject findProgram(JsonObject manifest, String path) {
    for (JsonElement element : manifest.getAsJsonArray("programs")) {
      var spec = element.getAsJsonObject();
      if (spec.get("path").getAsString().equals(path)) return spec;
    }
    throw new IllegalStateException("Unlisted program " + path);
  }

  private void normalizeMemory(JsonObject spec) throws Exception {
    var memory = currentProgram.getMemory();
    Set<String> names = new HashSet<>();
    for (JsonElement element : spec.getAsJsonArray("regions"))
      names.add(element.getAsJsonObject().get("name").getAsString());
    for (var block : memory.getBlocks()) {
      if (!names.contains(block.getName())) {
        if (!block.getStart().getAddressSpace().isOverlaySpace())
          throw new IllegalStateException("Unexpected physical block " + block.getName());
        memory.removeBlock(block, monitor);
      }
    }
    for (JsonElement element : spec.getAsJsonArray("regions")) {
      var region = element.getAsJsonObject();
      var block = memory.getBlock(region.get("name").getAsString());
      if (block == null) throw new IllegalStateException("Missing memory region");
      int flags = region.get("flags").getAsInt();
      block.setRead((flags & 4) != 0);
      block.setWrite((flags & 2) != 0);
      block.setExecute((flags & 1) != 0);
    }
  }

  private void setInstructionModes(JsonObject spec) throws Exception {
    Set<String> thumb = new HashSet<>();
    for (JsonElement element : spec.getAsJsonArray("thumb_regions"))
      thumb.add(element.getAsString());
    var mode = currentProgram.getRegister("TMode");
    for (var block : currentProgram.getMemory().getBlocks()) {
      if (block.isExecute() && thumb.contains(block.getName()))
        currentProgram
            .getProgramContext()
            .setValue(mode, block.getStart(), block.getEnd(), BigInteger.ONE);
    }
    for (JsonElement element : spec.getAsJsonArray("entry_points")) {
      var entry = element.getAsJsonObject();
      var address = toAddr(entry.get("address").getAsLong());
      currentProgram
          .getProgramContext()
          .setValue(
              mode,
              address,
              address,
              entry.get("thumb").getAsBoolean() ? BigInteger.ONE : BigInteger.ZERO);
    }
  }
}
