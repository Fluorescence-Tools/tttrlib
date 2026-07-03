// ImageJ/Fiji macro: time the tttrlib CLSM reader on a file. Install the plugin
// in Fiji, edit FILE below, then run this macro (Plugins > Macros > Run...).
// The elapsed time is printed to the Log window.
//
// To compare against another installed reader, duplicate the timed block below
// and call that reader's command in place of "Open TTTR CLSM Image".
//
// For an unattended run, time the reader head-less from Java
// (see test/java/Benchmark.java).

FILE = "/path/to/image.ptu";

run("Close All");

t0 = getTime();
run("Open TTTR CLSM Image", "open=[" + FILE + "]");
t1 = getTime();
print("tttrlib CLSM reader: " + (t1 - t0) + " ms");
