// Decompiles the functions at the given addresses or names (script arguments) to standard output.
// Run by tools/ghidra.sh decompile ADDR|NAME ...
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.util.List;

public class Decompile extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        for (String arg : getScriptArgs()) {
            Function f = null;
            try {
                Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(Long.parseLong(arg, 16));
                f = getFunctionContaining(a);
            } catch (NumberFormatException e) {
                List<Function> named = getGlobalFunctions(arg);
                if (!named.isEmpty()) f = named.get(0);
            }
            if (f == null) { println("//// no function at " + arg); continue; }
            DecompileResults r = d.decompileFunction(f, 120, monitor);
            System.out.println("//// " + f.getName() + " @ " + f.getEntryPoint() + "  (" + arg + ")");
            System.out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed: " + r.getErrorMessage());
        }
    }
}
