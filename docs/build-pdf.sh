#!/usr/bin/env bash
# Renders docs/*.html reports to PDF, next to each source file.
#
# Prefers a locally installed browser/PDF tool (no network, no Docker).
# Falls back to a Docker-based headless Chrome (chromedp/headless-shell)
# driven over the DevTools protocol via _cdp_print.py, for machines with
# nothing installed but Docker. That image is NOT pulled automatically if
# missing -- this script expects it to already be present locally (pull it
# once yourself with `docker pull chromedp/headless-shell` when you have
# the bandwidth for it, or pass --allow-pull to let this script do it).
#
# Usage:
#   ./docs/build-pdf.sh docs/reports/2026-10-06-foo.html
#   ./docs/build-pdf.sh --all                 # every report + the index
#   ./docs/build-pdf.sh --allow-pull --all    # ...and pull the image if needed
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ALLOW_PULL=0
FILES=()
for arg in "$@"; do
    case "$arg" in
        --allow-pull) ALLOW_PULL=1 ;;
        --all)
            while IFS= read -r f; do FILES+=("$f"); done < <(
                find "$SCRIPT_DIR" -maxdepth 2 -name '*.html' ! -name 'template.html' | sort
            )
            ;;
        *) FILES+=("$arg") ;;
    esac
done

if [ "${#FILES[@]}" -eq 0 ]; then
    echo "Usage: $0 <report.html> [more.html ...] | --all [--allow-pull]" >&2
    exit 1
fi

render_local() {
    local html="$1" pdf="$2"
    if command -v chromium >/dev/null 2>&1; then
        chromium --headless --disable-gpu --no-sandbox --print-to-pdf="$pdf" --no-pdf-header-footer "file://$html" >/dev/null 2>&1 && return 0
    fi
    if command -v chromium-browser >/dev/null 2>&1; then
        chromium-browser --headless --disable-gpu --no-sandbox --print-to-pdf="$pdf" --no-pdf-header-footer "file://$html" >/dev/null 2>&1 && return 0
    fi
    if command -v google-chrome >/dev/null 2>&1; then
        google-chrome --headless --disable-gpu --no-sandbox --print-to-pdf="$pdf" --no-pdf-header-footer "file://$html" >/dev/null 2>&1 && return 0
    fi
    if command -v wkhtmltopdf >/dev/null 2>&1; then
        wkhtmltopdf "$html" "$pdf" >/dev/null 2>&1 && return 0
    fi
    return 1
}

HAVE_LOCAL_TOOL=0
for t in chromium chromium-browser google-chrome wkhtmltopdf; do
    command -v "$t" >/dev/null 2>&1 && HAVE_LOCAL_TOOL=1
done

if [ "$HAVE_LOCAL_TOOL" -eq 1 ]; then
    for html in "${FILES[@]}"; do
        pdf="${html%.html}.pdf"
        if render_local "$html" "$pdf"; then
            echo "wrote $pdf"
        else
            echo "failed to render $html" >&2
            exit 1
        fi
    done
    exit 0
fi

# --- Docker fallback, via the Chrome DevTools Protocol ---

if ! command -v docker >/dev/null 2>&1; then
    echo "No local PDF tool (chromium/wkhtmltopdf) and no Docker found. Install one of those, or Docker, to render PDFs." >&2
    exit 1
fi

IMAGE="chromedp/headless-shell"
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    if [ "$ALLOW_PULL" -eq 1 ]; then
        docker pull "$IMAGE"
    else
        echo "The '$IMAGE' image isn't pulled locally yet, and this script won't" >&2
        echo "pull it without --allow-pull (it's a few hundred MB). Either:" >&2
        echo "  - run 'docker pull $IMAGE' yourself when you have the bandwidth, or" >&2
        echo "  - re-run this script with --allow-pull." >&2
        exit 1
    fi
fi

CONTAINER_ID=""
cleanup() {
    [ -n "$CONTAINER_ID" ] && docker rm -f "$CONTAINER_ID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

CONTAINER_ID=$(docker run -d --rm -p 127.0.0.1:9222:9222 -v "$SCRIPT_DIR":/docs "$IMAGE")

ready=0
for _ in $(seq 1 30); do
    if curl -fsS "http://127.0.0.1:9222/json/version" >/dev/null 2>&1; then
        ready=1
        break
    fi
    sleep 0.3
done
[ "$ready" -eq 1 ] || { echo "headless Chrome did not come up in time" >&2; exit 1; }

for html in "${FILES[@]}"; do
    pdf="${html%.html}.pdf"
    rel="/docs/$(realpath --relative-to="$SCRIPT_DIR" "$html")"
    python3 "$SCRIPT_DIR/_cdp_print.py" "http://127.0.0.1:9222" "file://$rel" "$pdf"
done
