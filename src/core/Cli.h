#pragma once

// Entry point for all CLI subcommands.  Returns an exit code, or -1 if
// no subcommand was recognised (caller should launch the GUI instead).
int cliDispatch(int argc, char *argv[]);
