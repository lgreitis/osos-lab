// SPDX-License-Identifier: GPL-3.0-only

// Export the current program's analysis and its memory image.
// @category OSOS

import com.google.gson.Gson;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.importer.MessageLog;
import ghidra.program.model.address.Address;
import java.io.File;
import java.io.OutputStream;
import java.nio.file.Files;
import java.util.Arrays;
import sarif.SarifProgramOptions;
import sarif.managers.ProgramSarifMgr;

public class ExportAnalysis extends GhidraScript {
  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    File root = new File(args[0]);
    String path = currentProgram.getDomainFile().getPathname().substring(1);
    if (!Arrays.asList(args).subList(1, args.length).contains(path)) {
      println("Skipping unlisted program " + path);
      return;
    }
    File output = new File(root, path + ".sarif");
    output.getParentFile().mkdirs();

    SarifProgramOptions options = new SarifProgramOptions();
    options.setProperties(false);
    options.setTrees(false);

    ProgramSarifMgr exporter = new ProgramSarifMgr(currentProgram, output, new MessageLog());
    println(
        exporter.write(currentProgram, currentProgram.getMemory(), monitor, options).toString());
    writeBoundedMemory(output);
    println("Exported " + path);
  }

  private void writeBoundedMemory(File output) throws Exception {
    // Ghidra 12.1.4's SARIF writer can over-read the last chunk into an
    // adjacent block. Rebuild its byte stream with exact per-block limits.
    JsonObject document =
        JsonParser.parseString(Files.readString(output.toPath())).getAsJsonObject();
    JsonObject run = document.getAsJsonArray("runs").get(0).getAsJsonObject();
    File bytesFile = new File(output + ".bytes");
    long offset = 0;
    try (OutputStream stream = Files.newOutputStream(bytesFile.toPath())) {
      for (JsonElement element : run.getAsJsonArray("results")) {
        JsonObject result = element.getAsJsonObject();
        if (!result.get("ruleId").getAsString().equals("MEMORY_MAP")) continue;
        JsonObject properties =
            result.getAsJsonObject("properties").getAsJsonObject("additionalProperties");
        if (!properties.has("location") || !properties.get("type").getAsString().equals("DEFAULT"))
          continue;
        JsonObject address =
            result
                .getAsJsonArray("locations")
                .get(0)
                .getAsJsonObject()
                .getAsJsonObject("physicalLocation")
                .getAsJsonObject("address");
        long length = address.get("length").getAsLong();
        Address start = toAddr(address.get("absoluteAddress").getAsLong());
        properties.addProperty("location", bytesFile.getName() + ":" + offset);
        for (long position = 0; position < length; ) {
          monitor.checkCancelled();
          byte[] buffer = new byte[(int) Math.min(32768, length - position)];
          if (currentProgram.getMemory().getBytes(start.add(position), buffer) != buffer.length) {
            throw new IllegalStateException("Short memory read at " + start.add(position));
          }
          stream.write(buffer);
          position += buffer.length;
        }
        offset += length;
      }
    }
    Files.writeString(output.toPath(), new Gson().toJson(document));
  }
}
