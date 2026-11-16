#!/bin/bash
# TRM lookup helper.
#
# Usage:
#   trm-lookup.sh <pattern>           # grep with page numbers
#   trm-lookup.sh -p <pattern>        # group hits by page
#   trm-lookup.sh -i <pattern>        # case-insensitive
#   trm-lookup.sh --page N            # print which line range corresponds to PDF page N
#   trm-lookup.sh --pages-of N        # alias for --page
#   trm-lookup.sh --line N            # print which PDF page contains text line N
#
# The text file uses form-feed (\f) as page boundary.
# Page 1 = lines before the first \f; page K = lines between (K-1)th and K-th \f.

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
TXT="${TRM_TXT:-$HERE/TRM.txt}"
[[ -f "$TXT" ]] || { echo "TRM text not found: $TXT" >&2; exit 1; }

# Build (or refresh) cached page-boundary index: one line per page, value = line
# number in TXT where the page STARTS.
IDX="$HERE/.TRM.pageidx"
if [[ ! -f "$IDX" || "$TXT" -nt "$IDX" ]]; then
  echo "(rebuilding page index)" >&2
  awk 'BEGIN{print 1}
       /\f/ { print NR+1 }' "$TXT" > "$IDX"
fi

line_to_page() {
  local line="$1"
  awk -v target="$line" '
    {
      if ($1 > target) { print NR-1; exit }
    }
    END {
      if (NR > 0 && $1 <= target) print NR
    }' "$IDX"
}

page_to_lines() {
  local page="$1"
  local start end
  start=$(awk -v p="$page" 'NR==p {print; exit}' "$IDX")
  end=$(awk -v p="$page" 'NR==p+1 {print $1-1; exit}' "$IDX")
  [[ -z "$end" ]] && end=$(wc -l < "$TXT")
  echo "$start $end"
}

case "${1:-}" in
  --line)
    line_to_page "$2"
    ;;
  --page|--pages-of)
    read -r start end < <(page_to_lines "$2")
    echo "page $2: text lines $start-$end"
    ;;
  -p)
    shift
    pat="$1"
    grep -n "$pat" "$TXT" | while IFS=: read -r ln rest; do
      pg=$(line_to_page "$ln")
      printf "p%-5s l%-7s  %s\n" "$pg" "$ln" "$rest"
    done | awk '!seen[$1]++ {curpg=$1; print; next} {print}'
    ;;
  -i)
    shift
    pat="$1"
    grep -in "$pat" "$TXT" | while IFS=: read -r ln rest; do
      pg=$(line_to_page "$ln")
      printf "p%-5s l%-7s  %s\n" "$pg" "$ln" "$rest"
    done
    ;;
  "")
    sed -n '1,/^$/p' "$0" | sed -n 's/^# *//p'
    ;;
  *)
    pat="$1"
    grep -n "$pat" "$TXT" | while IFS=: read -r ln rest; do
      pg=$(line_to_page "$ln")
      printf "p%-5s l%-7s  %s\n" "$pg" "$ln" "$rest"
    done
    ;;
esac
