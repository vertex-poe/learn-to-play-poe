package steam

import (
	"context"
	"net/http"
	"net/http/httptest"
	"net/url"
	"testing"

	"github.com/MovingCairn/poe-info-service/internal/testfixtures"
)

func newOfficialServer(t *testing.T, status int, body string) *httptest.Server {
	t.Helper()
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(status)
		w.Write([]byte(body))
	}))
	t.Cleanup(srv.Close)
	return srv
}

func TestFetchOfficialNoAPIKeyOrIDsIsANoOp(t *testing.T) {
	c := NewClient(nil, WithOfficialBaseURL("http://unused.invalid"))

	got, err := c.FetchOfficial(context.Background(), "", []string{"76561197960287930"})
	if err != nil {
		t.Fatalf("FetchOfficial with no key: unexpected error: %v", err)
	}
	if len(got) != 0 {
		t.Errorf("FetchOfficial with no key = %v, want empty map (no request should be made)", got)
	}

	got, err = c.FetchOfficial(context.Background(), "key", nil)
	if err != nil {
		t.Fatalf("FetchOfficial with no ids: unexpected error: %v", err)
	}
	if len(got) != 0 {
		t.Errorf("FetchOfficial with no ids = %v, want empty map", got)
	}
}

func TestFetchOfficialInGame(t *testing.T) {
	srv := newOfficialServer(t, http.StatusOK, testfixtures.SteamPlayerSummariesInGame)
	c := NewClient(nil, WithOfficialBaseURL(srv.URL))

	got, err := c.FetchOfficial(context.Background(), "key", []string{"76561197960287930"})
	if err != nil {
		t.Fatalf("FetchOfficial: unexpected error: %v", err)
	}
	want := OfficialResult{PersonaName: "Xylia", GameName: "Path of Exile", GameAppID: "238960", InGame: true}
	if got["76561197960287930"] != want {
		t.Errorf("FetchOfficial result = %+v, want %+v", got["76561197960287930"], want)
	}
}

func TestFetchOfficialNotInGame(t *testing.T) {
	srv := newOfficialServer(t, http.StatusOK, testfixtures.SteamPlayerSummariesNotInGame)
	c := NewClient(nil, WithOfficialBaseURL(srv.URL))

	got, err := c.FetchOfficial(context.Background(), "key", []string{"76561197960287930"})
	if err != nil {
		t.Fatalf("FetchOfficial: unexpected error: %v", err)
	}
	want := OfficialResult{PersonaName: "Xylia"}
	if got["76561197960287930"] != want {
		t.Errorf("FetchOfficial result = %+v, want %+v", got["76561197960287930"], want)
	}
}

func TestFetchOfficialMultipleUnorderedResponse(t *testing.T) {
	srv := newOfficialServer(t, http.StatusOK, testfixtures.SteamPlayerSummariesMultipleUnordered)
	c := NewClient(nil, WithOfficialBaseURL(srv.URL))

	got, err := c.FetchOfficial(context.Background(), "key", []string{"76561197960287930", "76561197960287931"})
	if err != nil {
		t.Fatalf("FetchOfficial: unexpected error: %v", err)
	}
	if len(got) != 2 {
		t.Fatalf("FetchOfficial returned %d results, want 2: %+v", len(got), got)
	}
	if got["76561197960287930"].GameName != "Path of Exile" {
		t.Errorf("result for ...930 = %+v, want GameName=Path of Exile", got["76561197960287930"])
	}
	if got["76561197960287931"].GameName != "Dota 2" {
		t.Errorf("result for ...931 = %+v, want GameName=Dota 2", got["76561197960287931"])
	}
}

func TestFetchOfficialEmptyResponse(t *testing.T) {
	srv := newOfficialServer(t, http.StatusOK, testfixtures.SteamPlayerSummariesEmpty)
	c := NewClient(nil, WithOfficialBaseURL(srv.URL))

	got, err := c.FetchOfficial(context.Background(), "key", []string{"76561197960287930"})
	if err != nil {
		t.Fatalf("FetchOfficial: unexpected error: %v", err)
	}
	if len(got) != 0 {
		t.Errorf("FetchOfficial of an unrecognized id = %v, want empty map", got)
	}
}

func TestFetchOfficialBadKeyStatus(t *testing.T) {
	srv := newOfficialServer(t, http.StatusForbidden, "")
	c := NewClient(nil, WithOfficialBaseURL(srv.URL))

	if _, err := c.FetchOfficial(context.Background(), "bad-key", []string{"76561197960287930"}); err == nil {
		t.Error("FetchOfficial against a 403: want error, got nil")
	}
}

func TestFetchOfficialRequestsExpectedQueryString(t *testing.T) {
	var gotQuery url.Values
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotQuery = r.URL.Query()
		w.Write([]byte(testfixtures.SteamPlayerSummariesEmpty))
	}))
	defer srv.Close()

	c := NewClient(nil, WithOfficialBaseURL(srv.URL))
	if _, err := c.FetchOfficial(context.Background(), "my-key", []string{"1a", "1b"}); err != nil {
		t.Fatalf("FetchOfficial: unexpected error: %v", err)
	}

	if got := gotQuery.Get("key"); got != "my-key" {
		t.Errorf("key param = %q, want %q", got, "my-key")
	}
	if got := gotQuery.Get("steamids"); got != "1a,1b" {
		t.Errorf("steamids param = %q, want %q", got, "1a,1b")
	}
}
