// SPDX-License-Identifier: GPL-3.0-only
// Restore SARIF import gaps after all referenced types and functions exist.
// @category OSOS

import com.google.gson.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.disassemble.Disassembler;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
import java.util.*;

public class ImportAnalysis extends GhidraScript {
  @Override
  public void run() throws Exception {
    String name =
        currentProgram.getName().replaceFirst("\\.sarif$", "").replaceFirst("\\.[^.]+$", "");
    Path directory = Path.of(getScriptArgs()[0], name);
    if (!Files.isDirectory(directory))
      throw new IllegalStateException("Missing prepared analysis: " + directory);
    currentProgram
        .getOptions(Program.DISASSEMBLER_PROPERTIES)
        .setBoolean(Disassembler.RESTRICT_DISASSEMBLY_TO_EXECUTE_MEMORY_PROPERTY, true);
    restoreTypes(read(directory, "datatype"));
    List<JsonObject> functions = read(directory, "functions");
    restoreFunctions(functions);
    restoreComments(read(directory, "comments"));
    verifyFunctions(functions);
    verifyCode(read(directory, "code"));
    restoreBookmarks(read(directory, "bookmarks"));
    currentProgram.setName(currentProgram.getName().replaceFirst("\\.sarif$", ""));
    println("Analysis imported: " + currentProgram.getName());
  }

  private List<JsonObject> read(Path directory, String name) throws Exception {
    Path file = directory.resolve(name + ".jsonl");
    List<JsonObject> result = new ArrayList<>();
    if (Files.exists(file)) {
      for (String line : Files.readAllLines(file)) {
        if (!line.isBlank()) result.add(JsonParser.parseString(line).getAsJsonObject());
      }
    }
    return result;
  }

  private JsonObject properties(JsonObject row) {
    return row.getAsJsonObject("properties").getAsJsonObject("additionalProperties");
  }

  private Address location(JsonObject row) {
    return toAddr(
        row.getAsJsonArray("locations")
            .get(0)
            .getAsJsonObject()
            .getAsJsonObject("physicalLocation")
            .getAsJsonObject("address")
            .get("absoluteAddress")
            .getAsLong());
  }

  private DataType type(JsonObject value) throws Exception {
    DataTypeManager manager = currentProgram.getDataTypeManager();
    String name = value.get("name").getAsString();
    String path = value.has("location") ? value.get("location").getAsString() : "/";
    DataType existing = manager.getDataType(new DataTypePath(path, name));
    if (existing != null) return existing;
    String kind = value.get("kind").getAsString();
    if (kind.equals("pointer") && value.has("subtype")) {
      return new PointerDataType(type(value.getAsJsonObject("subtype")), 4, manager);
    }
    if (kind.equals("array")) {
      DataType element = type(value.getAsJsonObject("subtype"));
      return new ArrayDataType(
          element, value.get("count").getAsInt(), element.getLength(), manager);
    }
    throw new IllegalStateException("Unresolved datatype " + path + "/" + name);
  }

  private void restoreTypes(List<JsonObject> rows) throws Exception {
    for (JsonObject row : rows) {
      JsonObject p = properties(row);
      if (!p.has("fields")
          || !p.has("kind")
          || !p.has("name")
          || !Set.of("struct", "union").contains(p.get("kind").getAsString())) continue;
      DataType target = type(p);
      List<JsonObject> fields = new ArrayList<>();
      for (JsonElement element : p.getAsJsonObject("fields").asMap().values())
        fields.add(element.getAsJsonObject());
      fields.sort(Comparator.comparingInt(f -> f.get("ordinal").getAsInt()));
      if (target instanceof Union union) {
        while (union.getNumComponents() > 0) union.delete(0);
      }
      for (JsonObject field : fields) {
        DataType member = type(field.getAsJsonObject("type"));
        int length = field.get("length").getAsInt();
        String name = field.get("field_name").getAsString();
        String comment = field.has("comment") ? field.get("comment").getAsString() : null;
        if (target instanceof Structure) {
          ((Structure) target)
              .replaceAtOffset(field.get("offset").getAsInt(), member, length, name, comment);
        } else if (target instanceof Union) {
          ((Union) target).add(member, length, name, comment);
        }
      }
    }
  }

  private VariableStorage storage(JsonObject parameter) throws Exception {
    if (parameter.has("registers")) {
      List<Register> registers = new ArrayList<>();
      for (JsonElement name : parameter.getAsJsonArray("registers")) {
        registers.add(currentProgram.getRegister(name.getAsString()));
      }
      return new VariableStorage(currentProgram, registers.toArray(new Register[0]));
    }
    return new VariableStorage(
        currentProgram, parameter.get("stackOffset").getAsInt(), parameter.get("size").getAsInt());
  }

  private void restoreSignature(Function function, JsonObject p) throws Exception {
    boolean custom = p.get("hasCustomStorage").getAsBoolean();
    List<Variable> parameters = new ArrayList<>();
    for (JsonElement element : p.getAsJsonArray("params")) {
      JsonObject parameter = element.getAsJsonObject();
      parameters.add(
          new ParameterImpl(
              parameter.get("name").getAsString(),
              type(parameter.getAsJsonObject("type")),
              custom ? storage(parameter) : VariableStorage.UNASSIGNED_STORAGE,
              currentProgram));
    }
    JsonObject result = p.getAsJsonObject("ret");
    Variable returned =
        new ReturnParameterImpl(
            type(result.getAsJsonObject("type")),
            custom ? storage(result) : VariableStorage.UNASSIGNED_STORAGE,
            currentProgram);
    function.updateFunction(
        p.get("callingConvention").getAsString(),
        returned,
        parameters,
        custom
            ? Function.FunctionUpdateType.CUSTOM_STORAGE
            : Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,
        true,
        SourceType.USER_DEFINED);
  }

