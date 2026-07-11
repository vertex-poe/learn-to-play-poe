package testfixtures

// Sample HTML fragments shaped like https://steamcommunity.com/miniprofile/<id3>,
// covering the cases internal/steam's richpresence.go parses for: a game
// with rich presence text set, a game with none, no game at all, and a page
// whose game name disagrees with what the official API already reported
// (the mismatch-guard case).

const SteamMiniprofileWithRichPresence = `<div class="miniprofile">
	<div class="playerAvatar"><img src="https://avatars.example/avatar.jpg"></div>
	<div class="miniprofile_game_name_ctn">
		<span class="miniprofile_game_name">Path of Exile</span>
	</div>
	<div class="rich_presence_ctn">
		<span class="rich_presence">Standard League - LvL 92 Witch, Act 10</span>
	</div>
</div>`

const SteamMiniprofileGameNoRichPresence = `<div class="miniprofile">
	<div class="miniprofile_game_name_ctn">
		<span class="miniprofile_game_name">Satisfactory</span>
	</div>
</div>`

const SteamMiniprofileNoGame = `<div class="miniprofile">
	<div class="playerAvatar"><img src="https://avatars.example/avatar.jpg"></div>
</div>`

// SteamMiniprofileMismatchedGame has a rich_presence span, but its
// miniprofile_game_name disagrees with what a caller would pass in as
// knownGameName (e.g. "Path of Exile" from the official API) — the
// mismatch guard must suppress the rich presence text in this case.
const SteamMiniprofileMismatchedGame = `<div class="miniprofile">
	<div class="miniprofile_game_name_ctn">
		<span class="miniprofile_game_name">Some Other Game</span>
	</div>
	<div class="rich_presence_ctn">
		<span class="rich_presence">Stale rich presence text</span>
	</div>
</div>`
