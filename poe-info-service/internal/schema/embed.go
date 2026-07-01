// Package schema owns the l2p SQLite schema and seed data: the DDL, the
// reference-data seed files, and the version-gated migration steps needed to
// bring an existing database up to date. poe-info-service is the sole writer
// of the l2p database, so it is also the sole owner of its schema.
package schema

import (
	"embed"
	"sort"
	"strings"
)

//go:embed sql/schema.sql
var schemaSQL string

//go:embed sql/seed_base.sql
var seedBaseSQL string

//go:embed sql/areas/*.sql
var areaSeedFiles embed.FS

// combinedSeedSQL concatenates seed_base.sql with every data/areas/*.sql file,
// sorted by filename, mirroring dev/build/combine_seed.py's ordering.
func combinedSeedSQL() (string, error) {
	entries, err := areaSeedFiles.ReadDir("sql/areas")
	if err != nil {
		return "", err
	}
	names := make([]string, 0, len(entries))
	for _, e := range entries {
		names = append(names, e.Name())
	}
	sort.Strings(names)

	parts := []string{strings.TrimRight(seedBaseSQL, "\n")}
	for _, name := range names {
		data, err := areaSeedFiles.ReadFile("sql/areas/" + name)
		if err != nil {
			return "", err
		}
		parts = append(parts, strings.TrimRight(string(data), "\n"))
	}
	return strings.Join(parts, "\n\n") + "\n", nil
}
