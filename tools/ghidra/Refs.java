// Lists what refers to the given addresses (script arguments): the referring address and its function.
// Run by tools/ghidra.sh refs ADDR ...
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class Refs extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String arg : getScriptArgs()) {
            Address a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(Long.parseLong(arg, 16));
            for (Reference r : getReferencesTo(a)) {
                Function f = getFunctionContaining(r.getFromAddress());
                System.out.println("//// " + arg + " <- " + r.getFromAddress() + " " + r.getReferenceType() + (f != null ? " in " + f.getName() + " @ " + f.getEntryPoint() : ""));
            }
        }
    }
}
