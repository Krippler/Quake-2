# Releases and images

## Tags

| | |
| --- | --- |
| `ghcr.io/krippler/quake-2:latest` | the newest release |
| `ghcr.io/krippler/quake-2:edge` | tracks `master` |
| `ghcr.io/krippler/quake-2:1.2.3` | a specific release |

`linux/amd64` only. The engine is portable C; it would build for arm64, but
nothing here has run on one, and the game library is named for x86-64.

## Cutting a release

The workflow in `.github/workflows/docker-publish.yml` reads the top section
of [CHANGELOG.md](CHANGELOG.md):

- `## [Unreleased]` at the top: a merge to `master` publishes `edge` only.
- `## [X.Y.Z] — DATE` at the top, with no `vX.Y.Z` tag yet: a merge publishes
  `X.Y.Z`, `X.Y`, `X`, `latest` and `edge`, signs them with cosign, tags the
  commit `vX.Y.Z`, and creates the GitHub Release from that section with the
  Linux desktop tarball attached.

Pushing a `vX.Y.Z` tag by hand does the same.

The version is baked into the image as `QUAKE2_VERSION`, printed at startup
and stamped into `play.html`. That matters more than it looks: everything
that decides whether sound arrives happens in the page, so a browser quietly
running a cached client from two releases ago looks identical from the
container's side. The start screen shows the client's stamp; the log shows
the container's.

## Unraid

[`templates/unraid.xml`](templates/unraid.xml) is the Community Applications
template. `PUID`/`PGID` default to 99:100 there, which is what the appdata
share is owned by.

The one thing to get right in the template is the game data path: there is
nothing bundled, so a container with nothing mounted stops and says so.
