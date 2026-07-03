package main

import "testing"

func TestCliDispatchFallsThroughForServiceFlags(t *testing.T) {
	if _, handled := cliDispatch([]string{"--port", "47652"}); handled {
		t.Error("expected normal service flags not to be treated as a subcommand")
	}
	if _, handled := cliDispatch(nil); handled {
		t.Error("expected no args not to be treated as a subcommand")
	}
}

func TestCliDispatchRejectsUnknownDialogNoun(t *testing.T) {
	code, handled := cliDispatch([]string{"dialog", "hash"})
	if !handled {
		t.Fatal("expected 'dialog' to be recognised as a subcommand")
	}
	if code == 0 {
		t.Error("expected a nonzero exit code for an unsupported dialog noun (hash stays on the C++ side)")
	}
}
