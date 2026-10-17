#!/bin/sh
# Publishes the packages as a GitHub release, from the release workflow
# (.github/workflows/release.yml), which gives GITHUB_TOKEN and the rest.
#     sh tools/publish_release.sh FILE...
# A push to main replaces the "latest" release, a pushed tag (v1.0) gets its own.
set -e
API=https://api.github.com/repos/$GITHUB_REPOSITORY
api() { curl -sS -f -H "Authorization: Bearer $GITHUB_TOKEN" -H "Accept: application/vnd.github+json" "$@"; }

if [ "$GITHUB_REF_TYPE" = tag ]; then
    TAG=$GITHUB_REF_NAME
    TITLE=$TAG
else
    TAG=latest
    TITLE="Latest build ($(git rev-parse --short HEAD))"
fi

# the release this tag had goes first, and "latest" moves to the new commit
OLD=$(api "$API/releases/tags/$TAG" 2>/dev/null | jq -r .id || true)
if [ -n "$OLD" ] && [ "$OLD" != null ]; then api -X DELETE "$API/releases/$OLD"; fi
if [ "$TAG" = latest ]; then api -X DELETE "$API/git/refs/tags/latest" 2>/dev/null || true; fi

BODY="$(git log -1 --format=%s)

Bring your own disc: nothing of the game's files is in here. Start Play, then \"Extract disc...\"."
case " $* " in *mods.zip*) BODY="$BODY

Janus-Project-mods.zip is the mod levels: unzip it into the game's folder, next to Play." ;; esac
UPLOAD=$(jq -n --arg t "$TAG" --arg c "$GITHUB_SHA" --arg n "$TITLE" --arg b "$BODY" \
            '{tag_name: $t, target_commitish: $c, name: $n, body: $b}' |
         api -X POST "$API/releases" -d @- | jq -r .upload_url | sed 's/{.*//')

for f in "$@"; do
    api -X POST -H "Content-Type: application/zip" --data-binary "@$f" "$UPLOAD?name=$(basename "$f")" > /dev/null
    echo "uploaded $(basename "$f")"
done
