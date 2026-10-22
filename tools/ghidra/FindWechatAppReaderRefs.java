// Finds AppReader UI strings and their direct RIP-relative code references.
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

import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class FindWechatAppReaderRefs extends GhidraScript {
    private static final int CHUNK_SIZE = 1024 * 1024;
    private static final int MAX_TRAILING_BYTES = 4;
    private static final int MAX_BYTES_BEFORE_DISPLACEMENT = 11;
    private static final List<String> TERMS = List.of(
        "mmui::ChatAppReaderItemView",
        "mmui::AppReaderCateItemView",
        "mmui::AppReaderTemplateView",
        "AppReaderMouseClickInfo",
        "style::ChatAppReaderViewStyle",
        "AppReaderCateView",
        "AppReaderCateCoverItemView",
        "AppReaderCateSecondaryItemView"
    );

    @Override
    protected void run() throws Exception {
        Memory memory = currentProgram.getMemory();
        Listing listing = currentProgram.getListing();
        ReferenceManager referenceManager = currentProgram.getReferenceManager();
        PseudoDisassembler disassembler = new PseudoDisassembler(currentProgram);
        Map<Long, Match> matches = new HashMap<>();
        Set<String> reported = new HashSet<>();

        for (String term : TERMS) {
            byte[] pattern = (term + "\0").getBytes(StandardCharsets.US_ASCII);
            Address cursor = currentProgram.getMinAddress();
            int termMatches = 0;
            while (cursor != null && !monitor.isCancelled()) {
                Address found = memory.findBytes(cursor, pattern, null, true, monitor);
                if (found == null) break;
                matches.put(found.getOffset(), new Match(term, found));
                MemoryBlock block = memory.getBlock(found);
                println("MATCH|term=" + term + "|address=" + found + "|block=" +
                    (block == null ? "<none>" : block.getName()));

                ReferenceIterator iterator = referenceManager.getReferencesTo(found);
                while (iterator.hasNext()) {
                    Reference reference = iterator.next();
                    Function caller = listing.getFunctionContaining(reference.getFromAddress());
                    println("KNOWN_REF|term=" + term + "|target=" + found + "|from=" +
                        reference.getFromAddress() + "|type=" + reference.getReferenceType() + "|caller=" +
                        (caller == null ? "<none>" : caller.getName(true)) + "|caller_entry=" +
                        (caller == null ? "<none>" : caller.getEntryPoint()));
                }
                termMatches += 1;
                cursor = found.add(1);
            }
            println("TERM_SUMMARY|term=" + term + "|matches=" + termMatches);
        }

        for (MemoryBlock block : memory.getBlocks()) {
            if (!block.isInitialized() || !block.isExecute() || block.getSize() < 4) continue;
            long positionsRemaining = block.getSize() - 3;
            for (long chunkOffset = 0; chunkOffset < positionsRemaining && !monitor.isCancelled(); chunkOffset += CHUNK_SIZE) {
                int positionCount = (int) Math.min(CHUNK_SIZE, positionsRemaining - chunkOffset);
                byte[] bytes = new byte[positionCount + 3];
                Address chunkStart = block.getStart().add(chunkOffset);
                int bytesRead = memory.getBytes(chunkStart, bytes);
                int availablePositions = Math.min(positionCount, bytesRead - 3);

                for (int index = 0; index < availablePositions; index++) {
                    int displacement = (bytes[index] & 0xff) | ((bytes[index + 1] & 0xff) << 8) |
                        ((bytes[index + 2] & 0xff) << 16) | (bytes[index + 3] << 24);
                    long displacementAddress = chunkStart.getOffset() + index;
                    long baseTarget = displacementAddress + 4L + displacement;
                    for (int trailingBytes = 0; trailingBytes <= MAX_TRAILING_BYTES; trailingBytes++) {
                        Match match = matches.get(baseTarget + trailingBytes);
                        if (match != null) decodeCandidate(
                            disassembler, listing, block, displacementAddress, trailingBytes, match, reported);
                    }
                }
            }
        }

        println("SUMMARY|terms=" + TERMS.size() + "|matches=" + matches.size() + "|rip_refs=" +
            reported.size() + "|cancelled=" + monitor.isCancelled());
    }

    private void decodeCandidate(PseudoDisassembler disassembler, Listing listing, MemoryBlock block,
        long displacementAddress, int trailingBytes, Match match, Set<String> reported) {
        Address displacement = block.getStart().getAddressSpace().getAddress(displacementAddress);
        Address expectedEnd = displacement.add(3L + trailingBytes);
        PseudoInstruction bestInstruction = null;
        for (int bytesBefore = 0; bytesBefore <= MAX_BYTES_BEFORE_DISPLACEMENT; bytesBefore++) {
            Address start;
            try {
                start = displacement.subtract(bytesBefore);
            } catch (Exception exception) {
                continue;
            }
            if (!block.contains(start)) continue;
            try {
                PseudoInstruction instruction = disassembler.disassemble(start);
                if (instruction == null || !instruction.getMaxAddress().equals(expectedEnd) ||
                    !instruction.toString().contains(match.address.toString())) continue;
                if (bestInstruction == null || instruction.getLength() > bestInstruction.getLength()) {
                    bestInstruction = instruction;
                }
            } catch (Exception ignored) {
                // Candidate starts are intentionally speculative.
            }
        }
        if (bestInstruction == null) return;
        Address start = bestInstruction.getMinAddress();
        String key = start + "->" + match.address;
        if (!reported.add(key)) return;
        Function caller = listing.getFunctionContaining(start);
        println("RIP_REF|term=" + match.term + "|target=" + match.address + "|instruction=" + start +
            "|assembly=" + clean(bestInstruction.toString()) + "|caller=" +
            (caller == null ? "<none>" : caller.getName(true)) + "|caller_entry=" +
            (caller == null ? "<none>" : caller.getEntryPoint()));
    }

    private String clean(String value) {
        return value == null ? "" : value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }

    private static class Match {
        private final String term;
        private final Address address;

        private Match(String term, Address address) {
            this.term = term;
            this.address = address;
        }
    }
}
