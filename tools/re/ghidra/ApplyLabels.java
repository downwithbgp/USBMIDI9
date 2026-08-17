// ApplyLabels.java — reapply the docs/re/ghidra-functions.csv labels to a
// fresh Ghidra import, so the RE names do not depend on the opaque local
// Ghidra project database.
//
// Input: docs/re/ghidra-functions.csv, one row per line:
//   <address-hex>,<label>[,<comment>]
//   address-hex = flat program address in the Ghidra image (e.g. the
//   68K blob imported at base 0, or the PEF at its image base).
//
// Usage (headless):
//   $GHIDRA/support/analyzeHeadless <proj> <name> -import <binary> \
//       -scriptPath tools/re/ghidra -postScript ApplyLabels.java \
//       -scriptPath <repo>/docs/re
// (the CSV is opened relative to the repo root; pass -scriptPath for it
// or edit CSV_PATH below).
//
// NOTE: not run in the host CI (requires Ghidra + a real import). The CSV
// is the durable artifact; this script is a convenience re-application
// helper. Modeled on the session's ListSymbols.java (Ghidra 12.1).
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.SourceType;

public class ApplyLabels extends GhidraScript {
    // Edit if the repo is not the CWD when the script runs.
    private static final String CSV_PATH = "docs/re/ghidra-functions.csv";

    @Override
    public void run() throws Exception {
        File csv = new File(CSV_PATH);
        if (!csv.isFile()) {
            csv = new File(getScriptArgs().length > 0 ? getScriptArgs()[0] : CSV_PATH);
        }
        if (!csv.isFile()) {
            println("ApplyLabels: cannot open " + csv.getAbsolutePath());
            return;
        }
        int applied = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(csv))) {
            String line;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty() || line.startsWith("#")) {
                    continue;
                }
                String[] parts = line.split(",", 3);
                if (parts.length < 2) {
                    continue;
                }
                Address addr = toAddr(Long.parseLong(parts[0].trim(), 16));
                if (addr == null) {
                    println("ApplyLabels: bad address " + parts[0]);
                    continue;
                }
                String label = parts[1].trim();
                if (getSymbolTable().getSymbol(label, addr) == null) {
                    createLabel(addr, label, true, SourceType.USER_DEFINED);
                    applied++;
                }
                if (parts.length > 2 && !parts[2].trim().isEmpty()) {
                    setPlateComment(addr, parts[2].trim());
                }
            }
        }
        println("ApplyLabels: applied " + applied + " labels from " + csv.getName());
    }
}
