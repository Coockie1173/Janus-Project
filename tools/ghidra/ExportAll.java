// Writes what a Ghidra program knows as text, for tools that are not Ghidra:
//   functions.tsv   addr, size, name, source, signature, comment
//   symbols.tsv     addr, name, source, type     (labels that are not functions, not default names)
//   structs.txt     every structure and union: its fields
//   enums.txt       every enumeration
//   comments.tsv    addr, kind, text
// Run by tools/ghidra.sh (headless), with the output folder as its argument.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;
import java.util.Iterator;

public class ExportAll extends GhidraScript {
    private static String clean(String s) {
        return s == null ? "" : s.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ');
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String out = args.length > 0 ? args[0] : ".";
        Listing listing = currentProgram.getListing();
        int n = 0;
        try (PrintWriter w = new PrintWriter(out + "/functions.tsv", "UTF-8")) {
            w.println("addr\tsize\tname\tsource\tsignature\tcomment");
            for (Function f : listing.getFunctions(true)) {
                if (f.isExternal()) continue;
                w.printf("%08X\t%d\t%s\t%s\t%s\t%s%n", f.getEntryPoint().getOffset(), f.getBody().getNumAddresses(),
                         clean(f.getName()), f.getSymbol().getSource(), clean(f.getPrototypeString(false, false)),
                         clean(f.getComment()));
                ++n;
            }
        }
        println("functions: " + n);
        n = 0;
        try (PrintWriter w = new PrintWriter(out + "/symbols.tsv", "UTF-8")) {
            w.println("addr\tname\tsource\ttype");
            SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
            while (it.hasNext()) {
                Symbol s = it.next();
                if (s.getSource() == SourceType.DEFAULT || s.getSymbolType() == SymbolType.FUNCTION) continue;
                if (!s.getAddress().isMemoryAddress()) continue;
                Data d = listing.getDataAt(s.getAddress());
                w.printf("%08X\t%s\t%s\t%s%n", s.getAddress().getOffset(), clean(s.getName(true)), s.getSource(),
                         d != null ? clean(d.getDataType().getName()) : s.getSymbolType().toString());
                ++n;
            }
        }
        println("symbols: " + n);
        int ns = 0, ne = 0;
        try (PrintWriter w = new PrintWriter(out + "/structs.txt", "UTF-8");
             PrintWriter e = new PrintWriter(out + "/enums.txt", "UTF-8")) {
            Iterator<DataType> it = currentProgram.getDataTypeManager().getAllDataTypes();
            while (it.hasNext()) {
                DataType t = it.next();
                if (t instanceof Composite) {
                    Composite c = (Composite) t;
                    w.printf("%s %s  // %s, size 0x%X%n", t instanceof Union ? "union" : "struct", c.getName(),
                             c.getCategoryPath(), c.getLength());
                    for (DataTypeComponent m : c.getDefinedComponents())
                        w.printf("  +0x%04X  %-28s %s%s%n", m.getOffset(), clean(m.getDataType().getName()),
                                 m.getFieldName() == null ? "(unnamed)" : m.getFieldName(),
                                 m.getComment() == null ? "" : "  // " + clean(m.getComment()));
                    w.println();
                    ++ns;
                } else if (t instanceof ghidra.program.model.data.Enum) {
                    ghidra.program.model.data.Enum en = (ghidra.program.model.data.Enum) t;
                    e.printf("enum %s  // %s, size %d%n", en.getName(), en.getCategoryPath(), en.getLength());
                    for (String name : en.getNames()) e.printf("  %s = 0x%X%n", name, en.getValue(name));
                    e.println();
                    ++ne;
                }
            }
        }
        println("structs: " + ns + ", enums: " + ne);
        n = 0;
        try (PrintWriter w = new PrintWriter(out + "/comments.tsv", "UTF-8")) {
            w.println("addr\tkind\ttext");
            int[] kinds = {CodeUnit.PLATE_COMMENT, CodeUnit.PRE_COMMENT, CodeUnit.EOL_COMMENT, CodeUnit.POST_COMMENT,
                           CodeUnit.REPEATABLE_COMMENT};
            String[] names = {"plate", "pre", "eol", "post", "repeatable"};
            for (int k = 0; k < kinds.length; ++k) {
                for (Address a : listing.getCommentAddressIterator(kinds[k], currentProgram.getMemory(), true)) {
                    w.printf("%08X\t%s\t%s%n", a.getOffset(), names[k], clean(listing.getComment(kinds[k], a)));
                    ++n;
                }
            }
        }
        println("comments: " + n);
    }
}