  private void restoreFunctions(List<JsonObject> rows) throws Exception {
    for (JsonObject row : rows) {
      JsonObject p = properties(row);
      Function function = getFunctionAt(toAddr(p.get("location").getAsString()));
      if (function == null)
        throw new IllegalStateException("Missing saved function " + p.get("location"));
      if (p.has("callFixup")) function.setCallFixup(p.get("callFixup").getAsString());
      boolean internalThunk =
          p.has("thunkAddress") && p.get("thunkAddress").getAsString().matches("[0-9a-fA-F]{8}");
      if (internalThunk) {
        Address targetAddress = toAddr(p.get("thunkAddress").getAsString());
        Function target = getFunctionAt(targetAddress);
        if (target == null
            && p.get("sourceType").getAsString().equals("USER_DEFINED")
            && currentProgram.getMemory().getExecuteSet().contains(targetAddress)) {
          disassemble(targetAddress);
          target = createFunction(targetAddress, null);
          if (target == null)
            throw new IllegalStateException("Missing thunk target " + targetAddress);
        }
        if (target != null) function.setThunkedFunction(target);
      }
      if (p.get("hasCustomStorage").getAsBoolean()
          || (internalThunk && p.get("signatureSource").getAsString().equals("USER_DEFINED"))) {
        restoreSignature(function, p);
      }
      if (p.get("sourceType").getAsString().equals("USER_DEFINED")) {
        function.setName(p.get("name").getAsString(), SourceType.USER_DEFINED);
      }
      if (p.get("isStackPurgeSizeValid").getAsBoolean())
        function.setStackPurgeSize(p.getAsJsonObject("stack").get("purgeSize").getAsInt());
    }
  }

  private void verifyFunctions(List<JsonObject> rows) {
    for (JsonObject row : rows) {
      JsonObject p = properties(row);
      Function function = getFunctionAt(toAddr(p.get("location").getAsString()));
      if (function == null || !function.getName().equals(p.get("name").getAsString()))
        throw new IllegalStateException("Function name differs: " + p.get("location"));
      String fixup = p.has("callFixup") ? p.get("callFixup").getAsString() : null;
      String comment = p.has("comment") ? p.get("comment").getAsString() : null;
      if (!Objects.equals(fixup, function.getCallFixup())
          || !Objects.equals(comment, function.getComment()))
        throw new IllegalStateException("Function annotations differ: " + function.getName());
      AddressSet body = ranges(List.of(row));
      if (!body.equals(function.getBody()))
        throw new IllegalStateException("Function body differs: " + function.getName());
    }
    if (rows.size() != currentProgram.getFunctionManager().getFunctionCount())
      throw new IllegalStateException("Function count differs");
  }

  private AddressSet ranges(List<JsonObject> rows) {
    AddressSet addresses = new AddressSet();
    for (JsonObject row : rows) {
      for (JsonElement element : row.getAsJsonArray("locations")) {
        var address =
            element
                .getAsJsonObject()
                .getAsJsonObject("physicalLocation")
                .getAsJsonObject("address");
        var start = toAddr(address.get("absoluteAddress").getAsLong());
        addresses.add(start, start.add(address.get("length").getAsLong() - 1));
      }
    }
    return addresses;
  }

  private void verifyCode(List<JsonObject> rows) {
    AddressSet actual = new AddressSet();
    for (Instruction instruction : currentProgram.getListing().getInstructions(true))
      actual.add(instruction.getMinAddress(), instruction.getMaxAddress());
    if (!ranges(rows).equals(actual))
      throw new IllegalStateException("Disassembled ranges differ from saved analysis");
  }

  private void restoreBookmarks(List<JsonObject> rows) {
    // SARIF disassembly adds diagnostics even when the saved code ranges match.
    // Restore the saved bookmarks only after verifying the reconstructed listing.
    var manager = currentProgram.getBookmarkManager();
    List<Bookmark> existing = new ArrayList<>();
    manager.getBookmarksIterator().forEachRemaining(existing::add);
    for (Bookmark bookmark : existing) manager.removeBookmark(bookmark);
    for (JsonObject row : rows) {
      JsonObject p = properties(row);
      manager.setBookmark(
          location(row),
          p.get("kind").getAsString(),
          p.get("name").getAsString(),
          p.get("comment").getAsString());
    }
  }

  private void restoreComments(List<JsonObject> rows) {
    Map<String, CommentType> kinds =
        Map.of(
            "plate",
            CommentType.PLATE,
            "pre",
            CommentType.PRE,
            "post",
            CommentType.POST,
            "end-of-line",
            CommentType.EOL,
            "repeatable",
            CommentType.REPEATABLE);
    for (JsonObject row : rows) {
      JsonObject p = properties(row);
      CommentType kind = kinds.get(p.get("kind").getAsString());
      if (kind == null) throw new IllegalStateException("Unknown comment kind " + p.get("kind"));
      currentProgram.getListing().setComment(location(row), kind, p.get("value").getAsString());
    }
  }
}
