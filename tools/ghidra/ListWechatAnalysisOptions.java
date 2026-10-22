// Lists available Ghidra analysis options for the imported Weixin module.
// @category CFPay

import ghidra.app.script.GhidraScript;

import java.util.Arrays;
import java.util.Map;

public class ListWechatAnalysisOptions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        Map<String, String> options = getCurrentAnalysisOptionsAndValues(currentProgram);
        String[] names = options.keySet().toArray(new String[0]);
        Arrays.sort(names);
        for (String name : names) {
            println("OPTION|name=" + clean(name) + "|value=" + clean(options.get(name)));
        }
        println("SUMMARY|options=" + names.length);
    }

    private String clean(String value) {
        return value == null ? "" : value.replace("\r", " ").replace("\n", " ").replace("|", "/");
    }
}
