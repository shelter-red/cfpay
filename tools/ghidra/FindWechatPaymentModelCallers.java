// Finds direct callers of Weixin payment model creation and clone functions.
// @category CFPay

import ghidra.app.script.GhidraScript;
import ghidra.app.util.PseudoDisassembler;
import ghidra.app.util.PseudoInstruction;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;

import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class FindWechatPaymentModelCallers extends GhidraScript {
    private static final int CHUNK_SIZE = 1024 * 1024;
    private static final List<String> TARGETS = List.of(
        "185297290",
        "185297630",
        "18529e3b0",
        "18529eb00",
        "1852a2800",
        "1852a2ba0"
    );

    @Override
    protected void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();
        ReferenceManager referenceManager = currentProgram.getReferenceManager();
        PseudoDisassembler disassembler = new PseudoDisassembler(currentProgram);
        Map<Long, Address> targets = new HashMap<>();
        Set<String> directBranches = new HashSet<>();

        for (String value : TARGETS) {
            Address target = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
            targets.put(target.getOffset(), target);
            int references = 0;
            ReferenceIterator iterator = referenceManager.getReferencesTo(target);
            while (iterator.hasNext()) {
                Reference reference = iterator.next();
                Function caller = listing.getFunctionContaining(reference.getFromAddress());
                MemoryBlock block = memory.getBlock(reference.getFromAddress());
                println(
                    "KNOWN_REF|target=" + target+
                    "|from=" + reference.getFromAddress()+
                    "|type=" + reference.getReferenceType()+
                    "|block=" + (block == null ? "<none>" : block.getName())+
                    "|caller=" + (caller == null ? "<none>" : caller.getName(true))+
                    "|caller_entry=" + (caller == null ? "<none>" : caller.getEntryPoint())
                );
                references += 1;
            }
            println("KNOWN_SUMMARY|target=" + target + "|references=" + references);
        }

        for (MemoryBlock block : memory.getBlocks()) {
            if (!block.isInitialized() || !block.isExecute() || block.getSize() < 5) continue;
            long positionsRemaining = block.getSize() - 4;
            for (long chunkOffset = 0; chunkOffset < positionsRemaining && !monitor.isCancelled(); chunkOffset += CHUNK_SIZE) {
                int positionCount = (int) Math.min(CHUNK_SIZE, positionsRemaining - chunkOffset);
                byte[] bytes = new byte[positionCount + 4];
                Address chunkStart = block.getStart().add(chunkOffset);
                int bytesRead = memory.getBytes(chunkStart, bytes);
                int availablePositions = Math.min(positionCount, bytesRead - 4);

                for (int index = 0; index < availablePositions; index++) {
                    int opcode = bytes[index] & 0xff;
                    if (opcode != 0xe8 && opcode != 0xe9) continue;
                    int displacement =
                        (bytes[index + 1] & 0xff) |
                        ((bytes[index + 2] & 0xff) << 8) |
                        ((bytes[index + 3] & 0xff) << 16) |
                        (bytes[index + 4] << 24);
                    Address instructionAddress = chunkStart.add(index);
                    Address target = targets.get(instructionAddress.getOffset() + 5L + displacement);
                    if (target == null) continue;

                    try {
                        PseudoInstruction instruction = disassembler.disassemble(instructionAddress);
                        if (instruction == null || instruction.getLength() != 5) continue;
                        String mnemonic = instruction.getMnemonicString();
                        if (!("CALL".equals(mnemonic) || "JMP".equals(mnemonic))) continue;
                        if (!instruction.toString().contains(target.toString())) continue;

                        String key = instructionAddress + "->" + target;
                        if (!directBranches.add(key)) continue;
                        Function caller = listing.getFunctionContaining(instructionAddress);
                        println(
                            "DIRECT_BRANCH|target=" + target+
                            "|instruction=" + instructionAddress+
                            "|assembly=" + clean(instruction.toString())+
                            "|caller=" + (caller == null ? "<none>" : caller.getName(true))+
                            "|caller_entry=" + (caller == null ? "<none>" : caller.getEntryPoint())
                        );
                    } catch (Exception ignored) {
                        // Executable sections can contain embedded data that resembles a branch opcode.
                    }
                }
            }
        }

        println(
            "SUMMARY|targets=" + targets.size()+
            "|direct_branches=" + directBranches.size()+
            "|cancelled=" + monitor.isCancelled()
        );
    }

    private String clean(String value) {
        return value == null ? "" : value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }
}
