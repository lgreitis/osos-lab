// SPDX-License-Identifier: GPL-3.0-only

// Export the current program's analysis and its memory image.
// @category OSOS

import ghidra.app.script.GhidraScript;
import ghidra.app.util.importer.MessageLog;
import java.io.File;
import sarif.SarifProgramOptions;
import sarif.managers.ProgramSarifMgr;

public class ExportAnalysis extends GhidraScript {
    @Override
    public void run() throws Exception {
        File root = new File(getScriptArgs()[0]);
        String path = currentProgram.getDomainFile().getPathname().substring(1);
        File output = new File(root, path + ".sarif");
        output.getParentFile().mkdirs();

        SarifProgramOptions options = new SarifProgramOptions();
        options.setProperties(false);
        options.setTrees(false);

        ProgramSarifMgr exporter =
            new ProgramSarifMgr(currentProgram, output, new MessageLog());
        println(
            exporter.write(currentProgram, currentProgram.getMemory(), monitor, options)
                .toString());
        println("Exported " + path);
    }
}
